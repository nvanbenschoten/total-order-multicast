#include "process.h"

namespace process {

UdpClientMap ClientsForProcessList(const ProcessList& processes) {
  UdpClientMap clients(processes.size());
  for (auto const& addr : processes) {
    clients.emplace(addr, std::make_shared<udp::Client>(addr, kAckTimeout));
  }
  return clients;
}

uint32_t RandomUint32() {
  // Static thread local to avoid expensive initialization cost on every call,
  // while maintaining thread safety.
  static thread_local std::random_device r;
  static thread_local std::seed_seq seed{r(), r(), r(), r(), r()};
  static thread_local std::default_random_engine random_engine(seed);

  std::uniform_int_distribution<uint32_t> distribution(0, UINT32_MAX);
  return distribution(random_engine);
}

std::experimental::optional<uint32_t> MessageTypeFromBuf(char* buf, size_t n) {
  // Check to make sure the size of the buffer is at least the size of the type
  // field.
  if (n < sizeof(uint32_t)) {
    return {};
  }

  // Read the first 32-bits and convert to host endianness.
  uint32_t* buf_type = reinterpret_cast<uint32_t*>(buf);
  return ntohl(*buf_type);
}

std::experimental::optional<msg::DataMessage> DataMsgFromBuf(char* buf,
                                                             size_t n) {
  // Check to make sure the size of the buffer is correct.
  if (n != sizeof(msg::DataMessage)) {
    return {};
  }

  msg::DataMessage* buf_msg = reinterpret_cast<msg::DataMessage*>(buf);
  msg::DataMessage msg;
  msg.type = ntohl(buf_msg->type);
  msg.sender = ntohl(buf_msg->sender);
  msg.msg_id = ntohl(buf_msg->msg_id);
  msg.data = ntohl(buf_msg->data);
  return msg;
}

std::experimental::optional<msg::AckMessage> AckMsgFromBuf(char* buf,
                                                           size_t n) {
  // Check to make sure the size of the buffer is correct.
  if (n != sizeof(msg::AckMessage)) {
    return {};
  }

  msg::AckMessage* buf_msg = reinterpret_cast<msg::AckMessage*>(buf);
  msg::AckMessage msg;
  msg.type = ntohl(buf_msg->type);
  msg.sender = ntohl(buf_msg->sender);
  msg.msg_id = ntohl(buf_msg->msg_id);
  msg.proposed_seq = ntohl(buf_msg->proposed_seq);
  msg.proposer = ntohl(buf_msg->proposer);
  return msg;
}

std::experimental::optional<msg::SeqMessage> SeqMsgFromBuf(char* buf,
                                                           size_t n) {
  // Check to make sure the size of the buffer is correct.
  if (n != sizeof(msg::SeqMessage)) {
    return {};
  }

  msg::SeqMessage* buf_msg = reinterpret_cast<msg::SeqMessage*>(buf);
  msg::SeqMessage msg;
  msg.type = ntohl(buf_msg->type);
  msg.sender = ntohl(buf_msg->sender);
  msg.msg_id = ntohl(buf_msg->msg_id);
  msg.final_seq = ntohl(buf_msg->final_seq);
  msg.final_seq_proposer = ntohl(buf_msg->final_seq_proposer);
  return msg;
}

std::experimental::optional<msg::SeqAckMessage> SeqAckMsgFromBuf(char* buf,
                                                                 size_t n) {
  // Check to make sure the size of the buffer is correct.
  if (n != sizeof(msg::SeqAckMessage)) {
    return {};
  }

  msg::SeqAckMessage* buf_msg = reinterpret_cast<msg::SeqAckMessage*>(buf);
  msg::SeqAckMessage msg;
  msg.type = ntohl(buf_msg->type);
  msg.sender = ntohl(buf_msg->sender);
  msg.msg_id = ntohl(buf_msg->msg_id);
  return msg;
}

void SendDataMsg(const udp::ClientPtr client, const udp::OnReceiveFn handle_ack,
                 msg::DataMessage data_msg) {
  data_msg.type = htonl(msg::kDataMessageType);
  data_msg.sender = htonl(data_msg.sender);
  data_msg.msg_id = htonl(data_msg.msg_id);
  data_msg.data = htonl(data_msg.data);

  char* buf = reinterpret_cast<char*>(&data_msg);
  client->SendWithAck(buf, sizeof(data_msg), udp::kUnlimitedAttempts,
                      handle_ack);
}

void SendAckMsg(const udp::ClientPtr client, msg::AckMessage ack_msg) {
  ack_msg.type = htonl(msg::kAckMessageType);
  ack_msg.sender = htonl(ack_msg.sender);
  ack_msg.msg_id = htonl(ack_msg.msg_id);
  ack_msg.proposed_seq = htonl(ack_msg.proposed_seq);
  ack_msg.proposer = htonl(ack_msg.proposer);

  char* buf = reinterpret_cast<char*>(&ack_msg);
  client->Send(buf, sizeof(ack_msg));
}

void SendSeqMsg(const udp::ClientPtr client, msg::SeqMessage seq_msg) {
  msg::SeqMessage orig_seq_msg = seq_msg;

  seq_msg.type = htonl(msg::kSeqMessageType);
  seq_msg.sender = htonl(seq_msg.sender);
  seq_msg.msg_id = htonl(seq_msg.msg_id);
  seq_msg.final_seq = htonl(seq_msg.final_seq);
  seq_msg.final_seq_proposer = htonl(seq_msg.final_seq_proposer);

  // Passed to SendWithAck to verify that any acknowledgement we hear is valid.
  auto isValidAck = [&orig_seq_msg](udp::ClientPtr _, char* buf, size_t n) {
    auto seqack_msg = SeqAckMsgFromBuf(buf, n);
    bool valid = seqack_msg && ValidSeqAckMsg(*seqack_msg, orig_seq_msg);
    if (!valid) return udp::ServerAction::Continue;
    return udp::ServerAction::Stop;
  };

  char* buf = reinterpret_cast<char*>(&seq_msg);
  client->SendWithAck(buf, sizeof(seq_msg), udp::kUnlimitedAttempts,
                      isValidAck);
}

void SendSeqAckMsg(const udp::ClientPtr client, msg::SeqAckMessage seqack_msg) {
  seqack_msg.type = htonl(msg::kSeqAckMessageType);
  seqack_msg.sender = htonl(seqack_msg.sender);
  seqack_msg.msg_id = htonl(seqack_msg.msg_id);

  char* buf = reinterpret_cast<char*>(&seqack_msg);
  client->Send(buf, sizeof(seqack_msg));
}

bool ValidDataMsg(msg::DataMessage& data_msg, size_t process_count) {
  if (data_msg.type != msg::kDataMessageType) {
    return false;
  }
  if (data_msg.sender >= process_count) {
    return false;
  }
  return true;
}

bool ValidAckMsg(msg::AckMessage& ack_msg, uint32_t exp_sender,
                 uint32_t exp_msg_id, uint32_t exp_proposer) {
  if (ack_msg.type != msg::kAckMessageType) {
    return false;
  }
  if (ack_msg.sender != exp_sender) {
    return false;
  }
  if (ack_msg.msg_id != exp_msg_id) {
    return false;
  }
  if (ack_msg.proposer != exp_proposer) {
    return false;
  }
  return true;
}

bool ValidSeqMsg(msg::SeqMessage& seq_msg, size_t process_count) {
  if (seq_msg.type != msg::kSeqMessageType) {
    return false;
  }
  if (seq_msg.sender >= process_count) {
    return false;
  }
  if (seq_msg.final_seq_proposer >= process_count) {
    return false;
  }
  return true;
}

bool ValidSeqAckMsg(msg::SeqAckMessage& seqack_msg, msg::SeqMessage& seq_msg) {
  if (seqack_msg.type != msg::kSeqAckMessageType) {
    return false;
  }
  if (seqack_msg.sender != seq_msg.sender) {
    return false;
  }
  if (seqack_msg.msg_id != seq_msg.msg_id) {
    return false;
  }
  return true;
}

bool operator<(const PendingMessage& lhs, const PendingMessage& rhs) {
  if (lhs.pms.seq != rhs.pms.seq) {
    // 1. Sort by sequence number.
    return lhs.pms.seq < rhs.pms.seq;
  }
  if (lhs.deliverable != rhs.deliverable) {
    // 2. Sort undeliverable messages first.
    return !lhs.deliverable;
  }
  if (lhs.pmk.sender != rhs.pmk.sender) {
    // 3. Sort by sender id.
    return lhs.pmk.sender < rhs.pmk.sender;
  }
  if (lhs.pmk.msg_id != rhs.pmk.msg_id) {
    // 4. Sort by message id.
    return lhs.pmk.msg_id < rhs.pmk.msg_id;
  }
  // (sender, msg_id) should uniquely identify a message, so if we're here,
  // we are comparing the same message.
  return false;
}

void HoldBackQueue::InsertPending(const msg::AckMessage& ack_msg,
                                  uint32_t data) {
  PendingMessageKey pmk;
  pmk.sender = ack_msg.sender;
  pmk.msg_id = ack_msg.msg_id;
  if (pending_seqs_.count(pmk)) {
    logging::out << "Received identical message " << ack_msg << " twice\n";
    return;
  }

  PendingMessageSeq pms;
  pms.seq = ack_msg.proposed_seq;
  pms.seq_proposer = ack_msg.proposer;
  pending_seqs_[pmk] = pms;

  PendingMessage pm;
  pm.pmk = pmk;
  pm.pms = pms;
  pm.data = data;
  pm.deliverable = false;
  if (ordering_.count(pm)) {
    throw std::invalid_argument("existing pending message in HoldBackQueue");
  }
  ordering_.insert(pm);
}

void HoldBackQueue::Deliver(const msg::SeqMessage& seq_msg,
                            const deliverMsgFn deliver) {
  PendingMessageKey pmk;
  pmk.sender = seq_msg.sender;
  pmk.msg_id = seq_msg.msg_id;
  if (!pending_seqs_.count(pmk)) {
    logging::out << "Unknown SeqMessage " << seq_msg << "...\n";
    return;
  }

  PendingMessageSeq old_pms = pending_seqs_.at(pmk);
  pending_seqs_.erase(pmk);

  // Remove old pending message.
  PendingMessage pm;
  pm.pmk = pmk;
  pm.pms = old_pms;
  pm.deliverable = false;
  ordering_.erase(pm);

  // Update with the new information and reinsert.
  PendingMessageSeq pms;
  pms.seq = seq_msg.final_seq;
  pms.seq_proposer = seq_msg.final_seq_proposer;

  pm.pms = pms;
  pm.deliverable = true;
  ordering_.insert(pm);

  // Iterate through map and deliver messages that an be delivered.
  for (auto it = ordering_.begin(); it != ordering_.end() /* not hoisted */;) {
    if (!it->deliverable) {
      break;
    }
    // Deliver and delete for as long as the head of the queue is deliverable.
    msg::SeqMessage to_deliver;
    to_deliver.sender = it->pmk.sender;
    to_deliver.msg_id = it->pmk.msg_id;
    to_deliver.final_seq = it->pms.seq;
    to_deliver.final_seq_proposer = it->pms.seq_proposer;
    deliver(to_deliver);

    // Erase and move to next message in ordered map.
    it = ordering_.erase(it);
  }
}

void Process::TotalOrder(deliverMsgFn deliver) {
  // Launch a multicast sender for each message.
  for (unsigned int i = 0; i < send_count_; ++i) {
    LaunchMulticastSender();
  }
  server_.Listen(
      // Called on all incoming Data Messages.
      [this, deliver](udp::ClientPtr client, char* buf, size_t n) {
        auto data_type = MessageTypeFromBuf(buf, n);
        if (!data_type) {
          // We didn't even get a full data type message. Drop it on the floor.
          logging::out << "Received message with incomplete type\n";
          return udp::ServerAction::Continue;
        }

        // Delegate message handling based on the message type.
        switch (*data_type) {
          case msg::kDataMessageType:
            HandleDataMsg(client, buf, n);
            break;
          case msg::kSeqMessageType:
            HandleSeqMsg(client, buf, n, deliver);
            break;
          case msg::kAckMessageType:
          case msg::kSeqAckMessageType:
            logging::out << "Received ack message when one was not expected\n";
            break;
          default:
            logging::out << "Received message with unknown type: " << *data_type
                         << "\n";
            break;
        }
        return udp::ServerAction::Continue;
      });
}

void Process::HandleDataMsg(const udp::ClientPtr client, char* buf, size_t n) {
  auto data_msg = DataMsgFromBuf(buf, n);
  if (!data_msg || !ValidDataMsg(*data_msg, processes_.size())) {
    // If the data message was not valid, return without trying to use it.
    return;
  }
  logging::out << "Received message " << *data_msg << "\n";

  msg::AckMessage ack_msg;
  ack_msg.sender = data_msg->sender;
  ack_msg.msg_id = data_msg->msg_id;
  ack_msg.proposed_seq = NextSeqNum();
  ack_msg.proposer = id_;
  SendAckMsg(client, ack_msg);

  hold_back_queue_.InsertPending(ack_msg, data_msg->data);
}

void Process::HandleSeqMsg(const udp::ClientPtr client, char* buf, size_t n,
                           const deliverMsgFn deliver) {
  auto seq_msg = SeqMsgFromBuf(buf, n);
  if (!seq_msg || !ValidSeqMsg(*seq_msg, processes_.size())) {
    // If the seq message was not valid, return without trying to use it.
    return;
  }
  logging::out << "Received message " << *seq_msg << "\n";

  msg::SeqAckMessage seqack_msg;
  seqack_msg.sender = seq_msg->sender;
  seqack_msg.msg_id = seq_msg->msg_id;
  SendSeqAckMsg(client, seqack_msg);

  ForwardSeqNum(seq_msg->final_seq);
  hold_back_queue_.Deliver(*seq_msg, deliver);
}

void Process::MaybeDelaySend() {
  if (!delays_) {
    return;
  }

  // Static thread local to avoid expensive initialization cost on every call,
  // while maintaining thread safety.
  static thread_local std::default_random_engine random_engine(
      std::chrono::system_clock::now().time_since_epoch().count());

  // Only delay about half of the messages.
  std::uniform_real_distribution<double> distribution(0.0, 1.0);
  if (distribution(random_engine) < 0.50) {
    return;
  }

  // Delay for a random duration based on a selection from a poisson
  // distribution centered at half the round timeout, at intervals of 1/10th a
  // second.
  typedef std::chrono::duration<int, std::deci> deciseconds;
  std::poisson_distribution<int> poisson(deciseconds{5}.count());
  int delay = poisson(random_engine);
  if (delay <= 0) {
    return;
  }
  std::this_thread::sleep_for(deciseconds{delay});
  return;
}

uint32_t Process::NextSeqNum() {
  std::lock_guard<std::mutex> guard(seq_counter_mutex_);
  return ++seq_counter_;
}
void Process::ForwardSeqNum(uint32_t seen) {
  std::lock_guard<std::mutex> guard(seq_counter_mutex_);
  if (seen > seq_counter_) {
    seq_counter_ = seen;
  }
}

uint32_t Process::UniqueID() { return RandomUint32(); }
uint32_t Process::RandomData() { return RandomUint32(); }

void Process::LaunchMulticastSender() {
  // TODO add some nondeterministic behavior here.
  multicast_threads_.AddThread([this] {
    // Create the DataMessage.
    uint32_t msg_id = UniqueID();
    msg::DataMessage data_msg;
    data_msg.sender = id_;
    data_msg.msg_id = msg_id;
    data_msg.data = RandomData();

    // Multicasts the message to everyone and record response sequence numbers.
    std::vector<uint32_t> seqs(processes_.size());
    threadutil::ThreadGroup sender_threads;
    for (unsigned int pid = 0; pid < processes_.size(); ++pid) {
      udp::ClientPtr client = ClientForId(pid);
      sender_threads.AddThread([this, &seqs, client, pid, &data_msg, msg_id] {
        // Called on each ack attempt. Validate the ack and update the seqs
        // vector based on the provided sequence number in the acknowledgement.
        auto handle_ack = [this, &seqs, pid, &data_msg, msg_id](
            udp::ClientPtr _, char* buf, size_t n) {
          auto ack_msg = AckMsgFromBuf(buf, n);
          if (!ack_msg || !ValidAckMsg(*ack_msg, id_, msg_id, pid)) {
            // If the ack message was not valid, try again.
            // logging::out << "Received invalid ack: " << *ack_msg << "\n";
            return udp::ServerAction::Continue;
          }
          // Record the sequence number on the ack.
          seqs[pid] = ack_msg->proposed_seq;
          return udp::ServerAction::Stop;
        };

        MaybeDelaySend();
        SendDataMsg(client, handle_ack, data_msg);
      });
    }
    sender_threads.JoinAll();

    // Determine the maximum sequence number from all receivers. This is the
    // final sequence number for the multicast message.
    uint32_t final_seq = 0;
    uint32_t final_seq_proposer = 0;
    for (unsigned int pid = 0; pid < processes_.size(); ++pid) {
      uint32_t seq = seqs.at(pid);
      // seq > final_seq implicitly chooses the smallest final_seq_proposer is
      // case of ties for the proposed seqs.
      if (seq > final_seq) {
        final_seq = seq;
        final_seq_proposer = pid;
      }
    }

    // Create the SeqMessage.
    msg::SeqMessage seq_msg;
    seq_msg.sender = id_;
    seq_msg.msg_id = msg_id;
    seq_msg.final_seq = final_seq;
    seq_msg.final_seq_proposer = final_seq_proposer;

    // Multicast final_seq to all processes.
    for (unsigned int pid = 0; pid < processes_.size(); ++pid) {
      udp::ClientPtr client = ClientForId(pid);
      sender_threads.AddThread([this, client, &seq_msg] {
        MaybeDelaySend();
        SendSeqMsg(client, seq_msg);
      });
    }
    sender_threads.JoinAll();
  });
}

}  // namespace process
