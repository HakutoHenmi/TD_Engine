#include "ExperienceOrbScript.h"
#include "ObjectTypes.h"
#include "Scenes/GameScene.h"
#include "ScriptEngine.h"
#include <cmath>
#include <cstdlib>

namespace Game {

static bool HasTag(const SceneObject& obj, const char* tagName) {
	for (int i = 0; i < (int)obj.tags.size(); ++i) {
		if (obj.tags[i].tag == tagName) {
			return true;
		}
	}
	return false;
}

void ExperienceOrbScript::Start(SceneObject& obj, GameScene* /*scene*/) {

	float randomX = (float(rand()) / float(RAND_MAX)) - 0.5f;
	float randomZ = (float(rand()) / float(RAND_MAX)) - 0.5f;

	velocityX_ = randomX * 2.0f;
	velocityZ_ = randomZ * 2.0f;
	velocityY_ = 2.5f;

	startY_ = obj.translate.y;

	floatTimer_ = 0.0f;
	isFloating_ = false;
}

void ExperienceOrbScript::Update(SceneObject& obj, GameScene* scene, float dt) {

	obj.rotate.y += 1.5f * dt;

	SceneObject* playerObject = nullptr;
	float nearestDistance = suctionRange_;

	for (int i = 0; i < (int)scene->GetObjects().size(); ++i) {
		SceneObject& other = const_cast<SceneObject&>(scene->GetObjects()[i]);

		if (!HasTag(other, "Player")) {
			continue;
		}

		float dx = other.translate.x - obj.translate.x;
		float dy = other.translate.y - obj.translate.y;
		float dz = other.translate.z - obj.translate.z;

		float distance = std::sqrt(dx * dx + dy * dy + dz * dz);

		if (distance < nearestDistance) {
			nearestDistance = distance;
			playerObject = &other;
		}
	}

	if (playerObject != nullptr) {

		float playerCenterY = playerObject->translate.y + 1.0f;

		float dx = playerObject->translate.x - obj.translate.x;
		float dy = playerCenterY - obj.translate.y;
		float dz = playerObject->translate.z - obj.translate.z;

		float distance = std::sqrt(dx * dx + dy * dy + dz * dz);

		if (distance < hitRange_) {
			if ((int)obj.healths.size() > 0) {
				obj.healths[0].hp = 0.0f;
			}
			return;
		}

		if (distance > 0.0001f) {
			float directionX = dx / distance;
			float directionY = dy / distance;
			float directionZ = dz / distance;

			float moveDistance = suctionSpeed_ * dt;

			if (moveDistance > distance) {
				moveDistance = distance;
			}

			obj.translate.x += directionX * moveDistance;
			obj.translate.y += directionY * moveDistance;
			obj.translate.z += directionZ * moveDistance;
		}

		return;
	}

	if (isFloating_ == false) {

		float gravity = -4.0f;
		velocityY_ += gravity * dt;

		obj.translate.x += velocityX_ * dt;
		obj.translate.y += velocityY_ * dt;
		obj.translate.z += velocityZ_ * dt;

		if (obj.translate.y < startY_ - 1.0f) {
			isFloating_ = true;
		}

	} else {

		floatTimer_ += dt;

		float floatHeight = std::sin(floatTimer_ * 3.0f) * 0.2f;

		obj.translate.y = (startY_ - 1.0f) + floatHeight;
	}


	
}

void ExperienceOrbScript::OnDestroy(SceneObject& /*obj*/, GameScene* /*scene*/) {}

REGISTER_SCRIPT(ExperienceOrbScript);

} // namespace Game