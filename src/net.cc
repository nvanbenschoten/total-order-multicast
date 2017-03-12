#include "net.h"

namespace net {

std::string GetHostname() {
  char hostname[HOST_NAME_MAX];
  gethostname(hostname, HOST_NAME_MAX);
  return std::string(hostname);
}

std::ostream& operator<<(std::ostream& os, const Address& addr) {
  os << addr.hostname_ << ':' << addr.port_;
  return os;
}

Address AddressWithDefaultPort(
    std::string addr, std::experimental::optional<unsigned short> port) {
  size_t found = addr.find(":");
  if (found != std::string::npos) {
    port = std::stoi(addr.substr(found + 1));
    addr = addr.substr(0, found);
  } else if (!port) {
    throw std::invalid_argument(
        "port not specified in address and no default port provided");
  }
  return Address(addr, *port);
}

}  // namespace net
