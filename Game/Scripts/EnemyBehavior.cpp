#include "EnemyBehavior.h"
#include "../../Engine/ThirdParty/nlohmann/json.hpp"
#ifdef USE_IMGUI
#include "../../externals/imgui/imgui.h"
#endif
#include "ObjectTypes.h"
#include "Scenes/GameScene.h"
#include "ScriptEngine.h"

namespace Game {

static bool HasTag(entt::registry& registry, entt::entity entity, const char* tagName) {
	if (!registry.valid(entity) || !registry.all_of<TagComponent>(entity)) return false;
	return registry.get<TagComponent>(entity).tag == tagName;
}

void EnemyBehavior::Start(entt::entity entity, GameScene* scene) {
	ownerId_ = static_cast<uint32_t>(entity);
	pCurrentScene_ = scene;
	auto& registry = scene->GetRegistry();
	auto& tc = registry.get<TransformComponent>(entity);

	// ★修正: 物理演算を有効化し、ノックバックを受け入れ可能にする
	if (registry.all_of<RigidbodyComponent>(entity)) {
		registry.get<RigidbodyComponent>(entity).isKinematic = false;
	}

	// 出現時に一度ターゲットを検索
	SearchTarget(entity, scene);

	// 出現時に地面の高さを即座に計算（初動の埋まり防止）
	float h = scene->GetHeightAt(tc.translate.x, tc.translate.z, tc.translate.y + 1.0f, static_cast<uint32_t>(entity));
	if (h > -9999.0f) {
		groundHeight_ = h;
	} else {
		groundHeight_ = tc.translate.y - 1.0f; // 地面が見つからない場合は現在の位置を基準にする
	}

	// Flyタイプの場合は重力を無効化し、初期高度を設定
	if (type_ == Fly) {
		if (registry.all_of<RigidbodyComponent>(entity)) {
			auto& rb = registry.get<RigidbodyComponent>(entity);
			rb.useGravity = false;
			rb.velocity = {0, 0, 0};
		}

		// スポナーの高さではなく、即座に正しい浮遊高度へ移動
		float baseHeight = 9.0f;
		tc.translate.y = groundHeight_ + baseHeight;
	} else {
		// Walkタイプも埋まり防止のためにオフセットを乗せる
		tc.translate.y = groundHeight_ + 1.0f;
	}
}

void EnemyBehavior::Update(entt::entity entity, GameScene* scene, float dt) {
	if (!scene || !scene->GetRegistry().valid(entity)) return;
	auto& registry = scene->GetRegistry();
	if (!registry.all_of<TransformComponent>(entity)) return;

	ownerId_ = static_cast<uint32_t>(entity);
	pCurrentScene_ = scene;

	if (registry.all_of<HealthComponent>(entity)) {
		auto& hc = registry.get<HealthComponent>(entity);
		if (hc.isDead) return;
		if (hc.hitStopTimer > 0.0f) return; // ヒットストップ中は動きを止める
	}

	// ターゲットが実在するか確認
	bool targetExists = false;
	if (targetId_ != 0) {
		entt::entity targetEntity = static_cast<entt::entity>(targetId_);
		if (registry.valid(targetEntity)) {
			targetExists = true;
		}
	}
	if (!targetExists) {
		targetId_ = 0;
		SearchTarget(entity, scene);
	}

	// ヒット中（無敵時間中）はスキャンと移動を停止
	bool isHit = false;
	if (registry.all_of<HealthComponent>(entity)) {
		if (registry.get<HealthComponent>(entity).invincibleTime > 0.0f) isHit = true;
	}

	if (!isHit) {
		scanTimer_ += dt;
		if (scanTimer_ > 0.2f) {
			scanTimer_ = 0.0f;
			ScanSurround(entity, scene);
			AStar();
		}

		Move(entity, scene, dt);
	}
}

void EnemyBehavior::OnDestroy(entt::entity /*entity*/, GameScene* /*scene*/) {
	// 終了時のクリーンアップなどを記述
}

void EnemyBehavior::OnEditorUI() {
#if defined(USE_IMGUI) && !defined(NDEBUG)
	// 敵の移動タイプ(地面や空中)
	int typeNum = static_cast<int>(type_);
	const char* types[] = {"Walk", "Fly"};
	if (ImGui::Combo("Enemy Type", &typeNum, types, IM_ARRAYSIZE(types))) {
		type_ = static_cast<MoveType>(typeNum);
	}

	// 追うオブジェクトのタグを設定
	int targetNum = static_cast<int>(targetType_);
	const char* targetTypes[] = {"Player", "Core", "Defender"};
	if (ImGui::Combo("Target", &targetNum, targetTypes, IM_ARRAYSIZE(targetTypes))) {
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
		if (ownerId_ != 0 && pCurrentScene_) {
			entt::entity ownerEntity = static_cast<entt::entity>(ownerId_);
			if (pCurrentScene_->GetRegistry().valid(ownerEntity)) {
				SearchTarget(ownerEntity, pCurrentScene_);
			}
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
#endif
}

void EnemyBehavior::SearchTarget(entt::entity entity, GameScene* scene) {
	if (scene == nullptr) {
		return;
	}

	entt::entity bestTarget = entt::null;
	float bestDistance = (priority_ == TargetPriority::Near) ? FLT_MAX : -1.0f;

	auto view = scene->GetRegistry().view<TagComponent, TransformComponent>();
	auto& myTc = scene->GetRegistry().get<TransformComponent>(entity);

	for (auto e : view) {
		if (view.get<TagComponent>(e).tag == targetName_) {
			// 距離を計算
			auto& targetTc = view.get<TransformComponent>(e);
			float dx = targetTc.translate.x - myTc.translate.x;
			float dz = targetTc.translate.z - myTc.translate.z;
			float distSq = dx * dx + dz * dz;	// 軽量化のために平方根は取らない

			// priorityごとの対応
			if (priority_ == TargetPriority::Near) {
				if (distSq < bestDistance) {
					bestDistance = distSq;
					bestTarget = e;
				}
			} else { // Far
				if (distSq > bestDistance) {
					bestDistance = distSq;
					bestTarget = e;
				}
			}
		}
	}
	targetId_ = bestTarget != entt::null ? static_cast<uint32_t>(bestTarget) : 0;
}

void EnemyBehavior::Move(entt::entity entity, GameScene* scene, float dt) {
	// ターゲットが存在しない、またはtargetまでのPath(ルート)がなければ止める
	if (targetId_ == 0 || path_.empty()) {
		if (scene->GetRegistry().all_of<RigidbodyComponent>(entity)) {
			auto& rb = scene->GetRegistry().get<RigidbodyComponent>(entity);
			rb.velocity.x = 0; rb.velocity.z = 0;
		}
		return;
	}

	// path_の0番目は自分自身の場所に近いので、1番目(次のマス)を目指すのがスムーズ
	// もしpath_が1つしかないなら0番目を目指す
	int index = (path_.size() > 1) ? 1 : 0;
	DirectX::XMFLOAT3 nextPos = path_[index];

	auto& tc = scene->GetRegistry().get<TransformComponent>(entity);
	
	// 自分の位置を取得
	myPos_ = tc.translate;

	// 次のポイントへの方向
	float diffX = nextPos.x - myPos_.x;
	float diffZ = nextPos.z - myPos_.z;
	float distance = std::sqrt(diffX * diffX + diffZ * diffZ);

	// 移動方向の計算
	float vx = 0, vz = 0;
	if (distance > 0.1f) {
		float dirX = diffX / distance;
		float dirZ = diffZ / distance;
		vx = dirX * speed_;
		vz = dirZ * speed_;
		// 向きを合わせる
		tc.rotate.y = std::atan2(dirX, dirZ);
	}

	// 1. 水平移動の更新 (段差制限による壁判定を追加)
	float nextX = tc.translate.x + vx * dt;
	float nextZ = tc.translate.z + vz * dt;
	
	// 移動先の地面高さを先読み
	float currentFeetY = tc.translate.y - 1.0f; // 1.0f は埋まり防止オフセット
	float futureGround = scene->GetHeightAt(nextX, nextZ, tc.translate.y + 1.0f, static_cast<uint32_t>(entity));
	
	// 移動先が 0.4m 以上高いなら壁とみなして進ませない
	if (futureGround > currentFeetY + 0.4f) {
		vx = 0;
		vz = 0;
	}

	// 2. 垂直移動と重力の計算
	if (scene->GetRegistry().all_of<RigidbodyComponent>(entity)) {
		auto& rb = scene->GetRegistry().get<RigidbodyComponent>(entity);
		
		if (type_ == Walk) {
			// 重力を適用
			rb.velocity.y -= 9.8f * dt;
			tc.translate.y += rb.velocity.y * dt;

			// 地面スナップ (埋まり防止オフセット 1.0f)
			float floorY = groundHeight_ + 1.0f;
			if (tc.translate.y <= floorY + 0.05f) {
				tc.translate.y = floorY;
				rb.velocity.y = 0.0f;
			}
		} else if (type_ == Fly) {
			// Flyタイプはふわふわさせる
			float baseHeight = 9.0f;
			totalTime_ += dt;
			float hoverRange = 0.5f;
			float hoverSpeed = 2.0f;
			tc.translate.y = groundHeight_ + baseHeight + (std::sin(totalTime_ * hoverSpeed) * hoverRange);
			rb.velocity.y = 0.0f;
		}
		
		// 実体移動は PhysicsSystem が rb.velocity に基づいて行う。
		// ここでは AI が望む速度（vx, vz）をセットする。
		rb.velocity.x = vx;
		rb.velocity.z = vz;
	}

	// 目標地点に十分近い場合の補正
	if (distance <= 0.1f && path_.size() > 1) {
		// tc.translate.x = nextPos.x;
		// tc.translate.z = nextPos.z;
	}

	// 現在のXZ座標から地面の高さを取得 (負荷軽減のため頻度を制限)
	if (scanTimer_ <= dt) {
		float newHeight = scene->GetHeightAt(tc.translate.x, tc.translate.z, tc.translate.y + 1.0f, static_cast<uint32_t>(entity));
		if (newHeight > -9999.0f) {
			groundHeight_ = newHeight;
		}
	}
}

void EnemyBehavior::ScanSurround(entt::entity entity, GameScene* scene) {
	auto& registry = scene->GetRegistry();
	myPos_ = registry.get<TransformComponent>(entity).translate;

	// 自分の位置をスナップ（基準座標）
	float snappedX = std::floor(myPos_.x / cellLength_) * cellLength_;
	float snappedZ = std::floor(myPos_.z / cellLength_) * cellLength_;

	// 自分が立っている場所の地面の高さを基準にする
	float currentGroundHeight = scene->GetHeightAt(myPos_.x, myPos_.z, myPos_.y + 1.0f, static_cast<uint32_t>(entity));

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
				float cellHeight = scene->GetHeightAt(cellWorldX, cellWorldZ, myPos_.y + 1.0f, static_cast<uint32_t>(entity));

				// 自分の位置からそのマスまでの距離を計算する
				float dx = (float)(x - GRID_SIZE / 2);
				float dz = (float)(z - GRID_SIZE / 2);
				float dist = std::sqrt(dx * dx + dz * dz);

				// 坂の許容範囲
				float maxSlope = 2.0f; // 45度の坂を許容する（調整可能）
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

	// 壁オブジェクトを事前に抽出（計算量を減らすため、二重ループを避ける）
	struct WallInfo {
		float minX, maxX, minZ, maxZ;
	};
	std::vector<WallInfo> walls;
	
	auto wallView = registry.view<TagComponent, TransformComponent>();
	for (auto e : wallView) {
		const auto& tag = wallView.get<TagComponent>(e).tag;
		if (tag == "Wall" || tag == "Canon" || tag == "Pipe" || tag == "BulletTank") {
			auto& tc = wallView.get<TransformComponent>(e);
			WallInfo w;
			w.minX = tc.translate.x - tc.scale.x - enemyRadius;
			w.maxX = tc.translate.x + tc.scale.x + enemyRadius;
			w.minZ = tc.translate.z - tc.scale.z - enemyRadius;
			w.maxZ = tc.translate.z + tc.scale.z + enemyRadius;
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
	if (targetId_ != 0) {
		entt::entity targetEntity = static_cast<entt::entity>(targetId_);
		if (!pCurrentScene_->GetRegistry().valid(targetEntity) || !pCurrentScene_->GetRegistry().all_of<TransformComponent>(targetEntity)) return;

		auto& targetTc = pCurrentScene_->GetRegistry().get<TransformComponent>(targetEntity);
		// targetの位置を最新の状態に
		targetPos_.x = targetTc.translate.x;
		targetPos_.y = targetTc.translate.y;
		targetPos_.z = targetTc.translate.z;

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
/*
#ifndef NDEBUG
#ifdef USE_IMGUI
	ImGui::Begin("Enemy Debug");
	ImGui::Text("State: %d", (int)state_);
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
*/
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
