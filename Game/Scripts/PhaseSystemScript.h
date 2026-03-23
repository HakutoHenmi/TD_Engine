#pragma once
#include "IScript.h"
#include "../../externals/entt/entt.hpp"

struct ImVec2; // 前方宣言

namespace Game {

class PhaseSystemScript : public IScript {
public:
	void Start(entt::entity entity, GameScene* scene) override;
	void Update(entt::entity entity, GameScene* scene, float dt) override;
	void OnDestroy(entt::entity entity, GameScene* scene) override;

	void Installation(GameScene* scene, const ImVec2& gameImageMin, float tW, float tH);

	static bool IsPreparation() { return isPreparation_; };

private:
	static bool isPreparation_;

	bool preKeyP_ = false; // 初期化しておく
};

} // namespace Game