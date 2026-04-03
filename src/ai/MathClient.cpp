#include "ai/MathClient.h"

#include <httplib.h>
#include <nlohmann/json.hpp>

namespace physisim::ai {

MathClient::MathClient(MathClientConfig cfg) : cfg_(std::move(cfg)) {}

bool MathClient::solve(const std::string& question, std::string& outText, std::string& err) {
    httplib::Client cli(cfg_.host.c_str(), cfg_.port);
    cli.set_connection_timeout(3, 0);
    cli.set_read_timeout(cfg_.timeoutSeconds, 0);

    nlohmann::json body;
    body["model"] = cfg_.model;
    body["prompt"] =
        "Reply with ONLY the final number or short expression, no prose.\nProblem: " + question;
    body["stream"] = false;

    auto res = cli.Post("/api/generate", body.dump(), "application/json");
    if (!res) {
        err = "HTTP error connecting to math model";
        return false;
    }
    if (res->status != 200) {
        err = "Math model HTTP " + std::to_string(res->status);
        return false;
    }
    try {
        auto j = nlohmann::json::parse(res->body);
        if (j.contains("response") && j["response"].is_string()) {
            outText = j["response"].get<std::string>();
            return true;
        }
        err = "Unexpected math response";
        return false;
    } catch (const std::exception& e) {
        err = e.what();
        return false;
    }
}

} // namespace physisim::ai
