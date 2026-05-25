#include "Obj.hlsli"

Texture2D<float4> tex : register(t0);
SamplerState smp : register(s0);

float4 main(VSOutput input) : SV_TARGET
{
    float2 uv = float2(
        input.uv.x * m_uv_scale.x + m_uv_offset.x,
        input.uv.y * m_uv_scale.y + m_uv_offset.y
    );
    float4 texcolor = tex.Sample(smp, uv);
    
    // アルファテスト：透明なピクセルは描画しない（Zバッファへの書き込みも防ぐ）
    clip(texcolor.a - 0.1f);
    
    // そのままの色だとHDRトーンマッピング等で暗く見えるため、明度を上げる係数をさらに上げる
    // （元の色が暗く沈むのを防ぐため、かなり強めの値に設定）
    float brightness = 6.0f;
    float3 finalColor = texcolor.rgb * color.rgb * brightness;
    
    return float4(finalColor, texcolor.a * color.a);
}
