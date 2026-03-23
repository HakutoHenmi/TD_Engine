#include "EnemyBehavior.h"
#include "../../Engine/ThirdParty/nlohmann/json.hpp"
#include "../../externals/imgui/imgui.h"
#include "ObjectTypes.h"
#include "Scenes/GameScene.h"
#include "ScriptEngine.h"

namespace Game {

static bool HasTag(const SceneObject& obj, const char* tagName) {
	for (int i = 0; i < (int)obj.tags.size(); ++i) { // タグ配列を最初から最後までループして、指定されたタグがあるか確認
		if (obj.tags[i].tag == tagName) {            // タグが見つかったらtrueを返す
			return true;
		}
	}
	return false; // タグが見つからなかったらfalseを返す
}

void EnemyBehavior::Start(SceneObject& obj, GameScene* scene) {
	pOwner_ = &obj;
	pCurrentScene_ = scene;

	// 出現時に一度ターゲットを検索
	SearchTarget(obj, scene);

	// 出現時に地面の高さを即座に計算（初動の埋まり防止）
	groundHeight_ = scene->GetHeightAt(obj.translate.x, obj.translate.z, obj.id);

	// Flyタイプの場合は重力を無効化し、初期高度を設定
	if (type_ == Fly) {
		for (auto& rb : obj.rigidbodies) {
			rb.useGravity = false;
			rb.velocity = {0, 0, 0};
		}

		// スポナーの高さではなく、即座に正しい浮遊高度へ移動
		float baseHeight = 9.0f;
		obj.translate.y = groundHeight_ + baseHeight;
	}
}

void EnemyBehavior::Update(SceneObject& obj, GameScene* scene, float dt) {
	// ここに毎フレームの挙動を記述

	// 自身の情報を常に最新に
	pOwner_ = &obj;
	pCurrentScene_ = scene;

	// 既存のターゲットを失ったときなどに改めて検索
	if (target_ == nullptr) {
		SearchTarget(obj, scene);
	}

	// 周囲情報のスキャンとA*（負荷が高いため頻度を下げる）
	scanTimer_ += dt;
	if (scanTimer_ > 0.1f) { // 0.2秒ごとに実施
		scanTimer_ = 0.0f;

		// 周囲の情報をスキャン
		ScanSurround(obj, scene);

		// A*(エースターアルゴリズム)でオブジェクトまでの最短ルートを探す
		AStar();
	}

	// 移動
	Move(obj, scene, dt);

	// デバッグ描画
	Debug();
}

void EnemyBehavior::OnDestroy(SceneObject& /*obj*/, GameScene* /*scene*/) {
	// 終了時のクリーンアップなどを記述
}

void EnemyBehavior::OnEditorUI() {
	// 敵の移動タイプ(地面や空中)
	// ライトの種類を選べるようにする
	int typeNum = static_cast<int>(type_);
	const char* types[] = {"Walk", "Fly"};
	if (ImGui::Combo("Enemy Type", &typeNum, types, IM_ARRAYSIZE(types))) {
		// 選ばれた番号をそのまま enum にキャストして戻せばOK！
		type_ = static_cast<MoveType>(typeNum);
	}

	// 追うオブジェクトのタグを設定
	// ライトの種類を選べるようにする
	int targetNum = static_cast<int>(targetType_);
	const char* targetTypes[] = {"Player", "Core", "Defender"};
	if (ImGui::Combo("Target", &targetNum, targetTypes, IM_ARRAYSIZE(targetTypes))) {
		// 選ばれた番号をそのまま enum にキャストして戻せばOK！
		targetType_ = static_cast<TargetType>(targetNum);

		// タグ名を更新(シーンを動かしたときにすぐに検索できるように)
		if (targetType_ == Player) {
			targetName_ = "Player";
		} else if (targetType_ == Core) {
			targetName_ = "Core";
		} else if (targetType_ == Defender) {
			targetName_ = "Defender";
		}

		// 追尾するタグが変わったら新たに検索
		if (pOwner_ && pCurrentScene_) {
			SearchTarget(*pOwner_, pCurrentScene_);
		}
	}

	// 複数存在し得るオブジェクトに対して優先順位を選べるように
	if (targetType_ >= Defender) {
		int priorityNum = static_cast<int>(priority_);
		const char* priorities[] = {"Near", "Far"};
		if (ImGui::Combo("Priority", &priorityNum, priorities, IM_ARRAYSIZE(priorities))) {
			priority_ = static_cast<TargetPriority>(priorityNum);
		}

		ImGui::Checkbox("Show Debug Grid", &showDebugGrid_);
	}
}

void EnemyBehavior::SearchTarget(SceneObject& obj, GameScene* scene) {
	if (scene == nullptr) {
		return;
	}

	auto& objects = scene->GetObjects();
	SceneObject* bestTarget = nullptr;
	float bestDistance = (priority_ == Near) ? FLT_MAX : -1.0f;

	for (size_t i = 0; i < objects.size(); ++i) {
		if (HasTag(objects[i], targetName_.c_str())) {
			// 距離を計算
			float dx = objects[i].GetTransform().translate.x - obj.GetTransform().translate.x;
			float dz = objects[i].GetTransform().translate.z - obj.GetTransform().translate.z;
			float distSq = dx * dx + dz * dz; // 軽量化のために平方根は取らない

			// priorityごとの対応
			if (priority_ == Near) { // Near
				if (distSq < bestDistance) {
					bestDistance = distSq;
					bestTarget = const_cast<SceneObject*>(&objects[i]);
				}
			} else { // Far
				if (distSq > bestDistance) {
					bestDistance = distSq;
					bestTarget = const_cast<SceneObject*>(&objects[i]);
				}
			}
		}
	}
	target_ = bestTarget;
}

void EnemyBehavior::Move(SceneObject& obj, GameScene* scene, float dt) {
	// ターゲットが存在しない、またはtargetまでのPath(ルート)がなければ止める
	if (target_ == nullptr || path_.empty()) {
		return;
	}

	// path_の0番目は自分自身の場所に近いので、1番目(次のマス)を目指すのがスムーズ
	// もしpath_が1つしかないなら0番目を目指す
	int index = (path_.size() > 1) ? 1 : 0;
	DirectX::XMFLOAT3 nextPos = path_[index];

	// 自分の位置を取得
	myPos_.x = obj.GetTransform().translate.x;
	myPos_.y = obj.GetTransform().translate.y;
	myPos_.z = obj.GetTransform().translate.z;

	// 次のポイントへの方向
	float diffX = nextPos.x - myPos_.x;
	float diffZ = nextPos.z - myPos_.z;
	float distance = std::sqrt(diffX * diffX + diffZ * diffZ);

	float dirX = 0.0f;
	float dirZ = 0.0f;
	if (distance > 0.1f) {
		dirX = diffX / distance;
		dirZ = diffZ / distance;

		obj.translate.x += dirX * speed_ * dt;
		obj.translate.z += dirZ * speed_ * dt;
	} else if (path_.size() > 1) {
		// 目的地にほぼ着いたら、座標をマスの中心に強制セットしてズレをリセット
		obj.translate.x = nextPos.x;
		obj.translate.z = nextPos.z;
	}

	// 他の敵と重ならないための処理
	float separationX = 0.0f;
	float separationZ = 0.0f;
	auto& allObjects = scene->GetObjects();

	for (size_t i = 0; i < allObjects.size(); ++i) {
		// 自分自身、または「Enemy」タグを持っていないものは無視 
		if (&allObjects[i].id == &obj.id || HasTag(allObjects[i], "Enemy")) {
			continue;
		}

		// 他の敵との距離を計算
		float vx = obj.translate.x - allObjects[i].translate.x;
		float vz = obj.translate.z - allObjects[i].translate.z;
		float distSq = vx * vx + vz * vz;

		// 一定範囲内にいたら、離れる方向の力を蓄積
		if (distSq < separationRadius_ * separationRadius_ && distSq > 0.0001f) {
			float d = std::sqrt(distSq);
			separationX += (vx / d) * (separationRadius_ - d); // 近いほど強く押し戻す
			separationZ += (vz / d) * (separationRadius_ - d);
		}
	}

	// 最終的な移動方向に合成
	float finalDirX = dirX + separationX * separationWeight_;
	float finalDirZ = dirZ + separationZ * separationWeight_;

	// 正規化(斜め移動が速くならないように)
	float finalLength = std::sqrt(finalDirX * finalDirX + finalDirZ * finalDirZ);
	if (finalLength > 0.01f) {
		obj.translate.x += (finalDirX / finalLength) * speed_ * dt;
		obj.translate.z += (finalDirZ / finalLength) * speed_ * dt;
	}

	// 現在のXZ座標から地面の高さを取得 (負荷が高いため頻度を下げる)
	// scanTimer_ が 0.0f にリセットされた直後のフレーム（0.2秒に1回）のみ実行
	if (scanTimer_ <= dt) {
		float newHeight = scene->GetHeightAt(obj.translate.x, obj.translate.z, obj.id);
		// 0.0は「ヒットせず」の可能性があるため、前回の値を保持するガードを入れる
		if (newHeight != 0.0f || groundHeight_ == 0.0f) {
			groundHeight_ = newHeight;
		}
	}

	// type別Y座標の対応
	if (type_ == Fly) {
		float baseHeight = 9.0f;                    // 基準とする高さ
		float targetY = groundHeight_ + baseHeight; // 本来あるべき高さの目標値

		// 補間用の値
		float interpolationSpeed = 5.0f;
		obj.translate.y += (targetY - obj.translate.y) * interpolationSpeed * dt;

		// sin波を使ってふわふわさせる
		totalTime_ += dt;

		float hoverRange = 0.5f; // 揺れ幅
		float hoverSpeed = 2.0f; // 揺れのスピード

		// 基準の高さに揺れの高さを足す
		obj.translate.y += (std::sin(totalTime_ * hoverSpeed) * hoverRange) * dt * 10.0f;

		// Rigidbodyがある場合は、座標の強制同期を行う（物理挙動との競合防止）
		for (auto& rb : obj.rigidbodies) {
			rb.velocity.y = 0.0f;
		}
	}

	// 歩行タイプは地面にいるので特別な処理はなし
}

void EnemyBehavior::ScanSurround(SceneObject& obj, GameScene* scene) {
	myPos_ = obj.translate;

	// 自分の位置をスナップ（基準座標）
	float snappedX = std::floor(myPos_.x / cellLength_) * cellLength_;
	float snappedZ = std::floor(myPos_.z / cellLength_) * cellLength_;

	// 自分が立っている場所の地面の高さを基準にする
	float currentGroundHeight = scene->GetHeightAt(myPos_.x, myPos_.z, obj.id);

	// グリッドの初期化
	for (int z = 0; z < GRID_SIZE; ++z) {
		for (int x = 0; x < GRID_SIZE; ++x) {
			localGrid_[z][x].isWall = false;
			localGrid_[z][x].gridX = x;
			localGrid_[z][x].gridZ = z;

			// 急な坂を壁として判定する処理
			if(type_ == Walk) {
				// 各セルの中心座標を計算
				float cellWorldX = snappedX + (x - GRID_SIZE / 2) * cellLength_;
				float cellWorldZ = snappedZ + (z - GRID_SIZE / 2) * cellLength_;

				// その場所の地面の高さを取得
				float cellHeight = scene->GetHeightAt(cellWorldX, cellWorldZ, obj.id);

				// 自分の位置からそのマスまでの距離を計算する
				float dx = (float)(x - GRID_SIZE / 2);
				float dz = (float)(z - GRID_SIZE / 2);
				float dist = std::sqrt(dx * dx + dz * dz);

				// 坂の許容範囲
				float maxSlope = 1.0f; // 45度の坂を許容する（調整可能）
				float heightDiff = std::abs(cellHeight - currentGroundHeight);

				if (heightDiff > (dist * cellLength_ * maxSlope)) {
					localGrid_[z][x].isWall = true;
				}

				// あまりにも高い段差（垂直な壁）は距離に関わらずブロック
				if (heightDiff > 3.0f) {
					localGrid_[z][x].isWall = true;
				}
			}
		}
	}

	float enemyRadius = 1.0f;
	auto& objects = scene->GetObjects();

	// 壁オブジェクトを事前に抽出（計算量を減らすため、二重ループを避ける）
	struct WallInfo {
		float minX, maxX, minZ, maxZ;
	};
	std::vector<WallInfo> walls;
	for (const auto& o : objects) {
		if (HasTag(o, "Wall") || HasTag(o, "Canon") || HasTag(o, "Pipe") || HasTag(o, "BulletTank")) {
			WallInfo w;
			w.minX = o.translate.x - o.scale.x - enemyRadius;
			w.maxX = o.translate.x + o.scale.x + enemyRadius;
			w.minZ = o.translate.z - o.scale.z - enemyRadius;
			w.maxZ = o.translate.z + o.scale.z + enemyRadius;
			walls.push_back(w);
		}
	}

	// 抽出した壁情報をもとにグリッドを更新
	for (const auto& wall : walls) {
		// グリッド内での範囲を計算
		int minXIdx = static_cast<int>((wall.minX - snappedX) / cellLength_) + (GRID_SIZE / 2);
		int maxXIdx = static_cast<int>((wall.maxX - snappedX) / cellLength_) + (GRID_SIZE / 2);
		int minZIdx = static_cast<int>((wall.minZ - snappedZ) / cellLength_) + (GRID_SIZE / 2);
		int maxZIdx = static_cast<int>((wall.maxZ - snappedZ) / cellLength_) + (GRID_SIZE / 2);

		// 範囲をグリッド内にクランプ
		minXIdx = std::max(0, std::min(minXIdx, GRID_SIZE - 1));
		maxXIdx = std::max(0, std::min(maxXIdx, GRID_SIZE - 1));
		minZIdx = std::max(0, std::min(minZIdx, GRID_SIZE - 1));
		maxZIdx = std::max(0, std::min(maxZIdx, GRID_SIZE - 1));

		for (int z = minZIdx; z <= maxZIdx; ++z) {
			for (int x = minXIdx; x <= maxXIdx; ++x) {
				localGrid_[z][x].isWall = true;
			}
		}
	}

	// 自分がいる中心マスだけは、絶対に壁にしない！
	localGrid_[GRID_SIZE / 2][GRID_SIZE / 2].isWall = false;
}

void EnemyBehavior::AStar() {
	// ターゲットがいればA*を実行
	if (target_ != nullptr) {
		// targetの位置を最新の状態に
		targetPos_.x = target_->GetTransform().translate.x;
		targetPos_.y = target_->GetTransform().translate.y;
		targetPos_.z = target_->GetTransform().translate.z;

		// ScanSurroundと同じスナップ座標を作る
		float snappedX = std::floor(myPos_.x / cellLength_) * cellLength_;
		float snappedZ = std::floor(myPos_.z / cellLength_) * cellLength_;

		// 自分のグリッド内での位置（中心付近のどこか）
		int startX = static_cast<int>((myPos_.x - snappedX) / cellLength_) + (GRID_SIZE / 2);
		int startZ = static_cast<int>((myPos_.z - snappedZ) / cellLength_) + (GRID_SIZE / 2);

		// ターゲットのグリッド内での位置
		int targetGridX = static_cast<int>(std::floor((targetPos_.x - snappedX) / cellLength_)) + (GRID_SIZE / 2);
		int targetGridZ = static_cast<int>(std::floor((targetPos_.z - snappedZ) / cellLength_)) + (GRID_SIZE / 2);

		// グリッドの範囲内に収める(ターゲットが遠くにいても、とりあえずその方向の端を目指す)
		if (targetGridX < 1)
			targetGridX = 1;
		if (targetGridX >= GRID_SIZE - 1)
			targetGridX = GRID_SIZE - 2;
		if (targetGridZ < 1)
			targetGridZ = 1;
		if (targetGridZ >= GRID_SIZE - 1)
			targetGridZ = GRID_SIZE - 2;

		// ゴール地点のマスだけは、壁であっても無理やり「通れる道」として扱う
		localGrid_[targetGridZ][targetGridX].isWall = false;

		CalculatePath(startX, startZ, targetGridX, targetGridZ);
	}
}

void EnemyBehavior::CalculatePath(int startX, int startZ, int targetX, int targetZ) {
	// リストをリセットする
	openList_.clear();
	closedList_.clear();
	path_.clear();

	// 全てのノードのコストを初期化
	for (int z = 0; z < GRID_SIZE; ++z) {
		for (int x = 0; x < GRID_SIZE; ++x) {
			localGrid_[z][x].gCost = 999999.0f; // 大きい数字
			localGrid_[z][x].parent = nullptr;
			localGrid_[z][x].isOpen = false;
			localGrid_[z][x].isClosed = false;
		}
	}

	// スタート地点の設定
	Node* startNode = &localGrid_[startZ][startX];
	Node* targetNode = &localGrid_[targetZ][targetX];

	startNode->gCost = 0;
	startNode->hCost = static_cast<float>(std::abs(targetX - startX) + std::abs(targetZ - startZ));
	startNode->isOpen = true; // オープンリスト入り
	openList_.push_back(startNode);

	// リストの終わりまで続ける
	while (!openList_.empty()) {
		// オープンリストの中で一番fCostが低いノードを探す
		int currentIndex = 0;
		for (int i = 1; i < (int)openList_.size(); ++i) {
			// i番目のfCostがcurrentIndex番目より大きかったら
			if (openList_[i]->fCost() < openList_[currentIndex]->fCost()) {
				// currentIndex番目のものをi番目の物と置き換える
				currentIndex = i;
			}
		}

		Node* currentNode = openList_[currentIndex];

		// ゴールに到達したかをチェック
		if (currentNode == targetNode) {
			// 親をたどってルートを構築
			Node* temp = targetNode;

			while (temp != nullptr) {
				// グリッド座標をワールド座標に変換して格納
				DirectX::XMFLOAT3 p;
				p.x = myPos_.x + (temp->gridX - GRID_SIZE / 2) * cellLength_;
				p.y = myPos_.y;
				p.z = myPos_.z + (temp->gridZ - GRID_SIZE / 2) * cellLength_;
				path_.push_back(p);
				temp = temp->parent;
			}

			// ルートは逆順(ゴール->スタート)で入るので反転させる
			std::reverse(path_.begin(), path_.end());
			return;
		}

		// 現在のノードをクローズリストへ移動
		openList_.erase(openList_.begin() + currentIndex);
		currentNode->isOpen = false;
		currentNode->isClosed = true;
		closedList_.push_back(currentNode);

		// 移動するための8マス(上下左右斜め)調べる
		int dx[] = {0, 0, 1, -1, 1, 1, -1, -1};
		int dz[] = {1, -1, 0, 0, 1, -1, 1, -1};

		for (int i = 0; i < 8; ++i) {
			int nextX = currentNode->gridX + dx[i];
			int nextZ = currentNode->gridZ + dz[i];

			// グリッド範囲外ならスキップ
			if (nextX < 0 || nextX >= GRID_SIZE || nextZ < 0 || nextZ >= GRID_SIZE) {
				continue;
			}

			Node* neighbor = &localGrid_[nextZ][nextX];

			// Walkの時と、壁or既に調べ終わったマスならスキップ
			if (type_ == Walk && neighbor->isWall) {
				continue;
			}

			if (i >= 4) { // 斜め移動の場合
				// 例えば「右」と「上」が壁なら、「右斜め上」は通れないようにする
				if (localGrid_[currentNode->gridZ][nextX].isWall || localGrid_[nextZ][currentNode->gridX].isWall) {
					continue;
				}
			}

			// 既にクローズドリスト(探索済み)に入っているかチェック (O(1)に改善)
			if (neighbor->isClosed) {
				continue;
			}

			// 新しいGコスト(スタートからの歩数)を計算
			float moveCost = (i < 4) ? 1.0f : 1.41f;
			float newGCost = currentNode->gCost + moveCost;

			// 既にオープンリストにあるかチェック (O(1)に改善)
			if (!neighbor->isOpen || newGCost < neighbor->gCost) {
				neighbor->gCost = newGCost;
				neighbor->hCost = static_cast<float>(std::abs(targetX - nextX) + std::abs(targetZ - nextZ));
				neighbor->parent = currentNode;

				if (!neighbor->isOpen) {
					neighbor->isOpen = true;
					openList_.push_back(neighbor);
				}
			}
		}
	}
}

void EnemyBehavior::Debug() {
#ifndef NDEBUG
	ImGui::Begin("Enemy Infomation");
	ImGui::Text("Target Name : %s", targetName_.c_str());
	ImGui::Text("GroundHeight : %f", groundHeight_);
	ImGui::Text("Move Type : %s", (type_ == Fly ? "Fly" : "Walk"));

	if (showDebugGrid_) {
		ImGui::Text("Local Grid Debug");
		// 文字列を一括で構築して表示速度を稼ぐ
		std::string gridStr;
		gridStr.reserve(GRID_SIZE * (GRID_SIZE * 3 + 1));

		for (int z = GRID_SIZE - 1; z >= 0; --z) {
			for (int x = 0; x < GRID_SIZE; ++x) {
				// そのマスが path_ に含まれているかチェック
				bool isPath = false;
				for (const auto& p : path_) {
					int px = static_cast<int>((p.x - myPos_.x) / cellLength_) + (GRID_SIZE / 2);
					int pz = static_cast<int>((p.z - myPos_.z) / cellLength_) + (GRID_SIZE / 2);
					if (px == x && pz == z) {
						isPath = true;
						break;
					}
				}

				if (x == GRID_SIZE / 2 && z == GRID_SIZE / 2)
					gridStr += "|S|";
				else if (isPath)
					gridStr += " * ";
				else if (localGrid_[z][x].isWall)
					gridStr += " # ";
				else
					gridStr += " . ";
			}
			gridStr += "\n";
		}
		ImGui::TextUnformatted(gridStr.c_str());
	}
	ImGui::End();
#endif
}

std::string EnemyBehavior::SerializeParameters() {
	nlohmann::json j;
	j["moveType"] = (int)type_;
	j["targetType"] = (int)targetType_;
	j["priority"] = (int)priority_;
	j["speed"] = speed_;
	return j.dump();
}

void EnemyBehavior::DeserializeParameters(const std::string& data) {
	if (data.empty())
		return;
	try {
		auto j = nlohmann::json::parse(data);
		if (j.contains("moveType"))
			type_ = (MoveType)j["moveType"].get<int>();
		if (j.contains("targetType"))
			targetType_ = (TargetType)j["targetType"].get<int>();
		if (j.contains("priority"))
			priority_ = (TargetPriority)j["priority"].get<int>();
		if (j.contains("speed"))
			speed_ = j["speed"].get<float>();
	} catch (...) {
	}
}

// ★ スクリプト自動登録
REGISTER_SCRIPT(EnemyBehavior);

} // namespace Game
