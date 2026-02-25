#include "Obj.hlsli"

Texture2D<float4> tex : register(t0);
SamplerState smp : register(s0);

float4 main(VSOutput input) : SV_TARGET
{
    // 1. テクスチャとオブジェクト色の取得
    float4 texColor = tex.Sample(smp, input.uv);
    float3 baseColor = texColor.rgb * color.rgb;

    // 2. 光の計算準備
    float3 N = normalize(input.normal); // 法線
    float3 L = normalize(float3(1.0, 1.0, -1.0)); // 仮のライト方向 (右上前から)
    float3 lightColor = float3(1.0, 1.0, 1.0); // ライトの色

    // スポットライトが有効なら、その位置を使用する
    if (dirLights[0].enabled != 0)
    {
        L = normalize(-dirLights[0].direction);
        lightColor = dirLights[0].color;
    }

    // 3. 拡散反射 (NとLの内積) - 光の当たり具合 (0.0 ～ 1.0)
    float diffuse = saturate(dot(N, L));

    // ★トゥーン処理の核: 光の強さを階段状にする
    float toonDiffuse = 0.0f;
    if (diffuse > 0.6f)
    {
        toonDiffuse = 1.0f; // 明るい
    }
    else if (diffuse > 0.2f)
    {
        toonDiffuse = 0.6f; // 普通（影の境目）
    }
    else
    {
        toonDiffuse = 0.3f; // 暗い（影）
    }

    // 4. リムライト (輪郭光) - キャラクタの縁を光らせる
    float3 V = normalize(cameraPos - input.worldpos.xyz); // カメラへの方向
    float rim = 1.0 - saturate(dot(N, V)); // 縁ほど値が大きくなる
    float rimIntensity = 0.0f;
    
    // 縁が一定以上なら光らせる
    if (rim > 0.7f)
    {
        rimIntensity = 0.5f;
    }

    // 5. 最終カラーの合成
    // (ベース色 * (トゥーン明度 * ライト色 + 環境光)) + リムライト
    float3 finalColor = baseColor * (toonDiffuse * lightColor + ambientColor) + rimIntensity;

    return float4(finalColor, texColor.a * color.a);
}