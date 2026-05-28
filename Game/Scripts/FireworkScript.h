#pragma once
#include "IScript.h"
#include <DirectXMath.h>
#include <vector>

namespace Game {

class FireworkScript : public IScript {
public:
    struct Spark {
        DirectX::XMFLOAT3 pos;
        DirectX::XMFLOAT3 vel;
        DirectX::XMFLOAT4 color;
        float life;
        float maxLife;
    };

    void Start(entt::entity entity, GameScene* scene) override;
    void Update(entt::entity entity, GameScene* scene, float dt) override;
    void OnDestroy(entt::entity entity, GameScene* scene) override;

private:
    DirectX::XMFLOAT3 centerPos_ = {0,0,0};
    DirectX::XMFLOAT4 fwColor_ = {1,1,1,1};
    DirectX::XMFLOAT4 fwColor2_ = {1,1,1,1};
    bool isMultiColor_ = false;
    int state_ = 0; // 0: 上昇中, 1: 爆発
    float timer_ = 0.0f;
    float launchHeight_ = 15.0f;
    DirectX::XMFLOAT3 currentPos_ = {0,0,0};
    
    std::vector<Spark> sparks_;
    uint32_t planeMesh_ = 0;
    uint32_t sparkTex_ = 0;
};

}
