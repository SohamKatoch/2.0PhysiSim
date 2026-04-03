#pragma once

namespace physisim::client {

/// Standalone Vulkan + ImGui viewer that drives `physisim` over HTTP (`--ipc-port` on the server).
class ClientApp {
public:
    int run(int argc, char** argv);
    void addScrollDelta(float y) { pendingScrollY_ += y; }

private:
    float pendingScrollY_{0.f};
};

} // namespace physisim::client
