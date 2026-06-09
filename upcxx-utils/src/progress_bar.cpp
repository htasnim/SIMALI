#include "upcxx_utils/progress_bar.hpp"

#include <sys/stat.h>
#include <chrono>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <upcxx/upcxx.hpp>

#include "upcxx_utils/colors.h"
#include "upcxx_utils/log.hpp"
#include "upcxx_utils/mem_profile.hpp"


namespace upcxx_utils {

  ProgressBar::ProgressBar(int64_t total, string prefix, int pwidth,
                           int width, char complete, char incomplete)
    : total_ticks{total}
    , prefix_str{prefix}
    , prefix_width{pwidth}
    , bar_width{width}
    , complete_char{complete}
    , incomplete_char{incomplete}
    , is_done{false} {
    if (upcxx::rank_me() != RANK_FOR_PROGRESS) return;
    ten_perc = total / 10;
    if (ten_perc == 0) ten_perc = 1;
    if (ProgressBar::SHOW_PROGRESS) SLOG(KLGREEN, "* ", prefix_str, "... ", KNORM, "\n");
    prev_time = start_time;
  }

  ProgressBar::ProgressBar(std::ifstream *infile, string prefix, bool one_file_per_rank,
                           int pwidth, int width, char complete, char incomplete) 
    : infile{infile}
    , prefix_str{prefix}
    , prefix_width{pwidth}
    , bar_width{width}
    , complete_char{complete}
    , incomplete_char{incomplete} {
    if (upcxx::rank_me() != RANK_FOR_PROGRESS) return;
    auto num_ranks = one_file_per_rank ? 1 : upcxx::rank_n();
    infile->seekg(0, infile->end);
    total_ticks = infile->tellg() / num_ranks;
    infile->seekg(0);
    ten_perc = total_ticks / 10;
    if (ten_perc == 0) ten_perc = 1;
    ticks = 0;
    prev_ticks = ticks;
    if (ProgressBar::SHOW_PROGRESS) {
      std::ostringstream oss;
      oss << KLGREEN << std::setw(prefix_width) << std::left << prefix_str << " " << std::flush << std::endl;
      SLOG(oss.str());
    }
  }


  ProgressBar::ProgressBar(const string &fname, istream *infile_, string prefix_, int pwidth, int width, 
                           char complete, char incomplete)
    : infile{infile_}
    , total_ticks{0}
    , prefix_str{prefix_}
    , prefix_width{pwidth}
    , bar_width{width}
    , complete_char{complete}
    , incomplete_char{incomplete}
    , is_done{false} {
    if (upcxx::rank_me() != RANK_FOR_PROGRESS) return;
    int64_t sz = get_file_size(fname);
    if (sz < 0) WARN("Could not read the file size for: ", fname);
    total_ticks = sz;
    ten_perc = total_ticks / 10;
    if (ten_perc == 0) ten_perc = 1;
    ticks = 0;
    prev_ticks = ticks;
    if (ProgressBar::SHOW_PROGRESS)
      SLOG(KLGREEN, "* ", prefix_str, " (", fname.substr(fname.find_last_of("/\\") + 1), " ", get_size_str(sz), ")...", 
           KNORM, "\n");
    prev_time = start_time;
  }

  ProgressBar::~ProgressBar() {
    if (!is_done) done();
  }
  
  void ProgressBar::display(bool is_last) {
    if (upcxx::rank_me() != RANK_FOR_PROGRESS) return;
    if (total_ticks == 0) return;
    float progress = (float) ticks / total_ticks;
    std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    auto time_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time).count();
    auto time_delta = std::chrono::duration_cast<std::chrono::milliseconds>(now - prev_time).count();
    prev_time = now;
    if (ProgressBar::SHOW_PROGRESS) {
      std::cout << std::setprecision(2) << std::fixed;
      std::cout << KLGREEN << "  " << int(progress * 100.0) << "% " << (float(time_elapsed) / 1000.0) << "s " 
                << (float(time_delta) / 1000.0) << "s " << get_size_str(get_free_mem()) << KNORM << std::endl;
    }
  }

  future<> ProgressBar::set_done() {
    is_done = true;
    std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    double time_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time).count() / 1000.;
    future<double> min_time_fut = upcxx::reduce_one(time_elapsed, upcxx::op_fast_min, 0);
    future<double> max_time_fut = upcxx::reduce_one(time_elapsed, upcxx::op_fast_max, 0);
    future<double> tot_time_fut = upcxx::reduce_one(time_elapsed, upcxx::op_fast_add, 0);

    // print once all have completed, but do not create an implicit barrier
    return upcxx::when_all( min_time_fut, max_time_fut, tot_time_fut ).then(
      [](double min_time, double max_time, double tot_time) {
        if (upcxx::rank_me() == RANK_FOR_PROGRESS) {
          double av_time = tot_time / upcxx::rank_n();
          stringstream ss;
          ss << std::setprecision(2) << std::fixed;
          ss << KLGREEN << "  min " << min_time << " Average " << av_time << " max " << max_time << " (balance " 
             <<  (max_time == 0.0 ? 1.0 : (av_time / max_time)) << ")"<< KNORM << std::endl;
          if (ProgressBar::SHOW_PROGRESS) SLOG(ss.str());
        }
      });
  }

  void ProgressBar::done() {
    set_done().wait();
  }
  
  bool ProgressBar::update(int64_t new_ticks) {
    if (!SHOW_PROGRESS) return false;
    if (total_ticks == 0) return false;
    if (new_ticks != -1) ticks = new_ticks;
    else if (infile) ticks = infile->tellg();
    else ticks++;
    if (ticks - prev_ticks > ten_perc) {
      display();
      prev_ticks = ticks;
      return true;
    }
    return false;
  }

  // set static variables
  bool ProgressBar::SHOW_PROGRESS = false;
  int ProgressBar::RANK_FOR_PROGRESS = 0;

}; // namespace upcxx_utils
