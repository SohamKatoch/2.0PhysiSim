# GPU + AI CAD / FEA — target architecture

This document is the **north star** for PhysiSim: how the full system is intended to fit together. The repo may not implement every block yet; see **Implementation map** below.

---

## 1. Front-end CAD / mesh system

- Custom CAD engine (**mesh-first**).
- Real-time **parametric** geometry (evolve from today’s command + mesh ops).
- **B-rep** optional / mainly for export.
- Handles **user input** and **visualization** (viewport, panels, IPC).

**In-repo today:** `core/`, `geometry/`, `rendering/` (Vulkan **graphics**), `ui/`, `ipc/`, `client/`. Parametric/B-rep export are partial or future.

---

## 2. AI models (two roles)

| Role | Purpose | Intended behavior |
|------|---------|-------------------|
| **AI model 1 — geometry / design** | Suggest part shapes, feature placement, design intent → commands | Natural language / structured prompts → validated **JSON commands** → `GeometryEngine` |
| **AI model 2 — optimization / prediction** | Predict stress/strain (later), guide iterative design | Consumes **solver + heuristic summaries**, returns suggestions / rankings; must not override **ground truth** from the engine |

**In-repo today:**

- **Model 1 path:** `ai/AIOrchestrator`, `LLMClient`, `MathClient`, `CommandValidator` → `core/CommandSystem` → `geometry/GeometryEngine`.
- **Model 2 path (interpretation, not FEA yet):** `ai/AnalysisClient`, `analysis/DefectDetector`, `GroundTruth`, `FeedbackBuilder`, `AnalysisMemory` — mesh **design/manufacturing** signals, structured for future hook-up to **FEA fields** once the GPU solver exists.

**Contract:** AI never writes mesh memory directly; the **command system** and **analysis pipeline** own mutations and truth tables.

---

## 3. GPU FEA engine (core — target)

- **Vulkan compute** (not only rasterization).
- Responsibilities:
  - Real-time or fast **mesh prep** where needed.
  - **Iterative solvers** (e.g. conjugate gradient, Jacobi) on GPU.
  - **Visualization** of stress, displacement, thermal fields (same or linked render pass).
- Delivers **interactive feedback** to the user and to **AI model 2**.

**In-repo today:** Vulkan is used for **mesh drawing** (`rendering/`, `shaders/mesh.*`). A first **Vulkan compute** path lives under **`fea/`** (`GpuLaplacianSmooth`, `shaders/fea/laplacian_smooth.comp`): one **Jacobi Laplacian** smoothing step on the GPU (PoC; results read back to CPU). Full **elasticity / CG solvers** and **field visualization** are still to add.

**Next steps in-tree:** device-local SSBOs + ping-pong, stress/displacement buffers, render pass or fragment mapping for false-color fields; optional `shaders/fea/` for more kernels.

---

## 4. CalculiX backend (validation — target)

- Optional **CPU** reference solver.
- Use cases: high-accuracy checks, edge cases, material model validation.
- Runs **asynchronously**; **must not block** the interactive GPU path.
- Feeds **ground truth** or regression baselines for **AI training** and **solver verification**.

**In-repo today:** not integrated. Future: job queue + results diff vs GPU solution.

---

## 5. Workflow summary (target loop)

1. User designs and/or **AI model 1** suggests geometry (commands applied through the CAD core).
2. **GPU FEA engine** computes fields in near real time.
3. **AI model 2** uses those results (+ summaries) to propose design adjustments (again via validated commands or parameters).
4. **CalculiX** optionally runs offline/background for validation.
5. **Export** to CAD/CAE formats (mesh/B-rep as appropriate).

**In-repo today:** steps **1** (partial), **2–4** (not yet), **5** (STL binary; broader export TBD).

---

## Implementation map (quick reference)

| Architecture block | Directory / component | Status |
|--------------------|----------------------|--------|
| §1 CAD / mesh / UI | `core/`, `geometry/`, `ui/`, `platform/`, `ipc/`, `client/` | Active |
| §1 Visualization | `rendering/`, `shaders/mesh.*` | Active (graphics) |
| §2 AI model 1 | `ai/AIOrchestrator`, `LLMClient`, `MathClient` | Active (HTTP/Ollama) |
| §2 AI model 2 | `ai/AnalysisClient`, `analysis/*` | Active (mesh analysis); FEA coupling **TBD** |
| §3 GPU FEA | `fea/GpuLaplacianSmooth`, `shaders/fea/laplacian_smooth.comp` | **Started** (compute Laplacian PoC); full solver + field viz **TBD** |
| §4 CalculiX | — | **Planned** (async validation) |
| §5 Export | `geometry/Mesh` STL | Partial |

---

## Design rules (carry forward)

1. **Mesh-first CAD** remains the runtime source of truth until B-rep is explicitly added.
2. **Two AI roles** stay separated: generation/commands vs optimization/prediction over **measured** data.
3. **GPU solver** is the interactive path; **CalculiX** is verification, not the main thread.
4. **Engine ground truth** (geometry metrics, later FEA norms) is not overridden by model outputs — AI suggests; validators and solvers decide.

When adding FEA or CalculiX, update this file and the README **Layout** table so newcomers see the same story.
