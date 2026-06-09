#pragma once

#include <ctime>
#include <chrono>
#include <upcxx/upcxx.hpp>

#include "version.h"
#include "colors.h"
#include "log.hpp"

using upcxx::barrier;
using upcxx::rank_n;
using upcxx::rank_me;
using upcxx::reduce_one;
using upcxx::reduce_all;
using upcxx::op_fast_add;
using upcxx::op_fast_min;
using upcxx::op_fast_max;
using upcxx::future;
using upcxx::make_future;
using upcxx::to_future;
using upcxx::progress;
using upcxx::when_all;

using std::shared_ptr;
using std::make_shared;
using std::string;
using std::stringstream;
using std::ostringstream;
using timepoint_t = std::chrono::time_point<std::chrono::high_resolution_clock>;

namespace upcxx_utils {

class Timings {
  std::chrono::time_point<std::chrono::high_resolution_clock> t;
  std::chrono::time_point<std::chrono::high_resolution_clock> before, after;
  static future<> &get_last_pending();

public:
  double before_elapsed, after_elapsed, before_sum, after_sum, count_sum, instance_sum, before_min, after_min, count_min, instance_min, before_max, after_max, count_max, instance_max, reduction_elapsed;
  size_t my_count, my_instance;

  Timings();

  static future<> get_pending();

  static void set_pending(future<> fut);

  static void wait_pending();

  string to_string(bool print_count = false) const;

  static void set_before(Timings &timings, size_t count, double elapsed, size_t instances = 0);

  // timings must remain in scope until the returened future is ready()
  static future<> set_after(Timings &timings, std::chrono::time_point<std::chrono::high_resolution_clock> after = std::chrono::high_resolution_clock::now());

  // barrier and reduction
  static Timings barrier(size_t count, double elapsed, size_t instances = 0);

  static void print_barrier_timings(string label);

  // no barrier but a future reduction is started
  static future< shared_ptr<Timings> > reduce(size_t count, double elapsed, size_t instances = 0);

  static void print_reduce_timings(string label);

};

class BaseTimer {
// Just times between start & stop, does not print a thing
// does not time construction / destruction

private:
  static size_t &instance_count();

protected:
  timepoint_t t;
  double t_elapsed;
  size_t count;
  string name;

  static void increment_instance();
  static void decrement_instance();
  static size_t get_instance_count();


public:
  BaseTimer(const string &_name);
  BaseTimer(const BaseTimer &copy) = default;
  BaseTimer(BaseTimer &&move) = default;

  virtual ~BaseTimer();

  void clear();

  void start();

  void stop();

  double get_elapsed() const;

  double get_elapsed_since_start() const;

  size_t get_count() const;

  const string &get_name() const;

  void done() const;

  void done_all() const;

  string get_final() const;

  future< shared_ptr<Timings> > reduce_timings(size_t my_instances = 0) const;

  static future< shared_ptr<Timings> > reduce_timings(size_t my_count, double my_elapsed, size_t my_instances = 0);

  Timings barrier_timings(size_t my_instances = 0) const;

  static Timings barrier_timings(size_t my_count, double my_elapsed, size_t my_instances = 0);

  static timepoint_t now();

  static string now_str();

  // returns the indent nesting depending on how many nested BaseTimers are active
  static string get_indent(int indent = -1);

};


class StallTimer : public BaseTimer {
// prints a Warning if called too many times or for too long. use in a while loop that could be indefinite
  double max_seconds;
  int64_t max_count;
public:
  StallTimer(const string _name, double _max_seconds = 60.0, int64_t _max_count = -1);
  virtual ~StallTimer();
  void check();

};



class IntermittentTimer : public BaseTimer {
// prints a summary on destruction
    double t_interval;
    string interval_label;
    void start_interval();
    void stop_interval();
public:
  IntermittentTimer(const string &name, string interval_label = "");

  virtual ~IntermittentTimer();

  void start() {
      start_interval();
      BaseTimer::start();
  }

  void stop() {
      stop_interval();
      BaseTimer::stop();
  }

  inline double get_interval() const {
      return t_interval;
  }

  void print_out();
};

class ProgressTimer : public BaseTimer {
private:
  size_t calls;
public:
  ProgressTimer(const string &name);

  virtual ~ProgressTimer();

  void progress(size_t run_every = 1);

  void discharge(size_t run_every = 1);

  void print_out();
};


class Timer : public BaseTimer {
// times between construction and destruction
// barrier and load balance calcs on destruction
  string indent;
public:
  Timer(const string &name);
  virtual ~Timer();
};

class BarrierTimer : public BaseTimer {
   bool exit_barrier;
   string indent;
public:
  BarrierTimer(const string &name, bool entrance_barrier = true, bool exit_barrier = true);
  virtual ~BarrierTimer();

};


class ActiveCountTimer {
protected:
  double total_elapsed;
  size_t total_count;
  size_t active_count;
  size_t max_active;
  string name;
  future<> my_fut;
public:
  ActiveCountTimer(const string _name = "");
  ~ActiveCountTimer();

