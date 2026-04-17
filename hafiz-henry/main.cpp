
#include <boost/asio.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <iostream>
#include <iomanip>
#include <random>
#include <chrono>
#include <cstring>
#include <algorithm> 

#include "network.hpp"
#include "dpf.hpp"
#include "pir_database.hpp"

using namespace boost::asio;
using ip::tcp;

const uint16_t BASE_PORT = 10000;
const std::string LOCALHOST = "127.0.0.1";

// ============================================================================
// CLIENT RUNNER
// ============================================================================
awaitable<void> run_client(boost::asio::io_context& io, size_t nitems, int num_ops) {
    try {
        // 1. Connect to the 4 Servers
        auto s0 = co_await connect_with_retry(io, LOCALHOST, BASE_PORT + 0, 10);
        auto s1 = co_await connect_with_retry(io, LOCALHOST, BASE_PORT + 1, 10);
        auto s2 = co_await connect_with_retry(io, LOCALHOST, BASE_PORT + 2, 10);
        auto s3 = co_await connect_with_retry(io, LOCALHOST, BASE_PORT + 3, 10);

        NetPeer p0(Role::P0, std::move(s0)), p1(Role::P1, std::move(s1)),
                p2(Role::P2, std::move(s2)), p3(Role::P3, std::move(s3));

        std::cout << "\n[Client] Connected. DB Size: 2^" << (int)(log2(nitems)) 
                  << " (" << nitems << " rows) | Operations: " << num_ops << std::endl;

        // Securely initialize AES Key
        AES_KEY prg_key;
        std::memset(&prg_key, 0, sizeof(AES_KEY));

        std::random_device rd;
        std::mt19937_64 gen(rd());
        std::uniform_int_distribution<uint64_t> dist_idx(0, nitems - 1);

        double total_query_time = 0;
        double total_respond_time = 0;
        double total_recon_time = 0;
        
        // Communication Cost Trackers ---
        uint64_t total_upload_bytes = 0;
        uint64_t total_download_bytes = 0;

        // --- BARRIER SYNC ---
        std::cout << "[Client] Waiting for servers to initialize..." << std::flush;
        uint8_t dummy; 
        co_await (p0 >> dummy); co_await (p1 >> dummy); 
        co_await (p2 >> dummy); co_await (p3 >> dummy);
        std::cout << " Done!\n" << std::endl;

        // 2. Execution Loop
        for(int i = 0; i < num_ops; ++i) {
            uint64_t target_idx = dist_idx(gen);
            
            uint64_t expected_w0 = target_idx + 1;
            uint64_t expected_w1 = target_idx + 2;
            uint64_t expected_w2 = target_idx + 3;

            std::cout << "Op " << i+1 << "/" << num_ops << " [Reading Idx: " << target_idx << "] " << std::flush;

            // =========================================================
            // 1. QUERY PHASE
            // =========================================================
            auto t_query_start = std::chrono::high_resolution_clock::now();
            __m128i dummy_target_val = _mm_setzero_si128();
            auto pair_L1 = dpf::dpf_key<__m128i, __m128i, AES_KEY>::gen(prg_key, nitems, target_idx, dummy_target_val);
            auto pair_L0 = dpf::dpf_key<__m128i, __m128i, AES_KEY>::gen(prg_key, nitems, target_idx, dummy_target_val);
            auto t_query_end = std::chrono::high_resolution_clock::now();
            
            double dur_query = std::chrono::duration_cast<std::chrono::nanoseconds>(t_query_end - t_query_start).count() / 1000000.0;
            total_query_time += dur_query;

            // --- CALCULATE UPLOAD COST ---
            // Serialize keys locally first to measure their exact byte size
            auto s_L1_first = dpf::serialize_dpf_key(pair_L1.first);
            auto s_L1_second = dpf::serialize_dpf_key(pair_L1.second);
            auto s_L0_first = dpf::serialize_dpf_key(pair_L0.first);
            auto s_L0_second = dpf::serialize_dpf_key(pair_L0.second);

            // size of the two keys assigned to each server
            uint64_t up_s0 = s_L1_first.size()  + s_L0_first.size();
            uint64_t up_s1 = s_L1_first.size()  + s_L0_second.size();
            uint64_t up_s2 = s_L1_second.size() + s_L0_first.size();
            uint64_t up_s3 = s_L1_second.size() + s_L0_second.size();
            
            uint64_t upload_this_op = up_s0 + up_s1 +up_s2 +up_s3;  // total upload bytes
            total_upload_bytes += upload_this_op;

            // Send queries over network using pre-serialized keys
            auto send_read = [](NetPeer& p, const std::vector<uint8_t>& k1, const std::vector<uint8_t>& k0) -> awaitable<void> {
                uint8_t op = 1; co_await (p << op); 
                co_await (p << k1);
                co_await (p << k0);
            };

            co_await send_read(p0, s_L1_first,  s_L0_first);
            co_await send_read(p1, s_L1_first,  s_L0_second);
            co_await send_read(p2, s_L1_second, s_L0_first);
            co_await send_read(p3, s_L1_second, s_L0_second);

            // Receive Responses and Server Times
            uint64_t r0=0, r1=0, r2=0, r3=0;
            double t0=0, t1=0, t2=0, t3=0;

            co_await (p0 >> r0); co_await (p0 >> t0);
            co_await (p1 >> r1); co_await (p1 >> t1);
            co_await (p2 >> r2); co_await (p2 >> t2);
            co_await (p3 >> r3); co_await (p3 >> t3);

            // --- CALCULATE DOWNLOAD COST ---
            // Only counting the actual data payload (r0, r1, r2, r3). 
            // We strictly ignore the t0-t3 benchmarking variables.
            uint64_t download_this_op = 4 * sizeof(r0); // 4 servers * 8 bytes
            total_download_bytes += download_this_op;

            // =========================================================
            // 2. RESPONS PHASE TIME (Max of all server process times)
            // =========================================================
            double dur_respond = std::max({t0, t1, t2, t3});
            total_respond_time += dur_respond;

            // =========================================================
            // 3. RECONSTRUCT PHASE 
            // =========================================================
            auto t_rec_start = std::chrono::high_resolution_clock::now();
            
            std::vector<__m128i> dummy_eval(nitems); 
            std::vector<uint8_t> flags_L1_0(nitems), flags_L1_1(nitems);
            std::vector<uint8_t> flags_L0_0(nitems), flags_L0_1(nitems);

            dpf::__evalinterval(pair_L1.first, 0, nitems - 1, dummy_eval.data(), flags_L1_0.data());
            dpf::__evalinterval(pair_L1.second, 0, nitems - 1, dummy_eval.data(), flags_L1_1.data());
            dpf::__evalinterval(pair_L0.first, 0, nitems - 1, dummy_eval.data(), flags_L0_0.data());
            dpf::__evalinterval(pair_L0.second, 0, nitems - 1, dummy_eval.data(), flags_L0_1.data());
            
            uint8_t sigma[4] = {
                static_cast<uint8_t>((flags_L1_0[target_idx] << 1) | flags_L0_0[target_idx]),
                static_cast<uint8_t>((flags_L1_0[target_idx] << 1) | flags_L0_1[target_idx]),
                static_cast<uint8_t>((flags_L1_1[target_idx] << 1) | flags_L0_0[target_idx]),
                static_cast<uint8_t>((flags_L1_1[target_idx] << 1) | flags_L0_1[target_idx])
            };

            uint64_t response_k[4] = {r0, r1, r2, r3};
            uint64_t response_k_prime = 0; 
            
            for(int k = 0; k < 4; k++) {
                if (sigma[k] == 3) { 
                    response_k_prime = response_k[k]; 
                    break; 
                }
            }

            uint64_t rec_w0=0, rec_w1=0, rec_w2=0;
            for(int k = 0; k < 4; k++) {
                if (sigma[k] == 0) rec_w0 = response_k[k] ^ response_k_prime;
                if (sigma[k] == 1) rec_w1 = response_k[k] ^ response_k_prime;
                if (sigma[k] == 2) rec_w2 = response_k[k] ^ response_k_prime;
            }

            auto t_rec_end = std::chrono::high_resolution_clock::now();
            double dur_recon = std::chrono::duration_cast<std::chrono::nanoseconds>(t_rec_end - t_rec_start).count() / 1000000.0;
            total_recon_time += dur_recon;

            // --- Verification ---
            if (rec_w0 == expected_w0 && rec_w1 == expected_w1 && rec_w2 == expected_w2) {
                std::cout << "\033[32m[OK]\033[0m"; 
                std::cout << "[" << rec_w0 << "]" << "[" << rec_w1 << "]" << "[" << rec_w2 << "]\n"; 
            } else {
                std::cout << "\033[31m[FAIL]\033[0m"; 
            }

            std::cout << std::fixed << std::setprecision(3) 
                      << " | Query: " << dur_query << "ms"
                      << " | Respond: " << dur_respond << "ms"
                      << " | Recon: " << dur_recon << "ms" 
                      << " | Up: " << upload_this_op << " B"
                      << " | Down: " << download_this_op << " B" << std::endl;
        }

        // 3. Shutdown Signal
        uint8_t op_kill = 2; 
        co_await (p0 << op_kill); co_await (p1 << op_kill); 
        co_await (p2 << op_kill); co_await (p3 << op_kill);

        std::cout << "\n================ READ RESULTS ================" << std::endl;
        std::cout << "Total Ops:            " << num_ops << std::endl;
        std::cout << "DB Size:              2^" << (int)(log2(nitems)) << " (" << nitems << ")" << std::endl;
        std::cout << "Avg Query Time:       " << std::fixed << std::setprecision(4) << (total_query_time / num_ops) << " ms" << std::endl;
        std::cout << "Avg Server Respond:   " << std::fixed << std::setprecision(4) << (total_respond_time / num_ops) << " ms" << std::endl;
        std::cout << "Avg Reconstruct Time: " << std::fixed << std::setprecision(4) << (total_recon_time / num_ops) << " ms" << std::endl;
        std::cout << "-----------------------------------------" << std::endl;
        std::cout << "Upload / Op:          " << (total_upload_bytes / num_ops) << " Bytes" << std::endl;
        std::cout << "Download / Op:        " << (total_download_bytes / num_ops) << " Bytes" << std::endl;
        std::cout << "=========================================" << std::endl;

    } catch (std::exception& e) { std::cerr << "[Client Error] " << e.what() << std::endl; }
}

