#pragma once
#include "IScript.h"
#include "Scenes/GameScene.h"
#include <string>
#include <list>
#include <vector>

//*** 二次元マップを探索して最短ルートを進むアルゴリズムにする ***//

struct Node {
	bool isWall = false;		// Wallタグを持ったオブジェクトがあるか
	int gridX, gridZ;			// グリッド上の座標
	float gCost;				// スタートからの距離
	float hCost;				// ゴールまでの推定距離
	Node* parent = nullptr;		// どのマスからきたか(ルートを逆算する)

	// 最短経路のコストを計算するメソッド
	float fCost() { return gCost + hCost; }
};

namespace Game {

class ToObjectMove : public IScript {
public:
	// 初期化処理（シーン開始時に1回呼ばれる）
	void Start(SceneObject& obj, GameScene* scene) override;

	// 毎フレーム処理
	void Update(SceneObject& obj, GameScene* scene, float dt) override;

	// オブジェクト破棄時の処理
	void OnDestroy(SceneObject& obj, GameScene* scene) override;

private:
	// ImGuiで追尾するタグをいじれるように
	void ChangeTargetTag(SceneObject& obj, GameScene* scene, float dt);

	// オブジェクトの移動処理
	void Move(SceneObject& obj, float dt);

	// オブジェクトの周囲をチェックする関数
	void ScanSurround(SceneObject& obj, GameScene* scene);

	// targetの方向をグリッドに直す
	void AStar();

	// A*アルゴリズムでルートを計算する関数
	void CalculatePath(int startX, int startZ, int targetX, int targetZ);

	// オブジェクト周囲のグリッド描画
	void DrawGrid();

private: // メンバ変数
	// 参照するObjectのポインタと位置
	// 初期値はPlayer
	std::string targetName_ = {};
	// ImGui編集用のデータ
	char tagBuffer_[64] = {};
	const SceneObject* target_ = nullptr;
	DirectX::XMFLOAT3 myPos_ = {};
	DirectX::XMFLOAT3 targetPos_ = {}; // 移動速度
	float speed_ = 5.0f;

	// グリッド関連
	static const int GRID_SIZE = 21;	// 21*21にして自分を真ん中に置いた20メートル四方のグリッドに
	float cellLength_ = 2.0f;			// グリッドのセル一つの大きさ

	// マップ探索用
	Node localGrid_[GRID_SIZE][GRID_SIZE];

	// 実際のルート探索に必要な変数
	std::vector<Node*> openList_;
	std::vector<Node*> closedList_;
	std::vector<DirectX::XMFLOAT3> path_;	// 最終的な移動ルート
};

} // namespace Game
