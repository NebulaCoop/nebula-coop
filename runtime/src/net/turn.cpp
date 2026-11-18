#include "galaxy/net/turn.h"

#include "galaxy/net/digest.h"

#include <algorithm>
#include <array>
#include <atomic>

namespace galaxy::net::turn {

namespace {

constexpr std::uint32_t kCookie = 0x2112A442u;
constexpr std::size_t kHeader = 20u;

// A message's class, in the type's bits 4 and 8 (RFC 8489 section 5).
enum class Class : std::uint16_t { Request = 0x000, Indication = 0x010, Success = 0x100, Error = 0x110 };

enum Attribute : std::uint16_t {
    kUsername = 0x0006,
    kMessageIntegrity = 0x0008,
    kErrorCode = 0x0009,
    kChannelNumber = 0x000C,
    kLifetime = 0x000D,
    kXorPeerAddress = 0x0012,
    kData = 0x0013,
    kRealm = 0x0014,
    kNonce = 0x0015,
    kXorRelayedAddress = 0x0016,
    kRequestedTransport = 0x0019,
};

std::uint16_t u16(std::span<const std::uint8_t> bytes, std::size_t at) {
    return static_cast<std::uint16_t>((bytes[at] << 8) | bytes[at + 1]);
}

std::uint32_t u32(std::span<const std::uint8_t> bytes, std::size_t at) {
    return (std::uint32_t{bytes[at]} << 24) | (std::uint32_t{bytes[at + 1]} << 16) |
           (std::uint32_t{bytes[at + 2]} << 8) | bytes[at + 3];
}

void put16(std::vector<std::uint8_t>& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>(value >> 8));
    out.push_back(static_cast<std::uint8_t>(value));
}

void put32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    put16(out, static_cast<std::uint16_t>(value >> 16));
    put16(out, static_cast<std::uint16_t>(value));
}

void attribute(std::vector<std::uint8_t>& out, std::uint16_t type, std::span<const std::uint8_t> value) {
    put16(out, type);
    put16(out, static_cast<std::uint16_t>(value.size()));
    out.insert(out.end(), value.begin(), value.end());
    while (out.size() % 4u != 0u) out.push_back(0u);
}

void attribute(std::vector<std::uint8_t>& out, std::uint16_t type, std::string_view value) {
    attribute(out, type,
              std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(value.data()), value.size()));
}

// XOR-PEER-ADDRESS and XOR-RELAYED-ADDRESS: as XOR-MAPPED-ADDRESS, masked with the cookie (and the transaction id
// for IPv6).
std::vector<std::uint8_t> xor_address(const Address& address, const stun::TransactionId& id) {
    std::array<std::uint8_t, 16> ip{};
    const std::size_t size = address.ip_bytes(ip);
    std::array<std::uint8_t, 16> mask{};
    mask[0] = static_cast<std::uint8_t>(kCookie >> 24);
    mask[1] = static_cast<std::uint8_t>(kCookie >> 16);
    mask[2] = static_cast<std::uint8_t>(kCookie >> 8);
    mask[3] = static_cast<std::uint8_t>(kCookie);
    std::copy(id.begin(), id.end(), mask.begin() + 4);
    std::vector<std::uint8_t> out{0u, static_cast<std::uint8_t>(size == 4u ? 0x01u : 0x02u)};
    put16(out, static_cast<std::uint16_t>(address.port() ^ (kCookie >> 16)));
    for (std::size_t i = 0; i < size; ++i) out.push_back(static_cast<std::uint8_t>(ip[i] ^ mask[i]));
    return out;
}

