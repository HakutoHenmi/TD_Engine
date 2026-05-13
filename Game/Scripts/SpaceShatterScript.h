#pragma once
#include "IScript.h"
#include "ObjectTypes.h"
#include <DirectXMath.h>
#include <vector>

namespace Game {

class SpaceShatterScript : public IScript {
public:
    void Start(entt::entity entity, GameScene* scene) override;
    void Update(entt::entity entity, GameScene* scene, float dt) override;
    void Draw(entt::entity entity, GameScene* scene) override;
    void DrawUI(entt::entity /*entity*/, GameScene* /*scene*/) override {}
    void OnDestroy(entt::entity entity, GameScene* scene) override;

private:
    float timer_ = 0.0f;
    float duration_ = -1.0f;  // 負の値 = 永続（手動で破棄するまで存在）

    // 空間の割れ目の中心と基底ベクトル
    DirectX::XMFLOAT3 centerPos_ = {0, 0, 0};
    DirectX::XMFLOAT3 planeRight_ = {1, 0, 0};
    DirectX::XMFLOAT3 planeUp_ = {0, 1, 0};
    DirectX::XMFLOAT3 planeNormal_ = {0, 0, 1};

    float shatterRadius_ = 5.0f;  // 割れ目全体の半径
    int shardCount_ = 60;         // 破片の数

    // 散らばるモード（銃撃エフェクト用）
    bool scatterMode_ = false;
    bool isSpecial_ = false;      // 特殊射撃フラグ
    float scatterDelay_ = 0.05f;
    float scatterSpeed_ = 10.0f;

    // === 破片データ ===
    struct ShardPiece {
        DirectX::XMFLOAT3 pos;
        DirectX::XMFLOAT3 velocity;
        DirectX::XMFLOAT3 rot;
        DirectX::XMFLOAT3 rotVel;
        float sizeScale;
        int colorType; // -1 for sparks
    };
    std::vector<ShardPiece> shards_;
    
    struct SmokePiece {
        DirectX::XMFLOAT3 pos;
        DirectX::XMFLOAT3 velocity;
        DirectX::XMFLOAT3 rot;
        float life;
        float maxLife;
        float size;
        float delay;
        bool isAppeared;
        DirectX::XMFLOAT4 color;
    };
    std::vector<SmokePiece> smokeParticles_;

    // ヘルパー関数
    uint32_t planeMesh_ = 0;
    uint32_t cubeMesh_ = 0;
    uint32_t sparkTex_ = 0;

    void GenerateShards(GameScene* scene);
    void GenerateSmokeParticles(GameScene* scene, int count);
    DirectX::XMFLOAT3 LocalToWorld(float localX, float localY) const;
};

} // namespace Game
