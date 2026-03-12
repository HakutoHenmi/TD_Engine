#pragma once
#include "IScript.h"
#include "Scenes/GameScene.h"
#include <list>
#include <string>
#include <vector>

//*** 二次元マップを探索して最短ルートを進むアルゴリズムにする ***//

struct Node {
	bool isWall = false;    // Wallタグを持ったオブジェクトがあるか
	int gridX, gridZ;       // グリッド上の座標
	float gCost;            // スタートからの距離
	float hCost;            // ゴールまでの推定距離
	Node* parent = nullptr; // どのマスからきたか(ルートを逆算する)

	// 最短経路のコストを計算するメソッド
	float fCost() { return gCost + hCost; }
};

// 移動のタイプ
enum MoveType {
	Walk,
	Fly,
};

// ターゲットにするオブジェクトのタイプ
enum TargetType {
	// 単体オブジェクト
	Player,
	Core,

	// 複数存在し得るオブジェクト
	Defender,
};

// 複数ターゲットを選ぶ基準
enum TargetPriority {
	Near,
	Far,
};

namespace Game {

class EnemyBehavior : public IScript {
public:
	// 初期化処理（シーン開始時に1回呼ばれる）
	void Start(SceneObject& obj, GameScene* scene) override;

	// 毎フレーム処理
	void Update(SceneObject& obj, GameScene* scene, float dt) override;

	// オブジェクト破棄時の処理
	void OnDestroy(SceneObject& obj, GameScene* scene) override;

	// ImGuiでパラメータをいじる
	void OnEditorUI() override;

private:
	// ターゲットを検索して更新する関数
	void SearchTarget(SceneObject& obj, GameScene* scene);

	// オブジェクトの移動処理
	void Move(SceneObject& obj, float dt);

	// オブジェクトの周囲をチェックする関数
	void ScanSurround(SceneObject& obj, GameScene* scene);

	// targetの方向をグリッドに直してA*アルゴリズムを実行する
	void AStar();

	// A*アルゴリズムでルートを計算する関数
	void CalculatePath(int startX, int startZ, int targetX, int targetZ);

	// デバッグ情報表示
	void Debug();

private: // メンバ変数
	// 自身の情報を持たせておく
	SceneObject* pOwner_ = nullptr; // 自分のオブジェクトへのポインタ
	GameScene* pCurrentScene_ = nullptr; // 現在のシーンへのポインタ

	// 動きのタイプ
	MoveType type_ = Walk;	// とりあえず初期値はWalk

	// 追尾するオブジェクトのタイプ
	TargetType targetType_ = Player;	// 初期値はPlayer

	// 複数ターゲットから選ぶ
	TargetPriority priority_ = Near;	// 初期値はNear

	// 参照するObjectのポインタと位置
	// 初期値はPlayer
	std::string targetName_ = "Player";	// 一旦初期値をPlayerに
	const SceneObject* target_ = nullptr;
	DirectX::XMFLOAT3 myPos_ = {};
	DirectX::XMFLOAT3 targetPos_ = {}; // 移動速度
	float speed_ = 5.0f;

	// グリッド関連
	static const int GRID_SIZE = 21; // 21*21にして自分を真ん中に置いた20メートル四方のグリッドに
	float cellLength_ = 2.0f;        // グリッドのセル一つの大きさ

	float KChinkoRadius_ = 5.0f;

	// マップ探索用
	Node localGrid_[GRID_SIZE][GRID_SIZE];

	// 実際のルート探索に必要な変数
	std::vector<Node*> openList_;
	std::vector<Node*> closedList_;
	std::vector<DirectX::XMFLOAT3> path_; // 最終的な移動ルート
};

} // namespace Game
