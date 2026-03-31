#pragma once
#include "IScript.h"

namespace Game {

class CannonButton : public IScript {
public:
	void Start(entt::entity entity, GameScene* scene) override;
	void Update(entt::entity entity, GameScene* scene, float dt) override;
	void OnDestroy(entt::entity entity, GameScene* scene) override;
	void OnEditorUI() override;

	static bool IsButtonPressed() { return isButtonPressed_; }

private:
	
	static bool isButtonPressed_;

};

} // namespace Game