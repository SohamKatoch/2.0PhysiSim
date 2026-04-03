# PhysiSim CAD (2.0)

Mesh-first CAD prototype toward a **GPU + AI CAD/FEA** stack: **Vulkan** viewport today, **command-based** geometry, **two-role AI** (design generation vs analysis/optimization signals over Ollama HTTP), and **deterministic + AI-assisted** mesh analysis with **severity-colored** viewport highlighting.

**Target system architecture** (CAD → dual AI → GPU FEA → optional CalculiX validation → export): **[`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md)** — use this as the roadmap; the table there maps **what exists now** vs **planned** (full elasticity solvers and CalculiX integration are not in-tree yet; a **Vulkan compute** Laplacian smoothing path exists as an early GPU hook).

**Developer notes / pitfalls / open questions:** see [`docs/LEARNINGS_AND_PITFALLS.md`](docs/LEARNINGS_AND_PITFALLS.md).

**Windows and `cmake` / `physisim` “not recognized”?** You need a C++ compiler, CMake, and the Vulkan SDK before anything will build — see **[`docs/WINDOWS_SETUP.md`](docs/WINDOWS_SETUP.md)**. If CMake is only inside Visual Studio, run **`.\scripts\Ensure-CMakeOnPath.ps1`** (then open a new terminal) or use **Developer PowerShell for VS**.

### Quick start

1. Install **CMake 3.20+**, a **C++20** toolchain, and the **[Vulkan SDK](https://vulkan.lunarg.com/)** (so `glslc` is on `PATH` at configure time).
2. From the repo root: `cmake -B build -DCMAKE_BUILD_TYPE=Release` then `cmake --build build --config Release`.
3. Run `build/Release/physisim.exe` on Windows (or `build/physisim` on Linux/macOS). Optional: `physisim --ipc-port 17500` for the localhost HTTP API; optional second window: `physisim_client --host 127.0.0.1 --port 17500`.

## Contents

- [Layout (source modules)](#layout)
- [Prerequisites](#prerequisites)
- [Build](#build)
- [Localhost HTTP API](#localhost-http-api)
- [Vulkan remote client](#vulkan-remote-client-physisim_client)
- [Using the app](#how-to-use-the-application-step-by-step)
- [Feature summary](#usage-feature-summary)
- [Command schema](#command-schema)
- [FEM preflight (`analyze_fem`)](#fem-preflight-analyze_fem)
- [Future hooks](#future-hooks)

## Layout

Aligned with **[`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md)** (section 1 front-end, section 2 dual AI, sections 3–5 FEA/CalculiX/workflow).

| Module | Role |
|--------|------|
| `core/` | `Application`, `Scene`, `CommandSystem` — CAD shell + command bus |
| `geometry/` | `Mesh`, STL load, `MeshOperations`, `GeometryEngine` — mesh-first CAD |
| `rendering/` | `VulkanDevice`, `VulkanPipeline`, `VulkanRenderer`, `Camera`, `RayPick` — **graphics** + viewport picking |
| `ai/` | **Model 1:** `AIOrchestrator`, `LLMClient`, `MathClient`, `CommandValidator` — design/commands · **Model 2:** `AnalysisClient` — optimization/prediction *signals* (mesh today; FEA fields TBD) |
| `analysis/` | `GeometryAnalyzer`, `HeuristicAnalyzer`, `DefectDetector`, `GroundTruth`, `FeedbackBuilder`, `AnalysisMemory`, `MeshMetrics`, `MeshHighlightMerge`, `MeshBenchmark` — metrics, highlights, deterministic A-vs-B reports |
| `ui/` | `ImGuiLayer` (ImGui + Vulkan backend) |
| `platform/` | `FileDialog` (Windows STL open/save) |
| `ipc/` | `CommandApiServer` — localhost HTTP API for terminals / custom UIs |
| `client/` | `physisim_client` — standalone **Vulkan + ImGui** app that pulls meshes over HTTP |
| `fea/` | **`GpuLaplacianSmooth`** — Vulkan **compute** Jacobi Laplacian step; `MeshAdjacency` CSR for 1-rings |
| `fem/` | **`FemMeshReadiness`** — deterministic surface **FEM preflight** (`READY` / `NEEDS_REPAIR` / `INVALID`); **CalculiX** adapter (`runCalculix`, input writer, runner, `.dat` parser); `TetrahedralMesh`, `FemCompare` |
| *(planned)* | In-process elasticity / CG / field viz — see [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) |

