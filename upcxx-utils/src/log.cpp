// log.cpp

#include "upcxx_utils/log.hpp"

#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <ctime>
#include <chrono>
#include <iostream>
#include <iomanip>
#include <fstream>
#include <limits>
#include <memory>
#include <upcxx/upcxx.hpp>

using upcxx::rank_n;
using upcxx::rank_me;

using std::string;
using std::stringstream;
using std::ostringstream;
using std::ostream;
using std::ifstream;
using std::ofstream;
using std::to_string;
using std::cout;
using std::cerr;

namespace upcxx_utils {

// the log files
ofstream _logstream;
ofstream _dbgstream;
bool _verbose = true;

void init_logger(const string name, bool verbose) {
  _verbose = verbose;
  if (!upcxx::rank_me()) {
    bool old_file = file_exists(name);
    _logstream.open(name, std::ofstream::out | std::ofstream::app);
    // the old file should only exist if this is a restart
    if (old_file) _logstream << "\n\n==========  RESTART  ==================\n\n";
  }
}

void open_dbg(const string name)
{
  time_t curr_t = std::time(nullptr);
  string dbg_fname = name + to_string(curr_t) + ".log"; // never in cached_io
  get_rank_path(dbg_fname, rank_me());
  _dbgstream.open(dbg_fname);
}

void close_dbg()
{
  _dbgstream.flush();
  _dbgstream.close();
}

ostream & _logger_write(ostream &os, string str)
{
#ifdef CONFIG_USE_COLORS
  if (os.rdbuf() != std::cout.rdbuf() && os.rdbuf() != std::cerr.rdbuf()) {
    // strip off colors for log file
    for (auto c : COLORS) find_and_replace(str, c, "");
  }
#endif
  os << str;
  return os;
}

ostream & _logger_timestamp(ostream &os)
{
  std::time_t result = std::time(nullptr);
  char buffer[100];
  size_t sz = strftime(buffer,sizeof(buffer),"%Y-%m-%d %H:%M:%S ", std::localtime(&result) );
  os << string( sz > 0 ? buffer : "BAD TIME " );
  return os;
}

//
// file path methods
//

bool file_exists(const string &fname) {
  ifstream ifs(fname, std::ios_base::binary);
  return ifs.good();
}

void check_file_exists(const string &filename)
{
  auto fnames = split(filename, ',');
  for (auto fname : fnames) {
    ifstream ifs(fname);
    if (!ifs.is_open()) SDIE("File ", fname, " cannot be accessed: ", strerror(errno), "\n");
  }
  upcxx::barrier();
}

// returns 1 when it created the directory, 0 otherwise, -1 if there is an error
int check_dir(const char *path)
{
  if (0 != access(path, F_OK)) {
    if (ENOENT == errno) {
      // does not exist
      // note: we make the directory to be world writable, so others can delete it later if we
      // crash to avoid cluttering up memory
      mode_t oldumask = umask(0000);
      if (0 != mkdir(path, 0777) && 0 != access(path, F_OK)) {
        umask(oldumask);
        fprintf(stderr, "Could not create the (missing) directory: %s (%s)", path, strerror(errno));
        return -1;
      }
      umask(oldumask);
    }
    if (ENOTDIR == errno) {
      // not a directory
      fprintf(stderr, "Expected %s was a directory!", path);
      return -1;
    }
  } else {
    return 0;
  }
  return 1;
}

// replaces the given path with a rank based path, inserting a rank-based directory
// example:  get_rank_path("path/to/file_output_data.txt", rank) -> "path/to/per_thread/<rankdir>/<rank>/file_output_data.txt"
// of if rank == -1, "path/to/per_thread/file_output_data.txt"
bool get_rank_path(string &fname, int rank)
{
  char buf[MAX_FILE_PATH];
  strcpy(buf, fname.c_str());
  int pathlen = strlen(buf);
  char newPath[MAX_FILE_PATH*2+50];
  char *lastslash = strrchr(buf, '/');
  int checkDirs = 0;
  int thisDir;
  char *lastdir = NULL;

  if (pathlen + 25 >= MAX_FILE_PATH) {
    WARN("File path is too long (max: ", MAX_FILE_PATH, "): ", buf, "\n");
    return false;
  }
  if (lastslash) {
    *lastslash = '\0';
  }
  if (rank < 0) {
    if (lastslash) {
      snprintf(newPath, MAX_FILE_PATH*2+50, "%s/per_thread/%s", buf, lastslash + 1);
      checkDirs = 1;
    } else {
      snprintf(newPath, MAX_FILE_PATH*2+50, "per_thread/%s", buf);
      checkDirs = 1;
    }
  } else {
    if (lastslash) {
      snprintf(newPath, MAX_FILE_PATH*2+50, "%s/per_thread/%08d/%08d/%s", buf, rank / MAX_RANKS_PER_DIR, rank, lastslash + 1);
      checkDirs = 3;
    } else {
      snprintf(newPath, MAX_FILE_PATH*2+50, "per_thread/%08d/%08d/%s", rank / MAX_RANKS_PER_DIR, rank, buf);
      checkDirs = 3;
    }
  }
  strcpy(buf, newPath);
  while (checkDirs > 0) {
    strcpy(newPath, buf);
    thisDir = checkDirs;
    while (thisDir--) {
      lastdir = strrchr(newPath, '/');
      if (!lastdir) {
        WARN("What is happening here?!?!\n");
        return false;
      }
      *lastdir = '\0';
    }
    check_dir(newPath);
    checkDirs--;
  }
  fname = buf;
  return true;
}


std::vector<string> find_per_thread_files(string &fname_list, const string &ext, bool cached_io)
{
  std::vector<string> full_fnames;
  auto fnames = split(fname_list, ',');
  for (auto fname : fnames) {
      if (cached_io) fname = "/dev/shm/" + fname;
      // first check for gzip file
      fname += ext;
      get_rank_path(fname, upcxx::rank_me());
      string gz_fname = fname + ".gz";
      struct stat stbuf;
      if (stat(gz_fname.c_str(), &stbuf) == 0) {
        // gzip file exists
        SOUT("Found compressed file '", gz_fname, "'\n");
        fname = gz_fname;
      } else {
        // no gz file - look for plain file
        if (stat(fname.c_str(), &stbuf) != 0)
          SDIE("File '", fname, "' cannot be accessed (either .gz or not): ", strerror(errno), "\n");
      }
      full_fnames.push_back(fname);
  }
  return full_fnames;
}

string remove_file_ext(const string &fname) {
  size_t lastdot = fname.find_last_of(".");
  if (lastdot == std::string::npos) return fname;
  return fname.substr(0, lastdot);
}

string get_basename(const string &fname) {
  size_t i = fname.rfind('/', fname.length());
  if (i != string::npos) return(fname.substr(i + 1, fname.length() - i));
  return fname;
}

int64_t get_file_size(string fname) {
  struct stat s;
  if (stat(fname.c_str(), &s) != 0) return -1;
  return s.st_size;
}

//
// formatting methods
//
string get_size_str(int64_t sz)
{
  ostringstream oss;
  oss << std::fixed << std::setprecision(2);
  if (sz >= ONE_TB) oss << ((double)sz / (ONE_TB)) << "TB";
  else if (sz >= ONE_GB) oss << ((double)sz / (ONE_GB)) << "GB";
  else if (sz >= ONE_MB) oss << ((double)sz / (ONE_MB)) << "MB";
  else if (sz >= ONE_KB) oss << ((double)sz / (ONE_KB)) << "KB";
  else oss << sz << "B";
  return oss.str();
}

string get_float_str(double fraction, int precision) {
  std::stringstream ss;
  ss << std::setprecision(precision) << std::fixed << fraction;
  return ss.str();
}

string perc_str(int64_t num, int64_t tot) {
  ostringstream os;
  os.precision(2);
  os << std::fixed;
  os << num << " (" << 100.0 * num / tot << "%)";
  return os.str();
}

string get_current_time(bool fname_fmt) {
  auto t = std::time(nullptr);
  std::ostringstream os;
  if (!fname_fmt) os << std::put_time(localtime(&t), "%D %T");
  else os << std::put_time(localtime(&t), "%y%m%d%H%M%S");
  return os.str();
}

vector<string> split(const string &s, char delim)
{
  std::vector<string> elems;
  std::stringstream ss(s);
  string token;
  while (std::getline(ss, token, delim)) elems.push_back(token);
  return elems;
}

void find_and_replace(std::string& subject, const std::string& search, const std::string& replace) {
  size_t pos = 0;
  while ((pos = subject.find(search, pos)) != std::string::npos) {
    subject.replace(pos, search.length(), replace);
    pos += replace.length();
  }
}

std::string_view substr_view(const std::string &s, size_t from, size_t len) {
  if (from>=s.size()) return {};
  return std::string_view(s.data() + from, std::min(s.size() - from,len));
}


void replace_spaces(string &s) {
  for (int i = 0; i < s.size(); i++)
    if (s[i] == ' ') s[i] = '_';
}

string tail(const string &s, int n) {
  return s.substr(s.size() - n);
}

string head(const string &s, int n) {
  return s.substr(0, n);
}

}; // namespace upcxx_utils
