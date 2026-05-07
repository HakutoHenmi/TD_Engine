#include "MirrorShatterScript.h"
#include "Scenes/GameScene.h"
#include "ScriptEngine.h"
#include <cmath>
#include <cstdlib>
#include <algorithm>

namespace Game {

static constexpr float kCrackTime = 0.22f; 

void MirrorShatterScript::Start(entt::entity entity, GameScene* scene) {
    auto& reg = scene->GetRegistry();
    if (reg.all_of<TransformComponent>(entity)) centerPos_ = reg.get<TransformComponent>(entity).translate;

    float scale = 1.0f;
    int shardCount = 150;
    if (reg.all_of<VariableComponent>(entity)) {
        auto& vc = reg.get<VariableComponent>(entity);
        bulletDir_ = { vc.GetValue("DirX",0), vc.GetValue("DirY",0), vc.GetValue("DirZ",1) };
        scale = vc.GetValue("Scale", 1.0f);
        shardCount = (int)vc.GetValue("Count", 150.0f);
        duration_ = vc.GetValue("Duration", 2.5f);
        arcRadius_ = vc.GetValue("Radius", 8.5f) * scale;
        noFlash_ = vc.GetValue("NoFlash", 0.0f) > 0.5f;
        noCracks_ = vc.GetValue("NoCracks", 0.0f) > 0.5f;
    }

    arcForward_ = bulletDir_;
    float cx = arcForward_.z, cz = -arcForward_.x;
    float cl = std::sqrt(cx*cx+cz*cz);
    arcRight_ = (cl > 0.001f) ? DirectX::XMFLOAT3{cx/cl,0,cz/cl} : DirectX::XMFLOAT3{1,0,0};
    arcUp_ = { arcForward_.y*arcRight_.z - arcForward_.z*arcRight_.y,
               arcForward_.z*arcRight_.x - arcForward_.x*arcRight_.z,
               arcForward_.x*arcRight_.y - arcForward_.y*arcRight_.x };

    GenerateCurvedCracks((int)(18 * scale)); 
    GenerateShards(scene, (int)(shardCount * scale)); 
    SpawnSparks((int)(60 * scale)); // 火花を増量
    
    // 追加: 放射状の光条（Light Streaks）
    GenerateLightStreaks((int)(24 * scale));
}

void MirrorShatterScript::GenerateLightStreaks(int count) {
    lightStreaks_.clear();
    for (int i = 0; i < count; ++i) {
        float u = ((rand() % 2000) / 1000.0f - 1.0f);
        float v = ((rand() % 400) / 1000.0f - 0.2f); // 扇状に合わせる
        auto n = GetArcNormal(u, v);
        
        LightStreak s;
        s.startPos = centerPos_;
        float len = arcRadius_ * (0.8f + (rand() % 100) / 100.0f * 0.7f);
        s.endPos = { centerPos_.x + n.x * len, centerPos_.y + n.y * len, centerPos_.z + n.z * len };
        s.delay = kCrackTime + (std::sqrtf(u*u + v*v) * 0.05f);
        s.life = 0.08f + (rand() % 100) / 100.0f * 0.08f; // 大幅に短縮
        s.maxLife = s.life;
        s.width = 0.02f + (rand() % 100) / 100.0f * 0.05f;
        lightStreaks_.push_back(s);
    }
}

DirectX::XMFLOAT3 MirrorShatterScript::GetArcPoint(float u, float v) const {
    float theta = u * DirectX::XM_PI * 0.8f;
    float phi = v * DirectX::XM_PI * 0.18f; 
    float lx = std::sin(theta) * std::cos(phi), ly = std::sin(phi), lz = std::cos(theta) * std::cos(phi);
    return { centerPos_.x + (arcRight_.x*lx + arcUp_.x*ly + arcForward_.x*lz) * arcRadius_,
             centerPos_.y + (arcRight_.y*lx + arcUp_.y*ly + arcForward_.y*lz) * arcRadius_,
             centerPos_.z + (arcRight_.z*lx + arcUp_.z*ly + arcForward_.z*lz) * arcRadius_ };
}

DirectX::XMFLOAT3 MirrorShatterScript::GetArcNormal(float u, float v) const {
    float theta = u * DirectX::XM_PI * 0.8f, phi = v * DirectX::XM_PI * 0.18f;
    float lx = std::sin(theta) * std::cos(phi), ly = std::sin(phi), lz = std::cos(theta) * std::cos(phi);
    return { arcRight_.x*lx + arcUp_.x*ly + arcForward_.x*lz,
             arcRight_.y*lx + arcUp_.y*ly + arcForward_.y*lz,
             arcRight_.z*lx + arcUp_.z*ly + arcForward_.x*lz };
}

void MirrorShatterScript::GenerateShards(GameScene* scene, int count) {
    auto* renderer = scene->GetRenderer(); if (!renderer) return;
    auto& reg = scene->GetRegistry();
    int cracksCount = (int)crackSegments_.size(); if (cracksCount == 0) return;

    for (int i = 0; i < count; ++i) {
        const auto& seg = crackSegments_[i % cracksCount];
        float t = (rand() % 100) / 100.0f;
        DirectX::XMFLOAT3 basePos = { seg.start.x + (seg.end.x - seg.start.x) * t, seg.start.y + (seg.end.y - seg.start.y) * t, seg.start.z + (seg.end.z - seg.start.z) * t };
        float offset = 0.05f + (rand() % 100) / 100.0f * 0.4f;
        float offAngle = (rand() % 100) / 100.0f * DirectX::XM_2PI;
        DirectX::XMFLOAT3 arcP = { basePos.x + (arcRight_.x * std::cos(offAngle) + arcUp_.x * std::sin(offAngle)) * offset, basePos.y + (arcRight_.y * std::cos(offAngle) + arcUp_.y * std::sin(offAngle)) * offset, basePos.z + (arcRight_.z * std::cos(offAngle) + arcUp_.z * std::sin(offAngle)) * offset };

        auto ent = scene->CreateEntity("MirrorShard_VFX");
        auto& tc = reg.get<TransformComponent>(ent);
        tc.translate = arcP; tc.scale = {0, 0, 0};
        float dx = arcP.x - centerPos_.x, dy = arcP.y - centerPos_.y, dz = arcP.z - centerPos_.z;
        float dist = std::sqrt(dx*dx + dy*dy + dz*dz);
        float sizeBase = (0.35f + (dist / arcRadius_) * 1.6f) * (arcRadius_ / 8.5f);
        float sX = sizeBase * (0.6f + (rand() % 100) / 100.0f * 0.8f);
        float sY = sizeBase * (0.6f + (rand() % 100) / 100.0f * 0.8f);
        DirectX::XMFLOAT3 n = { dx, dy, dz }; float len = std::sqrt(n.x*n.x + n.y*n.y + n.z*n.z);
        if (len > 0) { n.x /= len; n.y /= len; n.z /= len; }
        tc.rotate = { std::atan2(n.x, n.z), -std::asin(std::max(-1.0f, std::min(1.0f, n.y))), ((rand() % 3600) / 1800.0f) * DirectX::XM_PI };

        auto& mrc = reg.emplace<MeshRendererComponent>(ent);
        mrc.shaderName = "GlassShatter"; mrc.modelPath = "Resources/Models/plane.obj";
        mrc.modelHandle = renderer->LoadObjMesh(mrc.modelPath);
        mrc.color = {(rand() % 10000) / 10000.0f, 0.0f, 1.0f, 6.0f};

        ShardPiece sp; sp.entity = ent; sp.targetPos = arcP; sp.arcNormal = n;
        float spd = (4.0f + (rand() % 100) / 100.0f * 9.0f) * (arcRadius_ / 8.5f);
        sp.velocity = { n.x * spd, n.y * spd, n.z * spd };
        sp.rotSpeed = { ((rand() % 100) / 100.0f - 0.5f) * 14.0f, ((rand() % 100) / 100.0f - 0.5f) * 14.0f, ((rand() % 100) / 100.0f - 0.5f) * 14.0f };
        sp.sizeX = sX; sp.sizeY = sY; sp.appearDelay = seg.delay + (t * 0.06f) + 0.02f;
        shards_.push_back(sp);
    }
}

void MirrorShatterScript::Update(entt::entity entity, GameScene* scene, float dt) {
    timer_ += dt;
    auto& reg = scene->GetRegistry();
    float oa = std::max(0.0f, (timer_ > duration_ * 0.7f) ? 1.0f - (timer_ - duration_ * 0.7f) / (duration_ * 0.3f) : 1.0f);

    for (auto& sh : shards_) {
        if (!reg.valid(sh.entity)) continue;
        auto& tc = reg.get<TransformComponent>(sh.entity);
        if (!sh.isAppeared && timer_ >= sh.appearDelay) { sh.isAppeared = true; tc.scale = { sh.sizeX, sh.sizeY, 1.0f }; }
        if (sh.isAppeared) {
            float scatterStartTime = sh.appearDelay + 0.07f;
            if (!sh.isScattering && timer_ >= scatterStartTime) sh.isScattering = true;
            if (sh.isScattering) {
                float el = timer_ - scatterStartTime;
                tc.translate.x += sh.velocity.x * dt; tc.translate.y += sh.velocity.y * dt; tc.translate.z += sh.velocity.z * dt;
                sh.velocity.y -= 5.0f * dt;
                tc.rotate.x += sh.rotSpeed.x * dt; tc.rotate.y += sh.rotSpeed.y * dt; tc.rotate.z += sh.rotSpeed.z * dt;
                float sk = std::max(0.0f, 1.0f - el * 1.5f); tc.scale = { sh.sizeX * sk, sh.sizeY * sk, 1.0f };
            } else {
                float vib = std::sin(timer_ * 80) * 0.015f;
                tc.translate = { sh.targetPos.x + sh.arcNormal.x * vib, sh.targetPos.y + sh.arcNormal.y * vib, sh.targetPos.z + sh.arcNormal.z * vib };
            }
        }
        if (reg.all_of<MeshRendererComponent>(sh.entity)) reg.get<MeshRendererComponent>(sh.entity).color.w = 6.0f * oa;
    }

    if (timer_ >= duration_) {
        for (auto& sh : shards_) if (reg.valid(sh.entity)) scene->DestroyObject(static_cast<uint32_t>(sh.entity));
        shards_.clear(); scene->DestroyObject(static_cast<uint32_t>(entity));
    }
}

void MirrorShatterScript::DrawUI(entt::entity, GameScene*) {
    auto* r = Engine::Renderer::GetInstance(); if (!r) return;
    float dt = 1.0f / 60.0f;
    float oa = std::max(0.0f, (timer_ > duration_ * 0.4f) ? 1.0f - (timer_ - duration_ * 0.4f) / (duration_ * 0.6f) : 1.0f);

    // 1. ヒビ割れ (破砕が始まったら即座に消す)
    if (!noCracks_) {
        float crackAlpha = std::max(0.0f, (timer_ < kCrackTime) ? 1.0f : 1.0f - (timer_ - kCrackTime) / 0.12f);
        if (crackAlpha > 0.0f) {
            for (auto& seg : crackSegments_) {
                if (timer_ < seg.delay) continue;
                float cp = std::min((timer_ - seg.delay) / 0.04f, 1.0f);
                DirectX::XMFLOAT3 ce = { seg.start.x + (seg.end.x - seg.start.x) * cp, seg.start.y + (seg.end.y - seg.start.y) * cp, seg.start.z + (seg.end.z - seg.start.z) * cp };
                float br = (timer_ < kCrackTime + 0.05f) ? 4.0f : 1.0f;
                r->DrawLine3D({seg.start.x, seg.start.y, seg.start.z}, {ce.x, ce.y, ce.z}, {1.4f * br, 1.4f * br, 1.8f * br, oa * crackAlpha * 0.95f}, true);
            }
        }
    }

    // 2. 放射状の光条（Light Streaks）
    for (auto& ls : lightStreaks_) {
        if (timer_ < ls.delay) continue;
        float el = timer_ - ls.delay;
        if (el < ls.life) {
            float alpha = (1.0f - el / ls.life) * oa * 0.8f;
            float progress = std::min(el / 0.035f, 1.0f); // 伸びる速度も上げる
            DirectX::XMFLOAT3 curEnd = { ls.startPos.x + (ls.endPos.x - ls.startPos.x) * progress, ls.startPos.y + (ls.endPos.y - ls.startPos.y) * progress, ls.startPos.z + (ls.endPos.z - ls.startPos.z) * progress };
            r->DrawLine3D({ls.startPos.x, ls.startPos.y, ls.startPos.z}, {curEnd.x, curEnd.y, curEnd.z}, {0.6f, 0.9f, 1.0f, alpha * 0.8f}, true);
        }
    }

    // 3. 中心部の強烈なフラッシュ (フラグがなければ描画)
    if (!noFlash_ && timer_ > kCrackTime && timer_ < kCrackTime + 0.15f) {
        float fz = (timer_ - kCrackTime) * 60.0f;
        float fa = 1.0f - (timer_ - kCrackTime) / 0.15f;
        Engine::Vector4 fc = {2.0f, 2.0f, 2.5f, fa};
        r->DrawLine3D({centerPos_.x - fz, centerPos_.y, centerPos_.z}, {centerPos_.x + fz, centerPos_.y, centerPos_.z}, fc, true);
        r->DrawLine3D({centerPos_.x, centerPos_.y - fz, centerPos_.z}, {centerPos_.x, centerPos_.y + fz, centerPos_.z}, fc, true);
        r->DrawLine3D({centerPos_.x, centerPos_.y, centerPos_.z - fz}, {centerPos_.x, centerPos_.y, centerPos_.z + fz}, fc, true);
    }

    // 4. スパーク（細かい火花）
    for (auto& sp : sparks_) {
        sp.life -= dt; sp.pos.x += sp.velocity.x * dt; sp.pos.y += sp.velocity.y * dt; sp.pos.z += sp.velocity.z * dt;
        if (sp.life > 0.0f) {
            float a = (sp.life / sp.maxLife) * oa;
            r->DrawLine3D({sp.pos.x, sp.pos.y, sp.pos.z}, {sp.pos.x + sp.velocity.x * 0.03f, sp.pos.y + sp.velocity.y * 0.03f, sp.pos.z + sp.velocity.z * 0.03f}, {0.6f, 0.9f, 1.0f, a * 0.7f}, true);
        }
    }
}

void MirrorShatterScript::GenerateCurvedCracks(int numBranches) {
    crackSegments_.clear();
    for (int b = 0; b < numBranches; ++b) {
        float u = 0, v = 0, a = (float)b / numBranches * DirectX::XM_2PI + ((rand() % 100) / 100.0f - 0.5f) * 0.6f;
        float du = std::cos(a) * 0.22f, dv = std::sin(a) * 0.22f;
        for (int s = 0; s < 5; ++s) {
            auto p0 = GetArcPoint(u, v); du += ((rand() % 100) / 100.0f - 0.5f) * 0.07f; dv += ((rand() % 100) / 100.0f - 0.5f) * 0.07f; u += du; v += dv;
            crackSegments_.push_back({p0, GetArcPoint(u, v), (float)s * 0.035f, 0.03f});
        }
    }
}

void MirrorShatterScript::SpawnSparks(int count) {
    sparks_.clear();
    for (int i = 0; i < count; ++i) {
        SparkParticle sp; float u = ((rand() % 200) / 100.0f - 1.0f) * 0.8f, v = ((rand() % 200) / 100.0f - 1.0f) * 0.8f;
        sp.pos = GetArcPoint(u, v); auto n = GetArcNormal(u, v); float spd = 7.0f + (rand() % 100) / 100.0f * 15.0f;
        sp.velocity = { n.x * spd, n.y * spd, n.z * spd }; sp.life = 0.2f + (rand() % 100) / 100.0f * 0.5f; sp.maxLife = sp.life;
        sparks_.push_back(sp);
    }
}

REGISTER_SCRIPT(MirrorShatterScript);
} // namespace Game
