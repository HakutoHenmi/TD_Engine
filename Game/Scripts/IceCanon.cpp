#include "IceCanon.h"
#include "ObjectTypes.h"
#include "Scenes/GameScene.h"
#include "ScriptEngine.h"
#include "ScriptUtils.h"

#include <cmath>

namespace Game {

void IceCanon::Start(entt::entity /*entity*/, GameScene* /*scene*/) { attackTimer_ = 0.0f; }

void IceCanon::Update(entt::entity entity, GameScene* scene, float dt) {
	if (!scene) {
		return;
	}

	entt::registry& registry = scene->GetRegistry();

	if (!registry.valid(entity)) {
		return;
	}

	if (!registry.all_of<TransformComponent>(entity)) {
		return;
	}

	TransformComponent& canonTransform = registry.get<TransformComponent>(entity);

	attackTimer_ -= dt;
	if (attackTimer_ > 0.0f) {
		return;
	}

	// ===== ターゲット探す =====
	entt::entity target = entt::null;
	float bestDistance = attackRange_;

	const std::vector<entt::entity>& enemies = scene->GetEntitiesByTag(TagType::Enemy);

	for (entt::entity other : enemies) {
		if (!registry.valid(other)) {
			continue;
		}

		if (!registry.all_of<TransformComponent>(other)) {
			continue;
		}

		TransformComponent& enemyTransform = registry.get<TransformComponent>(other);

		float dx = enemyTransform.translate.x - canonTransform.translate.x;
		float dy = enemyTransform.translate.y - canonTransform.translate.y;
		float dz = enemyTransform.translate.z - canonTransform.translate.z;

		float distance = std::sqrt(dx * dx + dy * dy + dz * dz);

		if (distance < bestDistance) {
			bestDistance = distance;
			target = other;
		}
	}

	if (target == entt::null) {
		return;
	}

	TransformComponent& targetTransform = registry.get<TransformComponent>(target);

	float toX = targetTransform.translate.x - canonTransform.translate.x;
	float toZ = targetTransform.translate.z - canonTransform.translate.z;

	float yaw = std::atan2(toX, toZ);
	canonTransform.rotate.y = yaw;

	// ===== 弾ばらまき =====
	int bulletCount = 6;
	

	for (int i = 0; i < bulletCount; i++) {

		entt::entity bullet = registry.create();

		TagComponent& bulletTag = registry.emplace<TagComponent>(bullet);
		bulletTag.tag = TagType::Bullet;

		TransformComponent& bulletTransform = registry.emplace<TransformComponent>(bullet);
		bulletTransform.translate = canonTransform.translate;
		bulletTransform.translate.y += 1.0f;

		float angle = (6.283185f / static_cast<float>(bulletCount)) * static_cast<float>(i);
		float flowerRadius = 0.6f;

		bulletTransform.translate.x += std::cos(angle) * flowerRadius;
		bulletTransform.translate.z += std::sin(angle) * flowerRadius;

		float directionX = std::cos(angle);
		float directionZ = std::sin(angle);

		bulletTransform.rotate = canonTransform.rotate;
		bulletTransform.rotate.y = std::atan2(directionX, directionZ);
		bulletTransform.rotate.x = -0.35f;

		bulletTransform.scale = {0.3f, 0.3f, 0.3f};

		// ===== 見た目 =====
		auto* renderer = scene->GetRenderer();
		if (renderer) {
			MeshRendererComponent& mesh = registry.emplace<MeshRendererComponent>(bullet);
			mesh.modelHandle = renderer->LoadObjMesh("Resources/Models/cube/cube.obj");
			mesh.textureHandle = renderer->LoadTexture2D("Resources/Textures/white1x1.png");
		}

		// ===== ダメージ =====
		HitboxComponent& hitbox = registry.emplace<HitboxComponent>(bullet);
		hitbox.isActive = true;
		hitbox.damage = damage_;
		hitbox.tag = TagType::Bullet;
		hitbox.size = {0.5f, 0.5f, 0.5f};

		// ===== スクリプト =====
		ScriptComponent& sc = registry.emplace<ScriptComponent>(bullet);
		sc.scripts.push_back({"IceBulletScript", "", nullptr});

		// ===== 追加情報 =====
		SetVar(bullet, scene, "HasTarget", 1.0f);
		SetVar(bullet, scene, "TargetEntity", (float)(uint32_t)target);
		SetVar(bullet, scene, "StopTime", stopTime_);
	}

	attackTimer_ = attackInterval_;
}

void IceCanon::OnDestroy(entt::entity /*entity*/, GameScene* /*scene*/) {}

REGISTER_SCRIPT(IceCanon);

} // namespace Game