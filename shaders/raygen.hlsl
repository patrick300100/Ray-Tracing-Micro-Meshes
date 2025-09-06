struct Payload {
    float3 color;
};

cbuffer Camera : register(b0) {
    float4x4 invViewProj;
};

cbuffer Screenshot : register(b1) {
    bool takeScreenshot;
};

RaytracingAccelerationStructure scene : register(t0);
RWTexture2D<float4> outputTexture : register(u0);
RWTexture2D<float4> screenshotTexture : register(u1);

[shader("raygeneration")]
void main() {
    // Pixel coordinates
    uint2 pixelIndex = DispatchRaysIndex().xy;
    uint2 screenSize = DispatchRaysDimensions().xy;

    // Convert to [0, 1]
    float2 screenUV = (pixelIndex + 0.5f) / screenSize;

    // Convert to Normalized Device Coordinates [-1, 1]
    float2 ndc = screenUV * 2.0f - 1.0f;
    ndc.y *= -1.0f; // Flip Y for DX convention

    // Unproject to world space
    float4 nearPoint = mul(invViewProj, float4(ndc.x, ndc.y, 0.0f, 1.0f));
    float4 farPoint  = mul(invViewProj, float4(ndc.x, ndc.y, 1.0f, 1.0f));

    nearPoint /= nearPoint.w;
    farPoint  /= farPoint.w;

    RayDesc ray;
    ray.Origin = nearPoint.xyz;
    ray.Direction = normalize(farPoint.xyz - nearPoint.xyz);
    ray.TMin = 0.001;
    ray.TMax = 10000;

    Payload payload;
    payload.color = float3(0, 0, 0);

    TraceRay(scene, RAY_FLAG_NONE, 0xFF, 0, 1, 0, ray, payload);

    if(takeScreenshot) screenshotTexture[DispatchRaysIndex().xy] = float4(payload.color, 1.0);
    else outputTexture[DispatchRaysIndex().xy] = float4(payload.color, 1.0);
}
