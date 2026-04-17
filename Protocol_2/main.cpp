#define BOOST_ASIO_HAS_STD_CHRONO 1
#include <cstddef>
#include <cstdlib>
#include <type_traits>
#include <vector>
#include <string>
#include <map>
#include <iostream>
#include <limits> 
#include <chrono> 
#include <random>
#include <tuple>
#include <iomanip>
#include <algorithm> 
#include <boost/asio.hpp>


#ifndef arc4random_buf
extern "C" void arc4random_buf(void* buf, size_t n) {
    static std::random_device rd;
    static std::mt19937_64 gen(rd());
    static std::uniform_int_distribution<uint8_t> dist(0, 255);
    uint8_t* p = static_cast<uint8_t*>(buf);
    for(size_t i = 0; i < n; ++i) p[i] = dist(gen);
}
#endif

#include "types.hpp"
#include "shares.hpp"
#include "mpcops.hpp"
#include "network.hpp"
#include "locoram.hpp"
#include "dpf.hpp"

using boost::asio::awaitable;
using namespace boost::asio;
using ip::tcp;
using leaf_t = uint64_t;

const uint16_t BASE_PORT = 10000;
const std::string LOCALHOST = "127.0.0.1";

// ============================================================================
// CLIENT KEY GENERATION
// ============================================================================
std::map<Role, std::vector<std::vector<uint8_t>>> 
gen_read_bundles(uint64_t idx, size_t nitems) {
    std::map<Role, std::vector<std::vector<uint8_t>>> bundles;
    AES_KEY prgkey{};
    uint64_t ALL_ONES = (uint64_t)-1; 
    
    // Helper to generate a pair of keys pointing to the target row
    auto gen_pair = [&](uint64_t pos) {
        auto [k1, k2] = dpf::dpf_key<uint64_t, __m128i, AES_KEY>::gen(prgkey, nitems, pos, ALL_ONES);
        return std::make_pair(dpf::serialize_dpf_key(k1), dpf::serialize_dpf_key(k2));
    };

    // We have 4 database shares (D1, D2, D3, D4), so we make 4 pairs of keys.
    auto k1 = gen_pair(idx); 
    auto k2 = gen_pair(idx); 
    auto k3 = gen_pair(idx); 
    auto k4 = gen_pair(idx); 

    auto assign = [&](Role r, const std::vector<uint8_t>& key) { bundles[r].push_back(key); };

    // We hand out specific halves of the keys to Server 1, 2, and 3.
    assign(Role::P1, k2.first); assign(Role::P1, k3.first); assign(Role::P1, k4.first);
    assign(Role::P2, k1.first); assign(Role::P2, k3.second); assign(Role::P2, k4.second);
    assign(Role::P3, k1.second); assign(Role::P3, k2.second);
    
    // Server 4 does not get any keys for the read operation.
    bundles[Role::P4] = {}; 

    return bundles;
}

