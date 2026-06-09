#pragma once

#include "version.h"

#ifndef UPCXX_UTILS_NO_THREADS
#include <thread>
#endif

#include <upcxx/upcxx.hpp>


namespace upcxx_utils {
    

double get_free_mem(void);

#ifndef UPCXX_UTILS_NO_THREADS
class MemoryTrackerThread {
  std::thread *t = nullptr;
  double start_free_mem, min_free_mem;
  int ticks = 0;
  bool fin = false;
  std::unique_ptr<upcxx::team> node_team;

public:
  void start();
  void stop();
};
#endif

}; // namespace upcxx_utils
