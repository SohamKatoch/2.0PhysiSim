#pragma once

#include <optional>
#include <string>

struct GLFWwindow;

namespace physisim::platform {

/// Native “open file” for `.stl`. Windows: `GetOpenFileNameW` with GLFW parent window.
/// Other platforms: returns `std::nullopt` (use path field + drag-and-drop).
std::optional<std::string> openStlFileDialog(GLFWwindow* parentWindow);

/// Native “save as” for `.stl` (binary export). Windows: `GetSaveFileNameW`.
std::optional<std::string> saveStlFileDialog(GLFWwindow* parentWindow);

} // namespace physisim::platform
