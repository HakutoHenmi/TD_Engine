#include "Canon.h"
#include "BulletScript.h"
#include "BulletScript.h"
#include "ObjectTypes.h"
#include "Scenes/GameScene.h"
#include "ScriptEngine.h"
#include "ScriptUtils.h"
#include <algorithm>
#include <cmath>
#include <unordered_set>
#include <vector>
#ifdef USE_IMGUI
#include "../../externals/imgui/imgui.h"
#endif

namespace Game {

static bool HasTag(entt::registry& registry, entt::entity entity, TagType tagName) {
	if (!registry.valid(entity)) {
		return false;
	}

	if (!registry.all_of<TagComponent>(entity)) {
		return false;
	}

	return registry.get<TagComponent>(entity).tag == tagName;
}



void Canon::Start(entt::entity entity, GameScene* scene) {
	attackTimer_ = 0.0f;
	baseEntity_ = entt::null;

	if (!scene) {
		return;
	}

	entt::registry& registry = scene->GetRegistry();

	baseEntity_ = registry.create();



	if (registry.all_of<TransformComponent>(entity)) {
		TransformComponent& canonTransform = registry.get<TransformComponent>(entity);




		TransformComponent& baseTransform = registry.emplace<TransformComponent>(baseEntity_);
		baseTransform.translate = canonTransform.translate;
		baseTransform.rotate = {0.0f, canonTransform.rotate.y, 0.0f};
		baseTransform.scale = canonTransform.scale;
	}

	auto* renderer = scene->GetRenderer();
	if (renderer) {
		MeshRendererComponent& baseRenderer = registry.emplace<MeshRendererComponent>(baseEntity_);
		baseRenderer.modelHandle = renderer->LoadObjMesh("Resources/Models/CanonBase/CanonBase.obj");
		baseRenderer.textureHandle = renderer->LoadTexture2D("Resources/Textures/canonbase.png");
	}

	if (!registry.all_of<HealthComponent>(entity)) {
		HealthComponent& hc = registry.emplace<HealthComponent>(entity);
		hc.hp = 100.0f;
		hc.maxHp = 100.0f;
	}

	if (!registry.all_of<HurtboxComponent>(entity)) {
		HurtboxComponent& hurtbox = registry.emplace<HurtboxComponent>(entity);
		hurtbox.size = {2.0f, 2.0f, 2.0f};
	}

	if (!registry.all_of<WorldSpaceUIComponent>(entity)) {
		registry.emplace<WorldSpaceUIComponent>(entity);
	}

	if (!registry.all_of<BuffComponent>(entity)) {
		registry.emplace<BuffComponent>(entity);
	}
}

void Canon::Update(entt::entity entity, GameScene* scene, float dt) {
	if (!scene) {
		return;
	}

	entt::registry& registry = scene->GetRegistry();

	if (!registry.valid(entity)) {
		return;
	}



	entt::entity gm = entt::null;
	auto viewScript = registry.view<ScriptComponent>();
	for (auto e : viewScript) {
		const auto& sc = viewScript.get<ScriptComponent>(e);
		for (const auto& instance : sc.scripts) {
			if (instance.scriptPath == "PhaseSystemScript" || instance.scriptPath == "TutorialScript") {
				gm = e;
				break;
			}
		}
		if (gm != entt::null) break;
	}



	if (gm != entt::null) {
		skillPowerRate = GetVar(gm, scene, "AttackPowerRateCanon", 1.0f);
		skillSpeedRate = GetVar(gm, scene, "AttackSpeedRateCanon", 1.0f);
		skillRangeRate = GetVar(gm, scene, "AttackRangeRateCanon", 1.0f);
	}

	float currentAttackInterval = attackInterval_ / skillSpeedRate;
	float currentRange = attackRange_ * skillRangeRate;
	float currentDamage = damage_ * skillPowerRate;

	// ★追加: プレイヤーからの距離によるバフ適用
	if (registry.all_of<BuffComponent>(entity)) {
		auto& buff = registry.get<BuffComponent>(entity);
		buff.isBuffed = false;

		auto players = scene->GetEntitiesByTag(TagType::Player);
		if (!players.empty() && registry.valid(players[0])) {
			if (registry.all_of<TransformComponent>(players[0])) {
				auto& pTrans = registry.get<TransformComponent>(players[0]);
				auto& cTrans = registry.get<TransformComponent>(entity);
				float dx = pTrans.translate.x - cTrans.translate.x;
				float dy = pTrans.translate.y - cTrans.translate.y;
				float dz = pTrans.translate.z - cTrans.translate.z;
				float dist = std::sqrt(dx*dx + dy*dy + dz*dz);

				if (dist <= buff.buffRadius) {
					buff.isBuffed = true;
					// 大砲のバフ効果：連射速度（インターバル）が短縮され、攻撃力も少し上がる
					currentAttackInterval /= buff.buffMultiplier; 
					currentDamage *= 1.2f; // バフ時は威力も20%UP
				}
			}
		}
	}

	currentAttackInterval_ = currentAttackInterval;
	SetVar(entity, scene, "AttackRange", currentRange);
	// Debug(isConnectedToTank_); // ★削除: Update 内での ImGui 呼び出しは例外の原因となる可能性があるため

	if (attackTimer_ > 0.0f) {
		attackTimer_ -= dt;
	}



	TransformComponent& canonTransform = registry.get<TransformComponent>(entity);

	// ★追加: 常時立ち上る美しい蒸気エフェクト (SpaceShatterScriptのProceduralSmokeを応用)
	idleSteamTimer_ -= dt;
	if (idleSteamTimer_ <= 0.0f) {
		idleSteamTimer_ = 1.0f; // ★最適化: 0.2秒→1.0秒に削減（エンティティ生成を80%削減）

		entt::entity idleSteam = scene->CreateEntity("CanonIdleSteam_VFX");
		scene->SetTag(idleSteam, TagType::VFX);
		auto& isTrans = registry.get<TransformComponent>(idleSteam);
		isTrans.translate = canonTransform.translate;
		isTrans.translate.y += 2.4f; // ゲージとの干渉を避けるため、発生位置を少し高めに調整

		auto& isVc = registry.emplace<VariableComponent>(idleSteam);
		isVc.SetValue("NormalX", 0.0f);
		isVc.SetValue("NormalY", 1.0f); // 真上に噴き上がらせる
		isVc.SetValue("NormalZ", 0.0f);
		isVc.SetValue("Radius", 2.2f); // 煙が大きく豊かに広がるサイズ
		isVc.SetValue("Duration", 2.5f); // 上空高くゆっくり立ち上って消えるまで長めに設定（ゲージをはるか上に通過）
		isVc.SetValue("ScatterMode", 0.0f);
		isVc.SetValue("ScatterSpeed", 11.0f); // 速度を倍増させ、ゲージの位置を瞬時に通過させる
		isVc.SetValue("Count", 6.0f); // 1回あたりの煙の量を増やして豪華に
		isVc.SetValue("IsFlight", 1.0f); // 火花は出さず、美しい蒸気（煙）だけを生成

		auto& isSc = registry.emplace<ScriptComponent>(idleSteam);
		isSc.scripts.push_back({"SpaceShatterScript", "", nullptr});
	}

	if (registry.valid(currentTarget_)) {
		if (!registry.all_of<TransformComponent>(currentTarget_)) {
			currentTarget_ = entt::null;
		}
	} else {
		currentTarget_ = entt::null;
	}
	if (currentTarget_ != entt::null) {

		if (!registry.valid(currentTarget_)) {
			currentTarget_ = entt::null;
		}

		if (currentTarget_ != entt::null) {
			if (!registry.all_of<TransformComponent>(currentTarget_)) {
				currentTarget_ = entt::null;
			}
		}

		if (currentTarget_ != entt::null) {

			TransformComponent& currentTargetTransform = registry.get<TransformComponent>(currentTarget_);

			float diffX = currentTargetTransform.translate.x - canonTransform.translate.x;

			float diffY = currentTargetTransform.translate.y - canonTransform.translate.y;

			float diffZ = currentTargetTransform.translate.z - canonTransform.translate.z;

			float distanceToCurrentTarget = std::sqrt(diffX * diffX + diffY * diffY + diffZ * diffZ);

			if (distanceToCurrentTarget > currentRange) {
				currentTarget_ = entt::null;
			}
		}
	}
	if (currentTarget_ == entt::null) {
		float bestDistance = currentRange;

		const std::vector<entt::entity>& enemies = scene->GetEntitiesByTag(TagType::Enemy);

		for (entt::entity other : enemies) {
			if (!registry.valid(other)) {
				continue;
			}

			if (!registry.all_of<TransformComponent>(other)) {
				continue;
			}

			TransformComponent& enemyTransform = registry.get<TransformComponent>(other);

			float diffX = enemyTransform.translate.x - canonTransform.translate.x;
			float diffY = enemyTransform.translate.y - canonTransform.translate.y;
			float diffZ = enemyTransform.translate.z - canonTransform.translate.z;

			float distance = std::sqrt(diffX * diffX + diffY * diffY + diffZ * diffZ);

			if (distance < bestDistance) {
				bestDistance = distance;
				currentTarget_ = other;
			}
		}
	}

	if (currentTarget_ == entt::null) {
		return;
	}

	TransformComponent& targetTransform = registry.get<TransformComponent>(currentTarget_);

	float toX = targetTransform.translate.x - canonTransform.translate.x;
	float toZ = targetTransform.translate.z - canonTransform.translate.z;

	if (std::fabs(toX) < 0.0001f && std::fabs(toZ) < 0.0001f) {
		return;
	}

float desiredYaw = std::atan2(toX, toZ) + 3.14159265f;

	float toY = targetTransform.translate.y - canonTransform.translate.y;
	float distanceXZ = std::sqrt(toX * toX + toZ * toZ);
	float desiredPitch = std::atan2(toY, distanceXZ);

	canonTransform.rotate.y = desiredYaw;
	canonTransform.rotate.x = desiredPitch;

if (attackTimer_ > 0.0f) {

		if (currentAttackInterval_ > 0.0f) {

			float rate = 1.0f - (attackTimer_ / currentAttackInterval_);

			if (rate < 0.0f) {
				rate = 0.0f;
			}

			if (rate > 1.0f) {
				rate = 1.0f;
			}

			SetVar(entity, scene, "CoolTimeRate", rate);
		}

		return;
	}

	entt::entity bullet = registry.create();

	TagComponent& bulletTag = registry.emplace<TagComponent>(bullet);
	bulletTag.tag = TagType::Bullet;

	TransformComponent& bulletTransform = registry.emplace<TransformComponent>(bullet);
	bulletTransform.translate = canonTransform.translate;

	float baseHeight = 0.0f;
	bulletTransform.translate.y += baseHeight;

	float muzzleOffset = -2.5f;
	float cosX = std::cos(canonTransform.rotate.x);
	float sinX = std::sin(canonTransform.rotate.x);

	bulletTransform.translate.x += std::sin(canonTransform.rotate.y) * cosX * muzzleOffset;
	bulletTransform.translate.y += -sinX * muzzleOffset;
	bulletTransform.translate.z += std::cos(canonTransform.rotate.y) * cosX * muzzleOffset;

	bulletTransform.rotate = canonTransform.rotate;
	bulletTransform.scale = {0.3f, 0.3f, 0.3f};

	auto* renderer = scene->GetRenderer();
	if (renderer) {
		MeshRendererComponent& bulletMeshRenderer = registry.emplace<MeshRendererComponent>(bullet);
		bulletMeshRenderer.modelHandle = renderer->LoadObjMesh("Resources/Models/cube/cube.obj");
		bulletMeshRenderer.textureHandle = renderer->LoadTexture2D("Resources/Textures/white1x1.png");
	}

	HitboxComponent& bulletHitbox = registry.emplace<HitboxComponent>(bullet);
	bulletHitbox.isActive = true;
	bulletHitbox.damage = currentDamage;
	bulletHitbox.tag = TagType::Bullet;
	bulletHitbox.size = {1.0f, 1.0f, 1.0f};

	ScriptComponent& bulletScriptComponent = registry.emplace<ScriptComponent>(bullet);
	bulletScriptComponent.scripts.push_back({"BulletScript", "", nullptr});

	SetVar(bullet, scene, "HasTarget", 1.0f);
	SetVar(bullet, scene, "TargetEntity", static_cast<float>(static_cast<uint32_t>(currentTarget_)));
	attackTimer_ = currentAttackInterval_;
	SetVar(entity, scene, "CoolTimeRate", 0.0f);

	// --- マズルフラッシュエフェクト (軽量かつ高級感のある専用設計) ---
	entt::entity muzzleVfx = scene->CreateEntity("CanonMuzzle_VFX");
	scene->SetTag(muzzleVfx, TagType::VFX);
	TransformComponent& vfxTrans = registry.get<TransformComponent>(muzzleVfx);
	vfxTrans.translate = bulletTransform.translate;

	// 1. 銃口からの火花＆スモーク（Playerがブーストで使用するSpaceShatterScriptを応用し、同じ美しい表現に統一）
	float dirX = std::sin(canonTransform.rotate.y) * cosX;
	float dirY = sinX;
	float dirZ = std::cos(canonTransform.rotate.y) * cosX;

	auto& vc = registry.emplace<VariableComponent>(muzzleVfx);
	vc.SetValue("NormalX", dirX);
	vc.SetValue("NormalY", dirY);
	vc.SetValue("NormalZ", dirZ);
	vc.SetValue("Radius", 3.5f);
	vc.SetValue("Duration", 0.5f);
	vc.SetValue("ScatterMode", 0.0f); // マズルエフェクトモード
	vc.SetValue("ScatterSpeed", 18.0f);
	vc.SetValue("Count", 30.0f);
	vc.SetValue("IsFlight", 0.0f); // スパークと煙の両方を出す

	auto& sc = registry.emplace<ScriptComponent>(muzzleVfx);
	sc.scripts.push_back({"SpaceShatterScript", "", nullptr});

	// 2. 大砲の銃身から上に伸びる煙・蒸気（発射時の排気・反動演出、同じSpaceShatterScriptの仕組みで美しい煙のみを真上に放出）
	entt::entity aroundSmoke = scene->CreateEntity("CanonAround_Smoke_VFX");
	scene->SetTag(aroundSmoke, TagType::VFX);
	auto& asTrans = registry.get<TransformComponent>(aroundSmoke);
	asTrans.translate = canonTransform.translate;
	asTrans.translate.y += 2.4f; // 発生高度を少し高めにしてゲージをクリア

	auto& asVc = registry.emplace<VariableComponent>(aroundSmoke);
	asVc.SetValue("NormalX", 0.0f);
	asVc.SetValue("NormalY", 1.0f); // 真上に向けて吹き出させる
	asVc.SetValue("NormalZ", 0.0f);
	asVc.SetValue("Radius", 2.0f); // 煙が広がる半径
	asVc.SetValue("Duration", 1.2f); // 寿命を伸ばし上空高くへ立ち上らせる
	asVc.SetValue("ScatterMode", 0.0f);
	asVc.SetValue("ScatterSpeed", 15.0f); // 噴射速度をほぼ倍増させ、ゲージの上へと一瞬で突き抜けさせる
	asVc.SetValue("Count", 15.0f); // 煙の密度
	asVc.SetValue("IsFlight", 1.0f); // 1.0fを設定することで火花（Shard）は出さず、美しい蒸気（煙）だけを生成

	auto& asSc = registry.emplace<ScriptComponent>(aroundSmoke);
	asSc.scripts.push_back({"SpaceShatterScript", "", nullptr});

	// 地面が光る演出（PointLightComponent）は完全に削除しました。
}

void Canon::OnDestroy(entt::entity entity, GameScene* scene) {
	(void)entity; // 未使用警告回避baws
	if (!scene) {
		return;
	}

	entt::registry& registry = scene->GetRegistry();

	if (registry.valid(baseEntity_)) {
		registry.destroy(baseEntity_);
	}
}

void Canon::OnEditorUI() {
#if defined(USE_IMGUI) && !defined(NDEBUG)
	ImGui::DragFloat("Rotate Speed", &rotationSpeed_, 0.01f, 0.1f, 10.0f);
	ImGui::DragFloat("Attack Range", &attackRange_, 0.1f, 1.0f, 100.0f);
	ImGui::DragFloat("Attack Interval", &attackInterval_, 0.01f, 0.1f, 10.0f);

	ImGui::Separator();
	ImGui::Text("Status (Debug)");
	ImGui::Text("Rotation: %.2f", rotationSpeed_);
#endif
}


void Canon::DrawUI(entt::entity entity, GameScene* scene) {
	if (!scene) {
		return;
	}

	if (!scene->GetRenderer()) {
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

	DirectX::XMFLOAT3 uiWorldPosition;
	uiWorldPosition.x = canonTransform.translate.x;
	uiWorldPosition.y = canonTransform.translate.y + 2.0f;
	uiWorldPosition.z = canonTransform.translate.z;

	float screenX = 0.0f;
	float screenY = 0.0f;

	bool isVisible = UISystem::WorldToScreen(uiWorldPosition, scene->GetCamera(), screenX, screenY);

	if (!isVisible) {
		return;
	}

	float coolTimeRate = 1.0f;

	if (currentAttackInterval_ > 0.0f) {
		coolTimeRate = 1.0f - (attackTimer_ / currentAttackInterval_);
	}

	if (coolTimeRate < 0.0f) {
		coolTimeRate = 0.0f;
	}

	if (coolTimeRate > 1.0f) {
		coolTimeRate = 1.0f;
	}

	Engine::Renderer::SdfUIDesc backDesc;
	backDesc.shape = 1;
	backDesc.centerPx = {screenX, screenY};
	backDesc.sizePx = {44.0f, 44.0f};
	backDesc.color = {0.05f, 0.05f, 0.05f, 0.75f};
	backDesc.progress = 1.0f;
	backDesc.fill = 1.0f;
	backDesc.round = 22.0f;
	backDesc.glow = 0.0f;

	scene->GetRenderer()->DrawSDFUI(backDesc);

	Engine::Renderer::SdfUIDesc gaugeDesc;
	gaugeDesc.shape = 1;
	gaugeDesc.centerPx = {screenX, screenY};
	gaugeDesc.sizePx = {36.0f, 36.0f};
	gaugeDesc.color = {0.2f, 0.8f, 1.0f, 1.0f};
	gaugeDesc.progress = coolTimeRate;
	gaugeDesc.fill = 0.0f;
	gaugeDesc.round = 18.0f;
	gaugeDesc.glow = 1.0f;

	scene->GetRenderer()->DrawSDFUI(gaugeDesc);
}
REGISTER_SCRIPT(Canon);

} // namespace Game