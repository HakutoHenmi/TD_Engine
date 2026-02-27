#pragma once
#include "ISystem.h"
#include <cmath>

namespace Game {

class CameraFollowSystem : public ISystem {
public:
	void Update(std::vector<SceneObject>& objects, GameContext& ctx) override {
		if (!ctx.isPlaying || !ctx.camera) return;

		for (const auto& obj : objects) {
			for (const auto& ct : obj.cameraTargets) {
				if (!ct.enabled) continue;

				DirectX::XMFLOAT3 targetPos = obj.translate;

				if (!obj.playerInputs.empty() && obj.playerInputs[0].enabled) {
					auto rot = ctx.camera->Rotation();
					rot.y += obj.playerInputs[0].cameraYaw;
					rot.x += obj.playerInputs[0].cameraPitch;

					const float PITCH_LIMIT = 1.5f;
					if (rot.x > PITCH_LIMIT) rot.x = PITCH_LIMIT;
					if (rot.x < -PITCH_LIMIT) rot.x = -PITCH_LIMIT;

					ctx.camera->SetRotation(rot);
				}

				auto curRot = ctx.camera->Rotation();
				float camSy = std::sin(curRot.y);
				float camCy = std::cos(curRot.y);
				float camSx = std::sin(curRot.x);
				float camCx = std::cos(curRot.x);

				DirectX::XMFLOAT3 offset = {
					-camSy * camCx * ct.distance,
					ct.height + camSx * ct.distance,
					-camCy * camCx * ct.distance
				};

				DirectX::XMFLOAT3 desiredPos = {
					targetPos.x + offset.x,
					targetPos.y + offset.y,
					targetPos.z + offset.z
				};

				DirectX::XMFLOAT3 currentPos = ctx.camera->Position();
				float t = ct.smoothSpeed * ctx.dt;
				if (t > 1.0f) t = 1.0f;

				DirectX::XMFLOAT3 newPos = {
					currentPos.x + (desiredPos.x - currentPos.x) * t,
					currentPos.y + (desiredPos.y - currentPos.y) * t,
					currentPos.z + (desiredPos.z - currentPos.z) * t
				};

				ctx.camera->SetPosition(newPos);
				break;
			}
		}
	}
};

} // namespace Game
