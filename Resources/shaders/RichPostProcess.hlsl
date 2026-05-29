Texture2D gScene : register(t0); 
Texture2D gBloom : register(t1);  // ★追加: ブルームテクスチャ
Texture2D gDepth : register(t2);  // ★追加: 深度テクスチャ (DOF用)
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
    float gSpeedLine;
};

// 定数
static const float BLOOM_THRESH = 1.0;
static const float BLOOM_INTENSITY = 0.6; // ブルーム強度

// 輝度計算
float luminance(float3 rgb) {
    return dot(rgb, float3(0.299, 0.587, 0.114));
}

// 簡単なノイズ
float hash(float2 p) { return frac(sin(dot(p, float2(12.9898,78.233))) * 43758.5453); }

float4 main(float4 svpos:SV_POSITION, float2 uv:TEXCOORD0) : SV_TARGET {
    // -----------------------------------------------------------------
    // 1. 被写界深度 (DOF) - シーンカラーをぼかす
    // -----------------------------------------------------------------
    float depth = gDepth.Sample(gSmp, uv).r;
    float nearZ = 0.1;
    float farZ = 1000.0;
    float linearDepth = nearZ * farZ / (farZ - depth * (farZ - nearZ));
    
    // 遠くのみボケるようにする（手前のオブジェクトがボケて黒ずむ/影のようになる現象を防ぐ）
    float coc = 0.0;
    if (linearDepth > gDofFocusDistance) {
        coc = saturate((linearDepth - gDofFocusDistance) / gDofFocusRange);
    }
    coc *= gDofIntensity;
    
    float2 centerOffset = uv - 0.5;
    float dist = length(centerOffset);
    float chromaPower = 0.005 + gDamageVignette * 0.05; // gDamageVignetteを使用 (元gChromaShift)
    float2 shift = centerOffset * dist * chromaPower;

    float3 baseColor = 0.0;
    if (coc > 0.001) {
        float2 texSize = float2(1.0 / 1280.0, 1.0 / 720.0);
        float2 blurOff = texSize * coc * 6.0; // ぼかし幅
        
        // クロマティックアベレーションを加味しつつ、3x3のボックス（円形近似）でぼかす
        float3 sum = 0.0;
        float weightSum = 0.0;
        
        for (int x = -1; x <= 1; x++) {
            for (int y = -1; y <= 1; y++) {
                float2 offset = float2(x, y) * blurOff;
                float w = 1.0 / (1.0 + length(float2(x, y))); // 中心に近いほど重みを大きく
                float r = gScene.Sample(gSmp, saturate(uv + offset + shift)).r;
                float g = gScene.Sample(gSmp, saturate(uv + offset)).g;
                float b = gScene.Sample(gSmp, saturate(uv + offset - shift)).b;
                sum += float3(r, g, b) * w;
                weightSum += w;
            }
        }
        baseColor = sum / weightSum;
    } else {
        float r = gScene.Sample(gSmp, saturate(uv + shift)).r;
        float g = gScene.Sample(gSmp, uv).g;
        float b = gScene.Sample(gSmp, saturate(uv - shift)).b;
        baseColor = float3(r, g, b);
    }

    // -----------------------------------------------------------------
    // 2. 新しいブルームの合成
    // -----------------------------------------------------------------
    if (gBloomIntensity > 0.0) {
        float3 bloom = gBloom.Sample(gSmp, uv).rgb;
        baseColor += bloom * gBloomIntensity;
    }

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

    // -----------------------------------------------------------------
    // 3. Vignette (周辺減光) ＆ Noise
    // -----------------------------------------------------------------
    float vig = saturate(1.0 - dot(centerOffset, centerOffset) * 1.0);
    baseColor *= lerp(0.9, 1.0, vig);
    
    // 被ダメージビネット（赤色）
    float dVig = saturate(dot(centerOffset, centerOffset) * gVignette);
    float3 redColor = float3(1.0, 0.0, 0.0);
    // gDamageVignette の値に応じて赤みを調整 (最大でも 0.8 程度に抑える)
    float damageIntensity = saturate(gDamageVignette) * 0.8;
    baseColor = lerp(baseColor, redColor, dVig * damageIntensity);
    
    // フィルムノイズ
    baseColor += (hash(uv * 1000.0 + gTime) - 0.5) * 0.03;

    return float4(baseColor, 1.0);
}