**Rule:** AI never mutates meshes directly. It produces **validated JSON commands** that the `CommandSystem` forwards to `GeometryEngine`.

**Vulkan vs AI:** Ollama is reached over **HTTP** (`127.0.0.1:11434`). The **Vulkan** path only draws whatever mesh is **active** in the scene. After a successful `create` command, the new model id becomes **active** so the viewport updates; transforms apply to the active model (or to `target` if set). If the Log shows `[ok]` but the view is unchanged, select the right model under **Viewport & STL → Models in scene**.

## Prerequisites

- [CMake](https://cmake.org/) 3.20+
- [Vulkan SDK](https://vulkan.lunarg.com/) (provides `glslc` and validation layers)
- C++20 compiler (MSVC 2022, Clang, or GCC)
- Git (for CMake `FetchContent`)

Optional (for AI panels):

- [Ollama](https://ollama.com/) with models such as `llama3.1:8b` and `qwen2.5-math:7b` (names must match `LLMClient` / `MathClient` defaults or your edits)

## Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

- **Windows:** run `build/Release/physisim.exe` (or your generator’s output folder). Ensure **Vulkan SDK** `Bin` is on `PATH` so `glslc` is found at configure time.
- **Visual Studio 18 Build Tools + `Visual Studio 17 2022` generator:** if configure fails with **MSB8020** / missing **v143**, see **[`docs/WINDOWS_SETUP.md`](docs/WINDOWS_SETUP.md)** §3 (install v143, use VS 18 generator with CMake 4.2+, or Ninja).
- **Linux/macOS:** run `build/physisim`.

SPIR-V shaders are compiled at configure/build time into `build/.../shaders/`. The target defines `PHYSISIM_SHADER_DIR` to that folder.

### Windows: `physisim` is not recognized (not on PATH)

CMake puts the `.exe` under your **build folder**; it is **not** added to `PATH` automatically. From PowerShell, either:

**Option A — full path** (adjust if your clone is elsewhere):

```powershell
cd C:\Users\soham\2.0PhysiSim
.\build\Release\physisim.exe --ipc-port 17500
.\build\Release\physisim_client.exe --port 17500
```

If you built **Debug**, use `.\build\Debug\physisim.exe` instead.

**Option B — helper script** (from the **repo root** `2.0PhysiSim`):

```powershell
cd C:\Users\soham\2.0PhysiSim
.\scripts\Run-PhysiSim.ps1 --ipc-port 17500
.\scripts\Run-PhysiSim.ps1 -Client --port 17500
```

**Option C — install to PATH** (optional): copy or symlink the built `.exe` into a folder that is already on your user `PATH`, or add `...\2.0PhysiSim\build\Release` to PATH in *Settings → Environment variables*.

If `build\Release\physisim.exe` does not exist, build first (Visual Studio or `cmake --build` as above).

### Localhost HTTP API

Run the app with a port (**127.0.0.1 only**):

```bash
physisim --ipc-port 17500
```

Or set `PHYSISIM_IPC_PORT=17500`. Then use **your own terminal**, scripts, or any HTTP client:

| | |
|--|--|
| Health | `curl -s http://127.0.0.1:17500/v1/health` |
| Scene | `curl -s http://127.0.0.1:17500/v1/scene` |
| Load STL | `curl -s -X POST http://127.0.0.1:17500/v1/stl/load -H "Content-Type: application/json" -d "{\"path\":\"C:/models/part.stl\"}"` |
| Export STL | `curl -s -X POST http://127.0.0.1:17500/v1/stl/export -H "Content-Type: application/json" -d "{\"path\":\"C:/models/out.stl\"}"` |
| Command | `curl -s -X POST http://127.0.0.1:17500/v1/command -H "Content-Type: application/json" -d "{\"action\":\"transform\",\"operations\":[],\"parameters\":{\"translate\":[0,0.1,0]}}"` |
| Active model | `curl -s -X POST http://127.0.0.1:17500/v1/scene/active -H "Content-Type: application/json" -d "{\"id\":\"demo\"}"` |
| Mesh (binary) | `curl -s -o mesh.stl http://127.0.0.1:17500/v1/mesh/stl` |

PowerShell: `Invoke-RestMethod http://127.0.0.1:17500/v1/scene`

Mutations are applied on the **next frame** (Vulkan-safe). With IPC enabled, the **Terminal / external UI** ImGui panel lists these routes. `GET /v1/mesh/stl` returns the **active** mesh as `application/octet-stream`. `physisim --help` shows flags.

### Vulkan remote client (`physisim_client`)

Second executable: same shaders and mesh pipeline as the editor, but it only talks to a running server.

1. Terminal A: `physisim --ipc-port 17500`
2. Terminal B: `physisim_client --host 127.0.0.1 --port 17500`

In the client window, use **Pull mesh (Vulkan)** to `GET /v1/mesh/stl`, parse the STL in RAM, upload to **this process’s** Vulkan device, and orbit with **right-drag** / **wheel**. You can change host/port in ImGui and drive the server from your own scripts while watching the mesh in the client viewport.

Build produces `physisim_client` next to `physisim` (same CMake build directory).

---

## How to use the application (step by step)

1. **Start** `physisim`. You should see a **dark 3D viewport** (Vulkan) and several **ImGui panels** (floating windows; resize/stack as you like).

2. **Move the camera (3D view)**  
   - **Orbit:** hold **right mouse button** and drag.  
   - **Zoom:** **mouse wheel**, or **`=`** / **`-`** on the keyboard.  
   - **Exit:** **Esc** closes the app.

3. **Interactive mesh insight (optional)**  
   In **Viewport & STL**, enable **Interactive mesh insight (hover / pick)**. Hover a triangle for a tooltip (face id, severity band, Laplacian proxy, thickness proxy; hold **Ctrl** for normal and boundary info). **Left-click** opens the **Face inspector**; **Shift+click** is reserved for a future apply-to-region workflow.

4. **Load an STL (local files, millimeters)**  
   Coordinates are kept as in the file (manual workflow). For **mm STLs**, leave **Analysis → mm per 1 mesh unit** at **1.0**.  
   - **Drag and drop** a `.stl` file **anywhere on the window**.  
   - **Windows:** **Viewport & STL (local, mm)** → **Browse for STL...**  
   - Type a full path → **Load from path**.

5. **Export the active mesh**  
   Same panel: set **Export path** (or **Browse save location...** on Windows), then **Export binary STL**. Output is **binary STL** in the same units as the in-memory mesh (mm if you started from mm). Scene **transform is not baked** into export.

6. **GPU smoothing (experimental)**  
   Still under **Viewport & STL**, section **GPU FEA (experimental)**: adjust **Smooth blend (lambda)** and click **Run GPU Laplacian (1 step)** for one Jacobi-style smoothing step on the **active** mesh (requires successful Vulkan compute init; if init failed, the button is disabled and the **Log** shows `[fea]` errors).

7. **Switch models**  
   Under **Models in scene**, click a name to make it **Active**. The viewport draws the active mesh only.

8. **Commands & AI**  
   - **Command & AI:** **Interpret + execute (LLM)** for natural language (needs Ollama) or paste JSON and **Execute JSON**.  
   - **Log:** shows STL load errors, command results, and AI errors.

9. **Analysis**  
   - **mm per 1 mesh unit** (manual): use **1.0** for standard mm STLs; change only if your CAD uses other length units.  
   - **Material density (kg/m³)** feeds mass-related metrics (set **0** to omit mass).  
   - Toggle AI / feedback loop / RAG / persist as needed.  
   - **Run analysis** updates the JSON text and tints flagged triangles in the viewport (severity-colored).

10. **Benchmark (original vs active)**  
    In **Analysis**, set a **baseline** STL path (original reference mesh) and click **Compare baseline vs active** for a deterministic JSON report (volume, mass, center-of-gravity shift, Laplacian proxy delta). Use after GPU smoothing or other edits to quantify change vs the baseline file.

---

## Usage (feature summary)

- **Viewport:** RMB orbit, wheel / `=` / `-` zoom.  
- **Mesh insight:** optional hover tooltips and **Face inspector** on left-click (see step 3 above).  
- **STL:** local paths only — drag-drop, **Browse** (Windows), or path + **Load**; **mm** workflow with manual **mm per 1 mesh unit** = 1.0.  
- **Export:** **Export binary STL** (Windows save dialog or typed path); active mesh only, no baked transform.  
- **GPU Laplacian:** one Vulkan compute smoothing step on the active mesh (lambda slider + **Run GPU Laplacian (1 step)**).  
- **Execute JSON:** paste a command matching the schema and click **Execute JSON**.  
- **Natural language:** **Interpret + execute (LLM)** requires Ollama; uses LLaMA for JSON command generation and Qwen2.5-Math for `math:...` string parameters.  
- **Analysis:** deterministic `ground_truth`, optional AI overlay, `feedback`, optional refinement and RAG; **material density** for mass metrics (see learnings doc).  
- **Benchmark:** compare a baseline STL file to the active mesh for deterministic deltas (Analysis panel).

### Hybrid AI phases (implemented)

| Phase | What it does |
|-------|----------------|
| **1 — Context loop** | After the first AI pass, `FeedbackBuilder` compares predictions to `ground_truth`. `AnalysisClient::refineWithFeedback` returns interpretive JSON only — it **cannot** override engine numbers. |
| **2 — RAG memory** | `AnalysisMemory` stores JSON snapshots + a deterministic **fingerprint**; retrieval uses L1 distance on features for prompt context. |
| **3 — Fine-tuning** | Not in-repo; your LoRA/FT jobs stay offline. The runtime contract stays: **engine = reality**, **AI = interpretation + suggestions**. |

Deterministic checks tint triangles **green (lower concern) → red (higher concern)** via `defectHighlight` (`mesh.frag`): **thin slivers** (continuous by aspect), **non-manifold edges** (severity 5), **boundary edges** (severity 2), and **inconsistent normals** when the mesh-wide threshold trips (severity 3, stronger red when more inverted).

## Command schema

```json
{
  "action": "create | modify | boolean | transform | analyze | analyze_fem",
  "operations": [],
  "parameters": {},
  "target": "optional_model_id"
}
```

Implemented today: `create` (cube, optional `attach_fem_demo_volume`), `transform` (translate / uniform scale), `analyze_fem` (CalculiX — see below).

## FEM preflight (`analyze_fem`)

Before running **`analyze_fem`** on a real model (when **`demo_mesh`** is not `true`), the app runs a **deterministic surface check** on the target model’s triangle **`Mesh`**: `fem::evaluateFEMReadiness` / `checkFEMReadiness`.

| Classification | Meaning (surface) | Default `analyze_fem` behavior |
|----------------|-------------------|--------------------------------|
| **INVALID** | Non-manifold edges, degenerate triangles, or detected **self-intersection** (non-adjacent triangles) | **Blocked** — Log shows `[fem] preflight (blocked): …` plus JSON |
| **NEEDS_REPAIR** | Open boundary (not watertight), poor global edge-length ratio, high triangle aspect ratio, or self-intersection scan incomplete (very heavy meshes) | **Blocked** unless **`"fem_allow_needs_repair": true`** — then warning + proceed |
| **READY** | Watertight, manifold, acceptable triangle quality, no intersection hits in the scan | **Proceed** |

Structured log payload (from `FemReadinessReport::toJson()`):

- **`status`**: `"ok"` | `"warning"` | `"blocked"` (maps to readiness)
- **`readiness`**: `"READY"` | `"NEEDS_REPAIR"` | `"INVALID"`
- **`issues`**: `[{ "code", "detail", "severity_class" }]`
- **`suggestions`**: human-readable next steps

Escape hatches (JSON parameters on `analyze_fem`):

- **`fem_skip_readiness`: true** — skip preflight (automation only).
- **`fem_allow_needs_repair`: true** — allow **NEEDS_REPAIR** through after logging the report.

**`demo_mesh`: true** skips surface preflight (built-in tet demo). CalculiX and mesh repair are **not** applied automatically in this step — validation and gating only.

## Future hooks

- **Compute:** additional mesh ops (decimation, more smoothing passes, spatial queries) in dedicated compute pipelines; Laplacian today is the first in-app compute path.
- **FEM / solvers:** keep analysis assistive; swap `AnalysisClient` prompts or add deterministic FEM exporters without touching the command boundary.

## License

Prototype code; depend on GLFW, ImGui, glm, nlohmann/json, cpp-httplib under their respective licenses.
