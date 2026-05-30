#include "PoisonTrap.h"
#include "ObjectTypes.h"
#include "Scenes/GameScene.h"
#include "ScriptEngine.h"
#include "ScriptUtils.h"
#include "TutorialScript.h"
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



void PoisonTrap::Start(entt::entity entity, GameScene* scene) {
	poisonActiveTimer_ = 0.0f;
	persistentVfxCreated_ = false;
	vfxDelayTimer_ = 0.2f;
	persistentGasVfx_ = entt::null;

	// 緑がかった不気味な見た目に
	auto& registry = scene->GetRegistry();
	if (registry.all_of<MeshRendererComponent>(entity)) {
		auto& mr = registry.get<MeshRendererComponent>(entity);
		mr.color = {0.7f, 1.0f, 0.6f, 1.0f};
	}

	if (!registry.all_of<BuffComponent>(entity)) {
		registry.emplace<BuffComponent>(entity);
	}
	if (!registry.all_of<HealthComponent>(entity)) {
		auto& hc = registry.emplace<HealthComponent>(entity);
		hc.hp = 500.0f;
		hc.maxHp = 500.0f;
	}

	if (!registry.all_of<HurtboxComponent>(entity)) {
		HurtboxComponent& hurtbox = registry.emplace<HurtboxComponent>(entity);
		hurtbox.size = {2.0f, 2.0f, 2.0f}; // 見た目に合わせてサイズは調整してね！
	}
}

