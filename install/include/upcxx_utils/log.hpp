#pragma once

#include <string>
#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <string_view>
#include <upcxx/upcxx.hpp>

#include "colors.h"
#include "version.h"

#define __FILENAME__ (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__)
#define __FILEFUNC__ (__FILENAME__ + string(":") + __func__)

#define ONE_B  (1L)
#define ONE_KB (1024L)
#define ONE_MB (ONE_KB*1024L)
#define ONE_GB (ONE_MB*1024L)
#define ONE_TB (ONE_GB*1024L)

#define CLOCK_NOW std::chrono::high_resolution_clock::now

using upcxx::rank_n;
using upcxx::rank_me;

using std::string;
using std::stringstream;
using std::ostringstream;
using std::ostream;
using std::ifstream;
using std::ofstream;
using std::cout;
using std::cerr;
using std::vector;

namespace upcxx_utils {

// the log files as externs
extern ofstream _logstream;
extern ofstream _dbgstream;
extern bool _verbose;

// public methods to open loggers

// logger is opened once per application, closed automatically
// typically only rank 0 writes to this. Logs include timestamps
// and generally are a superset of stdout + stderr
void init_logger(const string name, bool verbose = true);

// dbg loggers are meant to be open and closed by each module
// every rank opens its own file
void open_dbg(const string name);
void close_dbg();


// methods to write to a stream
ostream & _logger_write(ostream &os, string str);
ostream & _logger_timestamp(ostream &os);


// last in list is a noop
inline void _logger_recurse(ostringstream &os)
{
}

// log the next item in a list
template <typename T, typename... Params>
inline void _logger_recurse(ostringstream &os, T first, Params... params)
{
  os << first;
  _logger_recurse(os, params ...);
}

// initial log line
template <typename... Params>
void logger(ostream &stream, bool fail, bool serial, bool timestamp, bool flush, Params... params)
{
  if (serial && upcxx::rank_me()) return;
  ostringstream os;
  if (timestamp) _logger_timestamp(os);
  _logger_recurse(os, params ...); // recurse through remaining parameters
  string outstr = os.str();
  // FIXME: this still double prints to the debug file
  //if (_dbgstream && _dbgstream.rdbuf() != stream.rdbuf()) { // do not double print to dbgstream
  //  _logger_write(_dbgstream, outstr).flush(); // always flush dbg stream
  //}
  // don't need to write on fail because this will be thrown
  if (!fail) {
    _logger_write(stream, outstr);
    if (flush) stream.flush();
  }
  if (fail) throw std::runtime_error(outstr);
}

}; // namespace upcxx_utils

// rank0 to stdout
#define SOUT(...) do {                                                  \
    upcxx_utils::logger(std::cout, false, true, false, rank_me() == 0, ##__VA_ARGS__);        \
  } while (0)

// any to stdout (take care)
#define OUT(...) do {                                                  \
    upcxx_utils::logger(std::cout, false, false, false, true, ##__VA_ARGS__);                  \
  } while (0)

// any with timestamp to stdout
#define INFO(...) do {\
    upcxx_utils::logger(std::cout, false, false, true, rank_me() == 0, ##__VA_ARGS__);                    \
  } while (0)

