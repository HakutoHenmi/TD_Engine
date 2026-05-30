#include "Enchanter.h"
#include "../ScriptEngine.h"
#include "../ScriptUtils.h"
#include "ObjectTypes.h"
#include <cmath>
namespace Game {
void Enchanter::Start(entt::entity entity, GameScene* scene) {
	// 歩行タイプ
	type_ = Walk;
	BaseEnemy::Start(entity, scene);
	hp_ = 60.0f;
	maxHp_ = 60.0f;
	searchRange_ = 20.0f;
	attackRange_ = 5.0f;
	// サポート役なのでアタッカー用の待機などはせず、Otherカテゴリにする
	SetCategory(entity, scene, Other);
}
void Enchanter::Update(entt::entity entity, GameScene* scene, float dt) {
	// BaseEnemyの移動や索敵処理を先に呼ぶ
	BaseEnemy::Update(entity, scene, dt);
	// バフ（回復）処理
	buffTimer_ += dt;
	if (buffTimer_ >= buffInterval_) {
		buffTimer_ = 0.0f;
		PlayEnemySound(scene, entity, "Resources/Audio/SE/Enchant.mp3", 0.8f);

		auto& registry = scene->GetRegistry();
		if (!registry.all_of<TransformComponent>(entity)) {
			return;
		}
		auto& myTc = registry.get<TransformComponent>(entity);
		const auto& enemies = scene->GetEntitiesByTag(TagType::Enemy);
		for (auto target : enemies) {
			// 自分自身や無効なエンティティは弾く
			if (!registry.valid(target) || target == entity) {
				continue;
			}
			if (!registry.all_of<TransformComponent>(target)) {
				continue;
			}
			auto& targetTc = registry.get<TransformComponent>(target);
			float dx = targetTc.translate.x - myTc.translate.x;
			float dz = targetTc.translate.z - myTc.translate.z;
			float distSq = dx * dx + dz * dz;
			// 範囲内の敵を回復
			if (distSq <= buffRange_ * buffRange_) {
				if (registry.all_of<HealthComponent>(target)) {
					auto& hc = registry.get<HealthComponent>(target);
					hc.hp += 20.0f; // 20回復
					if (hc.hp > hc.maxHp) {
						hc.hp = hc.maxHp;
					}
					// 回復した合図として少しだけ光らせる
					hc.hitFlashTimer = 0.2f;
				}

				// 2. 移動速度アップ処理 (1.5倍の速度を3.5秒間持続させる)
				if (!registry.all_of<VariableComponent>(target)) {
					registry.emplace<VariableComponent>(target);
				}
				auto& vc = registry.get<VariableComponent>(target);
				vc.SetValue("SpeedBuffMultiplier", 1.5f);
				vc.SetValue("SpeedBuffTimer", 3.5f); // バフをかける間隔(3.0秒)より少し長めにして途切れないようにする
			}
		}
	}
}
void Enchanter::ExecuteAttack(entt::entity /*entity*/, GameScene* /*scene*/, float /*dt*/) {
	// エンチャンターは攻撃手段を持たない（近寄って何もしない）設定
	// もし攻撃もさせたい場合は、Gunnerのような弾を撃つ処理をここに書けばOK！
}
REGISTER_SCRIPT(Enchanter);
} // namespace Game