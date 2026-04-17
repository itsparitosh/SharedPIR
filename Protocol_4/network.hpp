#pragma once

// Define this macro to fix Boost Asio compatibility with C++20 standard library
#define BOOST_ASIO_HAS_STD_CHRONO 1

#include <boost/asio.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/use_awaitable.hpp>

#include <vector>
#include <cstdint>
#include <string>
#include <iostream>
#include <stdexcept>
#include <memory>
#include <type_traits>
#include <thread>

using boost::asio::awaitable;
using boost::asio::use_awaitable;
using boost::asio::async_write;
using boost::asio::async_read;
using boost::asio::buffer;
using boost::asio::ip::tcp;

// Role enum for 4-Party + Client setup
enum class Role { P0, P1, P2, P3, P4, P5, Client };

struct NetPeer {
    Role role;
    tcp::socket sock;

    explicit NetPeer(Role r, tcp::socket&& s) : role(r), sock(std::move(s)) {}

    // 1. Basic send for single values (trivially copyable)
    template<typename T>
    awaitable<void> send(const T& value) {
        static_assert(std::is_trivially_copyable_v<T>, "Type must be trivially copyable");
        co_await async_write(sock, buffer(&value, sizeof(T)), use_awaitable);
        co_return;
    }

    // 2. Basic recv for single values
    template<typename T>
    awaitable<T> recv() {
        static_assert(std::is_trivially_copyable_v<T>, "Type must be trivially copyable");
        T value;
        co_await async_read(sock, buffer(&value, sizeof(T)), use_awaitable);
        co_return value;
    }

    // 3. Send for a flat vector
    template<typename T>
    awaitable<void> send_vector(const std::vector<T>& vec) {
        static_assert(std::is_trivially_copyable_v<T>, "Vector elements must be trivially copyable");
        uint64_t sz = vec.size();
        co_await send(sz); 
        if (sz > 0) co_await async_write(sock, buffer(vec.data(), sizeof(T) * sz), use_awaitable);
        co_return;
    }

    // 4. Recv for a flat vector
    template<typename T>
    awaitable<std::vector<T>> recv_vector() {
        static_assert(std::is_trivially_copyable_v<T>, "Vector elements must be trivially copyable");
        uint64_t sz = co_await recv<uint64_t>();
        std::vector<T> v(sz);
        if (sz > 0) co_await async_read(sock, buffer(v.data(), sizeof(T) * sz), use_awaitable);
        co_return v;
    }

    // 5. Send bundles (vector of vectors)
    awaitable<void> send_bundles(const std::vector<std::vector<uint8_t>>& bundles) {
        uint64_t num = bundles.size();
        co_await send(num); 
        for (const auto& b : bundles) {
            co_await send_vector(b); 
        }
        co_return;
    }

    // 6. Recv bundles
    awaitable<std::vector<std::vector<uint8_t>>> recv_bundles() {
        uint64_t num = co_await recv<uint64_t>();
        std::vector<std::vector<uint8_t>> out(num);
        for (uint64_t i = 0; i < num; ++i) {
            out[i] = co_await recv_vector<uint8_t>();
        }
        co_return out;
    }

    // --- OPERATOR OVERLOADS ---

    // Generic Send
    template<typename T>
    awaitable<void> operator<<(const T& val) {
        if constexpr (std::is_same_v<T, std::vector<std::vector<uint8_t>>>) {
            co_await send_bundles(val);
        }
        else if constexpr (std::is_trivially_copyable_v<T>) {
            co_await send(val);
        } 
        else {
            // Assume vector<T> where T is trivial
            co_await send_vector(val);
        }
        co_return;
    }

    // Generic Recv
    template<typename T>
    awaitable<void> operator>>(T& out) {
        if constexpr (std::is_same_v<T, std::vector<std::vector<uint8_t>>>) {
            out = co_await recv_bundles();
        }
        else if constexpr (std::is_trivially_copyable_v<T>) {
            out = co_await recv<T>();
        } 
        else {
            // Assume vector<T>
            out = co_await recv_vector<typename T::value_type>();
        }
        co_return;
    }
};

// --- Connection Helpers ---

inline awaitable<tcp::socket> connect_with_retry(boost::asio::io_context& io,
                                          const std::string& host,
                                          uint16_t port,
                                          int max_tries = -1) {
    tcp::resolver resolver(io);
    int attempt = 0;

    for (;;) {
        try {
            tcp::socket sock(io);
            auto endpoints = co_await resolver.async_resolve(host, std::to_string(port), use_awaitable);
            co_await boost::asio::async_connect(sock, endpoints, use_awaitable);
            co_return std::move(sock);
        } catch (std::exception& e) {
            attempt++;
            if (max_tries > 0 && attempt >= max_tries)
                throw;
            std::cerr << "[connect_with_retry] " << host << ":" << port
                      << " failed (" << e.what() << "), retrying..." << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    }
}

inline awaitable<tcp::socket> make_server(boost::asio::io_context& io, uint16_t port) {
    tcp::acceptor acceptor(io, tcp::endpoint(tcp::v4(), port));
    acceptor.set_option(tcp::acceptor::reuse_address(true));
    tcp::socket sock(io);
    co_await acceptor.async_accept(sock, use_awaitable);
    co_return std::move(sock);
}

inline awaitable<tcp::socket> make_client(boost::asio::io_context& io, const std::string& host, uint16_t port) {
    tcp::resolver resolver(io);
    auto results = co_await resolver.async_resolve(host, std::to_string(port), use_awaitable);
    tcp::socket sock(io);
    co_await boost::asio::async_connect(sock, results, use_awaitable);
    co_return std::move(sock);
}

struct NetContext {
    Role self_role;
    std::vector<std::unique_ptr<NetPeer>> peers;
    explicit NetContext(Role r) : self_role(r) {}

    void add_peer(Role r, tcp::socket&& sock) {
        peers.emplace_back(std::make_unique<NetPeer>(r, std::move(sock)));
    }

    NetPeer& peer(Role r) {
        for (auto& p : peers) if (p->role == r) return *p;
        throw std::runtime_error("peer not found");
    }
};