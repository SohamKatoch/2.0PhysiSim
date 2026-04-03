#include "fem/calculix/CalculixInputWriter.h"

#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>

#include "fem/FemTypes.h"
#include "fem/TetrahedralMesh.h"

namespace physisim::fem {

namespace {

std::string fp6(double v) {
    std::ostringstream os;
    os << std::scientific << std::setprecision(6) << v;
    return os.str();
}

} // namespace

bool writeCalculixInp(const std::filesystem::path& inpPath, const TetrahedralMesh& mesh, const FemInput& input,
                      std::string& errOut) {
    if (mesh.empty()) {
        errOut = "TetrahedralMesh is empty";
        return false;
    }
    for (const auto& t : mesh.tets) {
        for (uint32_t k : t) {
            if (k >= mesh.nodes.size()) {
                errOut = "Tet references out-of-range node index";
                return false;
            }
        }
    }

    std::set<uint32_t> fixed(input.fixedNodes.begin(), input.fixedNodes.end());
    for (uint32_t n : fixed) {
        if (n >= mesh.nodes.size()) {
            errOut = "fixedNodes references out-of-range node index";
            return false;
        }
    }
    if (input.loadNode && *input.loadNode >= mesh.nodes.size()) {
        errOut = "loadNode out of range";
        return false;
    }
    if (fixed.empty() && !input.loadNode && !input.enableGravity) {
        errOut = "FemInput needs at least one of: fixedNodes, loadNode, or enableGravity";
        return false;
    }

    std::ofstream out(inpPath, std::ios::binary | std::ios::trunc);
    if (!out) {
        errOut = "Cannot open .inp for write: " + inpPath.string();
        return false;
    }

    out << std::setprecision(9) << std::fixed;
    out << "*HEADING\nPhysiSim CalculiX export (C3D4)\n";

    out << "*NODE\n";
    for (size_t i = 0; i < mesh.nodes.size(); ++i) {
        const glm::vec3& p = mesh.nodes[i];
        out << (i + 1) << ", " << fp6(static_cast<double>(p.x)) << ", " << fp6(static_cast<double>(p.y)) << ", "
            << fp6(static_cast<double>(p.z)) << "\n";
    }

    out << "*ELEMENT, TYPE=C3D4, ELSET=EALL\n";
    for (size_t e = 0; e < mesh.tets.size(); ++e) {
        const auto& t = mesh.tets[e];
        out << (e + 1) << ", " << (t[0] + 1) << ", " << (t[1] + 1) << ", " << (t[2] + 1) << ", " << (t[3] + 1)
            << "\n";
    }

    if (!fixed.empty()) {
        out << "*NSET,NSET=NFIXED\n";
        size_t col = 0;
        for (uint32_t n : fixed) {
            out << (n + 1);
            if (++col % 16 == 0)
                out << "\n";
            else
                out << ", ";
        }
        if (col % 16 != 0) out << "\n";
    }

    out << "*NSET,NSET=NALL\n";
    for (size_t i = 0; i < mesh.nodes.size(); ++i) {
        out << (i + 1);
        if ((i + 1) % 16 == 0 || i + 1 == mesh.nodes.size())
            out << "\n";
        else
            out << ", ";
    }

    out << "*MATERIAL, NAME=MAT1\n";
    out << "*ELASTIC\n";
    out << fp6(input.youngModulusMpa) << ", " << fp6(input.poissonRatio) << "\n";
    double rhoTonnesPerMm3 = input.densityKgM3 * 1e-12;
    out << "*DENSITY\n";
    out << fp6(rhoTonnesPerMm3) << "\n";

    out << "*SOLID SECTION, ELSET=EALL, MATERIAL=MAT1\n";

    out << "*STEP\n";
    out << "*STATIC\n";

    if (input.enableGravity) {
        out << "*DLOAD\n";
        out << "EALL, GRAV, " << fp6(input.gravityMmPerS2) << ", 0., 0., -1.\n";
    }

    if (!fixed.empty()) {
        out << "*BOUNDARY\n";
        out << "NFIXED, 1, 3, 0.\n";
    }

    if (input.loadNode) {
        uint32_t n1 = *input.loadNode + 1;
        bool any = std::abs(input.loadForceN.x) > 1e-30 || std::abs(input.loadForceN.y) > 1e-30 ||
                   std::abs(input.loadForceN.z) > 1e-30;
        if (any) {
            out << "*CLOAD\n";
            if (std::abs(input.loadForceN.x) > 1e-30) out << n1 << ", 1, " << fp6(input.loadForceN.x) << "\n";
            if (std::abs(input.loadForceN.y) > 1e-30) out << n1 << ", 2, " << fp6(input.loadForceN.y) << "\n";
            if (std::abs(input.loadForceN.z) > 1e-30) out << n1 << ", 3, " << fp6(input.loadForceN.z) << "\n";
        }
    }

    out << "*NODE PRINT, NSET=NALL\n";
    out << "U,\n";
    out << "*EL PRINT, ELSET=EALL\n";
    out << "S,\n";
    out << "*END STEP\n";

    if (!out) {
        errOut = "Failed writing .inp";
        return false;
    }
    return true;
}

} // namespace physisim::fem
