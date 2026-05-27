// =============================================================
// SteampunkVS.hlsl - スチームパンク用頂点シェーダー (Instanced)
// =============================================================

#pragma pack_matrix(row_major)

struct InstanceData {
    matrix world;
    float4 color;
    float4 uvScaleOffset;
};

StructuredBuffer<InstanceData> gInstanceData : register(t2);

cbuffer CBFrame : register(b0) {
    matrix gView;
    matrix gProj;
    matrix gViewProj;
    float3 gCamPos;
    float gTime;
};

struct VSIn {
    float4 pos : POSITION;
    float2 uv : TEXCOORD0;
    float3 nrm : NORMAL;
    float4 weights : WEIGHTS;
    uint4 indices : BONES;
};

struct VSOut {
    float4 svpos : SV_POSITION;
    float3 worldPos : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float2 uv : TEXCOORD2;
    float4 color : COLOR0;
};

VSOut main(VSIn v, uint instanceID : SV_InstanceID) {
    VSOut o;
    InstanceData data = gInstanceData[instanceID];
    
    float4 localPos = v.pos;
    float3 localNrm = v.nrm;

    // color.a を使ってモデルの種類を判別
    // 0.5 = Gear, 0.6 = Horizontal Pipe, 0.7 = Vertical Pipe
    float typeFlag = data.color.a;
    if (abs(typeFlag - 0.5) < 0.05) {
        // Gear: モデリングがXZ平面上なので、XY平面になるように回転 (x, z, -y)
        localPos.xyz = float3(localPos.x, localPos.z, -localPos.y);
        localNrm = float3(localNrm.x, localNrm.z, -localNrm.y);
    } else if (abs(typeFlag - 0.6) < 0.05) {
        // Horizontal Pipe: モデリングの中心が Y=2.0 付近なので原点にオフセット
        localPos.y -= 2.0;
    } else if (abs(typeFlag - 0.7) < 0.05) {
        // Vertical Pipe: 原点にオフセット後、Z軸周りに90度回転 (x'=-y, y'=x)
        localPos.y -= 2.0;
        float tempX = localPos.x;
        localPos.x = -localPos.y;
        localPos.y = tempX;
        float tempNx = localNrm.x;
        localNrm.x = -localNrm.y;
        localNrm.y = tempNx;
    }

    float4 wp = mul(localPos, data.world);
    o.worldPos = wp.xyz;
    o.normal = normalize(mul(float4(localNrm, 0), data.world).xyz);
    o.svpos = mul(mul(wp, gView), gProj);
    o.uv = v.uv;
    
    // 識別フラグとして使った alpha を 1.0 に戻してピクセルシェーダーに渡す
    o.color = float4(data.color.rgb, 1.0);
    
    return o;
}
