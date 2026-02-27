#pragma once
#include "ISystem.h"
#include "../../Engine/Audio.h"
#include <cmath>

namespace Game {

class AudioSystem : public ISystem {
public:
	void Update(std::vector<SceneObject>& objects, GameContext& ctx) override {
		if (!ctx.isPlaying) return;

		// リスナー位置の検索
		DirectX::XMFLOAT3 listenerPos = ctx.camera ? ctx.camera->Position() : DirectX::XMFLOAT3{0, 0, 0};
		for (const auto& obj : objects) {
			for (const auto& al : obj.audioListeners) {
				if (al.enabled) {
					listenerPos = obj.translate;
					goto found_listener;
				}
			}
		}
	found_listener:

		auto* audio = Engine::Audio::GetInstance();
		if (!audio) return;

		for (auto& obj : objects) {
			for (auto& as : obj.audioSources) {
				if (!as.enabled) continue;

				if (as.playOnStart && !as.isPlaying && as.soundHandle != 0xFFFFFFFF) {
					as.voiceHandle = audio->Play(as.soundHandle, as.loop, as.volume);
					as.isPlaying = true;
				}

				if (as.isPlaying && as.voiceHandle != 0) {
					float finalVol = as.volume;
					if (as.is3D) {
						float dx = obj.translate.x - listenerPos.x;
						float dy = obj.translate.y - listenerPos.y;
						float dz = obj.translate.z - listenerPos.z;
						float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
						if (as.maxDistance > 0.001f) {
							float atten = 1.0f - (dist / as.maxDistance);
							if (atten < 0.0f) atten = 0.0f;
							atten = atten * atten;
							finalVol *= atten;
						}
					}
					audio->SetVolume(as.voiceHandle, finalVol);
				}
			}
		}
	}
};

} // namespace Game
