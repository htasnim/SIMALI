#include <ctime>
#include <chrono>
#include <upcxx/upcxx.hpp>
#include <iomanip>

#define _TIMERS_CPP
#include "upcxx_utils/timers.hpp"

namespace upcxx_utils {

  //
  // Timings
  //

  future<> &Timings::get_last_pending() {
    static future<> _ = make_future();
    return _;
  }

  Timings::Timings() : before_elapsed(0.0), after_elapsed(0.0), before_sum(0.0), after_sum(0.0), count_sum(0.0),
          instance_sum(0), before_min(0.0), after_min(0.0), count_min(0.0), instance_min(0), before_max(0.0), after_max(0.0),
          count_max(0.0), instance_max(0.0), reduction_elapsed(0.0), my_count(0), my_instance(0), t() {
  }

  future<> Timings::get_pending() {
    return get_last_pending();
  }

  void Timings::set_pending(future<> fut) {
    get_last_pending() = when_all(get_last_pending(), fut);
  }

  void Timings::wait_pending() {
    DBG(__func__, "\n");
    get_last_pending().wait();
    get_last_pending() = make_future();
  }

  string Timings::to_string(bool print_count) const {
    ostringstream os;
    os << "(min/my/avg/max, bal)";
    os << std::setprecision(2) << std::fixed;
    // print the timing metrics
    if (before_max > 0.0) {
      double bal = (before_max > 0.0 ? before_sum/rank_n()/before_max : 1.0);
      if (before_max > 10.0 && bal < .9) os << KLRED; // highlight large imbalances
      os << " (" << before_min << "/" << before_elapsed << "/" << before_sum/rank_n() << "/" << before_max << " s, " << bal << ")";
      if (before_max > 1.0 && bal < .9) os << KLCYAN;
    } else {
      os << " No-Time";
    }

    os << std::setprecision(1) << std::fixed;

    // print the timings around a barrier if they are significant
    if (after_max >= 0.1) {
      os << (after_max > 1.0 ? KLRED : "") << " barrier(" << after_min << "/" << after_elapsed << "/" << after_sum/rank_n() << "/" << after_max << " s, " << (after_max > 0.0 ? after_sum/rank_n()/after_max : 0.0) << ")" << (after_max > 1.0 ? KLCYAN : "");
    } else if (after_max > 0.0) {
      os << std::setprecision(2) << std::fixed;
      os << " barrier(" << after_max << " s)";
      os << std::setprecision(1) << std::fixed;
    }

    // print the max_count if it is more than 1 or more than 0 if asked to print the count
    if (count_max > (print_count ? 0.0 : 1.00001)) os << " count(" << count_min << "/" << my_count << "/" << count_sum/rank_n() << "/" << count_max << ", " << (count_max > 0.0 ? count_sum/rank_n()/count_max : 0.0) << ")";
    // print the instances if it is both non-zero and not 1 per rank
    if (instance_sum > 0 && ((int) (instance_sum+0.01)) != rank_n() && ((int) (instance_sum+0.99)) != rank_n() ) os << " instance(" << instance_min << "/" << my_instance << "/" << instance_sum/rank_n() << "/" << instance_max << ", " << (instance_max > 0.0 ? instance_sum/rank_n()/instance_max : 0.0) << ")";
    // print the reduction timings if they are significant
    if (reduction_elapsed > 0.05) os << (reduction_elapsed > .5 ? KLRED : "") << " reduct(" << reduction_elapsed << ")" << (reduction_elapsed > .5 ? KLCYAN : "");
    return os.str();
  }

  void Timings::set_before(Timings &timings, size_t count, double elapsed, size_t instances) {
    DBG("set_before: my_count=", count, " my_elapsed=", elapsed, " instances=", instances, "\n");
    timings.before = std::chrono::high_resolution_clock::now() ;

    timings.my_count = count;
    timings.count_sum = timings.count_min = timings.count_max = timings.my_count;

    timings.before_elapsed = elapsed;
    timings.before_sum = timings.before_min = timings.before_max = timings.before_elapsed;

    timings.my_instance = instances;
    timings.instance_sum = timings.instance_min = timings.instance_max = instances;

  }

