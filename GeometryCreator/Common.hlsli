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
                  SRV(t3)"

#define MAX_MS_X 1
#define MAX_MS_Y 1
#define MAX_MS_Z 1

struct Payload
{
    uint InstanceCounts[5];   // The instance count for each LOD level.
    uint GroupOffsets[5 + 1]; // The offset in threadgroups for each LOD level.

    // The list of instance indices after culling. Ordered as:
    // (list of LOD 0 instance indices), (list of LOD 1 instance indices), ... (list of LOD MAX_LOD_LEVELS-1 instance indices)                                            
    uint InstanceList[10];
    uint InstanceOffsets[5 + 1]; // The offset into the Instance List at which each LOD level begins.
};