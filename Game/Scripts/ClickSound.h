#pragma once
#include "IScript.h"

namespace Game {

class ClickSound : public IScript {
public:
	void Start(entt::entity entity, GameScene* scene) override;
	void Update(entt::entity entity, GameScene* scene, float dt) override;
	void OnClick(entt::entity entity, GameScene* scene, const std::string& callbackName) override;
	void OnDestroy(entt::entity entity, GameScene* scene) override;

private:
	uint32_t clickSeHandle_ = 0;


	inline static size_t currentSEVoiceHandle_ = 0;
};

} // namespace Game