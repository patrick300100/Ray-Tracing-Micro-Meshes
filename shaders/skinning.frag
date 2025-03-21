#version 450

const float shadingWeight = 1.0f;
const float metallic = 0.25f;
const float roughness = 0.45f;
const float ao = 0.1f;
const vec3 lightDir = vec3(0, 0, 1);
const vec3 meshColor = vec3(0.51f, 0.62f, 0.82f);
const vec3 lightColor = vec3(1);
const float lightIntensity = 22;
const float PI = 3.14159265359;

layout(location = 3) uniform vec3 cameraPos;

in vec3 fragNormal;
in vec3 fragSurfacePos;

layout(location = 0) out vec4 fragColor;

float DistributionGGX(vec3 N, vec3 H, float roughness) {
    float a      = roughness*roughness;
    float a2     = a*a;
    float NdotH  = max(dot(N, H), 0.0);
    float NdotH2 = NdotH*NdotH;

    float num   = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;

    return num / denom;
}

float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness + 1.0);
    float k = (r*r) / 8.0;

    float num   = NdotV;
    float denom = NdotV * (1.0 - k) + k;

    return num / denom;
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2  = GeometrySchlickGGX(NdotV, roughness);
    float ggx1  = GeometrySchlickGGX(NdotL, roughness);

    return ggx1 * ggx2;
}

vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

void main() {
    // surface normal
    vec3 t1 = dFdx(fragSurfacePos);
    vec3 t2 = dFdy(fragSurfacePos);
    vec3 N = normalize(fragNormal);
    N = normalize(cross(t1, t2));

    // view direction
    vec3 V = normalize( -fragSurfacePos);

    vec3 albedo = meshColor;

    vec3 F0 = vec3(0.04);
    F0 = mix(F0, albedo, metallic);

    vec3 lightDirs[2];
    lightDirs[0] = vec3(0.0f, 0.0f, 1.0f);
    lightDirs[1] = vec3(0.0f, 1.0f, 0.0f);

    float intensities[2];
    intensities[0] = lightIntensity;
    intensities[1] = lightIntensity / 2.0f;

    // reflectance equation
    vec3 Lo = vec3(0.0);
    for(int i = 0; i < 2; ++i) {
        // calculate per-light radiance
        vec3 L = normalize(lightDirs[i]);
        vec3 H = normalize(V + L);
        vec3 radiance = lightColor * intensities[i];

        // cook-torrance brdf
        float NDF = DistributionGGX(N, H, roughness);
        float G = GeometrySmith(N, V, L, roughness);
        vec3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);

        vec3 kS = F;
        vec3 kD = vec3(1.0) - kS;
        kD *= 1.0 - metallic;

        vec3 numerator = NDF * G * F;
        float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;
        vec3 specular = numerator / denominator;

        // add to outgoing radiance Lo
        float NdotL = max(dot(N, L), 0.0);
        Lo += (kD * albedo / PI + specular) * radiance * NdotL;
    }

    vec3 ambient = albedo * ao * lightIntensity * 0.1;
    vec3 fcolor = ambient + Lo;

    fcolor = fcolor / (fcolor + vec3(1.0));

    fcolor = mix(albedo, fcolor, shadingWeight);

    fragColor = vec4(fcolor, 1.0);
}