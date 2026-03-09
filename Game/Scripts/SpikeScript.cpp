#include "SpikeScript.h"
#include "../imgui/imgui.h"
#include "ObjectTypes.h"
#include "Scenes/GameScene.h"
#include "ScriptEngine.h"
#include <cmath>
#include <vector>

namespace Game {
// タグ走査
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

void SpikeScript::Start(SceneObject& obj, GameScene* scene) {
	(void)obj;
	(void)scene;
}

void SpikeScript::Update(SceneObject& obj, GameScene* scene, float dt) {

	(void)dt;

	for (const SceneObject& other : scene->GetObjects()) {
		if (!HasTag(other, "Enemy")) {
			continue;
		}

		if (!IsHitSphere(obj, other, connectRange)) {
			continue;
		}

		// obj.healths[0].hp -= 10.0f;
	}
}

void SpikeScript::OnDestroy(SceneObject& obj, GameScene* scene) {
	(void)obj;
	(void)scene;
}

} // namespace Game