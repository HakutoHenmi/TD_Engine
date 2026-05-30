#include "ClickSound.h"
#include "../../Engine/Audio.h"
#include "../../Engine/Input.h"
#include "ObjectTypes.h"
#include "Scenes/GameScene.h"
#include "ScriptEngine.h"
#include <cmath>
#include <iostream>

namespace Game {
void ClickSound::Start(entt::entity /*entity*/, GameScene* /*scene*/) {
	// クリックSEをロード
	if (auto* audio = Engine::Audio::GetInstance()) {
		clickSeHandle_ = audio->Load("Resources/Audio/SE/Click.mp3");
	}
}

void ClickSound::Update(entt::entity entity, GameScene* scene, float /*dt*/) {

	// ★追加: マスターボリュームの反映（PhaseSystemが直接再生するBGM用）
	if (scene->GetRegistry().all_of<UIButtonComponent>(entity)) {
		auto& btn = scene->GetRegistry().get<UIButtonComponent>(entity);
		if (btn.isPressed) {
			if (auto input = Engine::Input::GetInstance(); input && input->IsMouseTrigger(0)) {
				if (auto* audio = Engine::Audio::GetInstance()) {
					audio->Play(clickSeHandle_, false, 0.6f * audio->GetMasterSEVolume());
				}
			}
		}
	}
}

void ClickSound::OnClick(entt::entity /*entity*/, GameScene* /*scene*/, const std::string& /*callbackName*/) {
	if (auto* audio = Engine::Audio::GetInstance()) {
		audio->Play(clickSeHandle_, false, 0.6f * audio->GetMasterSEVolume());
	}
}

void ClickSound::OnDestroy(entt::entity /*entity*/, GameScene* /*scene*/) {}

REGISTER_SCRIPT(ClickSound);

} // namespace Game