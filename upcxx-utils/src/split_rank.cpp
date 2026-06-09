#include "upcxx_utils/split_rank.hpp"

#include <upcxx/upcxx.hpp>

using namespace upcxx;

//
// split_rank
//
  upcxx::team &split_rank::split_local_team() 
  {
#ifdef SPLIT_NOT_LOCAL
    static upcxx::team localTeam = upcxx::world().split( rank_me() / (rank_n() / SPLIT_NOT_LOCAL) , -1 );
    return localTeam;
#else // not defined SPLIT_NOT_LOCAL
  #ifdef DEBUG
    /* if the local team == world and >1 ranks, split the local team so there are 2 nodes in a debug run */
    static upcxx::team localTeam = upcxx::world().split( rank_me() / ( (upcxx::local_team().rank_n() == rank_n() && rank_n() > 1) ? (rank_n()/2) : upcxx::local_team().rank_n()), -1 );
    return localTeam;
  #else // not def DEBUG
    return upcxx::local_team();
  #endif // def DEBUG
#endif // def SPLIT_NOT_LOCAL
  }

  split_rank::split_rank(intrank_t world_rank): node(0), thread(0) {
    set(world_rank);
  }

  void split_rank::set(intrank_t world_rank) {
    node = get_node_from_rank(world_rank);
    thread = world_rank - node * split_local_team().rank_n();
    assert(world_rank == get_rank());
  }

  // returns the world rank
  intrank_t split_rank::get_rank() const {
    return node * split_local_team().rank_n() + thread;
  }

  // returns the node -1 .. nodes-2
  node_num_t split_rank::get_node() {
    return node;
  }

  // returns the thread 0 .. threads-1 or -1 if it is rank_me()
  thread_num_t split_rank::get_thread() {
    return thread;
  }

  bool split_rank::is_local() {
    return node == get_my_node();
  }

  // store some statics to cache often calculated data relative to me
  node_num_t split_rank::get_my_node() {
    static node_num_t _ = rank_me() / split_local_team().rank_n();
    return _;
  }

  thread_num_t split_rank::get_my_thread() {
    static thread_num_t _ = split_local_team().rank_me();
    return _;
  }

  intrank_t split_rank::get_first_local() {
    static intrank_t _ = get_my_node() * split_local_team().rank_n();
    return _;
  }

  intrank_t split_rank::get_last_local() {
    static intrank_t _ = get_first_local() + split_local_team().rank_n() - 1;
    return _;
  }

  node_num_t split_rank::get_node_from_rank(intrank_t target_rank) {
    node_num_t target_node = target_rank / split_local_team().rank_n();
    return target_node;
  }

  intrank_t split_rank::get_rank_from_thread(thread_num_t target_thread) {
    return get_first_local() + target_thread;
  }

  intrank_t split_rank::get_rank_from_node(node_num_t target_node) {
    return target_node * split_local_team().rank_n() + get_my_thread();
  }

  split_rank split_rank::from_thread(thread_num_t target_thread) {
    split_rank sr;
    sr.node = get_my_node();
    sr.thread = target_thread;
    assert(sr.get_rank() == get_rank_from_thread(target_thread));
    return sr;
  }

  split_rank split_rank::from_node(node_num_t target_node) {
    split_rank sr;
    sr.node = target_node;
    sr.thread = get_my_thread();
    assert(sr.get_rank() == get_rank_from_node(target_node));
    return sr;
  }

  node_num_t split_rank::num_nodes() {
    static node_num_t _ = rank_n() / split_local_team().rank_n();
    assert( _ * split_local_team().rank_n() == upcxx::rank_n());
    return _;
  }

  thread_num_t split_rank::num_threads() {
    static thread_num_t _ = split_local_team().rank_n();
    assert( _ * num_nodes() == upcxx::rank_n());
    return _;
  }

  bool split_rank::all_local_ranks_are_local_castable() {
    static bool _ = upcxx::local_team_contains( get_first_local() ) & upcxx::local_team_contains( get_last_local() );
    return _;
  }

  bool split_rank::all_ranks_are_local_castable() {
    static bool _ = upcxx::local_team_contains( 0 ) & upcxx::local_team_contains( upcxx::rank_n() - 1 );
    return _;
  }

