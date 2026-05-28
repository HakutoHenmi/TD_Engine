#include "SkyGunner.h"
#include "../ScriptEngine.h"
#include "../ScriptUtils.h"
// 攻撃のクールタイム
static inline const float kCooltime = 2.0f;
namespace Game {
void SkyGunner::Start(entt::entity entity, GameScene* scene) {
	// BaseEnemyのStartが呼ばれる前にFlyにしておく
	type_ = Fly;
	BaseEnemy::Start(entity, scene);
	hp_ = 40.0f;
	maxHp_ = 40.0f;
	searchRange_ = 35.0f;
	attackRange_ = 25.0f; // 少し遠くから撃つように広め
	attackCooltime_ = kCooltime;
	SetCategory(entity, scene, Attacker);
}
void SkyGunner::ExecuteAttack(entt::entity entity, GameScene* scene, float /*dt*/) {
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
	bulletTransform.translate.y -= 0.5f; // 飛んでるので少し下(お腹のあたり)から発射
	bulletTransform.rotate = myTransform.rotate;
	bulletTransform.scale = {0.3f, 0.3f, 0.3f};
	auto* renderer = scene->GetRenderer();
	if (renderer) {
		MeshRendererComponent& bulletMeshRenderer = registry.emplace<MeshRendererComponent>(bullet);
		bulletMeshRenderer.modelHandle = renderer->LoadObjMesh("Resources/Models/cube/cube.obj");
		bulletMeshRenderer.textureHandle = renderer->LoadTexture2D("Resources/Textures/white1x1.png");
		// 普通のGunnerと見分けがつくように弾を水色にしてみる
		bulletMeshRenderer.color = {0.2f, 0.8f, 1.0f, 1.0f};
	}
	HitboxComponent& bulletHitbox = registry.emplace<HitboxComponent>(bullet);
	bulletHitbox.isActive = true;
	bulletHitbox.damage = 10.0f;
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
REGISTER_SCRIPT(SkyGunner);
} // namespace Game