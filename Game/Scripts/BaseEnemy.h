#pragma once
#include "IScript.h"
#include "Scenes/GameScene.h"
#include <list>
#include <string>
#include <vector>

enum MoveType {
	Walk,
	Fly
};

//敵の挙動になる基底クラス
namespace Game {

class BaseEnemy : public IScript {
public:
	// 初期化処理（シーン開始時に1回呼ばれる）
	void Start(entt::entity entity, GameScene* scene) override;

	// 毎フレーム処理
	void Update(entt::entity entity, GameScene* scene, float dt) override;

	// オブジェクト破棄時の処理
	void OnDestroy(entt::entity entity, GameScene* scene) override;

	// ImGuiでパラメータをいじる
	void OnEditorUI() override;

	// パラメーターの個別保存・読み込み用 (エディター用)
	std::string SerializeParameters() override;
	void DeserializeParameters(const std::string& data) override;

protected:
	// オブジェクトの移動処理
	void DefaultMove(entt::entity entity, GameScene* scene, float dt);

	//ターゲットを探す関数
	void SearchTarget(entt::entity entity, GameScene* scene);

	// デバッグ情報表示
	void Debug();

protected: // メンバ変数
	// 自身の情報を持たせておく
	uint32_t ownerId_ = 0; // 自分のオブジェクトのID
	GameScene* pCurrentScene_ = nullptr; // 現在のシーンへのポインタ

	// 動きのタイプ
	MoveType type_ = Walk;	// とりあえず初期値はWalk

	// 参照するObjectのポインタと位置　　
	// 初期値はPlayer
	std::string targetName_ = "Player";	// 一旦初期値をPlayerに
	uint32_t targetId_ = 0;
	DirectX::XMFLOAT3 myPos_ = {};
	float groundHeight_ = 0.0f;	// FlyTypeが地面の高さを取るため
	DirectX::XMFLOAT3 targetPos_ = {}; // 移動速度
	float totalTime_ = 0.0f;
	float speed_ = 2.0f;
	float separationRadius_ = 1.5f; // 近づきすぎないための半径
	float separationWeight_ = 2.0f; // 離れる力の強さ
	
	//敵の動きにの基底クラス部分に使う変数
	entt::entity currentTarget_ = entt::null;
	float searchRange_ = 15.0f;
	float loseTargetRange_ = 25.0f;
	float attackRange_ = 2.0f; // 攻撃を始める距離
	float scanTimer_ = 0.0f;
};

} // namespace Game