// ============================================================================
// CLIENT MAIN LOOP
// ============================================================================
awaitable<void> run_client(boost::asio::io_context& io, size_t nitems, size_t ncols, int num_ops) {
    try {
        // Connect the client to all 4 servers
        auto s1 = co_await connect_with_retry(io, LOCALHOST, BASE_PORT + 11);
        auto s2 = co_await connect_with_retry(io, LOCALHOST, BASE_PORT + 22);
        auto s3 = co_await connect_with_retry(io, LOCALHOST, BASE_PORT + 33);
        auto s4 = co_await connect_with_retry(io, LOCALHOST, BASE_PORT + 44);

        NetPeer p1(Role::P1, std::move(s1)), p2(Role::P2, std::move(s2)),
                p3(Role::P3, std::move(s3)), p4(Role::P4, std::move(s4));

        MPCContext ctx(Role::Client, p1);
        ctx.add_peer(Role::P1, &p1); ctx.add_peer(Role::P2, &p2);
        ctx.add_peer(Role::P3, &p3); ctx.add_peer(Role::P4, &p4);

        std::cout << "\n[Client] Connected to 4 parties. DB Size: " << nitems 
                  << " | Columns: " << ncols << " | Read Ops: " << num_ops << std::endl;

        std::random_device rd;
        std::mt19937_64 gen(rd());
        std::uniform_int_distribution<uint64_t> dist_idx(0, nitems - 1);

        // Variables to store total times and sizes
        double total_query_time = 0, total_recon_time = 0;
        double total_dpf_eval_time = 0, total_xor_time = 0;
        uint64_t total_upload_bytes = 0, total_download_bytes = 0;

        // ====================================================================
        // WARMUP PHASE
        // We do one fake "practice" run first. This wakes up the computer's memory
        // and network sockets. We throw away the time it took so it doesn't mess up our average.
        // ====================================================================
        std::cout << "[Client] Executing Warmup (practice run)..." << std::flush;
        uint8_t dummy; 
        for(auto r : {Role::P1, Role::P2, Role::P3, Role::P4}) co_await (ctx.get_peer(r) >> dummy);
        
        auto b_read = gen_read_bundles(0, nitems);
        uint8_t r_op = 1;
        for(auto r : {Role::P1, Role::P2, Role::P3, Role::P4}) co_await ctx.get_peer(r).send(r_op);
        for(auto r : {Role::P1, Role::P2, Role::P3}) co_await ctx.get_peer(r).send_bundles(b_read[r]);
        
        leaf_t d_v; uint64_t d_t_dpf, d_t_xor;
        for(auto r : {Role::P1, Role::P2, Role::P3}) {
            for(size_t c=0; c<ncols; ++c) co_await (ctx.get_peer(r) >> d_v); 
            co_await (ctx.get_peer(r) >> d_t_dpf); 
            co_await (ctx.get_peer(r) >> d_t_xor); 
        }
        std::cout << " Done!\n" << std::endl;

        // ====================================================================
        // REAL TESTING LOOP
        // ====================================================================
        for(int i = 0; i < num_ops; ++i) {
            uint64_t idx = dist_idx(gen); // Pick a random row to read
            
            // This is the answer we expect to get back from the servers (e.g. i+1, i+2, i+3)
            std::vector<leaf_t> expected_vals(ncols);
            for(size_t c = 0; c < ncols; ++c) expected_vals[c] = idx + 1 + c;

            // ----------------------------------------------------------------
            // 1. CLIENT TIMING: Creating the keys
            // ----------------------------------------------------------------
            auto t_query_start = std::chrono::high_resolution_clock::now();
            auto bundles_read = gen_read_bundles(idx, nitems);
            auto t_query_end = std::chrono::high_resolution_clock::now();
            double dr_query = std::chrono::duration_cast<std::chrono::nanoseconds>(t_query_end - t_query_start).count() / 1000000.0;
            total_query_time += dr_query;

            // Figure out how many bytes we are sending (Upload)
            uint64_t up_this_op = 0;
            for(auto r : {Role::P1, Role::P2, Role::P3, Role::P4}) up_this_op += 1; 
            for(auto r : {Role::P1, Role::P2, Role::P3}) {
                for(const auto& key : bundles_read[r]) up_this_op += key.size(); 
            }
            total_upload_bytes += up_this_op;

            // Send the instruction and the keys to the servers
            uint8_t op_read = 1; 
            for(auto r : {Role::P1, Role::P2, Role::P3, Role::P4}) co_await ctx.get_peer(r).send(op_read);
            for(auto r : {Role::P1, Role::P2, Role::P3}) co_await ctx.get_peer(r).send_bundles(bundles_read[r]);
            
            // ----------------------------------------------------------------
            // 2. SERVER TIMING: Waiting for the servers to finish
            // ----------------------------------------------------------------
            std::vector<leaf_t> r1(ncols), r2(ncols), r3(ncols);
            uint64_t dpf1_us = 0, xor1_us = 0, dpf2_us = 0, xor2_us = 0, dpf3_us = 0, xor3_us = 0;

            // Wait for data and exact timers from Server 1, 2, and 3
            for(size_t c=0; c<ncols; ++c) co_await (ctx.get_peer(Role::P1) >> r1[c]); 
            co_await (ctx.get_peer(Role::P1) >> dpf1_us); co_await (ctx.get_peer(Role::P1) >> xor1_us);
            
            for(size_t c=0; c<ncols; ++c) co_await (ctx.get_peer(Role::P2) >> r2[c]); 
            co_await (ctx.get_peer(Role::P2) >> dpf2_us); co_await (ctx.get_peer(Role::P2) >> xor2_us);
            
            for(size_t c=0; c<ncols; ++c) co_await (ctx.get_peer(Role::P3) >> r3[c]); 
            co_await (ctx.get_peer(Role::P3) >> dpf3_us); co_await (ctx.get_peer(Role::P3) >> xor3_us);

            // --- CALCULATE DOWNLOAD COST ---
            // Calculate the byte size of the exact data payload we just received from the 3 servers
            uint64_t down_this_op = (r1.size() * sizeof(leaf_t)) + 
                                    (r2.size() * sizeof(leaf_t)) + 
                                    (r3.size() * sizeof(leaf_t));

            total_download_bytes += down_this_op;

            
            // Since the servers run at the same time, the true wait time for the client 
            // is just whichever server was the slowest. We use std::max to find the slowest one.
            double dr_dpf = std::max({dpf1_us, dpf2_us, dpf3_us}) / 1000.0; 
            double dr_xor = std::max({xor1_us, xor2_us, xor3_us}) / 1000.0; 
            total_dpf_eval_time += dr_dpf;
            total_xor_time += dr_xor;

            // ----------------------------------------------------------------
            // 3. CLIENT RECONSTRUCTION: Putting the answer together
            // ----------------------------------------------------------------
            auto t_rec_start = std::chrono::high_resolution_clock::now();
            std::vector<leaf_t> result(ncols);
            bool read_ok = true;
            std::string res_str = "";

            // We simply XOR the three answers from the servers together to get the real data.
            for(size_t c=0; c<ncols; ++c) {
                result[c] = r1[c] ^ r2[c] ^ r3[c];
                res_str += std::to_string(result[c]) + (c == ncols - 1 ? "" : ", ");
                if (result[c] != expected_vals[c]) read_ok = false;
            }

            auto t_rec_end = std::chrono::high_resolution_clock::now();
            double dr_recon = std::chrono::duration_cast<std::chrono::nanoseconds>(t_rec_end - t_rec_start).count() / 1000000.0; 
            total_recon_time += dr_recon;

            // Print the final result for this round
            std::cout << "Read Op " << i+1 << "/" << num_ops << " [Idx: " << idx << "] ";
            if (read_ok) std::cout << "\033[32m[OK]\033[0m Data: [" << res_str << "]\n"; 
            else std::cout << "\033[31m[FAIL]\033[0m Data: [" << res_str << "]\n"; 

            std::cout << std::fixed << std::setprecision(3) 
                      << "  └─ Read Timings  -> Query: " << dr_query << "ms | DPF Eval: " << dr_dpf 
                      << "ms | XOR: " << dr_xor << "ms | Recon: " << dr_recon << "ms\n"
                      << "  └─ Network Costs -> Up: " << up_this_op << " B | Down: " << down_this_op << " B\n\n";
        }

        // Tell the servers we are done so they can shut down
        uint8_t op_kill = 2; 
        for(auto r : {Role::P1, Role::P2, Role::P3, Role::P4}) co_await ctx.get_peer(r).send(op_kill);

        // --- Print the final averages ---
        std::cout << "================ READ RESULTS ===================" << std::endl;
        std::cout << "Total Operations      : " << num_ops << std::endl;
        std::cout << "Total Key Gen Time    : " << std::fixed << std::setprecision(4) << total_query_time << " ms" << std::endl;
        std::cout << "-------------------------------------------------" << std::endl;
        std::cout << "Avg Query Time        : " << std::fixed << std::setprecision(4) << (total_query_time / num_ops) << " ms" << std::endl;
        std::cout << "Avg Server DPF Time   : " << std::fixed << std::setprecision(4) << (total_dpf_eval_time / num_ops) << " ms" << std::endl;
        std::cout << "Avg Server XOR Time   : " << std::fixed << std::setprecision(4) << (total_xor_time / num_ops) << " ms" << std::endl;
        std::cout << "Avg Reconstruct Time  : " << std::fixed << std::setprecision(4) << (total_recon_time / num_ops) << " ms" << std::endl;
        std::cout << "-------------------------------------------------" << std::endl;
        std::cout << "Avg Upload / Op       : " << (total_upload_bytes / num_ops) << " Bytes" << std::endl;
        std::cout << "Avg Download / Op     : " << (total_download_bytes / num_ops) << " Bytes" << std::endl;
        std::cout << "=================================================" << std::endl;

    } catch (std::exception& e) { std::cerr << "[Client Error] " << e.what() << std::endl; }
}

