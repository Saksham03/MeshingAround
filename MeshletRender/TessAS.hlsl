#include "Common.hlsli"


groupshared OutVertsList s_OutVertsList;

ConstantBuffer<Constants> Globals             : register(b0);
ConstantBuffer<MeshInfo>  MeshInfo            : register(b1);
StructuredBuffer<Vertex>  Vertices            : register(t0);
StructuredBuffer<Meshlet> Meshlets            : register(t1);
ByteAddressBuffer         UniqueVertexIndices : register(t2);
StructuredBuffer<uint>    PrimitiveIndices    : register(t3);


uint VertIndex(Meshlet m, uint index)
{
    uint localIndex = m.VertOffset + index;
    return UniqueVertexIndices.Load(localIndex * 4);
}

float4 GetTransformedVert(float4 in_vert, float2 t_xy, float rot, float scale)
{
    float4 out_vert = in_vert;
    out_vert.xy *= scale;
    out_vert.x = out_vert.x * cos(rot) - out_vert.y * sin(rot);
    out_vert.y = out_vert.x * sin(rot) + out_vert.y * cos(rot);
    out_vert.xy += t_xy;

    out_vert.xy *= 50;
    return mul(out_vert, Globals.WorldViewProj);
}

void GetSubdividedVerts(uint key, float4 in_verts[3], out float4 vout[3])
{
    matrix<float, 3, 3> cumulativeTransform = keyToXform(key);
    float2 t1 = mul(cumulativeTransform, float3(0.f, 0.f, 1.f)).xy;
    float2 t2 = mul(cumulativeTransform, float3(1.f, 0.f, 1.f)).xy;
    float2 t3 = mul(cumulativeTransform, float3(0.f, 1.f, 1.f)).xy;
    vout[0] = mul(barycentricInterp(in_verts, t1), Globals.WorldViewProj);
    vout[1] = mul(barycentricInterp(in_verts, t2), Globals.WorldViewProj);
    vout[2] = mul(barycentricInterp(in_verts, t3), Globals.WorldViewProj);
}


[RootSignature(ROOT_SIG)]
[NumThreads(3, 1, 1)]
void main(uint dtid : SV_DispatchThreadID, uint gtid : SV_GroupThreadID, uint gid : SV_GroupID, uint idx : SV_GroupIndex)
{

    //float r = 150.;
    //float diffFact = 50. * float(dtid);
    //float test_depth = 20.5;
    //float4 pr_0 = float4(r / 2 + diffFact, 0, test_depth, 1);
    //float4 pr_1 = float4(r + diffFact, 0, test_depth, 1);
    //float4 pr_2 = float4(diffFact, r, test_depth, 1);    

    //if (gid & 1u)
    //{
    //    s_OutVertsList.currTessLevel = 6u;
    //}
    //else
    //{
    //    s_OutVertsList.currTessLevel = 2u;
    //}
    //s_OutVertsList.MeshletIndex = dtid + 2;
    //s_OutVertsList.OutVerts[0] = pr_0;
    //s_OutVertsList.OutVerts[1] = pr_1;
    //s_OutVertsList.OutVerts[2] = pr_2;
    //
    //DispatchMesh(1u << s_OutVertsList.currTessLevel, 1, 1, s_OutVertsList);


    Meshlet m = Meshlets[MeshInfo.MeshletOffset];

    uint3 triFromMeshletBeingProcessedByCurrentThread = UnpackPrimitive(PrimitiveIndices[m.PrimOffset + gid]);
    uint3 triVertIndices = { 
        VertIndex(m, triFromMeshletBeingProcessedByCurrentThread.x),
        VertIndex(m, triFromMeshletBeingProcessedByCurrentThread.y),
        VertIndex(m, triFromMeshletBeingProcessedByCurrentThread.z)
    };

    s_OutVertsList.currTessLevel = 3u;
    s_OutVertsList.OutVerts[0] = float4(Vertices[triVertIndices.x].Position, 1);
    s_OutVertsList.OutVerts[1] = float4(Vertices[triVertIndices.y].Position, 1);
    s_OutVertsList.OutVerts[2] = float4(Vertices[triVertIndices.z].Position, 1);

    DispatchMesh(1u << s_OutVertsList.currTessLevel, 1, 1, s_OutVertsList);
}