  // timings must remain in scope until the returened future is ready()
  future<> Timings::set_after(Timings &timings, std::chrono::time_point<std::chrono::high_resolution_clock> t_after) {
    timings.after = t_after;
    std::chrono::duration<double> interval = timings.after - timings.before;
    timings.after_elapsed = interval.count();
    timings.after_sum = timings.after_max = timings.after_elapsed;
    DBG("set_after: ", interval.count(), "\n");

    // time the reductions
    timings.t = t_after;

    // FIXME can this be reduce one and save some network traffic??
    auto fut_sum_elapsed = reduce_all(&timings.before_sum, &timings.before_sum, 4, op_fast_add);
    auto fut_min_elapsed = reduce_all(&timings.before_min, &timings.before_min, 4, op_fast_min);
    auto fut_max_elapsed = reduce_all(&timings.before_max, &timings.before_max, 4, op_fast_max);
    auto ret = when_all(fut_min_elapsed, fut_max_elapsed, fut_sum_elapsed).then(
           [&timings]()
           {
               std::chrono::duration<double> interval = std::chrono::high_resolution_clock::now() - timings.t;
               timings.reduction_elapsed = interval.count();
               DBG("Finished reductions:, ", interval.count(), "\n");
           });

    set_pending( when_all(ret, get_pending()) );
    return ret;
  }

  // barrier and reduction
  Timings Timings::barrier(size_t count, double elapsed, size_t instances) {
    DBG("Timings::barrier(", count, ", ", elapsed, ", ", instances, "\n");
    Timings timings;
    set_before(timings, count, elapsed, instances);
    upcxx::barrier();
    progress(); // explicitly make progress after the barrier if the barrier itself was already ready()
    auto fut = set_after(timings);
    wait_pending();
    assert(fut.ready());
    return timings;
  }

  void Timings::print_barrier_timings(string label) {
    Timings timings = barrier(0,0,0);
    wait_pending();
    SLOG_VERBOSE(KLCYAN, " - Elapsed time for barrier at ", label, ": ", timings.to_string(), " - \n", KNORM);
  }

  // no barrier but a future reduction is started
  future< shared_ptr<Timings> > Timings::reduce(size_t count, double elapsed, size_t instances) {
    DBG("Timings::reduce(", count, ", ", elapsed, ", ", instances, "\n");
    auto timings = make_shared<Timings>();
    set_before(*timings, count, elapsed, instances);
    auto future_reduction = set_after(*timings, timings->before); // after == before, so no barrier info will be output
    return when_all( make_future( timings ), future_reduction, get_pending() );
  }

  void Timings::print_reduce_timings(string label) {
    future< shared_ptr<Timings> > fut_timings = reduce(0,0,0);
    auto fut = when_all(fut_timings, to_future(label), get_pending()).then(
      [](shared_ptr<Timings> shptr_timings, string label)
      {
        SLOG_VERBOSE(KLCYAN, " - Elapsed time for ", label, ": ", shptr_timings->to_string(), " - \n", KNORM);
      });
    set_pending( fut );
  }


  //
  // BaseTimer
  //

  size_t &BaseTimer::instance_count() {
    static size_t _ = 0;
    return _;
  }

  void BaseTimer::increment_instance() {
    ++instance_count();
  }
  void BaseTimer::decrement_instance() {
    instance_count()--;
  }
  size_t BaseTimer::get_instance_count() {
    return instance_count();
  }


  BaseTimer::BaseTimer(const string &_name)
    : t()
    , name(_name)
    , t_elapsed(0.0)
    , count(0)
  {
  }

  BaseTimer::~BaseTimer() { }

  void BaseTimer::clear() {
    t = timepoint_t();
    t_elapsed = 0.0;
    count = 0;
  }

  void BaseTimer::start() {
    t = now();
  }

  void BaseTimer::stop() {
    double elapsed = get_elapsed_since_start();
    t = std::chrono::time_point<std::chrono::high_resolution_clock>(); // reset to 0
    //DBG("stop(", name, ", inst=", get_instance_count(), "): ", elapsed, " s, ", now_str(), "\n");
    t_elapsed += elapsed;
    count++;
  }

  double BaseTimer::get_elapsed() const {
    return t_elapsed;
  }

  double BaseTimer::get_elapsed_since_start() const {
    std::chrono::duration<double> interval = now() - t;
    return interval.count();
  }

  size_t BaseTimer::get_count() const {
    return count;
  }

  const string &BaseTimer::get_name() const {
    return name;
  }

