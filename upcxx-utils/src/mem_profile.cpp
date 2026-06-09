#include "upcxx_utils/version.h"
#include "upcxx_utils/mem_profile.hpp"
#include "upcxx_utils/log.hpp"

#include <fstream>
#include <string>
#include <sstream>
#include <upcxx/upcxx.hpp>

using namespace std;

namespace upcxx_utils {
    
double get_free_mem(void) {
  string buf;
  ifstream f("/proc/meminfo");
  double mem_free = 0;
  while (!f.eof()) {
    getline(f, buf);
    if (buf.find("MemFree") == 0 || buf.find("Buffers") == 0 || buf.find("Cached") == 0) {
      stringstream fields;
      string units;
      string name;
      double mem;
      fields << buf;
      fields >> name >> mem >> units;
      if (units[0] == 'k') mem *= 1024;
      mem_free += mem;
    }
  }
  return mem_free;
}

#define IN_NODE_TEAM() (!(upcxx::rank_me() % upcxx::local_team().rank_n()))

#ifndef UPCXX_UTILS_NO_THREADS

  void MemoryTrackerThread::start() {
    // create teams of one process per node
    node_team = std::make_unique<upcxx::team>(upcxx::world().split(IN_NODE_TEAM(), 0));
    if (!IN_NODE_TEAM()) return;
    start_free_mem = get_free_mem();
    auto all_start_mem_free = upcxx::reduce_one(start_free_mem, upcxx::op_fast_add, 0, *node_team).wait();
    SLOG("Initial free memory across all nodes: ", std::setprecision(3), std::fixed, get_size_str(all_start_mem_free), "\n");
    min_free_mem = start_free_mem;
    t = new std::thread([&] {
      while (!fin) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        double free_mem = get_free_mem();
        if (free_mem < min_free_mem) min_free_mem = free_mem;
        ticks++;
      }
    });
  }

  void MemoryTrackerThread::stop() {
    if (IN_NODE_TEAM()) {
      if (t) {
        fin = true;
        t->join();
        delete t;
        auto peak_mem = start_free_mem - min_free_mem;
        auto all_peak_mem = upcxx::reduce_one(peak_mem, upcxx::op_fast_add, 0, *node_team).wait();
        SLOG("Peak memory used across all nodes: ", get_size_str(all_peak_mem), "\n");
      }
    }
    upcxx::barrier();
    node_team->destroy();
    upcxx::barrier();
  }

#endif

}; // namespace upcxx_utils
