#pragma once

#include <string>

namespace physisim::ai {

struct MathClientConfig {
    std::string host = "127.0.0.1";
    int port = 11434;
    std::string model = "qwen2.5-math-7b";
    int timeoutSeconds = 120;
};

class MathClient {
public:
    explicit MathClient(MathClientConfig cfg = {});

    /// Ask for a single numeric or short symbolic resolution as plain text.
    bool solve(const std::string& question, std::string& outText, std::string& err);

private:
    MathClientConfig cfg_;
};

} // namespace physisim::ai