  void BaseTimer::done() const {
    SLOG_VERBOSE(KLCYAN, get_indent(), name, " took ", std::setprecision(2), std::fixed, t_elapsed, " s ", get_indent(),
                 KNORM, "\n");
    DBG(get_indent(), " ", name, " took ", std::setprecision(2), std::fixed, t_elapsed, " s ", get_indent(), "\n");
  }

  void BaseTimer::done_all() const {
    auto max_t_elapsed = upcxx::reduce_one(t_elapsed, upcxx::op_fast_max, 0).wait();
    auto avg_t_elapsed = upcxx::reduce_one(t_elapsed, upcxx::op_fast_add, 0).wait() / upcxx::rank_n();
    SLOG_VERBOSE(KLCYAN, get_indent(), " ", name, " took ", std::setprecision(2), std::fixed, " avg ", avg_t_elapsed,
                 " s max ", max_t_elapsed, " s balance ", (avg_t_elapsed / max_t_elapsed), " ", get_indent(), KNORM, "\n");
    DBG(get_indent(), " ", name, " took ", std::setprecision(2), std::fixed, t_elapsed, " s ", get_indent(), "\n");
  }

  string BaseTimer::get_final() const {
    ostringstream os;
    os << name << ": " << std::setprecision(2) << std::fixed << t_elapsed;
    return os.str();
  }

  future< shared_ptr<Timings> > BaseTimer::reduce_timings(size_t my_instances) const {
    return reduce_timings(count, t_elapsed, my_instances);
  }

  future< shared_ptr<Timings> > BaseTimer::reduce_timings(size_t my_count, double my_elapsed, size_t my_instances) {
    return Timings::reduce(my_count, my_elapsed, my_instances);
  }

  Timings BaseTimer::barrier_timings(size_t my_instances) const {
    return barrier_timings(count, t_elapsed, my_instances);
  }

  Timings BaseTimer::barrier_timings(size_t my_count, double my_elapsed, size_t my_instances) {
    return Timings::barrier(my_count, my_elapsed, my_instances);
  }

  timepoint_t BaseTimer::now() {
    return std::chrono::high_resolution_clock::now();
  }

  string BaseTimer::now_str() {
    std::time_t result = std::time(nullptr);
    char buffer[100];
    size_t sz = strftime(buffer,sizeof(buffer),"%Y-%m-%d %H:%M:%S", std::localtime(&result) );
    return string( sz > 0 ? buffer : "BAD TIME" );
  }

  // returns the indent nesting depending on how many nested BaseTimers are active
  string BaseTimer::get_indent(int indent) {
    if (indent < 0) indent = 1 + get_instance_count() * 2;
    return string(" ") + string("----------------").substr(0, indent) + string(" ");
  }


  //
  // StallTimer
  //

  StallTimer::StallTimer(const string _name, double _max_seconds, int64_t _max_count)
    : BaseTimer(_name)
    , max_seconds(_max_seconds)
    , max_count(_max_count)
  {
    start();
  }

  StallTimer::~StallTimer() {
    stop();
  }

  void StallTimer::check() {
    stop();
    bool print = false;
    if (max_seconds > 0.0 && t_elapsed > max_seconds) {
      print = true;
    } else if (max_count > 0 && count > max_count) {
      print = true;
    }
    if (print) {
      WARN("StallTimer - ", name, " on ", rank_me(), " stalled for ", t_elapsed, " s and ", count, " iterations\n");
      max_seconds *= 2.0;
      max_count *= 2;
    }
    start();
  }


  //
  // IntermittentTimer
  //

  IntermittentTimer::IntermittentTimer(const string &_name, string _interval_label)
  : BaseTimer(_name)
  , t_interval(0.0)
  , interval_label(_interval_label)
  { }

  IntermittentTimer::~IntermittentTimer() { }

  void IntermittentTimer::start_interval() {
  }

  void IntermittentTimer::stop_interval() {
    t_interval = get_elapsed_since_start();
    if (!interval_label.empty()) {
      ostringstream oss;
      oss << KBLUE << std::left << std::setw(40) << interval_label << std::setprecision(2) << std::fixed << t_interval
          << " s" << KNORM << "\n";
      if (_verbose) SLOG_VERBOSE(oss.str());
      else SLOG(oss.str());
    }
  }

