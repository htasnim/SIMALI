#pragma once
// heavy_hitter_streaming_store.hpp

#include "version.h"
#include "bin_hash.hpp"
#include "log.hpp"

#include <vector>

using std::vector;

namespace upcxx_utils {

    template<typename T>
    class HeavyHitterStreamingStore {
        // a stochastic Heavy Hitter accumulation class
        // aggregate and count identical updates of T to the same targetRank
        // retrieve a vector of T and the count for a given target rank
    public:
        using count_t = uint8_t; // count has 4 possible states:
        // 0 - observed once, will be replaced on next collision
        // 1 - observed once, no collisions
        // 2-254 - observed count times
        // 255 - observed 255 times, future observations will be deflected
        using t_count_t = std::pair<T, count_t>;
        using rank_t = upcxx::intrank_t;
        using rank_store_t = std::vector<rank_t>;
        using t_count_store_t = std::vector<t_count_t>;

        static const t_count_t &getEmptyRecord() {
            static t_count_t EMPTY_RECORD = t_count_t({}, 0);
            return EMPTY_RECORD;
        }

    private:
        rank_store_t rank_store;
        t_count_store_t t_count_store;
        size_t max_size;
        size_t reduced_records;
        size_t updates;

        // makeshift hash for the type T
        BinHash<T> hh_hash;

    public:
        HeavyHitterStreamingStore()
        : rank_store{}
        , t_count_store{}
        , max_size(0)
        , reduced_records(0)
        , updates(0) {
        }

        ~HeavyHitterStreamingStore() {
            clear();
        }

        void clear() {
            if (max_size) DBG("HH reduced=", reduced_records, " of updates=", updates, "\n");
            for (auto &rank : rank_store) {
                if (rank != upcxx::rank_n()) throw std::runtime_error("HeavyHitterStreamingStore is not empty");
            }
            rank_store_t().swap(rank_store);
            t_count_store_t().swap(t_count_store);
            max_size = reduced_records = updates = 0;
        }

        void swap(HeavyHitterStreamingStore &other) {
            std::swap(rank_store, other.rank_store);
            std::swap(t_count_store, other.t_count_store);
            std::swap(max_size, other.max_size);
            std::swap(updates, other.updates);
            std::swap(reduced_records, other.reduced_records);
        }

        // must be called with non-zero size to activate

        void reserve(size_t size) {
            if (size == 0) {
                clear();
                return;
            }
            max_size = size;
            rank_store.resize(max_size, upcxx::rank_n());
            t_count_store.resize(max_size, getEmptyRecord());

            SLOG_VERBOSE("Using HH cache of ", max_size, " buckets and ", get_size_str(max_size * (sizeof (upcxx::intrank_t) + sizeof (t_count_t))), " per rank\n");
        }

        // if returning true, countOnce is undefined and neither elem nor countOnce should be further counted
        // if returning false, countOnce needs to be counted once (may be different from elem)
        // O(1)

        bool update(upcxx::intrank_t targetRank, const T &elem, T &countOnce) {
            assert(max_size == rank_store.size());
            assert(max_size == t_count_store.size());
            updates++;
            if (max_size > 0) {
                size_t idx = hh_hash(elem) % max_size;
                rank_t &rank_bucket = rank_store[idx];
                if (rank_bucket == upcxx::rank_n()) {
                    // unused, so insert it
                    rank_bucket = targetRank;
                    t_count_t &t_count_bucket = t_count_store[idx];
                    t_count_bucket.first = elem;
                    t_count_bucket.second = 1;
                    return true;
                } else if (rank_bucket == targetRank) {
                    // used and destined for the same rank
                    t_count_t &t_count_bucket = t_count_store[idx];
                    if (memcmp(&elem, &t_count_bucket.first, sizeof (elem)) == 0) {
                        // same element to same rank
                        if (t_count_bucket.second == 0) {
                            // was counted once then contended once, keep it
                            t_count_bucket.second = 2;
                            return true;
                        } else if (t_count_bucket.second == 255) {
                            // overflow, deflect and set countOnce
                            countOnce = t_count_bucket.first;
                            return false;
                        } else {
                            // just count it
                            t_count_bucket.second++;
                            return true;
                        }
                    } else {
                        // different element to same rank
                        if (t_count_bucket.second == 0) {
                            // contended once already, so set countOnce to this recorded bucket
                            countOnce = t_count_bucket.first;
                            // and record the new elem
                            t_count_bucket.first = elem;
                            t_count_bucket.second = 1;
                            return false;
                        } else if (t_count_bucket.second == 1) {
                            // first contention, deflect elem
                            t_count_bucket.second = 0;
                            // deflect elem
                        } else {
                            // just deflect elem
                        }
                    }
                } // else different rank, so deflect elem
            } // else no store, so deflect elem
            // just deflect elem
            countOnce = elem;
            return false;
        }

        // gets any heavy hitters for target rank and removes from the HH store
        // O(n)

        t_count_store_t retrieve(upcxx::intrank_t targetRank) {
            assert(max_size == t_count_store.size());
            assert(max_size == rank_store.size());
            t_count_store_t retrieved;
            for (int idx = 0; idx < max_size; idx++) {
                rank_t &rank = rank_store[idx];
                if (rank == targetRank) {
                    t_count_t &t_count = t_count_store[idx];

                    // possibly fix count if contended once
                    if (t_count.second == 0) t_count.second++;

                    // store in retrieved vector
                    retrieved.push_back(t_count);
                    reduced_records += t_count.second - 1;

                    // clear this record
                    t_count = getEmptyRecord();
                    rank = upcxx::rank_n();
                }
            }
            DBG("HH retrieved ", retrieved.size(), " elements for rank=", targetRank, "\n");
            return retrieved;
        }

    };

}; // namespace upcxx_utils