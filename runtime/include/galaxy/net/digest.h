// The digests online play needs, small enough to carry: SHA-1 (a WebSocket's handshake), MD5 and HMAC-SHA1 (TURN's
// long-term credentials, RFC 8489 section 9.2: the key is MD5("username:realm:password"), MESSAGE-INTEGRITY its
// HMAC-SHA1 over the message). None of them guards a secret of this game's: they are what those protocols ask for.
#pragma once

#include <array>
#include <cstdint>
#include <span>

namespace galaxy::net::digest {

[[nodiscard]] std::array<std::uint8_t, 20> sha1(std::span<const std::uint8_t> data);
[[nodiscard]] std::array<std::uint8_t, 16> md5(std::span<const std::uint8_t> data);
[[nodiscard]] std::array<std::uint8_t, 20> hmac_sha1(std::span<const std::uint8_t> key,
                                                     std::span<const std::uint8_t> data);

}  // namespace galaxy::net::digest
