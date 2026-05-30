#include "Boss.h"
#include "../ScriptEngine.h"
#include "../ScriptUtils.h"
#include "ObjectTypes.h"

namespace Game {

void Boss::Start(entt::entity entity, GameScene* scene) {
	// ボスは歩行タイプ
	type_ = Walk;
	BaseEnemy::Start(entity, scene);

	// ボスのステータス設定
	hp_ = 2000.0f;
	maxHp_ = 2000.0f;
	speed_ = 5.0f; // ボスなので遅め
	searchRange_ = 30.0f;
	attackRange_ = 5.0f;

	// ボスとしてのカテゴリ設定（タンク扱いで他の敵の進行基準になるなど）
	SetCategory(entity, scene, Tank);

	auto& registry = scene->GetRegistry();

	// ボス本体のサイズと当たり判定を大きくする
	if (registry.all_of<TransformComponent>(entity)) {
		auto& tc = registry.get<TransformComponent>(entity);
		tc.scale = { 4.0f, 4.0f, 4.0f };
	}
	if (registry.all_of<HurtboxComponent>(entity)) {
		auto& hurtbox = registry.get<HurtboxComponent>(entity);
		hurtbox.size = { 4.0f, 4.0f, 4.0f };
	}
	// HPバーを少し上に調整
	if (registry.all_of<WorldSpaceUIComponent>(entity)) {
		auto& ui = registry.get<WorldSpaceUIComponent>(entity);
		ui.offset = { 0.0f, 4.5f, 0.0f };
		ui.barWidth = 150.0f; // ボス用の長いHPバー
	}

	// ---------------------------------------------------------
	// シールド（ドーム）エンティティの生成
	// ---------------------------------------------------------
	shieldEntity_ = registry.create();
	auto& shieldTc = registry.get_or_emplace<TransformComponent>(shieldEntity_);
	if (registry.all_of<TransformComponent>(entity)) {
		shieldTc.translate = registry.get<TransformComponent>(entity).translate;
	}
	shieldTc.scale = { shieldRadius_, shieldRadius_, shieldRadius_ };

	auto& shieldHc = registry.emplace<HealthComponent>(shieldEntity_);
	shieldHc.hp = 1000.0f;
	shieldHc.maxHp = 1000.0f;

	auto& shieldHurtbox = registry.emplace<HurtboxComponent>(shieldEntity_);
	// 大きなドーム状の当たり判定（直径がshieldRadius_ * 2程度）
	shieldHurtbox.size = { shieldRadius_ * 2.0f, shieldRadius_ * 2.0f, shieldRadius_ * 2.0f };
	shieldHurtbox.tag = TagType::Enemy;
	shieldHurtbox.enabled = true;

	auto* renderer = scene->GetRenderer();
	if (renderer) {
		auto& shieldMr = registry.emplace<MeshRendererComponent>(shieldEntity_);
		// skydome.obj をドームモデルとして代用（球体などがベターですが現状あるものを利用）
		shieldMr.modelHandle = renderer->LoadObjMesh("Resources/Models/cube/cube.obj"); 
		shieldMr.textureHandle = renderer->LoadTexture2D("Resources/Textures/white1x1.png");
		shieldMr.color = { 0.2f, 0.5f, 1.0f, 0.3f }; // 青っぽく半透明
		shieldMr.shaderName = "Transparent"; // 半透明用のシェーダーを指定
		shieldMr.enabled = true;
	}

	auto& shieldTag = registry.emplace<TagComponent>(shieldEntity_);
	shieldTag.tag = TagType::Enemy; // このタグにより、プレイヤー側の弾がシールドに当たるようになる
	
	// 名前をつけてデバッグしやすくする
	auto& shieldName = registry.emplace<NameComponent>(shieldEntity_);
	shieldName.name = "BossShield";
}

void Boss::Update(entt::entity entity, GameScene* scene, float dt) {
	// BaseEnemyの処理（移動やターゲット探索など）
	BaseEnemy::Update(entity, scene, dt);

	auto& registry = scene->GetRegistry();

	// シールドの追従と破壊チェック
	if (registry.valid(shieldEntity_)) {
		if (registry.all_of<HealthComponent>(shieldEntity_)) {
			auto& shieldHc = registry.get<HealthComponent>(shieldEntity_);
			if (shieldHc.isDead) {
				// 耐久が切れたらシールドを破棄
				scene->DestroyObject(static_cast<uint32_t>(shieldEntity_));
				shieldEntity_ = entt::null;
				// シールドが壊れたらボスの当たり判定を復活
				if (registry.all_of<HurtboxComponent>(entity)) {
					registry.get<HurtboxComponent>(entity).enabled = true;
				}
			} else {
				// シールド展開中はボス本体への当たり判定を無効にする（弾をシールドだけに当てさせる）
				if (registry.all_of<HurtboxComponent>(entity)) {
					registry.get<HurtboxComponent>(entity).enabled = false;
				}

				// ボスの座標にシールドを追従させる
				if (registry.all_of<TransformComponent>(entity) && registry.all_of<TransformComponent>(shieldEntity_)) {
					auto& bossTc = registry.get<TransformComponent>(entity);
					auto& shieldTc = registry.get<TransformComponent>(shieldEntity_);
					shieldTc.translate = bossTc.translate;
				}
			}
		} else {
			// HealthComponentが無くなっている（何らかの理由で死んだ等）ならIDを破棄
			shieldEntity_ = entt::null;
			// シールドが壊れたらボスの当たり判定を復活
			if (registry.all_of<HurtboxComponent>(entity)) {
				registry.get<HurtboxComponent>(entity).enabled = true;
			}
		}
	}
}

void Boss::ExecuteAttack(entt::entity entity, GameScene* scene, float /*dt*/) {
	// ボス専用の攻撃処理
	// 現状は近接攻撃などはBaseEnemy側に任せる、もしくは追加の衝撃波などをここで実装可能
	PlayEnemySound(scene, entity, "Resources/Audio/SE/blow.mp3", 1.0f);
}

void Boss::OnDestroy(entt::entity entity, GameScene* scene) {
	// ボス本体が破棄されたらシールドも道連れにする
	if (scene->GetRegistry().valid(shieldEntity_)) {
		scene->DestroyObject(static_cast<uint32_t>(shieldEntity_));
		shieldEntity_ = entt::null;
	}
	BaseEnemy::OnDestroy(entity, scene);
}

REGISTER_SCRIPT(Boss);

} // namespace Game
