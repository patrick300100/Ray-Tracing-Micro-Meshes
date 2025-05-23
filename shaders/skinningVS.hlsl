cbuffer BoneMatrices : register(b0)
{
    float4x4 boneTransforms[50];
};

cbuffer MVPMatrix : register(b1)
{
    float4x4 mvpMatrix;
};

cbuffer MVMatrix : register(b2)
{
    float4x4 mvMatrix;
};

cbuffer dScale : register(b3)
{
    float displacementScale;
};

struct VertexInput {
    float3 position            : POSITION;
    float3 normal              : NORMAL;
    int4   boneIndices         : BONEINDICES;
    float4 boneWeights         : BONEWEIGHTS;
    float3 displacement        : DISPLACEMENT;

    int4   baseBoneIndices0    : BASEBONEINDICES0;
    int4   baseBoneIndices1    : BASEBONEINDICES1;
    int4   baseBoneIndices2    : BASEBONEINDICES2;

    float4 baseBoneWeights0    : BASEBONEWEIGHTS0;
    float4 baseBoneWeights1    : BASEBONEWEIGHTS1;
    float4 baseBoneWeights2    : BASEBONEWEIGHTS2;

    float3 baryCoords          : BARYCOORDS;
};

struct VertexOutput {
    float4 position        : SV_POSITION;
    float3 fragNormal      : NORMAL;
    float3 fragSurfacePos  : POSITION1;
};

VertexOutput VSMain(VertexInput input) {
    VertexOutput output;

    float4x4 bv0SkinMatrix =
        input.baseBoneWeights0.x * boneTransforms[input.baseBoneIndices0.x] +
        input.baseBoneWeights0.y * boneTransforms[input.baseBoneIndices0.y] +
        input.baseBoneWeights0.z * boneTransforms[input.baseBoneIndices0.z] +
        input.baseBoneWeights0.w * boneTransforms[input.baseBoneIndices0.w];

    float4x4 bv1SkinMatrix =
        input.baseBoneWeights1.x * boneTransforms[input.baseBoneIndices1.x] +
        input.baseBoneWeights1.y * boneTransforms[input.baseBoneIndices1.y] +
        input.baseBoneWeights1.z * boneTransforms[input.baseBoneIndices1.z] +
        input.baseBoneWeights1.w * boneTransforms[input.baseBoneIndices1.w];

    float4x4 bv2SkinMatrix =
        input.baseBoneWeights2.x * boneTransforms[input.baseBoneIndices2.x] +
        input.baseBoneWeights2.y * boneTransforms[input.baseBoneIndices2.y] +
        input.baseBoneWeights2.z * boneTransforms[input.baseBoneIndices2.z] +
        input.baseBoneWeights2.w * boneTransforms[input.baseBoneIndices2.w];

    float4x4 skinMatrix =
        input.baryCoords.x * bv0SkinMatrix +
        input.baryCoords.y * bv1SkinMatrix +
        input.baryCoords.z * bv2SkinMatrix;

    float4 newPos = mul(float4(input.position + displacementScale * input.displacement, 1.0f), skinMatrix);

    output.position = mul(newPos, mvpMatrix);
    output.fragSurfacePos = mul(newPos, mvMatrix).xyz;

    float3x3 normalMatrix = (float3x3)skinMatrix;
    output.fragNormal = mul(input.normal, normalMatrix);

    return output;
}
