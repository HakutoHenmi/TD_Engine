// GlassShatter.hlsl - 3Dクリスタル用 フレネル・半透明・HDRシェーダー
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
Texture2D gBackdropTex : register(t0); // 画面の背景キャプチャ
SamplerState gSmp : register(s0);

float4 ps_main(VSOut i) : SV_TARGET {
    // 1. 視線ベクトルと法線の計算
    float3 viewDir = normalize(gCamPos - i.worldPos);
    float3 normal = normalize(i.worldNrm);
    
    // 2. セルルック調（アニメ調）の鋭いフレネル
    float NdotV = saturate(dot(normal, viewDir));
    
    // 鳴潮などのアニメ調クリスタルは、境界線がクッキリ発光するのが特徴
    // 緩やかなグラデーションではなく、smoothstepでパキッとしたエッジを作る
    float fresnel = smoothstep(0.2, 0.35, 1.0 - NdotV);
    
    // --- 屈折 (Refraction) の計算 ---
    float3 baseColor = i.color.rgb;
    float baseAlpha = i.color.a;
    
    // 画面空間のUV座標を計算
    float2 screenUV = i.clipPos.xy / i.clipPos.w;
    screenUV.x = screenUV.x * 0.5 + 0.5;
    screenUV.y = -screenUV.y * 0.5 + 0.5;

    // ワールド法線をビュー空間（画面空間）に投影して、歪みの方向を決定
    float3 viewNormal = mul(normal, (float3x3)gView); 
    
    // アニメ調の屈折：歪みすぎるとノイズになるので、一定の強さで面ごとにパキッと歪ませる
    float distortionStrength = 0.04 * baseAlpha;
    float2 distortedUV = clamp(screenUV + viewNormal.xy * distortionStrength, 0.001, 0.999);
    
    // 背景色を取得
    float3 refractionColor = gBackdropTex.SampleLevel(gSmp, distortedUV, 0).rgb;
    
    // --- 色の合成 (AAAアニメ調・白飛び抑制) ---
    // 内部: 屈折した背景色にベースカラーを乗算
    float3 innerColor = refractionColor * saturate(baseColor * 0.8) + (baseColor * 0.2);
    
    // エッジ (輪郭): 真鍮や高熱の金属を思わせる暖色（オレンジ/ブラウン系）のハイライトを加算
    float3 edgeColor = baseColor * 1.5 + float3(0.6, 0.4, 0.1); 
    
    // フレネルで内部とエッジを合成
    float3 finalRGB = lerp(innerColor, edgeColor, fresnel);
    
    // 4. シャープなハイライト (Specular) で硬質なガラス感を強調
    float3 lightDir = normalize(float3(0.5, 1.0, 0.3));
    float3 halfVector = normalize(lightDir + viewDir);
    float NdotH = saturate(dot(normal, halfVector));
    
    // アニメ調なので、ハイライトもグラデーションではなく2値化（step）する
    float specular = step(0.98, NdotH); 
    // スチームパンクの金属や琥珀が光を反射したような、少し温かみのある白〜黄色の輝き
    finalRGB += (baseColor * 0.5 + float3(0.7, 0.6, 0.3)) * specular;
    
    // 不透明度は1.0（背景は既に屈折して取り込んでいるため）
    return float4(finalRGB, 1.0);
}
