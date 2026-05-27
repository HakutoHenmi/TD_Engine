// =============================================================
// SteampunkPS.hlsl - スチームパンク風メタリックピクセルシェーダー
// 銅・真鍮の重厚なメタリック質感 + 錆 + パティナ(緑青)
// =============================================================

// b0: フレーム共有
cbuffer CBFrame : register(b0) {
    row_major float4x4 gView;
    row_major float4x4 gProj;
    row_major float4x4 gViewProj;
    float3 gCamPos;
    float gTime;
};

// b1: オブジェクト固有 (VSOutからcolorとして受け取るため削除)
// cbuffer CBObj : register(b1) はもう使いません

// ライト構造体
struct DirLight { float3 dir; float pad0; float3 color; float pad1; uint enabled; float3 pad2; };
struct PointLight { float3 pos; float pad0; float3 color; float range; float3 atten; float pad1; uint enabled; float3 pad2; };
struct SpotLight { float3 pos; float pad0; float3 dir; float range; float3 color; float inner; float3 atten; float outer; uint enabled; float3 pad2; };
struct AreaLight { float3 pos; float pad0; float3 color; float range; float3 right; float halfWidth; float3 up; float halfHeight; float3 dir; float pad1; float3 atten; float pad2; uint enabled; float3 pad3; };

#define MAX_DIR 1
#define MAX_POINT 4
#define MAX_SPOT 4
#define MAX_AREA 4

cbuffer CBLight : register(b2) {
    float3 gAmbientColor;
    float padA0;
    DirLight gDir[MAX_DIR];
    PointLight gPoint[MAX_POINT];
    SpotLight gSpot[MAX_SPOT];
    AreaLight gArea[MAX_AREA];
    row_major float4x4 gShadowMatrix;
};

Texture2D gTex : register(t0);
Texture2D gShadowMap : register(t1);
SamplerState gSmp : register(s0);
SamplerComparisonState gShadowSmp : register(s1);

// ================================================
// ユーティリティ関数
// ================================================

// プロシージャルノイズ
float hash(float2 p) {
    return frac(sin(dot(p, float2(127.1, 311.7))) * 43758.5453);
}

float noise2D(float2 p) {
    float2 i = floor(p);
    float2 f = frac(p);
    f = f * f * (3.0 - 2.0 * f); // smoothstep
    float a = hash(i);
    float b = hash(i + float2(1.0, 0.0));
    float c = hash(i + float2(0.0, 1.0));
    float d = hash(i + float2(1.0, 1.0));
    return lerp(lerp(a, b, f.x), lerp(c, d, f.x), f.y);
}

// FBMノイズ（複数オクターブ）
float fbm(float2 p) {
    float v = 0.0;
    float a = 0.5;
    float2 shift = float2(100.0, 100.0);
    for (int i = 0; i < 4; ++i) {
        v += a * noise2D(p);
        p = p * 2.0 + shift;
        a *= 0.5;
    }
    return v;
}

// フレネル近似（Schlick）
float3 FresnelSchlick(float cosTheta, float3 F0) {
    return F0 + (1.0 - F0) * pow(saturate(1.0 - cosTheta), 5.0);
}

// GGX分布関数
float DistributionGGX(float3 N, float3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    float denom = NdotH2 * (a2 - 1.0) + 1.0;
    denom = 3.14159265 * denom * denom;
    return a2 / max(denom, 0.001);
}

float GetAttenuation(float3 atten, float d) {
    return 1.0 / (atten.x + atten.y * d + atten.z * d * d);
}

// シャドウ計算
float CalcShadow(float3 worldPos) {
    float4 shadowPos = mul(float4(worldPos, 1.0), gShadowMatrix);
    float3 proj = shadowPos.xyz / shadowPos.w;
    proj.x = proj.x * 0.5 + 0.5;
    proj.y = -proj.y * 0.5 + 0.5;
    if (proj.x < 0 || proj.x > 1 || proj.y < 0 || proj.y > 1 || proj.z < 0 || proj.z > 1)
        return 1.0;
    return gShadowMap.SampleCmpLevelZero(gShadowSmp, proj.xy, proj.z).r;
}

