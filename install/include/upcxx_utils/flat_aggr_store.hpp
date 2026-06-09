#pragma once

#include <algorithm>
#include <upcxx/upcxx.hpp>
#include <string>

#include "log.hpp"
#include "bin_hash.hpp"
#include "heavy_hitter_streaming_store.hpp"

using std::string;
using std::to_string;

using upcxx::intrank_t;
using upcxx::rank_me;
using upcxx::rank_n;
using upcxx::barrier;
using upcxx::dist_object;
using upcxx::rpc;
using upcxx::reduce_one;
using upcxx::reduce_all;
using upcxx::op_fast_add;
using upcxx::op_fast_max;
using upcxx::progress;
using upcxx::make_view;
using upcxx::view;
using upcxx::future;
using upcxx::make_future;
using upcxx::promise;

//#define USE_HH

namespace upcxx_utils {

    // this class aggregates updates into local buffers and then periodically does an rpc to dispatch them

    template<typename T, typename... Data>
    class FlatAggrStore {
        using RankStore = vector<T>;
        using Store = vector<RankStore>;
        using UpdateFunc = std::function<void(T, Data&...)>;
#ifdef USE_HH
        using HHStore = HeavyHitterStreamingStore<T>;
        HHStore hh_store;
#endif
        Store store;
        int64_t max_store_size_per_target;
        int max_rpcs_in_flight;
        vector<int64_t> rpcs_sent;
        int64_t tot_rpcs_sent;
        dist_object<vector<int64_t>> rpcs_expected;
        dist_object<vector<int64_t>> rpcs_processed;

        // save the update function to use in both update and flush
        dist_object<UpdateFunc> update_func = dist_object<UpdateFunc>({});
        // save all associated data structures as a tuple of a variable number of parameters
        std::tuple<Data&...> data;

        static void wait_for_rpcs(FlatAggrStore *astore, intrank_t target_rank) {
            assert(target_rank < rank_n());
            astore->tot_rpcs_sent++;
            astore->rpcs_sent[target_rank]++;
            // limit the number in flight by making sure we don't have too many more sent than received (with good load balance,
            // every process is sending and receiving about the same number)
            // we don't actually want to check every possible rank's count while waiting, so just check the target rank
            if (astore->max_rpcs_in_flight) {
              auto rpcs_sent_per_rank = astore->tot_rpcs_sent / rank_n();
              while (rpcs_sent_per_rank - (*astore->rpcs_processed)[target_rank] > astore->max_rpcs_in_flight) progress();
            }
        }

        // operates on a vector of elements in the store

        static void update_remote(FlatAggrStore *astore, intrank_t target_rank, Data &...data) {
#ifdef USE_HH
            // get any active heavy hitters for this target and flush it
            auto hh = astore->hh_store.retrieve(target_rank);
            // no need to send an empty rpc
            if (hh.empty() && astore->store[target_rank].empty()) return;
#else
            if (astore->store[target_rank].empty()) return;
#endif
            wait_for_rpcs(astore, target_rank);
            rpc_ff(target_rank,
                   [](dist_object<UpdateFunc> &update_func,
                      view<T> rank_store,
#ifdef USE_HH
                      view< std::pair<T, uint8_t> > hh_store,
#endif
                      dist_object< vector<int64_t> > &rpcs_processed,
                      intrank_t source_rank,
                      Data &...data) {
                     (*rpcs_processed)[source_rank]++;
                     for (auto elem : rank_store) {
                       (*update_func)(elem, data...);
                     }
#ifdef USE_HH
                     for (auto hh_elem_count : hh_store) {
                       for (int i = 0; i < hh_elem_count.second; i++) {
                         (*update_func)(hh_elem_count.first, data...);
                       }
                     }
#endif
                   },
                   astore->update_func,
                   make_view(astore->store[target_rank].begin(), astore->store[target_rank].end()),
#ifdef USE_HH
                   make_view(hh.begin(), hh.end()),
#endif
                   astore->rpcs_processed,
                   rank_me(),
                   data...);
            astore->store[target_rank].clear();
        }

        // operates on a single element

        static void update_remote1(FlatAggrStore *astore, intrank_t target_rank, const T &elem, Data &...data) {
            wait_for_rpcs(astore, target_rank);
            rpc_ff(target_rank,
                    [](dist_object<UpdateFunc> &update_func,
                    T elem,
                    dist_object< vector<int64_t> > &rpcs_processed,
                    intrank_t source_rank,
                    Data &...data) {
                        (*rpcs_processed)[source_rank]++;
                        (*update_func)(elem, data...);
                    },
                    astore->update_func,
                    elem,
                    astore->rpcs_processed,
                    rank_me(),
                    data...);
        }

    public:

