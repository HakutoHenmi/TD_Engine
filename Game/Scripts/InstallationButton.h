#pragma once
#include "IScript.h"

namespace Game {

class InstallationButton : public IScript {
public:

	enum ButtonTypes {
		Cannon = 0,
		Pipe,
		Tank, 
		ButtonTypesNum,
	};

	void Start(entt::entity entity, GameScene* scene) override;
	void Update(entt::entity entity, GameScene* scene, float dt) override;
	void OnDestroy(entt::entity entity, GameScene* scene) override;
	void OnEditorUI() override;

	static bool IsButtonPressed();


	

private:

	ButtonTypes buttonTypes_ = Cannon;
	
	static bool isButtonPressed_[ButtonTypesNum];

};

} // namespace Game