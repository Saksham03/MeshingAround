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


struct Constants
{
    float4x4 World;
    float4x4 WorldView;
    float4x4 WorldViewProj;
    uint     DrawMeshlets;
};

struct MeshInfo
{
    uint IndexBytes;
    uint MeshletOffset;
};

struct Vertex
{
    float3 Position;
    float3 Normal;
};

struct VertexOut
{
    float4 PositionHS   : SV_Position;
    float3 PositionVS   : POSITION0;
    float3 Normal       : NORMAL0;
    uint   MeshletIndex : COLOR0;
};

struct Meshlet
{
    uint VertCount;
    uint VertOffset;
    uint PrimCount;
    uint PrimOffset;
};

ConstantBuffer<Constants> Globals             : register(b0);
ConstantBuffer<MeshInfo>  MeshInfo            : register(b1);

StructuredBuffer<Vertex>  Vertices            : register(t0);
StructuredBuffer<Meshlet> Meshlets            : register(t1);
ByteAddressBuffer         UniqueVertexIndices : register(t2);
StructuredBuffer<uint>    PrimitiveIndices    : register(t3);


/////
// Data Loaders

uint3 UnpackPrimitive(uint primitive)
{
    // Unpacks a 10 bits per index triangle from a 32-bit uint.
    return uint3(primitive & 0x3FF, (primitive >> 10) & 0x3FF, (primitive >> 20) & 0x3FF);
}

uint3 GetPrimitive(Meshlet m, uint index)
{
    return UnpackPrimitive(PrimitiveIndices[m.PrimOffset + index]);
}

uint GetVertexIndex(Meshlet m, uint localIndex)
{
    localIndex = m.VertOffset + localIndex;

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

VertexOut GetVertexAttributes(uint meshletIndex, uint vertexIndex)
{
    Vertex v = Vertices[vertexIndex];

    VertexOut vout;
    vout.PositionVS = mul(float4(v.Position, 1), Globals.WorldView).xyz;
    vout.PositionHS = mul(float4(v.Position, 1), Globals.WorldViewProj);
    vout.Normal = mul(float4(v.Normal, 0), Globals.World).xyz;
    vout.MeshletIndex = meshletIndex;

    return vout;
}

VertexOut GetTransformedVert(float4 in_vert, float2 t_xy, float rot, float scale)
{
    VertexOut vout;
    float4 out_vert = in_vert;
    out_vert.xy *= scale;
    out_vert.x = out_vert.x * cos(rot) - out_vert.y * sin(rot);
    out_vert.y = out_vert.x * sin(rot) + out_vert.y * cos(rot);
    out_vert.xy += t_xy;

    out_vert.xy *= 50;
    vout.PositionHS = mul(out_vert, Globals.WorldViewProj);
    return vout;
}


[RootSignature(ROOT_SIG)]
[NumThreads(1, 1, 1)]
[OutputTopology("triangle")]
void main(
    uint gtid : SV_GroupThreadID,
    uint3 gid : SV_GroupID,
    uint dtid: SV_DispatchThreadID,
    in payload Payload payload,
    out indices uint3 tris[126],
    out vertices VertexOut verts[64]
)
{

    uint noOfPrims = 4;
    uint noOfVerts = 6;

    SetMeshOutputCounts(noOfVerts, noOfPrims);

    float r = 1.;
    float4 pr_0 = float4(0, 0, 0.2, 1);
    float4 pr_1 = float4(r, 0, 0.2, 1);
    float4 pr_2 = float4(0, r, 0.2, 1);

    if (gtid < noOfPrims)
    {
        //tris[gtid] = uint3( gtid + 1, 0, gtid == noOfPrims - 1 ? (gtid + 2) % 6 : gtid + 2);
   
        float PI = 3.14;

        VertexOut p_0 = GetTransformedVert(pr_0, float2(0, 0), radians(0.), 1.);
        p_0.MeshletIndex = 1;
        VertexOut p_1 = GetTransformedVert(pr_1, float2(0, 0), radians(0.), 1.);
        p_1.MeshletIndex = 2;
        VertexOut p_2 = GetTransformedVert(pr_2, float2(0, 0), radians(0.), 1.);
        p_2.MeshletIndex = 3;

        VertexOut p_12 = GetTransformedVert(pr_1, float2(0, 0.5), radians(0), 0.5);
        p_12.MeshletIndex = 4;
        VertexOut p_01 = GetTransformedVert(pr_0, float2(0.5, 0), radians(PI/2), 0.5);
        p_01.MeshletIndex = 5;
        VertexOut p_02 = GetTransformedVert(pr_0, float2(0, 0.5), radians(3 * PI / 2), 0.5);
        p_02.MeshletIndex = 6;

        /*verts[0] = p_12;
        verts[1] = p_1;
        verts[2] = p_01;
        tris[0] = uint3(0, 1, 2);*/

        verts[0] = p_12;
        verts[1] = p_02;
        verts[2] = p_2;
        tris[0] = uint3(0, 1, 2);

        verts[3] = p_0;
        tris[1] = uint3(3, 1, 0);

        verts[4] = p_01;
        tris[2] = uint3(4, 3, 0);

        verts[5] = p_1;
        tris[3] = uint3(5, 4, 0);
    }
}
