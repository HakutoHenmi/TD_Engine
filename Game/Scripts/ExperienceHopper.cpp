#include "ExperienceHopper.h"
#include "ScriptEngine.h"

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

// 球体接続判定
static bool IsConnectedSphere(const SceneObject& a, const SceneObject& b, float connectRange) {
	float dx = b.translate.x - a.translate.x;
	float dy = b.translate.y - a.translate.y;
	float dz = b.translate.z - a.translate.z;

	float dist = std::sqrt(dx * dx + dy * dy + dz * dz);

	if (dist > connectRange) {
		return false;
	}

	return true;
}

// 訪問済みチェック
static bool IsAlreadyVisited(const std::vector<const SceneObject*>& visitedObjects, const SceneObject& obj) {
	for (int i = 0; i < (int)visitedObjects.size(); ++i) {
		if (visitedObjects[i] == &obj) {
			return true;
		}
	}
	return false;
}

// Pipe をたどって ExperienceMiner に届くか
static bool IsPipeConnectedToExperienceMinerRecursive(GameScene* scene, const SceneObject& currentPipe, std::vector<const SceneObject*>& visitedObjects, float connectRange) {

	visitedObjects.push_back(&currentPipe);

	for (const SceneObject& other : scene->GetObjects()) {

		if (&other == &currentPipe) {
			continue;
		}

		if (!IsConnectedSphere(currentPipe, other, connectRange)) {
			continue;
		}

		if (HasTag(other, "ExprienceMiner")) {
			return true;
		}

		if (HasTag(other, "Pipe")) {

			if (IsAlreadyVisited(visitedObjects, other)) {
				continue;
			}

			bool connected = IsPipeConnectedToExperienceMinerRecursive(scene, other, visitedObjects, connectRange);

			if (connected) {
				return true;
			}
		}
	}

	return false;
}

// ExperienceHopper から Pipe を通して ExperienceMiner につながるか
static bool IsExperienceHopperConnectedToExperienceMiner(GameScene* scene, const SceneObject& hopperObj) {
	const float connectRange = 2.5f;

	for (const SceneObject& other : scene->GetObjects()) {

		if (!HasTag(other, "Pipe")) {
			continue;
		}

		if (!IsConnectedSphere(hopperObj, other, connectRange)) {
			continue;
		}

		std::vector<const SceneObject*> visitedObjects;

		bool connected = IsPipeConnectedToExperienceMinerRecursive(scene, other, visitedObjects, connectRange);

		if (connected) {
			return true;
		}
	}

	return false;
}

void Game::ExperienceHopper::Start(SceneObject& obj, GameScene* scene) {
	(void)obj;
	(void)scene;
}

void Game::ExperienceHopper::Update(SceneObject& obj, GameScene* scene, float dt) {

	(void)obj;
	(void)scene;

	obj.rotate.y += 1.0f * dt;
	bool connected = IsExperienceHopperConnectedToExperienceMiner(scene, obj);

	if (!connected) {
		return;
	}

	if (connected) {
		obj.translate.y += 5.0f * dt; // 上昇速度を0.5ユニット/秒に設定
	}
}

void Game::ExperienceHopper::OnDestroy(SceneObject& obj, GameScene* scene) {
	(void)obj;
	(void)scene;
}

REGISTER_SCRIPT(ExperienceHopper);
} // namespace Game