void PoisonTrap::Update(entt::entity entity, GameScene* scene, float dt) {
	if (!scene) {
		return;
	}

	entt::registry& registry = scene->GetRegistry();

	if (!registry.valid(entity)) {
		return;
	}



	// --- 待機時エフェクト (Idle VFX) ---
	if (!persistentVfxCreated_) {
		vfxDelayTimer_ -= dt;
		if (vfxDelayTimer_ <= 0.0f) {
			CreatePersistentVFX(entity, scene);
		}
	} else {
		float targetEmitRate = 8.0f;
		if (registry.valid(persistentGasVfx_) && registry.all_of<ParticleEmitterComponent>(persistentGasVfx_)) {
			auto& pec = registry.get<ParticleEmitterComponent>(persistentGasVfx_);
			pec.emitter.params.emitRate = targetEmitRate;
		}
	}


	if (!IsEnemyInRange(entity, scene, poisonRange_)) {
		return;
	}

	//--------------------------------
	// ① 毒を出している時間（青ゲージ）
	//--------------------------------
	if (poisonActiveTimer_ > 0.0f) {
		poisonActiveTimer_ -= dt;

		float rate = poisonActiveTimer_ / finalPoisonActiveTime_;

		if (rate < 0.0f) {
			rate = 0.0f;
		}

		SetVar(entity, scene, "PoisonGaugeRate", rate);
		SetVar(entity, scene, "PoisonGaugeState", 1.0f); // 青

		// 毒終了 → クールダウンへ
		if (poisonActiveTimer_ <= 0.0f) {
			poisonCoolTimer_ = finalPoisonCoolTime_;
		}

		return;
	}

	//--------------------------------
	// ② クールダウン（グレー）
	//--------------------------------
	if (poisonCoolTimer_ > 0.0f) {
		poisonCoolTimer_ -= dt;

		float rate = poisonCoolTimer_ / finalPoisonCoolTime_;

		if (rate < 0.0f) {
			rate = 0.0f;
		}

		SetVar(entity, scene, "PoisonGaugeRate", rate);
		SetVar(entity, scene, "PoisonGaugeState", 2.0f); // グレー

		return;
	}

	//--------------------------------
	// ③ 発射（ここで毒スタート）
	//--------------------------------

	entt::entity gm = entt::null;
	auto viewScript = registry.view<ScriptComponent>();

	for (entt::entity e : viewScript) {
		const ScriptComponent& sc = viewScript.get<ScriptComponent>(e);

		for (const auto& instance : sc.scripts) {
			if (instance.scriptPath == "PhaseSystemScript" || instance.scriptPath == "TutorialScript") {
				gm = e;
				break;
			}
		}

		if (gm != entt::null) {
			break;
		}
	}

	if (gm != entt::null) {
		skillPowerRate_ = GetVar(gm, scene, "AttackPowerRatePoison", 1.0f);
		skillRangeRate_ = GetVar(gm, scene, "AttackRangeRatePoison", 1.0f);
		poisonDurationRate_ = GetVar(gm, scene, "PoisonDurationRate", 1.0f);
		poisonCooldownRate_ = GetVar(gm, scene, "PoisonCooldownRate", 1.0f);
	}

	float finalDamage = poisonDamage_ * skillPowerRate_;
	float finalRange = poisonRange_ * skillRangeRate_;
	finalPoisonActiveTime_ = poisonActiveTime_ * poisonDurationRate_;
	finalPoisonCoolTime_ = poisonCoolTime_ * poisonCooldownRate_;

	// ★追加: プレイヤーからの距離によるバフ適用
	if (registry.all_of<BuffComponent>(entity)) {
		auto& buff = registry.get<BuffComponent>(entity);
		buff.isBuffed = false;

		auto players = scene->GetEntitiesByTag(TagType::Player);
		if (!players.empty() && registry.valid(players[0])) {
			bool auraEnabled = true;
			if (auto* tutorial = TutorialScript::GetInstance(); tutorial && !tutorial->IsAuraEnabled()) {
				auraEnabled = false;
			}
			if (auraEnabled) {
				if (registry.all_of<TransformComponent>(players[0])) {
					auto& pTrans = registry.get<TransformComponent>(players[0]);
					auto& cTrans = registry.get<TransformComponent>(entity);
					float dx = pTrans.translate.x - cTrans.translate.x;
					float dy = pTrans.translate.y - cTrans.translate.y;
					float dz = pTrans.translate.z - cTrans.translate.z;
					float dist = std::sqrt(dx*dx + dy*dy + dz*dz);

					if (dist <= buff.buffRadius) {
						buff.isBuffed = true;
						// ポイズンのバフ効果：範囲拡大、持続時間延長
						finalRange *= buff.buffMultiplier;
						finalPoisonActiveTime_ *= 1.2f;
					}
				}
			}
		}
	}
	CreatePoisonAttackArea(entity, scene, finalDamage, finalRange);

// タイマー開始
	poisonActiveTimer_ = finalPoisonActiveTime_;

	SetVar(entity, scene, "PoisonGaugeRate", 1.0f);
	SetVar(entity, scene, "PoisonGaugeState", 1.0f);
}

void PoisonTrap::OnDestroy(entt::entity /*entity*/, GameScene* scene) {
	if (!scene)
		return;
	auto& registry = scene->GetRegistry();
	if (persistentVfxCreated_) {
		if (registry.valid(persistentGasVfx_)) {
			scene->DestroyObject(static_cast<uint32_t>(persistentGasVfx_));
			persistentGasVfx_ = entt::null;
		}
		persistentVfxCreated_ = false;
	}
}

void PoisonTrap::OnEditorUI() {}


#pragma region HelperFunctions
bool PoisonTrap::IsEnemyInRange(entt::entity entity, GameScene* scene, float range) {
	if (!scene) {
		return false;
	}

	entt::registry& registry = scene->GetRegistry();

	if (!registry.valid(entity)) {
		return false;
	}

	if (!registry.all_of<TransformComponent>(entity)) {
		return false;
	}

	const TransformComponent& trapTransform = registry.get<TransformComponent>(entity);
	const std::vector<entt::entity>& enemies = scene->GetEntitiesByTag(TagType::Enemy);

	for (entt::entity enemy : enemies) {
		if (!registry.valid(enemy)) {
			continue;
		}

		if (!registry.all_of<TransformComponent>(enemy)) {
			continue;
		}

		const TransformComponent& enemyTransform = registry.get<TransformComponent>(enemy);

		float diffX = enemyTransform.translate.x - trapTransform.translate.x;
		float diffY = enemyTransform.translate.y - trapTransform.translate.y;
		float diffZ = enemyTransform.translate.z - trapTransform.translate.z;

		float distance = std::sqrt(diffX * diffX + diffY * diffY + diffZ * diffZ);

		if (distance <= range) {
			return true;
		}
	}

	return false;
}

