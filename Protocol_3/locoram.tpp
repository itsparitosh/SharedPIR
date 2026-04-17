
#pragma once
#include <iostream>
#include <vector>
#include <iomanip>
#include <random>
#include <algorithm>
#include <cstring> 
#include <chrono>
#include <optional>
#include <utility> 

#include "types.hpp"
#include "mpcops.hpp" 
#include "dpf.hpp"

using block_t = __m128i;

// ----------------------------------------------------------------------------
// Constructor
// ----------------------------------------------------------------------------
template<typename leaf_t, typename node_t, typename prgkey_t>
Locoram<leaf_t, node_t, prgkey_t>::Locoram(size_t n, size_t cols, Role role) : nitems(n), ncols(cols) {

    // We set up three different "tables" for the database.
    D.assign(6, std::vector<leaf_t>(n * cols, 0));
    Zeta.assign(6, std::vector<leaf_t>(n * cols, 0));
    Dt.assign(6, std::vector<leaf_t>(n * cols, 0));
}

// ============================================================================
// HELPER: WRITE Implementation
// ============================================================================
template<typename leaf_t, typename node_t, typename prgkey_t>
std::pair<double, double> update_impl(
    size_t nitems, size_t ncols, Role r,
    const std::vector<std::optional<dpf::dpf_key<uint64_t, node_t, prgkey_t>>>& keys,
    const std::vector<leaf_t>& payloads, 
    std::vector<std::vector<leaf_t>>& D,
    std::vector<std::vector<leaf_t>>& Zeta,
    std::vector<std::vector<leaf_t>>& Dt
) {

    // We process the database in small 256KB "chunks".
    // If we try to process millions of rows at once, the CPU cache gets overwhelmed.
    // Chunking keeps the CPU fast
    const size_t CHUNK_SIZE = 256 * 1024 / sizeof(leaf_t); 
    size_t blocks_needed = (CHUNK_SIZE * sizeof(leaf_t) + 15) / 16;

    std::vector<std::vector<block_t>> local_V_buf(keys.size(), std::vector<block_t>(blocks_needed));
    std::vector<uint8_t> local_flags(blocks_needed * 16);

    auto get_v = [&](size_t i) { return (const uint64_t*)local_V_buf[i].data(); };

    // --- Benchmarking Trackers ---
    double total_eval_ms = 0.0;
    double total_xor_ms = 0.0;

    // Loop through the database one chunk at a time
    for (size_t base = 0; base < nitems; base += CHUNK_SIZE) {
        size_t end = std::min(base + CHUNK_SIZE, nitems);
        size_t len = end - base;
        
        // --- 1. EVALUATION PHASE ---
        auto t_eval_start = std::chrono::high_resolution_clock::now();

        for(size_t i=0; i<keys.size(); ++i) {
            if(keys[i].has_value()) {
                dpf::__evalinterval(*keys[i], base, end - 1, (uint64_t*)local_V_buf[i].data(), local_flags.data());
            } else {
                std::memset(local_V_buf[i].data(), 0, len * sizeof(leaf_t)); // Fill with 0s if no key
            }
        }
        auto t_eval_end = std::chrono::high_resolution_clock::now();
        total_eval_ms += std::chrono::duration_cast<std::chrono::nanoseconds>(t_eval_end - t_eval_start).count() / 1000000.0;


        // --- 2. XOR / MERGE PHASE ---
        auto t_xor_start = std::chrono::high_resolution_clock::now();
        
        // Now we blend our payload data into the database.
        if (r == Role::P1) { 
            uint64_t* d0 = D[0].data() + base * ncols;  uint64_t* d2 = D[2].data() + base * ncols;  uint64_t* d4 = D[4].data() + base * ncols;  
            uint64_t* z1 = Zeta[1].data() + base * ncols; uint64_t* z3 = Zeta[3].data() + base * ncols; uint64_t* dt5 = Dt[5].data() + base * ncols;  
            const uint64_t* v0 = get_v(0); const uint64_t* v1 = get_v(1); const uint64_t* v2 = get_v(2);
            const uint64_t* v3 = get_v(3); const uint64_t* v4 = get_v(4); const uint64_t* v5 = get_v(5);

            for(size_t j=0; j<len; ++j) {
                for(size_t c=0; c<ncols; ++c) {
                    uint64_t m1 = payloads[0*ncols + c]; uint64_t m2 = payloads[1*ncols + c]; uint64_t m3 = payloads[2*ncols + c];
                    size_t off = j * ncols + c;
                    d0[off] ^= (v0[j] & m1); d2[off] ^= (v1[j] & m2); d4[off] ^= (v2[j] & m3); 
                    z1[off] ^= (v3[j] & m1); z3[off] ^= (v4[j] & m2); dt5[off] ^= ((v2[j] ^ v5[j]) & m3); 
                }
            }
        }
        else if (r == Role::P2) { 
            uint64_t* d0 = D[0].data() + base * ncols;  uint64_t* d3 = D[3].data() + base * ncols;  uint64_t* d5 = D[5].data() + base * ncols;  
            uint64_t* z2 = Zeta[2].data() + base * ncols; uint64_t* z4 = Zeta[4].data() + base * ncols; uint64_t* dt1 = Dt[1].data() + base * ncols;  
            const uint64_t* v0 = get_v(0); const uint64_t* v1 = get_v(1); const uint64_t* v2 = get_v(2);
            const uint64_t* v3 = get_v(3); const uint64_t* v4 = get_v(4); const uint64_t* v5 = get_v(5);

            for(size_t j=0; j<len; ++j) {
                for(size_t c=0; c<ncols; ++c) {
                    uint64_t m1 = payloads[0*ncols + c]; uint64_t m2 = payloads[1*ncols + c]; uint64_t m3 = payloads[2*ncols + c];
                    size_t off = j * ncols + c;
                    d0[off] ^= (v0[j] & m1); d3[off] ^= (v1[j] & m2); d5[off] ^= (v2[j] & m3); 
                    z2[off] ^= (v3[j] & m2); z4[off] ^= (v4[j] & m3); dt1[off] ^= ((v0[j] ^ v5[j]) & m1); 
                }
            }
        }
        else if (r == Role::P3) { 
            uint64_t* d1 = D[1].data() + base * ncols;  uint64_t* d2 = D[2].data() + base * ncols;  uint64_t* d5 = D[5].data() + base * ncols;  
            uint64_t* z0 = Zeta[0].data() + base * ncols; uint64_t* dt3 = Dt[3].data() + base * ncols;  uint64_t* dt4 = Dt[4].data() + base * ncols;  
            const uint64_t* v0 = get_v(0); const uint64_t* v1 = get_v(1); const uint64_t* v2 = get_v(2);
            const uint64_t* v3 = get_v(3); const uint64_t* v4 = get_v(4); const uint64_t* v5 = get_v(5); const uint64_t* v6 = get_v(6);

            for(size_t j=0; j<len; ++j) {
                for(size_t c=0; c<ncols; ++c) {
                    uint64_t m1 = payloads[0*ncols + c]; uint64_t m2 = payloads[1*ncols + c]; uint64_t m3 = payloads[2*ncols + c];
                    size_t off = j * ncols + c;
                    d1[off] ^= (v0[j] & m1); d2[off] ^= (v1[j] & m2); d5[off] ^= (v2[j] & m3); 
                    z0[off] ^= (v3[j] & m1); dt3[off] ^= ((v1[j] ^ v4[j]) & m2); dt4[off] ^= ((v5[j] ^ v6[j]) & m3); 
                }
            }
        }
        else if (r == Role::P4) { 
            uint64_t* d1 = D[1].data() + base * ncols;  uint64_t* d3 = D[3].data() + base * ncols;  uint64_t* d4 = D[4].data() + base * ncols;  
            uint64_t* z5 = Zeta[5].data() + base * ncols; uint64_t* dt0 = Dt[0].data() + base * ncols;  uint64_t* dt2 = Dt[2].data() + base * ncols;  
            const uint64_t* v0 = get_v(0); const uint64_t* v1 = get_v(1); const uint64_t* v2 = get_v(2);
            const uint64_t* v3 = get_v(3); const uint64_t* v4 = get_v(4); const uint64_t* v5 = get_v(5);

            for(size_t j=0; j<len; ++j) {
                for(size_t c=0; c<ncols; ++c) {
                    uint64_t m1 = payloads[0*ncols + c]; uint64_t m2 = payloads[1*ncols + c]; uint64_t m3 = payloads[2*ncols + c];
                    size_t off = j * ncols + c;
                    d1[off] ^= (v0[j] & m1); d3[off] ^= (v1[j] & m2); d4[off] ^= (v2[j] & m3); 
                    z5[off] ^= (v3[j] & m3); dt0[off] ^= ((v0[j] ^ v4[j]) & m1); dt2[off] ^= ((v1[j] ^ v5[j]) & m2); 
                }
            }
        }
        auto t_xor_end = std::chrono::high_resolution_clock::now();
        total_xor_ms += std::chrono::duration_cast<std::chrono::nanoseconds>(t_xor_end - t_xor_start).count() / 1000000.0;
    }

    // Return the times so the client knows how much time was spent on each part
    return {total_eval_ms, total_xor_ms};
}