bool read_xor_address(std::span<const std::uint8_t> value, const stun::TransactionId& id, Address& out) {
    if (value.size() < 8u) return false;
    const std::size_t size = value[1] == 0x01u ? 4u : value[1] == 0x02u ? 16u : 0u;
    if (size == 0u || value.size() < 4u + size) return false;
    std::array<std::uint8_t, 16> mask{};
    mask[0] = static_cast<std::uint8_t>(kCookie >> 24);
    mask[1] = static_cast<std::uint8_t>(kCookie >> 16);
    mask[2] = static_cast<std::uint8_t>(kCookie >> 8);
    mask[3] = static_cast<std::uint8_t>(kCookie);
    std::copy(id.begin(), id.end(), mask.begin() + 4);
    std::array<std::uint8_t, 16> ip{};
    for (std::size_t i = 0; i < size; ++i) ip[i] = static_cast<std::uint8_t>(value[4 + i] ^ mask[i]);
    out = make_address(std::span<const std::uint8_t>(ip.data(), size),
                       static_cast<std::uint16_t>(u16(value, 2) ^ (kCookie >> 16)));
    return out.valid();
}

// Calls `each(type, value)` for every attribute of a STUN message; false when one runs past its end.
template <typename Each>
bool attributes(std::span<const std::uint8_t> message, Each&& each) {
    const std::size_t end = kHeader + u16(message, 2);
    for (std::size_t at = kHeader; at + 4u <= end;) {
        const std::uint16_t type = u16(message, at);
        const std::size_t length = u16(message, at + 2);
        if (at + 4u + length > end) return false;
        each(type, message.subspan(at + 4u, length));
        at += 4u + ((length + 3u) & ~std::size_t{3});
    }
    return true;
}

std::string text_of(std::span<const std::uint8_t> value) { return std::string(value.begin(), value.end()); }

}  // namespace

bool is_channel_data(std::span<const std::uint8_t> datagram) noexcept {
    if (datagram.size() < 4u || (datagram[0] & 0xC0u) != 0x40u) return false;
    return 4u + u16(datagram, 2) <= datagram.size();
}

Client::Client(Send send) : send_(std::move(send)), random_(std::random_device{}()) {}

namespace {
std::atomic<bool> s_allow_private{false};
}  // namespace

void Client::allow_private_peers(bool allow) noexcept { s_allow_private.store(allow); }

bool Client::permittable(const Address& address) {
    std::array<std::uint8_t, 16> ip{};
    const std::size_t size = address.ip_bytes(ip);
    if (size != 0u && s_allow_private.load()) return true;
    if (size == 4u) {
        // 0/8, 10/8, 100.64/10 (CGNAT), 127/8, 169.254/16, 172.16/12, 192.168/16, multicast and up.
        if (ip[0] == 0u || ip[0] == 10u || ip[0] == 127u || ip[0] >= 224u) return false;
        if (ip[0] == 100u && (ip[1] & 0xC0u) == 64u) return false;
        if (ip[0] == 169u && ip[1] == 254u) return false;
        if (ip[0] == 172u && (ip[1] & 0xF0u) == 16u) return false;
        if (ip[0] == 192u && ip[1] == 168u) return false;
        return true;
    }
    if (size == 16u) {
        // Loopback, link-local (fe80::/10), unique local (fc00::/7), multicast (ff00::/8).
        const bool loopback = std::all_of(ip.begin(), ip.begin() + 15, [](std::uint8_t b) { return b == 0u; }) &&
                              ip[15] == 1u;
        if (loopback || ip[0] == 0xFFu || (ip[0] & 0xFEu) == 0xFCu || (ip[0] == 0xFEu && (ip[1] & 0xC0u) == 0x80u)) {
            return false;
        }
        return true;
    }
    return false;
}

void Client::start(Server server, Clock::time_point now) {
    stop();
    server_ = std::move(server);
    realm_.clear();
    nonce_.clear();
    key_.clear();
    relayed_ = Address{};
    failure_.clear();
    lifetime_ = 600u;
    next_channel_ = kFirstChannel;
    if (!server_.address.valid()) {
        fail("the relay's address did not resolve");
        return;
    }
    state_ = State::Allocating;
    // The first Allocate goes without credentials: the server's 401 names its realm and nonce.
    Request allocate;
    allocate.method = Method::Allocate;
    ask(allocate, now);
}

void Client::stop() {
    if (state_ == State::Ready && !key_.empty()) {
        Request release;
        release.method = Method::Refresh;
        release.lifetime = 0u;
        release.authed = true;
        for (auto& byte : release.id) byte = static_cast<std::uint8_t>(random_());
        const std::vector<std::uint8_t> datagram = build(release);
        send_(server_.address, datagram);
    }
    state_ = State::Idle;
    pending_.clear();
    peers_.clear();
    relayed_ = Address{};
}

