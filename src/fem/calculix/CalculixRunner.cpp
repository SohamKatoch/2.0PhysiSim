#include "fem/calculix/CalculixRunner.h"

#include <array>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <cstdlib>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace physisim::fem {

namespace {

#ifdef _WIN32

std::wstring utf8ToWide(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    if (n <= 0) return L"";
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

bool runProcessWin(const std::filesystem::path& exe, const std::filesystem::path& cwd, const std::string& argStem,
                   std::string& combinedOut, int& exitCode, std::string& errOut) {
    std::wstring wexe = utf8ToWide(exe.string());
    std::wstring wcwd = utf8ToWide(cwd.string());
    std::wstring wstem = utf8ToWide(argStem);
    std::wstring cmdLine = L"\"" + wexe + L"\" " + wstem;

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE rd = nullptr, wr = nullptr;
    if (!CreatePipe(&rd, &wr, &sa, 0)) {
        errOut = "CreatePipe failed";
        return false;
    }
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = wr;
    si.hStdError = wr;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> mutableCmd(cmdLine.begin(), cmdLine.end());
    mutableCmd.push_back(L'\0');

    BOOL ok = CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, TRUE,
                             CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT, nullptr,
                             wcwd.empty() ? nullptr : wcwd.c_str(), &si, &pi);
    CloseHandle(wr);
    if (!ok) {
        CloseHandle(rd);
        errOut = "CreateProcessW failed for CalculiX";
        return false;
    }

    char buf[4096];
    DWORD nread = 0;
    while (ReadFile(rd, buf, sizeof(buf), &nread, nullptr) && nread > 0)
        combinedOut.append(buf, nread);
    CloseHandle(rd);

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD ec = 1;
    GetExitCodeProcess(pi.hProcess, &ec);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    exitCode = static_cast<int>(ec);
    return true;
}

#else

bool runProcessPosix(const std::filesystem::path& exe, const std::filesystem::path& cwd, const std::string& argStem,
                     std::string& combinedOut, int& exitCode, std::string& errOut) {
    int pipefd[2];
    if (pipe(pipefd) != 0) {
        errOut = "pipe() failed";
        return false;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        errOut = "fork() failed";
        return false;
    }

    if (pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);

        std::string c = cwd.string();
        if (!c.empty() && chdir(c.c_str()) != 0)
            _exit(126);

        execl(exe.c_str(), exe.c_str(), argStem.c_str(), nullptr);
        _exit(127);
    }

    close(pipefd[1]);
    std::array<char, 4096> buf{};
    ssize_t n;
    while ((n = read(pipefd[0], buf.data(), buf.size())) > 0) combinedOut.append(buf.data(), static_cast<size_t>(n));
    close(pipefd[0]);

    int st = 0;
    waitpid(pid, &st, 0);
    if (WIFEXITED(st))
        exitCode = WEXITSTATUS(st);
    else
        exitCode = -1;
    return true;
}

#endif

} // namespace

bool runCalculixProcess(const std::filesystem::path& ccxExecutable, const std::filesystem::path& jobDirectory,
                      const std::string& jobStem, std::string& combinedOut, int& exitCode, std::string& errOut) {
    combinedOut.clear();
    exitCode = -1;

    if (jobStem.empty()) {
        errOut = "jobStem is empty";
        return false;
    }
    std::filesystem::path inp = jobDirectory / (jobStem + ".inp");
    if (!std::filesystem::exists(inp)) {
        errOut = "Missing input file: " + inp.string();
        return false;
    }
    if (!std::filesystem::exists(ccxExecutable)) {
        errOut = "ccx executable not found: " + ccxExecutable.string();
        return false;
    }

#ifdef _WIN32
    if (!runProcessWin(ccxExecutable, jobDirectory, jobStem, combinedOut, exitCode, errOut)) return false;
#else
    if (!runProcessPosix(ccxExecutable, jobDirectory, jobStem, combinedOut, exitCode, errOut)) return false;
#endif
    return true;
}

} // namespace physisim::fem
