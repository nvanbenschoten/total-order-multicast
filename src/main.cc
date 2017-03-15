#include <exception>
#include <experimental/optional>
#include <fstream>
#include <iostream>
#include <string>

#include "args.h"
#include "log.h"
#include "message.h"
#include "net.h"
#include "process.h"

const std::string program_desc =
    "An implementation of the ISIS Total Order Multicast Algorithm.";
const std::string help_desc = "Display this help menu.";
const std::string port_desc =
    "The port identifies on which port the process will be listening on for "
    "incoming messages. It can take any integer from 1024 to 65535. Can be "
    "overridden by specifying a port on a host in the hostfile using "
    "<hostname>:<port> notation. Not required if all hosts have specified "
    "ports in the hostfile. Required otherwise.";
const std::string hostfile_desc =
    "The hostfile is the path to a file that contains the list of hostnames "
    "that the processes are running on. It should be in the following format.\n"
    "`\n"
    "xinu01.cs.purdue.edu\n"
    "xinu02.cs.purdue.edu\n"
    "...\n"
    "`\n"
    "All the processes will listen on the port, specified by the port flag. "
    "Alternatively, hosts can specify an alternate port using "
    "<hostname>:<port> notation, like:\n"
    "`\n"
    "xinu01.cs.purdue.edu:1234\n"
    "xinu01.cs.purdue.edu:1235\n"
    "...\n"
    "`\n"
    "This makes it possible to run multiple processes on the same host. "
    "The line number indicates the identifier of the process.";
const std::string id_desc =
    "The optional id specifier of this process. Only needed if multiple "
    "processes in the hostfile are running on the same host, otherwise it can "
    "be deduced from the hostfile. 0-indexed.";
const std::string count_desc =
    "The count indicates the number of messages the process needs to "
    "multicast. The value of count is non-negative. The process should remain "
    "operational once started, even if the count is 0 or the process finishes "
    "multicasting all its messages.";
const std::string delays_desc =
    "When set, the delays flag indicates that random delays should be "
    "introduced when sending messages between processes.";
const std::string verbose_desc = "Sets the logging level to verbose.";
const std::string red_start = "\033[1;31m";
const std::string red_end = "\033[0m";

typedef args::ValueFlag<int> IntFlag;
typedef args::ValueFlag<std::string> StringFlag;

// Gets the process list from the hostfile.
process::ProcessList GetProcesses(
    const std::string hostfile,
    std::experimental::optional<unsigned short> default_port) {
  process::ProcessList processes;
  std::ifstream file(hostfile);
  if (!file) {
    throw std::runtime_error("could not open hostfile");
  }

  std::string host;
  while (file >> host) {
    try {
      auto addr = net::AddressWithDefaultPort(host, default_port);
      processes.push_back(addr);
    } catch (std::invalid_argument e) {
      throw args::UsageError(e.what());
    }
  }
  return processes;
}

// Checks if the --id flag is within the process list and pointing to
// our hostname.
void CheckProcessId(const process::ProcessList& processes, int my_id) {
  // Check if the id is within bounds.
  if (my_id < 0 || (uint)my_id >= processes.size()) {
    throw args::ValidationError("--id value not found in hostfile");
  }

  // Check if the process is on this host.
  if (processes.at(my_id).hostname() != net::GetHostname()) {
    throw args::ValidationError("--id value is not the hostname of this host");
  }
}

// Gets the current process ID.
int GetProcessId(const process::ProcessList& processes) {
  int found = -1;
  auto hostname = net::GetHostname();
  for (std::size_t i = 0; i < processes.size(); ++i) {
    if (processes.at(i).hostname() == hostname) {
      if (found >= 0) {
        // Multiple processes are set to use our host.
        throw args::UsageError(
            "when running multiple processes on the same host, use the --id "
            "flag");
      }
      found = i;
    }
  }
  if (found == -1) {
    // Our process is not in the file, throw an error
    throw args::ValidationError("current hostname not found in hostfile");
  }
  return found;
}

int main(int argc, const char** argv) {
  args::ArgumentParser parser(program_desc);
  args::HelpFlag help(parser, "help", help_desc, {"help"});
  IntFlag port(parser, "port", port_desc, {'p', "port"});
  StringFlag hostfile(parser, "hostfile", hostfile_desc, {'h', "hostfile"});
  IntFlag id(parser, "id", id_desc, {'i', "id"});
  IntFlag count(parser, "count", count_desc, {'c', "count"});
  args::Flag delays(parser, "delays", delays_desc, {'d', "delays"});
  args::Flag verbose(parser, "verbose", verbose_desc, {'v', "verbose"});

  try {
    parser.ParseCLI(argc, argv);

    // Set up logging.
    logging::out.enable(verbose);

    // Check required fields.
    if (!hostfile) throw args::UsageError("--hostfile is a required flag");
    if (!count) throw args::UsageError("--count is a required flag");
    auto hostfile_val = args::get(hostfile);
    auto count_val = args::get(count);

    // Get the default process port, if one is supplied.
    std::experimental::optional<unsigned short> default_port;
    if (port) {
      default_port = args::get(port);
    }

    // Create the process list from the hostfile.
    auto processes = GetProcesses(hostfile_val, default_port);

    // Determine the current process's ID.
    int id_val;
    if (id) {
      id_val = args::get(id);
      CheckProcessId(processes, id_val);
    } else {
      id_val = GetProcessId(processes);
    }
    auto server_port = processes.at(id_val).port();

    // Create the process with the command line options.
    process::Process p(processes, id_val, server_port, delays);

    // Run the total order algorithm.
    p.TotalOrder(count_val, [id_val](msg::SeqMessage& seq_msg) {
      std::cout << id_val << ": Processed message " << seq_msg.msg_id
                << " from sender " << seq_msg.sender << " with seq ("
                << seq_msg.final_seq << ", " << seq_msg.final_seq_proposer
                << ")" << std::endl;
    });
  } catch (const args::Help) {
    std::cout << parser;
    return 0;
  } catch (const args::UsageError& e) {
    std::cerr << "\n  " << red_start << e.what() << red_end << "\n\n";
    std::cerr << parser;
    return 1;
  } catch (const std::exception& e) {
    std::cerr << red_start << e.what() << red_end << "\n";
    return 1;
  }
}
