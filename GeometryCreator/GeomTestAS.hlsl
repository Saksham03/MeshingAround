#include "Common.hlsli"

groupshared Payload s_Payload;

[RootSignature(ROOT_SIG)]
[NumThreads(5, 1, 1)]
void main(uint dtid : SV_DispatchThreadID, uint gtid : SV_GroupThreadID, uint gid : SV_GroupID, uint idx: SV_GroupIndex)
{
    s_Payload.ParentGroupID = 100.f * dtid;
    DispatchMesh(1, 1, 1, s_Payload);
}
