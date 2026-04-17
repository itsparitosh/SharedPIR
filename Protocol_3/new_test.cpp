
#define BOOST_ASIO_HAS_STD_CHRONO 1
#include <cstddef>
#include <cstdlib>
#include <vector>
#include <string>
#include <map>
#include <iostream>
#include <chrono> 
#include <random>
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

// Custom struct to bundle the DPF keys and the actual data payload together
struct WriteBundle {
    std::vector<std::vector<uint8_t>> keys;
    std::vector<leaf_t> flat_payloads;
};

// ============================================================================
// CLIENT HELPER: Generate the keys for a WRITE operation
// ============================================================================
std::map<Role, WriteBundle> gen_update_bundles(uint64_t idx, const std::vector<leaf_t>& val, size_t nitems, size_t ncols) {
    std::map<Role, WriteBundle> bundles;
    AES_KEY prgkey{}; 
    static std::mt19937_64 gen(std::random_device{}());

    // 1. Split the Payload into Secret Shares
    std::vector<leaf_t> M1(ncols), M2(ncols), M3(ncols);
    for(size_t c = 0; c < ncols; ++c) {
        M1[c] = gen(); M2[c] = gen();
        M3[c] = val[c] ^ M1[c] ^ M2[c];
    }
    
    // Flatten the shares into one long list so it's easy to send over the network
    std::vector<leaf_t> flat_payloads;
    flat_payloads.insert(flat_payloads.end(), M1.begin(), M1.end());
    flat_payloads.insert(flat_payloads.end(), M2.begin(), M2.end());
    flat_payloads.insert(flat_payloads.end(), M3.begin(), M3.end());

    uint64_t ALL_ONES = (uint64_t)-1;

    // 2. Generate the DPF Keys at target index
    auto gen_3_pairs = [&](uint64_t target_idx) {
        std::vector<std::vector<uint8_t>> res;
        auto [u0, u1] = dpf::dpf_key<uint64_t, __m128i, AES_KEY>::gen(prgkey, nitems, target_idx, ALL_ONES);
        res.push_back(dpf::serialize_dpf_key(u0)); res.push_back(dpf::serialize_dpf_key(u1));
        
        auto [v0, v1] = dpf::dpf_key<uint64_t, __m128i, AES_KEY>::gen(prgkey, nitems, target_idx, ALL_ONES);
        res.push_back(dpf::serialize_dpf_key(v0)); res.push_back(dpf::serialize_dpf_key(v1));
        
        auto [w0, w1] = dpf::dpf_key<uint64_t, __m128i, AES_KEY>::gen(prgkey, nitems, target_idx, ALL_ONES);
        res.push_back(dpf::serialize_dpf_key(w0)); res.push_back(dpf::serialize_dpf_key(w1));
        return res;
    };

    auto k1 = gen_3_pairs(idx); 
    auto k2 = gen_3_pairs(idx); 
    auto k3 = gen_3_pairs(idx);

    // 3. Distribute the keys among the 4 servers based on the protocol math
    auto add = [&](Role r, const std::vector<uint8_t>& k) { bundles[r].keys.push_back(k); };

    add(Role::P1, k1[0]); add(Role::P1, k2[0]); add(Role::P1, k3[0]); add(Role::P1, k1[5]); add(Role::P1, k2[5]); add(Role::P1, k3[4]);
    add(Role::P2, k1[0]); add(Role::P2, k2[1]); add(Role::P2, k3[1]); add(Role::P2, k2[2]); add(Role::P2, k3[2]); add(Role::P2, k1[4]);
    add(Role::P3, k1[1]); add(Role::P3, k2[0]); add(Role::P3, k3[1]); add(Role::P3, k1[2]); add(Role::P3, k2[4]); add(Role::P3, k3[1]); add(Role::P3, k3[3]);
    add(Role::P4, k1[1]); add(Role::P4, k2[1]); add(Role::P4, k3[0]); add(Role::P4, k3[5]); add(Role::P4, k1[3]); add(Role::P4, k2[3]);

    for(auto& pair : bundles) pair.second.flat_payloads = flat_payloads;
    return bundles;
}

