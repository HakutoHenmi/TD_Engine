#include "FireworkScript.h"
#include "Scenes/GameScene.h"
#include "../../Engine/Renderer.h"
#include "Camera.h"
#include "ScriptEngine.h"
#include "../../Engine/Time/TimeManager.h"
#include <cstdlib>

namespace Game {

void FireworkScript::Start(entt::entity entity, GameScene* scene) {
    auto& reg = scene->GetRegistry();
    auto* renderer = scene->GetRenderer(); 
    if (!renderer) return;

    planeMesh_ = renderer->LoadObjMesh("Resources/Models/plane.obj");
    sparkTex_ = renderer->LoadTexture2D("Resources/Textures/particles/diamond_flare.png");

    if (reg.all_of<TransformComponent>(entity)) {
        centerPos_ = reg.get<TransformComponent>(entity).translate;
    }

    currentPos_ = centerPos_;
    state_ = 0;
    timer_ = 0.0f;
    launchHeight_ = 40.0f + ((rand() % 100) / 100.0f) * 20.0f; // 40~60の高さ

    auto getRandomColor = []() {
        int colorType = rand() % 7;
        float r=1, g=1, b=1;
        switch(colorType) {
            case 0: r = 1.0f; g = 0.2f; b = 0.2f; break; // Red
            case 1: r = 0.2f; g = 1.0f; b = 0.2f; break; // Green
            case 2: r = 0.2f; g = 0.5f; b = 1.0f; break; // Blue
            case 3: r = 1.0f; g = 1.0f; b = 0.2f; break; // Yellow
            case 4: r = 1.0f; g = 0.2f; b = 1.0f; break; // Magenta
            case 5: r = 0.2f; g = 1.0f; b = 1.0f; break; // Cyan
            case 6: r = 1.0f; g = 0.6f; b = 0.1f; break; // Orange
        }
        r += ((rand() % 100)/100.0f - 0.5f) * 0.2f;
        g += ((rand() % 100)/100.0f - 0.5f) * 0.2f;
        b += ((rand() % 100)/100.0f - 0.5f) * 0.2f;
        return DirectX::XMFLOAT4{
            (std::max)(0.0f, (std::min)(1.0f, r)) * 3.0f, 
            (std::max)(0.0f, (std::min)(1.0f, g)) * 3.0f, 
            (std::max)(0.0f, (std::min)(1.0f, b)) * 3.0f, 
            1.0f
        };
    };

    fwColor_ = getRandomColor();
    fwColor2_ = getRandomColor();
    isMultiColor_ = (rand() % 100) < 40; // 40% chance for multi-colored firework
}

void FireworkScript::Update(entt::entity entity, GameScene* scene, float dt) {
    if (dt <= 0.0001f) {
        dt = Engine::TimeManager::GetInstance().GetUnscaledDeltaTime();
    }
    timer_ += dt;

    if (state_ == 0) {
        // 打ち上げ
        currentPos_.y += 50.0f * dt;
        
        // 上昇中の火花（トレイル）
        Spark sp;
        sp.pos = currentPos_;
        sp.vel = { ((rand()%100)/100.0f - 0.5f)*3.0f, -8.0f, ((rand()%100)/100.0f - 0.5f)*3.0f };
        sp.color = {2.0f, 1.5f, 0.8f, 1.0f};
        sp.maxLife = 0.5f;
        sp.life = sp.maxLife;
        sparks_.push_back(sp);

        if (currentPos_.y >= centerPos_.y + launchHeight_) {
            state_ = 1;
            // 爆発
            int count = 250 + rand() % 150; // 増量
            for (int i = 0; i < count; ++i) {
                float phi = ((rand() % 100) / 100.0f) * DirectX::XM_PI;
                float theta = ((rand() % 100) / 100.0f) * DirectX::XM_2PI;
                float speed = 15.0f + ((rand() % 100) / 100.0f) * 45.0f; // 少し広げる
                
                Spark p;
                p.pos = currentPos_;
                p.vel = {
                    std::sin(phi) * std::cos(theta) * speed,
                    std::cos(phi) * speed,
                    std::sin(phi) * std::sin(theta) * speed
                };
                p.color = (isMultiColor_ && (i % 2 == 0)) ? fwColor2_ : fwColor_;
                p.maxLife = 1.5f + ((rand() % 100) / 100.0f) * 1.5f;
                p.life = p.maxLife;
                sparks_.push_back(p);
            }
        }
    }

    // 火花の更新
    auto* renderer = scene->GetRenderer();
    Engine::Camera& cam = scene->GetCamera();
    DirectX::XMFLOAT3 cPos = cam.Position();
    DirectX::XMVECTOR camPos = DirectX::XMLoadFloat3(&cPos);

    for (auto it = sparks_.begin(); it != sparks_.end(); ) {
        it->life -= dt;
        if (it->life <= 0.0f) {
            it = sparks_.erase(it);
            continue;
        }

        it->pos.x += it->vel.x * dt;
        it->pos.y += it->vel.y * dt;
        it->pos.z += it->vel.z * dt;
        
        it->vel.x *= std::pow(0.92f, dt * 60.0f);
        it->vel.y *= std::pow(0.92f, dt * 60.0f);
        it->vel.y -= 9.8f * dt * 0.4f; // 重力
        it->vel.z *= std::pow(0.92f, dt * 60.0f);
        
        // 描画
        if (renderer) {
            float alpha = std::max(0.0f, it->life / it->maxLife);
            DirectX::XMFLOAT4 c = it->color;
            c.w = alpha;
            
            float s = 0.8f * alpha;
            if (state_ == 0) s = 0.5f * alpha;
            
            DirectX::XMVECTOR pPos = DirectX::XMLoadFloat3(&it->pos);
            if (it->pos.y < 3.0f) {
                pPos = DirectX::XMVectorSetY(pPos, DirectX::XMVectorGetY(pPos) + s * 0.35f);
            }
            DirectX::XMVECTOR toCam = DirectX::XMVector3Normalize(DirectX::XMVectorSubtract(camPos, pPos));
            DirectX::XMVECTOR upHint = DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
            DirectX::XMVECTOR right = DirectX::XMVector3Normalize(DirectX::XMVector3Cross(upHint, toCam));
            DirectX::XMVECTOR up = DirectX::XMVector3Cross(toCam, right);

            DirectX::XMMATRIX m;
            m.r[0] = right * s;
            m.r[1] = up * s;
            m.r[2] = toCam * s;
            m.r[3] = DirectX::XMVectorSetW(pPos, 1.0f);

            Engine::Matrix4x4 world; DirectX::XMStoreFloat4x4(reinterpret_cast<DirectX::XMFLOAT4X4*>(&world), m);
            renderer->DrawParticleInstanced(planeMesh_, sparkTex_, world, {c.x, c.y, c.z, c.w}, {1.0f, 1.0f, 0.0f, 0.0f}, "ParticleAdditive");
        }
        
        ++it;
    }

    if (state_ == 1 && sparks_.empty()) {
        scene->DestroyObject(static_cast<uint32_t>(entity));
    }
}

void FireworkScript::OnDestroy(entt::entity, GameScene*) {
    sparks_.clear();
}
REGISTER_SCRIPT(FireworkScript);

}
