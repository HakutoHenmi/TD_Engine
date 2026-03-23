#pragma once
#include "IScript.h"
#include "../../externals/entt/entt.hpp"

struct ImVec2; // 前方宣言
namespace Engine {
struct Vector3;
}

namespace Game {

class PhaseSystemScript : public IScript {
public:
	void Start(entt::entity entity, GameScene* scene) override;
	void Update(entt::entity entity, GameScene* scene, float dt) override;
	void OnDestroy(entt::entity entity, GameScene* scene) override;

	void Installation(GameScene* scene, const std::string& objPath);
	bool TryGetTerrainHitPoint(GameScene* scene, Engine::Vector3& outHitPoint) const;
	void DrawPlacementPreview(GameScene* scene, const Engine::Vector3& hitPoint, const std::string& objPath, bool canPlace);
	void SpawnPlacedObject(GameScene* scene, const Engine::Vector3& hitPoint, const std::string& objPath);
	bool IsPlacementBlocked(GameScene* scene, const Engine::Vector3& hitPoint) const;
	bool IsPrefabPath(const std::string& path) const;
	bool ExtractPrefabRenderPaths(const std::string& prefabPath, std::string& outModelPath, std::string& outTexturePath) const;

	static bool IsPreparation() { return isPreparation_; };
	static void SetPreparation(bool prep) { isPreparation_ = prep; }

private:
	inline static bool isPreparation_ = true;
	int currentPhase_ = 0;

	bool preKeyP_ = false; // 初期化しておく
	bool prekeySpace_ = false;
	bool preKey1_ = false;
	bool preKey2_ = false;
	bool preKey3_ = false;
	bool isPlacementMode_ = false;
	std::string selectedObjPath_ = "Resources/cube/cube.obj";
	std::string previewObjPath_;
	uint32_t previewModelHandle_ = 0;
	uint32_t previewTextureHandle_ = 0;
};

} // namespace Game