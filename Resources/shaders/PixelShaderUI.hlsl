// ================================================
// UI SDF Pixel Shader（DirectX 12 / D3DCompile対象）
// 四角・円・三日月をピクセルシェーダだけで描く
// 画面空間(px)で処理 / グロー付 / 加算ブレンド推奨
// ================================================

cbuffer CBUI : register(b1)
{
    float2  uCenterPx;     // 画面上の中心位置(px)
    float2  uSizePx;       // 図形サイズ(px) … 四角は幅高、円は半径を uSizePx.x に入れる
    float2  uViewportPx;   // 画面解像度(px)（ViewportのWidth/Height）
    float   uLineWidth;    // 線の太さ(px)
    float   uGlow;         // グローの厚み(px)（外側のにじみ範囲）
    float4  uColor;        // ベース色（線色）
    int     uShape;        // 0:Square 1:Circle 2:Crescent
    float   uRound;        // 角丸半径(px)（四角用）
    float   uInner;        // 三日月の内側半径（円との差分用）
    float   uRotateRad;    // 回転（ラジアン） 2D図形を回転したい時
    float   uProgress;     // プログレスバーの割合(0.0 - 1.0) ※未使用時は -1.0 などを設定してください
    float   uFill;         // 1.0: 塗りつぶし, 0.0: アウトラインのみ
    float2  _pad;          // アライメント調整用
}

// 受け取り：頂点で出したUV（0-1）。全画面クアッドならそのまま使える
struct PSIn {
    float4 pos  : SV_POSITION;
    float2 uv   : TEXCOORD0;  // (0,1)範囲を想定
};

static float2 Rotate(float2 p, float a) {
    float c = cos(a), s = sin(a);
    return float2(c*p.x - s*p.y, s*p.x + c*p.y);
}

