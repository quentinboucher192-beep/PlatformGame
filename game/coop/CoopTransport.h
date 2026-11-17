#pragma once

// 2.62: how a game reaches the relay of its team (docs/COOP.md § 4): a TCP connection to a PC of
// the local network (each message preceded by its length), or a WebSocket to the online room
// (WinHTTP on Windows, libcurl elsewhere). Used only by the network thread of NetCoopLink.

#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <vector>

namespace cosmic {

class CoopTransport {
public:
    virtual ~CoopTransport() = default;
    /// Connects (blocking, bounded); false with `error` in French.
    virtual bool connect(std::string& error) = 0;
    virtual bool send(const std::vector<uint8_t>& msg) = 0;
    /// Waits at most `waitMs` for messages; false when the connection is closed.
    virtual bool receive(std::deque<std::vector<uint8_t>>& out, int waitMs) = 0;
    virtual void close() = 0;
};

/// The relay of a PC of the local network.
std::unique_ptr<CoopTransport> makeLanTransport(const std::string& host, uint16_t port);
/// The online room: `url` http(s)://…/api/v1/coop/rooms/<id>/socket, `token` the session's.
std::unique_ptr<CoopTransport> makeWebSocketTransport(const std::string& url, const std::string& token);

/// WebSocket frames (RFC 6455), shared by the implementations that read raw bytes.
namespace ws {
/// A binary frame from the client (masked with `mask`).
std::vector<uint8_t> clientFrame(const std::vector<uint8_t>& payload, uint8_t opcode, uint32_t mask);
/// Reads the frames complete in `in` (server frames, unmasked); `pong` gets the answers due
/// (pings). False on a close frame or a malformed one.
bool readFrames(std::vector<uint8_t>& in, std::vector<uint8_t>& partial, std::deque<std::vector<uint8_t>>& out,
                std::vector<std::vector<uint8_t>>& pongs);
}  // namespace ws

}  // namespace cosmic
