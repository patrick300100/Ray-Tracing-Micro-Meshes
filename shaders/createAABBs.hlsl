struct Vertex {
    float3 position;
    float3 displacement;
};

struct Triangle {
    uint uVerticesStart;
    uint uVerticesCount;
};

struct AABB {
    float3 minPos;
    float3 maxPos;
};

StructuredBuffer<Vertex> microVertices : register(t0);
StructuredBuffer<Triangle> triangles : register(t1);

RWStructuredBuffer<AABB> AABBs : register(u0);

[numthreads(64, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID) {
    uint id = DTid.x;

    //We don't want a thread writes outside of the given buffer region
    uint triangleCount, triangleStride;
    triangles.GetDimensions(triangleCount, triangleStride);
    if(id >= triangleCount) return;

    Triangle t = triangles[id];

    const float MAX_FLOAT = 3.402823466e+38f;

    AABB aabb;
    aabb.minPos = float3(MAX_FLOAT, MAX_FLOAT, MAX_FLOAT);
    aabb.maxPos = float3(-MAX_FLOAT, -MAX_FLOAT, -MAX_FLOAT);

    for(int i = t.uVerticesStart; i < t.uVerticesStart + t.uVerticesCount; i++) {
        Vertex mv = microVertices[i];
        float3 displacedPos = mv.position + mv.displacement;

        aabb.minPos = min(aabb.minPos, displacedPos);
        aabb.maxPos = max(aabb.maxPos, displacedPos);
    }

    AABBs[id] = aabb;
}
