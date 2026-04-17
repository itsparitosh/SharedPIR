
#pragma once
#include <iostream>
#include <vector>
#include <optional>
#include <chrono>

#include "types.hpp"
#include "mpcops.hpp" 
#include "dpf.hpp"

template<typename leaf_t, typename node_t, typename prgkey_t>
Locoram<leaf_t, node_t, prgkey_t>::Locoram(size_t n, size_t cols, Role role) : nitems(n), ncols(cols) {
    size_t total_size = nitems * ncols;
    
    // Each server only creates memory for the specific shares it is assigned to hold.
    if (role == Role::P0) { D0.resize(total_size, 0); D7.resize(total_size, 0); D5.resize(total_size, 0); zeta6.resize(total_size, 0); D_tilde_4.resize(total_size, 0); }
    else if (role == Role::P1) { D2.resize(total_size, 0); D1.resize(total_size, 0); D7.resize(total_size, 0); zeta0.resize(total_size, 0); D_tilde_6.resize(total_size, 0); }
    else if (role == Role::P2) { D4.resize(total_size, 0); D3.resize(total_size, 0); D1.resize(total_size, 0); zeta2.resize(total_size, 0); D_tilde_0.resize(total_size, 0); }
    else if (role == Role::P3) { D6.resize(total_size, 0); D5.resize(total_size, 0); D3.resize(total_size, 0); zeta4.resize(total_size, 0); D_tilde_2.resize(total_size, 0); }

    // Prepare our reusable buffers
    eval_buf.resize(nitems, 0);
    flags_buf.resize(nitems * sizeof(leaf_t), 0);
    bufA1.resize(nitems, 0);
    bufB1.resize(nitems, 0);
}

// ============================================================================
// SERVER READ OPERATION
// ============================================================================
template<typename leaf_t, typename node_t, typename prgkey_t>
boost::asio::awaitable<std::vector<leaf_t>>
Locoram<leaf_t, node_t, prgkey_t>::apply_rss_read(MPCContext* ctx) {
    
    // 1. Receive the secret keys from the client over the network
    std::vector<std::vector<uint8_t>> bundle;
    co_await (ctx->get_peer(Role::Client) >> bundle);
    
    // Create a vector to hold the final retrieved columns to send back
    std::vector<leaf_t> response(ncols, 0);
    if (bundle.empty()) co_return response;

    // 2. DESERIALIZATION PHASE
    using KeyT = dpf::dpf_key<leaf_t, node_t, prgkey_t>;
    std::vector<std::optional<KeyT>> keys(bundle.size());
    for(size_t i = 0; i < bundle.size(); ++i) {
        if (!bundle[i].empty()) keys[i].emplace(dpf::deserialize_dpf_key<leaf_t, node_t, prgkey_t>(bundle[i].data(), bundle[i].size()));
    }

    // --- Benchmarking Trackers ---
    uint64_t dpf_time_us = 0;
    uint64_t xor_time_us = 0;

    // A small helper function to process one single key against one database share
    auto dot_product = [&](int key_idx, const std::vector<leaf_t>& share_vec) {
        if (key_idx >= (int)keys.size() || !keys[key_idx].has_value()) return;
        
        // --- STEP A: DPF Expansion ---
        auto t_dpf_start = std::chrono::high_resolution_clock::now();
        dpf::__evalinterval(*keys[key_idx], 0, nitems - 1, eval_buf.data(), flags_buf.data());
        auto t_dpf_end = std::chrono::high_resolution_clock::now();
        dpf_time_us += std::chrono::duration_cast<std::chrono::microseconds>(t_dpf_end - t_dpf_start).count();
        
        // --- STEP B: XOR Math ---
        auto t_xor_start = std::chrono::high_resolution_clock::now();
        for(size_t i = 0; i < nitems; ++i) {
            for(size_t c = 0; c < ncols; ++c) response[c] ^= (share_vec[i * ncols + c] & eval_buf[i]); 
        }
        auto t_xor_end = std::chrono::high_resolution_clock::now();
        xor_time_us += std::chrono::duration_cast<std::chrono::microseconds>(t_xor_end - t_xor_start).count();
    };


    // Apply the math to whichever shares this specific server holds
    if (ctx->role == Role::P0) { dot_product(0, D5); dot_product(1, D7); dot_product(2, D0); dot_product(3, D_tilde_4); dot_product(4, zeta6); }
    else if (ctx->role == Role::P1) { dot_product(0, D1); dot_product(1, D7); dot_product(2, D2); dot_product(3, D_tilde_6); dot_product(4, zeta0); }
    else if (ctx->role == Role::P2) { dot_product(0, D1); dot_product(1, D3); dot_product(2, D4); dot_product(3, D_tilde_0); dot_product(4, zeta2); }
    else if (ctx->role == Role::P3) { dot_product(0, D3); dot_product(1, D5); dot_product(2, D6); dot_product(3, D_tilde_2); dot_product(4, zeta4); }

    // Send the extracted shares back to the client, along with how long it took
    for(size_t c = 0; c < ncols; ++c) co_await (ctx->get_peer(Role::Client) << response[c]);
    
    co_await (ctx->get_peer(Role::Client) << dpf_time_us);
    co_await (ctx->get_peer(Role::Client) << xor_time_us);
    
    co_return response;
}

