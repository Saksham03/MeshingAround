#include "Common.hlsli"


groupshared OutVertsList s_OutVertsList;

ConstantBuffer<Constants> Globals             : register(b0);
ConstantBuffer<MeshInfo>  MeshInfo            : register(b1);
StructuredBuffer<Vertex>  Vertices            : register(t0);
StructuredBuffer<Meshlet> Meshlets            : register(t1);
ByteAddressBuffer         UniqueVertexIndices : register(t2);
StructuredBuffer<uint>    PrimitiveIndices    : register(t3);
StructuredBuffer<uint>    TessellateFlags     : register(t4);


uint VertIndex(Meshlet m, uint index)
{
    uint localIndex = m.VertOffset + index;

    if (MeshInfo.IndexBytes == 4) // 32-bit Vertex Indices
    {
        return UniqueVertexIndices.Load(localIndex * 4);
    }
    else // 16-bit Vertex Indices
    {
        // Byte address must be 4-byte aligned.
        uint wordOffset = (localIndex & 0x1);
        uint byteOffset = (localIndex / 2) * 4;

        // Grab the pair of 16-bit indices, shift & mask off proper 16-bits.
        uint indexPair = UniqueVertexIndices.Load(byteOffset);
        uint index = (indexPair >> (wordOffset * 16)) & 0xffff;

        return index;
    }
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
[NumThreads(1, 1, 1)]
void main(uint dtid : SV_DispatchThreadID, uint gtid : SV_GroupThreadID, uint gid : SV_GroupID, uint idx : SV_GroupIndex)
{  

    Meshlet m = Meshlets[MeshInfo.MeshletOffset];    

    uint3 triFromMeshletBeingProcessedByCurrentThread = UnpackPrimitive(PrimitiveIndices[m.PrimOffset + gid]);
    uint3 triVertIndices = { 
        VertIndex(m, triFromMeshletBeingProcessedByCurrentThread.x),
        VertIndex(m, triFromMeshletBeingProcessedByCurrentThread.y),
        VertIndex(m, triFromMeshletBeingProcessedByCurrentThread.z)
    };

    s_OutVertsList.currTessLevel = 4u;
    s_OutVertsList.OutVerts[0] = float4(Vertices[triVertIndices.x].Position, 1);
    s_OutVertsList.OutVerts[1] = float4(Vertices[triVertIndices.y].Position, 1);
    s_OutVertsList.OutVerts[2] = float4(Vertices[triVertIndices.z].Position, 1);
    s_OutVertsList.MeshletIndex = TessellateFlags[59];//m.PrimOffset;

    DispatchMesh(1u << s_OutVertsList.currTessLevel, 1, 1, s_OutVertsList);
}