void Client::fail(std::string why) {
    failure_ = std::move(why);
    state_ = State::Failed;
    pending_.clear();
}

std::vector<std::uint8_t> Client::build(const Request& request) const {
    std::vector<std::uint8_t> body;
    switch (request.method) {
    case Method::Allocate: {
        const std::uint8_t udp[4] = {17u, 0u, 0u, 0u};
        attribute(body, kRequestedTransport, udp);
        break;
    }
    case Method::Refresh: {
        std::vector<std::uint8_t> value;
        put32(value, request.lifetime);
        attribute(body, kLifetime, value);
        break;
    }
    case Method::CreatePermission: attribute(body, kXorPeerAddress, xor_address(request.peer, request.id)); break;
    case Method::ChannelBind: {
        std::vector<std::uint8_t> value;
        put16(value, request.channel);
        put16(value, 0u);
        attribute(body, kChannelNumber, value);
        attribute(body, kXorPeerAddress, xor_address(request.peer, request.id));
        break;
    }
    default: break;
    }
    if (request.authed) {
        attribute(body, kUsername, server_.username);
        attribute(body, kRealm, realm_);
        attribute(body, kNonce, nonce_);
    }
    std::vector<std::uint8_t> out;
    out.reserve(kHeader + body.size() + 24u);
    put16(out, static_cast<std::uint16_t>(static_cast<std::uint16_t>(request.method) |
                                          static_cast<std::uint16_t>(Class::Request)));
    // The length counts MESSAGE-INTEGRITY already: the HMAC covers the header as it will be sent.
    put16(out, static_cast<std::uint16_t>(body.size() + (request.authed ? 24u : 0u)));
    put32(out, kCookie);
    out.insert(out.end(), request.id.begin(), request.id.end());
    out.insert(out.end(), body.begin(), body.end());
    if (request.authed) {
        const auto integrity = digest::hmac_sha1(key_, out);
        attribute(out, kMessageIntegrity, integrity);
    }
    return out;
}

void Client::ask(Request request, Clock::time_point now) {
    for (auto& byte : request.id) byte = static_cast<std::uint8_t>(random_());
    request.authed = request.authed || !key_.empty();
    request.tries = 0;
    pending_.push_back(request);
    resend(pending_.back(), now);
}

void Client::resend(Request& request, Clock::time_point now) {
    ++request.tries;
    request.last = now;
    const std::vector<std::uint8_t> datagram = build(request);
    send_(server_.address, datagram);
}

void Client::update(Clock::time_point now) {
    if (!active()) return;
    // Unanswered requests again; one that never is fails the relay when it's the allocation's.
    for (std::size_t i = 0; i < pending_.size();) {
        Request& request = pending_[i];
        if (now - request.last < kResendEvery) {
            ++i;
            continue;
        }
        if (request.tries >= kTries) {
            const Method method = request.method;
            pending_.erase(pending_.begin() + static_cast<std::ptrdiff_t>(i));
            if (method == Method::Allocate) {
                fail("the relay did not answer");
                return;
            }
            continue;
        }
        resend(request, now);
        ++i;
    }
    if (state_ != State::Ready) return;
    // The allocation at half its lifetime, and each peer's permission or channel before it lapses.
    const auto half = std::chrono::seconds(std::max<std::uint32_t>(lifetime_ / 2u, 30u));
    if (now - refreshed_ >= half) {
        refreshed_ = now;
        Request refresh;
        refresh.method = Method::Refresh;
        refresh.lifetime = lifetime_;
        ask(refresh, now);
    }
    for (Peer& peer : peers_) {
        if (now - peer.refreshed < kPeerRefreshEvery) continue;
        peer.refreshed = now;
        ask_peer(peer, now);
    }
}

void Client::ask_peer(const Peer& peer, Clock::time_point now) {
    Request request;
    request.peer = peer.address;
    if (peer.channel != 0u) {
        request.method = Method::ChannelBind;
        request.channel = peer.channel;
    } else {
        request.method = Method::CreatePermission;
    }
    ask(request, now);
}

