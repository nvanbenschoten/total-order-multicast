# Total Order Multicast

_Nathan VanBenschoten_

An implementation of the ISIS Total Order Multicast Algorithm.


## Building

Run `make` to build the binary `bin/proj2`

Run `make clean` to clean all build artifacts


## Running

To run a process, a command like the following can be used.

```
./bin/proj2 -p 54321 -h hostfile -c 2
```

All processes in the total order multicast algorithm are identical; there is no
leader process and there is no order in which processes need to be brought up.
Each process remains operational once started, even if there are no more
messages to multicast.

### Delayed Messages

Adding the **-d** (**--delays**) flag will turn on delayed message mode. In this
mode, messages are occasionally delayed by a random amount of time. The
nondeterminism provided by this randomization helps stress the underlying
algorithm.

### Verbose Mode

Adding the **-v** (**--verbose**) flag will turn on verbose mode, which will
print logging information to standard error. This information includes details
about all messages sent and received, as well as round timeout information.

### Command Line Arguments

A full list of command line arguments can be seen by running `./bin/proj2
--help`.


## Testing

The project comes with a automated test suite located under the `test`
directory. Each test configures a set of processes with different numbers of
messages to send. There is a test in each configuration with and without
"delays" mode on. The following tests are included:

- `TestTotalOrderUniform`
- `TestTotalOrderUniformWithDelays`
- `TestTotalOrderDifferent`
- `TestTotalOrderDifferentWithDelays`
- `TestTotalOrderSingleSender`
- `TestTotalOrderSingleSenderWithDelays`
- `TestTotalOrderDualSenders`
- `TestTotalOrderDualSendersWithDelays`
- `TestTotalOrderLarge`
- `TestTotalOrderLargeWithDelays`
- `TestTotalOrderRandom`
- `TestTotalOrderRandomWithDelays`

To run all tests, run the command `make test`

Each test creates a subprocess for all processes in the system and then reads in
the messages that the subprocesses deliver. The tests then verify that the
correct number of messages were delivered, and that all processes delivered the
same messages in the same order.


## System Architecture

The system is designed around two main classes: the `Process` and the
``HoldBackQueue``.

When a process starts up, it processes all command line arguments and performs
validation on all flags. The validation includes checking that a message count
flag was provided and determining the list of hosts participating in the
algorithm.

Once command line parsing and validation is complete, the options are used to
construct a `Process`.

### Process

`Process` is a class that exposes a `TotalOrder` method. This method runs the
total order algorithm and calls a provided callback on each message delivery. It
is given a parameter for the number of messages that the process should send
during the execution of the algorithm.

#### Multicast Senders