  void clear();

  timepoint_t begin();

  void end(timepoint_t t);

  inline double get_total_elapsed() const { return total_elapsed; }
  inline size_t get_total_count() const { return total_count; }
  inline size_t get_active_count() const { return active_count; }
  inline size_t get_max_active_count() const { return max_active; }

  void print_barrier_timings(string label = "");

  void print_reduce_timings(string label = "");

  void print_timings(Timings &timings, string label = "");

};

template<typename Base>
class ActiveInstantiationTimerBase : public Base {
protected:
  ActiveCountTimer &_act;
  timepoint_t _t;

public:
  ActiveInstantiationTimerBase(ActiveCountTimer &act)
    : Base()
    , _act(act)
    , _t()
  {
     _t = act.begin();
  }

  ActiveInstantiationTimerBase(ActiveInstantiationTimerBase &&move)
    : Base( std::move( static_cast<Base&>(move) ) )
    , _act(move._act)
    , _t(move._t)
  {
    if (this != &move) move._t = {}; // delete timer for moved instance
  }

  virtual ~ActiveInstantiationTimerBase() {
    if (_t != timepoint_t() ) _act.end(_t);
  }

  double get_elapsed_since_start() const {
    std::chrono::duration<double> interval = BaseTimer::now() - this->_t;
    return interval.count();
  }

  // move but not copy
  ActiveInstantiationTimerBase(const ActiveInstantiationTimerBase &copy) = delete;


};

// to be used in inheritence to time all the instances of a class (like the duration of promises)
// to be used with an external ActiveContTimer
template<typename Base>
class ActiveInstantiationTimer : public ActiveInstantiationTimerBase<Base> {
public:
  ActiveInstantiationTimer(ActiveCountTimer &act)
    : ActiveInstantiationTimerBase<Base>(act)
  { }
  ActiveInstantiationTimer(const ActiveInstantiationTimer &copy) = delete;
  ActiveInstantiationTimer(ActiveInstantiationTimer &&move)
    : ActiveInstantiationTimerBase<Base>( std::move( static_cast<ActiveInstantiationTimerBase<Base>&>(move) ) )
  { }
  virtual ~ActiveInstantiationTimer() {}

  void print_barrier_timings(string label = "") { this->_act.print_barrier_timings(label); }
  void print_reduce_timings(string label = "") { this->_act.print_reduce_timings(label); }
  void print_timings(Timings &timings, string label = "") { this->_act.print_timings(timings, label); }
  double get_total_elapsed() const { return this->_act.get_total_elapsed(); }
  size_t get_total_count() const { return this->_act.get_total_count(); }
  size_t get_active_count() const { return this->_act.get_active_count(); }
  size_t get_max_active_count() const { return this-> _act.get_max_active_count(); }
  void clear() { return this->_act.clear(); }
};

// to be used in inheritence to time all the instances of a class (like the duration of promises)
// hold a static (by templated-class) specific ActiveCountTimer an external ActiveContTimer
// e.g. template <A,...> class my_timed_class : public my_class<A,...>, public InstantiationTimer<A,...> {};
// then when all instances have been destryed, call my_timed_class::print_barrier_timings();
template<typename Base, typename ... DistinguishingArgs>
class InstantiationTimer : public ActiveInstantiationTimerBase<Base> {
protected:
  static ActiveCountTimer &get_ACT() {
    static ActiveCountTimer _act = ActiveCountTimer();
    return _act;
   }

public:
  InstantiationTimer()
    : ActiveInstantiationTimerBase<Base>( get_ACT() )
  { }
  // move but not copy this timer
  InstantiationTimer(const InstantiationTimer &copy) = delete;
  InstantiationTimer(InstantiationTimer &&move)
    : ActiveInstantiationTimerBase<Base>( std::move( static_cast<ActiveInstantiationTimerBase<Base>&>(move) ) )
  { }

  virtual ~InstantiationTimer() { }

  static void print_barrier_timings(string label = "") { get_ACT().print_barrier_timings(label); }
  static void print_reduce_timings(string label) { get_ACT().print_reduce_timings(label); }
  static void print_timings(Timings &timings, string label = "") { get_ACT().print_timings(timings, label); }
  static size_t get_total_count() { return get_ACT().get_total_count(); }
  static size_t get_active_count() { return get_ACT().get_active_count(); }
  static void clear() { get_ACT().clear(); }

};

//
// speed up compile with standard implementations of the Instantiation Timers
//

struct _upcxx_utils_dummy {};

typedef ActiveInstantiationTimer<_upcxx_utils_dummy> GenericInstantiationTimer;

typedef InstantiationTimer<_upcxx_utils_dummy> SingletonInstantiationTimer;

#ifndef _TIMERS_CPP

// use extern templates (implemented in timers.cpp) to speed up compile
extern template class ActiveInstantiationTimer<_upcxx_utils_dummy>;
extern template class InstantiationTimer<_upcxx_utils_dummy>;

#endif


}; // namespace upcxx_utils