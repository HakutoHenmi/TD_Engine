#pragma once
#include "ISystem.h"
#include "../Math/Spline.h"

namespace Game {

class MotionSystem : public ISystem {
public:
    void Update(entt::registry& registry, GameContext& ctx) override {
        auto view = registry.view<MotionComponent, TransformComponent>();
        for (auto entity : view) {
            auto& motion = view.get<MotionComponent>(entity);
            auto& transform = view.get<TransformComponent>(entity);

            if (motion.isPlaying && ctx.isPlaying) {
                motion.currentTime += ctx.dt;
                if (motion.loop) {
                    motion.currentTime = fmod(motion.currentTime, motion.totalDuration);
                } else if (motion.currentTime > motion.totalDuration) {
                    motion.currentTime = motion.totalDuration;
                    motion.isPlaying = false;
                }
            }

            // Apply motion to transform
            ApplyMotion(motion, transform);
        }
    }

    void ApplyMotion(const MotionComponent& motion, TransformComponent& transform) {
        if (motion.keyframes.size() < 2) return;

        // Find current segment
        float t = motion.currentTime;
        
        // Prepare points for spline
        std::vector<DirectX::XMFLOAT3> posPoints;
        for (const auto& kf : motion.keyframes) {
            posPoints.push_back(kf.translate);
        }

        // Map currentTime [0, duration] to spline t [0, count-1]
        float splineT = (t / motion.totalDuration) * (static_cast<float>(motion.keyframes.size()) - 1.0f);
        
        DirectX::XMVECTOR pos = Engine::Spline::Interpolate(posPoints, splineT);
        DirectX::XMStoreFloat3(&transform.translate, pos);

        // Simple linear interpolation for rotate and scale for now
        // TODO: Use Quaternion Spline for rotation
        int i = static_cast<int>(splineT);
        int next = (i + 1 < motion.keyframes.size()) ? i + 1 : i;
        float localT = splineT - static_cast<float>(i);

        transform.rotate.x = motion.keyframes[i].rotate.x * (1.0f - localT) + motion.keyframes[next].rotate.x * localT;
        transform.rotate.y = motion.keyframes[i].rotate.y * (1.0f - localT) + motion.keyframes[next].rotate.y * localT;
        transform.rotate.z = motion.keyframes[i].rotate.z * (1.0f - localT) + motion.keyframes[next].rotate.z * localT;

        transform.scale.x = motion.keyframes[i].scale.x * (1.0f - localT) + motion.keyframes[next].scale.x * localT;
        transform.scale.y = motion.keyframes[i].scale.y * (1.0f - localT) + motion.keyframes[next].scale.y * localT;
        transform.scale.z = motion.keyframes[i].scale.z * (1.0f - localT) + motion.keyframes[next].scale.z * localT;
    }

    void Draw(entt::registry& registry, GameContext& ctx) override {
        // Draw the path line in editor
        if (!ctx.isPlaying) {
            auto view = registry.view<MotionComponent, TransformComponent>();
            for (auto entity : view) {
                auto& motion = view.get<MotionComponent>(entity);
                if (motion.keyframes.size() < 2) continue;

                // Draw segments
                const int segments = 50;
                Engine::Vector3 prevP;
                std::vector<DirectX::XMFLOAT3> posPoints;
                for (const auto& kf : motion.keyframes) posPoints.push_back(kf.translate);

                for (int i = 0; i <= segments; ++i) {
                    float t = (static_cast<float>(i) / segments) * (motion.keyframes.size() - 1);
                    DirectX::XMVECTOR p = Engine::Spline::Interpolate(posPoints, t);
                    Engine::Vector3 currP;
                    DirectX::XMStoreFloat3(reinterpret_cast<DirectX::XMFLOAT3*>(&currP), p);
                    
                    if (i > 0) {
                        ctx.renderer->DrawLine3D(prevP, currP, {1, 1, 0, 1}, true);
                    }
                    prevP = currP;
                }

                // Draw points
                for (int i = 0; i < motion.keyframes.size(); ++i) {
                    Engine::Vector4 color = (i == motion.selectedKeyframe) ? Engine::Vector4{1, 0, 0, 1} : Engine::Vector4{0, 1, 1, 1};
                    Engine::Vector3 p = {motion.keyframes[i].translate.x, motion.keyframes[i].translate.y, motion.keyframes[i].translate.z};
                    // Draw a small cross or box
                    float s = 0.2f;
                    ctx.renderer->DrawLine3D({p.x - s, p.y, p.z}, {p.x + s, p.y, p.z}, color, true);
                    ctx.renderer->DrawLine3D({p.x, p.y - s, p.z}, {p.x, p.y + s, p.z}, color, true);
                    ctx.renderer->DrawLine3D({p.x, p.y, p.z - s}, {p.x, p.y, p.z + s}, color, true);
                }
            }
        }
    }
    void Reset(entt::registry& registry) override {
        auto view = registry.view<MotionComponent>();
        for (auto entity : view) {
            auto& motion = registry.get<MotionComponent>(entity);
            if (motion.enabled) {
                motion.isPlaying = true;
                motion.currentTime = 0.0f;
            }
        }
    }
};

} // namespace Game