#pragma endregion

void PoisonTrap::CreatePoisonAttackArea(entt::entity entity, GameScene* scene, float damage, float range) {
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

	const TransformComponent& trapTransform = registry.get<TransformComponent>(entity);

	entt::entity poisonAttackArea = registry.create();

	TagComponent& poisonTag = registry.emplace<TagComponent>(poisonAttackArea);
	poisonTag.tag = TagType::Poison;

	TransformComponent& poisonTransform = registry.get_or_emplace<TransformComponent>(poisonAttackArea);
	poisonTransform.translate = trapTransform.translate;
	poisonTransform.rotate = trapTransform.rotate;

	// Y軸の高さを地形に合わせる(デカールが埋まらないように)
	float groundH = scene->GetHeightAt(poisonTransform.translate.x, poisonTransform.translate.z, poisonTransform.translate.y + 1.0f, static_cast<uint32_t>(poisonAttackArea));
	if (groundH > -5000.0f) {
		poisonTransform.translate.y = groundH + 0.05f; // 地面より少しだけ浮かす
	}

	// デカールなので平面にするためスケールを調整
	poisonTransform.scale = {range / 2.0f, 1.0f, range / 2.0f};

	auto* renderer = scene->GetRenderer();
	if (renderer) {
		// （板ポリゴンのMeshRendererは見た目が不自然になるため削除し、パーティクルのみで表現する）

		// 常に有毒ガスが立ち昇るエフェクトをアタッチ
		auto& gasPec = registry.emplace<ParticleEmitterComponent>(poisonAttackArea);
		gasPec.emitter.params.name = "ToxicGas_Decal";
		gasPec.emitter.params.texturePath = "Resources/Textures/white1x1.png";
		gasPec.emitter.params.emitRate = 45.0f;
		gasPec.emitter.params.shape = Engine::EmissionShape::Sphere;
		gasPec.emitter.params.shapeRadius = range * 0.6f;
		gasPec.emitter.params.lifeTime = 2.0f;
		gasPec.emitter.params.lifeTimeVariance = 0.5f;
		gasPec.emitter.params.startVelocity = {0.0f, 1.8f, 0.0f};
		gasPec.emitter.params.velocityVariance = {1.0f, 0.5f, 1.0f};
		gasPec.emitter.params.startColor = {0.4f, 1.0f, 0.2f, 0.45f};
		gasPec.emitter.params.endColor = {0.2f, 0.8f, 0.0f, 0.0f};
		gasPec.emitter.params.startSize = {2.5f, 2.5f, 2.5f};
		gasPec.emitter.params.endSize = {6.0f, 6.0f, 6.0f};
		gasPec.emitter.params.isAdditive = true;
		gasPec.emitter.params.position = {poisonTransform.translate.x, poisonTransform.translate.y, poisonTransform.translate.z};
		gasPec.emitter.Initialize(*renderer, "ToxicGas_Decal_Emitter");
		gasPec.isInitialized = true;

		// プレイヤーのスチームブーストと同じ表現で毒霧(SpaceShatterScript)を発生させる
		entt::entity blastVfx = scene->CreateEntity("PoisonSteamBlast_VFX");
		scene->SetTag(blastVfx, TagType::VFX);
		auto& sTrans = registry.get<TransformComponent>(blastVfx);
		sTrans.translate = trapTransform.translate;
		sTrans.translate.y += 0.5f;

		auto& bVc = registry.emplace<VariableComponent>(blastVfx);
		bVc.SetValue("NormalX", 0.0f);
		bVc.SetValue("NormalY", 1.0f); // 上方向＋周囲に拡散
		bVc.SetValue("NormalZ", 0.0f);
		bVc.SetValue("Radius", range * 1.5f);
		bVc.SetValue("Duration", 1.0f);
		bVc.SetValue("ScatterMode", 1.0f);   // 爆発的な拡散
		bVc.SetValue("ScatterSpeed", 18.0f); // 速い拡散速度
		bVc.SetValue("Count", 80.0f);        // パーティクル数
		bVc.SetValue("ColorMode", 1.0f);     // ポイズンカラーモード

		auto& bSc = registry.emplace<ScriptComponent>(blastVfx);
		bSc.scripts.push_back({"SpaceShatterScript", "", nullptr});
	}

	HitboxComponent& poisonHitbox = registry.emplace<HitboxComponent>(poisonAttackArea);
	poisonHitbox.isActive = true;
	poisonHitbox.damage = damage;
	poisonHitbox.tag = TagType::Poison;
	poisonHitbox.size = {range, 5.0f, range}; // 高さ判定はある程度持たせる

	ScriptComponent& poisonScript = registry.emplace<ScriptComponent>(poisonAttackArea);
	poisonScript.scripts.push_back({"PoisonAttackArea", "", nullptr});
}

