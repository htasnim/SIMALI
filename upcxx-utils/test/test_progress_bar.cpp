#include <upcxx/upcxx.hpp>

#include "upcxx_utils/progress_bar.hpp"

int main(int argc, char **argv)
{
  upcxx::init();

  int total=1000;
  upcxx_utils::ProgressBar prog(total, "TestProgress");
  for(int i = 0 ; i < total; i++) prog.update();
  prog.done();

  upcxx::finalize();
  return 0;
}
