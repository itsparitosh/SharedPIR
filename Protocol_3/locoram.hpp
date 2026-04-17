
#pragma once
#include <vector>
#include <map>
#include <memory>
#include <boost/asio.hpp>
#include "types.hpp"
#include "network.hpp"

// Forward decl
struct MPCContext;

template<typename leaf_t, typename node_t, typename prgkey_t>
class Locoram {
public:
    // Dimensions
    size_t nitems; // Total number of rows in our database
    size_t ncols;  // Number of items per row (e.g., 3 columns)

    // --- SHARE STORAGE ---
    // store the shares as a flat 1D list instead of a 2D grid (vector of vectors).
    // To find row 'i' and column 'c', we just do: (i * ncols) + c
    std::vector<std::vector<leaf_t>> D;
    std::vector<std::vector<leaf_t>> Zeta;
    std::vector<std::vector<leaf_t>> Dt; // Tilde D

    // Constructor
    Locoram(size_t n, size_t cols, Role role);

    boost::asio::awaitable<void> apply_rss_update(MPCContext* ctx);
    boost::asio::awaitable<std::vector<leaf_t>> apply_rss_read(MPCContext* ctx);
};

#include "locoram.tpp"