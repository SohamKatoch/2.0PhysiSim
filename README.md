
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

<<<<<<< HEAD
Optional: [Ollama](https://ollama.com/) (`llama3.1:8b`, `qwen2.5-math:7b` or matching client defaults).

## Build & run

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

- **Windows:** `build/Release/physisim.exe` — keep Vulkan SDK `Bin` on `PATH` for `glslc`.
- **VS 18 tools + VS 17 2022 generator / MSB8020:** see **WINDOWS_SETUP** §3.
- **Linux/macOS:** `build/physisim`.

Shaders compile into `build/.../shaders/`; `PHYSISIM_SHADER_DIR` points there.

**`physisim` not on PATH:** the `.exe` stays under `build\` — use a full path, or from repo root `.\scripts\Run-PhysiSim.ps1` (see script for flags). Debug builds use `build\Debug\`.

### Docker

Linux image: builds in-container, runs **Xvfb** + **Lavapipe** (CPU Vulkan) so GLFW/Vulkan start without a physical display. The HTTP API binds **`0.0.0.0`** inside the image so you can publish a port.

```bash
docker compose up --build
# API: http://127.0.0.1:17500/v1/health
```

Override bind address or port with **`PHYSISIM_IPC_HOST`** / **`PHYSISIM_IPC_PORT`** or **`--ipc-host`** / **`--ipc-port`**. Binding to all interfaces exposes the API with **no authentication** — use only on trusted networks or behind a reverse proxy.

## Localhost HTTP API

**127.0.0.1 only:** `physisim --ipc-port 17500` or `PHYSISIM_IPC_PORT=17500`.

| | |
|--|--|
| Health | `curl -s http://127.0.0.1:17500/v1/health` |
| Scene | `curl -s http://127.0.0.1:17500/v1/scene` |
| Load STL | `POST /v1/stl/load` JSON `{"path":"C:/models/part.stl"}` |
| Export STL | `POST /v1/stl/export` JSON `{"path":"C:/models/out.stl"}` |
| Command | `POST /v1/command` JSON e.g. `transform` with `parameters` |
| Active model | `POST /v1/scene/active` JSON `{"id":"demo"}` |
| Mesh (binary) | `GET /v1/mesh/stl` → active mesh |

PowerShell: `Invoke-RestMethod http://127.0.0.1:17500/v1/scene`

```bash
curl -s -X POST http://127.0.0.1:17500/v1/stl/load -H "Content-Type: application/json" -d "{\"path\":\"C:/models/part.stl\"}"
curl -s -X POST http://127.0.0.1:17500/v1/command -H "Content-Type: application/json" -d "{\"action\":\"transform\",\"operations\":[],\"parameters\":{\"translate\":[0,0.1,0]}}"
curl -s -o mesh.stl http://127.0.0.1:17500/v1/mesh/stl
```

Mutations apply on the **next frame**. `GET /v1/mesh/stl` is `application/octet-stream`. `physisim --help` lists flags.

## Vulkan remote client (`physisim_client`)

1. `physisim --ipc-port 17500`
2. `physisim_client --host 127.0.0.1 --port 17500`

**Pull mesh (Vulkan)** fetches STL over HTTP into this process’s GPU. Orbit: **RMB** + wheel. Built next to `physisim` in the same build dir.

## Using the app

1. **Start** `physisim` — Vulkan viewport + ImGui panels.
2. **Camera:** RMB orbit · wheel or `=` / `-` zoom · **Esc** quit.
3. **Mesh insight (optional):** **Viewport & STL** → enable hover/pick; **LMB** face inspector; **Shift+click** reserved.
4. **Load STL (mm):** drag-drop, **Browse** (Windows), or path + **Load**. Set **Analysis → mm per 1 mesh unit** to `1.0` for mm files.
5. **Export:** set path → **Export binary STL** (active mesh; scene transform **not** baked).
6. **GPU smoothing:** **Run GPU Laplacian (1 step)** on active mesh (needs compute init).
7. **Models:** **Models in scene** — click to set **Active** (only active mesh is drawn).
8. **Commands / AI:** **Interpret + execute (LLM)** or paste JSON → **Execute JSON**. **Log** shows errors and results.
9. **Analysis:** density (kg/m³), toggles, **Run analysis** → JSON + viewport tinting. After a run, **Defect heatmap (multi-channel)** exposes stress / velocity / load **scales**, **time mix** (blend toward the **strain** scalar channel), **visual mode** (combined heatmap, RGB-style channels, or multi-objective emphasis), and **simulation scenario** controls (material, scenario type, speed / intensity / duration) that drive mass–spring loads and heatmap weights. **Mass–spring preview (CPU)** (optional) deforms the active mesh and drives **strain-based** stress for the viewport; see [below](#mass-spring-preview-cpu). Hover tooltips and the face inspector show merged severity, weighted combo, Laplacian vs strain where applicable, and (Ctrl) defect-direction hints.
10. **Benchmark:** baseline STL path → **Compare baseline vs active** for deterministic deltas.

## Features & AI phases

**Summary:** STL load/export, optional mesh insight, GPU Laplacian step, JSON + optional NL commands (Ollama), analysis with multi-channel severity coloring, optional **CPU mass–spring** strain preview (not FEA), baseline comparison.

| Phase | Behavior |
|-------|----------|
| **1 — Context loop** | `FeedbackBuilder` vs `ground_truth`; `AnalysisClient::refineWithFeedback` is interpretive only — cannot override engine numbers. |
| **2 — RAG memory** | `AnalysisMemory` + fingerprinted snapshots; retrieval via L1 feature distance. |
| **3 — Fine-tuning** | Out of repo; runtime rule: **engine = reality**, **AI = interpretation + suggestions**. |

### Defect highlighting (viewport)

Per-triangle state is a **`TriangleWeakness`** (`src/analysis/TriangleWeakness.h`): **geo** (thin slivers, non-manifold (5), open boundaries (2), inconsistent normals when mesh-wide threshold trips (3)), **`stressProxy`** (Laplacian-derived from analysis), **`strainStress`** (optional; from **mass–spring** edge strain when that preview is running), optional **velocity** / **load** weights derived from the simulation scenario (deterministic), and **defect direction** (face normal hint for tooling or future arrow viz). AI `design_actions` still merge with **`std::max`** on the scalar overlay so interpretation cannot reduce deterministic checks.

The stress **channel** sent to the GPU uses **`max(stressProxy, strainStress)`** per triangle (then averaged to vertices).

**GPU:** each vertex carries **`defectHighlight`** as a **`glm::vec4`** (geo, stress, velocity, load), averaged from incident triangles, plus **`weaknessPropagated`** (scalar), filled with the per-triangle **strain** value for shader **time mix**. Uniforms supply scales, visual mode, and time mix; **`shaders/mesh.frag`** maps the result to the usual cool→hot tint (or channel / alignment modes). **GPU Laplacian** still reads/writes the **`.x`** (geo) channel as the smoothing weight passthrough.

### Mass–spring preview (CPU)

- **Code:** `src/sim/MassSpringSystem.{h,cpp}`, `SimulationScenario.{h,cpp}`, `Constraints.{h,cpp}`, `SimMaterial.h` — one spring per **unique undirected edge**, lumped mass from shell area × thickness × density, **stiffness** reduced where **`geoWeakness`** is high on adjacent triangles, explicit Euler with **damping**, optional **constraints** (open boundary + heuristic mounts with partial axis locks), optional **max displacement** clamp, and **external** body acceleration from **simulation scenarios** (Highway / Braking / Cornering / Bump) with **F = m a** in model space (+Z forward, +X lateral, +Y up). Strain **stress = E·strain** feeds normalized **`strainStress`** via `maxStrain`.
- **UI:** **Analysis** panel → **Material** / **Scenario** dropdowns, speed (mph) / intensity / duration, **Enable constraints**, **Reset simulation**; **Reset mesh to analysis rest** restores the pose stored at the last **Run analysis** (and clears strain).
- **Not FEA:** This is a **toy** visualization aid. It **mutates** `Mesh::positions` while enabled; **Run analysis** restores from the saved rest snapshot before recomputing. **Export STL** writes the **current** CPU mesh (including deformation if you exported while the preview was on).

When adding real FEA or GPU solvers, treat this path as a placeholder and keep **engine / solver** outputs separate from AI narrative (see **ARCHITECTURE.md**).

## Command schema

```json
{
  "action": "create | modify | boolean | transform | analyze | analyze_fem",
=======
Command Schema
json{
  "action": "create | transform | analyze | analyze_fem",
>>>>>>> 30adb9a61f678d83fd68654a95cc25bd4dce598d
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
