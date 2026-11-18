// The platform-free parts of galaxy/net/web.h: URLs and the WebSocket's frames.
#include "galaxy/net/web.h"

#include "galaxy/net/digest.h"

#include <array>
#include <cctype>
#include <cstring>

namespace galaxy::net {

namespace {

// The longest message NebulaNet sends is a few hundred bytes; anything far longer is not NebulaNet.
constexpr std::uint64_t kMaxWsMessage = 1u << 20;

std::string lower(std::string_view text) {
    std::string out(text);
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

}  // namespace

std::optional<Url> parse_url(std::string_view url) {
    Url out;
    if (url.starts_with("https://")) {
        url.remove_prefix(8);
    } else if (url.starts_with("http://")) {
        out.secure = false;
        out.port = 80u;
        url.remove_prefix(7);
    } else {
        return std::nullopt;
    }
    const std::size_t slash = url.find('/');
    std::string_view authority = url.substr(0, slash);
    if (slash != std::string_view::npos) out.path = std::string(url.substr(slash));
    std::string_view port;
    if (authority.starts_with("[")) {
        const std::size_t close = authority.find(']');
        if (close == std::string_view::npos) return std::nullopt;
        out.host = std::string(authority.substr(1, close - 1));
        if (close + 1u < authority.size()) {
            if (authority[close + 1u] != ':') return std::nullopt;
            port = authority.substr(close + 2u);
        }
    } else {
        const std::size_t colon = authority.find(':');
        out.host = std::string(authority.substr(0, colon));
        if (colon != std::string_view::npos) port = authority.substr(colon + 1u);
    }
    if (out.host.empty()) return std::nullopt;
    if (!port.empty()) {
        unsigned value = 0u;
        for (const char c : port) {
            if (c < '0' || c > '9') return std::nullopt;
            value = value * 10u + static_cast<unsigned>(c - '0');
            if (value > 65535u) return std::nullopt;
        }
        if (value == 0u) return std::nullopt;
        out.port = static_cast<std::uint16_t>(value);
    }
    return out;
}

std::string base64(std::span<const std::uint8_t> bytes) {
    static const char* const kLetters = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    for (std::size_t i = 0; i < bytes.size(); i += 3u) {
        const std::uint32_t n = (std::uint32_t{bytes[i]} << 16) |
                                (i + 1u < bytes.size() ? std::uint32_t{bytes[i + 1u]} << 8 : 0u) |
                                (i + 2u < bytes.size() ? std::uint32_t{bytes[i + 2u]} : 0u);
        out += kLetters[(n >> 18) & 63u];
        out += kLetters[(n >> 12) & 63u];
        out += i + 1u < bytes.size() ? kLetters[(n >> 6) & 63u] : '=';
        out += i + 2u < bytes.size() ? kLetters[n & 63u] : '=';
    }
    return out;
}

std::string ws_handshake(const Url& url, std::string_view key) {
    const bool default_port = url.port == (url.secure ? 443u : 80u);
    const bool six = url.host.find(':') != std::string::npos;
    std::string host = six ? "[" + url.host + "]" : url.host;
    if (!default_port) host += ":" + std::to_string(url.port);
    return "GET " + url.path + " HTTP/1.1\r\nHost: " + host +
           "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: " + std::string(key) +
           "\r\nSec-WebSocket-Version: 13\r\nUser-Agent: Nebula\r\n\r\n";
}

bool ws_accepts(std::string_view response, std::string_view key) {
    if (!response.starts_with("HTTP/1.1 101")) return false;
    const std::string accept_input = std::string(key) + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    const auto hashed = digest::sha1(std::span<const std::uint8_t>(
        reinterpret_cast<const std::uint8_t*>(accept_input.data()), accept_input.size()));
    const std::string expected = base64(hashed);
    // Header names are case-insensitive; the value is compared as sent.
    const std::string head = lower(response);
    const std::size_t at = head.find("\r\nsec-websocket-accept:");
    if (at == std::string::npos) return false;
    std::size_t start = at + 23u;
    while (start < response.size() && response[start] == ' ') ++start;
    const std::size_t end = response.find("\r\n", start);
    std::string_view value =
        response.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
    while (!value.empty() && value.back() == ' ') value.remove_suffix(1);
    return value == expected;
}

std::vector<std::uint8_t> ws_frame(WsOpcode opcode, std::span<const std::uint8_t> payload,
                                   const std::uint8_t (&mask)[4]) {
    std::vector<std::uint8_t> out;
    out.push_back(static_cast<std::uint8_t>(0x80u | static_cast<std::uint8_t>(opcode)));
    const std::uint64_t size = payload.size();
    if (size < 126u) {
        out.push_back(static_cast<std::uint8_t>(0x80u | size));
    } else if (size <= 0xFFFFu) {
        out.push_back(0x80u | 126u);
        out.push_back(static_cast<std::uint8_t>(size >> 8));
        out.push_back(static_cast<std::uint8_t>(size));
    } else {
        out.push_back(0x80u | 127u);
        for (int i = 7; i >= 0; --i) out.push_back(static_cast<std::uint8_t>(size >> (i * 8)));
    }
    out.insert(out.end(), mask, mask + 4);
    for (std::size_t i = 0; i < payload.size(); ++i) out.push_back(payload[i] ^ mask[i % 4u]);
    return out;
}

void WsReader::add(std::span<const std::uint8_t> bytes) { buffer_.insert(buffer_.end(), bytes.begin(), bytes.end()); }

std::optional<WsReader::Message> WsReader::next() {
    while (!failed_) {
        if (buffer_.size() < 2u) return std::nullopt;
        const bool fin = (buffer_[0] & 0x80u) != 0u;
        const auto opcode = static_cast<WsOpcode>(buffer_[0] & 0x0Fu);
        const bool masked = (buffer_[1] & 0x80u) != 0u;
        std::uint64_t size = buffer_[1] & 0x7Fu;
        std::size_t head = 2u;
        if (size == 126u) {
            if (buffer_.size() < 4u) return std::nullopt;
            size = (std::uint64_t{buffer_[2]} << 8) | buffer_[3];
            head = 4u;
        } else if (size == 127u) {
            if (buffer_.size() < 10u) return std::nullopt;
            size = 0u;
            for (int i = 0; i < 8; ++i) size = (size << 8) | buffer_[2u + static_cast<std::size_t>(i)];
            head = 10u;
        }
        const bool control = (static_cast<std::uint8_t>(opcode) & 0x08u) != 0u;
        if (masked || size > kMaxWsMessage || (control && (!fin || size > 125u))) {
            failed_ = true;
            return std::nullopt;
        }
        if (buffer_.size() < head + size) return std::nullopt;
        std::string payload(buffer_.begin() + static_cast<std::ptrdiff_t>(head),
                            buffer_.begin() + static_cast<std::ptrdiff_t>(head + size));
        buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(head + size));
        if (control) return Message{opcode, std::move(payload)};
        if (opcode != WsOpcode::Continuation) {
            partial_opcode_ = opcode;
            partial_.clear();
        }
        partial_ += payload;
        if (partial_.size() > kMaxWsMessage) {
            failed_ = true;
            return std::nullopt;
        }
        if (fin) {
            Message out{partial_opcode_, std::move(partial_)};
            partial_.clear();
            return out;
        }
    }
    return std::nullopt;
}

}  // namespace galaxy::net
