#include "BulletTank.h"
#include "ObjectTypes.h"
#include "Scenes/GameScene.h"
#include "ScriptEngine.h"
#include "Renderer.h"
#include "Camera.h"
#include <cmath>
#include <algorithm>
#include <DirectXMath.h>

namespace Game {
void BulletTank::Start(entt::entity entity, GameScene* scene) {
    if (!scene) return;
    auto& reg = scene->GetRegistry();
    if (!reg.all_of<PointLightComponent>(entity)) {
        auto& light = reg.emplace<PointLightComponent>(entity);
        light.color = {0.2f, 1.0f, 0.8f};
        light.range = 5.0f;
        light.intensity = 0.0f;
    }
}

void BulletTank::Update(entt::entity entity, GameScene* scene, float dt) {
	if (!scene || !scene->GetRegistry().valid(entity)) return;
	auto& registry = scene->GetRegistry();
	timer_ += dt;
	if (!registry.all_of<TransformComponent>(entity)) return;
	registry.get<TransformComponent>(entity).rotate.y += rotationSpeed_ * dt;

	// 呼吸するようなパルス発光 (ゆっくりと明滅)
	float pulse = (std::sin(timer_ * 2.0f) * 0.5f) + 0.5f; // 0.0 ~ 1.0

	// タンク自体をシアン系に明滅させる
	if (registry.all_of<MeshRendererComponent>(entity)) {
		auto& mesh = registry.get<MeshRendererComponent>(entity);
		mesh.color.x = 0.3f + pulse * 0.2f;
		mesh.color.y = 0.3f + pulse * 0.7f;
		mesh.color.z = 0.3f + pulse * 0.7f;
	}

	// タンクの周囲を照らすライト
	if (registry.all_of<PointLightComponent>(entity)) {
		auto& light = registry.get<PointLightComponent>(entity);
		light.intensity = 1.5f + pulse * 4.0f; // 1.5から5.5への明滅
		light.range = 8.0f;
		light.color = {0.2f, 1.0f, 0.8f};
	}
}

void BulletTank::OnDestroy(entt::entity /*entity*/, GameScene* /*scene*/) {}
REGISTER_SCRIPT(BulletTank);
} // namespace Game