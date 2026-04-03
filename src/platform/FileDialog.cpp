#include "platform/FileDialog.h"

#include <GLFW/glfw3.h>

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <commdlg.h>
#endif

#include <vector>

namespace physisim::platform {

#ifdef _WIN32

static std::string wideToUtf8(const wchar_t* wstr) {
    if (!wstr || !*wstr) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::vector<char> buf(static_cast<size_t>(n));
    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, buf.data(), n, nullptr, nullptr);
    return std::string(buf.data());
}

std::optional<std::string> openStlFileDialog(GLFWwindow* parentWindow) {
    HWND owner = parentWindow ? glfwGetWin32Window(parentWindow) : nullptr;

    wchar_t pathBuf[MAX_PATH + 1] = {0};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFile = pathBuf;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = L"STL meshes\0*.stl\0All files\0*.*\0\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_EXPLORER | OFN_NOCHANGEDIR;

    if (!GetOpenFileNameW(&ofn)) return std::nullopt;
    std::string utf8 = wideToUtf8(pathBuf);
    if (utf8.empty()) return std::nullopt;
    return utf8;
}

std::optional<std::string> saveStlFileDialog(GLFWwindow* parentWindow) {
    HWND owner = parentWindow ? glfwGetWin32Window(parentWindow) : nullptr;

    wchar_t pathBuf[MAX_PATH + 1] = {0};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFile = pathBuf;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = L"STL meshes\0*.stl\0All files\0*.*\0\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrDefExt = L"stl";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_EXPLORER | OFN_NOCHANGEDIR | OFN_HIDEREADONLY;

    if (!GetSaveFileNameW(&ofn)) return std::nullopt;
    std::string utf8 = wideToUtf8(pathBuf);
    if (utf8.empty()) return std::nullopt;
    return utf8;
}

#else

std::optional<std::string> openStlFileDialog(GLFWwindow*) { return std::nullopt; }

std::optional<std::string> saveStlFileDialog(GLFWwindow*) { return std::nullopt; }

#endif

} // namespace physisim::platform
