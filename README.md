# PhysiSim CAD (2.0)

Mesh-first CAD prototype toward a **GPU + AI CAD/FEA** stack: **Vulkan** viewport, **command-based** geometry, **two-role AI** (design vs analysis signals over Ollama HTTP), and **deterministic + AI-assisted** mesh analysis with **multi-channel defect visualization** (geometry, Laplacian stress proxy, optional **CPU mass–spring strain**, kinematic weights).

| Doc | Purpose |
|-----|---------|
| [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) | Roadmap: what exists vs planned (elasticity solvers, full CalculiX depth, etc.) |
| [`docs/LEARNINGS_AND_PITFALLS.md`](docs/LEARNINGS_AND_PITFALLS.md) | Pitfalls and open questions |
| [`docs/WINDOWS_SETUP.md`](docs/WINDOWS_SETUP.md) | Windows toolchain, Vulkan SDK, CMake on `PATH`, MSB8020 / v143 |

**Windows quick fixes:** if `cmake` or `physisim` is missing from the shell, see **WINDOWS_SETUP**. CMake inside VS only → run `.\scripts\Ensure-CMakeOnPath.ps1` (new terminal) or **Developer PowerShell for VS**.

## Quick start

1. Install **CMake 3.20+**, **C++20**, and the **[Vulkan SDK](https://vulkan.lunarg.com/)** (`glslc` on `PATH` at configure time).
2. `cmake -B build -DCMAKE_BUILD_TYPE=Release` then `cmake --build build --config Release`.
3. Run `build/Release/physisim.exe` (Windows) or `build/physisim` (Linux/macOS). Optional: `physisim --ipc-port 17500`; second window: `physisim_client --host 127.0.0.1 --port 17500`.

## Contents

- [Layout](#layout)
- [Prerequisites](#prerequisites)
- [Build & run](#build--run)
- [Localhost HTTP API](#localhost-http-api)
- [Remote client](#vulkan-remote-client-physisim_client)
- [Using the app](#using-the-app)
- [Features & AI phases](#features--ai-phases)
- [Defect highlighting (viewport)](#defect-highlighting-viewport)
- [Mass–spring preview (CPU)](#mass-spring-preview-cpu)
- [Command schema](#command-schema)
- [FEM preflight](#fem-preflight-analyze_fem)
- [Roadmap](#roadmap)

## Layout

Source layout matches **ARCHITECTURE** (front-end, dual AI, FEA/CalculiX/workflow).

| Module | Role |
|--------|------|
| `core/` | `Application`, `Scene`, `CommandSystem` — shell + command bus |
| `geometry/` | `Mesh`, STL, `MeshOperations`, `GeometryEngine` |
| `rendering/` | `VulkanDevice`, pipelines, `Camera`, `RayPick` |
| `ai/` | **Model 1:** orchestration, LLM/math clients, validation · **Model 2:** `AnalysisClient` (signals; FEA fields TBD) |
| `analysis/` | Metrics, `GeometryAnalyzer`, `TriangleWeakness`, `MeshHighlightMerge`, `WeaknessField` (kinematic scenario helpers; legacy neighbor propagation helpers unused by default UI path) |
| `sim/` | `MassSpringSystem` — CPU edge mass–spring preview, strain → `TriangleWeakness::strainStress` |
| `ui/` | `ImGuiLayer` |
| `platform/` | `FileDialog` (Windows) |
| `ipc/` | `CommandApiServer` — localhost HTTP |
| `client/` | `physisim_client` — Vulkan viewer over HTTP |
| `fea/` | `GpuLaplacianSmooth` (compute), `MeshAdjacency` |
| `fem/` | `FemMeshReadiness`, CalculiX adapter, `TetrahedralMesh`, `FemCompare` |
| *(planned)* | In-process elasticity / CG / field viz — see ARCHITECTURE |

**AI rule:** models emit **validated JSON commands** only; `CommandSystem` → `GeometryEngine` applies changes.

**Viewport:** Ollama at `127.0.0.1:11434`. Vulkan draws the **active** mesh. After `create`, the new id is active. If the Log shows `[ok]` but the view is wrong, pick the model under **Viewport & STL → Models in scene**.

## Prerequisites

- [CMake](https://cmake.org/) 3.20+
- [Vulkan SDK](https://vulkan.lunarg.com/)
- C++20 (MSVC 2022, Clang, or GCC)
- Git (CMake `FetchContent`)

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
9. **Analysis:** density (kg/m³), toggles, **Run analysis** → JSON + viewport tinting. After a run, **Defect heatmap (multi-channel)** exposes stress / velocity / load **scales**, **time mix** (blend toward the **strain** scalar channel), **visual mode** (combined heatmap, RGB-style channels, or multi-objective emphasis), and **kinematic scenario** sliders (speed, accel/brake, cornering). **Mass–spring preview (CPU)** (optional) deforms the active mesh and drives **strain-based** stress for the viewport; see [below](#mass-spring-preview-cpu). Hover tooltips and the face inspector show merged severity, weighted combo, Laplacian vs strain where applicable, and (Ctrl) defect-direction hints.
10. **Benchmark:** baseline STL path → **Compare baseline vs active** for deterministic deltas.

## Features & AI phases

**Summary:** STL load/export, optional mesh insight, GPU Laplacian step, JSON + optional NL commands (Ollama), analysis with multi-channel severity coloring, optional **CPU mass–spring** strain preview (not FEA), baseline comparison.

| Phase | Behavior |
|-------|----------|
| **1 — Context loop** | `FeedbackBuilder` vs `ground_truth`; `AnalysisClient::refineWithFeedback` is interpretive only — cannot override engine numbers. |
| **2 — RAG memory** | `AnalysisMemory` + fingerprinted snapshots; retrieval via L1 feature distance. |
| **3 — Fine-tuning** | Out of repo; runtime rule: **engine = reality**, **AI = interpretation + suggestions**. |

### Defect highlighting (viewport)

Per-triangle state is a **`TriangleWeakness`** (`src/analysis/TriangleWeakness.h`): **geo** (thin slivers, non-manifold (5), open boundaries (2), inconsistent normals when mesh-wide threshold trips (3)), **`stressProxy`** (Laplacian-derived from analysis), **`strainStress`** (optional; from **mass–spring** edge strain when that preview is running), optional **velocity** / **load** weights from kinematic sliders, and **defect direction** (face normal hint for tooling or future arrow viz). AI `design_actions` still merge with **`std::max`** on the scalar overlay so interpretation cannot reduce deterministic checks.

The stress **channel** sent to the GPU uses **`max(stressProxy, strainStress)`** per triangle (then averaged to vertices).

**GPU:** each vertex carries **`defectHighlight`** as a **`glm::vec4`** (geo, stress, velocity, load), averaged from incident triangles, plus **`weaknessPropagated`** (scalar), filled with the per-triangle **strain** value for shader **time mix**. Uniforms supply scales, visual mode, and time mix; **`shaders/mesh.frag`** maps the result to the usual cool→hot tint (or channel / alignment modes). **GPU Laplacian** still reads/writes the **`.x`** (geo) channel as the smoothing weight passthrough.

### Mass–spring preview (CPU)

- **Code:** `src/sim/MassSpringSystem.{h,cpp}` — one spring per **unique undirected edge**, rest lengths from the mesh at build time, **stiffness** reduced where **`geoWeakness`** is high on adjacent triangles, explicit Euler integration with **damping**, optional **boundary pinning** (vertices on open edges), optional **max displacement** clamp, and **external** body forces derived from the same kinematic scenario sliders (+Z / −Y / +X in model space).
- **UI:** **Analysis** panel → enable **Mass–spring preview**, tune stiffness / damping / substeps / strain reference / external force; **Reset mesh to analysis rest** restores the pose stored at the last **Run analysis** (and clears strain).
- **Not FEA:** This is a **toy** visualization aid. It **mutates** `Mesh::positions` while enabled; **Run analysis** restores from the saved rest snapshot before recomputing. **Export STL** writes the **current** CPU mesh (including deformation if you exported while the preview was on).

When adding real FEA or GPU solvers, treat this path as a placeholder and keep **engine / solver** outputs separate from AI narrative (see **ARCHITECTURE.md**).

## Command schema

```json
{
  "action": "create | modify | boolean | transform | analyze | analyze_fem",
  "operations": [],
  "parameters": {},
  "target": "optional_model_id"
}
```

Implemented: `create` (cube, optional `attach_fem_demo_volume`), `transform` (translate / uniform scale), `analyze_fem` (CalculiX path — see below).

## FEM preflight (`analyze_fem`)

For real meshes (`demo_mesh` not true), surface **`Mesh`** is checked via `fem::evaluateFEMReadiness` / `checkFEMReadiness`.

| Class | Surface meaning | Default behavior |
|-------|-----------------|------------------|
| **INVALID** | Non-manifold, degenerate triangles, or self-intersection | **Blocked** — `[fem] preflight (blocked): …` |
| **NEEDS_REPAIR** | Open boundary, bad edge-length ratio, high aspect, or incomplete intersection scan | **Blocked** unless `"fem_allow_needs_repair": true` |
| **READY** | Watertight, manifold, acceptable quality, no hits in scan | **Proceed** |

Report JSON: `status` (`ok` / `warning` / `blocked`), `readiness`, `issues[]`, `suggestions[]`.

Parameters:

- `fem_skip_readiness: true` — skip (automation only).
- `fem_allow_needs_repair: true` — allow **NEEDS_REPAIR** after logging.

`demo_mesh: true` skips surface preflight (built-in tet demo). No automatic CalculiX or repair here — gating only.

## Roadmap

- **Compute:** more mesh ops (decimation, extra smoothing, spatial queries).
- **Physics:** evolve `sim/` toward GPU-friendly kernels or proper elasticity while keeping AI/solver separation from **ARCHITECTURE**.
- **FEM:** assistive analysis; swap prompts or add exporters without changing the command boundary.

## License

Prototype code; depends on GLFW, ImGui, glm, nlohmann/json, cpp-httplib under their licenses.