  void IntermittentTimer::print_out() {
    string indent = get_indent();
    future< shared_ptr<Timings>> fut_shptr_timings = reduce_timings();
    auto fut = when_all(Timings::get_pending(), fut_shptr_timings, to_future(name), to_future(count), to_future(indent)).then(
      [](shared_ptr<Timings> shptr_timings, string __name, size_t count, string indent)
      {
        if (shptr_timings->count_max > 0.0)
          SLOG_VERBOSE(KLCYAN, indent, "Elapsed time for ", __name, ": ", count, " intervals ", shptr_timings->to_string(true),
                       indent, "\n", KNORM);
      });
    Timings::set_pending( fut );
    count = 0;
    t_elapsed = 0.0;
  }


  //
  // ProgressTimer
  //

  ProgressTimer::ProgressTimer(const string &_name): BaseTimer(_name), calls(0) { }

  ProgressTimer::~ProgressTimer() { }

  void ProgressTimer::progress(size_t run_every) {
    if (run_every != 1 && ++calls % run_every != 0) return;
    start();
    upcxx::progress();
    stop();
    //DBG("ProgressTimer(", name, ") - ", t_elapsed, "\n");
  }

  void ProgressTimer::discharge(size_t run_every) {
    if (run_every != 1 && ++calls % run_every != 0) return;
    start();
    upcxx::discharge();
    upcxx::progress();
    stop();
    //DBG("ProgressTimer(", name, ").discharge() - ", t_elapsed, "\n");
  }

  void ProgressTimer::print_out() {
    string indent = get_indent();
    future< shared_ptr<Timings> > fut_shptr_timings = reduce_timings();
    auto fut = when_all(Timings::get_pending(), fut_shptr_timings, to_future(indent), to_future(name)).then(
      [](shared_ptr<Timings> shptr_timings, string indent, string __name)
      {
        if (shptr_timings->count_max > 0.0) SLOG_VERBOSE(KLCYAN, indent, "Elapsed time for progress() in ", __name, ": ", shptr_timings->to_string(true), indent, "\n", KNORM);
      });
    Timings::set_pending( fut );
    count = 0;
    t_elapsed = 0.0;
  }


  //
  // Timer
  //

  Timer::Timer(const string &_name): indent(), BaseTimer(_name) {
    increment_instance();
    indent = get_indent();
    auto fut = when_all(Timings::get_pending(), make_future(name, now_str(), indent)).then(
      [](string __name, string now, string indent)
      {
//        SLOG_VERBOSE(KLCYAN, indent, "Timing ", __name, " at ", now, indent, "\n", KNORM);
      });
    Timings::set_pending(fut);
    start();
  }

  Timer::~Timer() {
    stop();
    future< shared_ptr<Timings> > fut_shptr_timings = reduce_timings();
    auto fut = when_all(Timings::get_pending(), fut_shptr_timings, make_future(name, indent)).then(
      [](shared_ptr<Timings> shptr_timings, string __name, string indent) {
        SLOG_VERBOSE(KLCYAN, indent, "Elapsed time for ", __name, ": ", shptr_timings->to_string(), indent, "\n", KNORM);
      });
    Timings::set_pending(fut);
    decrement_instance();
  }


  // BarrierTimer
  BarrierTimer::BarrierTimer(const string &_name, bool _entrance_barrier, bool _exit_barrier)
    : indent()
    , exit_barrier(_exit_barrier)
    , BaseTimer(_name)
  {
    increment_instance();
    if (!_entrance_barrier && !exit_barrier) SLOG_VERBOSE("Why are we using a BarrierTimer without any barriers???\n");
    indent = get_indent();
    if (_entrance_barrier) {
      auto fut = when_all(Timings::get_pending(), make_future(name, now_str(), indent)).then(
        [](string __name, string now, string indent)
        {
//          SLOG_VERBOSE(KLCYAN, indent, "Timing ", __name, ":  (entering barrier) ", now, " ...\n", KNORM);
        });
      Timings::set_pending(fut);
      auto timings = barrier_timings();
      Timings::wait_pending(); // should be noop
      SLOG_VERBOSE(KLCYAN, string("                      ").substr(0,indent.size()+4), " ... ", timings.to_string(), indent, "\n", KNORM);
    } else {
      auto fut = when_all(Timings::get_pending(), make_future(name, now_str(), indent)).then(
        [](string __name, string now, string indent)
        {
//          SLOG_VERBOSE(KLCYAN, indent, "Timing ", __name, ": ", now, " ", indent, "\n", KNORM);
        });
      Timings::set_pending(fut);
    }
    start();
  }

