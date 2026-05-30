#include "Obj.hlsli"

Texture2D<float4> tex : register(t0);
SamplerState smp : register(s0);

Texture2D<float> gShadowMap : register(t1);
SamplerComparisonState gShadowSmp : register(s1);

// 距離減衰計算
float AttenDist(float3 atten, float d)
{
    return 1.0 / (atten.x + atten.y * d + atten.z * d * d);
}

// 高品質ソフトシャドウ
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
    float texelSize = 1.0f / 2048.0f;
    float spread = 3.0f;
    
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
    float2 uv = float2(
        input.uv.x * m_uv_scale.x + m_uv_offset.x,
        input.uv.y * m_uv_scale.y + m_uv_offset.y
    );
    float4 texcolor = tex.Sample(smp, uv);

    float3 N = normalize(input.normal);
    float3 V = normalize(cameraPos - input.worldpos.xyz);
    
    float3 albedo = texcolor.rgb * color.rgb;
    
    // マットな質感を出すために環境光の比率を少し上げる
    float3 finalColor = albedo * ambientColor * 1.2f;

    float shadowFactor = CalcShadowPCF(input.worldpos);
    shadowFactor = lerp(0.5f, 1.0f, shadowFactor);

    // Directional Lights
    for (int i = 0; i < MAX_DIR_LIGHTS; ++i)
    {
        if (dirLights[i].enabled != 0)
        {
            float3 L = normalize(-dirLights[i].direction);
            // ハーフランバートで柔らかい陰影
            float NdotL = dot(N, L) * 0.5f + 0.5f;
            float3 diffuse = albedo * NdotL;
            // スペキュラなし
            finalColor += diffuse * dirLights[i].color * shadowFactor;
        }
    }

    // Point Lights
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
                float rangeFade = smoothstep(pointLights[j].range, pointLights[j].range * 0.2f, d);
                att *= rangeFade;
                float NdotL = dot(N, L) * 0.5f + 0.5f;
                float3 diffuse = albedo * NdotL;
                finalColor += diffuse * pointLights[j].color * att;
            }
        }
    }

    // Spot Lights
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
                float NdotL = dot(N, L) * 0.5f + 0.5f;
                float3 diffuse = albedo * NdotL;
                finalColor += diffuse * spotLights[k].color * att * ang;
            }
        }
    }

    return float4(finalColor, texcolor.a * color.a);
}
