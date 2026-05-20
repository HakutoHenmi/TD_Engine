#include "HitDistortionScript.h"
#include "GameScene.h"
#include <DirectXMath.h>
#include <algorithm>
#include <cmath>
#include "ScriptEngine.h"

namespace Game {

void HitDistortionScript::Start(entt::entity entity, GameScene* scene) {
    OutputDebugStringA("[HitDistortionScript] Start!\n");
    auto& registry = scene->GetRegistry();
    
    // ★追加: VariableComponentからカスタムパラメータを読み取り
    if (registry.all_of<VariableComponent>(entity)) {
        auto& vc = registry.get<VariableComponent>(entity);
        duration_ = vc.GetValue("Duration", duration_);
        startScale_ = vc.GetValue("StartScale", startScale_);
        endScale_ = vc.GetValue("EndScale", endScale_);
        initialAlpha_ = vc.GetValue("InitialAlpha", initialAlpha_);
    }

    if (registry.all_of<TransformComponent>(entity)) {
        auto& tc = registry.get<TransformComponent>(entity);
        tc.scale = {startScale_, startScale_, startScale_};

        // ★追加: 爆発エフェクトより奥になるよう、カメラの向いている方向（Z奥）へ少しずらす
        auto camRot = scene->GetCamera().Rotation();
        float cx = std::sin(camRot.y) * std::cos(camRot.x);
        float cy = -std::sin(camRot.x);
        float cz = std::cos(camRot.y) * std::cos(camRot.x);
        tc.translate.x += cx * 2.0f;
        tc.translate.y += cy * 2.0f;
        tc.translate.z += cz * 2.0f;
    }
}

void HitDistortionScript::Update(entt::entity entity, GameScene* scene, float dt) {
    timer_ += dt;
    float t = std::min(timer_ / duration_, 1.0f);

    auto& registry = scene->GetRegistry();
    
    float riseSpeed = 0.0f;
    if (registry.all_of<VariableComponent>(entity)) {
        auto& vc = registry.get<VariableComponent>(entity);
        riseSpeed = vc.GetValue("RiseSpeed", 0.0f);
    }

    if (registry.all_of<TransformComponent>(entity)) {
        auto& tc = registry.get<TransformComponent>(entity);
        // キレのある拡大: EaseOutExpo
        float scaleVal = startScale_ + (endScale_ - startScale_) * (1.0f - std::powf(2.0f, -10.0f * t));
        tc.scale = {scaleVal, scaleVal, scaleVal};

        // ★追加: 上昇処理 (陽炎用)
        if (riseSpeed > 0.0f) {
            tc.translate.y += riseSpeed * dt;
        }

        // ★追加: ビルボード処理 (常にカメラの方を向く)
        auto camRot = scene->GetCamera().Rotation();
        tc.rotate.x = camRot.x;
        tc.rotate.y = camRot.y + 3.14159265f; // カメラに向けるため180度反転
        tc.rotate.z = camRot.z;
    }

    if (registry.all_of<MeshRendererComponent>(entity)) {
        auto& mrc = registry.get<MeshRendererComponent>(entity);
        // 急激なフェードアウト（Alphaが歪みの強度）
        mrc.color.w = initialAlpha_ * (1.0f - t);
    }

    if (timer_ >= duration_) {
        OutputDebugStringA("[HitDistortionScript] Destroying distortion object.\n");
        scene->DestroyObject(static_cast<uint32_t>(entity));
        return;
    }
}

// 自動登録
REGISTER_SCRIPT(HitDistortionScript);

} // namespace Game
