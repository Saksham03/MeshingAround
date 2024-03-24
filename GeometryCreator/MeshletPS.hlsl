
struct VertexOut
{
    float4 PositionHS   : SV_Position;
    float3 PositionVS   : POSITION0;
    float3 Normal       : NORMAL0;
    uint   MeshletIndex : COLOR0;
};


float4 main(VertexOut input) : SV_TARGET
{
    return float4(1, 0, 0, 1);
}
