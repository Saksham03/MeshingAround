#include "Common.hlsli"

groupshared Payload s_Payload;

groupshared OutVertsList s_OutVertsList;

ConstantBuffer<Constants> Globals             : register(b0);


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

    float r = 150.;
    float diffFact = 50. * float(dtid);
    float4 pr_0 = float4(r / 2 + diffFact, 0, 0.2, 1);
    float4 pr_1 = float4(r + diffFact, 0, 0.2, 1);
    float4 pr_2 = float4(diffFact, r, 0.2, 1);

    if (gid & 1u)
    {
        s_OutVertsList.currTessLevel = 6u;
    }
    else
    {
        s_OutVertsList.currTessLevel = 2u;
    }
    s_OutVertsList.MeshletIndex = dtid + 2;
    s_OutVertsList.OutVerts[0] = pr_0;
    s_OutVertsList.OutVerts[1] = pr_1;
    s_OutVertsList.OutVerts[2] = pr_2;

    /*float4 in_verts[3] = { pr_0 , pr_1, pr_2 };
    float4 vouts[3];

    GetSubdividedVerts(1u, in_verts, vouts);
    

    s_OutVertsList.OutVerts[0] = vouts[1];
    s_OutVertsList.OutVerts[1] = vouts[0];
    s_OutVertsList.OutVerts[2] = vouts[2];
    s_OutVertsList.OutVerts[3] = vouts[0];*/

    //if (true)
    {
        DispatchMesh(1u << s_OutVertsList.currTessLevel, 1, 1, s_OutVertsList);
    }
    /*else
    {
        DispatchMesh(MAX_MS_X, MAX_MS_Y, MAX_MS_Z, s_Payload);
    }*/
}
