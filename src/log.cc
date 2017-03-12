#include "log.h"

namespace logging {

// Needed to be defined in .cc file to avoid duplicate symbols.
Logger out(&std::cerr);

}  // namespace logging
