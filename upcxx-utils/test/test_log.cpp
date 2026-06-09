#include <upcxx/upcxx.hpp>

#include "upcxx_utils/log.hpp"

int main(int argc, char **argv)
{
  upcxx::init();

  OUT("Success: ", upcxx::rank_me(), " of ", upcxx::rank_n(), "\n");
  upcxx::barrier();

  SOUT("Done\n");

  upcxx::finalize();

  return 0;
}