// ============================================================================
// SERVER STARTUP CODE
// ============================================================================
// This function runs on each server to get it ready to talk to the client.
awaitable<void> run_party_node(boost::asio::io_context& io, Role role, size_t nitems, size_t ncols) {
    try {
        std::vector<std::unique_ptr<NetPeer>> peers_list;
        auto add = [&](Role r, tcp::socket s) { peers_list.push_back(std::make_unique<NetPeer>(r, std::move(s))); };
        
        // Listen on a specific network port based on which party this is
        if (role == Role::P1) add(Role::Client, co_await make_server(io, BASE_PORT + 11));
        else if (role == Role::P2) add(Role::Client, co_await make_server(io, BASE_PORT + 22));
        else if (role == Role::P3) add(Role::Client, co_await make_server(io, BASE_PORT + 33));
        else if (role == Role::P4) add(Role::Client, co_await make_server(io, BASE_PORT + 44));

        tcp::socket dummy(io); NetPeer self_node(role, std::move(dummy));
        MPCContext ctx(role, self_node);
        for(auto& p : peers_list) ctx.add_peer(p->role, p.get());
        
        // Build the database for this specific server
        Locoram<leaf_t, __m128i, AES_KEY> loc(nitems, ncols, role);
        std::cout << "[P" << (int)role << "] Ready.\n";

        // Tell the client we are ready to start the warmup
        try { 
            uint8_t ready = 1; 
            co_await ctx.get_peer(Role::Client).send(ready); 
        } catch (...) {}

        // Keep running in a loop, waiting for the client to send a command
        while(true) {
            uint8_t op = 0;
            try { op = co_await ctx.get_peer(Role::Client).recv<uint8_t>(); } catch (...) { break; }
            if (op == 1) co_await loc.apply_rss_read(&ctx); // 1 = Do a read operation
            else if (op == 2) break;                        // 2 = Shut down
        }
    } catch (std::exception& e) { std::cerr << "[Node Error] " << e.what() << std::endl; }
}

