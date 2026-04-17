
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
    // Database Dimensions
    size_t nitems; // Total number of records (rows) in the database
    size_t ncols;  // Total number of elements (columns) per record


    // --- DATABASE SHARES ---
    // store the database as a flat 1D list instead of a 2D grid (vector of vectors).
    // To find row 'i' and column 'c', we just do: (i * ncols) + c
    std::vector<leaf_t> D1;
    std::vector<leaf_t> D2;
    std::vector<leaf_t> D3;
    std::vector<leaf_t> D4;

    /*
     * Persistent Evaluation Buffers.
     * Pre-allocated at initialization to prevent costly memory allocations 
     * during the latency-critical online phase of the read operation.
     */
    std::vector<leaf_t> eval_buf;   // Stores the expanded DPF payloads
    std::vector<uint8_t> flags_buf; // Stores the DPF control bits


    Locoram(size_t n, size_t cols, Role role);

    boost::asio::awaitable<std::vector<leaf_t>> apply_rss_read(MPCContext* ctx);
};


#include "locoram.tpp"