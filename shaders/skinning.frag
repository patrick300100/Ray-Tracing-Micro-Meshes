#version 450

in vec3 fragNormal;

layout(location = 0) out vec4 fragColor;

void main() {
    fragColor = vec4(abs(normalize(fragNormal)), 1);
}