  BarrierTimer::~BarrierTimer() {
    stop();
    if (exit_barrier) {
      auto fut = when_all(Timings::get_pending(), make_future(name, now_str(), indent)).then(
        [](string __name, string now, string indent)
        {
          SLOG_VERBOSE(KLCYAN, indent, "Elapsed time for ", __name, ":  (exiting barrier) ", now, " ...\n", KNORM);
        });
      Timings::set_pending(fut);
      auto timings = barrier_timings();
      Timings::wait_pending();
      SLOG_VERBOSE(KLCYAN, string("                      ").substr(0,indent.size()+4)," ... ", timings.to_string(), indent, "\n", KNORM);
    } else {
      future< shared_ptr<Timings> > fut_shptr_timings = reduce_timings();
      auto fut = when_all(Timings::get_pending(), fut_shptr_timings, make_future(name, indent)).then(
        [](shared_ptr<Timings> shptr_timings, string __name, string indent)
        {
          SLOG_VERBOSE(KLCYAN, indent, "Elapsed time for ", __name, ": ", shptr_timings->to_string(), indent, "\n", KNORM);
        });
      Timings::set_pending(fut);
    }
    decrement_instance();
  }

  //
  // ActiveCountTimer
  //

  ActiveCountTimer::ActiveCountTimer(const string _name)
    : total_elapsed(0.0)
    , total_count(0)
    , active_count(0)
    , max_active(0)
    , name(_name)
    , my_fut(make_future())
  {
  }

  ActiveCountTimer::~ActiveCountTimer() {
    my_fut.wait(); // keep alive until all futures have finished
  }

  void ActiveCountTimer::clear() {
    total_elapsed = 0.0;
    total_count = 0;
    active_count = 0;
    max_active = 0;
  }

  timepoint_t ActiveCountTimer::begin() {
    active_count++;
    if (max_active < active_count) max_active = active_count;
    return BaseTimer::now();
  }

  void ActiveCountTimer::end(timepoint_t t) {
    std::chrono::duration<double> interval = BaseTimer::now() - t;
    active_count--;
    total_count++;
    total_elapsed += interval.count();
  }

  void ActiveCountTimer::print_barrier_timings(string label) {
    Timings timings = BaseTimer::barrier_timings(total_count, total_elapsed, max_active);
    clear();
    Timings::wait_pending();
    print_timings(timings, label);
  }

  void ActiveCountTimer::print_reduce_timings(string label) {
    label = name + label;
    auto fut_timings = BaseTimer::reduce_timings(total_count, total_elapsed, max_active);
    auto _this = this;
    auto fut_clear = fut_timings.then(
      [_this]( shared_ptr<Timings> ignored )
      {
        _this->clear();
      });
    auto fut = when_all( Timings::get_pending(), fut_timings, fut_clear ).then(
      [_this, label]( shared_ptr<Timings> shptr_timings)
      {
        _this->print_timings(*shptr_timings, label);
      });
    Timings::set_pending(fut);
    my_fut = when_all(fut_clear, my_fut, fut); // keep this in scope until clear has been called...
  }

  void ActiveCountTimer::print_timings(Timings &timings, string label) {
    label = name + label;
    DBG(__func__, " label=", label, "\n");
    if (active_count > 0)
      SWARN("print_timings on ActiveCountTimer '", label, "' called while ", active_count, " (max ", max_active,
            ") are still active\n");
    string indent = "--";
    if (timings.count_max > 0.0) {
      SLOG_VERBOSE(KLCYAN, indent, "Elapsed time for instances of ", label, ": ",
                   (timings.count_max > 0.0 ? timings.to_string(true) : string("(none)")), indent, "\n", KNORM); }
  }

  ActiveCountTimer _GenericActiveCountTimer("_upcxx_dummy");
  GenericInstantiationTimer _GenericInstantiationTimer(_GenericActiveCountTimer);
  template class ActiveInstantiationTimer<_upcxx_utils_dummy>;

  SingletonInstantiationTimer _SingletonInstantiationTimer();
  template class InstantiationTimer<_upcxx_utils_dummy>;


}; // namespace upcxx_utils
