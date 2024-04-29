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

#define ROOT_SIG "CBV(b0), \
                  RootConstants(b1, num32bitconstants=2), \
                  SRV(t0), \
                  SRV(t1), \
                  SRV(t2), \
                  SRV(t3), \
                  SRV(t4)"

#define MAX_MS_X 1
#define MAX_MS_Y 1
#define MAX_MS_Z 1

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

struct Meshlet
{
    uint VertCount;
    uint VertOffset;
    uint PrimCount;
    uint PrimOffset;
};

struct OutVertsList
{
    uint MeshletIndex;
    uint currTessLevel;
    float4 OutVerts[512];
};


// Data Loaders begin here

uint3 UnpackPrimitive(uint primitive)
{
    // Unpacks a 10 bits per index triangle from a 32-bit uint.
    return uint3(primitive & 0x3FF, (primitive >> 10) & 0x3FF, (primitive >> 20) & 0x3FF);
}


// All bitwise operators related to tessellation go below

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