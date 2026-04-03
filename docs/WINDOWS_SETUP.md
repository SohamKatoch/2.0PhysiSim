# Windows: first-time setup (you need this to build)

PhysiSim is **C++20 + CMake + Vulkan**. If `cmake` and `physisim_client` are “not recognized,” Windows does not yet have the build tools (or they are not on your `PATH`).

## 1. Install a C++ compiler (pick one path)

### Option A — Visual Studio Community (recommended, includes an IDE)

1. Download [Visual Studio Community 2022](https://visualstudio.microsoft.com/downloads/).
2. Run **Visual Studio Installer** → **Modify** (or install fresh).
3. Enable workload: **Desktop development with C++**.
4. On the right, under **Installation details**, optional but useful:
   - **CMake tools for Windows** (so `cmake` works from normal PowerShell after install).
   - **Windows 11 SDK** (or Windows 10 SDK) if not already checked.
5. Install / modify and wait until it finishes.

### Option B — Build Tools only (no full IDE, smaller)

1. Download [Build Tools for Visual Studio 2022](https://visualstudio.microsoft.com/downloads/#build-tools-for-visual-studio-2022).
2. Run the installer → **Desktop development with C++** (same workload as above).
3. Add **CMake tools for Windows** if offered.

## 2. Install the Vulkan SDK

1. Download from [LunarG Vulkan SDK](https://vulkan.lunarg.com/sdk/home#windows).
2. Install with defaults.
3. **Important for CMake:** the installer can add Vulkan to your user `PATH`. If configure fails to find `glslc`, add manually, for example:
   - `C:\VulkanSDK\<version>\Bin`
4. Restart PowerShell after changing `PATH`.

### CMake: `cmake` not found in normal PowerShell

Visual Studio often installs CMake **inside** the product tree, not on your global `PATH`. **Developer PowerShell for VS** prepends that folder; a plain `powershell.exe` session does not, so `cmake` appears “not installed.”

Typical location for **VS 18 Build Tools**:

`C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin`

**Permanent fix:** *Settings → System → About → Advanced system settings → Environment Variables → Path (User) → New* → paste that folder (no `cmake.exe` at the end).

**Or** from the repo root:

```powershell
.\scripts\Ensure-CMakeOnPath.ps1
```

Then **close and reopen** the terminal (user `PATH` is read at process start). Confirm with `cmake --version` (expect **4.2+** for the **Visual Studio 18 2026** generator in §3).

## 3. Visual Studio **18** (2025/2026 Build Tools) and error **MSB8020** / **v143**

If configure fails with **MSB8020** (“build tools for Visual Studio 2022 … **v143** cannot be found”), CMake is using the **VS 2022** generator, which expects the **v143** toolset, while you only have **Visual Studio 18** Build Tools (newer MSVC). Pick **one** fix:

### Fix A — Install the **v143** toolset (keeps README commands)

1. Open **Visual Studio Installer** → **Modify** your Build Tools.
2. **Individual components** tab → search **`v143`**.
3. Enable **MSVC v143 - VS 2022 C++ x64/x86 build tools (Latest)** (wording may vary).
4. Install, then **delete the old `build` folder** and reconfigure:

```powershell
cd C:\Users\soham\2.0PhysiSim
Remove-Item -Recurse -Force build -ErrorAction SilentlyContinue
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

### Fix B — Use a generator that matches **VS 18** (newer CMake)

Requires **CMake 4.2+** (`cmake --version`). Then:

```powershell
Remove-Item -Recurse -Force build -ErrorAction SilentlyContinue
cmake -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Release
```

If CMake says the generator is unknown, upgrade CMake from [cmake.org/download](https://cmake.org/download/) or use **Fix A** / **Fix C**.

### Fix C — **Ninja** + **Native Tools** (no Visual Studio solution)

1. Install Ninja: `winget install Ninja-build.Ninja` (or [ninja releases](https://github.com/ninja-build/ninja/releases)).
2. Open **x64 Native Tools Command Prompt** (or Developer PowerShell) for **your** VS / Build Tools install.
3. Run:

```powershell
cd C:\Users\soham\2.0PhysiSim
Remove-Item -Recurse -Force build-ninja -ErrorAction SilentlyContinue
cmake -B build-ninja -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-ninja
```

Executables are under `build-ninja\` (single-config: **no** `Release` subfolder).

Optional: `cmake --preset ninja` then `cmake --build --preset ninja-release` using [`CMakePresets.json`](../CMakePresets.json) at repo root.

---

## 4. Open the right terminal

After installing VS, **normal** `powershell.exe` often still does **not** see `cmake` or the MSVC compiler until:

- You install **CMake tools for Windows** (step 1), **or**
- You use a VS developer shell:

**Start Menu** → **Visual Studio 2022** → **Developer PowerShell for VS 2022**  
(or **x64 Native Tools Command Prompt for VS 2022**)

Then (after **Fix A/B/C** above if you hit MSB8020):

```powershell
cd C:\Users\soham\2.0PhysiSim
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

## 5. Run the executables (still not on PATH)

The `.exe` files are under the build folder, not global PATH:

```powershell
.\build\Release\physisim.exe --ipc-port 17500
.\build\Release\physisim_client.exe --port 17500
```

Or from repo root:

```powershell
.\scripts\Run-PhysiSim.ps1 --ipc-port 17500
.\scripts\Run-PhysiSim.ps1 -Client --port 17500
```

## 6. Optional: CMake without Visual Studio CMake component

If you prefer standalone CMake:

- Install from [cmake.org/download](https://cmake.org/download/) and tick **Add CMake to PATH**,  
  **or** use winget (run PowerShell **as Administrator** if winget asks):

```powershell
winget install Kitware.CMake
```

You still need **MSVC** (or Clang on Windows with a supported generator) from Option A or B above.

## Quick checks

In **Developer PowerShell for VS 2022**:

```powershell
where.exe cmake
where.exe cl
```

If `cl` is missing, the C++ workload is not installed correctly.

---

**Bottom line:** There is no way to run `physisim_client` without building it first, and building requires a C++ toolchain + CMake + Vulkan SDK. After that, use the full path to `.\build\Release\physisim_client.exe` or the helper script.
