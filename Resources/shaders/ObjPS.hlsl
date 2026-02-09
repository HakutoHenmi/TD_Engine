#include "Obj.hlsli"

Texture2D<float4> tex : register(t0);
SamplerState smp : register(s0);

// 距離減衰計算
float AttenDist(float3 atten, float d)
{
    return 1.0 / (atten.x + atten.y * d + atten.z * d * d);
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
    
    // ベースカラー（テクスチャ * オブジェクト色 * アンビエント）
    float3 albedo = texcolor.rgb * color.rgb;
    float3 finalColor = albedo * ambientColor;

    // スポットライト計算
    if (spotLight.enabled != 0)
    {
        float3 Lvec = spotLight.position - input.worldpos.xyz;
        float d = length(Lvec);
        
        if (d < spotLight.range)
        {
            float3 L = Lvec / max(d, 1e-5);
            float cosAng = dot(L, normalize(spotLight.direction));
            float ang = smoothstep(spotLight.outerCos, spotLight.innerCos, cosAng);
            float att = AttenDist(spotLight.atten, d);

            float NdotL = saturate(dot(N, L));
            float3 H = normalize(L + V);
            float spec = pow(saturate(dot(N, H)), 32.0f); // Shininess 32.0
            
            float3 diffuse = albedo * NdotL;
            float3 specular = float3(spec, spec, spec) * m_specular;

            finalColor += (diffuse + specular) * spotLight.color * att * ang;
        }
    }

    return float4(finalColor, texcolor.a * color.a);
}