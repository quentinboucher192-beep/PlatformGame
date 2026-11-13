#include "game/online/OnlineClient.h"

#include <chrono>

namespace cosmic {

OnlineClient::OnlineClient(std::unique_ptr<IHttpTransport> transport) : transport_(std::move(transport)) {
    thread_ = std::thread([this] { run(); });
}

OnlineClient::~OnlineClient() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
        for (auto& j : queue_) j.request.cancel->store(true);
        if (runningCancel_) runningCancel_->store(true);
    }
    wake_.notify_all();
    if (thread_.joinable()) thread_.join();
}

OnlineClient::RequestId OnlineClient::send(HttpRequest request, Callback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    RequestId id = nextId_++;
    if (!request.cancel) request.cancel = std::make_shared<std::atomic<bool>>(false);
    queue_.push_back({id, std::move(request), std::move(callback)});
    wake_.notify_one();
    return id;
}

void OnlineClient::cancel(RequestId id) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = queue_.begin(); it != queue_.end(); ++it) {
        if (it->id != id) continue;
        HttpResponse r;
        r.error = NetError::Cancelled;
        r.errorMessage = "annulée";
        done_.push_back({id, std::move(r), std::move(it->callback)});
        queue_.erase(it);
        return;
    }
    if (running_ == id && runningCancel_) runningCancel_->store(true);
}

void OnlineClient::cancelAll() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& j : queue_) {
        HttpResponse r;
        r.error = NetError::Cancelled;
        r.errorMessage = "annulée";
        done_.push_back({j.id, std::move(r), std::move(j.callback)});
    }
    queue_.clear();
    if (runningCancel_) runningCancel_->store(true);
}

size_t OnlineClient::poll() {
    std::vector<Done> ready;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ready.swap(done_);
    }
    for (auto& d : ready)
        if (d.callback) d.callback(d.response);
    return ready.size();
}

size_t OnlineClient::pending() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size() + done_.size() + (running_ ? 1 : 0);
}

bool OnlineClient::waitIdle(int timeoutMs) {
    std::unique_lock<std::mutex> lock(mutex_);
    return idle_.wait_for(lock, std::chrono::milliseconds(timeoutMs), [this] { return queue_.empty() && running_ == 0; });
}

void OnlineClient::run() {
    for (;;) {
        Job job;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait(lock, [this] { return stop_ || !queue_.empty(); });
            if (stop_) return;
            job = std::move(queue_.front());
            queue_.pop_front();
            running_ = job.id;
            runningCancel_ = job.request.cancel;
        }
        HttpResponse response;
        if (job.request.cancel->load()) {
            response.error = NetError::Cancelled;
            response.errorMessage = "annulée";
        } else {
            response = transport_->perform(job.request);
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (job.request.cancel->load() && response.error == NetError::None) {
                // Cancelled while the answer arrived: report the cancellation, not the data.
                response = HttpResponse{};
                response.error = NetError::Cancelled;
                response.errorMessage = "annulée";
            }
            done_.push_back({job.id, std::move(response), std::move(job.callback)});
            running_ = 0;
            runningCancel_.reset();
            if (queue_.empty()) idle_.notify_all();
        }
    }
}

}  // namespace cosmic