// ============================================================================
// PROGRAM START
// ============================================================================
int main(int argc, char* argv[]) {
    if (argc < 4) {
        std::cerr << "Usage: " << argv[0] << " <party> <power> <num_ops>\n";
        return 1;
    }

    std::string r = argv[1];
    int power = std::stoi(argv[2]);
    int num_ops = std::stoi(argv[3]);
    
    // Calculate the total number of rows (2 to the power of the input)
    size_t nitems = 1ULL << power; 
    
    // We are testing with 3 columns of data per row
    size_t ncols = 3;

    boost::asio::io_context io;

    // Start the right function depending on what you typed in the terminal
    if (r == "p1") co_spawn(io, run_party_node(io, Role::P1, nitems, ncols), detached);
    else if (r == "p2") co_spawn(io, run_party_node(io, Role::P2, nitems, ncols), detached);
    else if (r == "p3") co_spawn(io, run_party_node(io, Role::P3, nitems, ncols), detached);
    else if (r == "p4") co_spawn(io, run_party_node(io, Role::P4, nitems, ncols), detached);
    else if (r == "client") co_spawn(io, run_client(io, nitems, ncols, num_ops), detached);
    else { std::cerr << "Invalid party! Use: p1..p4, or client\n"; return 1; }
    
    io.run();
    return 0;
}