void PoisonTrap::CreatePersistentVFX(entt::entity entity, GameScene* scene) {
	if (!scene || persistentVfxCreated_)
		return;
	auto& registry = scene->GetRegistry();
	Engine::Renderer* renderer = scene->GetRenderer();
	if (!renderer)
		return;

	auto& trapTransform = registry.get<TransformComponent>(entity);

	// 待機時：パイプの継ぎ目から不気味で蛍光色を帯びた緑紫のガスがドロドロ漏れる
	persistentGasVfx_ = scene->CreateEntity("PoisonIdleGas_Persistent");
	scene->SetTag(persistentGasVfx_, TagType::VFX);
	auto& gTrans = registry.get<TransformComponent>(persistentGasVfx_);
	gTrans.translate = trapTransform.translate;
	gTrans.translate.y += 0.8f;

	auto& pec = registry.emplace<ParticleEmitterComponent>(persistentGasVfx_);
	pec.emitter.params.name = "PoisonIdleGas";
	pec.emitter.params.texturePath = "Resources/Textures/white1x1.png";
	pec.emitter.params.emitRate = 25.0f;
	pec.emitter.params.shape = Engine::EmissionShape::Sphere;
	pec.emitter.params.shapeRadius = 1.0f;
	pec.emitter.params.lifeTime = 2.5f;
	pec.emitter.params.lifeTimeVariance = 0.5f;
	pec.emitter.params.startVelocity = {0.0f, -0.6f, 0.0f}; // ドロドロと下に落ちる
	pec.emitter.params.velocityVariance = {0.4f, 0.2f, 0.4f};
	pec.emitter.params.startColor = {0.5f, 0.9f, 0.2f, 0.5f}; // 蛍光黄緑
	pec.emitter.params.endColor = {0.6f, 0.2f, 0.8f, 0.0f};   // 毒々しい紫に変化して消える
	pec.emitter.params.startSize = {1.5f, 1.5f, 1.5f};
	pec.emitter.params.endSize = {3.5f, 3.5f, 3.5f};
	pec.emitter.params.isAdditive = true;
	pec.emitter.params.position = {gTrans.translate.x, gTrans.translate.y, gTrans.translate.z};

	pec.emitter.Initialize(*renderer, "PoisonIdleGas");
	pec.isInitialized = true;

	persistentVfxCreated_ = true;
}

void PoisonTrap::Debug(bool /*connected*/) {
	// 以前はここで ImGui::Begin を呼んでいたが、Update からの呼び出しは危険なため廃止。
	// 代わりに OnEditorUI を使用する。
}

REGISTER_SCRIPT(PoisonTrap);

} // namespace Game