// ============================================================================
// HELPER: READ Implementation
// ============================================================================
template<typename leaf_t, typename node_t, typename prgkey_t>
std::pair<std::vector<leaf_t>, std::pair<double, double>> read_impl(
    size_t nitems, size_t ncols, Role r,
    const std::vector<std::optional<dpf::dpf_key<uint64_t, node_t, prgkey_t>>>& keys,
    const std::vector<std::vector<leaf_t>>& D,
    const std::vector<std::vector<leaf_t>>& Zeta,
    const std::vector<std::vector<leaf_t>>& Dt
) {
    const size_t CHUNK_SIZE = 256 * 1024 / sizeof(leaf_t); 
    size_t blocks_needed = (CHUNK_SIZE * sizeof(leaf_t) + 15) / 16;
    
    std::vector<leaf_t> acc_val(ncols, 0); // This will hold the final extracted row
    std::vector<block_t> local_buffer(blocks_needed);
    std::vector<uint8_t> local_flags(blocks_needed * 16);

    double total_eval_ms = 0.0;
    double total_xor_ms = 0.0;

    // Loop through the database in chunks, just like we did for writing.
    for (size_t base = 0; base < nitems; base += CHUNK_SIZE) {
        size_t end = std::min(base + CHUNK_SIZE, nitems);
        size_t len = end - base;

        // A quick helper function to process one key against one database table
        auto process = [&](int key_idx, int share_idx, int type) {
            if (key_idx >= (int)keys.size() || !keys[key_idx].has_value()) return;

            // --- 1. EVALUATION PHASE ---
            auto t_eval_start = std::chrono::high_resolution_clock::now();
            
            // Unpack the read key into a long array. 
            // It will be all zeros, except for the one row the client is trying to read.
            dpf::__evalinterval(*keys[key_idx], base, end - 1, (uint64_t*)local_buffer.data(), local_flags.data());
            
            auto t_eval_end = std::chrono::high_resolution_clock::now();
            total_eval_ms += std::chrono::duration_cast<std::chrono::nanoseconds>(t_eval_end - t_eval_start).count() / 1000000.0;

            const uint64_t* __restrict T_ptr = (const uint64_t*)local_buffer.data();
            const uint64_t* __restrict data_ptr = nullptr;
            
            // Figure out if we are checking the Data (0), the Tags (1), or the Extra Checks (2)
            if(type == 0) data_ptr = D[share_idx].data();
            else if(type == 1) data_ptr = Zeta[share_idx].data();
            else if(type == 2) data_ptr = Dt[share_idx].data();

            const uint64_t* __restrict chunk_data = data_ptr + (base * ncols);
            
            // --- 2. XOR / MERGE PHASE ---
            auto t_xor_start = std::chrono::high_resolution_clock::now();
            
            // This loop acts like a giant filter/stencil. 
            // Because the unpacked key (T_ptr) is almost entirely zeros, the bitwise AND (&)
            // wipes out all the junk data we don't care about. It only snags the data 
            // from the secret row and adds it to our accumulator.
            for(size_t i=0; i<len; ++i) {
                for(size_t c=0; c<ncols; ++c) {
                    acc_val[c] ^= (chunk_data[i * ncols + c] & T_ptr[i]);
                }
            }
            
            auto t_xor_end = std::chrono::high_resolution_clock::now();
            total_xor_ms += std::chrono::duration_cast<std::chrono::nanoseconds>(t_xor_end - t_xor_start).count() / 1000000.0;
        };

        // Each server runs the process on their specific pieces of the database.
        if (r == Role::P1) { process(0,0,0); process(1,2,0); process(5,4,0); process(3,1,1); process(4,3,1); }
        else if (r == Role::P2) { process(0,0,0); process(2,5,0); process(4,3,0); process(3,1,2); process(5,4,1); }
        else if (r == Role::P3) { process(1,2,0); process(2,5,0); process(3,1,0); process(4,3,2); process(5,4,2); }
    }

    // Give back the data we grabbed, plus the time it took
    return {acc_val, {total_eval_ms, total_xor_ms}};
}

