#ifndef PROCESS_H_
#define PROCESS_H_

#include <atomic>
#include <chrono>
#include <cstdint>
#include <experimental/optional>
#include <functional>
#include <iostream>
#include <mutex>
#include <random>
#include <set>
#include <thread>
#include <unordered_map>
#include <vector>

#include "message.h"
#include "net.h"
#include "thread.h"
#include "udp_conn.h"

namespace process {

// The timeout a process will wait for an acknowledgement before sending a
// message again.
const auto kAckTimeout = std::chrono::milliseconds{250};

// Holds a list of processes participating in the agreement algorithm.
typedef std::vector<net::Address> ProcessList;

// Holds a mapping from network addresses to UDP clients.
typedef std::unordered_map<net::Address, udp::ClientPtr, net::AHash>
    UdpClientMap;

// Creates a mapping from network addresses to UDP clients, populated with each
// process provided.
UdpClientMap ClientsForProcessList(const ProcessList& processes);

// RandomUint32 creates a random uint32_t value. The method is safe to call
// from multiple threads.
uint32_t RandomUint32();

// Decodes an unknown message from the provided buffer and pulls out its type.
// If the decoding is successful, the optional return value will be present. If
// not, the return value will be absent.
std::experimental::optional<uint32_t> MessageTypeFromBuf(char* buf, size_t n);

// Decodes a msg::DataMessage from the provided buffer. If the decoding is
// successful, the optional return value will be present. If not, the return
// value will be absent.
std::experimental::optional<msg::DataMessage> DataMsgFromBuf(char* buf,
                                                             size_t n);

// Decodes a msg::AckMessage from the provided buffer. If the decoding is
// successful, the optional return value will be present. If not, the return
// value will be absent.
std::experimental::optional<msg::AckMessage> AckMsgFromBuf(char* buf, size_t n);

// Decodes a msg::SeqMessage from the provided buffer. If the decoding is
// successful, the optional return value will be present. If not, the return
// value will be absent.
std::experimental::optional<msg::SeqMessage> SeqMsgFromBuf(char* buf, size_t n);

// Decodes a msg::SeqAckMessage from the provided buffer. If the decoding is
// successful, the optional return value will be present. If not, the return
// value will be absent.
std::experimental::optional<msg::SeqAckMessage> SeqAckMsgFromBuf(char* buf,
                                                                 size_t n);

// Sends the DataMessage to the client. Passes the handleAck function to
// determine if the acknowledgement is valid or not.
void SendDataMsg(const udp::ClientPtr client, const udp::OnReceiveFn handleAck,
                 msg::DataMessage data_msg);

// Sends the AckMessage to the client without waiting for acknowledgement.
void SendAckMsg(const udp::ClientPtr client, msg::AckMessage ack_msg);

// Sends the SeqMessage to the client and waits for a SeqAckMessage
// acknowledgement.
void SendSeqMsg(const udp::ClientPtr client, msg::SeqMessage seq_msg);

// Sends the SeqAckMessage to the client without waiting for acknowledgement.
void SendSeqAckMsg(const udp::ClientPtr client, msg::SeqAckMessage seqack_msg);

// Determines if the DataMessage is valid, given the number of processes in the
// algorithm.
bool ValidDataMsg(msg::DataMessage& data_msg, size_t process_count);

// Determines if the AckMessage is valid, given the expected sender and message
// id.
bool ValidAckMsg(msg::AckMessage& ack_msg, uint32_t exp_sender,
                 uint32_t exp_msg_id, uint32_t exp_proposer);

// Determines if the SeqMessage is valid, given the number of processes in the
// algorithm.
bool ValidSeqMsg(msg::SeqMessage& seq_msg, size_t process_count);

// Determines if the SeqAckMessage is valid, given the corresponding SeqMessage
// it should be acknowledging.
bool ValidSeqAckMsg(msg::SeqAckMessage& seqack_msg, msg::SeqMessage& seq_msg);

// The key of the HoldBackQueue's pending_seqs_ unordered_map.
struct PendingMessageKey {
  uint32_t sender;
  uint32_t msg_id;

