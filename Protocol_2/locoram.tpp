#pragma once
#include <iostream>
#include <vector>
#include <random>
#include <optional>
#include <chrono>

#include "types.hpp"
#include "mpcops.hpp" 
#include "dpf.hpp"

template<typename leaf_t, typename node_t, typename prgkey_t>
Locoram<leaf_t, node_t, prgkey_t>::Locoram(size_t n, size_t cols, Role role) : nitems(n), ncols(cols) {
    size_t total_size = nitems * ncols;
    
    // 1. Allocate only the Replicated Secret Shares (RSS) the specific server holds
    // In this 4-party protocol, a party holds 3 out of 4 shares
    if (role != Role::P1) D1.resize(total_size, 0); // P1 does NOT hold D1
    if (role != Role::P2) D2.resize(total_size, 0); // P2 does NOT hold D2
    if (role != Role::P3) D3.resize(total_size, 0); // P3 does NOT hold D3
    if (role != Role::P4) D4.resize(total_size, 0); // P4 does NOT hold D4

    // Pre-allocate buffers
    // Done here to strictly avoid costly dynamic memory allocations during the online read phase.
    eval_buf.resize(nitems, 0);
    flags_buf.resize(nitems * sizeof(leaf_t), 0);

    // Deterministically populate the database using a fixed seed.
    // For any row at index 'i', the plaintext database effectively 
    // stores the sequential values [i+1, i+2, i+3] across its columns.
    std::mt19937_64 gen(123456789); 
    for(size_t i = 0; i < n; ++i) {
        for(size_t c = 0; c < ncols; ++c) {
            uint64_t target_val = i + 1 + c; 
            
            uint64_t d1 = gen();
            uint64_t d2 = gen();
            uint64_t d3 = gen();
            uint64_t d4 = target_val ^ d1 ^ d2 ^ d3; 

            // Calculate the flattened 1D index
            size_t idx = i * ncols + c;

            // Store the generated shares only if this party is assigned to hold them
            if (role != Role::P1) D1[idx] = d1;
            if (role != Role::P2) D2[idx] = d2;
            if (role != Role::P3) D3[idx] = d3;
            if (role != Role::P4) D4[idx] = d4;
        }
    }
}


template<typename leaf_t, typename node_t, typename prgkey_t>
boost::asio::awaitable<std::vector<leaf_t>>
Locoram<leaf_t, node_t, prgkey_t>::apply_rss_read(MPCContext* ctx) {
    // P4 does not participate in the active read phase of this specific 4-party protocol.
    if (ctx->role == Role::P4) co_return std::vector<leaf_t>(ncols, 0); 

    // 1. Receive the bundle of serialized DPF keys from the client asynchronously
    std::vector<std::vector<uint8_t>> bundle;
    co_await (ctx->get_peer(Role::Client) >> bundle);
    
    std::vector<leaf_t> response(ncols, 0);
    if (bundle.empty()) co_return response;

    // 2. Deserialize the byte arrays back into usable DPF key objects
    using KeyT = dpf::dpf_key<uint64_t, node_t, prgkey_t>;
    std::vector<std::optional<KeyT>> keys(bundle.size());
    for(size_t i = 0; i < bundle.size(); ++i) {
        if (!bundle[i].empty()) {
            keys[i].emplace(dpf::deserialize_dpf_key<uint64_t, node_t, prgkey_t>(
                bundle[i].data(), bundle[i].size()));
        }
    }

    // --- Benchmarking Trackers ---
    uint64_t total_dpf_eval_us = 0;
    uint64_t total_xor_response_us = 0;


    auto process_key = [&](int key_idx, const std::vector<leaf_t>& share_vec) {
        if (key_idx >= (int)keys.size() || !keys[key_idx].has_value()) return;

        // Phase A: Cryptographic Expansion (CPU Bound)
        auto t_dpf_start = std::chrono::high_resolution_clock::now();

        // Expands the DPF tree into eval_buf. 
        dpf::__evalinterval(*keys[key_idx], 0, nitems - 1, eval_buf.data(), flags_buf.data());
        auto t_dpf_end = std::chrono::high_resolution_clock::now();
        total_dpf_eval_us += std::chrono::duration_cast<std::chrono::microseconds>(t_dpf_end - t_dpf_start).count();

        
        // Phase B: Frobenius Inner Product (Memory Bandwidth Bound)
        auto t_xor_start = std::chrono::high_resolution_clock::now();
        for(size_t i = 0; i < nitems; ++i) {
            for(size_t c = 0; c < ncols; ++c) {
                // Multiplies (ANDs) the database row with the DPF evaluation bit, 
                // accumulating the result across all columns via XOR.
                response[c] ^= (share_vec[i * ncols + c] & eval_buf[i]);
            }
        }
        auto t_xor_end = std::chrono::high_resolution_clock::now();
        total_xor_response_us += std::chrono::duration_cast<std::chrono::microseconds>(t_xor_end - t_xor_start).count();
    };

    // 3. Evaluate the keys against the appropriate database shares based on the server's role.
    // The client strictly structures the key bundles to match this assignment.
    if (ctx->role == Role::P1) { process_key(0, D2); process_key(1, D3); process_key(2, D4); }
    else if (ctx->role == Role::P2) { process_key(0, D1); process_key(1, D3); process_key(2, D4); }
    else if (ctx->role == Role::P3) { process_key(0, D1); process_key(1, D2); }

    // 4. Send the computed response payload (all columns) back to the client
    for(size_t c = 0; c < ncols; ++c) {
        co_await (ctx->get_peer(Role::Client) << response[c]);
    }
    
    // 5. Send granular performance metrics back to the client for benchmarking
    co_await (ctx->get_peer(Role::Client) << total_dpf_eval_us);
    co_await (ctx->get_peer(Role::Client) << total_xor_response_us);
    
    co_return response;
}