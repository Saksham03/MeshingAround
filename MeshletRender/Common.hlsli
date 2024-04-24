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

struct Payload
{
    uint MeshletIndex;
    uint InstanceCounts[5];   // The instance count for each LOD level.
    uint GroupOffsets[5 + 1]; // The offset in threadgroups for each LOD level.

    // The list of instance indices after culling. Ordered as:
    // (list of LOD 0 instance indices), (list of LOD 1 instance indices), ... (list of LOD MAX_LOD_LEVELS-1 instance indices)                                            
    uint InstanceList[10];
    uint InstanceOffsets[5 + 1]; // The offset into the Instance List at which each LOD level begins.
    float4 OutVert;
};

struct OutVertsList
{
    uint MeshletIndex;
    uint currTessLevel;
    float4 OutVerts[512];
};

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