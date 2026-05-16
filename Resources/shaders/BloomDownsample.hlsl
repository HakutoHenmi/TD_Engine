// ============================================================================
// BloomDownsample.hlsl
// ============================================================================
// UE4/5と同様の13タップ・ダウンサンプルフィルタ。
// シーンの明るいピクセルを抽出しながら、高品質にダウンサンプリングする。
//
// 【アルゴリズム概要】
// 通常の4ピクセル平均ダウンサンプルではジャギーやちらつきが発生する。
// 13タップ法は、5つの重み付きサンプル群で構成されており、
// ファイアフライ（明るすぎるピクセルの暴走）を抑えながら滑らかに縮小する。
//
// 参考: "Next Generation Post Processing in Call of Duty: Advanced Warfare"
//       (SIGGRAPH 2014, Jorge Jimenez)
// ============================================================================

// 入力テクスチャ（前段の結果またはシーンカラー）
Texture2D gInput : register(t0);
SamplerState gSmp : register(s0);

// 定数バッファ: テクセルサイズと輝度抽出の閾値
cbuffer CBBloom : register(b0) {
    float2 gTexelSize;   // 入力テクスチャの1ピクセルあたりのUVサイズ (1.0/width, 1.0/height)
    float  gThreshold;   // 輝度抽出の閾値（最初のパスでのみ使用、0なら抽出なし）
    float  gPad0;
};

// ----------------------------------------------------------------------------
// 輝度計算（ITU-R BT.709 / sRGB）
// ----------------------------------------------------------------------------
float Luminance(float3 color) {
    return dot(color, float3(0.2126, 0.7152, 0.0722));
}

// ----------------------------------------------------------------------------
// Karis Average（ファイアフライ対策）
// 非常に明るいピクセルが1つあると、ダウンサンプル後にそのピクセルが
// ブロック全体を覆ってしまう。Karis Averageは各サンプルを輝度で
// 重み付けすることで、この「暴走」を防ぐ。
// 最初のダウンサンプルパスでのみ適用する。
// ----------------------------------------------------------------------------
float3 KarisAverage(float3 a, float3 b, float3 c, float3 d) {
    float wa = 1.0 / (1.0 + Luminance(a));
    float wb = 1.0 / (1.0 + Luminance(b));
    float wc = 1.0 / (1.0 + Luminance(c));
    float wd = 1.0 / (1.0 + Luminance(d));
    return (a * wa + b * wb + c * wc + d * wd) / (wa + wb + wc + wd);
}

// ----------------------------------------------------------------------------
// ソフト輝度抽出（Soft Threshold）
// ハードな閾値でカットすると、ブルームの境界にパキっとしたエッジが出る。
// ソフトなKneeカーブを使い、閾値付近で滑らかに減衰させる。
// ----------------------------------------------------------------------------
float3 ApplyThreshold(float3 color) {
    float lum = Luminance(color);
    // Knee幅を小さくして、閾値以下のピクセル（地面など）が光らないようにする
    float knee = 0.05; 
    float soft = lum - gThreshold + knee;
    soft = clamp(soft, 0.0, 2.0 * knee);
    soft = soft * soft / (4.0 * knee + 0.00001);
    float contribution = max(soft, lum - gThreshold) / max(lum, 0.00001);
    return color * max(contribution, 0.0);
}

// ----------------------------------------------------------------------------
// メインエントリ: 13タップ・ダウンサンプル
// 
// サンプリングパターン (各文字は4ピクセルのブロックを表す):
//
//    A . B . C
//    . D . E .
//    F . G . H
//    . I . J .
//    K . L . M
//
// 重みの配分:
//    中央   G: 0.125 (4サンプルの平均 × 0.5)
//    十字  D,E,I,J: 各 0.125 (4サンプルの平均 × 0.5)
//    四隅  A+B+F+G, B+C+G+H, F+G+K+L, G+H+L+M: 各 0.03125
//
// 結果として、中央ほど重みが大きく、端に向かって滑らかに減衰する。
// これにより、通常のダウンサンプルよりもはるかに高品質な結果が得られる。
// ----------------------------------------------------------------------------
float4 main(float4 svpos : SV_POSITION, float2 uv : TEXCOORD0) : SV_TARGET {
    float2 ts = gTexelSize;

    // --- 13タップのサンプリング ---
    // 中央（G）
    float3 a = gInput.Sample(gSmp, uv).rgb;
    
    // 十字パターン（D, E, I, J 相当の4ブロック）
    float3 b = gInput.Sample(gSmp, uv + float2(-ts.x,  -ts.y)).rgb;  // 左上
    float3 c = gInput.Sample(gSmp, uv + float2( ts.x,  -ts.y)).rgb;  // 右上
    float3 d = gInput.Sample(gSmp, uv + float2(-ts.x,   ts.y)).rgb;  // 左下
    float3 e = gInput.Sample(gSmp, uv + float2( ts.x,   ts.y)).rgb;  // 右下
    
    // さらに外側のサンプル（より広い範囲をカバー）
    float3 f = gInput.Sample(gSmp, uv + float2(-2.0 * ts.x, -2.0 * ts.y)).rgb;
    float3 g = gInput.Sample(gSmp, uv + float2( 0.0,        -2.0 * ts.y)).rgb;
    float3 h = gInput.Sample(gSmp, uv + float2( 2.0 * ts.x, -2.0 * ts.y)).rgb;
    float3 i = gInput.Sample(gSmp, uv + float2(-2.0 * ts.x,  0.0       )).rgb;
    float3 j = gInput.Sample(gSmp, uv + float2( 2.0 * ts.x,  0.0       )).rgb;
    float3 k = gInput.Sample(gSmp, uv + float2(-2.0 * ts.x,  2.0 * ts.y)).rgb;
    float3 l = gInput.Sample(gSmp, uv + float2( 0.0,         2.0 * ts.y)).rgb;
    float3 m = gInput.Sample(gSmp, uv + float2( 2.0 * ts.x,  2.0 * ts.y)).rgb;

    // --- 重み付き合成 ---
    float3 result;
    
    if (gThreshold > 0.0) {
        // 最初のパス: Karis Average + 輝度抽出
        // ファイアフライを防ぎつつ、明るい部分のみ抽出する
        float3 group0 = KarisAverage(a, b, c, d);    // 中央4ピクセル
        float3 group1 = KarisAverage(f, g, b, i);    // 左上ブロック
        float3 group2 = KarisAverage(g, h, c, j);    // 右上ブロック
        float3 group3 = KarisAverage(i, b, k, l);    // 左下ブロック
        float3 group4 = KarisAverage(c, j, l, m);    // 右下ブロック
        
        result = group0 * 0.5 + (group1 + group2 + group3 + group4) * 0.125;
        result = ApplyThreshold(result);
    } else {
        // 後続パス: 通常の重み付きダウンサンプル
        // UE4の重み配分に準拠
        result  = a * 0.125;                                          // 中央: 12.5%
        result += (b + c + d + e) * 0.125;                            // 内側十字: 各12.5%
        result += (f + h + k + m) * 0.03125;                          // 四隅: 各3.125%
        result += (g + i + j + l) * 0.0625;                           // 外側十字: 各6.25%
    }

    return float4(max(result, 0.0), 1.0);
}
