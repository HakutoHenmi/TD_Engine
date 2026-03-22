#include "PreparationCamera.h"
#include "ObjectTypes.h"
#include "Scenes/GameScene.h"
#include "ScriptEngine.h"
#include <cmath>
#include <iostream>

#include "PhaseSystemScript.h"

namespace Game {

bool HasTag(const SceneObject& obj, const char* tagName) {
	for (int i = 0; i < (int)obj.tags.size(); ++i) { // タグ配列を最初から最後までループして、指定されたタグがあるか確認
		if (obj.tags[i].tag == tagName) {            // タグが見つかったらtrueを返す
			return true;
		}
	}
	return false; // タグが見つからなかったらfalseを返す
}

void PreparationCamera::Start(SceneObject& obj, GameScene* scene) {
	(obj);
	(scene);
}

void PreparationCamera::Update(SceneObject& obj, GameScene* scene, float dt) {
	if (PhaseSystemScript::IsPreparation()) {
		UpdateMovement(obj, scene, dt);
		obj.playerInputs[0].enabled = true;
		obj.cameraTargets[0].enabled = true;
	} else {
		obj.playerInputs[0].enabled = false;
		obj.cameraTargets[0].enabled = false;
	}
}

void PreparationCamera::UpdateMovement(SceneObject& obj, GameScene* /*scene*/, float dt) {
	bool keyW = (GetAsyncKeyState('W') & 0x8000) != 0;
	bool keyS = (GetAsyncKeyState('S') & 0x8000) != 0;
	bool keyA = (GetAsyncKeyState('A') & 0x8000) != 0;
	bool keyD = (GetAsyncKeyState('D') & 0x8000) != 0;
	bool keySpace = (GetAsyncKeyState(VK_SPACE) & 0x8000) != 0;

	float speedMul = 1.0f;

	DirectX::XMFLOAT3 move = {0, 0, 0};
	if (keyW)
		move.z += speedMul;
	if (keyS)
		move.z -= speedMul;
	if (keyA)
		move.x -= speedMul;
	if (keyD)
		move.x += speedMul;

	if (!obj.playerInputs.empty()) {
		auto& input = obj.playerInputs[0];
		input.moveDir = {move.x, move.z};
		input.jumpRequested = keySpace;
	}

	if (std::abs(move.x) > 0.1f || std::abs(move.z) > 0.1f) {
		float targetAngle = std::atan2(move.x, move.z);
		float angleDiff = targetAngle - obj.rotate.y;
		while (angleDiff > DirectX::XM_PI)
			angleDiff -= DirectX::XM_2PI;
		while (angleDiff < -DirectX::XM_PI)
			angleDiff += DirectX::XM_2PI;
		obj.rotate.y += angleDiff * 10.0f * dt;
	}
}

void PreparationCamera::OnDestroy(SceneObject& /*obj*/, GameScene* /*scene*/) {}

REGISTER_SCRIPT(PreparationCamera);

} // namespace Game