// ============================================================================
// SERVER WRITE OPERATION
// ============================================================================
template<typename leaf_t, typename node_t, typename prgkey_t>
boost::asio::awaitable<void>
Locoram<leaf_t, node_t, prgkey_t>::apply_rss_write(MPCContext* ctx) {
    std::vector<std::vector<uint8_t>> bundle;
    std::vector<leaf_t> flat_payload; 
    
    // Receive the target keys AND the data payload ((which contains the actual values to write)) from the client
    co_await (ctx->get_peer(Role::Client) >> bundle);
    co_await (ctx->get_peer(Role::Client) >> flat_payload); 

    // 2. Deserialize the Keys (Same as Read operation)
    using KeyT = dpf::dpf_key<leaf_t, node_t, prgkey_t>;
    std::vector<std::optional<KeyT>> keys(bundle.size());
    for(size_t i = 0; i < bundle.size(); ++i) {
        if (!bundle[i].empty()) keys[i].emplace(dpf::deserialize_dpf_key<leaf_t, node_t, prgkey_t>(bundle[i].data(), bundle[i].size()));
    }

    uint64_t dpf_time_us = 0;
    uint64_t xor_time_us = 0;

    // Helper to update
    auto update_share = [&](int key_idx, std::vector<leaf_t>& share_vec) {
        if (key_idx >= (int)keys.size() || !keys[key_idx].has_value()) return;
        
        // --- STEP A: DPF Expansion (Same as Read) ---
        auto t_dpf_start = std::chrono::high_resolution_clock::now();
        dpf::__evalinterval(*keys[key_idx], 0, nitems - 1, eval_buf.data(), flags_buf.data());
        auto t_dpf_end = std::chrono::high_resolution_clock::now();
        dpf_time_us += std::chrono::duration_cast<std::chrono::microseconds>(t_dpf_end - t_dpf_start).count();
        
        // --- STEP B: The Payload Update ---
        // Multiply the generated mask by the client's payload.
        auto t_xor_start = std::chrono::high_resolution_clock::now();
        size_t p_offset = key_idx * ncols;
        for(size_t i = 0; i < nitems; ++i) {
            for(size_t c = 0; c < ncols; ++c) {
                share_vec[i * ncols + c] ^= (eval_buf[i] & flat_payload[p_offset + c]);
            }
        }
        auto t_xor_end = std::chrono::high_resolution_clock::now();
        xor_time_us += std::chrono::duration_cast<std::chrono::microseconds>(t_xor_end - t_xor_start).count();
    };

    // Helper to update the D_tilde shares
    auto update_d_tilde = [&](int key_idx_A1, int key_idx_B1, std::vector<leaf_t>& d_tilde_vec) {
        if (!keys[key_idx_A1].has_value() || !keys[key_idx_B1].has_value()) return;
        
        auto t_dpf_start = std::chrono::high_resolution_clock::now();
        dpf::__evalinterval(*keys[key_idx_A1], 0, nitems - 1, bufA1.data(), flags_buf.data());
        dpf::__evalinterval(*keys[key_idx_B1], 0, nitems - 1, bufB1.data(), flags_buf.data());
        auto t_dpf_end = std::chrono::high_resolution_clock::now();
        dpf_time_us += std::chrono::duration_cast<std::chrono::microseconds>(t_dpf_end - t_dpf_start).count();
        
        auto t_xor_start = std::chrono::high_resolution_clock::now();
        size_t p_offset = key_idx_A1 * ncols; 
        for(size_t i = 0; i < nitems; ++i) {
            for(size_t c = 0; c < ncols; ++c) {
                leaf_t valA = bufA1[i] & flat_payload[p_offset + c];
                leaf_t valB = bufB1[i] & flat_payload[p_offset + c];
                d_tilde_vec[i * ncols + c] ^= (valA ^ valB); // Secures the D_tilde invariant
            }
        }
        auto t_xor_end = std::chrono::high_resolution_clock::now();
        xor_time_us += std::chrono::duration_cast<std::chrono::microseconds>(t_xor_end - t_xor_start).count();
    };

    if (ctx->role == Role::P0) { update_share(0, D0); update_share(1, D5); update_share(2, D7); update_d_tilde(1, 3, D_tilde_4); update_share(4, zeta6); }
    else if (ctx->role == Role::P1) { update_share(0, D2); update_share(1, D1); update_share(2, D7); update_d_tilde(2, 3, D_tilde_6); update_share(4, zeta0); }
    else if (ctx->role == Role::P2) { update_share(0, D4); update_share(1, D1); update_share(2, D3); update_d_tilde(1, 3, D_tilde_0); update_share(4, zeta2); }
    else if (ctx->role == Role::P3) { update_share(0, D6); update_share(1, D3); update_share(2, D5); update_d_tilde(1, 3, D_tilde_2); update_share(4, zeta4); }
    
    // Let the client know we finished
    co_await (ctx->get_peer(Role::Client) << dpf_time_us);
    co_await (ctx->get_peer(Role::Client) << xor_time_us);

    co_return;
}