#include <iostream>
#include <upcxx/upcxx.hpp>

#include "upcxx_utils/version.h"
#include "upcxx_utils/flat_aggr_store.hpp"
#include "upcxx_utils/log.hpp"

#include <unordered_map>

    struct KV {
        char key;
        int val;
        UPCXX_SERIALIZED_FIELDS(key, val);
    };


int main(int argc, char **argv) {
    upcxx::init();

    if (!upcxx::rank_me())
        SOUT(argv[0], ": Found upcxx_utils version ", UPCXX_UTILS_VERSION_DATE
            , " on ", UPCXX_UTILS_BRANCH, " with ", upcxx::rank_n(), " procs.", "\n");

    using map_t = upcxx::dist_object< std::unordered_map< char, int > >;

    for (int i = 0; i < 2; i++) {
        upcxx::barrier();
        map_t myMap(upcxx::world());

        upcxx_utils::FlatAggrStore<KV, map_t&> flatStore(myMap);
        flatStore.set_size("char counter", i * 128 * upcxx::rank_n(), 100, i!=0);
        flatStore.set_update_func(
                [](KV kv, map_t & m) {
                    const auto it = m->find(kv.key);
                    if (it == m->end()) {
                        m->insert({kv.key, kv.val});
                    } else {
                        it->second += kv.val;
                    }
                });


        string data("The quick brown fox jumped over the lazy dog's tail...");

        for (char &c : data) {
            KV kv = {c, 1};
            flatStore.update(((int) c) % upcxx::rank_n(), kv);
        }
        flatStore.flush_updates();
        int count = 0;
        for (auto &kv : *myMap) {
            int exp = 0;
            switch (kv.first) {
                case '.': exp = 3; break;
                case ' ': exp = 9; break;
                case 'r': case 'd': case 'u': case 'a': case 'h': case 'i': case 't': case 'l': exp = 2; break;
                case 'e': case 'o': exp = 4; break;
                default: exp = 1;
            }
            assert(kv.second == exp * upcxx::rank_n());
            count++;
            OUT("rank=", upcxx::rank_me(), " c='", kv.first, "' ", kv.second, "\n");
        }
        int total = upcxx::reduce_one(count, upcxx::op_fast_add, 0).wait();
        if (!upcxx::rank_me()) assert(total == 30);
    }

    upcxx::finalize();
    return 0;
}
