#pragma pack_matrix(row_major)

// b0: フレーム共有情報 (Renderer::CBFrame に対応)
cbuffer ViewProjection : register(b0)
{
    matrix view; // ビュー変換行列
    matrix projection; // プロジェクション変換行列
    matrix viewProj; // ビュープロジェクション行列
    float3 cameraPos; // カメラ座標
    float time; // シェーダー時間
};

// b1: オブジェクト固有情報 (Renderer::CBObj に対応)
cbuffer WorldTransform : register(b1)
{
    matrix world; // ワールド行列
    float4 color; // オブジェクトカラー
};

// スポットライト構造体
struct SpotLight
{
    float3 position; // 座標
    float range; // 範囲
    float3 direction; // 方向
    float innerCos; // 内側コサイン
    float3 color; // 色
    float outerCos; // 外側コサイン
    float3 atten; // 減衰
    uint enabled; // 有効フラグ
    float3 pad;
};

// b2: ライト情報 (Renderer::LightCB に対応)
cbuffer LightGroup : register(b2)
{
    float3 ambientColor;
    float pad0;
    SpotLight spotLight; // 単一のスポットライト
};

// マテリアル定数 (固定値)
static const float3 m_ambient = float3(0.3, 0.3, 0.3);
static const float3 m_diffuse = float3(1.0, 1.0, 1.0);
static const float3 m_specular = float3(0.5, 0.5, 0.5);
static const float m_alpha = 1.0;
static const float3 m_uv_scale = float3(1.0, 1.0, 1.0);
static const float3 m_uv_offset = float3(0.0, 0.0, 0.0);

// 頂点シェーダー出力構造体
struct VSOutput
{
    float4 svpos : SV_POSITION; // システム用頂点座標
    float4 worldpos : POSITION; // ワールド座標
    float3 normal : NORMAL; // 法線
    float2 uv : TEXCOORD; // uv値
};