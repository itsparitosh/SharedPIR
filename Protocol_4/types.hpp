
#pragma once
#include <vector>
#include <iostream>
#include <stdexcept>
#include <immintrin.h> // Required for AVX2 intrinsics

// Wrapper for uint64_t to define algebraic behavior
struct XorStr {
    uint64_t v;
    XorStr() : v(0) {}
    XorStr(uint64_t val) : v(val) {}
    
    // Scalar operations (Fallback for single items)
    inline XorStr operator+(const XorStr& o) const { return XorStr(v ^ o.v); }
    inline XorStr operator-(const XorStr& o) const { return XorStr(v ^ o.v); } 
    inline XorStr operator*(const XorStr& o) const { return XorStr(v & o.v); } 
    inline XorStr& operator+=(const XorStr& o) { v ^= o.v; return *this; }
    bool operator==(const XorStr& o) const { return v == o.v; }
    bool operator!=(const XorStr& o) const { return v != o.v; }
};

inline std::ostream& operator<<(std::ostream& os, const XorStr& x) { return os << x.v; }


// 1. Vector Addition (XOR)
// Computes res[i] = a[i] ^ b[i] for the whole vector at 256 bits/cycle.
template<typename T>
inline std::vector<T> operator+(const std::vector<T>& a, const std::vector<T>& b) {
    if (a.size() != b.size()) throw std::runtime_error("Size mismatch +");
    
    size_t n = a.size();
    std::vector<T> res(n);
    
    // Pointers to raw data
    const uint64_t* pA = reinterpret_cast<const uint64_t*>(a.data());
    const uint64_t* pB = reinterpret_cast<const uint64_t*>(b.data());
    uint64_t* pRes     = reinterpret_cast<uint64_t*>(res.data());

    size_t i = 0;
    
    // Process 4 items (4 * 64-bit = 256-bit) per cycle
    for (; i + 4 <= n; i += 4) {
        // Load 256 bits
        __m256i va = _mm256_loadu_si256((__m256i*)&pA[i]);
        __m256i vb = _mm256_loadu_si256((__m256i*)&pB[i]);
        
        // XOR operation
        __m256i vres = _mm256_xor_si256(va, vb);
        
        // Store result
        _mm256_storeu_si256((__m256i*)&pRes[i], vres);
    }

    // Cleanup Loop: Handle remaining items (if size is not multiple of 4)
    for (; i < n; ++i) {
        res[i] = a[i] + b[i];
    }
    
    return res;
}

// 2. Vector Subtraction (Same as XOR for GF(2^k))
template<typename T>
inline std::vector<T> operator-(const std::vector<T>& a, const std::vector<T>& b) {
    return a + b; // Re-use the optimized + operator
}

// 3. Dot Product (AND then XOR Sum)
// Computes Sum(a[i] & b[i]) efficiently
template<typename T>
inline T operator*(const std::vector<T>& a, const std::vector<T>& b) {
    if (a.size() != b.size()) throw std::runtime_error("Size mismatch *");
    
    size_t n = a.size();
    const uint64_t* pA = reinterpret_cast<const uint64_t*>(a.data());
    const uint64_t* pB = reinterpret_cast<const uint64_t*>(b.data());

    // Accumulator register (init to 0)
    __m256i vsum = _mm256_setzero_si256();

    size_t i = 0;
    for (; i + 4 <= n; i += 4) {
        __m256i va = _mm256_loadu_si256((__m256i*)&pA[i]);
        __m256i vb = _mm256_loadu_si256((__m256i*)&pB[i]);
        
        // AND operation
        __m256i vand = _mm256_and_si256(va, vb);
        
        // Accumulate via XOR
        vsum = _mm256_xor_si256(vsum, vand);
    }

    // Horizontal XOR: Combine the 4 parts of the accumulator into one scalar
    // (This is a bit tricky in intrinsics, but necessary)
    uint64_t buffer[4];
    _mm256_storeu_si256((__m256i*)buffer, vsum);
    uint64_t scalar_res = buffer[0] ^ buffer[1] ^ buffer[2] ^ buffer[3];

    // Cleanup Loop for remaining items
    for (; i < n; ++i) {
        scalar_res ^= (pA[i] & pB[i]);
    }

    return T(scalar_res);
}


// ============================================================================
// VECTOR IN-PLACE ADDITION (+=)
// ============================================================================
template<typename T>
inline std::vector<T>& operator+=(std::vector<T>& a, const std::vector<T>& b) {
    if (a.size() != b.size()) throw std::runtime_error("Size mismatch +=");
    
    // Check if we can use the optimized XorStr update
    if constexpr (std::is_same_v<T, XorStr>) {
        // Raw pointer access for speed
        uint64_t* pA = reinterpret_cast<uint64_t*>(a.data());
        const uint64_t* pB = reinterpret_cast<const uint64_t*>(b.data());
        size_t n = a.size();
        
        // Simple loop (Compiler will auto-vectorize this with -O3)
        for(size_t i = 0; i < n; ++i) {
            pA[i] ^= pB[i];
        }
    } else {
        // Fallback for other types
        for(size_t i = 0; i < a.size(); ++i) a[i] += b[i];
    }
    return a;
}







// #pragma once
// #include <immintrin.h>
// #include <cstdint>
// #include <vector>
// #include <type_traits>
// #include <stdexcept>
// #include <array>
// #include <cstring>
// #include <sstream>
// #include <iomanip>
// #include <iostream>

// // --- XorStr Bridge ---
// struct XorStr {
//     uint64_t v;
//     XorStr() : v(0) {}
//     XorStr(uint64_t val) : v(val) {}
//     inline XorStr operator+(const XorStr& o) const { return XorStr(v ^ o.v); }
//     inline XorStr operator-(const XorStr& o) const { return XorStr(v ^ o.v); } 
//     inline XorStr operator*(const XorStr& o) const { return XorStr(v & o.v); } 
//     inline XorStr& operator+=(const XorStr& o) { v ^= o.v; return *this; }
//     bool operator==(const XorStr& o) const { return v == o.v; }
//     bool operator!=(const XorStr& o) const { return v != o.v; }
// };
// inline std::ostream& operator<<(std::ostream& os, const XorStr& x) { return os << x.v; }

// // --- Vector Ops ---
// template<typename T>
// inline std::vector<T> operator+(const std::vector<T>& a, const std::vector<T>& b) {
//     if (a.size() != b.size()) throw std::runtime_error("Size mismatch +");
//     std::vector<T> res(a.size());
//     for(size_t i=0; i<a.size(); ++i) res[i] = a[i] + b[i];
//     return res;
// }
// template<typename T>
// inline std::vector<T> operator-(const std::vector<T>& a, const std::vector<T>& b) {
//     if (a.size() != b.size()) throw std::runtime_error("Size mismatch -");
//     std::vector<T> res(a.size());
//     for(size_t i=0; i<a.size(); ++i) res[i] = a[i] - b[i];
//     return res;
// }
// template<typename T>
// inline T operator*(const std::vector<T>& a, const std::vector<T>& b) {
//     if (a.size() != b.size()) throw std::runtime_error("Size mismatch *");
//     T res = 0; 
//     for(size_t i=0; i<a.size(); ++i) res = res + (a[i] * b[i]); 
//     return res;
// }
// template<typename T>
// inline std::vector<T> operator*(const std::vector<T>& a, const T& b) {
//     std::vector<T> res(a.size());
//     for(size_t i=0; i<a.size(); ++i) res[i] = a[i] * b;
//     return res;
// }

