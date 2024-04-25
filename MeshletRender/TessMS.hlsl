//*********************************************************
//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
//*********************************************************

#include "Common.hlsli"

struct VertexOut
{
    float4 PositionHS   : SV_Position;
    float3 PositionVS   : POSITION0;
    float3 Normal       : NORMAL0;
    uint   MeshletIndex : COLOR0;
};


ConstantBuffer<Constants> Globals             : register(b0);
ConstantBuffer<MeshInfo>  MeshInfo            : register(b1);

StructuredBuffer<Vertex>  Vertices            : register(t0);
StructuredBuffer<Meshlet> Meshlets            : register(t1);
ByteAddressBuffer         UniqueVertexIndices : register(t2);
StructuredBuffer<uint>    PrimitiveIndices    : register(t3);


 void GetSubdividedVerts(uint key, float4 in_verts[3],out VertexOut vout[3])
{
    matrix<float, 3, 3> cumulativeTransform = keyToXform(key);
    float2 t1 = mul(cumulativeTransform, float3(0.f, 0.f, 1.f)).xy;
    float2 t2 = mul(cumulativeTransform, float3(1.f, 0.f, 1.f)).xy;
    float2 t3 = mul(cumulativeTransform, float3(0.f, 1.f, 1.f)).xy;
    vout[0].PositionHS = mul(barycentricInterp(in_verts, t1), Globals.WorldViewProj);    
    vout[1].PositionHS = mul(barycentricInterp(in_verts, t2), Globals.WorldViewProj);
    vout[2].PositionHS = mul(barycentricInterp(in_verts, t3), Globals.WorldViewProj);
}


[RootSignature(ROOT_SIG)]
[NumThreads(32, 1, 1)]
[OutputTopology("triangle")]
void main(
    uint gtid : SV_GroupThreadID,
    uint3 gid : SV_GroupID,
    uint dtid: SV_DispatchThreadID,
    in payload OutVertsList payload,
    out indices uint3 tris[256],
    out vertices VertexOut verts[256]
)
{
    
    uint maxTessLevel = 1u;
    uint noOfPrims = 1u << payload.currTessLevel;
    uint noOfVerts = 3 * noOfPrims;

    SetMeshOutputCounts(noOfVerts, noOfPrims);

    if (gtid < noOfPrims)
    {
        uint parentKey = noOfPrims;
        {
            uint i = gtid;
            uint currKey = parentKey | i;
            VertexOut vouts[3];
            float4 in_verts[3] = { payload.OutVerts[0], payload.OutVerts[1], payload.OutVerts[2] };
            GetSubdividedVerts(currKey, in_verts, vouts);
            vouts[0].MeshletIndex = payload.MeshletIndex + gtid;
            verts[3 * i] = vouts[0];
            verts[3 * i + 1] = vouts[((maxTessLevel + 1) % 2) + 1];
            verts[3 * i + 2] = vouts[(maxTessLevel % 2) + 1];
            tris[i] = uint3(3 * i, 3 * i + 1, 3 * i + 2);
        }

    }
}
