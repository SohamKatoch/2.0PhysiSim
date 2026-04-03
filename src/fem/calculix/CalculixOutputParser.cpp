#include "fem/calculix/CalculixOutputParser.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <glm/vec3.hpp>
#include <sstream>
#include <unordered_map>
#include <vector>

#include "fem/FemTypes.h"
#include "fem/TetrahedralMesh.h"

namespace physisim::fem {

namespace {

std::string toLower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string trim(std::string_view sv) {
    size_t a = 0;
    while (a < sv.size() && std::isspace(static_cast<unsigned char>(sv[a]))) ++a;
    size_t b = sv.size();
    while (b > a && std::isspace(static_cast<unsigned char>(sv[b - 1]))) --b;
    return std::string(sv.substr(a, b - a));
}

float vonMises6(double sxx, double syy, double szz, double sxy, double sxz, double syz) {
    return static_cast<float>(std::sqrt(
        0.5 * ((sxx - syy) * (sxx - syy) + (syy - szz) * (syy - szz) + (szz - sxx) * (szz - sxx)) +
        3.0 * (sxy * sxy + sxz * sxz + syz * syz)));
}

bool tryParseDispLine(const std::string& line, int& nid, float& u1, float& u2, float& u3) {
    std::string t = trim(line);
    if (t.empty() || t[0] == '*') return false;
    if (!std::isdigit(static_cast<unsigned char>(t[0])) && t[0] != '-' && t[0] != '+') return false;
    if (t.find(',') != std::string::npos) std::replace(t.begin(), t.end(), ',', ' ');
    std::istringstream ls(t);
    long n = 0;
    double a = 0, b = 0, c = 0;
    ls >> n >> a >> b >> c;
    if (n <= 0 || ls.fail()) return false;
    nid = static_cast<int>(n);
    u1 = static_cast<float>(a);
    u2 = static_cast<float>(b);
    u3 = static_cast<float>(c);
    return true;
}

bool tryParseStressLine(const std::string& line, int& el, int& ip, float& vm) {
    std::string t = trim(line);
    if (t.empty() || t[0] == '*') return false;
    if (!std::isdigit(static_cast<unsigned char>(t[0])) && t[0] != '-' && t[0] != '+') return false;
    std::string low = toLower(t);
    if (low.find("elem") != std::string::npos || low.find("integr") != std::string::npos ||
        low.find("sxx") != std::string::npos)
        return false;
    if (t.find(',') != std::string::npos) std::replace(t.begin(), t.end(), ',', ' ');
    std::istringstream ls(t);
    long e = 0, p = 0;
    double sxx = 0, syy = 0, szz = 0, sxy = 0, sxz = 0, syz = 0;
    ls >> e >> p >> sxx >> syy >> szz >> sxy >> sxz >> syz;
    if (e <= 0 || p <= 0 || ls.fail()) return false;
    el = static_cast<int>(e);
    ip = static_cast<int>(p);
    vm = vonMises6(sxx, syy, szz, sxy, sxz, syz);
    return true;
}

} // namespace

bool parseCalculixDat(const std::filesystem::path& datPath, const TetrahedralMesh& mesh, FemResult& out,
                      std::string& errOut) {
    out.displacement.assign(mesh.nodes.size(), glm::vec3(0.f));
    out.vonMises.assign(mesh.tets.size(), 0.f);
    out.ok = false;

    std::ifstream in(datPath);
    if (!in) {
        errOut = "Cannot open .dat: " + datPath.string();
        return false;
    }
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::vector<std::string> fileLines;
    {
        std::istringstream ss(content);
        std::string ln;
        while (std::getline(ss, ln)) fileLines.push_back(std::move(ln));
    }

    std::unordered_map<int, glm::vec3> dispByNode1;
    bool inDispSection = false;
    int dispBlankRun = 0;

    for (size_t i = 0; i < fileLines.size(); ++i) {
        const std::string& line = fileLines[i];
        std::string low = toLower(line);

        if (!inDispSection) {
            if (low.find("nodal displacements") != std::string::npos ||
                (low.find("displacements") != std::string::npos && low.find("nodal") != std::string::npos)) {
                inDispSection = true;
                dispBlankRun = 0;
            }
            continue;
        }

        int nid = 0;
        float u1 = 0, u2 = 0, u3 = 0;
        if (tryParseDispLine(line, nid, u1, u2, u3)) {
            dispByNode1[nid] = glm::vec3(u1, u2, u3);
            dispBlankRun = 0;
            continue;
        }
        if (trim(line).empty()) {
            if (++dispBlankRun >= 3 && !dispByNode1.empty()) inDispSection = false;
            continue;
        }
        dispBlankRun = 0;
        if (line.find('*') != std::string::npos) inDispSection = false;
    }

    using StressKey = uint64_t;
    auto sk = [](int el, int ip) -> StressKey {
        return (static_cast<StressKey>(static_cast<uint32_t>(el)) << 32) | static_cast<uint32_t>(ip);
    };
    std::unordered_map<StressKey, float> vmByElIp;

    bool inStressSection = false;
    for (size_t i = 0; i < fileLines.size(); ++i) {
        const std::string& line = fileLines[i];
        std::string low = toLower(line);
        if (!inStressSection) {
            if (low.find("stresses") != std::string::npos && low.find("element") != std::string::npos) {
                inStressSection = true;
            }
            continue;
        }
        int el = 0, ip = 0;
        float vm = 0;
        if (tryParseStressLine(line, el, ip, vm)) {
            vmByElIp[sk(el, ip)] = vm;
            continue;
        }
        if (low.find("total strain energy") != std::string::npos) break;
    }

    for (const auto& kv : dispByNode1) {
        int idx = kv.first - 1;
        if (idx >= 0 && static_cast<size_t>(idx) < out.displacement.size()) out.displacement[static_cast<size_t>(idx)] = kv.second;
    }

    for (size_t e = 0; e < mesh.tets.size(); ++e) {
        int el1 = static_cast<int>(e + 1);
        auto it = vmByElIp.find(sk(el1, 1));
        if (it != vmByElIp.end()) out.vonMises[e] = it->second;
    }

    if (dispByNode1.empty()) {
        errOut = "Could not parse nodal displacements from .dat (expect *NODE PRINT, U)";
        return false;
    }
    out.ok = true;
    return true;
}

} // namespace physisim::fem
