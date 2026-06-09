#include <upcxx/upcxx.hpp>

#include "upcxx_utils/timers.hpp"

using namespace upcxx_utils;

int main(int argc, char **argv)
{
  upcxx::init();

  {
    BarrierTimer bt("Barrier");
  }

  upcxx::finalize();

  return 0;
}

