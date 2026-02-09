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
    // 時間でUVを揺らす（ヒートヘイズ処理）
    float2 distUV = input.uv;
    float strength = 0.02f; // 歪みの強さ
    float speed = 5.0f; // 揺れの速さ
    
    // sin波でUVをずらす
    distUV.x += sin(distUV.y * 20.0f + time * speed) * strength;
    distUV.y += cos(distUV.x * 20.0f + time * speed) * strength;

    // テクスチャサンプリング
    float4 texcolor = tex.Sample(smp, distUV);

    // ライティング計算
    float3 N = normalize(input.normal);
    float3 V = normalize(cameraPos - input.worldpos.xyz);
    float3 albedo = texcolor.rgb * color.rgb;
    float3 finalColor = albedo * ambientColor; // 環境光

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
            float spec = pow(saturate(dot(N, H)), 10.0f); // 簡易スペキュラ
            
            float3 diffuse = albedo * NdotL;
            float3 specular = float3(spec, spec, spec) * 0.5f;

            finalColor += (diffuse + specular) * spotLight.color * att * ang;
        }
    }

    return float4(finalColor, texcolor.a * color.a);
}