// ============================================================================
// SERVER RUNNER
// ============================================================================
awaitable<void> run_party_node(boost::asio::io_context& io, Role role, size_t nitems) {
    try {
        uint16_t port = BASE_PORT + static_cast<int>(role);
        tcp::acceptor acc(io, tcp::endpoint(tcp::v4(), port));
        tcp::socket sock = co_await acc.async_accept(use_awaitable);
        NetPeer client(Role::CLIENT, std::move(sock));

        PIRDatabase<AES_KEY> db(nitems);
        std::cout << "[P" << static_cast<int>(role) << "] Ready. DB Size: " << nitems << "\n";

        // Signal Client that initialization is done
        uint8_t ready = 1; 
        co_await (client << ready); 

        while(true) {
            uint8_t op = 0;
            try { op = co_await client.recv<uint8_t>(); } catch (...) { break; }
            
            if (op == 1) { // Read Operation
                // Receive response and exact processing time from db
                auto [response, eval_time] = co_await db.read_server(client);
                
                co_await (client << response);
                co_await (client << eval_time); // Strictly benchmarking data
            }
            else if (op == 2) break; // Kill Signal
        }
    } catch (std::exception& e) { std::cerr << "[Node Error] " << e.what() << std::endl; }
}

int main(int argc, char* argv[]) {
    if (argc < 4) {
        std::cerr << "Usage: " << argv[0] << " <party> <power> <num_ops>\n";
        return 1;
    }

    std::string r = argv[1];
    int power = std::stoi(argv[2]);
    int num_ops = std::stoi(argv[3]);
    size_t nitems = 1ULL << power; 

    boost::asio::io_context io;

    if (r == "p0") co_spawn(io, run_party_node(io, Role::P0, nitems), detached);
    else if (r == "p1") co_spawn(io, run_party_node(io, Role::P1, nitems), detached);
    else if (r == "p2") co_spawn(io, run_party_node(io, Role::P2, nitems), detached);
    else if (r == "p3") co_spawn(io, run_party_node(io, Role::P3, nitems), detached);
    else if (r == "client") co_spawn(io, run_client(io, nitems, num_ops), detached);
    else {
        std::cerr << "Invalid party! Use: p0, p1, p2, p3, or client\n";
        return 1;
    }
    
    io.run();
    return 0;
}