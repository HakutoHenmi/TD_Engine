#include "PipeScript.h"
#include "ObjectTypes.h"
#include "Scenes/GameScene.h"
#include "ScriptEngine.h"
#include <cmath>
#include <vector>

namespace Game {

static bool HasTag(const SceneObject& obj, const char* tagName) {
	for (int i = 0; i < (int)obj.tags.size(); ++i) {
		if (obj.tags[i].tag == tagName) {
			return true;
		}
	}
	return false;
}

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

static bool IsAlreadyVisited(const std::vector<const SceneObject*>& visitedObjects, const SceneObject& obj) {
	for (int i = 0; i < (int)visitedObjects.size(); ++i) {
		if (visitedObjects[i] == &obj) {
			return true;
		}
	}
	return false;
}

static bool IsConnectedToBulletTankRecursive(GameScene* scene, const SceneObject& currentPipe, std::vector<const SceneObject*>& visitedObjects, float connectRange) {
	visitedObjects.push_back(&currentPipe);

	for (const SceneObject& other : scene->GetObjects()) {

		if (&other == &currentPipe) {
			continue;
		}

		if (!IsConnectedSphere(currentPipe, other, connectRange)) {
			continue;
		}

		// 隣に弾倉があれば到達成功
		if (HasTag(other, "BulletTank")) {
			return true;
		}

		// 隣がパイプならさらに先を調べる
		if (HasTag(other, "Pipe")) {

			if (IsAlreadyVisited(visitedObjects, other)) {
				continue;
			}

			bool connected = IsConnectedToBulletTankRecursive(scene, other, visitedObjects, connectRange);

			if (connected) {
				return true;
			}
		}
	}

	return false;
}

static bool IsConnectedToBulletTank(GameScene* scene, const SceneObject& selfObj) {
	const float connectRange = 2.5f;

	std::vector<const SceneObject*> visitedObjects;

	return IsConnectedToBulletTankRecursive(scene, selfObj, visitedObjects, connectRange);
}

void PipeScript::Start(SceneObject& obj, GameScene* scene) {
	(void)obj;
	(void)scene;
}

void PipeScript::Update(SceneObject& obj, GameScene* scene, float dt) {
	// bool connectedToTank = IsConnectedToBulletTank(scene, obj);
	(void)scene;
	float speed = rotationSpeed_;

	// if (connectedToTank) {
	// speed = rotationSpeed_ * 3.0f;
	//}

	obj.rotate.z += speed * dt;
}

void PipeScript::OnDestroy(SceneObject& obj, GameScene* scene) {
	(void)obj;
	(void)scene;
}

REGISTER_SCRIPT(PipeScript);

} // namespace Game