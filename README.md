PhysiSim CAD 2.0
A desktop CAD prototype with a Vulkan viewport, AI-assisted geometry commands, and real-time mesh analysis. Load an STL, run structural analysis, and get per-triangle defect highlighting — all driven by a JSON command system that two AI roles (design and analysis) can talk to via Ollama.

Windows users: if cmake or physisim isn't found in your shell, see docs/WINDOWS_SETUP.md.


Quick Start

Install CMake 3.20+, a C++20 compiler, and the Vulkan SDK (glslc must be on your PATH).
Build:

bash   cmake -B build -DCMAKE_BUILD_TYPE=Release
   cmake --build build --config Release

Run build/Release/physisim.exe (Windows) or build/physisim (Linux/macOS).

Optional two-window setup: physisim --ipc-port 17500, then physisim_client --host 127.0.0.1 --port 17500.

Prerequisites

CMake 3.20+
Vulkan SDK
C++20 compiler (MSVC 2022, Clang, or GCC)
Git
Optional: Ollama with llama3.1:8b / qwen2.5-math:7b for AI commands


Basic Usage

Launch physisim — you'll get a Vulkan viewport and ImGui panels.
Camera: RMB to orbit, scroll to zoom, Esc to quit.
Load a mesh: drag and drop an STL, or use Browse. For mm-unit files set Analysis → mm per 1 mesh unit to 1.0.
Run analysis: set density (kg/m³) and click Run analysis — the viewport tints triangles by defect severity.
AI commands: type natural language into Interpret + execute (LLM), or paste JSON directly into Execute JSON.
Export: set a path and click Export binary STL (scene transforms are not baked in).


HTTP API
Start with physisim --ipc-port 17500 to enable the local API (127.0.0.1 only).
EndpointWhat it doesGET /v1/healthHealth checkGET /v1/sceneCurrent scene statePOST /v1/stl/loadLoad an STL: {"path":"C:/models/part.stl"}POST /v1/stl/exportExport STL: {"path":"C:/models/out.stl"}POST /v1/commandRun a command (e.g. transform)GET /v1/mesh/stlDownload active mesh as binary STL

Command Schema
json{
  "action": "create | transform | analyze | analyze_fem",
  "operations": [],
  "parameters": {},
  "target": "optional_model_id"
}

Further Reading
DocContentsdocs/ARCHITECTURE.mdFull module breakdown, AI design, planned featuresdocs/LEARNINGS_AND_PITFALLS.mdKnown issues and open questionsdocs/WINDOWS_SETUP.mdWindows-specific toolchain setup

Roadmap

More mesh ops: decimation, smoothing, spatial queries
GPU-friendly physics kernels / proper elasticity solver
Deeper FEM integration via CalculiX


License
Prototype. Depends on GLFW, ImGui, glm, nlohmann/json, and cpp-httplib under their respective licenses.
