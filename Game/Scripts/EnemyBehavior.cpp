#include "EnemyBehavior.h"
#include "../imgui/imgui.h"
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
	// ここに初期設定を記述
	// 事前に決めたターゲットを検索
	SearchTarget(obj, scene);
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

	// 周囲の情報をスキャン
	ScanSurround(obj, scene);

	// A*(エースターアルゴリズム)でオブジェクトまでの最短ルートを探す
	AStar();

	// 移動
	Move(obj, dt);

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
	const char* types[] = { "Walk", "Fly" };
	if (ImGui::Combo("Enemy Type", &typeNum, types, IM_ARRAYSIZE(types))) {
		// 選ばれた番号をそのまま enum にキャストして戻せばOK！
		type_ = static_cast<MoveType>(typeNum);
	}

	// 追うオブジェクトのタグを設定
	// ライトの種類を選べるようにする
	int targetNum = static_cast<int>(targetType_);
	const char* targetTypes[] = { "Player", "Core", "Defender" };
	if (ImGui::Combo("Target", &targetNum, targetTypes, IM_ARRAYSIZE(targetTypes))) {
		// 選ばれた番号をそのまま enum にキャストして戻せばOK！
		targetType_ = static_cast<TargetType>(targetNum);

		// タグ名を更新(シーンを動かしたときにすぐに検索できるように)
		if (targetType_ == Player) {
			targetName_ = "Player";
		}
		else if (targetType_ == Core) {
			targetName_ = "Core";
		}
		else if (targetType_ == Defender) {
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
		const char* priorities[] = { "Near", "Far" };
		if (ImGui::Combo("Priority", &priorityNum, priorities, IM_ARRAYSIZE(priorities))) {
			priority_ = static_cast<TargetPriority>(priorityNum);
		}
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
			float distSq = dx * dx + dz * dz;	// 軽量化のために平方根は取らない

			// priorityごとの対応
			if (priority_ == Near) {	// Near
				if (distSq < bestDistance) {
					bestDistance = distSq;
					bestTarget = const_cast<SceneObject*>(&objects[i]);
				}
			}
			else {	// Far
				if (distSq > bestDistance) {
					bestDistance = distSq;
					bestTarget = const_cast<SceneObject*>(&objects[i]);
				}
			}
		}
	}
	target_ = bestTarget;
}

void EnemyBehavior::Move(SceneObject& obj, float dt) {
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

	if (distance > 0.1f) {
		float dirX = diffX / distance;
		float dirZ = diffZ / distance;

		obj.translate.x += dirX * speed_ * dt;
		obj.translate.z += dirZ * speed_ * dt;
	}
	else if (path_.size() > 1) {
		// 目的地にほぼ着いたら、座標をマスの中心に強制セットしてズレをリセット
		obj.translate.x = nextPos.x;
		obj.translate.z = nextPos.z;
	}

	// type別Y座標の対応
	//if (type_ == Fly) {
	//	/*オブジェクトのXZ軸から地面と接してるY座標を割り出せる関数を実装してもらったらそれを元にオフセットを作成*/

	//	float baseHeight = 3.0f;	// 基準とする高さ

	//	// sin波を使ってふわふわさせる
	//	static float totalTime = 0.0f;
	//	totalTime += dt;

	//	float hoverRange = 0.5f;	// 揺れ幅
	//	float hoverSpeed = 2.0f;	// 揺れのスピード

	//	// 基準の高さに揺れの高さを足す
	//	obj.translate.y = groundHeight_ + baseHeight + (std::sin(totalTime * hoverSpeed) * hoverRange);
	//}

	// 歩行タイプは地面にいるので特別な処理はなし
}