// rank0 to logfile and stdout
#define SLOG(...) do {                                              \
    upcxx_utils::logger(std::cout, false, true, false, true, ##__VA_ARGS__);                 \
    upcxx_utils::logger(upcxx_utils::_logstream, false, true, true, true, ##__VA_ARGS__);           \
  } while (0)

// rank0 to logfile and if _verbose is set also t stdout
#define SLOG_VERBOSE(...) do {                                       \
    if (upcxx_utils::_verbose) upcxx_utils::logger(std::cout, false, true, false, true, ##__VA_ARGS__);    \
    upcxx_utils::logger(upcxx_utils::_logstream, false, true, true, true, ##__VA_ARGS__);           \
  } while (0)


// extra new lines around errors and warnings for readability and do not color the arguments as it can lead to terminal color leaks
#define WARN(...) do {                                                      \
    if (upcxx_utils::_logstream) upcxx_utils::logger(upcxx_utils::_logstream, false, false, true, true, KRED, "[", upcxx::rank_me(), "] <", __FILENAME__, ":", __LINE__, "> WARNING: ", KNORM,  ##__VA_ARGS__, "\n"); \
    upcxx_utils::logger(std::cerr, false, false, false, true, KRED, "[", upcxx::rank_me(), "] <", __FILENAME__, ":", __LINE__, "> WARNING: ", KNORM,  ##__VA_ARGS__, "\n"); \
  } while(0)

// warn but only from rank 0
#define SWARN(...) do {                                                     \
    if (upcxx_utils::_logstream) upcxx_utils::logger(upcxx_utils::_logstream, false, true, true, true, KRED, "[", upcxx::rank_me(), "] <", __FILENAME__, ":", __LINE__, "> WARNING: ", KNORM, ##__VA_ARGS__, "\n"); \
    upcxx_utils::logger(std::cerr, false, true, false, true, KRED, "[", upcxx::rank_me(), "] <", __FILENAME__, ":", __LINE__, "> WARNING: ", KNORM, ##__VA_ARGS__, "\n"); \
   } while (0)

#define DIE(...) do {                                                      \
     if (upcxx_utils::_logstream) {\
       upcxx_utils::logger(upcxx_utils::_logstream, false, false, true, true, KLRED, "[", upcxx::rank_me(), "] <", __FILENAME__ , \
                           ":", __LINE__, "> ERROR: ", KNORM, ##__VA_ARGS__, "\n"); \
     } \
     upcxx_utils::logger(std::cerr, true, false, false, true, KLRED, "[", upcxx::rank_me(), "] <", __FILENAME__ , ":", __LINE__, \
                         "> ERROR: ", KNORM, ##__VA_ARGS__, "\n"); \
  } while (0)

// die but only from rank0
#define SDIE(...) do {                                                      \
    if (upcxx_utils::_logstream) {\
      upcxx_utils::logger(upcxx_utils::_logstream, false, true, true, true, KLRED, "[", upcxx::rank_me(), "] <", __FILENAME__ ,\
                          ":", __LINE__, "> ERROR: ", KNORM, ##__VA_ARGS__, "\n"); \
    } \
    upcxx_utils::logger(std::cerr, true, true, false, true, KLRED, "[", upcxx::rank_me(), "] <", __FILENAME__ , ":", __LINE__, \
                        "> ERROR: ", KNORM, ##__VA_ARGS__, "\n"); \
  } while (0)

#ifdef DEBUG
// any rank writes to its dbg log file, if available
#define DBG(...)                                                                                                     \
  do {                                                                                                               \
    if (upcxx_utils::_dbgstream) {                                                                                   \
      upcxx_utils::logger(upcxx_utils::_dbgstream, false, false, true, true, "<", __FILENAME__, ":", __LINE__, "> ", \
                          ##__VA_ARGS__);                                                                            \
    }                                                                                                                \
  } while (0)
#define DBG_CONT(...)                                                                         \
  do {                                                                                        \
    if (upcxx_utils::_dbgstream) {                                                            \
      upcxx_utils::logger(upcxx_utils::_dbgstream, false, false, false, true, ##__VA_ARGS__); \
    }                                                                                         \
  } while (0)
#else
#define DBG(...) /* noop */
#define DBG_CONT(...) /* noop */
#endif

//
// file path methods
//
#ifndef MAX_FILE_PATH
#define MAX_FILE_PATH PATH_MAX
#endif

#define MAX_RANKS_PER_DIR 1000

namespace upcxx_utils {

bool file_exists(const string &filename);

void check_file_exists(const string &filename);

// returns 1 when it created the directory, 0 otherwise, -1 if there is an error
int check_dir(const char *path);

// replaces the given path with a rank based path, inserting a rank-based directory
// example:  get_rank_path("path/to/file_output_data.txt", rank) -> "path/to/per_thread/<rankdir>/<rank>/file_output_data.txt"
// of if rank == -1, "path/to/per_thread/file_output_data.txt"
bool get_rank_path(string &fname, int rank);

std::vector<string> find_per_thread_files(string &fname_list, const string &ext, bool cached_io);

string remove_file_ext(const string &fname);

string get_basename(const string &fname);

int64_t get_file_size(string fname);

//
// formatting methods
//

string get_size_str(int64_t sz);

string get_float_str(double fraction, int precision = 3);

string perc_str(int64_t num, int64_t tot);

string get_current_time(bool fname_fmt=false);

vector<string> split(const string &s, char delim);

void find_and_replace(std::string& subject, const std::string& search, const std::string& replace);

std::string_view substr_view(const std::string &s, size_t from, size_t len=string::npos);

void replace_spaces(string &s);

string tail(const string &s, int n);

string head(const string &s, int n);

}; // namespace upcxx_utils
