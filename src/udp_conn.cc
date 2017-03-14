#include "udp_conn.h"

namespace udp {

// Creates a UDP socket or throws an exception on error.
Socket CreateSocket(const std::chrono::microseconds timeout) {
  // Create the socket.
  Socket sockfd = socket(AF_INET, SOCK_DGRAM, 0);
  if (sockfd < 0) {
    throw net::SocketException();
  }

  // Resuse the port immediately after the socket is killed.
  int optval = 1;
  if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (const void *)&optval,
                 sizeof(int))) {
    throw net::SocketException();
  }

  // Set socket timeout if provided.
  if (timeout.count() > 0) {
    // Truncate to integer number of seconds.
    const auto secs = std::chrono::duration_cast<std::chrono::seconds>(timeout);

    struct timeval timeval;
    timeval.tv_sec = secs.count();
    timeval.tv_usec = (timeout - secs).count();
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeval,
                   sizeof(timeval))) {
      throw net::SocketException();
    }
  }

  return sockfd;
}

inline bool IsErrnoTimeout() {
  return errno == EAGAIN || errno == EWOULDBLOCK || errno == ECONNREFUSED;
}

SocketAddress::SocketAddress(net::Address addr) {
  // Get the remote server's DNS entry.
  struct hostent *server = gethostbyname(addr.hostname().c_str());
  if (server == nullptr) {
    throw net::HostNotFoundException(addr.hostname());
  }

  // Build the server's Internet address.
  addr_.sin_family = AF_INET;
  bcopy((char *)server->h_addr, (char *)&addr_.sin_addr.s_addr,
        server->h_length);
  addr_.sin_port = htons(addr.port());
}

std::string SocketAddress::Hostname() const {
  struct hostent *hostp = gethostbyaddr((const char *)&addr_.sin_addr.s_addr,
                                        sizeof(addr_.sin_addr.s_addr), AF_INET);
  if (hostp == nullptr) {
    throw net::HostNotFoundException("");
  }

  std::string hostname(hostp->h_name);
  if (hostname == "localhost") {
    hostname = net::GetHostname();
  }
  return hostname;
}

unsigned short SocketAddress::Port() const { return ntohs(addr_.sin_port); }

void Client::Send(const char *buf, size_t size) {
  std::lock_guard<std::recursive_mutex> guard(sockfd_mutex_);
  auto addr = remote_address_.addr();
  auto addrlen = remote_address_.addr_len();
  if (sendto(sockfd_, buf, size, 0, addr, addrlen) < 0) {
    throw net::SendException();
  }
}

void Client::SendWithAck(const char *buf, size_t size, unsigned int attempts,
                         OnReceiveFn validAck) {
  std::lock_guard<std::recursive_mutex> guard(sockfd_mutex_);
  int backoff_mult = 1;
  bool noLimit = attempts == kUnlimitedAttempts;
  for (; noLimit || attempts > 0; --attempts) {
    // Send the message to the client.
    Send(buf, size);

    // Create a zeroed out buffer to read the ack message into.
    char ackbuf[BUFSIZE];
    bzero(ackbuf, BUFSIZE);

    // receive from the socket.
    struct sockaddr_in clientaddr;
    socklen_t clientlen = sizeof(clientaddr);
    int n = recvfrom(sockfd_, ackbuf, BUFSIZE, 0,
                     (struct sockaddr *)&clientaddr, &clientlen);

    // Check for error cases. This is either a timeout or some kind of
    // networking error. For timeouts, try sending the message again. For
    // anything else, throw an exception.
    if (n < 0) {
      if (IsErrnoTimeout()) {
        // Wait for a backoff period.
        WaitBackoff(backoff_mult);
        backoff_mult <<= 1;
        continue;
      } else {
        throw net::ReceiveException();
      }
    }

    // Make sure the ack was valid.
    auto action = validAck(shared_from_this(), ackbuf, n);
    if (action == ServerAction::Stop) {
      return;
    }
  }
}

void Client::WaitBackoff(unsigned int multiplier) const {
  static thread_local std::default_random_engine random_engine(
      std::chrono::system_clock::now().time_since_epoch().count());

  // Wait on the order of 10 ms * multiplier.
  auto delay_base = std::chrono::milliseconds{10}.count();
  std::poisson_distribution<int> poisson(delay_base);
  int delay = poisson(random_engine);
  if (delay <= 0) {
    return;
  }
  delay *= multiplier;
  std::this_thread::sleep_for(std::chrono::milliseconds{delay});
  return;
}

Server::Server(unsigned short port, std::chrono::microseconds timeout)
    : sockfd_(CreateSocket(timeout)) {
  // Create a socket and associate the it with the port
  struct sockaddr_in server_address = {};
  server_address.sin_family = AF_INET;
  server_address.sin_addr.s_addr = htonl(INADDR_ANY);
  server_address.sin_port = htons(port);

  if (bind(sockfd_, (struct sockaddr *)&server_address,
           sizeof(server_address)) < 0) {
    throw net::BindException();
  }
};

void Server::Listen(OnReceiveFn rcv, OnTimeout timeout) const {
  // While the server is running, wait for datagrams and
  // call the provided closure with their data.
  while (1) {
    // Create a zeroed out buffer to read the message into.
    char buf[BUFSIZE];
    bzero(buf, BUFSIZE);

    // Receive from the socket.
    struct sockaddr_in clientaddr;
    socklen_t clientlen = sizeof(clientaddr);
    int n = recvfrom(sockfd_, buf, BUFSIZE, 0, (struct sockaddr *)&clientaddr,
                     &clientlen);
    if (n < 0) {
      if (IsErrnoTimeout() && timeout != nullptr) {
        auto action = timeout();
        switch (action) {
          case ServerAction::Continue:
            continue;
          case ServerAction::Stop:
            return;
          default:
            throw std::invalid_argument("unexpected ServerAction value");
        }
      } else {
        throw net::ReceiveException();
      }
    }

    // Call closure with new client.
    auto client = std::make_shared<udp::Client>(clientaddr);

    // Call the receive callback with the data received.
    auto action = rcv(client, buf, n);
    if (action == ServerAction::Stop) {
      return;
    }
  }
}

}  // namespace udp
