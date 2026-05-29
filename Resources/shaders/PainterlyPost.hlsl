Texture2D gScene : register(t0);
Texture2D gBloom : register(t1);
Texture2D gDepth : register(t2);
SamplerState gSmp : register(s0);

cbuffer CBPost : register(b0) {
    float gTime;
    float gNoiseStrength;
    float gDistortion;
    float gDamageVignette; // chromaShift->damageVignette
    float gVignette;
    float gScanline;
    float gSan;
    float gBloomIntensity;
    float gDofFocusDistance;
    float gDofFocusRange;
    float gDofIntensity;

    // フォグパラメータ
    float gFogDensity;
    float gFogStart;
    float gFogEnd;
    float gFogHeightFalloff;

    // カメラ・FXAA
    float gNearPlane;
    float gFarPlane;
    float gFxaaEnabled;
    float gExposure;

    // フォグ色
    float gFogColorR;
    float gFogColorG;
    float gFogColorB;
    float gPad0;

    // モードボーダー
    float gPrepModeBorder;
    float gDeleteModeBorder;
    float gRadialBlur;
    float gPad2;
};

// 乱数生成
float hash(float2 p) {
    return frac(sin(dot(p, float2(12.9898, 78.233))) * 43758.5453);
}

// 簡単なKuwaharaフィルター（被写界深度による滲み対応）
float3 KuwaharaFilter(float2 uv, float coc) {
    const int R = 3; // 筆のタッチの大きさ
    const float n = float((R + 1) * (R + 1));
    float2 texSize = float2(1920.0, 1080.0);
    float2 texel = 1.0 / texSize;

    // 被写界深度（CoC）に応じて、サンプリング半径を拡大して筆跡を大きく滲ませる（奥ほど水彩画のようにぼかす）
    texel *= (1.0 + coc * 12.0);

    float3 means[4] = { float3(0,0,0), float3(0,0,0), float3(0,0,0), float3(0,0,0) };
    float3 vars[4]  = { float3(0,0,0), float3(0,0,0), float3(0,0,0), float3(0,0,0) };

    // 4つの象限ごとに平均値(Mean)と分散(Variance)を計算
    for (int j = -R; j <= 0; ++j) {
        for (int i = -R; i <= 0; ++i) {
            float3 c = gScene.SampleLevel(gSmp, uv + float2(i, j) * texel, 0).rgb;
            means[0] += c; vars[0] += c * c;
        }
    }
    for (int j = -R; j <= 0; ++j) {
        for (int i = 0; i <= R; ++i) {
            float3 c = gScene.SampleLevel(gSmp, uv + float2(i, j) * texel, 0).rgb;
            means[1] += c; vars[1] += c * c;
        }
    }
    for (int j = 0; j <= R; ++j) {
        for (int i = 0; i <= R; ++i) {
            float3 c = gScene.SampleLevel(gSmp, uv + float2(i, j) * texel, 0).rgb;
            means[2] += c; vars[2] += c * c;
        }
    }
    for (int j = 0; j <= R; ++j) {
        for (int i = -R; i <= 0; ++i) {
            float3 c = gScene.SampleLevel(gSmp, uv + float2(i, j) * texel, 0).rgb;
            means[3] += c; vars[3] += c * c;
        }
    }

    float minVar = 1e30;
    float3 finalColor = float3(0,0,0);
    for (int k = 0; k < 4; ++k) {
        means[k] /= n;
        vars[k] = abs(vars[k] / n - means[k] * means[k]);
        float totalVar = vars[k].r + vars[k].g + vars[k].b;
        if (totalVar < minVar) {
            minVar = totalVar;
            finalColor = means[k];
        }
    }

    return finalColor;
}

// 手書き風アウトライン（Sobel + Wobble）
float GetWobbleEdge(float2 uv) {
    float2 texSize = float2(1920.0, 1080.0);
    float2 texel = 1.0 / texSize;

    // UVを時間とノイズで少し揺らす（手書き風の歪み）
    float wobbleOffset = (hash(uv * 10.0 + gTime * 0.1) - 0.5) * 2.0;
    float2 wobbleUV = uv + float2(sin(gTime * 5.0 + uv.y * 50.0), cos(gTime * 5.0 + uv.x * 50.0)) * texel * 1.5 * wobbleOffset;

    // ★追加: 背景（空など無限遠）の場合はエッジを検出しないようにバイパス
    float centerDepth = gDepth.SampleLevel(gSmp, wobbleUV, 0).r;
    if (centerDepth >= 0.999f) {
        return 0.0;
    }

    float nearZ = 0.1;
    float farZ = 1000.0;

    // 簡易的なSobelフィルタ（クロス抽出）
    float d0 = nearZ * farZ / (farZ - gDepth.SampleLevel(gSmp, wobbleUV + float2(-texel.x, 0), 0).r * (farZ - nearZ));
    float d1 = nearZ * farZ / (farZ - gDepth.SampleLevel(gSmp, wobbleUV + float2( texel.x, 0), 0).r * (farZ - nearZ));
    float d2 = nearZ * farZ / (farZ - gDepth.SampleLevel(gSmp, wobbleUV + float2(0, -texel.y), 0).r * (farZ - nearZ));
    float d3 = nearZ * farZ / (farZ - gDepth.SampleLevel(gSmp, wobbleUV + float2(0,  texel.y), 0).r * (farZ - nearZ));

    float edge = (abs(d1 - d0) + abs(d3 - d2)) / max(d0, 0.1);
    // エッジの閾値（距離に対する比率。5%以上の距離差があればエッジとする）
    return step(0.05, edge); 
}

