#pragma once

#include <string>

namespace physisim::core {

class Application {
public:
    int run(int argc, char** argv);
    void markMeshDirty() { meshDirty_ = true; }
    void queueStlPath(std::string path) { pendingStlPath_ = std::move(path); }
    void addScrollDelta(float y) { pendingScrollY_ += y; }

private:
    std::string commandLog_;
    std::string analysisText_;
    bool useAiAnalysis_{false};
    bool analysisFeedbackLoop_{true};
    bool analysisUseRag_{false};
    bool analysisPersistCase_{false};
    float meshUnitToMm_{1.f};
    bool meshDirty_{true};

    std::string pendingStlPath_;
    float pendingScrollY_{0.f};
};

} // namespace physisim::core
