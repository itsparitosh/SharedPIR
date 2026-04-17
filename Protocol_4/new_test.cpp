
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

// ============================================================================
// CLIENT: GENERATE READ KEYS
// ============================================================================
std::map<Role, std::vector<std::vector<uint8_t>>> gen_read_bundles(uint64_t idx, size_t nitems) {
    std::map<Role, std::vector<std::vector<uint8_t>>> bundles;
    AES_KEY prgkey{};
    leaf_t ALL_ONES = (leaf_t)-1; // A 64-bit value where every bit is a '1'

    
    // Helper to generate a pair of keys pointing to the target row
    auto gen_pair = [&](uint64_t pos) {
        auto [k0, k1] = dpf::dpf_key<leaf_t, __m128i, AES_KEY>::gen(prgkey, nitems, pos, ALL_ONES); 
        return std::make_pair(dpf::serialize_dpf_key(k0), dpf::serialize_dpf_key(k1));
    };

    // Generating DPF keys
    auto K0 = gen_pair(idx); auto K1 = gen_pair(idx); auto K2 = gen_pair(idx); auto K3 = gen_pair(idx); 
    auto K4 = gen_pair(idx); auto K5 = gen_pair(idx); auto K6 = gen_pair(idx); auto K7 = gen_pair(idx); 

    auto assign = [&](Role r, const std::vector<uint8_t>& key) { bundles[r].push_back(key); };

    // Distribute halves of the keys to the 4 parties securely
    assign(Role::P0, K5.first);  assign(Role::P0, K7.first);  assign(Role::P0, K0.first);  assign(Role::P0, K4.second); assign(Role::P0, K6.second);
    assign(Role::P1, K1.first);  assign(Role::P1, K7.second); assign(Role::P1, K2.first);  assign(Role::P1, K6.second); assign(Role::P1, K0.second);
    assign(Role::P2, K1.second); assign(Role::P2, K3.first);  assign(Role::P2, K4.first);  assign(Role::P2, K0.second); assign(Role::P2, K2.second);
    assign(Role::P3, K3.second); assign(Role::P3, K5.second); assign(Role::P3, K6.first);  assign(Role::P3, K2.second); assign(Role::P3, K4.second);

    return bundles;
}

// A custom bundle that holds BOTH the DPF Keys and the Data Payloads to write
struct WriteBundle {
    std::vector<std::vector<uint8_t>> keys;
    std::vector<leaf_t> flat_payloads;
};

// ============================================================================
// CLIENT: GENERATE WRITE KEYS & DATA
// ============================================================================
std::map<Role, WriteBundle> gen_write_bundles(uint64_t idx, const std::vector<leaf_t>& values, size_t nitems, size_t ncols) {
    std::map<Role, WriteBundle> bundles;
    AES_KEY prgkey{};
    static std::mt19937_64 gen(std::random_device{}());
    leaf_t ALL_ONES = (leaf_t)-1;

    // Generate mask keys (Same as Read)
    auto gen_pair = [&](uint64_t pos) {
        auto [k0, k1] = dpf::dpf_key<leaf_t, __m128i, AES_KEY>::gen(prgkey, nitems, pos, ALL_ONES);
        return std::make_pair(dpf::serialize_dpf_key(k0), dpf::serialize_dpf_key(k1));
    };

    auto pairA0 = gen_pair(idx); auto pairB0 = gen_pair(idx);
    auto pairA1 = gen_pair(idx); auto pairB1 = gen_pair(idx);
    auto pairA2 = gen_pair(idx); auto pairB2 = gen_pair(idx);
    auto pairA3 = gen_pair(idx); auto pairB3 = gen_pair(idx);

    // Create Secret Shares of the actual column data.
    // M0, M1, and M2 are pure random garbage. 
    // M3 contains the actual data mixed with M0, M1, M2.
    std::vector<leaf_t> M0(ncols), M1(ncols), M2(ncols), M3(ncols);
    for(size_t c = 0; c < ncols; ++c) {
        M0[c] = gen(); M1[c] = gen(); M2[c] = gen();
        M3[c] = values[c] ^ M0[c] ^ M1[c] ^ M2[c];
    }

    auto assign = [&](Role r, const std::vector<uint8_t>& key, const std::vector<leaf_t>& payload) { 
        bundles[r].keys.push_back(key); 
        // Insert the specific data payload (M0, M1, etc) that goes with this key
        bundles[r].flat_payloads.insert(bundles[r].flat_payloads.end(), payload.begin(), payload.end());
    };

    assign(Role::P0, pairA0.first, M0); assign(Role::P0, pairA2.second, M2); assign(Role::P0, pairA3.second, M3); assign(Role::P0, pairB2.second, M2); assign(Role::P0, pairB3.first, M3);
    assign(Role::P1, pairA1.first, M1); assign(Role::P1, pairA0.second, M0); assign(Role::P1, pairA3.second, M3); assign(Role::P1, pairB3.second, M3); assign(Role::P1, pairB0.first, M0);
    assign(Role::P2, pairA2.first, M2); assign(Role::P2, pairA0.second, M0); assign(Role::P2, pairA1.second, M1); assign(Role::P2, pairB0.second, M0); assign(Role::P2, pairB1.first, M1);
    assign(Role::P3, pairA3.first, M3); assign(Role::P3, pairA1.second, M1); assign(Role::P3, pairA2.second, M2); assign(Role::P3, pairB1.second, M1); assign(Role::P3, pairB2.first, M2);

    return bundles;
}