// ============================================================================
// Network Helpers: These functions handle talking to the Client
// ============================================================================
template<typename leaf_t, typename node_t, typename prgkey_t>
boost::asio::awaitable<void>
Locoram<leaf_t, node_t, prgkey_t>::apply_rss_update(MPCContext* ctx) {
    std::vector<std::vector<uint8_t>> bundle;
    std::vector<leaf_t> payloads;
    
    // Wait for the client to send over the compressed keys and the new data
    co_await (ctx->get_peer(Role::Client) >> bundle);
    co_await (ctx->get_peer(Role::Client) >> payloads);
    if (bundle.empty()) co_return;

    // Convert the raw bytes back into usable Key objects
    using KeyT = dpf::dpf_key<uint64_t, node_t, prgkey_t>;
    std::vector<std::optional<KeyT>> keys(bundle.size());
    for(size_t i=0; i<bundle.size(); ++i) {
        if (!bundle[i].empty()) {
            keys[i].emplace(dpf::deserialize_dpf_key<uint64_t, node_t, prgkey_t>(bundle[i].data(), bundle[i].size()));
        }
    }

    // Start the heavy lifting (the update_impl function above)
    std::pair<double, double> timings = update_impl<leaf_t, node_t, prgkey_t>(nitems, ncols, ctx->role, keys, payloads, D, Zeta, Dt);

    // Tell the client exactly how much time we spent unpacking keys vs doing the math
    co_await (ctx->get_peer(Role::Client) << timings.first);  // Time spent unpacking
    co_await (ctx->get_peer(Role::Client) << timings.second); // Time spent blending data
}

