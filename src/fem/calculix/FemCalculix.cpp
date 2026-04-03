#include "fem/calculix/FemCalculix.h"

#include <chrono>
#include <filesystem>
#include <random>

#include "fem/calculix/CalculixInputWriter.h"
#include "fem/calculix/CalculixOutputParser.h"
#include "fem/calculix/CalculixRunner.h"
#include "fem/FemTypes.h"
#include "fem/TetrahedralMesh.h"

namespace physisim::fem {

FemResult runCalculix(const TetrahedralMesh& mesh, const FemInput& input, std::string& errOut) {
    FemResult result;
    errOut.clear();

    namespace fs = std::filesystem;
    bool ownedJobDir = false;
    fs::path jobDir;
    if (input.workDirectory.empty()) {
        auto seed = static_cast<uint64_t>(
            std::chrono::high_resolution_clock::now().time_since_epoch().count());
        std::mt19937_64 rng(seed ^ std::random_device{}());
        jobDir = fs::temp_directory_path() / ("physisim_ccx_" + std::to_string(rng()));
        std::error_code ec;
        fs::create_directories(jobDir, ec);
        if (ec) {
            errOut = "Failed to create temp job directory: " + ec.message();
            return result;
        }
        ownedJobDir = true;
    } else {
        jobDir = fs::path(input.workDirectory);
        std::error_code ec;
        fs::create_directories(jobDir, ec);
        if (ec) {
            errOut = "Failed to create work directory: " + ec.message();
            return result;
        }
    }

    fs::path inpPath = jobDir / (input.jobName + ".inp");
    if (!writeCalculixInp(inpPath, mesh, input, errOut)) {
        if (ownedJobDir && !input.keepWorkFiles) fs::remove_all(jobDir);
        return result;
    }

    fs::path ccxExe(input.ccxExecutable);
#ifdef _WIN32
    if (!fs::exists(ccxExe) && ccxExe.extension().empty()) {
        fs::path withExe = fs::path(ccxExe.string() + ".exe");
        if (fs::exists(withExe)) ccxExe = std::move(withExe);
    }
#endif
    std::string combined;
    int exitCode = -1;
    if (!runCalculixProcess(ccxExe, jobDir, input.jobName, combined, exitCode, errOut)) {
        if (ownedJobDir && !input.keepWorkFiles) fs::remove_all(jobDir);
        return result;
    }
    result.diagnosticLog = std::move(combined);

    if (exitCode != 0) {
        errOut = "CalculiX exited with code " + std::to_string(exitCode);
        if (ownedJobDir && !input.keepWorkFiles) fs::remove_all(jobDir);
        return result;
    }

    fs::path datPath = jobDir / (input.jobName + ".dat");
    std::string parseErr;
    if (!parseCalculixDat(datPath, mesh, result, parseErr)) {
        errOut = parseErr;
        result.diagnosticLog += "\n[parse] " + parseErr;
        if (ownedJobDir && !input.keepWorkFiles) fs::remove_all(jobDir);
        return result;
    }

    if (ownedJobDir && !input.keepWorkFiles) {
        std::error_code ec;
        fs::remove_all(jobDir, ec);
    }

    return result;
}

} // namespace physisim::fem
