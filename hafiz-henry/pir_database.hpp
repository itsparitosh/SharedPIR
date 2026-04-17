
#pragma once
#include <vector>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <x86intrin.h> 
#include <chrono>
#include <utility>

// Structure representing one block of the DB (3 data words + 1 augmented zero word)
struct DBRow {
    uint64_t words[4];
};

template<typename prgkey_t>
class PIRDatabase {
public:
    std::vector<DBRow> db;
    size_t nitems;

    PIRDatabase(size_t n) : nitems(n) {
        db.resize(n);
        // Initialize sequentially: Row 0 -> {1,2,3,0}, Row 1 -> {4,5,6,0}...
        for (size_t i = 0; i < n; ++i) {
            db[i].words[0] = i + 1;
            db[i].words[1] = i + 2;
            db[i].words[2] = i + 3;
            db[i].words[3] = 0; // this column is always 0
        }
    }

    // ============================================================================
    // READ OPERATION (Frobenius Inner Product with 2 DPFs)
    // ============================================================================
    boost::asio::awaitable<std::pair<uint64_t, double>> read_server(NetPeer& client) {
        std::vector<uint8_t> buf_L1, buf_L0;
        
        // 1. Receive L=2 DPF keys from the client (Excluded from server timing)
        co_await (client >> buf_L1);
        co_await (client >> buf_L0);

        std::vector<__m128i> eval(nitems); // Dummy payload array
        std::vector<uint8_t> flags_L1(nitems), flags_L0(nitems); // The actual t-bits

        // --- START SERVER RESPOND TIMER ---
        auto t_eval_start = std::chrono::high_resolution_clock::now();

        // Deserialize using __m128i payload to force 1-to-1 node-to-leaf mapping
        auto key_L1 = dpf::deserialize_dpf_key<__m128i, __m128i, prgkey_t>(buf_L1.data(), buf_L1.size());
        auto key_L0 = dpf::deserialize_dpf_key<__m128i, __m128i, prgkey_t>(buf_L0.data(), buf_L0.size());


        // 2. Expand both keys into full-domain bit vectors
        dpf::__evalinterval(key_L1, 0, nitems - 1, eval.data(), flags_L1.data());
        dpf::__evalinterval(key_L0, 0, nitems - 1, eval.data(), flags_L0.data());

        // 3. Frobenius inner product selection
        uint64_t response = 0; 

        for (size_t i = 0; i < nitems; ++i) {
            // Map the two DPF bits to a column index [0..3]
            uint8_t col = (flags_L1[i] << 1) | flags_L0[i];
            
            // XOR the chosen word into the response accumulator
            response ^= db[i].words[col]; 
        }

        // --- END SERVER RESPOND TIMER ---
        auto t_eval_end = std::chrono::high_resolution_clock::now();
        double eval_time = std::chrono::duration_cast<std::chrono::microseconds>(t_eval_end - t_eval_start).count() / 1000.0;

        co_return std::make_pair(response, eval_time);
    }
};
