#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "game/online/HttpTransport.h"

namespace cosmic {

/// Asynchronous HTTP executor: one dedicated network thread performs the blocking
/// transfers; completed responses are handed back to the main thread by poll(), which
/// runs the callbacks. The render loop therefore never waits for the network.
class OnlineClient {
public:
    using Callback = std::function<void(const HttpResponse&)>;
    using RequestId = uint64_t;

    explicit OnlineClient(std::unique_ptr<IHttpTransport> transport);
    ~OnlineClient();
    OnlineClient(const OnlineClient&) = delete;
    OnlineClient& operator=(const OnlineClient&) = delete;

    /// Queues a request (thread-safe). The callback runs later on the thread calling poll().
    RequestId send(HttpRequest request, Callback callback);
    /// Cancels a queued or running request: its callback receives NetError::Cancelled.
    void cancel(RequestId id);
    void cancelAll();
    /// Main thread: delivers completed responses. Returns the number of callbacks run.
    size_t poll();
    /// Requests queued or running, not yet delivered by poll().
    size_t pending() const;
    /// Blocks until every queued request completed (tests, shutdown); callbacks are not run.
    bool waitIdle(int timeoutMs);
    const char* transportName() const { return transport_->name(); }

private:
    struct Job {
        RequestId id;
        HttpRequest request;
        Callback callback;
    };
    struct Done {
        RequestId id;
        HttpResponse response;
        Callback callback;
    };
    void run();

    std::unique_ptr<IHttpTransport> transport_;
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::condition_variable idle_;
    std::deque<Job> queue_;
    std::vector<Done> done_;
    RequestId nextId_ = 1;
    RequestId running_ = 0;
    std::shared_ptr<std::atomic<bool>> runningCancel_;
    bool stop_ = false;
    std::thread thread_;
};

}  // namespace cosmic
