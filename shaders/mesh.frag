#version 450

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec3 fragWorldPos;
layout(location = 2) in vec4 fragDefect;
layout(location = 3) in float fragPick;
layout(location = 4) in float fragPropagated;
layout(location = 5) in vec3 fragDispWorld;

layout(set = 0, binding = 0) uniform UBO {
    mat4 model;
    mat4 view;
    mat4 proj;
    vec4 cameraWorld;
    vec4 defectScales;
    vec4 defectTime;
    vec4 defectAux;
    vec4 defectAux2;
} ubo;

layout(location = 0) out vec4 outColor;

vec3 weaknessHeatmap(float h) {
    h = clamp(h, 0.0, 1.0);
    vec3 k0 = vec3(0.05, 0.14, 0.62);
    vec3 k1 = vec3(0.0, 0.72, 0.90);
    vec3 k2 = vec3(0.18, 0.82, 0.32);
    vec3 k3 = vec3(0.98, 0.82, 0.12);
    vec3 k4 = vec3(0.92, 0.10, 0.12);
    float x = min(h * 4.0, 3.999);
    float f = fract(x);
    float s = smoothstep(0.0, 1.0, f);
    if (x < 1.0) return mix(k0, k1, s);
    if (x < 2.0) return mix(k1, k2, s);
    if (x < 3.0) return mix(k2, k3, s);
    return mix(k3, k4, s);
}

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
    vec3 baseLit = base * diff + rimCol * rim;

    float sS = ubo.defectScales.x;
    float sV = ubo.defectScales.y;
    float sL = ubo.defectScales.z;
    float vMode = ubo.defectScales.w;
    float tMix = clamp(ubo.defectTime.x, 0.0, 1.0);

    float r = clamp(fragDefect.r, 0.0, 1.0);
    float g = clamp(fragDefect.g, 0.0, 1.0);
    float b = clamp(fragDefect.b, 0.0, 1.0);
    float a = clamp(fragDefect.a, 0.0, 1.0);

    float combined = clamp(r + g * sS + b * sV + a * sL, 0.0, 1.0);
    float prop = clamp(fragPropagated, 0.0, 1.0);
    float mixed = mix(combined, prop, tMix);

    float align = clamp(r * 0.35 + g * sS * 0.35 + b * sV * 0.15 + a * sL * 0.15, 0.0, 1.0);

    float hPre = mixed;
    if (vMode > 1.5) hPre = align;

    bool dynOn = ubo.defectAux.x > 0.5;
    float h = hPre;
    if (dynOn) {
        float lo = ubo.defectTime.y;
        float hi = ubo.defectTime.z;
        float denom = max(hi - lo, 1e-5);
        h = clamp((hPre - lo) / denom, 0.0, 1.0);
    }

    vec3 heat = weaknessHeatmap(h);
    float gamma = 0.72;
    float heatBlend = smoothstep(0.0, 0.02, h) * (0.32 + 0.68 * pow(h, gamma));
    vec3 heatLit = heat * (0.26 + 0.74 * diff);
    vec3 lit = baseLit;

    if (vMode > 0.5 && vMode < 1.5) {
        vec3 fc = vec3(r, g * sS, a * sL);
        float fcm = max(fc.r, max(fc.g, fc.b));
        vec3 fcLit = fc * (0.35 + 0.65 * diff);
        lit = mix(baseLit, fcLit, smoothstep(0.02, 0.25, fcm) * 0.55);
    } else {
        lit = mix(baseLit, heatLit, heatBlend);
    }

    bool alertOn = ubo.defectAux.y > 0.5;
    if (alertOn) {
        float thresh = clamp(ubo.defectTime.w, 0.0, 1.0);
        float aAlert = smoothstep(thresh, min(thresh + 0.08, 1.0), h);
        float blink = 1.0;
        if (ubo.defectAux.z > 0.5) blink = 0.62 + 0.38 * sin(ubo.defectAux.w * 8.0);
        vec3 alertCol = vec3(0.96, 0.07, 0.06);
        lit = mix(lit, alertCol, aAlert * 0.82 * blink);
    }

    float dirW = clamp(ubo.defectAux2.x, 0.0, 1.0);
    if (dirW > 1e-3) {
        float dl = length(fragDispWorld);
        if (dl > 1e-6) {
            float dirCue = abs(dot(N, normalize(fragDispWorld)));
            float shade = mix(0.72, 1.08, dirCue);
            lit *= mix(1.0, shade, dirW);
        }
    }

    float pk = clamp(fragPick, 0.0, 1.0);
    vec3 pickTint = vec3(1.0, 0.92, 0.25);
    lit = mix(lit, pickTint, pk * 0.55);
    outColor = vec4(lit, 1.0);
}
