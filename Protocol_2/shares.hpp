#pragma once
#include<fstream>
#include <cstdint>
#include <vector>
#include <random>
#include <iostream>
#include <type_traits>
#include <utility>
#include <map>
#include <boost/asio/awaitable.hpp>

#include "types.hpp"
#include "network.hpp"

using boost::asio::awaitable;

enum class RandomnessMode {
    Online, Record, Replay
};

struct MPCContext {
    Role role;
    NetPeer& self;
    std::map<Role, NetPeer*> peers; // Added for new_test.cpp

    NetPeer* peer0 = nullptr;
    NetPeer* peer1 = nullptr;

    RandomnessMode rand_mode = RandomnessMode::Online;
    std::ofstream rand_out;
    std::ifstream rand_in;
    std::ofstream mul_out_p0, mul_out_p1, and_out_p0, and_out_p1;
    std::ifstream mul_in_p0, mul_in_p1, and_in_p0, and_in_p1;
    uint64_t mul_ctr = 0, and_ctr = 0;

    MPCContext(Role r, NetPeer& s) : role(r), self(s) {}

    // Restored helper methods
    void add_peer(Role r, NetPeer* p) {
        peers[r] = p;
        // if (role == Role::P1 && r == Role::P2) peer0 = p;
        // if (role == Role::P2 && r == Role::P1) peer0 = p;
        // Map as needed for Dealer/P3 logic
    }

    NetPeer& get_peer(Role r) {
        if (peers.find(r) == peers.end()) throw std::runtime_error("Peer not found");
        return *peers[r];
    }
};

template<typename T>
struct XShare {
    static_assert(std::is_integral_v<T>, "XShare requires integral type");
    T val{};
    MPCContext* ctx = nullptr;
    XShare(T v, MPCContext* c = nullptr) : val(v), ctx(c) {}
    inline XShare operator^(const XShare& o) const { return XShare(static_cast<T>(val ^ o.val), ctx); }
    inline XShare& operator^=(const XShare& o) { val ^= o.val; return *this; }
    friend std::ostream& operator<<(std::ostream& os, const XShare& s) { os << s.val; return os; }
};

template<typename T>
struct AShare {
    static_assert(std::is_integral_v<T>, "AShare requires integral type");
    T val{};
    MPCContext* ctx = nullptr;
    AShare(T v, MPCContext* c = nullptr) : val(v), ctx(c) {}
    inline AShare operator+(const AShare& o) const { return AShare(static_cast<T>(val + o.val), ctx); }
    inline AShare operator-(const AShare& o) const { return AShare(static_cast<T>(val - o.val), ctx); }
    inline AShare& operator+=(const AShare& o) { val = static_cast<T>(val + o.val); return *this; }
    friend std::ostream& operator<<(std::ostream& os, const AShare& s) { os << s.val; return os; }
};

template<typename T>
struct AdditiveShareVector {
    static_assert(std::is_integral_v<T>, "AdditiveShareVector requires integral type");
    std::vector<T> vals;
    MPCContext* ctx = nullptr;

    AdditiveShareVector() = default;
    explicit AdditiveShareVector(std::vector<T> v, MPCContext* c = nullptr) : vals(std::move(v)), ctx(c) {}

    inline AdditiveShareVector operator+(const AdditiveShareVector& o) const {
        if (vals.size() != o.vals.size()) throw std::runtime_error("Size mismatch");
        std::vector<T> out(vals.size());
        for(size_t i=0; i<vals.size(); ++i) out[i] = static_cast<T>(vals[i] + o.vals[i]);
        return AdditiveShareVector(std::move(out), ctx);
    }

    inline AdditiveShareVector operator-(const AdditiveShareVector& o) const {
        if (vals.size() != o.vals.size()) throw std::runtime_error("Size mismatch");
        std::vector<T> out(vals.size());
        for(size_t i=0; i<vals.size(); ++i) out[i] = static_cast<T>(vals[i] - o.vals[i]);
        return AdditiveShareVector(std::move(out), ctx);
    }

    friend std::ostream& operator<<(std::ostream& os, const AdditiveShareVector& s) {
        os << "["; for(size_t i=0; i<s.vals.size(); ++i) os << s.vals[i] << (i+1<s.vals.size()?", ":""); os << "]"; return os;
    }
};

inline uint64_t random_u64() {
    static thread_local std::mt19937_64 g(std::random_device{}());
    static std::uniform_int_distribution<uint64_t> dist;
    return dist(g);
}

template<typename T>
inline void mpc_random(MPCContext* ctx, T& out) {
    static thread_local std::mt19937_64 g(std::random_device{}());
    out = static_cast<T>(g());
}