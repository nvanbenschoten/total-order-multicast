#include "message.h"

namespace msg {

std::ostream& operator<<(std::ostream& o, const DataMessage& data_msg) {
  o << "{type: " << data_msg.type;
  o << ", sender: " << data_msg.sender;
  o << ", msg_id: " << data_msg.msg_id;
  o << ", data: " << data_msg.data;
  o << "}";
  return o;
}

std::ostream& operator<<(std::ostream& o, const AckMessage& ack_msg) {
  o << "{type: " << ack_msg.type;
  o << ", sender: " << ack_msg.sender;
  o << ", msg_id: " << ack_msg.msg_id;
  o << ", proposed_seq: " << ack_msg.proposed_seq;
  o << ", proposer: " << ack_msg.proposer;
  o << "}";
  return o;
}

std::ostream& operator<<(std::ostream& o, const SeqMessage& seq_msg) {
  o << "{type: " << seq_msg.type;
  o << ", sender: " << seq_msg.sender;
  o << ", msg_id: " << seq_msg.msg_id;
  o << ", final_seq: " << seq_msg.final_seq;
  o << ", final_seq_proposer: " << seq_msg.final_seq_proposer;
  o << "}";
  return o;
}

std::ostream& operator<<(std::ostream& o, const SeqAckMessage& seqack_msg) {
  o << "{type: " << seqack_msg.type;
  o << ", sender: " << seqack_msg.sender;
  o << ", msg_id: " << seqack_msg.msg_id;
  o << "}";
  return o;
}

}  // namespace msg
