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
    
    // 両面表示対応：裏面（視線の逆向き）を見ている場合は法線を反転する
    if (dot(normal, viewDir) < 0.0) {
        normal = -normal;
    }
    
    // 2. フレネル計算
    float NdotV = saturate(dot(normal, viewDir));
    float fresnel = smoothstep(0.2, 0.35, 1.0 - NdotV);
    
    // --- 基本パラメータ ---
    float3 baseColor = i.color.rgb;
    float baseAlpha = i.color.a;
    
    // プリズムモード判定: alpha 0.5~0.8 の場合にプリズム効果を強調
    float prismStrength = smoothstep(0.4, 0.6, baseAlpha) * smoothstep(0.9, 0.75, baseAlpha);
    
    // 画面空間のUV座標を計算
    float2 screenUV = i.clipPos.xy / i.clipPos.w;
    screenUV.x = screenUV.x * 0.5 + 0.5;
    screenUV.y = -screenUV.y * 0.5 + 0.5;

    // ワールド法線をビュー空間（画面空間）に投影して、歪みの方向を決定
    float3 viewNormal = mul(normal, (float3x3)gView); 
    float distortionStrength = 0.04 * baseAlpha;
    
    // === プリズム効果: 色収差 (Chromatic Aberration) ===
    // R, G, B チャンネルそれぞれを異なる屈折率でサンプリングし、虹色の分光を再現
    float aberrationScale = (0.012 + prismStrength * 0.025) * baseAlpha;
    float2 uvR = clamp(screenUV + viewNormal.xy * (distortionStrength + aberrationScale * 1.0), 0.001, 0.999);
    float2 uvG = clamp(screenUV + viewNormal.xy * (distortionStrength), 0.001, 0.999);
    float2 uvB = clamp(screenUV + viewNormal.xy * (distortionStrength - aberrationScale * 1.0), 0.001, 0.999);
    
    float3 refractionColor;
    refractionColor.r = gBackdropTex.SampleLevel(gSmp, uvR, 0).r;
    refractionColor.g = gBackdropTex.SampleLevel(gSmp, uvG, 0).g;
    refractionColor.b = gBackdropTex.SampleLevel(gSmp, uvB, 0).b;
    
    // --- 色の合成 ---
    // 内部: 屈折した背景色にベースカラーを乗算
    float3 innerColor = refractionColor * saturate(baseColor * 0.8) + (baseColor * 0.2);
    
    // === プリズム・フレネル: エッジにスペクトル虹色を生成 ===
    // 視線角度に応じて虹色のグラデーションを計算（薄膜干渉風）
    float spectrumPhase = NdotV * 6.28318 * 2.0 + gTime * 0.5;
    float3 spectrumColor = float3(
        0.5 + 0.5 * sin(spectrumPhase),
        0.5 + 0.5 * sin(spectrumPhase + 2.094),
        0.5 + 0.5 * sin(spectrumPhase + 4.189)
    );
    
    // プリズムモードのとき: エッジに虹色 / 通常モード: 従来の暖色系エッジ
    float3 warmEdge = baseColor * 1.5 + float3(0.6, 0.4, 0.1);
    float3 prismEdge = spectrumColor * 2.5 + baseColor * 0.5;
    float3 edgeColor = lerp(warmEdge, prismEdge, prismStrength);
    
    // フレネルで内部とエッジを合成
    float3 finalRGB = lerp(innerColor, edgeColor, fresnel);
    
    // 4. シャープなハイライト (Specular) で硬質なガラス感を強調
    float3 lightDir = normalize(float3(0.5, 1.0, 0.3));
    float3 halfVector = normalize(lightDir + viewDir);
    float NdotH = saturate(dot(normal, halfVector));
    float specular = step(0.98, NdotH); 
    
    // プリズムモードでは虹色スペキュラ、通常は暖色系
    float3 specWarm = baseColor * 0.5 + float3(0.7, 0.6, 0.3);
    float3 specPrism = spectrumColor * 1.5 + float3(0.8, 0.8, 0.8);
    finalRGB += lerp(specWarm, specPrism, prismStrength) * specular;
    
    // 不透明度は1.0（背景は既に屈折して取り込んでいるため）
    return float4(finalRGB, 1.0);
}