template<typename leaf_t, typename node_t, typename prgkey_t>
boost::asio::awaitable<std::vector<leaf_t>>
Locoram<leaf_t, node_t, prgkey_t>::apply_rss_read(MPCContext* ctx) {
    // Server 4 doesn't participate in reads, so he just returns nothing.
    if (ctx->role == Role::P4) co_return std::vector<leaf_t>(ncols, 0); 

    std::vector<std::vector<uint8_t>> bundle;
    
    // Wait for the client to send the read keys
    co_await (ctx->get_peer(Role::Client) >> bundle);

    // Convert the raw bytes back into usable Key objects
    using KeyT = dpf::dpf_key<uint64_t, node_t, prgkey_t>;
    std::vector<std::optional<KeyT>> keys(bundle.size());
    for(size_t i=0; i<bundle.size(); ++i) {
        if (!bundle[i].empty()) {
            keys[i].emplace(dpf::deserialize_dpf_key<uint64_t, node_t, prgkey_t>(bundle[i].data(), bundle[i].size()));
        }
    }
    
    // Start the heavy lifting (the read_impl function above)
    auto result = read_impl<leaf_t, node_t, prgkey_t>(nitems, ncols, ctx->role, keys, D, Zeta, Dt);

    // Send the extracted row back to the client, column by column
    for(size_t c = 0; c < ncols; ++c) {
        co_await (ctx->get_peer(Role::Client) << result.first[c]);
    }

    // Tell the client exactly how much time we spent unpacking keys vs doing the math
    co_await (ctx->get_peer(Role::Client) << result.second.first);  // Time spent unpacking
    co_await (ctx->get_peer(Role::Client) << result.second.second); // Time spent filtering data
    
    co_return result.first;
}