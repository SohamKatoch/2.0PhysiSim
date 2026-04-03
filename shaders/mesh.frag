#version 450

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec3 fragWorldPos;
layout(location = 2) in float fragDefect;
layout(location = 3) in float fragPick;

layout(set = 0, binding = 0) uniform UBO {
    mat4 model;
    mat4 view;
    mat4 proj;
    vec4 cameraWorld;
} ubo;

layout(location = 0) out vec4 outColor;

void main() {
    vec3 N = normalize(fragNormal);
    vec3 V = normalize(ubo.cameraWorld.xyz - fragWorldPos);
    vec3 L = normalize(vec3(0.45, 0.78, 0.42));
    vec3 L2 = normalize(vec3(-0.35, 0.2, -0.85));
    float ndl = max(dot(N, L), 0.0);
    float ndl2 = max(dot(N, L2), 0.0) * 0.35;
    float ambient = 0.14;
    float wrap = 0.18;
    float diff = ambient + (1.0 - ambient) * clamp((ndl + wrap) / (1.0 + wrap), 0.0, 1.0) + ndl2;
    vec3 base = vec3(0.52, 0.58, 0.68);
    float rim = pow(1.0 - max(dot(N, V), 0.0), 3.0) * 0.22;
    vec3 rimCol = vec3(0.55, 0.65, 0.88);
    // fragDefect: 0 = strong / minor concern (green), 1 = weak / severe (red)
    float h = clamp(fragDefect, 0.0, 1.0);
    vec3 strong = vec3(0.18, 0.72, 0.38);
    vec3 weak = vec3(0.92, 0.16, 0.18);
    vec3 heat = mix(strong, weak, h);
    float overlay = smoothstep(0.0, 0.04, h);
    vec3 baseLit = base * diff + rimCol * rim;
    vec3 heatLit = heat * (0.38 + 0.62 * diff);
    vec3 lit = mix(baseLit, heatLit, overlay * 0.9);
    // Cursor hover / selection (yellow emphasis, distinct from defect heat)
    float pk = clamp(fragPick, 0.0, 1.0);
    vec3 pickTint = vec3(1.0, 0.92, 0.25);
    lit = mix(lit, pickTint, pk * 0.55);
    outColor = vec4(lit, 1.0);
}