// ============================================================================
// CLIENT HELPER: Generate the keys for a READ operation
// ============================================================================
std::map<Role, std::vector<std::vector<uint8_t>>> gen_read_bundles(uint64_t idx, size_t nitems) {
    std::map<Role, std::vector<std::vector<uint8_t>>> bundles;
    AES_KEY prgkey{};
    uint64_t ALL_ONES = (uint64_t)-1;
    
    auto gen_pair = [&](uint64_t target_idx) {
        auto [k1, k2] = dpf::dpf_key<uint64_t, __m128i, AES_KEY>::gen(prgkey, nitems, target_idx, ALL_ONES);
        return std::make_pair(dpf::serialize_dpf_key(k1), dpf::serialize_dpf_key(k2));
    };
    auto assign = [&](Role r, const std::vector<uint8_t>& key) { bundles[r].push_back(key); };
    auto skip = [&](Role r) { bundles[r].push_back({}); };

    // Generate keys pointing directly to the exact target index (no permutations)
    auto k_d0 = gen_pair(idx); auto k_d1 = gen_pair(idx); 
    auto k_d2 = gen_pair(idx); auto k_d3 = gen_pair(idx);  
    auto k_d4 = gen_pair(idx); auto k_d5 = gen_pair(idx); 

    // Server P4 does not participate in reading
    skip(Role::P4); skip(Role::P4); skip(Role::P4); skip(Role::P4); skip(Role::P4); skip(Role::P4);

    assign(Role::P1, k_d0.first); assign(Role::P2, k_d0.second); skip(Role::P3);
    assign(Role::P1, k_d2.first); skip(Role::P2); assign(Role::P3, k_d2.second);
    skip(Role::P1); assign(Role::P2, k_d5.first); assign(Role::P3, k_d5.second);
    assign(Role::P3, k_d1.first); assign(Role::P1, k_d1.second); assign(Role::P2, k_d1.second);
    assign(Role::P2, k_d3.first); assign(Role::P1, k_d3.second); assign(Role::P3, k_d3.second);
    assign(Role::P1, k_d4.first); assign(Role::P2, k_d4.second); assign(Role::P3, k_d4.second);

    return bundles;
}



