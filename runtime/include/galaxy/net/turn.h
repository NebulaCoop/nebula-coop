// A TURN client (RFC 8656) for joins that can't punch through (docs/COOP_PLAN.md, "Hosting"; NebulaNet's
// /v1/relay): the joiner takes an address on a TURN server (Cloudflare's, or one NebulaNet was set up with) and the
// host sends to that address as to any joiner's. Only the joiner speaks TURN, over the same socket it plays on, so the
// host's router only ever sees the host send first.
//
// The small part of TURN that needs: Allocate (UDP), Refresh, CreatePermission, ChannelBind, Send and Data
// indications and ChannelData, with long-term credentials (a short-lived username and password; the realm and nonce
// come from the server's first 401). It never touches the socket itself: what it sends goes through the function it
// was made with, and the transport hands it what came from the server.
//
// Not thread-safe: the transport's thread calls everything.
#pragma once

#include "galaxy/net/stun.h"
#include "galaxy/net/udp.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <random>
#include <span>
#include <string>
#include <vector>

namespace galaxy::net::turn {

// A TURN server and the credentials NebulaNet made for this join.
struct Server {
    Address address;
    std::string username;
    std::string password;
};

// Whether `datagram` is ChannelData (its first two bits 01, a channel number from 0x4000, its length within).
[[nodiscard]] bool is_channel_data(std::span<const std::uint8_t> datagram) noexcept;

class Client {
public:
    using Clock = std::chrono::steady_clock;
    // Sends a datagram to the TURN server (the transport's socket).
    using Send = std::function<void(const Address& to, std::span<const std::uint8_t> datagram)>;

    enum class State : std::uint8_t { Idle, Allocating, Ready, Failed };
    // What receive made of a datagram from the server.
    enum class Received : std::uint8_t {
        NotMine,  // not TURN, or an answer to nothing this client asked (a STUN Binding answer, say)
        Handled,  // an answer to one of its requests
        Data,     // a peer's datagram, relayed: `peer` and `data` say whose and what
    };

    // How long a request is sent again before it's given up, and how often.
    static constexpr std::chrono::milliseconds kResendEvery{500};
    static constexpr int kTries = 8;
    // A permission lasts 5 minutes and a channel 10 (RFC 8656): both are asked again this often.
    static constexpr std::chrono::seconds kPeerRefreshEvery{120};
    // The first channel number (0x4000 to 0x4FFF are a client's).
    static constexpr std::uint16_t kFirstChannel = 0x4000u;

    explicit Client(Send send);

    // Asks `server` for an address (Allocate): state() is Allocating, then Ready (relayed_address()) or Failed.
    void start(Server server, Clock::time_point now);
    // Gives the address back (a Refresh to lifetime 0, sent once and not waited for) and goes back to Idle.
    void stop();
    // Sends again what wasn't answered, and refreshes the allocation, permissions and channels when due.
    void update(Clock::time_point now);
    // A datagram from `from`: only the server's are looked at.
    Received receive(std::span<const std::uint8_t> datagram, const Address& from, Address& peer,
                     std::span<const std::uint8_t>& data, Clock::time_point now);

    // Lets `peer`'s IP address send to the relayed address (CreatePermission; asked once Ready). Private, loopback
    // and link-local addresses are refused by most servers: they're left out here.
    void permit(const Address& peer);
    // A channel to `peer` (address and port), so what goes to it costs 4 bytes instead of 36 (ChannelBind; it
    // permits the IP too). Until the server answers, send() uses Send indications.
    void bind(const Address& peer);
    // Whether `peer` is reached through the relay (it was bound).
    [[nodiscard]] bool routes(const Address& peer) const;
    // Relays `data` to `peer`: ChannelData on its channel, or a Send indication. False when not Ready.
    bool send(const Address& peer, std::span<const std::uint8_t> data);

    [[nodiscard]] State state() const noexcept { return state_; }
    [[nodiscard]] bool active() const noexcept { return state_ == State::Allocating || state_ == State::Ready; }
    [[nodiscard]] const Address& server_address() const noexcept { return server_.address; }
    // The address the server gave (what the host sends to), once Ready.
    [[nodiscard]] const Address& relayed_address() const noexcept { return relayed_; }
    // Why it failed, for the log.
    [[nodiscard]] const std::string& failure() const noexcept { return failure_; }

    // Whether an IP address may be permitted (a public one, unless allow_private_peers).
    [[nodiscard]] static bool permittable(const Address& address);
    // Tests (everything on the loopback): private and loopback peers are permitted too, for every client.
    static void allow_private_peers(bool allow) noexcept;

private:
    enum class Method : std::uint16_t {
        Allocate = 0x003, Refresh = 0x004, Send = 0x006, Data = 0x007, CreatePermission = 0x008, ChannelBind = 0x009,
    };
    struct Request {
        Method method = Method::Allocate;
        stun::TransactionId id{};
        Address peer;               // a CreatePermission's or ChannelBind's
        std::uint16_t channel = 0u; // a ChannelBind's
        std::uint32_t lifetime = 0u;  // a Refresh's
        int tries = 0;
        Clock::time_point last{};
        bool authed = false;        // sent with the credentials (an Allocate's first try isn't)
    };
    struct Peer {
        Address address;
        std::uint16_t channel = 0u;  // 0: a permission only
        bool channel_ready = false;
        Clock::time_point refreshed{};
    };

    std::vector<std::uint8_t> build(const Request& request) const;
    void ask(Request request, Clock::time_point now);
    void resend(Request& request, Clock::time_point now);
    void answer(Request request, std::uint16_t type, std::span<const std::uint8_t> message, Clock::time_point now);
    void ask_peer(const Peer& peer, Clock::time_point now);
    void fail(std::string why);
    Peer* find_peer(const Address& address);
    const Peer* channel_peer(std::uint16_t channel) const;

    Send send_;
    Server server_;
    State state_ = State::Idle;
    std::string realm_;
    std::string nonce_;
    std::vector<std::uint8_t> key_;  // MD5(username:realm:password)
    Address relayed_;
    std::uint32_t lifetime_ = 600u;  // seconds, as the server said
    Clock::time_point allocated_{};
    Clock::time_point refreshed_{};
    std::vector<Request> pending_;
    std::vector<Peer> peers_;
    std::uint16_t next_channel_ = kFirstChannel;
    std::string failure_;
    std::mt19937_64 random_;
};

}  // namespace galaxy::net::turn
