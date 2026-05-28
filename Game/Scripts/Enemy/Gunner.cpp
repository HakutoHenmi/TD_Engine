#include "Gunner.h"
#include "../ScriptEngine.h"
#include "../ScriptUtils.h"
// 攻撃のクールタイム
static inline const float kCooltime = 2.0f;	// 秒

namespace Game {
void Gunner::Start(entt::entity entity, GameScene* scene) {
	// 継承元のStartを呼んで大まかな部分の初期化
	BaseEnemy::Start(entity, scene);

	hp_ = 50.0f;
	maxHp_ = 50.0f;
	searchRange_ = 25.0f;
	attackRange_ = 20.0f;

	attackCooltime_ = kCooltime;
	SetCategory(entity, scene, Attacker);
}

void Game::Gunner::ExecuteAttack(entt::entity entity, GameScene* scene, float /*dt*/) {
	auto& registry = scene->GetRegistry();

	if (!registry.all_of<TransformComponent>(entity)) {
		return;
	}
	TransformComponent& myTransform = registry.get<TransformComponent>(entity);

	// 弾エンティティを作成
	entt::entity bullet = registry.create();

	TagComponent& bulletTag = registry.emplace<TagComponent>(bullet);
	bulletTag.tag = TagType::EnemyBullet;

	TransformComponent& bulletTransform = registry.get_or_emplace<TransformComponent>(bullet);
	bulletTransform.translate = myTransform.translate;
	bulletTransform.translate.y += 1.0f; // 少し上から発射
	bulletTransform.rotate = myTransform.rotate;
	bulletTransform.scale = {0.3f, 0.3f, 0.3f}; // サイズ

	auto* renderer = scene->GetRenderer();
	if (renderer) {
		MeshRendererComponent& bulletMeshRenderer = registry.emplace<MeshRendererComponent>(bullet);
		bulletMeshRenderer.modelHandle = renderer->LoadObjMesh("Resources/Models/cube/cube.obj");
		bulletMeshRenderer.textureHandle = renderer->LoadTexture2D("Resources/Textures/white1x1.png");
		bulletMeshRenderer.color = {1.0f, 0.2f, 0.2f, 1.0f}; // 赤色
	}

	HitboxComponent& bulletHitbox = registry.emplace<HitboxComponent>(bullet);
	bulletHitbox.isActive = true;
	bulletHitbox.damage = 10.0f; // Gunnerの攻撃力
	bulletHitbox.tag = TagType::EnemyBullet;
	bulletHitbox.size = {1.0f, 1.0f, 1.0f};

	ScriptComponent& bulletScriptComponent = registry.emplace<ScriptComponent>(bullet);
	bulletScriptComponent.scripts.push_back({"BulletScript", "", nullptr});

	if (registry.valid(currentTarget_)) {
		SetVar(bullet, scene, "HasTarget", 1.0f);
		uint32_t targetId = static_cast<uint32_t>(currentTarget_);
		SetVar(bullet, scene, "TargetHigh", static_cast<float>((targetId >> 16) & 0xFFFF));
		SetVar(bullet, scene, "TargetLow", static_cast<float>(targetId & 0xFFFF));
		SetVar(bullet, scene, "TargetEntity", static_cast<float>(targetId));
	} else {
		SetVar(bullet, scene, "HasTarget", 0.0f);
	}

	// クールタイムリセット
	attackCooltime_ = kCooltime;
}

REGISTER_SCRIPT(Gunner);
} // namespace Game