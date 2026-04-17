
#pragma once
#include <vector>
#include <map>
#include <memory>
#include <boost/asio.hpp>
#include "types.hpp"
#include "network.hpp"


struct MPCContext;

template<typename leaf_t, typename node_t, typename prgkey_t>
class Locoram {
public:
    size_t nitems; // Total number of rows in our database
    size_t ncols;  // Number of items per row (e.g., 3 columns)

    // --- DATABASE SHARES ---
    // store the database as a flat 1D list instead of a 2D grid (vector of vectors).
    // To find row 'i' and column 'c', we just do: (i * ncols) + c

    std::vector<leaf_t> D0, D1, D2, D3, D4, D5, D6, D7;
    std::vector<leaf_t> zeta0, zeta2, zeta4, zeta6;
    std::vector<leaf_t> D_tilde_0, D_tilde_2, D_tilde_4, D_tilde_6;


    // --- PRE-ALLOCATED BUFFERS ---
    std::vector<leaf_t> eval_buf;
    std::vector<uint8_t> flags_buf;
    std::vector<leaf_t> bufA1;
    std::vector<leaf_t> bufB1;

    // Constructor to setup the server
    Locoram(size_t n, size_t cols, Role role);


    boost::asio::awaitable<std::vector<leaf_t>> apply_rss_read(MPCContext* ctx);
    boost::asio::awaitable<void> apply_rss_write(MPCContext* ctx);
};

#include "locoram.tpp"