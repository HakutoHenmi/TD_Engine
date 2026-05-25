#include "Obj.hlsli"

Texture2D<float4> tex : register(t0);
SamplerState smp : register(s0);

// ★追加: シャドウマップ
Texture2D<float> gShadowMap : register(t1);
SamplerComparisonState gShadowSmp : register(s1);

// 距離減衰計算
float AttenDist(float3 atten, float d)
{
    return 1.0 / (atten.x + atten.y * d + atten.z * d * d);
}

// ★変更: Poisson Disk Sampling を用いた高品質ソフトシャドウ
float CalcShadowPCF(float4 worldPos)
{
    float4 shadowCoord = mul(worldPos, shadowMatrix);
    shadowCoord.xyz /= shadowCoord.w;

    float2 shadowUV;
    shadowUV.x = shadowCoord.x * 0.5f + 0.5f;
    shadowUV.y = -shadowCoord.y * 0.5f + 0.5f;

    if (shadowUV.x < 0.0f || shadowUV.x > 1.0f ||
        shadowUV.y < 0.0f || shadowUV.y > 1.0f ||
        shadowCoord.z < 0.0f || shadowCoord.z > 1.0f)
        return 1.0f;

    float depth = shadowCoord.z - 0.005f;
    
    // シャドウマップ解像度 (2048x2048などを想定)
    float texelSize = 1.0f / 2048.0f;
    float spread = 3.0f; // ボケ幅（適宜調整）
    
    // 16サンプルのPoisson Disk配置
    const float2 poissonDisk[16] = {
        float2( -0.94201624, -0.39906216 ), float2( 0.94558609, -0.76890725 ),
        float2( -0.094184101, -0.92938870 ), float2( 0.34495938, 0.29387760 ),
        float2( -0.91588401, 0.45771432 ), float2( -0.81544232, -0.87912464 ),
        float2( -0.38277543, 0.27676845 ), float2( 0.97484398, 0.75648379 ),
        float2( 0.44323325, -0.97511554 ), float2( 0.53742981, -0.47373420 ),
        float2( -0.26496911, -0.41893023 ), float2( 0.79197514, 0.19090188 ),
        float2( -0.24188840, 0.99706507 ), float2( -0.81409955, 0.91437590 ),
        float2( 0.19984126, 0.78641367 ), float2( 0.14383161, -0.14100467 )
    };

    float shadow = 0.0f;
    [unroll]
    for (int i = 0; i < 16; ++i)
    {
        // オフセットを回転させる（バンディング防止）
        float s = sin(worldPos.x * 100.0f);
        float c = cos(worldPos.z * 100.0f);
        float2 rotatedOffset = float2(
            poissonDisk[i].x * c - poissonDisk[i].y * s,
            poissonDisk[i].x * s + poissonDisk[i].y * c
        );
        
        float2 offset = rotatedOffset * texelSize * spread;
        shadow += gShadowMap.SampleCmpLevelZero(gShadowSmp, shadowUV + offset, depth);
    }
    
    shadow /= 16.0f;
    return shadow;
}

