#include "galaxy/net/digest.h"

#include <vector>

namespace galaxy::net::digest {

namespace {

std::uint32_t rotl(std::uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }

// The message padded to 64-byte blocks with its length in bits last (big-endian for SHA-1, little-endian for MD5).
std::vector<std::uint8_t> padded(std::span<const std::uint8_t> data, bool big_endian) {
    std::vector<std::uint8_t> out(data.begin(), data.end());
    const std::uint64_t bits = static_cast<std::uint64_t>(data.size()) * 8u;
    out.push_back(0x80u);
    while (out.size() % 64u != 56u) out.push_back(0u);
    for (int i = 0; i < 8; ++i) {
        out.push_back(static_cast<std::uint8_t>(bits >> ((big_endian ? 7 - i : i) * 8)));
    }
    return out;
}

}  // namespace

std::array<std::uint8_t, 20> sha1(std::span<const std::uint8_t> input) {
    std::uint32_t h[5] = {0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u};
    const std::vector<std::uint8_t> data = padded(input, true);
    for (std::size_t chunk = 0; chunk < data.size(); chunk += 64u) {
        std::uint32_t w[80];
        for (int i = 0; i < 16; ++i) {
            const std::uint8_t* p = &data[chunk + static_cast<std::size_t>(i) * 4u];
            w[i] = (std::uint32_t{p[0]} << 24) | (std::uint32_t{p[1]} << 16) | (std::uint32_t{p[2]} << 8) | p[3];
        }
        for (int i = 16; i < 80; ++i) w[i] = rotl(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        std::uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; ++i) {
            std::uint32_t f = 0u, k = 0u;
            if (i < 20) { f = (b & c) | (~b & d); k = 0x5A827999u; }
            else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1u; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDCu; }
            else { f = b ^ c ^ d; k = 0xCA62C1D6u; }
            const std::uint32_t t = rotl(a, 5) + f + e + k + w[i];
            e = d;
            d = c;
            c = rotl(b, 30);
            b = a;
            a = t;
        }
        h[0] += a;
        h[1] += b;
        h[2] += c;
        h[3] += d;
        h[4] += e;
    }
    std::array<std::uint8_t, 20> out{};
    for (int i = 0; i < 5; ++i) {
        for (int j = 0; j < 4; ++j) {
            out[static_cast<std::size_t>(i * 4 + j)] = static_cast<std::uint8_t>(h[i] >> (24 - j * 8));
        }
    }
    return out;
}

std::array<std::uint8_t, 16> md5(std::span<const std::uint8_t> input) {
    static constexpr std::uint32_t k[64] = {
        0xd76aa478u, 0xe8c7b756u, 0x242070dbu, 0xc1bdceeeu, 0xf57c0fafu, 0x4787c62au, 0xa8304613u, 0xfd469501u,
        0x698098d8u, 0x8b44f7afu, 0xffff5bb1u, 0x895cd7beu, 0x6b901122u, 0xfd987193u, 0xa679438eu, 0x49b40821u,
        0xf61e2562u, 0xc040b340u, 0x265e5a51u, 0xe9b6c7aau, 0xd62f105du, 0x02441453u, 0xd8a1e681u, 0xe7d3fbc8u,
        0x21e1cde6u, 0xc33707d6u, 0xf4d50d87u, 0x455a14edu, 0xa9e3e905u, 0xfcefa3f8u, 0x676f02d9u, 0x8d2a4c8au,
        0xfffa3942u, 0x8771f681u, 0x6d9d6122u, 0xfde5380cu, 0xa4beea44u, 0x4bdecfa9u, 0xf6bb4b60u, 0xbebfbc70u,
        0x289b7ec6u, 0xeaa127fau, 0xd4ef3085u, 0x04881d05u, 0xd9d4d039u, 0xe6db99e5u, 0x1fa27cf8u, 0xc4ac5665u,
        0xf4292244u, 0x432aff97u, 0xab9423a7u, 0xfc93a039u, 0x655b59c3u, 0x8f0ccc92u, 0xffeff47du, 0x85845dd1u,
        0x6fa87e4fu, 0xfe2ce6e0u, 0xa3014314u, 0x4e0811a1u, 0xf7537e82u, 0xbd3af235u, 0x2ad7d2bbu, 0xeb86d391u};
    static constexpr int s[64] = {7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
                                  5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20,
                                  4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
                                  6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};
    std::uint32_t h[4] = {0x67452301u, 0xefcdab89u, 0x98badcfeu, 0x10325476u};
    const std::vector<std::uint8_t> data = padded(input, false);
    for (std::size_t chunk = 0; chunk < data.size(); chunk += 64u) {
        std::uint32_t m[16];
        for (int i = 0; i < 16; ++i) {
            const std::uint8_t* p = &data[chunk + static_cast<std::size_t>(i) * 4u];
            m[i] = std::uint32_t{p[0]} | (std::uint32_t{p[1]} << 8) | (std::uint32_t{p[2]} << 16) |
                   (std::uint32_t{p[3]} << 24);
        }
        std::uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
        for (int i = 0; i < 64; ++i) {
            std::uint32_t f = 0u;
            int g = 0;
            if (i < 16) { f = (b & c) | (~b & d); g = i; }
            else if (i < 32) { f = (d & b) | (~d & c); g = (5 * i + 1) % 16; }
            else if (i < 48) { f = b ^ c ^ d; g = (3 * i + 5) % 16; }
            else { f = c ^ (b | ~d); g = (7 * i) % 16; }
            const std::uint32_t next = d;
            d = c;
            c = b;
            b = b + rotl(a + f + k[i] + m[g], s[i]);
            a = next;
        }
        h[0] += a;
        h[1] += b;
        h[2] += c;
        h[3] += d;
    }
    std::array<std::uint8_t, 16> out{};
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            out[static_cast<std::size_t>(i * 4 + j)] = static_cast<std::uint8_t>(h[i] >> (j * 8));
        }
    }
    return out;
}

std::array<std::uint8_t, 20> hmac_sha1(std::span<const std::uint8_t> key, std::span<const std::uint8_t> data) {
    std::array<std::uint8_t, 64> block{};
    if (key.size() > block.size()) {
        const auto hashed = sha1(key);
        std::copy(hashed.begin(), hashed.end(), block.begin());
    } else {
        std::copy(key.begin(), key.end(), block.begin());
    }
    std::vector<std::uint8_t> inner(block.size() + data.size());
    std::vector<std::uint8_t> outer(block.size() + 20u);
    for (std::size_t i = 0; i < block.size(); ++i) {
        inner[i] = static_cast<std::uint8_t>(block[i] ^ 0x36u);
        outer[i] = static_cast<std::uint8_t>(block[i] ^ 0x5cu);
    }
    std::copy(data.begin(), data.end(), inner.begin() + static_cast<std::ptrdiff_t>(block.size()));
    const auto first = sha1(inner);
    std::copy(first.begin(), first.end(), outer.begin() + static_cast<std::ptrdiff_t>(block.size()));
    return sha1(outer);
}

}  // namespace galaxy::net::digest
