#include "Common.hlsli"

groupshared Payload s_Payload;

[RootSignature(ROOT_SIG)]
[NumThreads(1, 1, 1)]
void main(uint dtid : SV_DispatchThreadID, uint gtid : SV_GroupThreadID, uint gid : SV_GroupID, uint idx: SV_GroupIndex)
{
    DispatchMesh(MAX_MS_X, MAX_MS_Y, MAX_MS_Z, s_Payload);
}