// Noise functions for realistic smoke
float hash(float2 p) {
    p = frac(p * float2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return frac(p.x * p.y);
}

float noise(float2 p) {
    float2 i = floor(p);
    float2 f = frac(p);
    float2 u = f * f * (3.0 - 2.0 * f);
    return lerp(lerp(hash(i + float2(0.0, 0.0)), 
                     hash(i + float2(1.0, 0.0)), u.x),
                lerp(hash(i + float2(0.0, 1.0)), 
                     hash(i + float2(1.0, 1.0)), u.x), u.y);
}

float fbm(float2 p) {
    float f = 0.0;
    f += 0.5000 * noise(p); p = p * 2.02;
    f += 0.2500 * noise(p); p = p * 2.03;
    f += 0.1250 * noise(p); p = p * 2.01;
    f += 0.0625 * noise(p);
    return f;
}


// 距離関数 -------------------------------
// 角丸矩形（中心原点、size=半サイズ、round=角丸半径）
float sdRoundedBox(float2 p, float2 halfSize, float roundR) {
    float2 q = abs(p) - halfSize + roundR;
    float2 m = max(q, 0.0);
    return length(m) - roundR + min(max(q.x, q.y), 0.0);
}

// 円（中心原点、半径 r）
float sdCircle(float2 p, float r) {
    return length(p) - r;
}

// 三日月：外円 r と 内円 rInner の差（外円∩内円の外側）
// dOuter==0 が外円の線、 dInner==0 が内円の線。
// “ライン”としては |dOuter| の近傍を主に使い、内円側でマスク。
float sdCrescent(float2 p, float rOuter, float rInner) {
    // 外円＋内円のCSG差　※ここでは “外円の距離” を返して、後段で内円で消す
    return sdCircle(p, rOuter);
}

// 共通：線＆グローの着色
//  d     : 距離（0=図形境界）
//  width : 線の半分厚み（px）に近いイメージ → uLineWidthをそのまま使用
//  glow  : グロー厚（px）
//  col   : ベース色
//  fill  : 1.0=塗りつぶし, 0.0=アウトライン
float4 drawLineGlow(float d, float width, float glow, float4 col, float innerMask, float fill)
{
    // 塗りつぶしの場合は内側 (d < 0) を全て不透明にし、外側は輪郭でフェードする
    float lineDist = lerp(abs(d), max(d, 0.0), fill);
    
    // 線のα（-width..+width を1→0へ）
    float lineAlpha = 1.0 - smoothstep(width-1.0, width+1.0, lineDist); 
    // グロー（線の外側からglow範囲で0→1→0のフェード）
    float glowAlpha = 1.0 - smoothstep(width, width + max(glow, 0.001), lineDist);

    // ちょい内側を強調（発光コア）
    float core = 1.0 - smoothstep(0.0, width*0.6 + 0.001, lineDist);
    float4 glowCol = col * (0.55 * glowAlpha + 0.45 * core);
    
    if (fill > 0.5) {
        glowCol = col * lineAlpha; // 塗りつぶしの場合はそのままの色
    }

    // 内側マスク（三日月など内側を切るため）
    glowCol.a *= innerMask;

    return glowCol; // 加算ブレンド前提
}

// Crescent用の内側マスク（内円より内側は消す）
float innerMaskByInnerCircle(float2 p, float rInner) {
    // rInner 未使用時は常に1.0
    if (rInner <= 0.0) return 1.0;
    float din = sdCircle(p, rInner);
    // 内円の内側(din<0) → マスク0、外側→1
    return step(0.0, din);
}

float4 mainPS(PSIn i) : SV_TARGET
{
    // 画面UV→px座標へ（左上(0,0)）
    float2 uv = i.uv;
    float2 fragPx = uv * uViewportPx;

    // 図形中心基準へ
    float2 p = fragPx - uCenterPx;
    // 回転
    p = Rotate(p, uRotateRad);

    // 距離dの計算
    float d = 1e6;
    float innerMask = 1.0;

    if (uShape == 0) {
        // 角丸矩形：uSizePx=幅高、halfに
        d = sdRoundedBox(p, uSizePx * 0.5, uRound);
    } else if (uShape == 1) {
        // 円：uSizePx.x を半径として使用
        d = sdCircle(p, uSizePx.x);
    } else if (uShape == 2) {
        // Crescent
        d = sdCrescent(p, uSizePx.x, uInner);
        innerMask *= innerMaskByInnerCircle(p, uInner);
    } else if (uShape == 3) {
        // リアルな煙（プロシージャル）
        // UV座標に近い正規化された位置 (-0.5 to 0.5) を作る
        float2 center = p / (max(uSizePx.x, 1.0) * 2.0);
        float baseDist = length(center);
        
        // FBMノイズによる歪み
        float2 noiseUV = center * 3.0 + float2(uRotateRad, uRotateRad * 0.5);
        float n = fbm(noiseUV);
        
        float radius = baseDist * 2.0; 
        // 外周に近づくにつれて完全に0になるように減衰させる
        float falloff = saturate(1.0 - radius);
        falloff = pow(falloff, 2.0); // 端でスムーズに0になるように
        
        // ノイズによって境界を不規則にする (雲のようなモクモク感)
        float edge = saturate(falloff + (n - 0.5) * 1.5);
        edge = pow(edge, 1.5) * falloff; // 外周では必ず0になるように強制
        
        float4 smokeCol;
        // 色にもノイズを少し乗せる (煙の濃淡)
        float3 color = uColor.rgb * lerp(0.7, 1.3, n);
        // psoUI_ は SrcBlend=ONE のプリマルチプライドアルファを前提としているため、
        // 最終的な RGB にも uColor.a を掛ける必要がある。
        // （これを忘れると寿命でアルファが0になっても加算光が残り、消滅時にパッと消える不自然な見た目になる）
        smokeCol.rgb = color * edge * uColor.a; 
        smokeCol.a = edge * uColor.a;
        
        return smokeCol; 
    } else if (uShape == 4) {
        // 火花（プロシージャル加算合成用）
        float2 sp = p;
        sp.y *= 5.0; // 進行方向へ鋭く引き伸ばす
        float r = length(sp) / max(uSizePx.x, 1.0);
        
        // 中心部は非常に明るく、外周は柔らかく減衰
        float alpha = saturate(1.0 - r);
        alpha = pow(alpha, 3.0); // 鋭い減衰
        
        float4 sparkCol;
        sparkCol.rgb = uColor.rgb * alpha * 4.0; // コアを発光させるために色を強く乗せる
        sparkCol.a   = alpha * uColor.a; // 加算合成のためそのままアルファを渡す
        return sparkCol;
    }

    // プログレス(クリッピング)処理
    if (uProgress >= 0.0 && uProgress <= 1.0) {
        if (uShape == 1 || uShape == 2) {
            // 円・三日月の場合は扇形(角度)クリッピング (上から時計回り)
            float angle = atan2(p.x, -p.y);
            if (angle < 0.0) angle += 6.283185307f;
            float t = angle / 6.283185307f;
            innerMask *= step(t, uProgress);
        } else {
            // 左から右への直線プログレス
            float width = (uShape == 0) ? uSizePx.x : uSizePx.x * 2.0;
            float t = (p.x + width * 0.5) / width;
            innerMask *= step(t, uProgress);
        }
    }

    return drawLineGlow(d, uLineWidth, uGlow, uColor, innerMask, uFill);
}