Client::Peer* Client::find_peer(const Address& address) {
    for (Peer& peer : peers_) {
        if (peer.address == address) return &peer;
    }
    return nullptr;
}

const Client::Peer* Client::channel_peer(std::uint16_t channel) const {
    // Ready or not: the server's answer to the ChannelBind may come after the first ChannelData it relays.
    for (const Peer& peer : peers_) {
        if (peer.channel == channel) return &peer;
    }
    return nullptr;
}

void Client::permit(const Address& peer) {
    if (!permittable(peer) || !active()) return;
    // One permission an IP address: any port of it may send then.
    std::array<std::uint8_t, 16> ip{};
    std::array<std::uint8_t, 16> other{};
    const std::size_t size = peer.ip_bytes(ip);
    for (const Peer& known : peers_) {
        if (known.address.ip_bytes(other) == size && std::equal(ip.begin(), ip.begin() + size, other.begin())) return;
    }
    peers_.push_back({make_address(std::span<const std::uint8_t>(ip.data(), size), 0u), 0u, false, Clock::now()});
    if (state_ == State::Ready) ask_peer(peers_.back(), Clock::now());
}

void Client::bind(const Address& peer) {
    if (!permittable(peer) || !active()) return;
    if (Peer* known = find_peer(peer); known != nullptr && known->channel != 0u) return;
    if (next_channel_ > 0x4FFFu) return;
    peers_.push_back({peer, next_channel_++, false, Clock::now()});
    if (state_ == State::Ready) ask_peer(peers_.back(), Clock::now());
}

bool Client::routes(const Address& peer) const {
    if (!active()) return false;
    return std::any_of(peers_.begin(), peers_.end(),
                       [&](const Peer& known) { return known.channel != 0u && known.address == peer; });
}

bool Client::send(const Address& peer, std::span<const std::uint8_t> data) {
    if (state_ != State::Ready || data.size() > 0xFFFFu - 64u) return false;
    std::vector<std::uint8_t> out;
    const Peer* known = find_peer(peer);
    if (known != nullptr && known->channel != 0u && known->channel_ready) {
        out.reserve(4u + data.size());
        put16(out, known->channel);
        put16(out, static_cast<std::uint16_t>(data.size()));
        out.insert(out.end(), data.begin(), data.end());
    } else {
        stun::TransactionId id{};
        for (auto& byte : id) byte = static_cast<std::uint8_t>(random_());
        std::vector<std::uint8_t> body;
        attribute(body, kXorPeerAddress, xor_address(peer, id));
        attribute(body, kData, data);
        out.reserve(kHeader + body.size());
        put16(out, static_cast<std::uint16_t>(static_cast<std::uint16_t>(Method::Send) |
                                              static_cast<std::uint16_t>(Class::Indication)));
        put16(out, static_cast<std::uint16_t>(body.size()));
        put32(out, kCookie);
        out.insert(out.end(), id.begin(), id.end());
        out.insert(out.end(), body.begin(), body.end());
    }
    send_(server_.address, out);
    return true;
}

Client::Received Client::receive(std::span<const std::uint8_t> datagram, const Address& from, Address& peer,
                                 std::span<const std::uint8_t>& data, Clock::time_point now) {
    if (!active() || from != server_.address) return Received::NotMine;
    if (is_channel_data(datagram)) {
        const Peer* known = channel_peer(u16(datagram, 0));
        if (known == nullptr) return Received::Handled;
        peer = known->address;
        data = datagram.subspan(4u, u16(datagram, 2));
        return Received::Data;
    }
    if (!stun::is_stun(datagram)) return Received::NotMine;
    const std::uint16_t type = u16(datagram, 0);
    stun::TransactionId id{};
    std::copy_n(datagram.begin() + 8, id.size(), id.begin());
    if (type == (static_cast<std::uint16_t>(Method::Data) | static_cast<std::uint16_t>(Class::Indication))) {
        if (state_ != State::Ready) return Received::Handled;
        bool have_peer = false;
        bool have_data = false;
        (void)attributes(datagram, [&](std::uint16_t attr, std::span<const std::uint8_t> value) {
            if (attr == kXorPeerAddress) have_peer = read_xor_address(value, id, peer);
            if (attr == kData) {
                data = value;
                have_data = true;
            }
        });
        return have_peer && have_data ? Received::Data : Received::Handled;
    }
    const auto it = std::find_if(pending_.begin(), pending_.end(), [&](const Request& r) { return r.id == id; });
    if (it == pending_.end()) return Received::NotMine;
    const Request request = *it;
    pending_.erase(it);
    answer(request, type, datagram, now);
    return Received::Handled;
}

