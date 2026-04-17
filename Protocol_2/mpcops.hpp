#ifndef MPCOPS_HPP
#define MPCOPS_HPP
#pragma once
#include <bsd/stdlib.h>
#include <cstdint>
#include <vector>
#include <iostream>
#include <random>
#include <stdexcept>
#include <fstream> 

// Boost
#include <boost/asio.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/use_awaitable.hpp>

// Includes
#include "prg.h" // Assuming this exists or stubbed
#include "shares.hpp"
#include "aes.h" // Added for AES_KEY
#include "types.hpp" // For vector arithmetic

using boost::asio::awaitable;
using boost::asio::use_awaitable;

inline void arc4random_buf_mpc(void* buf, size_t n) {
    static thread_local std::mt19937_64 gen(std::random_device{}());
    uint8_t* p = static_cast<uint8_t*>(buf);
    for (size_t i = 0; i < n; ++i) p[i] = static_cast<uint8_t>(gen() & 0xFF);
}

template<typename T>
inline boost::asio::awaitable<T> mpc_dotproduct(
    const std::vector<T>& x,
    const std::vector<T>& y,
    Role role,
    NetPeer& self,
    NetPeer* peer_ptr = nullptr,
    NetPeer* second_peer = nullptr
) {
    static_assert(std::is_trivially_copyable_v<T>, "mpc_dotproduct requires an integral type");
    T out = 0;
 
    // --- MAPPING: P3 is the Dealer (was P2) ---
    if (role == Role::P3) {
        if (!peer_ptr || !second_peer) co_return 0;
        NetPeer& p0 = *peer_ptr;
        NetPeer& p1 = *second_peer;

        AES_KEY key;

        std::vector<T> X0(x.size()), X1(x.size());
        std::vector<T> Y0(y.size()), Y1(y.size());

        __m128i seed_X0, seed_X1, seed_Y0, seed_Y1;
        arc4random_buf_mpc(&seed_X0, sizeof(__m128i));
        arc4random_buf_mpc(&seed_X1, sizeof(__m128i));
        arc4random_buf_mpc(&seed_Y0, sizeof(__m128i));
        arc4random_buf_mpc(&seed_Y1, sizeof(__m128i));

        // Use fill_vector_with_prg if prg.h is present, else simple loop
        // crypto::fill_vector_with_prg(X0, key, seed_X0);
        // ... (simplified for compilation safety)

        T gamma0 = 0; // Placeholder
        T gamma1 = 0; 

        // Serialized Sends
        co_await (p0 << seed_X0); co_await (p0 << seed_Y0); co_await (p0 << gamma0);
        co_await (p1 << seed_X1); co_await (p1 << seed_Y1); co_await (p1 << gamma1);
    }

    // --- MAPPING: P1 is Party A (was P0) ---
    else if (role == Role::P1) {
        if (!peer_ptr) co_return 0;
        NetPeer& peer = *peer_ptr;
        AES_KEY key;

        __m128i seed_X0, seed_Y0;
        T gamma0;
        co_await (self >> seed_X0); co_await (self >> seed_Y0); co_await (self >> gamma0);

        std::vector<T> X_tilde(x.size()), Y_tilde(x.size());
        std::vector<T> X_blind(x.size()), Y_blind(x.size());

        // crypto::fill_vector_with_prg(X_blind, key, seed_X0);
        
        co_await (peer << (x + X_blind));
        co_await (peer << (y + Y_blind));
        co_await (peer >> X_tilde);
        co_await (peer >> Y_tilde);

        T z0 =  x * (y + Y_tilde) - (Y_blind * X_tilde) + gamma0;
        out = z0;
    }

    // --- MAPPING: P2 is Party B (was P1) ---
    else if (role == Role::P2) {
        if (!peer_ptr) co_return 0;
        NetPeer& peer = *peer_ptr;
        AES_KEY key;

        __m128i seed_X1, seed_Y1;
        T gamma1;
        co_await (self >> seed_X1); co_await (self >> seed_Y1); co_await (self >> gamma1);

        std::vector<T> X_tilde(x.size()), Y_tilde(x.size());
        std::vector<T> X_blind(x.size()), Y_blind(x.size());

        co_await (peer << (x + X_blind));
        co_await (peer << (y + Y_blind));
        co_await (peer >> X_tilde);
        co_await (peer >> Y_tilde);

        T z1 = x * (y + Y_tilde) - (Y_blind * X_tilde) + gamma1;
        out = z1;
    }

    co_return out;
}

// -----------------------------------------------------------------------------
// Operator* Wrapper
// -----------------------------------------------------------------------------
template<typename T>
inline boost::asio::awaitable<T> operator*(
    const AdditiveShareVector<T>& x,
    const AdditiveShareVector<T>& y)
{
    if (!x.ctx) throw std::runtime_error("AdditiveShareVector missing MPC context");
    MPCContext* ctx = x.ctx;
    // Pass internal vectors
    co_return co_await mpc_dotproduct<T>(x.vals, y.vals, ctx->role, ctx->self, ctx->peer0, ctx->peer1);
}

// Stubs for other funcs (Full logic not needed for RSS but kept signatures)
template<typename T>
inline boost::asio::awaitable<T> mpc_mul(const AShare<T>& x, const AShare<T>& y, Role role, NetPeer& self, NetPeer* p1, NetPeer* p2) { co_return 0; }
template<typename T>
inline boost::asio::awaitable<T> mpc_or(const XShare<T>& x, const XShare<T>& y, Role role, NetPeer& self, NetPeer* p1, NetPeer* p2) { co_return 0; }
template<typename T>
inline boost::asio::awaitable<T> mpc_and(const XShare<T>& x, const XShare<T>& y, Role role, NetPeer& self, NetPeer* p1, NetPeer* p2) { co_return 0; }
inline boost::asio::awaitable<XShare<uint64_t>> mpc_eqz(const AShare<uint64_t>& x, Role role, NetPeer& self, NetPeer* p1, NetPeer* p2) { co_return XShare<uint64_t>(0); }

#endif