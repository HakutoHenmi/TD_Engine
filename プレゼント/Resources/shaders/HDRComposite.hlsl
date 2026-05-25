// HDRComposite.hlsl
// 統合ポストプロセスシェーダー: ブルーム合成 + ACESトーンマッピング + 距離フォグ + FXAA
// 旧ポストプロセス (CRT, Anime, Rich, Painterly) を全て置き換える

Texture2D gScene : register(t0);   // HDRシーンカラー
Texture2D gBloom : register(t1);   // ブルームテクスチャ
Texture2D gDepth : register(t2);   // 深度バッファ
Texture2D gSSAO  : register(t3);   // SSAOテクスチャ
SamplerState gSmp : register(s0);

cbuffer CBPost : register(b0)
{
    float gTime;
    float gNoiseStrength;    // 未使用（互換用）
    float gDistortion;       // 未使用（互換用）
    float gDamageVignette;   // ダメージ用の赤色ビネット
    float gVignette;         // 通常の黒色ビネット
    float gScanline;         // 未使用（互換用）
    float gSan;              // グレースケール強度 (死亡演出用)
    float gBloomIntensity;
    float gDofFocusDist;     // 未使用（互換用）
    float gDofFocusRange;    // 未使用（互換用）
    float gDofIntensity;     // 未使用（互換用）

    // フォグパラメータ
    float gFogDensity;
    float gFogStart;
    float gFogEnd;
    float gFogHeightFalloff;

    // カメラパラメータ (深度リニア化用)
    float gNearPlane;
    float gFarPlane;
    float gFXAAEnabled;       // 1.0 = FXAA有効
    float gExposure;          // 露出補正

    float gFogColorR;
    float gFogColorG;
    float gFogColorB;
    float pad0;
};

struct VSOut
{
    float4 svpos : SV_POSITION;
    float2 uv : TEXCOORD0;
};

// =========================================================================
// 深度バッファからリニア深度に変換
// =========================================================================
float LinearizeDepth(float rawDepth)
{
    float near = gNearPlane;
    float far = gFarPlane;
    return near * far / (far - rawDepth * (far - near));
}

// ビネット効果 (黒色ビネットとダメージ用赤色ビネットの合成)
// =========================================================================
float3 ApplyVignette(float3 color, float2 uv)
{
    float2 d = uv - 0.5f;
    float distSq = dot(d, d);
    
    // 1. 通常の黒ビネット
    if (gVignette > 0.0f) {
        float v = saturate(1.0f - distSq * gVignette * 2.0f);
        color *= v;
    }
    
    // 2. ダメージ時の赤ビネット
    if (gDamageVignette > 0.0f) {
        float vDamage = saturate(1.0f - distSq * gDamageVignette * 2.0f);
        float damageFactor = 1.0f - vDamage;
        float3 bloodColor = float3(0.8f, 0.0f, 0.0f);
        color = lerp(color, bloodColor, damageFactor * 0.7f);
    }
    
    return color;
}

// =========================================================================
// FXAA 3.11 Quality (HDR対応版)
// =========================================================================
float Luminance(float3 color)
{
    // HDR空間の輝度をLDR空間に近似圧縮してから計算（白飛びによるジャギー検出漏れを防ぐ）
    float lum = dot(color, float3(0.299f, 0.587f, 0.114f));
    return lum / (1.0f + lum);
}

