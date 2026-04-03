#include "ai/LLMClient.h"

#include <httplib.h>
#include <nlohmann/json.hpp>

namespace physisim::ai {

LLMClient::LLMClient(LLMClientConfig cfg) : cfg_(std::move(cfg)) {}

bool LLMClient::generate(const std::string& prompt, std::string& outText, std::string& err) {
    httplib::Client cli(cfg_.host.c_str(), cfg_.port);
    cli.set_connection_timeout(3, 0);
    cli.set_read_timeout(cfg_.timeoutSeconds, 0);
    cli.set_write_timeout(cfg_.timeoutSeconds, 0);

    nlohmann::json body;
    body["model"] = cfg_.model;
    body["prompt"] = prompt;
    body["stream"] = false;

    auto res = cli.Post("/api/generate", body.dump(), "application/json");
    if (!res) {
        err = "HTTP error connecting to LLM server (is Ollama running?)";
        return false;
    }
    if (res->status != 200) {
        err = "LLM HTTP " + std::to_string(res->status) + ": " + res->body;
        return false;
    }
    try {
        auto j = nlohmann::json::parse(res->body);
        if (j.contains("response") && j["response"].is_string()) {
            outText = j["response"].get<std::string>();
            return true;
        }
        err = "Unexpected LLM response JSON";
        return false;
    } catch (const std::exception& e) {
        err = e.what();
        return false;
    }
}

} // namespace physisim::ai
