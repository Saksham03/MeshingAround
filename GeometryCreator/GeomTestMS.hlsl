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

float4 barycentricInterp(float4 in_verts[3], float2 t)
{
    return
        in_verts[0] +
        t.x * (in_verts[1] - in_verts[0]) +
        t.y * (in_verts[2] - in_verts[0]);
}

matrix<float, 3, 3> bitToXform(uint bit)
{
    float scale = float(bit) - 0.5f;
    float3 column1 = float3(scale, -0.5f, 0.0);
    float3 column2 = float3(-0.5f, -scale, 0.0);
    float3 column3 = float3(0.5f, 0.5f, 1.0);
    float3x3 xFormMat = 
    {
        float3(column1.x, column2.x, column3.x),
        float3(column1.y, column2.y, column3.y),
        float3(column1.z, column2.z, column3.z),
    };
    /*float3x3 xFormMat =
    {
        float3(column1.x, column1.y, column1.z),
        float3(column2.x, column2.y, column2.z),
        float3(column3.x, column3.y, column3.z),
    };*/
    return xFormMat;
}

matrix<float, 3, 3> keyToXform(uint key)
{
    matrix<float, 3, 3> cumulativeTransform = 
    {
        {1, 0, 0},
        {0, 1, 0},
        {0, 0, 1}
    };
    while (key > 1u)
    {
        cumulativeTransform = mul(bitToXform(key & 1u), cumulativeTransform);
        key = key >> 1u;
    }
    return cumulativeTransform;
}

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
[NumThreads(128, 1, 1)]
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
    
    uint maxTessLevel = 5u;
    uint noOfPrims = 1u << maxTessLevel;
    uint noOfVerts = 3 * noOfPrims;

    SetMeshOutputCounts(noOfVerts, noOfPrims);

    float r = 50.;
    float4 pr_0 = float4(r/2., 0, 0.2, 1);
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
        p_12.MeshletIndex = 3;
        VertexOut p_01 = GetTransformedVert(pr_0, float2(0.5, 0), radians(PI/2), 0.5);
        p_01.MeshletIndex = 3;
        VertexOut p_02 = GetTransformedVert(pr_0, float2(0, 0.5), radians(3 * PI / 2), 0.5);
        p_02.MeshletIndex = 6;

        //for (uint currKey = 0u; currKey <= 2u << maxTessLevel; currKey++)
        uint parentKey = 1u << maxTessLevel;
        //for (uint i = 0u; i < parentKey; i++)
        {
            uint i = gtid;
            uint currKey = parentKey | i;
            VertexOut vouts[3];
            float4 in_verts[3] = { pr_0 , pr_1, pr_2 };
            GetSubdividedVerts(currKey, in_verts, vouts);
            vouts[0].MeshletIndex = i + 1;
            verts[3 * i] = vouts[0];
            verts[3 * i + 1] = vouts[((maxTessLevel + 1) % 2) + 1];
            verts[3 * i + 2] = vouts[(maxTessLevel % 2) + 1];
            tris[i] = uint3(3 * i, 3 * i + 1, 3 * i + 2);
        }

        /*verts[0] = p_0;
        verts[1] = p_2;
        verts[2] = p_1;
        tris[0] = uint3(0, 1, 2);*/
    }
}
