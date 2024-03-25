#include "Common.hlsli"

struct VertexOut
{
    float4 PositionHS   : SV_Position;
    float3 PositionVS   : POSITION0;
    float3 Normal       : NORMAL0;
    uint   MeshletIndex : COLOR0;
};


float4 main(VertexOut input) : SV_TARGET
{
    float3 diffuseColor = float3(
            float(input.MeshletIndex & 1),
            float(input.MeshletIndex & 3) / 4,
            float(input.MeshletIndex & 7) / 8);
    return float4(diffuseColor, 1);
}
