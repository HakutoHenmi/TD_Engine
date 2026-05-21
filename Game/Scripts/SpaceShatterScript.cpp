#include "SpaceShatterScript.h"
#include "Scenes/GameScene.h"
#include "ScriptEngine.h"
#include <cmath>
#include <cstdlib>
#include <algorithm>
#include "Renderer.h"
#include "Camera.h"

namespace Game {

// ローカル座標(平面上のXY)をワールド座標に変換
DirectX::XMFLOAT3 SpaceShatterScript::LocalToWorld(float localX, float localY) const {
    return {
        centerPos_.x + planeRight_.x * localX + planeUp_.x * localY,
        centerPos_.y + planeRight_.y * localX + planeUp_.y * localY,
        centerPos_.z + planeRight_.z * localX + planeUp_.z * localY
    };
}

void SpaceShatterScript::Start(entt::entity entity, GameScene* scene) {
    auto& reg = scene->GetRegistry();
    auto* renderer = scene->GetRenderer(); if (!renderer) return;

    planeMesh_ = renderer->LoadObjMesh("Resources/Models/plane.obj");
    cubeMesh_ = renderer->LoadObjMesh("Resources/Models/cube/cube.obj");
    sparkTex_ = renderer->LoadTexture2D("Resources/Textures/particles/diamond_flare.png");

    // 中心位置の取得
    if (reg.all_of<TransformComponent>(entity))
        centerPos_ = reg.get<TransformComponent>(entity).translate;

    // VariableComponentからパラメータを読み取り
    DirectX::XMFLOAT3 normal = {0, 0, 1};
    if (reg.all_of<VariableComponent>(entity)) {
        auto& vc = reg.get<VariableComponent>(entity);
        normal = { vc.GetValue("NormalX", 0), vc.GetValue("NormalY", 0), vc.GetValue("NormalZ", 1) };
        shatterRadius_ = vc.GetValue("Radius", 5.0f);
        shardCount_ = (int)vc.GetValue("Count", 60.0f);
        duration_ = vc.GetValue("Duration", -1.0f); // -1 = 永続

        scatterMode_ = vc.GetValue("ScatterMode", 0.0f) > 0.5f;
        isSpecial_ = vc.GetValue("IsSpecial", 0.0f) > 0.5f;
        isFlight_ = vc.GetValue("IsFlight", 0.0f) > 0.5f;
        colorMode_ = (int)vc.GetValue("ColorMode", 0.0f);
        scatterDelay_ = vc.GetValue("ScatterDelay", 0.05f);
        scatterSpeed_ = vc.GetValue("ScatterSpeed", 10.0f);
    }

    // 銃撃エフェクトの場合は、銃口から少し離すために法線方向へオフセット
    if (!scatterMode_) {
        float muzzleOffset = 0.8f;
        centerPos_.x += normal.x * muzzleOffset;
        centerPos_.y += normal.y * muzzleOffset;
        centerPos_.z += normal.z * muzzleOffset;
    }

    // 平面の基底ベクトルを計算
    float nl = std::sqrt(normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
    if (nl > 0.001f) { normal.x /= nl; normal.y /= nl; normal.z /= nl; }
    planeNormal_ = normal;

    DirectX::XMFLOAT3 upHint = {0, 1, 0};
    if (std::abs(normal.y) > 0.99f) upHint = {0, 0, 1};

    planeRight_ = {
        upHint.y * normal.z - upHint.z * normal.y,
        upHint.z * normal.x - upHint.x * normal.z,
        upHint.x * normal.y - upHint.y * normal.x
    };
    float rl = std::sqrt(planeRight_.x * planeRight_.x + planeRight_.y * planeRight_.y + planeRight_.z * planeRight_.z);
    if (rl > 0.001f) { planeRight_.x /= rl; planeRight_.y /= rl; planeRight_.z /= rl; }

    planeUp_ = {
        normal.y * planeRight_.z - normal.z * planeRight_.y,
        normal.z * planeRight_.x - normal.x * planeRight_.z,
        normal.x * planeRight_.y - normal.y * planeRight_.x
    };

    if (!isFlight_ && colorMode_ != 1) {
        GenerateShards(scene);
    }
    
    int sCount = isFlight_ ? shardCount_ : (isSpecial_ ? 15 : 10);
    if (colorMode_ == 1) sCount = shardCount_; // 毒霧モードは破片の代わりに煙を大量に生成
    
    GenerateSmokeParticles(scene, sCount);
}

void SpaceShatterScript::GenerateShards(GameScene* scene) {
    if (!scene) return;

    if (!scatterMode_) {
        // マズルエフェクト（控えめに調整）
        int count = isSpecial_ ? 30 : 20;
        for (int i = 0; i < count; ++i) {
            float sizeBase = shatterRadius_ * (0.04f + (rand() % 100) / 100.0f * 0.08f);
            float speed = (scatterSpeed_ * 0.8f) + ((rand() % 100) / 100.0f) * scatterSpeed_ * 1.2f;
            float spread = 0.4f;
            float rx = ((rand() % 100) / 100.0f - 0.5f) * spread;
            float ry = ((rand() % 100) / 100.0f - 0.5f) * spread;
            
            DirectX::XMFLOAT3 dirVec;
            dirVec.x = planeNormal_.x + planeRight_.x * rx + planeUp_.x * ry;
            dirVec.y = planeNormal_.y + planeRight_.y * rx + planeUp_.y * ry;
            dirVec.z = planeNormal_.z + planeRight_.z * rx + planeUp_.z * ry;
            
            float vLen = std::sqrt(dirVec.x*dirVec.x + dirVec.y*dirVec.y + dirVec.z*dirVec.z);
            ShardPiece sp;
            sp.pos = centerPos_;
            sp.velocity = { (dirVec.x/vLen) * speed, (dirVec.y/vLen) * speed, (dirVec.z/vLen) * speed };
            
            float yaw = std::atan2(dirVec.x, dirVec.z);
            float pitch = -std::asin(std::max(-1.0f, std::min(1.0f, dirVec.y / vLen)));
            sp.rot = { pitch, yaw, 0.0f };
            sp.rotVel = { 0, 0, 0 };
            sp.sizeScale = sizeBase / shatterRadius_;
            sp.colorType = -1;
            shards_.push_back(sp);
        }
    } else {
        // 着弾エフェクト
        float yaw = std::atan2(planeNormal_.x, planeNormal_.z);
        float pitch = -std::asin(std::max(-1.0f, std::min(1.0f, planeNormal_.y)));

        int count = shardCount_;
        for (int i = 0; i < count; ++i) {
            float angle = (float)i / count * DirectX::XM_2PI;
            float dist = ((rand() % 100) / 100.0f) * shatterRadius_;
            DirectX::XMFLOAT3 pos = LocalToWorld(std::cos(angle) * dist, std::sin(angle) * dist);

            ShardPiece sp;
            sp.pos = pos;
            float speed = scatterSpeed_ * (0.3f + (rand() % 100) / 100.0f * 0.7f);
            sp.velocity = { 
                planeNormal_.x * speed + ((rand() % 100) / 100.0f - 0.5f) * 2.0f,
                planeNormal_.y * speed + ((rand() % 100) / 100.0f - 0.5f) * 2.0f,
                planeNormal_.z * speed + ((rand() % 100) / 100.0f - 0.5f) * 2.0f 
            };
            sp.rot = { pitch, yaw, 0.0f };
            sp.rotVel = { ((rand()%100)/100.0f-0.5f)*10.0f, ((rand()%100)/100.0f-0.5f)*10.0f, ((rand()%100)/100.0f-0.5f)*10.0f };
            sp.sizeScale = 1.0f;
            sp.colorType = rand() % 3;
            shards_.push_back(sp);
        }
    }
}

void SpaceShatterScript::Update(entt::entity entity, GameScene* scene, float dt) {
    timer_ += dt;
    auto& reg = scene->GetRegistry();

    float lifeT = std::max(0.0f, 1.0f - (timer_ / (duration_ > 0 ? duration_ : 0.5f)));
    for (auto it = shards_.begin(); it != shards_.end(); ) {
        if (lifeT <= 0.0f) {
            it = shards_.erase(it);
            continue;
        }

        it->pos.x += it->velocity.x * dt;
        it->pos.y += it->velocity.y * dt;
        it->pos.z += it->velocity.z * dt;
        
        it->rot.x += it->rotVel.x * dt;
        it->rot.y += it->rotVel.y * dt;
        it->rot.z += it->rotVel.z * dt;
        
        float damping = scatterMode_ ? 0.94f : 0.96f;
        it->velocity.x *= std::pow(damping, dt * 60.0f);
        it->velocity.y *= std::pow(damping, dt * 60.0f);
        it->velocity.z *= std::pow(damping, dt * 60.0f);
        it->velocity.y -= 9.8f * 0.12f * dt;
        ++it;
    }

    for (auto it = smokeParticles_.begin(); it != smokeParticles_.end(); ) {
        if (!it->isAppeared && timer_ >= it->delay) {
            it->isAppeared = true;
        }

        if (it->isAppeared) {
            it->life -= dt;
            if (it->life <= 0.0f) {
                it = smokeParticles_.erase(it);
                continue;
            }

            it->pos.x += it->velocity.x * dt;
            it->pos.y += it->velocity.y * dt;
            it->pos.z += it->velocity.z * dt;
            
            it->velocity.x *= std::pow(0.92f, dt * 60.0f);
            it->velocity.y *= std::pow(0.92f, dt * 60.0f);
            it->velocity.z *= std::pow(0.92f, dt * 60.0f);
            it->velocity.y += 1.2f * dt;
        }
        ++it;
    }

    if (!scatterMode_ && reg.all_of<PointLightComponent>(entity)) {
        auto& light = reg.get<PointLightComponent>(entity);
        light.intensity = std::max(0.0f, 15.0f * (1.0f - timer_ / 0.15f));
    }

    if (duration_ > 0.0f && timer_ >= duration_) {
        scene->DestroyObject(static_cast<uint32_t>(entity));
    }
}

void SpaceShatterScript::Draw(entt::entity /*entity*/, GameScene* scene) {
    auto* renderer = scene->GetRenderer(); if (!renderer) return;
    using namespace DirectX;

    float lifeT = std::max(0.0f, 1.0f - (timer_ / (duration_ > 0 ? duration_ : 0.5f)));
    if (lifeT <= 0.0f) return;

    for (auto& sh : shards_) {
        float s = shatterRadius_ * sh.sizeScale * std::pow(lifeT, 0.7f);
        XMMATRIX m;
        if (sh.colorType == -1) {
            m = XMMatrixScaling(s * 0.15f, s * 0.15f, s * 3.0f) *
                XMMatrixRotationRollPitchYaw(sh.rot.x, sh.rot.y, sh.rot.z) *
                XMMatrixTranslation(sh.pos.x, sh.pos.y, sh.pos.z);
            Engine::Matrix4x4 world; XMStoreFloat4x4(reinterpret_cast<XMFLOAT4X4*>(&world), m);
            Engine::Vector4 color = {0.2f, 0.7f, 2.0f, lifeT};
            if (colorMode_ == 1) color = {0.3f, 0.9f, 0.1f, lifeT};
            renderer->DrawParticleInstanced(cubeMesh_, sparkTex_, world, color, {1,1,0,0}, "ParticleAdditive");
        } else {
            m = XMMatrixScaling(s, s, s) *
                XMMatrixRotationRollPitchYaw(sh.rot.x, sh.rot.y, sh.rot.z) *
                XMMatrixTranslation(sh.pos.x, sh.pos.y, sh.pos.z);
            Engine::Matrix4x4 world; XMStoreFloat4x4(reinterpret_cast<XMFLOAT4X4*>(&world), m);
            Engine::Vector4 color = {1.0f, 1.0f, 1.0f, lifeT};
            if (sh.colorType == 1) color = {1.2f, 1.4f, 1.8f, lifeT};
            if (colorMode_ == 1) color = {0.4f, 0.9f, 0.2f, lifeT};
            renderer->DrawParticleInstanced(cubeMesh_, 0, world, color, {1.0f, 1.0f, 0.0f, 0.0f}, "Default");
        }
    }

    auto camPosRaw = scene->GetCamera().GetPosition();
    XMVECTOR camPos = XMLoadFloat3(reinterpret_cast<XMFLOAT3*>(&camPosRaw));
    for (auto& sm : smokeParticles_) {
        if (!sm.isAppeared) continue;
        float lifeRatio = std::max(0.0f, sm.life / sm.maxLife);
        float s = sm.size * (1.0f + (1.0f - lifeRatio) * 5.0f);

        XMVECTOR p = XMLoadFloat3(&sm.pos);
        XMVECTOR toCam = XMVector3Normalize(camPos - p);
        XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
        XMVECTOR right = XMVector3Normalize(XMVector3Cross(up, toCam));
        up = XMVector3Cross(toCam, right);

        XMMATRIX m;
        m.r[0] = right * s;
        m.r[1] = up * s;
        m.r[2] = toCam * s;
        m.r[3] = XMVectorSetW(p, 1.0f);

        Engine::Matrix4x4 world; XMStoreFloat4x4(reinterpret_cast<XMFLOAT4X4*>(&world), m);
        renderer->DrawParticleInstanced(planeMesh_, 0, world, {sm.color.x, sm.color.y, sm.color.z, lifeRatio * 0.5f}, {1.0f, 1.0f, 0.0f, 0.0f}, "ProceduralSmoke");
    }

    // 3. マズルフラッシュの閃光 (Shader-based Mesh)
    if (!scatterMode_ && !isFlight_ && lifeT > 0.8f) {
        float flashT = (lifeT - 0.8f) / 0.2f; // 1.0 -> 0.0 (非常に短時間)
        float s = 1.8f * (0.8f + flashT * 0.4f); // 銃口付近の小さな光
        
        XMVECTOR muzzlePos = XMLoadFloat3(&centerPos_);
        XMVECTOR toCam = XMVector3Normalize(camPos - muzzlePos);
        XMVECTOR upHint = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
        XMVECTOR right = XMVector3Normalize(XMVector3Cross(upHint, toCam));
        XMVECTOR up = XMVector3Cross(toCam, right);

        XMMATRIX m;
        m.r[0] = right * s;
        m.r[1] = up * s;
        m.r[2] = toCam * s;
        m.r[3] = XMVectorSetW(muzzlePos, 1.0f);

        Engine::Matrix4x4 world; XMStoreFloat4x4(reinterpret_cast<XMFLOAT4X4*>(&world), m);
        Engine::Vector4 color = isSpecial_ ? Engine::Vector4{0.5f, 0.8f, 1.0f, flashT} : Engine::Vector4{1.0f, 0.9f, 0.6f, flashT};
        renderer->DrawParticleInstanced(planeMesh_, sparkTex_, world, color, {1.0f, 1.0f, 0.0f, 0.0f}, "ParticleAdditive");
    }
}

void SpaceShatterScript::OnDestroy(entt::entity /*entity*/, GameScene* /*scene*/) {
    shards_.clear();
    smokeParticles_.clear();
}

void SpaceShatterScript::GenerateSmokeParticles(GameScene* scene, int count) {
    if (!scene) return;
    for (int i = 0; i < count; ++i) {
        float angle = ((float)(rand() % 1000) / 1000.0f) * DirectX::XM_2PI;
        float radius = std::sqrt((float)(rand() % 1000) / 1000.0f) * shatterRadius_ * 0.5f;
        DirectX::XMFLOAT3 pos = LocalToWorld(std::cos(angle) * radius, std::sin(angle) * radius);

        // ★銃の後方側から煙を出すためのオフセット
        if (!scatterMode_) {
            float backDist = 0.8f; // 銃の先端から本体の方へ戻す
            pos.x -= planeNormal_.x * backDist;
            pos.y -= planeNormal_.y * backDist;
            pos.z -= planeNormal_.z * backDist;
        }

        bool isSteam = (rand() % 4 != 0);
        SmokePiece sp;
        sp.pos = pos;
        sp.maxLife = 0.4f + (rand() % 100) / 100.0f * 0.5f;
        sp.life = sp.maxLife;
        sp.size = (0.4f + (rand() % 100) / 100.0f * 0.6f) * (shatterRadius_ / 5.0f);
        sp.delay = scatterDelay_ + (rand() % 100) / 100.0f * 0.05f;
        sp.isAppeared = false;
        sp.rot = {0,0,0};

        if (colorMode_ == 1) { // Poison Color
            sp.color = isSteam ? DirectX::XMFLOAT4{0.4f, 0.9f, 0.2f, 1.0f} : DirectX::XMFLOAT4{0.2f, 0.7f, 0.1f, 1.0f};
        } else {
            if (!scatterMode_) {
                if (isFlight_) {
                    sp.color = isSteam ? DirectX::XMFLOAT4{1.0f, 0.95f, 0.85f, 1.0f} : DirectX::XMFLOAT4{0.9f, 0.8f, 0.7f, 1.0f};
                } else {
                    sp.color = isSteam ? DirectX::XMFLOAT4{1.0f, 1.0f, 1.1f, 1.0f} : DirectX::XMFLOAT4{0.4f, 0.35f, 0.3f, 1.0f};
                }
            } else {
                sp.color = isSteam ? DirectX::XMFLOAT4{0.8f, 0.8f, 0.9f, 1.0f} : DirectX::XMFLOAT4{0.1f, 0.1f, 0.1f, 1.0f};
            }
        }

        float spd = (3.0f + (rand() % 100) / 100.0f * 5.0f) * (shatterRadius_ / 5.0f);
        if (!scatterMode_) {
            if (isFlight_) {
                // 飛行用・上に伸びる排気用 - 法線（真上・真下など）方向にまっすぐ強く噴射させ、横への拡散を小さく抑える
                float fwdSpd = spd * 3.5f; // 法線方向への力強い推進
                sp.velocity = {
                    planeNormal_.x * fwdSpd,
                    planeNormal_.y * fwdSpd,
                    planeNormal_.z * fwdSpd
                };
                // わずかな自然な揺らぎ（極小に制限）
                sp.velocity.x += ((rand() % 100) / 100.0f - 0.5f) * 0.8f;
                sp.velocity.y += ((rand() % 100) / 100.0f - 0.5f) * 0.8f;
                sp.velocity.z += ((rand() % 100) / 100.0f - 0.5f) * 0.8f;
            } else {
                // スチームパンク風の蒸気排気：銃の後方から左右に逃げるように
                float sideDir = (i % 2 == 0) ? 1.0f : -1.0f;
                float sideSpd = spd * 2.5f; // 横への勢いを強く
                float fwdSpd = spd * -0.2f; // わずかに後ろに流れる

                sp.velocity = { 
                    planeNormal_.x * fwdSpd + planeRight_.x * sideSpd * sideDir,
                    planeNormal_.y * fwdSpd + planeRight_.y * sideSpd * sideDir,
                    planeNormal_.z * fwdSpd + planeRight_.z * sideSpd * sideDir
                };
                // わずかな拡散ランダム
                sp.velocity.x += ((rand() % 100) / 100.0f - 0.5f) * 2.0f;
                sp.velocity.y += ((rand() % 100) / 100.0f - 0.5f) * 2.0f;
                sp.velocity.z += ((rand() % 100) / 100.0f - 0.5f) * 2.0f;
            }
        } else {
            // 着弾時（全方位に散る）
            if (colorMode_ == 1) {
                // 毒霧の場合は横方向（XZ平面）に大きく広がり、上方向（Y）は抑える
                float angleXZ = ((float)(rand() % 1000) / 1000.0f) * DirectX::XM_2PI;
                float spdImpact = spd * 2.5f; // 少しマイルドに
                sp.velocity = { 
                    std::cos(angleXZ) * spdImpact * 1.5f,
                    ((rand() % 100) / 100.0f) * 1.5f + 0.5f, // 少しだけ上にフワッと
                    std::sin(angleXZ) * spdImpact * 1.5f 
                };
            } else {
                float spdImpact = spd * 4.5f;
                sp.velocity = { 
                    planeNormal_.x * spdImpact + ((rand() % 100) / 100.0f - 0.5f) * 5.0f,
                    planeNormal_.y * spdImpact + 1.5f + ((rand() % 100) / 100.0f - 0.5f) * 5.0f,
                    planeNormal_.z * spdImpact + ((rand() % 100) / 100.0f - 0.5f) * 5.0f 
                };
            }
        }

        smokeParticles_.push_back(sp);
    }
}

REGISTER_SCRIPT(SpaceShatterScript);

} // namespace Game
