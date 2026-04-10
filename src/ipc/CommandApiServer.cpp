#include "ipc/CommandApiServer.h"

#include <cstdio>
#include <memory>

#include <httplib.h>
#include <nlohmann/json.hpp>

namespace physisim::ipc {

namespace {

std::string jsonError(const std::string& msg) {
    return nlohmann::json{{"ok", false}, {"error", msg}}.dump();
}

std::string jsonOk() { return R"({"ok":true})"; }

} // namespace

CommandApiServer::CommandApiServer() = default;

CommandApiServer::~CommandApiServer() { stop(); }

void CommandApiServer::enqueue(PendingOp op) {
    std::lock_guard<std::mutex> lk(qMu_);
    q_.push(std::move(op));
}

bool CommandApiServer::poll(PendingOp& out) {
    std::lock_guard<std::mutex> lk(qMu_);
    if (q_.empty()) return false;
    out = std::move(q_.front());
    q_.pop();
    return true;
}

void CommandApiServer::threadMain(std::shared_ptr<std::promise<void>> ready) {
    httplib::Server svr;
    server_ = &svr;
    if (ready) ready->set_value();

    svr.Get("/v1/health", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(R"({"ok":true,"service":"physisim-ipc"})", "application/json");
    });

    svr.Get("/v1/scene", [this](const httplib::Request&, httplib::Response& res) {
        if (!sceneFn_) {
            res.status = 503;
            res.set_content(jsonError("scene snapshot unavailable"), "application/json");
            return;
        }
        try {
            std::string snap = sceneFn_();
            res.set_content(snap, "application/json");
        } catch (...) {
            res.status = 500;
            res.set_content(jsonError("scene snapshot failed"), "application/json");
        }
    });

    if (meshStlFn_) {
        svr.Get("/v1/mesh/stl", [this](const httplib::Request&, httplib::Response& res) {
            try {
                std::vector<uint8_t> blob = meshStlFn_();
                if (blob.empty()) {
                    res.status = 404;
                    res.set_content(jsonError("no active mesh or empty geometry"), "application/json");
                    return;
                }
                res.set_content(reinterpret_cast<const char*>(blob.data()), blob.size(),
                                "application/octet-stream");
            } catch (...) {
                res.status = 500;
                res.set_content(jsonError("mesh export failed"), "application/json");
            }
        });
    }

    svr.Post("/v1/stl/load", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            auto j = nlohmann::json::parse(req.body.empty() ? "{}" : req.body);
            if (!j.contains("path") || !j["path"].is_string()) {
                res.status = 400;
                res.set_content(jsonError("JSON body must contain string \"path\""), "application/json");
                return;
            }
            enqueue({PendingOpType::LoadStl, j["path"].get<std::string>()});
            res.set_content(jsonOk(), "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(jsonError(e.what()), "application/json");
        }
    });

    svr.Post("/v1/stl/export", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            auto j = nlohmann::json::parse(req.body.empty() ? "{}" : req.body);
            if (!j.contains("path") || !j["path"].is_string()) {
                res.status = 400;
                res.set_content(jsonError("JSON body must contain string \"path\""), "application/json");
                return;
            }
            enqueue({PendingOpType::ExportStl, j["path"].get<std::string>()});
            res.set_content(jsonOk(), "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(jsonError(e.what()), "application/json");
        }
    });

    svr.Post("/v1/command", [this](const httplib::Request& req, httplib::Response& res) {
        if (req.body.empty()) {
            res.status = 400;
            res.set_content(jsonError("empty body"), "application/json");
            return;
        }
        enqueue({PendingOpType::CommandJson, req.body});
        res.set_content(jsonOk(), "application/json");
    });

    svr.Post("/v1/scene/active", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            auto j = nlohmann::json::parse(req.body.empty() ? "{}" : req.body);
            if (!j.contains("id") || !j["id"].is_string()) {
                res.status = 400;
                res.set_content(jsonError("JSON body must contain string \"id\""), "application/json");
                return;
            }
            enqueue({PendingOpType::SetActive, j["id"].get<std::string>()});
            res.set_content(jsonOk(), "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(jsonError(e.what()), "application/json");
        }
    });

    if (!svr.listen(listenHost_.c_str(), static_cast<int>(port_))) {
        std::fprintf(stderr, "[ipc] Failed to listen on %s:%u (port in use?)\n", listenHost_.c_str(), port_);
    }
    server_ = nullptr;
}

bool CommandApiServer::start(uint16_t port, std::function<std::string()> getSceneJson,
                             std::function<std::vector<uint8_t>()> getMeshStlBinary,
                             std::string listenHost) {
    stop();
    if (port == 0) return false;
    port_ = port;
    listenHost_ = listenHost.empty() ? "127.0.0.1" : std::move(listenHost);
    sceneFn_ = std::move(getSceneJson);
    meshStlFn_ = std::move(getMeshStlBinary);
    auto ready = std::make_shared<std::promise<void>>();
    std::future<void> fut = ready->get_future();
    thread_ = std::thread([this, ready] { threadMain(std::move(ready)); });
    fut.wait();
    return true;
}

void CommandApiServer::stop() {
    if (server_) {
        static_cast<httplib::Server*>(server_)->stop();
    }
    if (thread_.joinable()) thread_.join();
    server_ = nullptr;
}

} // namespace physisim::ipc
