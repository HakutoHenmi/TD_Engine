#pragma once
#include "IScript.h"
#include "ObjectTypes.h"
#include <DirectXMath.h>
#include <vector>
#include <vector>

namespace Game {

class MirrorShatterScript : public IScript {
public:
    void Start(entt::entity entity, GameScene* scene) override;
    void Update(entt::entity entity, GameScene* scene, float dt) override;
    void Draw(entt::entity entity, GameScene* scene) override;
    void DrawUI(entt::entity entity, GameScene* scene) override;
    void OnDestroy(entt::entity, GameScene*) override {}

private:
    float timer_ = 0.0f;
    float duration_ = 2.5f;
    float freezeTime_ = 0.15f;
    float arcRadius_ = 3.5f;

    DirectX::XMFLOAT3 bulletDir_ = {0, 0, 1};
    DirectX::XMFLOAT3 centerPos_ = {0, 0, 0};
    DirectX::XMFLOAT3 arcRight_ = {1, 0, 0};
    DirectX::XMFLOAT3 arcUp_ = {0, 1, 0};
    DirectX::XMFLOAT3 arcForward_ = {0, 0, 1};

    bool noFlash_ = false;
    bool noCracks_ = false;

    // すべて plane 破片
    struct ShardPiece {
        entt::entity entity{entt::null};
        DirectX::XMFLOAT3 targetPos{0.0f, 0.0f, 0.0f};
        DirectX::XMFLOAT3 arcNormal{0.0f, 0.0f, 0.0f};
        DirectX::XMFLOAT3 velocity{0.0f, 0.0f, 0.0f};
        DirectX::XMFLOAT3 rotSpeed{0.0f, 0.0f, 0.0f};
        float sizeX{0.0f};
        float sizeY{0.0f};
        float appearDelay{0.0f};
        bool isAppeared{false};
        bool isScattering{false};
        bool isGlassPanel{false}; // 空間割れガラス板
    };
    std::vector<ShardPiece> shards_;

    // ヒビ
    struct CrackSegment {
        DirectX::XMFLOAT3 start;
        DirectX::XMFLOAT3 end;
        float delay;
        float width;
    };
    std::vector<CrackSegment> crackSegments_;

    // 光の筋
    struct LightStreak {
        DirectX::XMFLOAT3 startPos;
        DirectX::XMFLOAT3 endPos;
        float delay;
        float life;
        float maxLife;
        float width;
    };
    std::vector<LightStreak> lightStreaks_;

    // 光粒子
    struct SparkParticle {
        DirectX::XMFLOAT3 pos;
        DirectX::XMFLOAT3 velocity;
        float life, maxLife, size;
        int colorType;
    };
    std::vector<SparkParticle> sparks_;

    void GenerateShards(GameScene* scene, int count);
    void GenerateGlassPanel(GameScene* scene, int count);
    void GenerateCurvedCracks(int numBranches);
    void GenerateLightStreaks(int count);
    void SpawnSparks(int count);
    void DrawMagicCircle(float radius, float alpha);

    DirectX::XMFLOAT3 GetArcPoint(float u, float v) const;
    DirectX::XMFLOAT3 GetArcNormal(float u, float v) const;
};

} // namespace Game
