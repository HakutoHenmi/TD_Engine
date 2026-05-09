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

    // 描画（DrawUI）は無効化していますが、破片の生成位置計算（GenerateShards）が
    // GenerateCurvedCracksの結果に依存しているため、計算処理だけは残す必要があります。
    GenerateCurvedCracks((int)(18 * scale)); 
    GenerateShards(scene, (int)(shardCount * scale)); 
    GenerateGlassPanel(scene, (int)(40 * scale)); // ★追加: 空間割れガラス板
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

        scene->SetTag(ent, TagType::VFX);
        auto& mrc = reg.emplace<MeshRendererComponent>(ent);
        mrc.shaderName = "GlassShatter";
        mrc.modelPath = "Resources/Models/cube/cube.obj";
        mrc.modelHandle = renderer->LoadObjMesh(mrc.modelPath);
        int colorType = rand() % 3;
        if (colorType == 0)      mrc.color = {3.5f, 1.2f, 0.2f, 1.0f};
        else if (colorType == 1) mrc.color = {2.5f, 1.8f, 0.4f, 1.0f};
        else                     mrc.color = {4.0f, 0.5f, 0.1f, 1.0f};

        ShardPiece sp; sp.entity = ent; sp.targetPos = arcP; sp.arcNormal = n;
        float spd = (4.0f + (rand() % 100) / 100.0f * 9.0f) * (arcRadius_ / 8.5f);
        sp.velocity = { n.x * spd, n.y * spd, n.z * spd };
        sp.rotSpeed = { ((rand() % 100) / 100.0f - 0.5f) * 14.0f, ((rand() % 100) / 100.0f - 0.5f) * 14.0f, ((rand() % 100) / 100.0f - 0.5f) * 14.0f };
        sp.sizeX = sX; sp.sizeY = sY; sp.appearDelay = seg.delay + (t * 0.06f) + 0.02f;
        shards_.push_back(sp);
    }
}

