#pragma once

#include <string>

namespace physisim::ai {

struct LLMClientConfig {
    std::string host = "127.0.0.1";
    int port = 11434;
    std::string model = "llama3.1:8b";
    int timeoutSeconds = 120;
};

class LLMClient {
public:
    explicit LLMClient(LLMClientConfig cfg = {});

    /// Raw completion from local server (Ollama-compatible /api/generate).
    bool generate(const std::string& prompt, std::string& outText, std::string& err);

    const LLMClientConfig& config() const { return cfg_; }

private:
    LLMClientConfig cfg_;
};

} // namespace physisim::ai
