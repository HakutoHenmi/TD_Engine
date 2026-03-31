#include "NavigationManager.h"
#include "../Scenes/GameScene.h"
#include <queue>

void NavigationManager::Initialize(int width, int height, float cellSize) {
	width_ = width;
	height_ = height;
	cellSize_ = cellSize;
	grid_.assign(width * height, FlowCell{1, FLT_MAX, 0.0f, 0.0f});
}

void NavigationManager::UpdateCostMap(Game::GameScene* scene) {
	auto& registry = scene->GetRegistry();

	// 全セルをリセット(平地[1]にリセット)
	for (auto& cell : grid_) {
		cell.cost = 1;
	}

	// シーン内の壁オブジェクト(Wall, Cannon, Pipe)を検索
	// コンポーネントをひとつづつに分ける
	auto tagView = registry.view<Game::TagComponent>();
	auto tcView = registry.view<Game::TransformComponent>();
	for (entt::entity entity : tagView) {
		// TagComponentを持っているかチェック
		if (registry.all_of<Game::TagComponent>(entity)) {
			const auto& tag = tagView.get<Game::TagComponent>(entity).tag;
			// チェックしたタグがWall, Canon, Pipeなら
			if (tag == "Wall" || tag == "Cannon" || tag == "Pipe") {
				if (registry.all_of<Game::TransformComponent>(entity)) {
					auto& tc = tcView.get<Game::TransformComponent>(entity);

					// オブジェクトの範囲をグリッド座標に変換して、その範囲を壁[255]にする
					int minX = static_cast<int>((tc.translate.x - tc.scale.x) / cellSize_);
					int maxX = static_cast<int>((tc.translate.x + tc.scale.x) / cellSize_);
					int minZ = static_cast<int>((tc.translate.z - tc.scale.z) / cellSize_);
					int maxZ = static_cast<int>((tc.translate.z + tc.scale.z) / cellSize_);

					for (int z = minZ; z <= maxZ; ++z) {
						for (int x = minX; x <= maxX; ++x) {
							// グリッドの範囲内かチェック
							if (x >= 0 && x < width_ && z >= 0 && z < height_) {
								grid_[GetIndex(x, z)].cost = 255; // 壁
							}
						}
					}
				}
			}
		}
	}
}

void NavigationManager::GenerateFlowField(float targetWorldX, float targetWorldZ) {
	// 初期化する(全マスのコストを最大に)
	for (auto& cell : grid_) {
		cell.bestCost = FLT_MAX;
	}

	// 目的地をグリッド座標に変換
	int targetX = static_cast<int>(targetWorldX / cellSize_);
	int targetZ = static_cast<int>(targetWorldZ / cellSize_);

	// 目的地がグリッドの範囲外なら何もしない
	if (targetX < 0 || targetX >= width_ || targetZ < 0 || targetZ >= height_)
		return;

	// ゴールの設定
	std::queue<int> openIndices;
	int targetIndex = GetIndex(targetX, targetZ);
	grid_[targetIndex].bestCost = 0;
	openIndices.push(targetIndex);

	// ダイクストラ法による伝播(上下左右斜め)
	int dx[] = {0, 0, 1, -1, 1, 1, -1, -1};
	int dz[] = {1, -1, 0, 0, 1, -1, 1, -1};

	while (!openIndices.empty()) {
		int currIndex = openIndices.front();
		openIndices.pop();

		int currX = currIndex % width_;
		int currZ = currIndex / width_;

		for (int i = 0; i < 8; ++i) {
			int nextX = currX + dx[i];
			int nextZ = currZ + dz[i];

			// 斜め移動の場合、その横にある2マスが壁なら通れないようにする
			if (grid_[GetIndex(nextX, currZ)].cost == 255 || grid_[GetIndex(currX, nextZ)].cost == 255) {
				continue;
			}

			if (nextX >= 0 && nextX < width_ && nextZ >= 0 && nextZ < height_) {
				int nextIndex = GetIndex(nextX, nextZ);
				FlowCell& nextCell = grid_[nextIndex];

				// 壁[255]ではないより短い経路が見つかった場合
				if (nextCell.cost < 255) {
					float moveCost = (i < 4) ? (float)nextCell.cost : (float)nextCell.cost * 1.414f;
					float newCost = grid_[currIndex].bestCost + moveCost;

					if (newCost < nextCell.bestCost) {
						nextCell.bestCost = newCost;
						openIndices.push(nextIndex);
					}
				}
			}
		}
	}

	// ベクトル場の生成
	CalculateDirections();
}

void NavigationManager::CalculateDirections() {
	for (int z = 0; z < height_; ++z) {
		for (int x = 0; x < width_; ++x) {
			int currIndex = GetIndex(x, z);
			if (grid_[currIndex].cost == 255) continue; // 壁[255]なら何もしない

			// 周囲のコスト差から勾配を作る
			// 左(x-1)と右(x+1)、上(z+1)と下(z-1)のコストを比較する
			float west = (x > 0) ? grid_[GetIndex(x - 1, z)].bestCost : grid_[currIndex].bestCost;
			float east  = (x < width_ - 1) ? grid_[GetIndex(x + 1, z)].bestCost : grid_[currIndex].bestCost;
			float north = (z < height_ - 1) ? grid_[GetIndex(x, z + 1)].bestCost : grid_[currIndex].bestCost;
			float south = (z > 0) ? grid_[GetIndex(x, z - 1)].bestCost : grid_[currIndex].bestCost;

			// よりコストの低いほうへ向かうベクトルを計算
			float dirX = west - east;
			float dirZ = south - north;

			//正規化して保存
			float length = std::sqrt(dirX * dirX + dirZ * dirZ);
			if (length > 0.001f) {
				grid_[currIndex].dirX = dirX / length;
				grid_[currIndex].dirZ = dirZ / length;
			}
			else {
				grid_[currIndex].dirX = 0;
				grid_[currIndex].dirZ = 0;
			}
		}
	}
}

void NavigationManager::GetDirection(float worldX, float worldZ, float& outX, float& outZ) {
	int x = static_cast<int>(worldX / cellSize_);
	int z = static_cast<int>(worldZ / cellSize_);

	if (x >= 0 && x < width_ && z >= 0 && z < height_) {
		const auto& cell = grid_[GetIndex(x, z)];
		outX = grid_[GetIndex(x, z)].dirX;
		outZ = grid_[GetIndex(x, z)].dirZ;
	} else {
		outX = 0;
		outZ = 0;
	}
}