#include "Obj.hlsli"

Texture2D<float> gDepthMap : register(t0); // 深度バッファ
SamplerState gSmp : register(s0);

// 乱数生成用ハッシュ関数
float rand(float2 co) {
    return frac(sin(dot(co.xy, float2(12.9898, 78.233))) * 43758.5453);
}

// 深度からView空間の座標を復元
float3 GetViewPos(float2 uv) {
    float z = gDepthMap.SampleLevel(gSmp, uv, 0);
    // NDC座標 [-1, 1]
    float4 clipSpacePos = float4(uv.x * 2.0 - 1.0, (1.0 - uv.y) * 2.0 - 1.0, z, 1.0);
    
    // invViewProj を使ってWorldに戻す
    float4 worldPos = mul(clipSpacePos, invViewProj);
    worldPos /= worldPos.w;
    return worldPos.xyz;
}

// 深度からワールド座標と法線を計算して SSAO を求める
float4 main(float4 svpos : SV_POSITION, float2 uv : TEXCOORD0) : SV_TARGET
{
    float depth = gDepthMap.SampleLevel(gSmp, uv, 0);

    // 空（背景）の場合は遮蔽なし
    if (depth >= 1.0f) {
        return float4(1.0f, 1.0f, 1.0f, 1.0f);
    }

    float3 worldPos = GetViewPos(uv);
    
    // 簡易的な法線の復元 (偏微分を利用)
    float3 ddxPos = ddx(worldPos);
    float3 ddyPos = ddy(worldPos);
    float3 N = normalize(cross(ddxPos, ddyPos));
    
    // SSAOパラメータ (★接地感を出すため大幅に強化)
    const int SAMPLES = 16; 
    const float RADIUS = 1.5f;     // サンプリング半径を広げ、大きな暗がりを作る
    const float BIAS = 0.01f;      // 微小な深度差を除外
    const float NORMAL_BIAS = 0.03f; // 自己遮蔽防止（小さくして接地面に密着させる）
    const float INTENSITY = 4.5f;  // 強度を上げて真っ黒に近い影を落とす
    
    float occlusion = 0.0f;
    
    // ランダムなベクトルを生成するためのシード
    float2 randomSeed = uv * 100.0f;
    float3 up = abs(N.z) < 0.999 ? float3(0, 0, 1) : float3(1, 0, 0);
    float3 tangent = normalize(cross(up, N));
    float3 bitangent = cross(N, tangent);
    
    for (int i = 0; i < SAMPLES; ++i) {
        // 重いsinベースの乱数を避け、事前定義された定数か簡易計算にする手もあるが、
        // サンプル数が半分なので十分軽量化される。
        float r1 = rand(randomSeed + float(i) * 0.1f);
        float r2 = rand(randomSeed + float(i) * 0.2f);
        
        float phi = 2.0 * 3.14159265 * r1;
        float cosTheta = 1.0 - r2;
        float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
        
        float3 hemisphereDir = float3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
        float3 sampleDir = tangent * hemisphereDir.x + bitangent * hemisphereDir.y + N * hemisphereDir.z;
        
        // サンプリングの起点を法線方向に少し浮かせて自己遮蔽を防ぐ
        float3 samplePos = (worldPos + N * NORMAL_BIAS) + sampleDir * RADIUS;
        
        float4 offsetPos = mul(float4(samplePos, 1.0f), viewProj);
        offsetPos /= offsetPos.w;
        float2 sampleUV = float2(offsetPos.x * 0.5f + 0.5f, 1.0f - (offsetPos.y * 0.5f + 0.5f));
        
        if(sampleUV.x < 0.0 || sampleUV.x > 1.0 || sampleUV.y < 0.0 || sampleUV.y > 1.0)
            continue;
            
        float sampleDepth = gDepthMap.SampleLevel(gSmp, sampleUV, 0);
        // ★最適化: ワールドに戻すのではなく、Z値だけで簡易判定し、mulを1回削る
        // 本来は正確な距離が必要だが、SSAOであれば深度差だけでも近似できる
        float depthDiff = offsetPos.z - sampleDepth;
        if (depthDiff > BIAS && depthDiff < RADIUS * 0.1f) {
             // 距離による減衰を簡易的に
             float rangeCheck = smoothstep(0.0, 1.0, RADIUS / (depthDiff * 100.0f + 0.1f));
             occlusion += 1.0 * rangeCheck;
        }
    }
    
    // 強調しつつ、真っ黒にならないよう制限 (ある程度の黒さは許容して重厚感を出す)
    occlusion = 1.0 - (occlusion / (float)SAMPLES) * INTENSITY;
    occlusion = clamp(occlusion, 0.1f, 1.0f);
    
    return float4(occlusion, occlusion, occlusion, 1.0f);
}
