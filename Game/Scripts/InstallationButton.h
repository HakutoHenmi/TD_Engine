#pragma once
#include "IScript.h"

namespace Game {

class InstallationButton : public IScript {
public:
	InstallationButton();

	enum FacilityTypes {
		Cannon = 0,
		Pipe,
		Tank, 
		FacilityTypesNum,
	};

	void Start(entt::entity entity, GameScene* scene) override;
	void Update(entt::entity entity, GameScene* scene, float dt) override;
	void OnDestroy(entt::entity entity, GameScene* scene) override;
	void OnEditorUI() override;
	
	std::string SerializeParameters() override;
	void DeserializeParameters(const std::string& data) override;

	static bool IsButtonPressed(FacilityTypes type);


	

private:

	FacilityTypes FacilityTypes_ = Cannon;
	
	static bool isButtonPressed_[FacilityTypesNum];

};

} // namespace Game