float4 main(float4 svpos : SV_POSITION, float2 uv : TEXCOORD0) : SV_TARGET {
    // -----------------------------------------------------------------
    // 被写界深度 (DOF) の錯乱円 (CoC) を計算
    // -----------------------------------------------------------------
    float depth = gDepth.SampleLevel(gSmp, uv, 0).r;
    float nearZ = 0.1;
    float farZ = 1000.0;
    float linearDepth = nearZ * farZ / (farZ - depth * (farZ - nearZ));
    
    float coc = 0.0;
    if (linearDepth > gDofFocusDistance) {
        coc = saturate((linearDepth - gDofFocusDistance) / gDofFocusRange);
    }
    coc *= gDofIntensity;

    // 1. Kuwaharaフィルターによる絵画調ベースカラー（CoCによる滲み込み対応）
    float3 baseColor = KuwaharaFilter(uv, coc);

    // 2. ブルームの加算
    if (gBloomIntensity > 0.0) {
        float3 bloom = gBloom.SampleLevel(gSmp, uv, 0).rgb;
        baseColor += bloom * gBloomIntensity;
    }

    // 3. 手書き風エッジの検出と合成
    float edge = GetWobbleEdge(uv);
    // エッジ部分を少し暗い紫/茶色っぽい色にして馴染ませる
    float3 edgeColor = float3(0.2, 0.15, 0.25);
    baseColor = lerp(baseColor, baseColor * edgeColor, edge * 0.8);

    // ★追加: ラジアルブラー (ブースト時用)
    if (gRadialBlur > 0.001) {
        float2 dir = 0.5 - uv;
        float3 blurColor = 0.0;
        float totalWeight = 0.0;
        for (int i = 0; i < 6; ++i) {
            float weight = 1.0 - (float)i / 6.0;
            float2 sampleUV = saturate(uv + dir * (float)i * gRadialBlur);
            blurColor += gScene.Sample(gSmp, sampleUV).rgb * weight;
            totalWeight += weight;
        }
        baseColor = lerp(baseColor, blurColor / totalWeight, saturate(gRadialBlur * 10.0));
    }

    // ★追加: 風/スピードエフェクト (より自然でしつこくない表現)
    float windIntensity = gSpeedLine + gRadialBlur * 10.0; 
    if (windIntensity > 0.001) {
        float2 d = uv - 0.5;
        float r = length(d);
        float angle = atan2(d.y, d.x); 
        
        // ソフトな流線ノイズ
        float noise1 = sin(angle * 15.0 + gTime * 3.0);
        float noise2 = sin(angle * 40.0 - gTime * 8.0);
        float lineNoise = smoothstep(0.2, 1.0, (noise1 + noise2) * 0.5);
        
        // 高速で手前に流れる動き
        float flow = frac(r * 4.0 - gTime * 25.0 + lineNoise);
        flow = smoothstep(0.5, 1.0, flow);
        
        // 画面端のみ
        float mask = smoothstep(0.25, 0.7, r);
        float windAlpha = lineNoise * flow * mask * windIntensity * 0.15;
        
        // 風っぽい淡い青白さ（加算合成）
        baseColor += float3(0.6, 0.8, 1.0) * windAlpha;
    }

    // 4. キャンバス/筆跡ノイズのオーバーレイ
    // 画面全体に薄くノイズを乗せる
    float noise = hash(uv * 500.0 + gTime * 0.5);
    baseColor *= lerp(0.95, 1.05, noise);

    // 5. Vignette (周辺減光) 
    float2 centerOffset = uv - 0.5;
    float vig = saturate(1.0 - dot(centerOffset, centerOffset) * 1.2);
    baseColor *= lerp(0.8, 1.0, vig);

    // 6. UIボーダー (準備フェーズ・削除モード用)
    if (gPrepModeBorder > 0.5) {
        float borderThickness = 0.015; // 縁の太さ
        float2 texSize = float2(1920.0, 1080.0);
        float2 aspect = float2(1.0, texSize.y / texSize.x);
        
        float2 absUv = abs(uv - 0.5) * 2.0; 
        
        if (absUv.x > 1.0 - borderThickness || absUv.y > 1.0 - borderThickness / aspect.y) {
            if (gDeleteModeBorder > 0.5) {
                // 赤いシマシマ模様 (斜め)
                float stripe = sin((uv.x + uv.y) * 200.0 - gTime * 10.0);
                if (stripe > 0.0) {
                    baseColor = float3(1.0, 0.1, 0.1);
                } else {
                    baseColor = float3(0.7, 0.05, 0.05);
                }
            } else {
                // 準備フェーズの通常の枠
                baseColor = float3(0.05, 0.05, 0.08); // 黒っぽい色
            }
        }
    }

    return float4(baseColor, 1.0);
}