        FlatAggrStore(Data&... data)
        : store({})
#ifdef USE_HH
        , hh_store({})
#endif
        , max_store_size_per_target(0)
        , rpcs_sent({})
        , tot_rpcs_sent(0)
        , rpcs_expected({})
        , rpcs_processed({})
        , max_rpcs_in_flight(0)
        , data(data...) {
            rpcs_sent.resize(upcxx::rank_n(), 0);
            rpcs_processed->resize(upcxx::rank_n(), 0);
            rpcs_expected->resize(upcxx::rank_n(), 0);
        }

        virtual ~FlatAggrStore() {
            clear();
            Store().swap(store);
        }

        void set_size(const string &desc, int64_t max_store_bytes, int64_t max_rpcs_in_flight = 128,
                      bool use_heavy_hitters = true) {
            this->max_rpcs_in_flight = max_rpcs_in_flight;
            store.resize(rank_n(), {});
            // at least 10 entries per target rank
            max_store_size_per_target = max_store_bytes / sizeof(T) / rank_n();
            if (max_store_size_per_target < 10)  {
                if (max_store_size_per_target < 2) max_store_size_per_target = 0;
                SWARN("FlatAggrStore max_store_size_per_target is small (", max_store_size_per_target,
                      ") please consider increasing the max_store_bytes (", get_size_str(max_store_bytes), ")\n");
                SWARN("at this scale of ", upcxx::rank_n(), " ranks, at least ", get_size_str(10*sizeof(T)*rank_n()),
                      " is necessary for good performance\n");
            }
            SLOG_VERBOSE(desc, ": using an aggregating store for each rank of max ", get_size_str(max_store_bytes / rank_n()),
                 " per target rank\n");
            SLOG_VERBOSE("  max ", max_store_size_per_target, " entries of ", get_size_str(sizeof(T)), " per target rank\n");
            SLOG_VERBOSE("  max RPCs in flight: ",
                         (!max_rpcs_in_flight ? string("unlimited") : to_string(max_rpcs_in_flight)), "\n");
#ifdef USE_HH
            if (use_heavy_hitters) {
                // allocate heavy hitters approx the size of the RankStore for one more node in the job
                hh_store.reserve(max_store_size_per_target * upcxx::local_team().rank_n());
            }
#endif
            barrier();
        }

        void set_update_func(UpdateFunc update_func) {
            *(this->update_func) = update_func;
            barrier(); // to avoid race of first update
        }

        void clear() {
            for (auto s : store) {
                if (!s.empty()) throw string("rank store is not empty!");
            }
            for (int i = 0; i < rpcs_sent.size(); i++) {
                rpcs_sent[i] = 0;
                (*rpcs_processed)[i] = 0;
                (*rpcs_expected)[i] = 0;
            }
            tot_rpcs_sent = 0;
#ifdef USE_HH
            hh_store.clear();
#endif
        }

        void update(intrank_t target_rank, const T &elem) {
            if (max_store_size_per_target > 1) {
#ifdef USE_HH
                T new_elem;
                if (hh_store.update(target_rank, elem, new_elem)) return;
                store[target_rank].push_back(new_elem);
#else
                store[target_rank].push_back(elem);
#endif
                if (store[target_rank].size() < max_store_size_per_target) return;
                std::apply(update_remote, std::tuple_cat(std::make_tuple(this, target_rank), data));
            } else {
                assert(max_store_size_per_target == 0);
                std::apply(update_remote1, std::tuple_cat(std::make_tuple(this, target_rank, elem), data));
            }
        }

        void flush_updates() {
            // when we update, every rank starts at a different rank to avoid bottlenecks
            upcxx::future<> base_fut = upcxx::make_future<>();
            for (int i = 0; i < rank_n(); i++) {
                intrank_t target_rank = (rank_me() + i) % rank_n();
                if (max_store_size_per_target > 0) {
                    std::apply(update_remote, std::tuple_cat(std::make_tuple(this, target_rank), data));
                }

                // tell the target how many rpcs we sent to it
                upcxx::future<> fut = rpc(target_rank,
                        [](dist_object<vector < int64_t>> &rpcs_expected, int64_t rpcs_sent, intrank_t source_rank) {
                            (*rpcs_expected)[source_rank] += rpcs_sent;
                        }, rpcs_expected, rpcs_sent[target_rank], rank_me());
                base_fut = upcxx::when_all(base_fut, fut);
            }
            base_fut.wait();
            barrier();
            int64_t tot_rpcs_processed = 0;
            // now wait for all of our rpcs.
            for (int i = 0; i < rpcs_expected->size(); i++) {
                while ((*rpcs_expected)[i] != (*rpcs_processed)[i]) progress();
                tot_rpcs_processed += (*rpcs_processed)[i];
            }
            SLOG_VERBOSE("Rank 0 sent ", tot_rpcs_sent, " rpcs and received ", tot_rpcs_processed, "\n");
            barrier();
            clear();
            barrier();
        }

    };

}; // namespace upcxx_utils
