#include "Obj.hlsli"

struct VSIn { 
	float4 pos : POSITION; 
	float2 uv : TEXCOORD0; 
	float3 nrm : NORMAL; 
	float4 weights : WEIGHTS; 
	uint4 indices : BONES; 
};

VSOutput main(VSIn input) {
	// 法線にワールド行列によるスケーリング・回転を適用
	// ※スケーリングが一様な場合のみ正しい
	float4 worldNormal = normalize(mul(float4(input.nrm, 0), world));
	float4 worldPos = mul(input.pos, world);

	VSOutput output; // ピクセルシェーダーに渡す値
	output.svpos = mul(input.pos, mul(world, mul(view, projection)));

	output.worldpos = worldPos;
	output.normal = worldNormal.xyz;
	output.uv = input.uv;

	return output;
}