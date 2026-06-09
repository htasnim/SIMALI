#pragma once
// split_rank.h

#include <upcxx/upcxx.hpp>
using namespace upcxx;

using node_num_t = uint32_t; /* i.e .0 to 4b */

// #define SPLIT_NOT_LOCAL 1 // Turn to 1 to enable a single tier (i.e 1 node in split_rank) -- with no inter-node transfers - all intra-node

//
// save space and use minimal integer to represent local threads
//
#ifdef SPLIT_NOT_LOCAL
  using thread_num_t = uint32_t; // needs to handle rank_n()
#else
  #if defined(HIPMER_MAX_LOCAL_THREADS) && HIPMER_MAX_LOCAL_THREADS > 255
    using thread_num_t = uint16_t; /* i.e. 0 to 64k */
  #else
    using thread_num_t = uint8_t; /* i.e. 0 to 255 */
  #endif
#endif // not defined SPLIT_NOT_LOCAL

// node and threads are the mappings between intrank_t divided and modulo by the local_team
// node is the inclusive range (0, (rank_n() / local_team().rank_n() - 1))
// thread is the inclusive range (0, (local_team().rank_n() - 1))
class split_rank {
  // get the node and thread indexes, excluding local node
  node_num_t node;
  thread_num_t thread;

public:
  static upcxx::team &split_local_team();

  split_rank(intrank_t world_rank = rank_me());

  void set(intrank_t world_rank);

  // returns the world rank
  intrank_t get_rank() const;

  // returns the node -1 .. nodes-2
  node_num_t get_node();

  // returns the thread 0 .. threads-1 or -1 if it is rank_me()
  thread_num_t get_thread();

  bool is_local();

  // store some statics to cache often calculated data relative to me
  static node_num_t get_my_node();

  static thread_num_t get_my_thread();

  static intrank_t get_first_local();

  static intrank_t get_last_local();

  // returns the node for a given rank
  static node_num_t get_node_from_rank(intrank_t target_rank);

  // return the intrank_t on this node given a thread
  static intrank_t get_rank_from_thread(thread_num_t target_thread);

  // returns the parallel thread on a remote node with the same split_local_team().rank_me()
  static intrank_t get_rank_from_node(node_num_t target_node);

  // returns the split_rank for the rank on this node with this thread (0 to split_local_team().rank_n() - 1)
  static split_rank from_thread(thread_num_t target_thread);

  // returns split_rank for the parallel thread on a remote node with the same split_local_team().rank_me()
  static split_rank from_node(node_num_t target_node);

  static node_num_t num_nodes();

  static thread_num_t num_threads();

  static bool all_local_ranks_are_local_castable();

  static bool all_ranks_are_local_castable();

};