void Client::answer(Request request, std::uint16_t type, std::span<const std::uint8_t> message,
                    Clock::time_point now) {
    const std::uint16_t method = static_cast<std::uint16_t>(type & ~0x0110u);
    const std::uint16_t message_class = static_cast<std::uint16_t>(type & 0x0110u);
    if (method != static_cast<std::uint16_t>(request.method)) return;
    if (message_class == static_cast<std::uint16_t>(Class::Error)) {
        int code = 0;
        std::string realm;
        std::string nonce;
        (void)attributes(message, [&](std::uint16_t attr, std::span<const std::uint8_t> value) {
            if (attr == kErrorCode && value.size() >= 4u) code = (value[2] & 0x07u) * 100 + value[3];
            if (attr == kRealm) realm = text_of(value);
            if (attr == kNonce) nonce = text_of(value);
        });
        // 401: the credentials asked for (an Allocate's first try), 438: a nonce gone stale. Asked again with them,
        // once each: a 401 to a request that had them means they're wrong.
        if ((code == 401 && !request.authed) || code == 438) {
            if (!realm.empty()) realm_ = realm;
            if (!nonce.empty()) nonce_ = nonce;
            if (nonce_.empty()) {
                if (request.method == Method::Allocate) fail("the relay asked for credentials without a nonce");
                return;
            }
            const std::string material = server_.username + ":" + realm_ + ":" + server_.password;
            const auto key = digest::md5(
                std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(material.data()), material.size()));
            key_.assign(key.begin(), key.end());
            if (request.tries > kTries * 2) return;
            request.authed = true;
            const int tries = request.tries;
            ask(request, now);
            pending_.back().tries = tries + 1;
            return;
        }
        if (request.method == Method::Allocate) {
            fail("the relay refused (" + std::to_string(code) + ")");
        } else if (request.method == Method::Refresh && request.lifetime != 0u) {
            // The allocation is gone (its credentials expired, or the server moved it): nothing reaches the host now.
            fail("the relay ended (" + std::to_string(code) + ")");
        } else if (request.method == Method::ChannelBind) {
            // Send indications still work under the permission: the channel goes.
            if (Peer* peer = find_peer(request.peer)) peer->channel_ready = false;
        }
        return;
    }
    if (message_class != static_cast<std::uint16_t>(Class::Success)) return;
    switch (request.method) {
    case Method::Allocate: {
        if (state_ != State::Allocating) return;
        Address relayed;
        (void)attributes(message, [&](std::uint16_t attr, std::span<const std::uint8_t> value) {
            if (attr == kXorRelayedAddress) (void)read_xor_address(value, request.id, relayed);
            if (attr == kLifetime && value.size() >= 4u) lifetime_ = std::max<std::uint32_t>(u32(value, 0), 60u);
        });
        if (!relayed.valid()) {
            fail("the relay gave no address");
            return;
        }
        relayed_ = relayed;
        state_ = State::Ready;
        allocated_ = refreshed_ = now;
        // Permissions and channels asked for while it allocated.
        for (Peer& peer : peers_) {
            peer.refreshed = now;
            ask_peer(peer, now);
        }
        return;
    }
    case Method::ChannelBind:
        if (Peer* peer = find_peer(request.peer); peer != nullptr && peer->channel == request.channel) {
            peer->channel_ready = true;
        }
        return;
    default: return;
    }
}

}  // namespace galaxy::net::turn