float3 ApplyFXAA(float2 uv, float2 texelSize)
{
    float3 rgbM  = gScene.Sample(gSmp, uv).rgb;
    float3 rgbNW = gScene.Sample(gSmp, uv + float2(-texelSize.x, -texelSize.y)).rgb;
    float3 rgbNE = gScene.Sample(gSmp, uv + float2( texelSize.x, -texelSize.y)).rgb;
    float3 rgbSW = gScene.Sample(gSmp, uv + float2(-texelSize.x,  texelSize.y)).rgb;
    float3 rgbSE = gScene.Sample(gSmp, uv + float2( texelSize.x,  texelSize.y)).rgb;
    
    float lumM  = Luminance(rgbM);
    float lumNW = Luminance(rgbNW);
    float lumNE = Luminance(rgbNE);
    float lumSW = Luminance(rgbSW);
    float lumSE = Luminance(rgbSE);
    
    float lumMin = min(lumM, min(min(lumNW, lumNE), min(lumSW, lumSE)));
    float lumMax = max(lumM, max(max(lumNW, lumNE), max(lumSW, lumSE)));
    
    float lumRange = lumMax - lumMin;
    
    // しきい値を極限まで下げて、少しのジャギーも逃さず検知する
    float FXAA_EDGE_THRESHOLD = 0.0312f;
    float FXAA_EDGE_THRESHOLD_MIN = 0.0156f;
    if (lumRange < max(FXAA_EDGE_THRESHOLD_MIN, lumMax * FXAA_EDGE_THRESHOLD))
        return rgbM;
    
    float dirSwMinusNe = lumSW - lumNE;
    float dirSeMinusNw = lumSE - lumNW;
    
    float2 dir;
    dir.x = -(dirSwMinusNe + dirSeMinusNw);
    dir.y =  (dirSwMinusNe - dirSeMinusNw);
    
    float dirReduce = max((lumNW + lumNE + lumSW + lumSE) * 0.25f * 0.125f, 1.0f / 128.0f);
    float rcpDirMin = 1.0f / (min(abs(dir.x), abs(dir.y)) + dirReduce);
    
    float FXAA_SPAN_MAX = 8.0f;
    dir = clamp(dir * rcpDirMin, -FXAA_SPAN_MAX, FXAA_SPAN_MAX) * texelSize;
    
    float3 rgbA = 0.5f * (
        gScene.Sample(gSmp, uv + dir * (1.0f / 3.0f - 0.5f)).rgb +
        gScene.Sample(gSmp, uv + dir * (2.0f / 3.0f - 0.5f)).rgb
    );
    
    float3 rgbB = rgbA * 0.5f + 0.25f * (
        gScene.Sample(gSmp, uv + dir * -0.5f).rgb +
        gScene.Sample(gSmp, uv + dir *  0.5f).rgb
    );
    
    float lumB = Luminance(rgbB);
    
    if (lumB < lumMin || lumB > lumMax)
        return rgbA;
    else
        return rgbB;
}

