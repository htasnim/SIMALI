#include <iostream>
#include <upcxx/upcxx.hpp>

#include "upcxx_utils/version.h"

int main(int argc, char **argv)
{
  upcxx::init();

  if (!upcxx::rank_me()) std::cout << "Found upcxx_utils version " << UPCXX_UTILS_VERSION << std::endl; 

  upcxx::finalize();
  return 0;
}
