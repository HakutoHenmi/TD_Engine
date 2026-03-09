#include "EnemyDamageBySpike.h"
#include "ObjectTypes.h"
#include "Scenes/GameScene.h"
#include "ScriptEngine.h"
#include <cmath>
#include <vector>

#include "../imgui/imgui.h"
namespace Game {

static bool HasTag(const SceneObject& obj, const char* tagName) {
	for (int i = 0; i < (int)obj.tags.size(); ++i) {
		if (obj.tags[i].tag == tagName) {
			return true;
		}
	}
	return false;
}

static bool IsHitSphere(const SceneObject& a, const SceneObject& b, float hitRange) {
	float dx = b.translate.x - a.translate.x;
	float dy = b.translate.y - a.translate.y;
	float dz = b.translate.z - a.translate.z;

	float dist = std::sqrt(dx * dx + dy * dy + dz * dz);

	if (dist > hitRange) {
		return false;
	}

	return true;
}

void EnemyDamageBySpike::Start(SceneObject& obj, GameScene* scene) {
	(void)obj;
	(void)scene;
}

void EnemyDamageBySpike::Update(SceneObject& obj, GameScene* scene, float dt) {
	damageTimer_ -= dt;

	obj.rotate.y += 1.0f * dt;

	for (const SceneObject& other : scene->GetObjects()) {

		if (!HasTag(other, "Spike")) {
			continue;
		}

		if (!IsHitSphere(obj, other, hitRange_)) {
			continue;
		}

		if ((int)obj.healths.size() <= 0) {
			continue;
		}

		if (damageTimer_ <= 0.0f) {

			obj.healths[0].hp -= damage_;

			// ダメージタイマーにダメージ間隔をセット走することにより、ダメージを1秒ごとに与えることができる
			damageTimer_ = damageInterval_;
		}
	}
}
void EnemyDamageBySpike::IsEnemyTouchingSpike(SceneObject& obj, GameScene* scene, float dt) {

	damageTimer_ -= dt;

	obj.rotate.y += 1.0f * dt;

	for (const SceneObject& other : scene->GetObjects()) {

		if (!HasTag(other, "Spike")) {
			continue;
		}

		if (!IsHitSphere(obj, other, hitRange_)) {
			continue;
		}

		if ((int)obj.healths.size() <= 0) {
			continue;
		}

		if (damageTimer_ <= 0.0f) {

			obj.healths[0].hp -= damage_;

			// ダメージタイマーにダメージ間隔をセット走することにより、ダメージを1秒ごとに与えることができる
			damageTimer_ = damageInterval_;
		}
	}
}


void EnemyDamageBySpike::OnDestroy(SceneObject& obj, GameScene* scene) {
	(void)obj;
	(void)scene;
}

REGISTER_SCRIPT(EnemyDamageBySpike);
} // namespace Game