// ============================================================================
// MAIN CLIENT PROCESS
// ============================================================================
awaitable<void> run_client(boost::asio::io_context& io, size_t nitems, size_t ncols, int num_ops) {
    try {
        // 1. Connect to all 4 servers
        auto s1 = co_await connect_with_retry(io, LOCALHOST, BASE_PORT + 5);
        auto s2 = co_await connect_with_retry(io, LOCALHOST, BASE_PORT + 55);
        auto s3 = co_await connect_with_retry(io, LOCALHOST, BASE_PORT + 555);
        auto s4 = co_await connect_with_retry(io, LOCALHOST, BASE_PORT + 5555);

        NetPeer p1(Role::P1, std::move(s1)), p2(Role::P2, std::move(s2)),
                p3(Role::P3, std::move(s3)), p4(Role::P4, std::move(s4));

        MPCContext ctx(Role::Client, p1);
        ctx.add_peer(Role::P1, &p1); ctx.add_peer(Role::P2, &p2);
        ctx.add_peer(Role::P3, &p3); ctx.add_peer(Role::P4, &p4);

        std::cout << "\n[Client] Connected. DB Size: " << nitems << " | Columns: " << ncols << " | Ops: " << num_ops << std::endl;

        std::random_device rd;
        std::mt19937_64 gen(rd());
        std::uniform_int_distribution<uint64_t> dist_idx(0, nitems - 1);

        // Wait for all servers to be ready
        uint8_t dummy; 
        for(auto r : {Role::P1, Role::P2, Role::P3, Role::P4}) { co_await (ctx.get_peer(r) >> dummy); }

        // --- WARMUP PHASE ---
        std::cout << "[Client] Executing Warmup (discarding metrics)..." << std::flush;
        std::vector<leaf_t> w_vals(ncols, 0);
        auto b_write = gen_update_bundles(0, w_vals, nitems, ncols);
        uint8_t w_op = 0;
        for(auto r : {Role::P1, Role::P2, Role::P3, Role::P4}) co_await ctx.get_peer(r).send(w_op);
        for(auto r : {Role::P1, Role::P2, Role::P3, Role::P4}) {
            co_await ctx.get_peer(r).send_bundles(b_write[r].keys);
            co_await ctx.get_peer(r).send_vector(b_write[r].flat_payloads);
        }
        
        double dummy_t; 
        for(auto r : {Role::P1, Role::P2, Role::P3, Role::P4}) { 
            co_await (ctx.get_peer(r) >> dummy_t); // eval_ms
            co_await (ctx.get_peer(r) >> dummy_t); // xor_ms
        }
        
        auto b_read = gen_read_bundles(0, nitems);
        uint8_t r_op = 1;
        for(auto r : {Role::P1, Role::P2, Role::P3, Role::P4}) co_await ctx.get_peer(r).send(r_op);
        for(auto r : {Role::P1, Role::P2, Role::P3}) co_await ctx.get_peer(r).send_bundles(b_read[r]);
        leaf_t d_v;
        for(auto r : {Role::P1, Role::P2, Role::P3}) {
            for(size_t c=0; c<ncols; ++c) co_await (ctx.get_peer(r) >> d_v); 
            co_await (ctx.get_peer(r) >> dummy_t); // eval_ms
            co_await (ctx.get_peer(r) >> dummy_t); // xor_ms
        }
        std::cout << " Done!\n" << std::endl;
        // --- END WARMUP ---

        // --- Benchmarking Trackers ---
        double w_query_tot = 0, w_eval_tot = 0, w_xor_tot = 0;
        double r_query_tot = 0, r_eval_tot = 0, r_xor_tot = 0, r_recon_tot = 0;
        uint64_t w_up_tot = 0, w_down_tot = 0;
        uint64_t r_up_tot = 0, r_down_tot = 0;

        for(int i = 0; i < num_ops; ++i) {
            uint64_t idx = dist_idx(gen);
            
            std::vector<leaf_t> write_vals(ncols);
            for(size_t c = 0; c < ncols; ++c) write_vals[c] = idx + 1 + c; 

            // ================================================================
            // WRITE OPERATION
            // ================================================================
            auto tw_query_start = std::chrono::high_resolution_clock::now();
            auto bundles_upd = gen_update_bundles(idx, write_vals, nitems, ncols);
            auto tw_query_end = std::chrono::high_resolution_clock::now();
            double dw_query = std::chrono::duration_cast<std::chrono::nanoseconds>(tw_query_end - tw_query_start).count() / 1000000.0;
            w_query_tot += dw_query;

            uint64_t w_up_this_op = 0;
            for(auto r : {Role::P1, Role::P2, Role::P3, Role::P4}) {
                w_up_this_op += 1; 
                for(const auto& key : bundles_upd[r].keys) w_up_this_op += key.size();
                w_up_this_op += bundles_upd[r].flat_payloads.size() * sizeof(leaf_t);
            }
            w_up_tot += w_up_this_op;

            uint8_t op_write = 0; 
            for(auto r : {Role::P1, Role::P2, Role::P3, Role::P4}) co_await ctx.get_peer(r).send(op_write);
            for(auto r : {Role::P1, Role::P2, Role::P3, Role::P4}) {
                co_await ctx.get_peer(r).send_bundles(bundles_upd[r].keys);
                co_await ctx.get_peer(r).send_vector(bundles_upd[r].flat_payloads);
            }
            
            double we1=0, wx1=0, we2=0, wx2=0, we3=0, wx3=0, we4=0, wx4=0;
            co_await (ctx.get_peer(Role::P1) >> we1); co_await (ctx.get_peer(Role::P1) >> wx1);
            co_await (ctx.get_peer(Role::P2) >> we2); co_await (ctx.get_peer(Role::P2) >> wx2);
            co_await (ctx.get_peer(Role::P3) >> we3); co_await (ctx.get_peer(Role::P3) >> wx3);
            co_await (ctx.get_peer(Role::P4) >> we4); co_await (ctx.get_peer(Role::P4) >> wx4);
            
            double dw_eval = std::max({we1, we2, we3, we4});
            double dw_xor  = std::max({wx1, wx2, wx3, wx4});
            w_eval_tot += dw_eval; w_xor_tot += dw_xor;

            // ================================================================
            // READ OPERATION
            // ================================================================
            auto tr_query_start = std::chrono::high_resolution_clock::now();
            auto bundles_read = gen_read_bundles(idx, nitems);
            auto tr_query_end = std::chrono::high_resolution_clock::now();
            double dr_query = std::chrono::duration_cast<std::chrono::nanoseconds>(tr_query_end - tr_query_start).count() / 1000000.0;
            r_query_tot += dr_query;

            uint64_t r_up_this_op = 4; // 4 op codes
            for(auto r : {Role::P1, Role::P2, Role::P3}) {
                for(const auto& key : bundles_read[r]) r_up_this_op += key.size();
            }
            r_up_tot += r_up_this_op;
            uint64_t r_down_this_op = 3 * (ncols * sizeof(leaf_t)); // 3 servers reply with data
            r_down_tot += r_down_this_op;

            uint8_t op_read = 1; 
            for(auto r : {Role::P1, Role::P2, Role::P3, Role::P4}) co_await ctx.get_peer(r).send(op_read);
            for(auto r : {Role::P1, Role::P2, Role::P3}) co_await ctx.get_peer(r).send_bundles(bundles_read[r]);
            
            std::vector<leaf_t> r1(ncols), r2(ncols), r3(ncols);
            double re1=0, rx1=0, re2=0, rx2=0, re3=0, rx3=0;
            
            for(size_t c=0; c<ncols; ++c) co_await (ctx.get_peer(Role::P1) >> r1[c]); 
            co_await (ctx.get_peer(Role::P1) >> re1); co_await (ctx.get_peer(Role::P1) >> rx1);
            
            for(size_t c=0; c<ncols; ++c) co_await (ctx.get_peer(Role::P2) >> r2[c]); 
            co_await (ctx.get_peer(Role::P2) >> re2); co_await (ctx.get_peer(Role::P2) >> rx2);
            
            for(size_t c=0; c<ncols; ++c) co_await (ctx.get_peer(Role::P3) >> r3[c]); 
            co_await (ctx.get_peer(Role::P3) >> re3); co_await (ctx.get_peer(Role::P3) >> rx3);

            double dr_eval = std::max({re1, re2, re3});
            double dr_xor  = std::max({rx1, rx2, rx3});
            r_eval_tot += dr_eval; r_xor_tot += dr_xor;

            // CLIENT RECONSTRUCTION
            auto tr_recon_start = std::chrono::high_resolution_clock::now();
            std::vector<leaf_t> result(ncols);
            bool read_ok = true;
            for(size_t c=0; c<ncols; ++c) {
                result[c] = r1[c] ^ r2[c] ^ r3[c]; // XOR the 3 server replies together
                if (result[c] != write_vals[c]) read_ok = false;
            }
            auto tr_recon_end = std::chrono::high_resolution_clock::now();
            double dr_recon = std::chrono::duration_cast<std::chrono::nanoseconds>(tr_recon_end - tr_recon_start).count() / 1000000.0;
            r_recon_tot += dr_recon;

            std::cout << "Op " << i+1 << "/" << num_ops << " [Idx: " << idx << "] " << (read_ok ? "\033[32m[OK]\033[0m" : "\033[31m[FAIL]\033[0m") << "\n";
            std::cout << std::fixed << std::setprecision(3) 
                      << "  ├─ Write -> Query: " << dw_query << "ms | Server Eval: " << dw_eval << "ms | Server XOR: " << dw_xor << "ms\n"
                      << "  └─ Read  -> Query: " << dr_query << "ms | Server Eval: " << dr_eval << "ms | Server XOR: " << dr_xor << "ms | Recon: " << dr_recon << "ms\n\n";
        }

        uint8_t op_kill = 2; 
        for(auto r : {Role::P1, Role::P2, Role::P3, Role::P4}) co_await ctx.get_peer(r).send(op_kill);

        std::cout << "================ WRITE AVERAGES =================" << std::endl;
        std::cout << "Avg Query Time        : " << std::fixed << std::setprecision(4) << (w_query_tot / num_ops) << " ms" << std::endl;
        std::cout << "Avg Server Evalfull   : " << std::fixed << std::setprecision(4) << (w_eval_tot / num_ops) << " ms" << std::endl;
        std::cout << "Avg Server XOR Merge  : " << std::fixed << std::setprecision(4) << (w_xor_tot / num_ops) << " ms" << std::endl;
        std::cout << "Avg Upload / Op       : " << (w_up_tot / num_ops) << " Bytes" << std::endl;
        std::cout << "================ READ AVERAGES ==================" << std::endl;
        std::cout << "Avg Query Time        : " << std::fixed << std::setprecision(4) << (r_query_tot / num_ops) << " ms" << std::endl;
        std::cout << "Avg Server Evalfull   : " << std::fixed << std::setprecision(4) << (r_eval_tot / num_ops) << " ms" << std::endl;
        std::cout << "Avg Server XOR Merge  : " << std::fixed << std::setprecision(4) << (r_xor_tot / num_ops) << " ms" << std::endl;
        std::cout << "Avg Reconstruct Time  : " << std::fixed << std::setprecision(4) << (r_recon_tot / num_ops) << " ms" << std::endl;
        std::cout << "Avg Upload / Op       : " << (r_up_tot / num_ops) << " Bytes" << std::endl;
        std::cout << "Avg Download / Op     : " << (r_down_tot / num_ops) << " Bytes" << std::endl;
        std::cout << "=================================================\n" << std::endl;

    } catch (std::exception& e) { std::cerr << "[Client Error] " << e.what() << std::endl; }
}

