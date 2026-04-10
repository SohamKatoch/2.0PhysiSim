#pragma once

namespace physisim::sim {

/// Linear-elastic material parameters for CPU mass–spring stress normalization (toy model).
struct SimMaterial {
    /// Young's modulus (Pa); used with edge strain for stress = E * strain (display via normalized strain).
    float youngsModulusPa{200e9f};
    float densityKgM3{7850.f};
    /// Engineering strain at which normalized stress reaches 1.0 in the viewport channel.
    float maxStrain{0.02f};
};

} // namespace physisim::sim
