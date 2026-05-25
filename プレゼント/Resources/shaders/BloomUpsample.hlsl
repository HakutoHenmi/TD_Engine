// ============================================================================
// BloomUpsample.hlsl
// ============================================================================
// テントフィルタ（9タップ）によるアップサンプル＋加算ブレンド。
// ダウンサンプルで縮小したブルームテクスチャを、段階的に元の解像度に
// 戻しながら合成していく。
//
// 【アルゴリズム概要】
// 3x3テントフィルタは、バイリニア補間よりも滑らかなアップサンプルを実現する。
// 各段階で前の段階の結果（より大きいテクスチャ）と加算合成することで、
// 遠距離のグローと近距離のシャープなグローが自然に混ざり合う。
//
// テントフィルタの重みパターン:
//    1  2  1
//    2  4  2   ÷ 16
//    1  2  1
//
// 参考: "Next Generation Post Processing in Call of Duty: Advanced Warfare"
// ============================================================================

// 入力テクスチャ（前段のアップサンプル結果、または最も小さいダウンサンプル結果）
Texture2D gInput : register(t0);
SamplerState gSmp : register(s0);

// 定数バッファ
cbuffer CBBloom : register(b0) {
    float2 gTexelSize;   // 入力テクスチャのテクセルサイズ
    float  gBloomRadius; // ブルームの広がり半径（サンプルオフセットの倍率）
    float  gPad0;
};

// ----------------------------------------------------------------------------
// メインエントリ: 9タップ・テントフィルタ・アップサンプル
//
// テントフィルタは、ボックスフィルタを2回適用した結果と等価であり、
// 非常に自然で滑らかなぼかしが得られる。
// gBloomRadius でサンプリング範囲を広げることで、ブルームの「広がり」を
// コントロールできる。
// ----------------------------------------------------------------------------
float4 main(float4 svpos : SV_POSITION, float2 uv : TEXCOORD0) : SV_TARGET {
    float2 ts = gTexelSize * gBloomRadius;

    // --- 9タップ・テントフィルタ ---
    // 重み配分: 中央4、辺2、角1 (合計16で割る)
    
    // 四隅 (重み: 1/16 = 0.0625)
    float3 a = gInput.Sample(gSmp, uv + float2(-ts.x, -ts.y)).rgb;  // 左上
    float3 c = gInput.Sample(gSmp, uv + float2( ts.x, -ts.y)).rgb;  // 右上
    float3 g = gInput.Sample(gSmp, uv + float2(-ts.x,  ts.y)).rgb;  // 左下
    float3 i = gInput.Sample(gSmp, uv + float2( ts.x,  ts.y)).rgb;  // 右下
    
    // 辺の中点 (重み: 2/16 = 0.125)
    float3 b = gInput.Sample(gSmp, uv + float2( 0.0,  -ts.y)).rgb;  // 上
    float3 d = gInput.Sample(gSmp, uv + float2(-ts.x,   0.0)).rgb;  // 左
    float3 f = gInput.Sample(gSmp, uv + float2( ts.x,   0.0)).rgb;  // 右
    float3 h = gInput.Sample(gSmp, uv + float2( 0.0,   ts.y)).rgb;  // 下
    
    // 中央 (重み: 4/16 = 0.25)
    float3 e = gInput.Sample(gSmp, uv).rgb;                          // 中心

    // テントフィルタ合成
    float3 result = e * 0.25                          // 中央: 4/16
                   + (b + d + f + h) * 0.125          // 辺: 各 2/16
                   + (a + c + g + i) * 0.0625;        // 角: 各 1/16

    return float4(max(result, 0.0), 1.0);
}