// ★追加: 空間が割れるガラス板エフェクト（爆発とは別に平面ガラスを生成）
void MirrorShatterScript::GenerateGlassPanel(GameScene* scene, int count) {
    auto* renderer = scene->GetRenderer(); if (!renderer) return;
    auto& reg = scene->GetRegistry();
    float yaw = std::atan2(arcForward_.x, arcForward_.z);
    float pitch = -std::asin(std::max(-1.0f, std::min(1.0f, arcForward_.y)));

    for (int i = 0; i < count; ++i) {
        float angle = ((float)(rand() % 1000) / 1000.0f) * DirectX::XM_2PI;
        float radius = std::sqrt((float)(rand() % 1000) / 1000.0f) * arcRadius_ * 0.6f;
        float localX = std::cos(angle) * radius;
        float localY = std::sin(angle) * radius;
        DirectX::XMFLOAT3 pos = {
            centerPos_.x + arcRight_.x * localX + arcUp_.x * localY,
            centerPos_.y + arcRight_.y * localX + arcUp_.y * localY,
            centerPos_.z + arcRight_.z * localX + arcUp_.z * localY
        };

        auto ent = scene->CreateEntity("GlassPanel_VFX");
        auto& tc = reg.get<TransformComponent>(ent);
        tc.translate = pos; tc.scale = {0, 0, 0};
        tc.rotate = { pitch, yaw, ((rand() % 3600) / 1800.0f) * DirectX::XM_PI };
        float distRatio = radius / std::max(arcRadius_ * 0.6f, 0.1f);
        float sB = (0.3f + distRatio * 0.5f) * (arcRadius_ / 5.0f);
        float sX = sB * (0.6f + (rand() % 100) / 100.0f * 0.5f);
        float sY = sB * (0.6f + (rand() % 100) / 100.0f * 0.5f);
        scene->SetTag(ent, TagType::VFX);
        auto& mrc = reg.emplace<MeshRendererComponent>(ent);
        mrc.shaderName = "GlassShatter";
        mrc.modelPath = "Resources/Models/cube/cube.obj";
        mrc.modelHandle = renderer->LoadObjMesh(mrc.modelPath);
        mrc.color = {0.8f, 0.9f, 1.2f, 0.6f}; // 透明がかったガラス色

        float outX = pos.x - centerPos_.x, outY = pos.y - centerPos_.y, outZ = pos.z - centerPos_.z;
        float outLen = std::sqrt(outX*outX + outY*outY + outZ*outZ);
        if (outLen > 0.01f) { outX /= outLen; outY /= outLen; outZ /= outLen; }
        float fS = (2.0f + (rand() % 100) / 100.0f * 4.0f) * (arcRadius_ / 5.0f);
        float oS = (0.5f + (rand() % 100) / 100.0f * 2.0f) * (arcRadius_ / 5.0f);

        ShardPiece sp; sp.entity = ent; sp.targetPos = pos; sp.arcNormal = arcForward_;
        sp.velocity = { arcForward_.x * fS + outX * oS, arcForward_.y * fS + outY * oS + 0.5f, arcForward_.z * fS + outZ * oS };
        sp.rotSpeed = { ((rand() % 100) / 100.0f - 0.5f) * 6.0f, ((rand() % 100) / 100.0f - 0.5f) * 6.0f, ((rand() % 100) / 100.0f - 0.5f) * 6.0f };
        sp.sizeX = sX; sp.sizeY = sY; sp.appearDelay = distRatio * 0.05f;
        sp.isGlassPanel = true;
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

        // ★ ガラス板は極薄、通常破片は厚め
        if (!sh.isAppeared && timer_ >= sh.appearDelay) {
            sh.isAppeared = true;
            if (sh.isGlassPanel)
                tc.scale = { sh.sizeX, sh.sizeY, sh.sizeX * 0.04f }; // 極薄のガラス板
            else
                tc.scale = { sh.sizeX, sh.sizeY, sh.sizeX * 0.5f };  // 厚みのある破片
        }

        if (sh.isAppeared) {
            float scatterStartTime = sh.appearDelay + freezeTime_;
            if (!sh.isScattering && timer_ >= scatterStartTime) sh.isScattering = true;

            if (sh.isScattering) {
                float el = timer_ - scatterStartTime;

                // 爆発→急減速
                float drag = std::exp(-el * 5.0f);

                tc.translate.x += sh.velocity.x * drag * dt;
                tc.translate.y += sh.velocity.y * drag * dt;
                tc.translate.z += sh.velocity.z * drag * dt;

                sh.velocity.y -= 4.0f * dt; // 重力

                // ヒラヒラと舞い落ちる回転
                tc.rotate.x += sh.rotSpeed.x * drag * dt;
                tc.rotate.y += sh.rotSpeed.y * drag * dt;
                tc.rotate.z += sh.rotSpeed.z * drag * dt;

                // フェードアウト（スケール縮小）
                float lifeRatio = std::max(0.0f, 1.0f - (el / 1.5f));
                float sk = lifeRatio * lifeRatio;
                if (sh.isGlassPanel)
                    tc.scale = { sh.sizeX * sk, sh.sizeY * sk, sh.sizeX * sk * 0.04f };
                else
                    tc.scale = { sh.sizeX * sk, sh.sizeY * sk, sh.sizeX * sk * 0.5f };
            } else {
                // ★ 静止状態: ガラス板として平面に固定（微振動）
                float vib = std::sin(timer_ * 60.0f) * 0.005f;
                tc.translate = {
                    sh.targetPos.x + sh.arcNormal.x * vib,
                    sh.targetPos.y + sh.arcNormal.y * vib,
                    sh.targetPos.z + sh.arcNormal.z * vib
                };
            }
        }
        if (reg.all_of<MeshRendererComponent>(sh.entity))
            reg.get<MeshRendererComponent>(sh.entity).color.w = 6.0f * oa;
    }

    if (timer_ >= duration_) {
        for (auto& sh : shards_) if (reg.valid(sh.entity)) scene->DestroyObject(static_cast<uint32_t>(sh.entity));
        shards_.clear(); scene->DestroyObject(static_cast<uint32_t>(entity));
    }
}

