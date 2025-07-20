static const float shadingWeight = 1.0f;
static const float metallic = 0.25f;
static const float roughness = 0.45f;
static const float ao = 0.1f;
static const float3 lightDir = float3(0.0f, 0.0f, 1.0f);
static const float3 meshColor = float3(0.51f, 0.62f, 0.82f);
static const float3 lightColor = float3(1.0f, 1.0f, 1.0f);
static const float lightIntensity = 22.0f;
static const float PI = 3.14159265359f;

struct Attributes {
    float3 N; //normal
    float3 V; //view direction
};

struct Payload {
    float3 color;
};

float DistributionGGX(float3 N, float3 H, float roughness) {
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

float GeometrySmith(float3 N, float3 V, float3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2  = GeometrySchlickGGX(NdotV, roughness);
    float ggx1  = GeometrySchlickGGX(NdotL, roughness);

    return ggx1 * ggx2;
}

float3 fresnelSchlick(float cosTheta, float3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

[shader("closesthit")]
void main(inout Payload payload, in Attributes attribs) {
    // surface normal
    float3 N = attribs.N;

    // view direction
    float3 V = attribs.V;

    float3 albedo = meshColor;

    float3 F0 = float3(0.04f, 0.04f, 0.04f);
    F0 = lerp(F0, albedo, metallic);

    float3 lightDirs[4] = {
        float3(0.0f, 0.0f, 1.0f),
        float3(0.0f, 1.0f, 0.0f),
        float3(0.0f, 0.0f, -1.0f),
        float3(0.0f, -1.0f, 0.0f)
    };

    float intensities[4] = {
        lightIntensity,
        lightIntensity / 2.0f,
        lightIntensity,
        lightIntensity / 2.0f
    };

    // reflectance equation
    float3 Lo = float3(0.0f, 0.0f, 0.0f);
    for (int i = 0; i < 4; ++i) {
        // calculate per-light radiance
        float3 L = normalize(lightDirs[i]);
        float3 H = normalize(V + L);
        float3 radiance = lightColor * intensities[i];

        // cook-torrance brdf
        float NDF = DistributionGGX(N, H, roughness);
        float G = GeometrySmith(N, V, L, roughness);
        float3 F = fresnelSchlick(max(dot(H, V), 0.0f), F0);

        float3 kS = F;
        float3 kD = float3(1.0f, 1.0f, 1.0f) - kS;
        kD *= 1.0 - metallic;

        float3 numerator = NDF * G * F;
        float denominator = 4.0f * max(dot(N, V), 0.0f) * max(dot(N, L), 0.0f) + 0.0001f;
        float3 specular = numerator / denominator;

        // add to outgoing radiance Lo
        float NdotL = max(dot(N, L), 0.0f);
        Lo += (kD * albedo / PI + specular) * radiance * NdotL;
    }

    float3 ambient = albedo * ao * lightIntensity * 0.1f;
    float3 fcolor = ambient + Lo;

    fcolor = fcolor / (fcolor + float3(1.0f, 1.0f, 1.0f));
    fcolor = lerp(albedo, fcolor, shadingWeight);

    payload.color = fcolor;
}