// =========================================================================
// 色相保持型 ACES Filmic Tone Mapping
// =========================================================================
float3 ACESFilm(float3 x)
{
    float a = 2.51f;
    float b = 0.03f;
    float c = 2.43f;
    float d = 0.59f;
    float e = 0.14f;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

float3 ToneMapHuePreserving(float3 color, float exposure)
{
    color *= exposure;
    
    // ルミナンス（輝度）を計算
    float lum = dot(color, float3(0.2126, 0.7152, 0.0722));
    
    // 輝度ベースでトーンマッピング
    float newLum = ACESFilm(lum.xxx).r;
    
    // 元の色相と彩度を維持しながら輝度をスケーリング
    // これにより、青い光が白飛び（クリッピング）せずに「青いまま明るく」見えます
    return color * (newLum / max(lum, 0.0001f));
}

// =========================================================================
// メインピクセルシェーダー
// =========================================================================
float4 main(float4 svpos : SV_POSITION, float2 uv : TEXCOORD0) : SV_TARGET
{
    // テクスチャサイズ取得
    float2 sceneDim;
    gScene.GetDimensions(sceneDim.x, sceneDim.y);
    float2 texelSize = 1.0f / sceneDim;
    
    // 1. FXAAまたは通常サンプリング
    float3 sceneColor;
    if (gFXAAEnabled > 0.5f)
        sceneColor = ApplyFXAA(uv, texelSize);
    else
        sceneColor = gScene.Sample(gSmp, uv).rgb;
        
    // 入力がLDR(sRGB)なのでリニア空間に戻す
    sceneColor = pow(max(sceneColor, 0.0f), 2.2f);
    
    // 1.5 SSAOの適用
    float ssao = gSSAO.Sample(gSmp, uv).r;
    sceneColor *= ssao;
    
    // 2. 距離フォグ (リニア空間で計算 + 満ち引きの動き)
    float rawDepth = gDepth.Sample(gSmp, uv).r;
    float linearDepth = LinearizeDepth(rawDepth);
    // フォグカラーもリニア空間として扱う
    float3 fogColor = float3(gFogColorR, gFogColorG, gFogColorB);
    float3 fogColorLinear = pow(max(fogColor, 0.0f), 2.2f);
    
    if (gFogDensity > 0.0f) {
        float dist = max(linearDepth - gFogStart, 0.0f);
        
        // 1. ベースフォグ（絶対に境目や奥の地平線を見せない、完全に静止したフォグ）
        float baseFogFactor = 1.0f - exp(-(dist * gFogDensity) * (dist * gFogDensity));
        
        // 2. 動的フォグ（空間を漂うムラ成分）
        float2 pseudoWorldXZ = float2(uv.x * 2.0f - 1.0f, 1.0f) * dist; 
        
        float spaceWave1 = sin(pseudoWorldXZ.x * 0.015f + gTime * 0.3f);
        float spaceWave2 = cos(pseudoWorldXZ.y * 0.02f + gTime * 0.4f);
        float spaceWave3 = sin((pseudoWorldXZ.x + pseudoWorldXZ.y) * 0.01f - gTime * 0.2f);
        
        float spatialVariation = (spaceWave1 + spaceWave2 + spaceWave3) / 3.0f;
        spatialVariation = spatialVariation * 0.5f + 0.5f; // 0.0 〜 1.0
        
        float timeWave = sin(gTime * 0.5f) * 0.5f + 0.5f; 
        
        // ムラ成分によって「さらに濃くなる」だけの追加密度を計算
        // （ベースより薄くなる＝晴れて境目が見える ことを防ぐため）
        float extraDensity = gFogDensity * 0.7f * spatialVariation * timeWave;
        float extraFogFactor = 1.0f - exp(-(dist * extraDensity) * (dist * extraDensity));
        
        // 3. ベースフォグと追加フォグを合成
        // ベースの濃さを最低保証しつつ、ムラの部分だけ少し濃くなる
        float finalFogFactor = saturate(baseFogFactor + extraFogFactor * 0.6f);
        
        float3 fogColorExp = lerp(fogColorLinear, fogColorLinear * 1.2f, finalFogFactor);
        sceneColor = lerp(sceneColor, fogColorExp, finalFogFactor);
    }
    
    // 3. ブルーム合成 (リニア空間で加算)
    if (gBloomIntensity > 0.0f)
    {
        // ブルームテクスチャもリニアである前提（または近似）
        float3 bloom = gBloom.Sample(gSmp, uv).rgb;
        
        // バンディング対策: ブルームの弱い部分を滑らかにフェードアウトさせる
        float bloomLum = dot(bloom, float3(0.2126, 0.7152, 0.0722));
        float smoothFactor = smoothstep(0.0f, 0.05f, bloomLum);
        
        sceneColor += bloom * smoothFactor * gBloomIntensity;
    }
    
    // 4. トーンマッピング (色相保持型ACES)
    // これにより、ブルームで1.0を超えた輝度が自然に圧縮され、
    // キューブの中心も青色（元の色）を保ちます。
    sceneColor = ToneMapHuePreserving(sceneColor, gExposure);
    
    // 5. sRGBへガンマ補正
    sceneColor = pow(max(sceneColor, 0.0f), 1.0f / 2.2f);
    
    // 6. ビネット
    sceneColor = ApplyVignette(sceneColor, uv);
    
    // 7. グレースケール (死亡演出用)
    if (gSan > 0.0f)
    {
        float gray = dot(sceneColor, float3(0.2126, 0.7152, 0.0722));
        sceneColor = lerp(sceneColor, float3(gray, gray, gray), gSan);

    }
    
    return float4(saturate(sceneColor), 1.0f);
}