  bool operator==(const PendingMessageKey& other) const {
    return sender == other.sender && msg_id == other.msg_id;
  }
};

// The value of the HoldBackQueue's pending_seqs_ unordered_map.
struct PendingMessageSeq {
  uint32_t seq;
  uint32_t seq_proposer;
};

// The hash function of the HoldBackQueue's pending_seqs_ unordered_map.
struct PMKHash {
  size_t operator()(const PendingMessageKey& pmk) const {
    return std::hash<uint32_t>()(pmk.sender) ^
           std::hash<uint32_t>()(pmk.msg_id);
  }
};

// The element type of the HoldBackQueue's ordering_ ordered set.
struct PendingMessage {
  PendingMessageKey pmk;
  PendingMessageSeq pms;
  uint32_t data;
  bool deliverable;
};

// The comparator that orders PendingMessages in the HoldBackQueue's ordering_
// ordered set.
bool operator<(const PendingMessage& lhs, const PendingMessage& rhs);

// A function that is called when a message is delivered.
typedef std::function<void(msg::SeqMessage&)> deliverMsgFn;

// HoldBackQueue is a queue that ...
class HoldBackQueue {
 public:
  // Inserts the pending message into the HoldBackQueue using the information
  // provided in the AckMessage along with the message's data.
  //
  // Complexity: O(log n) where n is the size of the HoldBackQueue.
  void InsertPending(const msg::AckMessage& ack_msg, uint32_t data);

  // Updates the pending message corresponding to the SeqMessage and delivers
  // all newly deliverable messages by calling the deliver function.
  //
  // Complexity: O(log n + m) where n is the size of the HoldBackQueue and m
  // is the number of messages that can be delivered after updating the
  // SeqMessage's corresponding message.
  void Deliver(const msg::SeqMessage& seq_msg, const deliverMsgFn deliver);

 private:
  // The unordered map and ordered set combination is similar to a linked
  // hashmap in that it allows O(log n) insertion time and O(log n) update time.
  // The unordered_map provides a hashed mapping between a message's uniquely
  // identifying information (sender, msg_id). This hashes to the rest of the
  // pending message information in constant time so that it can be recovered
  // later without a full search of the ordered set when updating the delivery
  // state. The ordered set maintains an ordered tree so that delivery can be
  // performed by scanning from the beginning of the tree to the end.
  std::unordered_map<PendingMessageKey, PendingMessageSeq, PMKHash>
      pending_seqs_;
  // The ordering of pending messages, based on the comparator defined for
  // PendingMessage.
  std::set<PendingMessage> ordering_;
};

// An abstraction of a single process providing total ordered reliable
// multicast.
class Process {
 public:
  Process(const ProcessList& processes, unsigned int id,
          unsigned int send_count, unsigned short server_port, bool delays)
      : processes_(processes),
        clients_(ClientsForProcessList(processes)),
        id_(id),
        send_count_(send_count),
        server_(server_port),
        delays_(delays),
        seq_counter_(0) {}

  // TotalOrder runs the ISIS Total Order Multicast Algorithm and calls the
  // provided deliver function for each message delivered.
  void TotalOrder(const deliverMsgFn deliver);

 private:
  const ProcessList processes_;
  const UdpClientMap clients_;
  const unsigned int id_;
  const unsigned int send_count_;
  const udp::Server server_;
  const bool delays_;

  // Returns the UDP client for a given process ID.
  inline udp::ClientPtr ClientForId(unsigned int pid) const {
    return clients_.at(processes_.at(pid));
  }

  // Holds the pending messages that are not yet deliverable.
  HoldBackQueue hold_back_queue_;
  // Handles the reception of a DataMessage.
  void HandleDataMsg(const udp::ClientPtr client, char* buf, size_t n);
  // Handles the reception of a SeqMessage. Calls the provided deliver message
  // for each message that the SeqMessage allows to be delivered.
  void HandleSeqMsg(const udp::ClientPtr client, char* buf, size_t n,
                    const deliverMsgFn deliver);

  // Possibly delay the send of a message, based on the if the delays flag was
  // provided. Blocks synchonously if delaying.
  void MaybeDelaySend();

  // A thread-safe counter for sequence numbers of messages originating from
  // this process.
  std::mutex seq_counter_mutex_;
  uint32_t seq_counter_;
  // NextSeqNum returns the next available sequence number.
  uint32_t NextSeqNum();
  // ForwardSeqNum should be called whenever a SeqMessage is received with the
  // SeqMessage's final sequence number to make sure we always propose sequence
  // numbers greater than any this process has ever seen before.
  void ForwardSeqNum(uint32_t seen);

  // UniqueID creates a random unique message identifier.
  uint32_t UniqueID();
  // RandomData creates random data for a message.
  uint32_t RandomData();

  // Holds the multicast threads of the process.
  threadutil::ThreadGroup multicast_threads_;
  // LaunchMulticastSender launches a multicast sender that transmits a message
  // to all other processes following the ISIS Total Order Multicast Algorithm.
  void LaunchMulticastSender();
};

}  // namespace process

#endif
