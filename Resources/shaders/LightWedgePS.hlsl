struct VSOutput { 
    float4 svpos : SV_POSITION; 
    float2 uv : TEXCOORD; 
    float4 color : COLOR; 
};

Texture2D<float4> tex : register(t0);
SamplerState smp : register(s0);

cbuffer CBFrame : register(b0) { 
    row_major float4x4 gView; 
    row_major float4x4 gProj; 
    row_major float4x4 gViewProj; 
    float3 gCamPos; 
    float gTime; 
};

float4 main(VSOutput input) : SV_TARGET {
    float x = abs(input.uv.x - 0.5f) * 2.0f;
    
    // 三層構造：鋭い芯 + 中間グロー + 超広域にじみ
    float core = exp(-x * x * 10.0f);           // 非常に細い芯
    float midGlow = exp(-x * x * 2.0f) * 0.5f;  // 中間的な広がり
    float halo = exp(-x * x * 0.5f) * 0.25f;    // 超広域にじみ（板ポリの輪郭を完全に消す）
    
    float edgeFade = core + midGlow + halo;

    // 縦方向（根元が明るく、先端が溶けるように消える）
    float v = input.uv.y;
    float distFade = exp(-v * 2.0f);

    float4 finalColor = input.color;
    finalColor.a *= edgeFade * distFade;
    
    // 芯ほど明るく輝く（発光感の強化）
    float coreBoost = 1.0f + core * 2.0f;
    finalColor.rgb *= coreBoost;
    
    if (finalColor.a <= 0.002f) { 
        discard; 
    }
    
    return finalColor;
}
