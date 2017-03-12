#ifndef THREAD_H_
#define THREAD_H_

#include <thread>
#include <utility>
#include <vector>

namespace threadutil {

// Holds references to a group of threads and exposes functionality to operate
// on all of them at once.
class ThreadGroup {
 public:
  // Adds a new thread to the group.
  template <class Function>
  inline void AddThread(Function&& f) {
    threads_.push_back(std::thread(std::forward<Function>(f)));
  };

  // Clears the group. Should only be callsd after JoinAll.
  inline void Clear() { threads_.clear(); };

  // Waits for all threads in the group to complete execution.
  inline void JoinAll() {
    for (auto& thread : threads_) {
      if (thread.joinable()) thread.join();
    }
  };

 private:
  std::vector<std::thread> threads_;
};

}  // namespace threadutil

#endif