// ============================================================================
// CLIENT PROCESS
// ============================================================================
awaitable<void> run_client(boost::asio::io_context& io, size_t nitems, size_t ncols, int num_ops) {
    try {
        // Connect to the 4 servers over TCP Sockets
        auto s1 = co_await connect_with_retry(io, LOCALHOST, BASE_PORT + 11);
        auto s2 = co_await connect_with_retry(io, LOCALHOST, BASE_PORT + 22);
        auto s3 = co_await connect_with_retry(io, LOCALHOST, BASE_PORT + 33);
        auto s4 = co_await connect_with_retry(io, LOCALHOST, BASE_PORT + 44);

        NetPeer p0(Role::P0, std::move(s1)), p1(Role::P1, std::move(s2)),
                p2(Role::P2, std::move(s3)), p3(Role::P3, std::move(s4));

        MPCContext ctx(Role::Client, p0);
        ctx.add_peer(Role::P0, &p0); ctx.add_peer(Role::P1, &p1);
        ctx.add_peer(Role::P2, &p2); ctx.add_peer(Role::P3, &p3);

        std::cout << "\n[Client] Connected. DB Size: " << nitems << " | Columns: " << ncols << " | Ops: " << num_ops << std::endl;

        std::random_device rd;
        std::mt19937_64 gen(rd());
        std::uniform_int_distribution<uint64_t> dist_idx(0, nitems - 1);


        // ====================================================================
        // WARMUP PHASE
        // We do one fake "practice" run first. This wakes up the computer's memory
        // and network sockets. We throw away the time it took so it doesn't mess up our average.
        // ====================================================================
        uint8_t dummy; 
        for(auto r : {Role::P0, Role::P1, Role::P2, Role::P3}) co_await (ctx.get_peer(r) >> dummy);
        std::cout << "[Client] Executing Warmup (discarding metrics)..." << std::flush;
        std::vector<leaf_t> w_vals(ncols, 0);
        auto b_write = gen_write_bundles(0, w_vals, nitems, ncols);
        uint8_t w_op = 2;
        for(auto r : {Role::P0, Role::P1, Role::P2, Role::P3}) co_await ctx.get_peer(r).send(w_op);
        for(auto r : {Role::P0, Role::P1, Role::P2, Role::P3}) { co_await ctx.get_peer(r).send_bundles(b_write[r].keys); co_await ctx.get_peer(r).send_vector(b_write[r].flat_payloads); }
        uint64_t d_t1, d_t2; 
        for(auto r : {Role::P0, Role::P1, Role::P2, Role::P3}) { co_await (ctx.get_peer(r) >> d_t1); co_await (ctx.get_peer(r) >> d_t2); }
        auto b_read = gen_read_bundles(0, nitems);
        uint8_t r_op = 1;
        for(auto r : {Role::P0, Role::P1, Role::P2, Role::P3}) co_await ctx.get_peer(r).send(r_op);
        for(auto r : {Role::P0, Role::P1, Role::P2, Role::P3}) co_await ctx.get_peer(r).send_bundles(b_read[r]);
        leaf_t d_v;
        for(auto r : {Role::P0, Role::P1, Role::P2, Role::P3}) { for(size_t c=0; c<ncols; ++c) co_await (ctx.get_peer(r) >> d_v); co_await (ctx.get_peer(r) >> d_t1); co_await (ctx.get_peer(r) >> d_t2); }
        std::cout << " Done!\n" << std::endl;
        // --- END WARMUP ---


        double w_query_tot = 0, w_dpf_tot = 0, w_xor_tot = 0;
        double r_query_tot = 0, r_dpf_tot = 0, r_xor_tot = 0, r_recon_tot = 0;
        size_t w_up_tot = 0, w_down_tot = 0, r_up_tot = 0, r_down_tot = 0;


        // ====================================================================
        // REAL TESTING LOOP
        // ====================================================================
        for(int i = 0; i < num_ops; ++i) {
            uint64_t idx = dist_idx(gen);
            
            std::vector<leaf_t> write_vals(ncols);
            for(size_t c = 0; c < ncols; ++c) write_vals[c] = idx + 1 + c; // Test data (idx+1, idx+2...)
            
            // ================================================================
            // 1. EXECUTE WRITE
            // ================================================================
            auto tw_query_start = std::chrono::high_resolution_clock::now();
            auto bundles_write = gen_write_bundles(idx, write_vals, nitems, ncols);
            auto tw_query_end = std::chrono::high_resolution_clock::now();
            double dw_query = std::chrono::duration_cast<std::chrono::nanoseconds>(tw_query_end - tw_query_start).count() / 1000000.0;
            w_query_tot += dw_query;

            // Calculate exact Upload byte size per server
            size_t w_up_bytes = 0, w_down_bytes = 0;
            for(auto r : {Role::P0, Role::P1, Role::P2, Role::P3}) {
                size_t current_up = 1; // 1 byte for OP Code
                for(const auto& k : bundles_write[r].keys) current_up += k.size();
                current_up += bundles_write[r].flat_payloads.size() * sizeof(leaf_t);
                w_up_bytes = std::max(w_up_bytes, current_up); // Bandwidth is determined by the maximum sent to any one server
            }
            w_up_tot += w_up_bytes;
            w_down_tot += w_down_bytes; // Writes do not download any data

            uint8_t op_write = 2; 
            for(auto r : {Role::P0, Role::P1, Role::P2, Role::P3}) co_await ctx.get_peer(r).send(op_write);
            for(auto r : {Role::P0, Role::P1, Role::P2, Role::P3}) {
                co_await ctx.get_peer(r).send_bundles(bundles_write[r].keys);
                co_await ctx.get_peer(r).send_vector(bundles_write[r].flat_payloads);
            }
            
            uint64_t w_dpf0 = 0, w_dpf1 = 0, w_dpf2 = 0, w_dpf3 = 0;
            uint64_t w_xor0 = 0, w_xor1 = 0, w_xor2 = 0, w_xor3 = 0;

            co_await (ctx.get_peer(Role::P0) >> w_dpf0); co_await (ctx.get_peer(Role::P0) >> w_xor0);
            co_await (ctx.get_peer(Role::P1) >> w_dpf1); co_await (ctx.get_peer(Role::P1) >> w_xor1); 
            co_await (ctx.get_peer(Role::P2) >> w_dpf2); co_await (ctx.get_peer(Role::P2) >> w_xor2); 
            co_await (ctx.get_peer(Role::P3) >> w_dpf3); co_await (ctx.get_peer(Role::P3) >> w_xor3); 
            
            double dw_dpf = std::max({w_dpf0, w_dpf1, w_dpf2, w_dpf3}) / 1000.0;
            double dw_xor = std::max({w_xor0, w_xor1, w_xor2, w_xor3}) / 1000.0;
            double dw_respond = dw_dpf + dw_xor; 
            w_dpf_tot += dw_dpf; w_xor_tot += dw_xor;

            // ================================================================
            // 2. EXECUTE READ
            // ================================================================
            auto tr_query_start = std::chrono::high_resolution_clock::now();
            auto bundles_read = gen_read_bundles(idx, nitems);
            auto tr_query_end = std::chrono::high_resolution_clock::now();
            double dr_query = std::chrono::duration_cast<std::chrono::nanoseconds>(tr_query_end - tr_query_start).count() / 1000000.0;
            r_query_tot += dr_query;

            size_t r_up_bytes = 0, r_down_bytes = 0;
            for(auto r : {Role::P0, Role::P1, Role::P2, Role::P3}) {
                size_t current_up = 1; 
                for(const auto& k : bundles_read[r]) current_up += k.size();
                r_up_bytes = std::max(r_up_bytes, current_up);
            }
            r_down_bytes = ncols * sizeof(leaf_t); // The server downloads 'ncols' columns back to the client
            r_up_tot += r_up_bytes;
            r_down_tot += r_down_bytes;

            uint8_t op_read = 1; 
            for(auto r : {Role::P0, Role::P1, Role::P2, Role::P3}) co_await ctx.get_peer(r).send(op_read);
            for(auto r : {Role::P0, Role::P1, Role::P2, Role::P3}) co_await ctx.get_peer(r).send_bundles(bundles_read[r]);
            
            std::vector<leaf_t> r0(ncols), r1(ncols), r2(ncols), r3(ncols);
            uint64_t r_dpf0 = 0, r_dpf1 = 0, r_dpf2 = 0, r_dpf3 = 0;
            uint64_t r_xor0 = 0, r_xor1 = 0, r_xor2 = 0, r_xor3 = 0;
            
            for(size_t c=0; c<ncols; ++c) co_await (ctx.get_peer(Role::P0) >> r0[c]); co_await (ctx.get_peer(Role::P0) >> r_dpf0); co_await (ctx.get_peer(Role::P0) >> r_xor0);
            for(size_t c=0; c<ncols; ++c) co_await (ctx.get_peer(Role::P1) >> r1[c]); co_await (ctx.get_peer(Role::P1) >> r_dpf1); co_await (ctx.get_peer(Role::P1) >> r_xor1);
            for(size_t c=0; c<ncols; ++c) co_await (ctx.get_peer(Role::P2) >> r2[c]); co_await (ctx.get_peer(Role::P2) >> r_dpf2); co_await (ctx.get_peer(Role::P2) >> r_xor2);
            for(size_t c=0; c<ncols; ++c) co_await (ctx.get_peer(Role::P3) >> r3[c]); co_await (ctx.get_peer(Role::P3) >> r_dpf3); co_await (ctx.get_peer(Role::P3) >> r_xor3);

            double dr_dpf = std::max({r_dpf0, r_dpf1, r_dpf2, r_dpf3}) / 1000.0;
            double dr_xor = std::max({r_xor0, r_xor1, r_xor2, r_xor3}) / 1000.0;
            double dr_respond = dr_dpf + dr_xor;
            r_dpf_tot += dr_dpf; r_xor_tot += dr_xor;

            // --- RECONSTRUCTION PHASE ---
            auto tr_recon_start = std::chrono::high_resolution_clock::now();
            std::vector<leaf_t> result(ncols);
            bool read_ok = true;
            std::string res_str = "";
            
            // The client takes the column data from the 4 servers and XORs them.
            // This cancels out the blinding factors, leaving only the real database values.
            for(size_t c=0; c<ncols; ++c) {
                result[c] = r0[c] ^ r1[c] ^ r2[c] ^ r3[c];
                res_str += std::to_string(result[c]) + (c == ncols - 1 ? "" : ", ");
                if (result[c] != write_vals[c]) read_ok = false;
            }
            auto tr_recon_end = std::chrono::high_resolution_clock::now();
            double dr_recon = std::chrono::duration_cast<std::chrono::nanoseconds>(tr_recon_end - tr_recon_start).count() / 1000000.0;
            r_recon_tot += dr_recon;

            // Output Results
            std::cout << "Op " << i+1 << "/" << num_ops << " [Idx: " << idx << "]";
            if (read_ok) std::cout << " \033[32m[OK]\033[0m Data: [" << res_str << "]\n"; 
            else std::cout << " \033[31m[FAIL]\033[0m Data: [" << res_str << "]\n"; 

            std::cout << std::fixed << std::setprecision(3) 
                      << "  ├─ Write Stats -> Query: " << dw_query << "ms | Srv-DPF: " << dw_dpf << "ms | Srv-XOR: " << dw_xor << "ms | Up/Srv: " << w_up_bytes << " B | Down/Srv: " << w_down_bytes << " B\n"
                      << "  └─ Read Stats  -> Query: " << dr_query << "ms | Srv-DPF: " << dr_dpf << "ms | Srv-XOR: " << dr_xor << "ms | Recon: " << dr_recon << "ms | Up/Srv: " << r_up_bytes << " B | Down/Srv: " << r_down_bytes << " B\n" << std::endl;
        }

        uint8_t op_kill = 0; 
        for(auto r : {Role::P0, Role::P1, Role::P2, Role::P3}) co_await ctx.get_peer(r).send(op_kill);

        std::cout << "================ WRITE AVERAGES =================" << std::endl;
        std::cout << "Avg Query Time        : " << std::fixed << std::setprecision(4) << (w_query_tot / num_ops) << " ms" << std::endl;
        std::cout << "Avg Server DPF Eval   : " << std::fixed << std::setprecision(4) << (w_dpf_tot / num_ops) << " ms" << std::endl;
        std::cout << "Avg Server XOR Mask   : " << std::fixed << std::setprecision(4) << (w_xor_tot / num_ops) << " ms" << std::endl;
        std::cout << "Avg Server Upload     : " << (w_up_tot / num_ops) << " Bytes" << std::endl;
        std::cout << "Avg Server Download   : " << (w_down_tot / num_ops) << " Bytes" << std::endl;
        std::cout << "================ READ AVERAGES ==================" << std::endl;
        std::cout << "Avg Query Time        : " << std::fixed << std::setprecision(4) << (r_query_tot / num_ops) << " ms" << std::endl;
        std::cout << "Avg Server DPF Eval   : " << std::fixed << std::setprecision(4) << (r_dpf_tot / num_ops) << " ms" << std::endl;
        std::cout << "Avg Server XOR Mask   : " << std::fixed << std::setprecision(4) << (r_xor_tot / num_ops) << " ms" << std::endl;
        std::cout << "Avg Reconstruct Time  : " << std::fixed << std::setprecision(4) << (r_recon_tot / num_ops) << " ms" << std::endl;
        std::cout << "Avg Server Upload     : " << (r_up_tot / num_ops) << " Bytes" << std::endl;
        std::cout << "Avg Server Download   : " << (r_down_tot / num_ops) << " Bytes" << std::endl;
        std::cout << "=================================================" << std::endl;

    } catch (std::exception& e) { std::cerr << "[Client Error] " << e.what() << std::endl; }
}

