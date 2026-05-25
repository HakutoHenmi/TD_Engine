Texture2D<float> gSSAO : register(t0);
SamplerState gSmp : register(s0);

float4 main(float4 svpos : SV_POSITION, float2 uv : TEXCOORD0) : SV_TARGET
{
    // テクスチャサイズ (解像度が1920x1080などの場合)
    // ここでは概算として固定のオフセットを使用、あるいはTextureのGetDimensionsを使用
    uint width, height;
    gSSAO.GetDimensions(width, height);
    float2 texelSize = 1.0f / float2(width, height);
    
    float result = 0.0f;
    // ★最適化: 7x7 (49回) から 3x3 (9回) に激減させてGPU負荷を大幅カット
    const int blurSize = 1; 
    float weightSum = 0.0f;
    
    for (int x = -blurSize; x <= blurSize; ++x) {
        for (int y = -blurSize; y <= blurSize; ++y) {
            // テクセルサイズを少し広げて、少ないサンプル数でも広範囲をぼかせるようにする
            float2 offset = float2(x, y) * texelSize * 1.5f;
            float ssaoValue = gSSAO.SampleLevel(gSmp, uv + offset, 0);
            
            // 簡易ガウシアンウェイト
            float weight = 1.0f / (1.0f + sqrt(float(x*x + y*y)));
            result += ssaoValue * weight;
            weightSum += weight;
        }
    }
    
    result /= weightSum;
    
    return float4(result, result, result, 1.0f);
}