float4 main(VSOutput input) : SV_TARGET
{
    // UV変換
    float2 uv = float2(
        input.uv.x * m_uv_scale.x + m_uv_offset.x,
        input.uv.y * m_uv_scale.y + m_uv_offset.y
    );
    float4 texcolor = tex.Sample(smp, uv);

    // 基本ベクトル
    float3 N = normalize(input.normal);
    float3 V = normalize(cameraPos - input.worldpos.xyz);
    
    // ベースカラー
    float3 albedo = texcolor.rgb * color.rgb;
    
    // ========================================================
    // ★追加: スチームパンク風のプロシージャル・グラウンド
    // ========================================================
    // 法線の向きによらず、色が赤茶色（r > 0.4, g < 0.35, b < 0.3）の巨大オブジェクトなら適用
    if (color.r > 0.4f && color.r < 0.5f && color.g > 0.2f && color.g < 0.35f && color.b < 0.3f)
    {
        // ワールド座標のXZ平面だけでなく、Yが立っている場合はXYやYZを使う（Zアップ対策）
        float2 wp = input.worldpos.xz;
        if (abs(N.z) > 0.8f) wp = input.worldpos.xy;
        else if (abs(N.x) > 0.8f) wp = input.worldpos.zy;
        
        // 1. 大きな鉄板のグリッド模様（目地）
        float tileSize = 0.5f; // パネルの大きさ (少し大きくした)
        float2 grid = frac(wp * tileSize);
        float lineX = smoothstep(0.0f, 0.03f, grid.x) * smoothstep(1.0f, 0.97f, grid.x);
        float lineY = smoothstep(0.0f, 0.03f, grid.y) * smoothstep(1.0f, 0.97f, grid.y);
        float lines = lineX * lineY;
        
        // 2. リベット（鉄板の四隅のボルト）
        float r1 = length(grid - float2(0.08f, 0.08f));
        float r2 = length(grid - float2(0.92f, 0.08f));
        float r3 = length(grid - float2(0.08f, 0.92f));
        float r4 = length(grid - float2(0.92f, 0.92f));
        float rivet = (r1 < 0.02f || r2 < 0.02f || r3 < 0.02f || r4 < 0.02f) ? 1.0f : 0.0f;
        
        // 3. 錆（サビ）と油汚れの簡易ノイズ
        float noise1 = frac(sin(dot(wp, float2(12.9898, 78.233))) * 43758.5453);
        float noise2 = frac(sin(dot(wp * 1.5f, float2(39.346, 11.135))) * 43758.5453);
        float rust = smoothstep(0.4f, 1.0f, (noise1 + noise2) * 0.5f);
        
        // 色の合成
        float3 darkGroove = albedo * 0.1f;     // 溝の色（暗い）
        float3 rustColor = float3(0.5f, 0.2f, 0.05f); // サビのオレンジ
        float3 rivetColor = float3(0.35f, 0.35f, 0.35f); // 鈍い銀色
        
        albedo = lerp(darkGroove, albedo, lines);        // グリッド溝を描画
        albedo = lerp(albedo, rustColor, rust * 0.5f);   // サビを乗せる
        albedo = lerp(albedo, rivetColor, rivet * lines);// リベットを描画
    }
    // ========================================================

    // アンビエント成分
    float3 finalColor = albedo * ambientColor;

    // ★追加: シャドウ計算
    float shadowFactor = CalcShadowPCF(input.worldpos);
    // 影の濃さを緩和（影の中でも完全に真っ暗にならないようにする）
    shadowFactor = lerp(0.5f, 1.0f, shadowFactor);

    // ----------------------------------------------------
    // Directional Lights (シャドウ適用)
    // ----------------------------------------------------
    for (int i = 0; i < MAX_DIR_LIGHTS; ++i)
    {
        if (dirLights[i].enabled != 0)
        {
            float3 L = normalize(-dirLights[i].direction);
            // 角度によって完全に真っ暗になるのを防ぐためラップライティング
            float NdotL = saturate(dot(N, L) * 0.8f + 0.2f);
            
            float3 H = normalize(L + V);
            float spec = pow(saturate(dot(N, H)), 32.0f);

            float3 diffuse = albedo * NdotL;
            float3 specular = m_specular * spec;

            // ★変更: シャドウファクターを乗算
            finalColor += (diffuse + specular) * dirLights[i].color * shadowFactor;
        }
    }

    // ----------------------------------------------------
    // Point Lights
    // ----------------------------------------------------
    for (int j = 0; j < MAX_POINT_LIGHTS; ++j)
    {
        if (pointLights[j].enabled != 0)
        {
            float3 Lvec = pointLights[j].position - input.worldpos.xyz;
            float d = length(Lvec);
            if (d < pointLights[j].range)
            {
                float3 L = Lvec / max(d, 1e-5);
                float att = AttenDist(pointLights[j].atten, d);
                
                // 境界で急に光が消えないよう滑らかにフェードアウト
                float rangeFade = smoothstep(pointLights[j].range, pointLights[j].range * 0.2f, d);
                att *= rangeFade;

                // 角度によって完全に真っ暗になるのを防ぐためラップライティング
                float NdotL = saturate(dot(N, L) * 0.8f + 0.2f);
                float3 H = normalize(L + V);
                
                // ハイライトが床で一直線に伸びる不自然さを解消するため、PointLightのスペキュラを大きく抑える
                float spec = pow(saturate(dot(N, H)), 16.0f); 

                float3 diffuse = albedo * NdotL;
                float3 specular = m_specular * spec * 0.05f; // スペキュラ強度を大幅に落とす

                finalColor += (diffuse + specular) * pointLights[j].color * att;
            }
        }
    }

    // ----------------------------------------------------
    // Spot Lights
    // ----------------------------------------------------
    for (int k = 0; k < MAX_SPOT_LIGHTS; ++k)
    {
        if (spotLights[k].enabled != 0)
        {
            float3 Lvec = spotLights[k].position - input.worldpos.xyz;
            float d = length(Lvec);
            if (d < spotLights[k].range)
            {
                float3 L = Lvec / max(d, 1e-5);
                float cosAng = dot(L, normalize(spotLights[k].direction));
                float ang = smoothstep(spotLights[k].outerCos, spotLights[k].innerCos, cosAng);
                float att = AttenDist(spotLights[k].atten, d);

                // 角度によって完全に真っ暗になるのを防ぐためラップライティング
                float NdotL = saturate(dot(N, L) * 0.8f + 0.2f);
                float3 H = normalize(L + V);
                float spec = pow(saturate(dot(N, H)), 32.0f);

                float3 diffuse = albedo * NdotL;
                float3 specular = m_specular * spec;

                finalColor += (diffuse + specular) * spotLights[k].color * att * ang;
            }
        }
    }

    return float4(finalColor, texcolor.a * color.a);
}