// ============================================================================
// INITIALIZATION
// ============================================================================
awaitable<void> run_party_node(boost::asio::io_context& io, Role role, size_t nitems, size_t ncols) {
    try {
        std::vector<std::unique_ptr<NetPeer>> peers;
        auto add = [&](Role r, tcp::socket s) { peers.push_back(std::make_unique<NetPeer>(r, std::move(s))); };
        
        // Create the network sockets
        if (role == Role::P0) add(Role::Client, co_await make_server(io, BASE_PORT + 11));
        else if (role == Role::P1) add(Role::Client, co_await make_server(io, BASE_PORT + 22));
        else if (role == Role::P2) add(Role::Client, co_await make_server(io, BASE_PORT + 33));
        else if (role == Role::P3) add(Role::Client, co_await make_server(io, BASE_PORT + 44));

        tcp::socket dummy(io); NetPeer self_node(role, std::move(dummy));
        MPCContext ctx(role, self_node);
        for(auto& p : peers) ctx.add_peer(p->role, p.get());
        
        Locoram<leaf_t, __m128i, AES_KEY> loc(nitems, ncols, role);
        std::cout << "[P" << (int)role << "] Ready. (Cols: " << ncols << ")\n";

        try { uint8_t ready = 1; co_await ctx.get_peer(Role::Client).send(ready); } catch (...) {}

        // Infinite loop to listen for commands
        while(true) {
            uint8_t op = 0;
            try { op = co_await ctx.get_peer(Role::Client).recv<uint8_t>(); } catch (...) { break; }
            if (op == 1) co_await loc.apply_rss_read(&ctx);
            else if (op == 2) co_await loc.apply_rss_write(&ctx);
            else if (op == 0) break;
        }
    } catch (std::exception& e) { std::cerr << "[Node Error] " << e.what() << std::endl; }
}

int main(int argc, char* argv[]) {
    if (argc < 4) { std::cerr << "Usage: " << argv[0] << " <party> <power> <num_ops>\n"; return 1; }

    std::string r = argv[1];
    int power = std::stoi(argv[2]);
    int num_ops = std::stoi(argv[3]);
    size_t nitems = 1ULL << power; 
    
    // Set 3 columns per row
    size_t ncols = 3; 

    boost::asio::io_context io;

    if (r == "p0") co_spawn(io, run_party_node(io, Role::P0, nitems, ncols), detached);
    else if (r == "p1") co_spawn(io, run_party_node(io, Role::P1, nitems, ncols), detached);
    else if (r == "p2") co_spawn(io, run_party_node(io, Role::P2, nitems, ncols), detached);
    else if (r == "p3") co_spawn(io, run_party_node(io, Role::P3, nitems, ncols), detached);
    else if (r == "client") co_spawn(io, run_client(io, nitems, ncols, num_ops), detached);
    else { std::cerr << "Invalid party! Use: p0..p3, or client\n"; return 1; }
    
    io.run();
    return 0;
}