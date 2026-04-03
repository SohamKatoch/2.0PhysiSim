#pragma once

#include <filesystem>
#include <string>

namespace physisim::fem {

/// Runs `ccx <jobStem>` with working directory `jobDirectory` (where `<jobStem>.inp` lives).
/// Captures combined stdout/stderr into `combinedOut`. `exitCode` is process exit status (0 = success on POSIX).
/// Thread-safe only if each call uses a distinct `jobDirectory`. Suitable for wrapping in `std::async` later.
bool runCalculixProcess(const std::filesystem::path& ccxExecutable, const std::filesystem::path& jobDirectory,
                        const std::string& jobStem, std::string& combinedOut, int& exitCode, std::string& errOut);

} // namespace physisim::fem
