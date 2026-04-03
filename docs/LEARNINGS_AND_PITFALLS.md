# Learnings, pitfalls, and open questions

This file captures what we learned building PhysiSim CAD, mistakes to avoid, and items where **your product choices** matter.

For the **target CAD + dual-AI + GPU FEA + CalculiX** system shape and module mapping, see **[`ARCHITECTURE.md`](ARCHITECTURE.md)**.

---

## Architectural lessons

### 1. Engine truth vs AI narrative

**Learning:** The mesh analysis pipeline must treat deterministic geometry checks as **authoritative**. LLM output is easy to “sound right” with wrong numbers.

**Pitfall:** Letting the model restate measurements (edge lengths, counts) without tying them to `ground_truth` JSON invites hallucinated “1.2 mm” values.

**Mitigation in code:** `ground_truth` is built only from `HeuristicAnalyzer` + `GeometryAnalyzer`. Refinement prompts explicitly say not to override `engine_results`.

### 2. ImGui + Vulkan version coupling

**Learning:** Dear ImGui’s Vulkan backend API changed around 1.91 (`ImGui_ImplVulkan_InitInfo` carries `RenderPass`; `ImGui_ImplVulkan_LoadFunctions` lost the API-version argument).

**Pitfall:** Copying an older tutorial causes compile errors or subtle init failure.

**Mitigation:** Pin ImGui version in CMake (we use v1.91.6) and match the header you read on GitHub.

### 3. `IMGUI_IMPL_VULKAN_NO_PROTOTYPES`

**Learning:** When this is defined project-wide, **all** translation units that include Vulkan must be consistent; the loader must run before ImGui Vulkan init.

**Pitfall:** Forgetting `ImGui_ImplVulkan_LoadFunctions` → crashes or unresolved symbols at runtime.

### 4. Vertex layout vs shader

**Learning:** Interleaved vertex attributes must match `VkVertexInputAttributeDescription` offsets exactly (we use 32-byte stride: `vec3` + pad + `vec3` + `float` defect weight).

**Pitfall:** Packing `defect` at the wrong float index makes highlights silently wrong.

### 5. Namespace lookup (`core::Command` inside `physisim::ai`)

**Learning:** Inside `namespace physisim::ai`, `core::Command` resolves to `physisim::core::Command` because `core` is a child of `physisim`.

**Pitfall:** Assuming it means `::core::Command` (global) causes confusion when reading code.

### 6. RAG without embeddings

**Learning:** Fingerprint + L1 distance is **cheap and deterministic** but only correlates with “similar topology/heuristics,” not semantic similarity.

**Pitfall:** Treating retrieved cases as ground truth — they are **pattern hints** only (labeled as such in the prompt).

### 7. cpp-httplib + optional OpenSSL

**Learning:** Letting httplib’s CMake search OpenSSL can fail or slow configure on machines without dev libraries.

**Pitfall:** We set `HTTPLIB_USE_OPENSSL_IF_AVAILABLE OFF` for predictable builds (HTTP to localhost Ollama only).

---

## Mistakes we made (or almost made) — avoid repeating

| Mistake | Why it hurts | Fix |
|--------|----------------|-----|
| Wrong ImGui `EndFrame` / frame order | Broken or blank UI | Use `NewFrame` → widgets → `ImGui::Render()` inside your Vulkan render path (see `ImGuiLayer::render`). |
| Feeding refinement errors into the same `lastError` as the first AI call | Loses the fact that primary analysis succeeded | Use a separate string for refinement (we use `ai_refinement_error`). |
| Broad substring match for “manifold” in AI feedback | False positives when AI says “watertight manifold” | Restrict non-manifold detection to “non-manifold” / “nonmanifold” substrings. |
| `destroySwapchain` helper left unused | Dead code / confusion | Remove or wire into resize; we inlined resize logic. |
| Assuming CMake/`glslc` on PATH in CI or agent shells | Configure fails | Document Vulkan SDK `Bin` in PATH; `find_program(GLSLC ... HINTS ...)`. |

---

## Resolved workflow choices (from product owner)

- **STL units:** **Millimeters**, **manual** — vertices stay as in the file; **mm per 1 mesh unit** defaults to **1.0** for analysis display (`ground_truth` mm fields). No automatic unit sniffing from STL header.
- **Import:** **Local files only** (path, drag-drop, Windows open dialog).
- **Export:** **Binary STL** of the **active** mesh via **Export binary STL** / save dialog (Windows). Transforms on the scene node are **not** baked into vertices yet.

## Open questions **for you** (optional next steps)

1. **Default camera framing:** After STL load, **auto-fit** camera to bounding box?
2. **Multi-document UI:** Tabs vs one scene per window?
3. **Export extras:** **OBJ**, **screenshot**, or **bake scene transform** into exported STL?

---

## Terminal / external UI (HTTP IPC)

**Learning:** Vulkan and GLFW must stay on the **main thread**. A second thread cannot safely upload GPU buffers or mutate the scene under the renderer without synchronization.

**Mitigation:** `CommandApiServer` (cpp-httplib) accepts JSON on `127.0.0.1` and **enqueues** `PendingOp` items; the main loop **drains the queue** each frame before rendering. `GET /v1/scene` uses a mutex-protected JSON snapshot updated every frame.

**Pitfall:** Calling `stop()` on the HTTP server **before** the worker thread has created `httplib::Server` and set `server_` caused a **join deadlock** in early designs. **Fix:** a `std::promise` signals “server pointer is live” before `listen()` blocks, and `start()` waits so `stop()` always has a valid target.

**Security:** The server binds **loopback only** (`127.0.0.1`). Do not expose this API on `0.0.0.0` without authentication.

**`physisim_client`:** A separate **Vulkan** process that downloads `GET /v1/mesh/stl` and renders locally. Two Vulkan apps = two GPU pipelines; the client does not share a device with the server. Large meshes duplicate VRAM briefly during download/parse.

## Operational notes

- **Esc to quit** is ignored while ImGui wants the keyboard (e.g. typing in a text field), so you do not accidentally close while editing a path.
- **Ollama** must be running for NL commands and AI analysis; failures are logged in the **Log** panel, not always as modal dialogs.
- **`analysis_memory/`** grows if **persist case** is on; it is gitignored by default.
- **Large STLs:** Current upload path uses **host-visible** Vulkan buffers — fine for prototypes, not for huge meshes (staging + device-local memory is the next step).

---

## Changelog of this doc

- Initial version: hybrid AI phases, Vulkan/ImGui pitfalls, open product questions.
