#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in float inDefect;
layout(location = 3) in float inPick;

layout(set = 0, binding = 0) uniform UBO {
    mat4 model;
    mat4 view;
    mat4 proj;
    vec4 cameraWorld;
} ubo;

layout(location = 0) out vec3 fragNormal;
layout(location = 1) out vec3 fragWorldPos;
layout(location = 2) out float fragDefect;
layout(location = 3) out float fragPick;

void main() {
    vec4 world = ubo.model * vec4(inPosition, 1.0);
    fragWorldPos = world.xyz;
    mat3 normalMat = mat3(transpose(inverse(ubo.model)));
    fragNormal = normalize(normalMat * inNormal);
    fragDefect = inDefect;
    fragPick = inPick;
    gl_Position = ubo.proj * ubo.view * world;
}