// ================================================
// メイン
// ================================================
float4 main(float4 svpos : SV_POSITION, float3 worldPos : TEXCOORD0, float3 normal : TEXCOORD1, float2 uv : TEXCOORD2, float4 color : COLOR0) : SV_TARGET
{
    float3 N = normalize(normal);
    float3 V = normalize(gCamPos - worldPos);
    float NdotV = max(dot(N, V), 0.0);

    // ================================================================
    // マテリアル: color.rgb を基準に銅/真鍮/鉄を選択
    // color.r > 0.7 → 銅（Copper）, color.g > 0.5 → 真鍮（Brass）, それ以外 → ダークアイアン
    // ================================================================
    float3 baseMetalColor;
    float roughness;
    float3 F0; // フレネル反射率

    if (color.r > 0.7) {
        // 銅（Copper）
        baseMetalColor = float3(0.72, 0.45, 0.20);
        roughness = 0.35;
        F0 = float3(0.955, 0.638, 0.538);
    } else if (color.g > 0.5) {
        // 真鍮（Brass）
        baseMetalColor = float3(0.78, 0.57, 0.11);
        roughness = 0.30;
        F0 = float3(0.910, 0.778, 0.423);
    } else {
        // ダークアイアン
        baseMetalColor = float3(0.30, 0.28, 0.26);
        roughness = 0.50;
        F0 = float3(0.560, 0.570, 0.580);
    }

    // テクスチャサンプリング（あれば適用）
    float4 texColor = gTex.Sample(gSmp, uv);
    float3 albedo = baseMetalColor * texColor.rgb;

    // ================================================================
    // プロシージャル錆とパティナ（緑青）
    // ================================================================
    float2 noiseCoord = worldPos.xz * 1.5 + worldPos.y * 0.3;

    // 錆パターン
    float rustNoise = fbm(noiseCoord * 2.0);
    float rustMask = smoothstep(0.45, 0.7, rustNoise);
    // 凹部（法線が下を向いている箇所）に錆を集中
    rustMask *= saturate(1.0 - N.y * 0.5 + 0.5);
    float3 rustColor = float3(0.55, 0.25, 0.08);

    // パティナ（緑青）パターン - 時間で微妙に変化
    float patinaNoise = fbm(noiseCoord * 3.0 + gTime * 0.01);
    float patinaMask = smoothstep(0.55, 0.85, patinaNoise) * 0.3;
    float3 patinaColor = float3(0.20, 0.55, 0.45);

    // 合成
    albedo = lerp(albedo, rustColor, rustMask * 0.4);
    albedo = lerp(albedo, patinaColor, patinaMask);

    // 錆部分はラフネスが高い
    roughness = lerp(roughness, 0.8, rustMask * 0.5);

    // ================================================================
    // ライティング（PBR風）
    // ================================================================
    float shadowFactor = CalcShadow(worldPos);
    shadowFactor = lerp(0.4, 1.0, shadowFactor);

    // アンビエント（環境光 + 半球ライティング）
    float3 skyColor = float3(0.15, 0.12, 0.10);
    float3 groundColor = float3(0.05, 0.03, 0.02);
    float3 hemisphere = lerp(groundColor, skyColor, N.y * 0.5 + 0.5);
    float3 ambient = albedo * (gAmbientColor + hemisphere * 0.3);

    // フレネル（エッジで反射が強くなる）
    float3 fresnel = FresnelSchlick(NdotV, F0);
    // メタルの環境反射（フェイク）
    float3 envReflect = fresnel * float3(0.12, 0.10, 0.08) * (1.0 - roughness);

    float3 finalColor = ambient + envReflect;

    // Directional Light
    for (int i = 0; i < MAX_DIR; ++i) {
        if (gDir[i].enabled) {
            float3 L = normalize(-gDir[i].dir);
            float3 H = normalize(L + V);

            float NdotL = saturate(dot(N, L));

            // ディフューズ
            float3 diff = albedo * NdotL;

            // スペキュラ（GGX）
            float D = DistributionGGX(N, H, roughness);
            float3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);
            float3 spec = D * F * 0.25; // 簡略化

            finalColor += (diff + spec) * gDir[i].color * shadowFactor;
        }
    }

    // Point Lights
    for (int j = 0; j < MAX_POINT; ++j) {
        if (gPoint[j].enabled) {
            float3 Lv = gPoint[j].pos - worldPos;
            float d = length(Lv);
            if (d < gPoint[j].range) {
                float3 L = normalize(Lv);
                float3 H = normalize(L + V);
                float att = GetAttenuation(gPoint[j].atten, d);
                float rangeFade = smoothstep(gPoint[j].range, gPoint[j].range * 0.2, d);
                att *= rangeFade;

                float NdotL = saturate(dot(N, L));
                float3 diff = albedo * NdotL;
                float D = DistributionGGX(N, H, roughness);
                float3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);
                float3 spec = D * F * 0.15;

                finalColor += (diff + spec) * gPoint[j].color * att;
            }
        }
    }

    // 暖色リムライト（スチームパンクの温かい輝き）
    float rim = pow(1.0 - NdotV, 3.0);
    float3 rimColor = float3(0.8, 0.4, 0.1) * rim * 0.15;
    finalColor += rimColor;

    // 微妙な脈動光（蒸気機関の呼吸感）
    float pulse = sin(gTime * 1.5) * 0.5 + 0.5;
    finalColor += albedo * pulse * 0.03;

    return float4(finalColor, texColor.a * color.a);
}
