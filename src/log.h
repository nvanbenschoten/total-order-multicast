#ifndef LOG_H_
#define LOG_H_

#include <atomic>
#include <iostream>

namespace logging {

// A logger that can be turned on or off. A better alternative would be to use
// macros, but this isn't a performance critical application.
class Logger {
 public:
  Logger(std::ostream* output) : output_(output), enabled_(false){};

  inline void enable(bool enable) { enabled_ = enable; };

  template <typename T>
  const Logger& operator<<(const T& v) const {
    if (enabled_) {
      *output_ << v;
    }
    return *this;
  }

 private:
  std::ostream* output_;
  std::atomic<bool> enabled_;
};

// The global logger. This should always be used instead of creating new Logger
// instances.
extern Logger out;

}  // namespace logging

#endif