void MirrorShatterScript::DrawUI(entt::entity, GameScene*) {
    // ユーザーの要望により、余分な線やパーティクルの描画は一旦すべて削除しました。
    // メインのクリスタル破片のみが描画されます。
}

void MirrorShatterScript::GenerateCurvedCracks(int numBranches) {
    crackSegments_.clear();

    // ========== 1. 放射状の主亀裂（Radial cracks） ==========
    // 中心から外側へ向かう主要なひび割れ線。各ブランチは8〜12セグメント
    struct BranchTip { float u, v, angle; };
    std::vector<BranchTip> tips; // 分岐先の記録（同心円リング接続用）

    for (int b = 0; b < numBranches; ++b) {
        float u = 0, v = 0;
        float a = (float)b / numBranches * DirectX::XM_2PI + ((rand() % 100) / 100.0f - 0.5f) * 0.4f;
        float speed = 0.15f + (rand() % 100) / 100.0f * 0.08f;
        float du = std::cos(a) * speed, dv = std::sin(a) * speed;

        int segCount = 8 + rand() % 5; // 8〜12セグメント（以前は5）
        for (int s = 0; s < segCount; ++s) {
            auto p0 = GetArcPoint(u, v);
            // わずかにランダムに曲がる
            du += ((rand() % 100) / 100.0f - 0.5f) * 0.05f;
            dv += ((rand() % 100) / 100.0f - 0.5f) * 0.05f;
            u += du; v += dv;
            float delay = (float)s * 0.02f; // 中心から外へ順に出現
            float width = 0.04f * (1.0f - (float)s / segCount * 0.6f); // 中心太い→先細り
            crackSegments_.push_back({p0, GetArcPoint(u, v), delay, width});

            // 途中で二次分岐を生成（確率30%）
            if (s > 1 && s < segCount - 1 && (rand() % 100) < 30) {
                float ba = a + ((rand() % 100) / 100.0f - 0.5f) * 1.5f; // 親からずれた角度
                float bu = u, bv = v;
                float bdu = std::cos(ba) * speed * 0.7f, bdv = std::sin(ba) * speed * 0.7f;
                int bSegCount = 3 + rand() % 3;
                for (int bs = 0; bs < bSegCount; ++bs) {
                    auto bp0 = GetArcPoint(bu, bv);
                    bdu += ((rand() % 100) / 100.0f - 0.5f) * 0.06f;
                    bdv += ((rand() % 100) / 100.0f - 0.5f) * 0.06f;
                    bu += bdu; bv += bdv;
                    crackSegments_.push_back({bp0, GetArcPoint(bu, bv), delay + (float)bs * 0.02f, width * 0.6f});
                }
            }
        }
        tips.push_back({u, v, a});
    }

    // ========== 2. 同心円リング（Concentric rings） ==========
    // 放射状ひびを横方向に繋ぐ円弧状のひび。ガラスのリアルな割れ方を再現
    int ringCount = 3 + (int)(arcRadius_ / 3.0f);
    for (int r = 1; r <= ringCount; ++r) {
        float ringRadius = (float)r / (ringCount + 1);
        float ringDelay = ringRadius * 0.15f;
        int arcSegments = 12 + r * 4; // 外側ほど細かい弧
        for (int a = 0; a < arcSegments; ++a) {
            float t0 = (float)a / arcSegments;
            float t1 = (float)(a + 1) / arcSegments;
            // u,v 座標系での円弧
            float angle0 = t0 * DirectX::XM_2PI;
            float angle1 = t1 * DirectX::XM_2PI;
            float ru0 = std::cos(angle0) * ringRadius * 0.9f;
            float rv0 = std::sin(angle0) * ringRadius * 0.9f;
            float ru1 = std::cos(angle1) * ringRadius * 0.9f;
            float rv1 = std::sin(angle1) * ringRadius * 0.9f;
            // わずかなランダムな歪み
            ru0 += ((rand() % 100) / 100.0f - 0.5f) * 0.02f;
            rv0 += ((rand() % 100) / 100.0f - 0.5f) * 0.02f;
            crackSegments_.push_back({GetArcPoint(ru0, rv0), GetArcPoint(ru1, rv1), ringDelay, 0.02f});
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

void MirrorShatterScript::Draw(entt::entity /*entity*/, GameScene* scene) {
    auto* renderer = scene->GetRenderer();
    if (!renderer || noCracks_) return;

    // フェードアウト制御: freezeTime後に急速にフェードアウト
    float fadeStart = freezeTime_;
    float fadeDuration = 0.6f;
    float alpha = 1.0f;
    if (timer_ > fadeStart) {
        alpha = std::max(0.0f, 1.0f - (timer_ - fadeStart) / fadeDuration);
    }
    if (alpha <= 0.0f) return;

    for (const auto& seg : crackSegments_) {
        if (timer_ < seg.delay) continue; // まだ出現していないひび

        // ひびが出現してからの経過時間で「広がりアニメーション」を計算
        float segAge = timer_ - seg.delay;
        float appear = std::min(1.0f, segAge / 0.05f); // 0.05秒で完全出現

        Engine::Vector3 start = {seg.start.x, seg.start.y, seg.start.z};
        Engine::Vector3 end   = {seg.end.x, seg.end.y, seg.end.z};

        // まだ完全に伸びきっていない場合、途中まで描画（リアルな伸び表現）
        if (appear < 1.0f) {
            end.x = start.x + (end.x - start.x) * appear;
            end.y = start.y + (end.y - start.y) * appear;
            end.z = start.z + (end.z - start.z) * appear;
        }

        // ひびの太さに応じた色（太い=中心=白熱、細い=外側=暗いオレンジ）
        float intensity = seg.width / 0.04f; // 0〜1
        // 白熱（中心）→ オレンジ → 暗い赤銅色のグラデーション
        float r = (1.5f + intensity * 2.5f) * alpha;
        float g = (0.4f + intensity * 1.2f) * alpha;
        float b = (0.05f + intensity * 0.15f) * alpha;

        Engine::Vector4 color = {r, g, b, alpha * appear};

        // メインライン
        renderer->DrawLine3D(start, end, color, false);

        // 太いひびは複数の平行線で厚みを表現
        if (seg.width > 0.025f) {
            // ひびの方向ベクトル
            float dx = end.x - start.x;
            float dy = end.y - start.y;
            float dz = end.z - start.z;
            float len = std::sqrt(dx*dx + dy*dy + dz*dz);
            if (len > 0.001f) {
                // 法線方向に少しずらした平行線
                float nx = -dy, ny = dx, nz = 0; // 簡易法線
                float nl = std::sqrt(nx*nx + ny*ny + nz*nz);
                if (nl > 0.001f) {
                    float offset = seg.width * 0.3f;
                    nx = nx / nl * offset;
                    ny = ny / nl * offset;
                    nz = nz / nl * offset;

                    Engine::Vector4 sideColor = {r * 0.6f, g * 0.5f, b * 0.3f, alpha * appear * 0.7f};
                    renderer->DrawLine3D(
                        {start.x + nx, start.y + ny, start.z + nz},
                        {end.x + nx, end.y + ny, end.z + nz},
                        sideColor, false);
                    renderer->DrawLine3D(
                        {start.x - nx, start.y - ny, start.z - nz},
                        {end.x - nx, end.y - ny, end.z - nz},
                        sideColor, false);
                }
            }
        }
    }
}

REGISTER_SCRIPT(MirrorShatterScript);
} // namespace Game
