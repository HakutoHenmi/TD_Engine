#pragma once
#include "IScript.h"
#include <cstdint>
#include <string>

struct ImVec2; // 前方宣言
namespace Engine {
struct Vector3;
}

namespace Game {

class PahaseSystemScript : public IScript {
public:
	void Start(SceneObject& obj, GameScene* scene) override;
	void Update(SceneObject& obj, GameScene* scene, float dt) override;
	void OnDestroy(SceneObject& obj, GameScene* scene) override;

    void Installation(GameScene* scene, const std::string& objPath);
	bool TryGetTerrainHitPoint(GameScene* scene, Engine::Vector3& outHitPoint) const;
   void DrawPlacementPreview(GameScene* scene, const Engine::Vector3& hitPoint, const std::string& objPath);
	void SpawnPlacedObject(GameScene* scene, const Engine::Vector3& hitPoint, const std::string& objPath);

	static bool IsPreparation() { return isPreparation_; };

private:
	static bool isPreparation_;

	bool preKeyP_ = false; // 初期化しておく
    bool preKey1_ = false;
	bool preKey2_ = false;
	bool isPlacementMode_ = false;
   std::string selectedObjPath_ = "Resources/cube/cube.obj";
	std::string previewObjPath_;
	uint32_t previewModelHandle_ = 0;
	uint32_t previewTextureHandle_ = 0;
};

} // namespace Game