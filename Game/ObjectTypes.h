#pragma once
#include <DirectXMath.h>
#include <cstdint>
#include <vector>

namespace Game {

// ゲーム固有のオブジェクトタイプ定義
enum class ObjectType : uint32_t {
	Cube = 0,
	Slope = 1,
	Ball = 2,
	LongFloor = 3,
	Model = 999,
};

// ★追加: メッシュコライダー用データ
struct CollisionMeshData {
	std::vector<DirectX::XMVECTOR> vertices; // 頂点座標リスト
	std::vector<int> indices;                // 三角形インデックスリスト
};

} // namespace Game