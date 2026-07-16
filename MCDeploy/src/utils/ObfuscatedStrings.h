#pragma once

// ============================================================================
// Compile-Time XOR String Obfuscation for MCDeploy
// ============================================================================
// Prevents sensitive credential strings from appearing as plaintext in the
// compiled binary. Uses constexpr XOR encryption with a compile-time key
// derived from __LINE__ and __COUNTER__ macros.
//
// Usage:
//   auto token = OBFUSCATE("my_secret_token");
//   std::string decoded = token.decode();
// ============================================================================

#include <string>
#include <array>
#include <cstdint>

namespace MCDeploy {
namespace Obfuscation {

// Compile-time XOR key generator using multiple seeds for entropy
constexpr uint8_t generateKey(size_t index, uint64_t seed) {
    // LCG-style pseudo-random using seed and index
    uint64_t h = seed ^ (index * 0x9E3779B97F4A7C15ULL);
    h ^= (h >> 30) * 0xBF58476D1CE4E5B9ULL;
    h ^= (h >> 27) * 0x94D049BB133111EBULL;
    h ^= (h >> 31);
    return static_cast<uint8_t>(h & 0xFF);
}

template<size_t N, uint64_t Seed>
class ObfuscatedString {
public:
    // Compile-time encryption constructor
    constexpr ObfuscatedString(const char (&str)[N]) : m_data{} {
        for (size_t i = 0; i < N; ++i) {
            m_data[i] = str[i] ^ generateKey(i, Seed);
        }
    }

    // Runtime decryption
    std::string decode() const {
        std::string result;
        result.reserve(N - 1);
        for (size_t i = 0; i < N - 1; ++i) {
            result.push_back(m_data[i] ^ generateKey(i, Seed));
        }
        return result;
    }

private:
    std::array<char, N> m_data;
};

// Helper to deduce array size automatically
template<size_t N, uint64_t Seed>
constexpr auto makeObfuscated(const char (&str)[N]) {
    return ObfuscatedString<N, Seed>(str);
}

} // namespace Obfuscation
} // namespace MCDeploy

// Macro that creates a unique seed per usage site using __LINE__ and __COUNTER__
// This ensures each obfuscated string uses a different XOR key schedule
#define OBFUSCATE(str) \
    ([]() { \
        constexpr auto s = ::MCDeploy::Obfuscation::makeObfuscated< \
            sizeof(str), \
            static_cast<uint64_t>(__LINE__) * 0x100000001ULL + __COUNTER__ * 0xDEADBEEF \
        >(str); \
        return s; \
    }())

// ============================================================================
// Hardcoded Cloudflare Credentials (obfuscated at compile time)
// ============================================================================
// These will NOT appear as plaintext strings in the compiled binary.
// At runtime they are decoded into memory only when needed.
// ============================================================================

namespace MCDeploy {
namespace DnsCredentials {

    inline std::string getApiToken() {
        return OBFUSCATE("").decode();
    }

    inline std::string getZoneId() {
        return OBFUSCATE("").decode();
    }

    inline std::string getDomain() {
        return OBFUSCATE("").decode();
    }

} // namespace DnsCredentials
} // namespace MCDeploy