When running, the Process launches a multicast sender thread for each message,
as well as running a blocking server that listens for incoming messages. Each
multicast sender thread walks through the stages for sending a message in the
total order algorithm (see the [state diagram](#state-diagram)). It creates a
message with random data and a random ID. It then sends this message to all
processes, collecting their acknowledgements in return. Using these
acknowledgements, it determines the final sequence number and sequence proposer
for the data message, and broadcasts this information through a sequence
message.

Note that multicast sender threads send through UDP even to their own process's
server thread. While this could be circumvented as an optimization, the approach
works fine and maintains a desirable level of symmetry.

#### Total Order Server

The Process's server listens for incoming messages, distinguishing them using
their type header. On a `DataMessage`, it adds the undeliverable pending message
to the `HoldBackQueue` before returning an acknowledgement with a proposed
sequence number. Before doing this though, it makes sure that the
`HoldBackQueue` does not already contain the message, in which case it returns
the previously sent acknowledgement. This prevents replayed messages from
receiving different sequence numbers.

On a `SequenceMessage`, the Process updates the now deliverable message in the
`HoldBackQueue`. It lets this queue determine which messages can now be
delivered based on the new delivery status and the final sequence number of the
sequence message.

#### Sequence Counter

A Process maintains a sequence counter that provides the causality necessary for
correctness of the ISIS Total Order Multicast Algorithm. It increments this
sequence counter every time it suggests a sequence for a data message.
Additionally, it forwards this sequence number when it receives a
`SequenceMessage` so that it never suggests a sequence number less than or equal
to one it has already seen. In this way, the sequence counter is handled in a
very similar way to a Lamport clock.

### HoldBackQueue

The `HoldBackQueue` is the data structure that buffers pending `DataMessage`s
until they can be delivered. It consists of an unordered map and an ordered set,
allowing it to expose `O(1)` lookup time, `O(log(n))` insertion time, and
`O(log(n))` update time. Undeliverable messages are added to the queue by a
Process when their `DataMessage`s are first received using the classes
`InsertUndeliverable` method. Later, when a finalized `SequenceMessage` is
received, the queue's `SetDeliverable` method is called, and the pending message
is updated to the relivable state. Because of the ordering of the ordered set in
the queue, it can then be scanned from left to right to deliver all newly
deliverable messages.

### UDP Client and Server

The abstraction of reliable communication is provided by the `udp` namespace.
This namespace exposes three classes to make dealing with UDP straightforward
for the General implementations. These classes also perform the task of hiding
away C Socket programming details behind a more idiomatic C++ interface.

First, the namespace exposes a `Client` class. The class wraps a UDP socket and
allows both unreliable and reliable (unacknowledged and acknowledged)
transmission of byte buffers. When sending reliable messages, the class allows
its caller to determine whether an acknowledgment is valid or not. The `Client`
is constructed with a remote address and an optional acknowledgment timeout.
Each individual `Client` is safe for use by multiple threads at the same time, 
meaning that external synchronization is not needed.

The namespace also exposes a `Server` class. This class wraps a UDP socket and
synchronously blocks on the socket while trying to receive information. When a
new message comes in, the `Server` calls a provided callback with the messages
data as well as with a `Client` instance for consumers to respond to the remote
client who sent the message. The `Server` also handles serving timeouts, calling
a secondary timeout callback in those cases. The `Server` class is constructed
with a port to bind to and an optional timeout.

### Logging Module

The `logging` namespace provides a conditional output logger `out` that is only
enabled when verbose mode is turned on. It exposes itself as an `std::ostream`,
and forwards all information to standard error when it is enabled.


## State Diagram

Below is a state diagram of the algorithm for the multicast delivery of a single
data message. Note that the parts in blue take place only on the sender, while
the parts in red take place on all processes concurrently.

![Total Order State Machine](./media/total_order.png)


## Design Decisions

### HoldBackQueue Data Structures

One major design decision was about the data structures to use for the
`HoldBackQueue`. Off the bat it was clear that an ordered data structure would
be necessary to perform the delivery scan that happens after any message is set
to deliverable. This scan walks over the queue, in-order, and delivers all
messages that have just become deliverable. However, because C++'s order set can
only be indexed into with a fully populated element and because its ordered map
can only define comparators on the map key, either of these data structures
alone were not enough to perform the deliverable update without requiring a
linear scan of the entire queue. This was because we needed to have the original
sequence number and suggester present when indexing into the set/map, which
would not be easily available during the second update.

To get around this, we introduced a hashmap mapping the unique parts of a
message, its `(sender, msg_id)` pair, with its local sequence information. This
allowed us to look this information up in linear time and then combine this with
the other known information to index into the set (implemented as a red-black
binary search tree) when updating the message.

Put together, this allowed our queue to expose `O(1)` lookup time, `O(log(n))`
insertion time, and `O(log(n))` update time.

### Sender Threading Model

The design of the state machine was primarily driven by the interface exposed by
the `udp::Server` and `udp::Client` classes. Both of these classes use a
synchronous execution model, which meant that to gain any concurrency, it was
necessary to do so outside their abstraction boundary. Because of this, we
decided to gain concurrency through the use of threads.

This was accomplished by introducing the `threadutil::ThreadGroup` class.

### Delayed Send Flag

To facilitate proper testing, we needed a way to introduce nondeterminism into
the execution of the total order algorithm. We did this by introducing a
[delayed messages](#delayed-messages) flag. When in use, this flag randomly
picks messages to delay, which means that any two runs will produce different
results. Our confidence in the algorithm grew tremendously once we verified that
even with this delayed behavior activated, our algorithm still worked.

### Testing Suite

Testing was a major concern for validating our implementation of the ISIS Total
Order Multicast Algorithm. While manual testing showed correct results, it took
a while to set up each scenario, and was impractical for larger tests. To
overcome this, we introduced a [testing suite](#testing). This test suite includes
a number of deterministic tests at varying sizes. It also includes a number of
randomized tests with the hopes of catching corner cases.


## Implementation Issues

### Multiple Processes on the Same Host

One of the implementation issues faced while developing the algorithm was its
difficulty to test, because the suggested template "assumes that each host is
running only one instance of the process." This meant that even during
development, to test a _m_ process instance of the algorithm, _m_ hosts needed
to coordinate and be kept in sync with code changes. To address this, the
single-process-per-host restriction was lifted early in the development cycle.
This was accomplished by allowing an optional port specification in the hostfile
for a given process using a `<hostname>:<port>` notation. Once individual
processes could specify unique ports, an optional **-i** (**--id**) flag was
used to distinguish the current process in a hostfile where multiple processes
were running on the same host. This way, the algorithm could be run on a single
host with a hostfile like:

```
<hostname>:1234
<hostname>:1235
<hostname>:1236
<hostname>:1237
```

And commands like:

```
./bin/proj2 -h hostfile -c 2 -i=0
```

## References

[1] http://studylib.net/doc/7830646/isis-algorithm-for-total-ordering-of-messages

[2] http://www.cse.buffalo.edu/~stevko/courses/cse486/spring13/lectures/12-multicast2.pdf