void EnemyBehavior::ScanSurround(SceneObject& obj, GameScene* scene) {
	myPos_.x = obj.GetTransform().translate.x;
	myPos_.y = obj.GetTransform().translate.y;
	myPos_.z = obj.GetTransform().translate.z;

	// 自分の位置をスナップ（ガタつき防止）
	float snappedX = std::floor(myPos_.x / cellLength_) * cellLength_;
	float snappedZ = std::floor(myPos_.z / cellLength_) * cellLength_;

	float enemyRadius = 1.0f;

	for (int z = 0; z < GRID_SIZE; ++z) {
		for (int x = 0; x < GRID_SIZE; ++x) {
			// このマスのワールド座標
			float worldX = snappedX + (x - GRID_SIZE / 2) * cellLength_;
			float worldZ = snappedZ + (z - GRID_SIZE / 2) * cellLength_;

			localGrid_[z][x].isWall = false;
			localGrid_[z][x].gridX = x;
			localGrid_[z][x].gridZ = z;

			auto& objects = scene->GetObjects();
			for (size_t i = 0; i < objects.size(); ++i) {
				if (HasTag(objects[i], "Wall")) {
					// 壁の座標とスケールを取得
					DirectX::XMFLOAT3 wallPos;
					wallPos.x = objects[i].GetTransform().translate.x;
					wallPos.y = objects[i].GetTransform().translate.y;
					wallPos.z = objects[i].GetTransform().translate.z;
					DirectX::XMFLOAT3 wallScale;
					wallScale.x = objects[i].GetTransform().scale.x;
					wallScale.y = objects[i].GetTransform().scale.y;
					wallScale.z = objects[i].GetTransform().scale.z;

					// 壁の当たり判定の範囲（AABB）を計算
					// 自作エンジンのスケールが「中心から端まで」ならそのまま、
					// 「端から端（全幅）」なら 0.5f を掛けてね。
					float minX = wallPos.x - wallScale.x - enemyRadius;
					float maxX = wallPos.x + wallScale.x + enemyRadius;
					float minZ = wallPos.z - wallScale.z - enemyRadius;
					float maxZ = wallPos.z + wallScale.z + enemyRadius;

					// マスの中心点が、壁の矩形の中に入っているか判定
					if (worldX >= minX && worldX <= maxX && worldZ >= minZ && worldZ <= maxZ) {
						localGrid_[z][x].isWall = true;
						break; // このマスは壁確定なので次のマスへ
					}
				}
			}

			// 自分がいる中心マスだけは、絶対に壁にしない！
			localGrid_[GRID_SIZE / 2][GRID_SIZE / 2].isWall = false;
		}
	}
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

	// 全てのノードのコストを初期化(コストが低いものを探索に使うため数字を大きくして初期化)
	for (int z = 0; z < GRID_SIZE; ++z) {
		for (int x = 0; x < GRID_SIZE; ++x) {
			localGrid_[z][x].gCost = 999999.0f; // 大きい数字
			localGrid_[z][x].parent = nullptr;
		}
	}

	// スタート地点の設定
	Node* startNode = &localGrid_[startZ][startX];
	Node* targetNode = &localGrid_[targetZ][targetX];

	startNode->gCost = 0;
	startNode->hCost = static_cast<float>(std::abs(targetX - startX) + std::abs(targetZ - startZ));
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
		closedList_.push_back(currentNode);

		// 移動するための8マス(上下左右斜め)調べる
		int dx[] = { 0, 0, 1, -1, 1, 1, -1, -1 };
		int dz[] = { 1, -1, 0, 0, 1, -1, 1, -1 };

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
				if (localGrid_[currentNode->gridZ][nextX].isWall ||
					localGrid_[nextZ][currentNode->gridX].isWall) {
					continue;
				}
			}

			// クローズドリスト(探索済み)に入っているかチェック
			bool isClosed = false;
			for (size_t j = 0; j < closedList_.size(); ++j) {
				if (closedList_[j] == neighbor) {
					isClosed = true;
					break;
				}
			}

			if (isClosed) {
				continue;
			}

			// 新しいGコスト(スタートからの歩数)を計算
			float moveCost = (i < 4) ? 1.0f : 1.41f;
			float newGCost = currentNode->gCost + moveCost;

			// 既にオープン理宇とにある場合、新しいルートの方が優秀かチェック
			bool isOpen = false;
			for (size_t j = 0; j < openList_.size(); ++j) {
				if (openList_[j] == neighbor) {
					isOpen = true;
					break;
				}
			}

			if (!isOpen || newGCost < neighbor->gCost) {
				neighbor->gCost = newGCost;
				neighbor->hCost = static_cast<float>(std::abs(targetX - nextX) + std::abs(targetZ - nextZ));
				neighbor->parent = currentNode;

				if (!isOpen) {
					openList_.push_back(neighbor);
				}
			}
		}
	}
}

void EnemyBehavior::Debug() {
	ImGui::Begin("Enemy Infomation");
	ImGui::Text("Target Name : %s", targetName_.c_str());
	ImGui::Text("Local Grid Debug");
	for (int z = GRID_SIZE - 1; z >= 0; --z) {
		for (int x = 0; x < GRID_SIZE; ++x) {
			// そのマスが path_ に含まれているかチェック
			bool isPath = false;
			for (size_t i = 0; i < path_.size(); ++i) {
				// ワールド座標から逆算してこのマスかどうか判定
				int px = static_cast<int>((path_[i].x - myPos_.x) / cellLength_) + (GRID_SIZE / 2);
				int pz = static_cast<int>((path_[i].z - myPos_.z) / cellLength_) + (GRID_SIZE / 2);
				if (px == x && pz == z) {
					isPath = true;
					break;
				}
			}

			if (x == GRID_SIZE / 2 && z == GRID_SIZE / 2)
				ImGui::Text("|S|"); // Self
			else if (isPath)
				ImGui::Text(" * "); // ルート
			else if (localGrid_[z][x].isWall)
				ImGui::Text(" # "); // 壁
			else
				ImGui::Text(" . ");

			ImGui::SameLine();
		}
		ImGui::NewLine();
	}
	ImGui::End();
}

// ★ スクリプト自動登録
REGISTER_SCRIPT(EnemyBehavior);

} // namespace Game
