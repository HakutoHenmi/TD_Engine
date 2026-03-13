#pragma once
#include "IScript.h"

struct ImVec2; // 前方宣言

namespace Game {

class PahaseSystemScript : public IScript {
public:
	void Start(SceneObject& obj, GameScene* scene) override;
	void Update(SceneObject& obj, GameScene* scene, float dt) override;
	void OnDestroy(SceneObject& obj, GameScene* scene) override;

	void Installation(GameScene* scene, const ImVec2& gameImageMin, float tW, float tH);

	static bool IsPreparation() { return isPreparation_; };

private:
	static bool isPreparation_;

	bool preKeyP_ = false; // 初期化しておく
};

} // namespace Game