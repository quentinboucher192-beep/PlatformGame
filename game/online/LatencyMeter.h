#pragma once
// 2.72: the round trip to Cosmic Online shown next to « En ligne » (« En ligne - 25ms »). The
// samples are the light requests the game already makes — its presence heartbeat (every minute
// with an account) and its health check — timed on the network thread by the transport
// (HttpResponse::roundTripMs); the figure is the median of the last few, so one slow exchange
// (a new connection) does not make it jump. Never a guess: without a sample there is no figure,
// and going offline forgets them (a new one is measured once the server answers again).

#include <cstddef>
#include <deque>
#include <string_view>

namespace cosmic {

class LatencyMeter {
public:
    static constexpr size_t kSamples = 5;

    /// A measured round trip in milliseconds; refused when not finite, negative or over a minute.
    void add(double ms);
    void clear() { samples_.clear(); }
    bool empty() const { return samples_.empty(); }
    size_t size() const { return samples_.size(); }
    /// The median of the last samples, rounded to the millisecond; -1 without any.
    int ms() const;

    /// Whether the answer to `path` is a sample: the light requests only (the heartbeat of the
    /// presence, the health check), never a list, an upload or a download.
    static bool probe(std::string_view path);

private:
    std::deque<double> samples_;
};

}  // namespace cosmic
