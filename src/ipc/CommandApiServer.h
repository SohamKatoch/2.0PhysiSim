#pragma once

#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

namespace physisim::ipc {

enum class PendingOpType { LoadStl, CommandJson, ExportStl, SetActive };

struct PendingOp {
    PendingOpType type;
    std::string payload;
};

/// Minimal HTTP API (default bind 127.0.0.1) — for terminals / custom UIs. Mutations are queued for the main thread.
class CommandApiServer {
public:
    CommandApiServer();
    ~CommandApiServer();

    /// `getSceneJson` / `getMeshStlBinary` run on worker threads — must be thread-safe.
    /// `getMeshStlBinary` empty vector → GET /v1/mesh/stl returns 404.
    /// `listenHost` e.g. "127.0.0.1" (default) or "0.0.0.0" for containers (exposes API on all interfaces).
    bool start(uint16_t port, std::function<std::string()> getSceneJson,
                std::function<std::vector<uint8_t>()> getMeshStlBinary = {},
                std::string listenHost = "127.0.0.1");
    void stop();

    bool poll(PendingOp& out);

private:
    void threadMain(std::shared_ptr<std::promise<void>> ready);
    void enqueue(PendingOp op);

    std::function<std::string()> sceneFn_;
    std::function<std::vector<uint8_t>()> meshStlFn_;
    std::string listenHost_{"127.0.0.1"};
    uint16_t port_{0};

    std::mutex qMu_;
    std::queue<PendingOp> q_;

    std::thread thread_;
    void* server_{nullptr}; // httplib::Server* — avoid including httplib in header
};

} // namespace physisim::ipc
