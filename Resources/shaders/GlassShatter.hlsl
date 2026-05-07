// GlassShatter.hlsl - 高光沢・環境反射シャード
cbuffer CBFrame : register(b0) { row_major float4x4 gView; row_major float4x4 gProj; row_major float4x4 gViewProj; float3 gCamPos; float gTime; };
cbuffer CBObj : register(b1) { row_major float4x4 gWorld; float4 gColor; };

struct InstanceData { row_major float4x4 world; float4 color; float4 uvScaleOffset; };
StructuredBuffer<InstanceData> gInstanceData : register(t2);

struct VSIn { float4 pos : POSITION; float2 uv : TEXCOORD0; float3 nrm : NORMAL; float4 weights : WEIGHTS; uint4 indices : BONES; };
struct VSOut { float4 svpos : SV_POSITION; float4 clipPos : TEXCOORD0; float2 uv : TEXCOORD1; float4 color : COLOR0; float3 worldNrm : NORMAL; float3 worldPos : TEXCOORD2; };

VSOut main(VSIn v, uint instanceID : SV_InstanceID) {
    VSOut o; float4x4 world = gWorld; float4 color = gColor;
    if (instanceID < 1024) { world = gInstanceData[instanceID].world; color = gInstanceData[instanceID].color; }
    float4 wp = mul(v.pos, world);
    o.svpos = mul(wp, gViewProj); o.clipPos = o.svpos; o.uv = v.uv; o.color = color;
    o.worldNrm = normalize(mul(v.nrm, (float3x3)world)); o.worldPos = wp.xyz;
    return o;
}

float hash1(float2 p) { return frac(sin(dot(p, float2(12.9898, 78.233))) * 43758.5453); }

float4 ps_main(VSOut i) : SV_TARGET {
    float seed = i.color.r * 17.31;
    float intensity = min(i.color.a, 1.0);
    float2 cuv = i.uv - 0.5;
    
    // 鋭利な破片マスク
    float aspect = 0.15 + hash1(float2(seed, 0.5)) * 0.3;
    float2 stUV = cuv; stUV.x /= aspect;
    float distStr = length(stUV);
    float numVerts = 3.0 + floor(frac(seed * 0.91) * 1.5);
    float polyAngle = 6.28318 / numVerts;
    float sector = fmod(atan2(stUV.y, stUV.x) + seed, polyAngle);
    float polyDist = (0.38 + hash1(float2(seed * 1.3, 0.7)) * 0.1) * cos(polyAngle * 0.5) / max(cos(sector - polyAngle * 0.5), 0.001);
    if (smoothstep(polyDist, polyDist - 0.015, distStr) < 0.01) discard;

    // =============================================
    // 鏡面反射 (擬似環境マップ)
    // =============================================
    float3 viewDir = normalize(gCamPos - i.worldPos);
    float3 reflDir = reflect(-viewDir, i.worldNrm);
    
    // 擬似空・地反射
    float3 skyColor = float3(0.4, 0.8, 1.2);    // 明るいネオン空
    float3 groundColor = float3(0.05, 0.1, 0.2); // 暗い底面
    float horizon = smoothstep(-0.2, 0.2, reflDir.y);
    float3 envColor = lerp(groundColor, skyColor, horizon);
    
    // フネル反射
    float fresnel = pow(1.0 - saturate(dot(viewDir, i.worldNrm)), 4.0);
    
    // ネオンカラー・グレーズ
    float3 neonBlue = float3(0.1, 0.5, 1.0);
    float3 magenta  = float3(1.0, 0.2, 0.8);
    float3 baseColor = lerp(neonBlue, magenta, horizon * 0.4 + hash1(float2(seed, 1.0)) * 0.3);
    
    // 反射光の合成
    float3 finalColor = baseColor * 0.5 + envColor * (0.5 + fresnel * 1.5);
    
    // 鋭いハイライト（太陽のような光源）
    float spec = pow(saturate(dot(reflDir, normalize(float3(0.5, 1.0, 0.3)))), 128.0);
    finalColor += float3(1, 1, 1) * spec * 5.0;
    
    // エッジ発光
    float edge = 1.0 - smoothstep(polyDist, polyDist - 0.06, distStr);
    finalColor += lerp(neonBlue, magenta, frac(seed * 2.0)) * edge * 4.0 * intensity;

    return float4(finalColor, 1.0 * intensity);
}