// ============================================================================
// SERVER SETUP
// ============================================================================
awaitable<void> run_party_node(boost::asio::io_context& io, Role role, size_t nitems, size_t ncols) {
    try {
        std::vector<std::unique_ptr<NetPeer>> peers_list;
        auto add = [&](Role r, tcp::socket s) { peers_list.push_back(std::make_unique<NetPeer>(r, std::move(s))); };
        
        // Setup the local networking rings
        if (role == Role::P1) {
            add(Role::P2, co_await make_server(io, BASE_PORT + 2)); add(Role::P3, co_await make_server(io, BASE_PORT + 3)); add(Role::P4, co_await make_server(io, BASE_PORT + 4)); add(Role::Client, co_await make_server(io, BASE_PORT + 5));
        } else if (role == Role::P2) {
            add(Role::P1, co_await connect_with_retry(io, LOCALHOST, BASE_PORT + 2)); add(Role::P3, co_await make_server(io, BASE_PORT + 33)); add(Role::P4, co_await make_server(io, BASE_PORT + 44)); add(Role::Client, co_await make_server(io, BASE_PORT + 55));
        } else if (role == Role::P3) {
            add(Role::P1, co_await connect_with_retry(io, LOCALHOST, BASE_PORT + 3)); add(Role::P2, co_await connect_with_retry(io, LOCALHOST, BASE_PORT + 33)); add(Role::P4, co_await make_server(io, BASE_PORT + 444)); add(Role::Client, co_await make_server(io, BASE_PORT + 555));
        } else if (role == Role::P4) {
            add(Role::P1, co_await connect_with_retry(io, LOCALHOST, BASE_PORT + 4)); add(Role::P2, co_await connect_with_retry(io, LOCALHOST, BASE_PORT + 44)); add(Role::P3, co_await connect_with_retry(io, LOCALHOST, BASE_PORT + 444)); add(Role::Client, co_await make_server(io, BASE_PORT + 5555));
        }
        
        tcp::socket dummy(io); NetPeer self_node(role, std::move(dummy));
        MPCContext ctx(role, self_node);
        for(auto& p : peers_list) ctx.add_peer(p->role, p.get());
        
        Locoram<leaf_t, __m128i, AES_KEY> loc(nitems, ncols, role);
        std::cout << "[P" << (int)role << "] Ready. (Cols: " << ncols << ")\n";

        // Let the client know this server has booted and is ready to accept commands
        try { uint8_t ready = 1; co_await ctx.get_peer(Role::Client).send(ready); } catch (...) {}

        // Infinite loop waiting for client commands (0=Write, 1=Read, 2=Kill)
        while(true) {
            uint8_t op = 0;
            try { op = co_await ctx.get_peer(Role::Client).recv<uint8_t>(); } catch (...) { break; }
            if (op == 0) co_await loc.apply_rss_update(&ctx);
            else if (op == 1) co_await loc.apply_rss_read(&ctx);
            else if (op == 2) break;
        }
    } catch (std::exception& e) { std::cerr << "[Node Error] " << e.what() << std::endl; }
}

int main(int argc, char* argv[]) {
    if (argc < 4) {
        std::cerr << "Usage: " << argv[0] << " <party> <power> <num_ops>\n"; return 1;
    }

    std::string r = argv[1];
    int power = std::stoi(argv[2]);
    int num_ops = std::stoi(argv[3]);
    size_t nitems = 1ULL << power; 
    
    size_t ncols = 3; // Easily adjust how many columns the database stores

    boost::asio::io_context io;

    if (r == "p1") co_spawn(io, run_party_node(io, Role::P1, nitems, ncols), detached);
    else if (r == "p2") co_spawn(io, run_party_node(io, Role::P2, nitems, ncols), detached);
    else if (r == "p3") co_spawn(io, run_party_node(io, Role::P3, nitems, ncols), detached);
    else if (r == "p4") co_spawn(io, run_party_node(io, Role::P4, nitems, ncols), detached);
    else if (r == "client") co_spawn(io, run_client(io, nitems, ncols, num_ops), detached);
    else { std::cerr << "Invalid party! Use: p1..p4, or client\n"; return 1; }
    
    io.run();
    return 0;
}