// Game/World.h
#pragma once
#include "../../Engine/GameObject.h"
#include "../../Engine/Renderer.h"
#include "../ObjectTypes.h"
#include <map>
#include <string>
#include <vector>

namespace Game {

class World {
public:
	void Initialize(Engine::Renderer* renderer);
	void Draw(Engine::Renderer* renderer);

	void Save(const std::string& filename);
	void Load(const std::string& filename);

	void CreateObject(Game::ObjectType type, const Engine::Vector3& pos);
	void CreateObjectFromFile(const std::string& objFileName, const Engine::Vector3& pos);

	void DeleteObject(Engine::GameObject* ptr);
	Engine::GameObject* DuplicateObject(Engine::GameObject* src);

	// ★変更: ignoreLocked 引数を追加 (デフォルト false)
	Engine::GameObject* CastRay(const Engine::Vector3& origin, const Engine::Vector3& dir, float& hitDist, bool ignoreLocked = false);

	std::vector<Engine::GameObject>& GetObjects() { return objects_; }

	uint32_t GetCubeMesh() const { return cubeMesh_; }
	uint32_t GetSlopeMesh() const { return ballMesh_; }
	uint32_t GetBallMesh() const { return ballMesh_; }
	uint32_t GetLongFloorMesh() const { return longFloorMesh_; }

	uint32_t GetMeshHandleFromFile(const std::string& filename);

private:
	// ★追加: OBJファイルからサイズ(AABB)と詳細メッシュを計算してキャッシュする関数
	void CalculateAndCacheBounds(const std::string& filename, Engine::Vector3& outMin, Engine::Vector3& outMax);

private:
	Engine::Renderer* renderer_ = nullptr;
	std::vector<Engine::GameObject> objects_;

	uint32_t cubeMesh_ = 0;
	uint32_t ballMesh_ = 0;
	uint32_t longFloorMesh_ = 0;
	uint32_t checkTex_ = 0;

	std::map<std::string, uint32_t> meshCache_;

	// ★追加: 衝突判定用のメッシュデータキャッシュ
	std::map<std::string, CollisionMeshData> collisionCache_;
};

} // namespace Game