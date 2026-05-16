Texture2D gScene : register(t0); 
Texture2D gBloom : register(t1);  // ★追加: ブルームテクスチャ
Texture2D gDepth : register(t2);  // ★追加: 深度テクスチャ (DOF用)
SamplerState gSmp : register(s0);

cbuffer CBPost : register(b0) { 
    float gTime; 
    float gNoiseStrength; 
    float gDistortion; 
    float gChromaShift; 
    float gVignette; 
    float gScanline; 
    float gSan;
    float gBloomIntensity;    // ★追加: ブルーム強度
    float gDofFocusDistance;  // ★追加: DOFフォーカス距離
    float gDofFocusRange;     // ★追加: DOFピント範囲
    float gDofIntensity;      // ★追加: DOFぼかし強度
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
    float chromaPower = 0.005 + gChromaShift;
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

    // -----------------------------------------------------------------
    // 3. Vignette (周辺減光) ＆ Noise
    // -----------------------------------------------------------------
    float vig = saturate(1.0 - dot(centerOffset, centerOffset) * 1.0);
    baseColor *= lerp(0.9, 1.0, vig);
    
    // 被ダメージビネット（赤色）
    float dVig = saturate(dot(centerOffset, centerOffset) * gVignette);
    float3 redColor = float3(1.0, 0.0, 0.0);
    baseColor = lerp(baseColor, redColor, dVig * 0.9);
    
    // フィルムノイズ
    baseColor += (hash(uv * 1000.0 + gTime) - 0.5) * 0.03;

    return float4(baseColor, 1.0);
}
