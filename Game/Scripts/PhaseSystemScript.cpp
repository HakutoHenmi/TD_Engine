#include "PhaseSystemScript.h"
#include "../../Engine/PathUtils.h"
#include "Editor/EditorUI.h"
#include "ObjectTypes.h"
#include "PhaseTransition.h"
#include "Scenes/GameScene.h"
#include "ScriptEngine.h"
#include <cfloat>
#include <cmath>
#include <fstream>
#include <string.h>
#include <vector>
#ifdef USE_IMGUI
#include <imgui.h>
#endif
#include "../../Engine/Input.h"
#include "../../Engine/WindowDX.h"
#include <iostream>
#include <unordered_map>

// Button UI
#include "InstallationManager.h"

#include "WaveManagement.h"
#include "../../Engine/ThirdParty/nlohmann/json.hpp"

using json = nlohmann::json;

namespace Game {

namespace {
float SnapTo2x2Grid(float value) { return std::floor(value / 2.0f) * 2.0f; }

bool IsPointerOverInstallationButton(GameScene* scene) {
	if (!scene)
		return false;

	auto& registry = scene->GetRegistry();
	auto view = registry.view<UIButtonComponent>();
	for (auto entity : view) {
		const auto& btn = view.get<UIButtonComponent>(entity);
		if (!btn.enabled || !btn.isHovered)
			continue;

		if (registry.all_of<RectTransformComponent>(entity) && !registry.get<RectTransformComponent>(entity).enabled)
			continue;

		if (InstallationManager::IsManagedButton(entity)) {
			return true;
		}
	}

	return false;
}

std::vector<Engine::Vector3> BuildPipePathPoints(const Engine::Vector3& start, const Engine::Vector3& end) {
	std::vector<Engine::Vector3> points;

	const int x0 = static_cast<int>(SnapTo2x2Grid(start.x));
	const int z0 = static_cast<int>(SnapTo2x2Grid(start.z));
	const int x1 = static_cast<int>(SnapTo2x2Grid(end.x));
	const int z1 = static_cast<int>(SnapTo2x2Grid(end.z));
	constexpr int kStep = 2;

  const float y = 0.0f;
	points.push_back({static_cast<float>(x0), y, static_cast<float>(z0)});

	int x = x0;
	int z = z0;
	const int stepX = (x1 > x0) ? kStep : -kStep;
	const int stepZ = (z1 > z0) ? kStep : -kStep;
	const int totalX = std::abs((x1 - x0) / kStep);
	const int totalZ = std::abs((z1 - z0) / kStep);

	int movedX = 0;
	int movedZ = 0;
	while (movedX < totalX || movedZ < totalZ) {
		const bool canMoveX = movedX < totalX;
		const bool canMoveZ = movedZ < totalZ;

		bool moveX = false;
		if (canMoveX && canMoveZ) {
			const int nextXScore = (movedX + 1) * totalZ;
			const int nextZScore = (movedZ + 1) * totalX;
			moveX = nextXScore <= nextZScore;
		} else {
			moveX = canMoveX;
		}

		if (moveX) {
			x += stepX;
			++movedX;
		} else {
			z += stepZ;
			++movedZ;
		}

		points.push_back({static_cast<float>(x), y, static_cast<float>(z)});
	}

	return points;
}

bool TryGetPlacementSurfaceYAt(GameScene* scene, float x, float z, float& outY) {
	if (!scene)
		return false;

	auto* renderer = scene->GetRenderer();
	if (!renderer)
		return false;

	auto& registry = scene->GetRegistry();
	DirectX::XMVECTOR rayOrig = DirectX::XMVectorSet(x, 1000.0f, z, 1.0f);
	DirectX::XMVECTOR rayDir = DirectX::XMVectorSet(0.0f, -1.0f, 0.0f, 0.0f);

	float bestDist = FLT_MAX;
	bool hit = false;

	registry.view<NameComponent, TransformComponent>().each([&](entt::entity entity, const NameComponent& nc, const TransformComponent& tc) {
		const bool isWallTag = registry.all_of<TagComponent>(entity) && registry.get<TagComponent>(entity).tag == TagType::Wall;
		const bool isTerrain = (nc.name.find("Terrain") != std::string::npos) || (nc.name.find("Floor") != std::string::npos) || (nc.name.find("Ground") != std::string::npos) ||
							   (nc.name.find("Stage") != std::string::npos) || (nc.name.find("Plane") != std::string::npos);
		if (!isTerrain && !isWallTag)
			return;

		Engine::Model* model = nullptr;
		if (registry.all_of<GpuMeshColliderComponent>(entity)) {
			auto& gmc = registry.get<GpuMeshColliderComponent>(entity);
			if (gmc.meshHandle != 0) {
				model = renderer->GetModel(gmc.meshHandle);
			}
		}

		if (!model && registry.all_of<MeshRendererComponent>(entity)) {
			auto& mr = registry.get<MeshRendererComponent>(entity);
			if (mr.modelHandle != 0) {
				model = renderer->GetModel(mr.modelHandle);
			}
		}

		if (!model)
			return;

		float d;
		Engine::Vector3 hp;
		if (model->RayCast(rayOrig, rayDir, tc.ToMatrix(), d, hp) && d < bestDist) {
			bestDist = d;
			outY = hp.y;
			hit = true;
		}
	});

	if (!hit) {
		outY = 0.0f;
	}

	return hit;
}
} // namespace

void PhaseSystemScript::Start(entt::entity entity, GameScene* scene) {
	(void)entity;
	(void)scene;
	isPhase_ = PreparationPhase;
	NextPhase_ = PreparationPhase;
	preIsPhase_ = PreparationPhase;
	currentPhase_ = 0;
	CoinCount = StartCoinCount_;

	// 状態フラグの初期化
	isPhaseTransitioning_ = false;
	isFadeFinished_ = false;
	isPlacementMode_ = false;
	isSellMode_ = false;
	isPipeSet_ = false;
	hasPipeStartPoint_ = false;

	enemyCountUI_ = entt::null;
	installationCostUI_ = entt::null;

	// キー入力の初期化
	preKeyP_ = false;
	preKeySpace_ = false;
	preKeyN_ = false;

	// 座標の初期化
	pipeStartX_ = 0.0f;
	pipeStartY_ = 0.0f;
	pipeStartZ_ = 0.0f;

	// 必要に応じてパスやハンドルの初期化
	selectedObjPath_ = "Resources/Models/cube/cube.obj";
	previewObjPath_ = "";
	previewModelHandle_ = 0;
	previewTextureHandle_ = 0;

	// スキルツリーの初期化
	if (auto* renderer = Engine::Renderer::GetInstance()) {
		skillTree_.SetUIContext(renderer, (float)Engine::WindowDX::kW, (float)Engine::WindowDX::kH, 0.0f, 0.0f);
		skillTree_.Start(entity, scene);
		skillTree_.LoadFromJson("Resources/Scenes/skills.json");
	}

	// 設置開始イベントの購読
	SubscribeString(scene, "StartInstallation", [this](const std::string& dataStr) {
		try {
			json data = json::parse(dataStr);
			selectedObjPath_ = data.value("prefab", "");
			selectedObjCost_ = data.value("cost", 0);
			isPipeSet_ = data.value("isPipe", false);
			isPlacementMode_ = true;
			isSellMode_ = false;
			hasPipeStartPoint_ = false;
		} catch (...) {}
	});
}

void PhaseSystemScript::Update(entt::entity entity, GameScene* scene, float dt) {
	(void)entity;
	(void)scene;
	(void)dt;
	auto* input = Engine::Input::GetInstance();
	if (!input)
		return;

	// スクリプト動作確認用の白い線 (常に表示)
	auto* renderer = scene->GetRenderer();
	if (renderer) {
#ifndef NDEBUG
		renderer->DrawLine3D({0, 20, 0}, {5, 20, 0}, {1, 1, 1, 1}, true);
		if (isPhase_ == PreparationPhase)
			renderer->DrawLine3D({0, 21, 0}, {5, 21, 0}, {0, 1, 0, 1}, true);
		if (isPlacementMode_)
			renderer->DrawLine3D({0, 22, 0}, {5, 22, 0}, {0, 0, 1, 1}, true);
		if (isSellMode_)
			renderer->DrawLine3D({0, 23, 0}, {5, 23, 0}, {1, 0, 0, 1}, true); // 赤線でSellモード表示
#endif
	}

	// ★入力処理: キーボードとUI両方からの入力を受け付ける
	bool key1 = input->Trigger(DIK_1) || (GetAsyncKeyState('1') & 0x8001);
	bool key2 = input->Trigger(DIK_2) || (GetAsyncKeyState('2') & 0x8001);
	bool key3 = input->Trigger(DIK_3) || (GetAsyncKeyState('3') & 0x8001);
	bool key4 = input->Trigger(DIK_4) || (GetAsyncKeyState('4') & 0x8001);
	bool key5 = input->Trigger(DIK_5) || (GetAsyncKeyState('5') & 0x8001);
	bool key6 = input->Trigger(DIK_6) || (GetAsyncKeyState('6') & 0x8001);
	bool keyX = input->Trigger(DIK_X) || (GetAsyncKeyState('X') & 0x8001); // 削除モード用
	bool keyP = false;
#ifndef NDEBUG
	keyP = input->Trigger(DIK_P) || (GetAsyncKeyState('P') & 0x8001);
#endif
	bool keySpace = input->Trigger(DIK_SPACE) || (GetAsyncKeyState(VK_SPACE) & 0x8001);

	// ★ スキルツリーの入力処理 (準備フェーズ中のみ)
	bool keyN = input->Trigger(DIK_N) || (GetAsyncKeyState('N') & 0x8001);

	// 外部(EnemySpawnerScript など)からのフェーズ変更要求を反映
	if (!isPhaseTransitioning_ && isPhase_ != Transition && NextPhase_ != isPhase_) {
		RequestPhaseChange(NextPhase_);
	}

	if (isPhase_ == PreparationPhase) {
        bool placementSelectionChangedThisFrame = false;
		const bool clickedInstallationButtonThisFrame = input->IsMouseTrigger(0) && IsPointerOverInstallationButton(scene);

		// Nキーでスキルツリーの開閉
		if (keyN && !preKeyN_) {
			skillTree_.Toggle(scene);
		}

// スキルツリーが開いている間はスキルツリーの更新のみ
		if (skillTree_.IsOpen()) {
			SetVar(entity, scene, "IsSkillTreeOpen", 1.0f);

			float mx = 0.0f;
			float my = 0.0f;
			float tW = (float)Engine::WindowDX::kW;
			float tH = (float)Engine::WindowDX::kH;

#if defined(USE_IMGUI) && !defined(NDEBUG)
			ImVec2 mousePos = ImGui::GetMousePos();
			ImVec2 gameMin = EditorUI::GetGameImageMin();
			ImVec2 gameMax = EditorUI::GetGameImageMax();
			float viewW = gameMax.x - gameMin.x;
			float viewH = gameMax.y - gameMin.y;

			if (viewW > 0.0f && viewH > 0.0f) {
				mx = (mousePos.x - gameMin.x) * (tW / viewW);
				my = (mousePos.y - gameMin.y) * (tH / viewH);
			}
#else
			input->GetMousePos(mx, my);
#endif

			skillTree_.SetUIContext(renderer, tW, tH, mx, my);
			skillTree_.Update(entity, scene, dt);

			preKeyN_ = keyN;
			return;
		}

		SetVar(entity, scene, "IsSkillTreeOpen", 0.0f);

		// 設置モードへの切り替え

		if (key1 || InstallationManager::IsButtonPressed("Resources/Prefabs/BulletTank.prefab")) {
			selectedObjPath_ = "Resources/Prefabs/BulletTank.prefab";
			selectedObjCost_ = InstallationManager::GetCost(selectedObjPath_);
			if (selectedObjCost_ == 0) selectedObjCost_ = tankCost_;
			isPlacementMode_ = true;
			isPipeSet_ = false;
			hasPipeStartPoint_ = false;
			placementSelectionChangedThisFrame = true;
		}

		if (key2 || InstallationManager::IsButtonPressed("Resources/Prefabs/Pipe.prefab")) {
			selectedObjPath_ = "Resources/Prefabs/Pipe.prefab";
			selectedObjCost_ = InstallationManager::GetCost(selectedObjPath_);
			if (selectedObjCost_ == 0) selectedObjCost_ = pipeCost_;
			isPipeSet_ = true;
			isPlacementMode_ = true;
			hasPipeStartPoint_ = false;
			placementSelectionChangedThisFrame = true;
		}

		if (key3 || InstallationManager::IsButtonPressed("Resources/Prefabs/Canon.prefab")) {
			selectedObjPath_ = "Resources/Prefabs/Canon.prefab";
			selectedObjCost_ = InstallationManager::GetCost(selectedObjPath_);
			if (selectedObjCost_ == 0) selectedObjCost_ = canonCost_;
			isPlacementMode_ = true;
			isPipeSet_ = false;
			hasPipeStartPoint_ = false;
			placementSelectionChangedThisFrame = true;
		}

		if (key4 || InstallationManager::IsButtonPressed("Resources/Prefabs/Missile.prefab")) {
			selectedObjPath_ = "Resources/Prefabs/Missile.prefab";
			selectedObjCost_ = InstallationManager::GetCost(selectedObjPath_);
			if (selectedObjCost_ == 0) selectedObjCost_ = missileCost_;
			isPlacementMode_ = true;
			isPipeSet_ = false;
			hasPipeStartPoint_ = false;
			placementSelectionChangedThisFrame = true;
		}

		if (key5 || InstallationManager::IsButtonPressed("Resources/Prefabs/Poison.prefab")) {
			selectedObjPath_ = "Resources/Prefabs/Poison.prefab";
			selectedObjCost_ = InstallationManager::GetCost(selectedObjPath_);
			if (selectedObjCost_ == 0) selectedObjCost_ = poisonCost_;
			isPlacementMode_ = true;
			isPipeSet_ = false;
			isSellMode_ = false;
			hasPipeStartPoint_ = false;
			placementSelectionChangedThisFrame = true;
		}

		if (key6 || InstallationManager::IsButtonPressed("Resources/Prefabs/IceCanon.prefab")) {
			selectedObjPath_ = "Resources/Prefabs/IceCanon.prefab";
			selectedObjCost_ = InstallationManager::GetCost(selectedObjPath_);
			if (selectedObjCost_ == 0) selectedObjCost_ = iceCanonCost_;
			isPlacementMode_ = true;
			isPipeSet_ = false;
			isSellMode_ = false;
			hasPipeStartPoint_ = false;
			placementSelectionChangedThisFrame = true;
		}

		// Xキーで削除(売却)モードへの切り替え
		if (keyX) {
			isSellMode_ = true;
			isPlacementMode_ = false;
			isPipeSet_ = false;
			hasPipeStartPoint_ = false;
			placementSelectionChangedThisFrame = true;
			EditorUI::Log("Sell Mode Activated");
		}

		if (input->IsMouseTrigger(1) && isPlacementMode_) {
			if (isPipeSet_ && hasPipeStartPoint_) {
				hasPipeStartPoint_ = false;
			} else {
				isPlacementMode_ = false;
				isPipeSet_ = false;
				hasPipeStartPoint_ = false;
			}
		}

		if (input->IsMouseTrigger(1) && isSellMode_) {
			isSellMode_ = false;
			EditorUI::Log("Sell Mode Deactivated");
		}

		if (!placementSelectionChangedThisFrame && !clickedInstallationButtonThisFrame && !isSellMode_) {
			Installation(scene, selectedObjPath_);
		}

		if (isSellMode_ && !clickedInstallationButtonThisFrame) {
			// マウス位置からレイキャストしてヒットしたハイライトを描画する
			float localX = 0, localY = 0;
			float tW = 0, tH = 0;
#if defined(USE_IMGUI) && !defined(NDEBUG)
			ImVec2 mousePos = ImGui::GetMousePos();
			ImVec2 gameMin = EditorUI::GetGameImageMin();
			ImVec2 gameMax = EditorUI::GetGameImageMax();
			tW = gameMax.x - gameMin.x;
			tH = gameMax.y - gameMin.y;
			if (tW > 0.0f && tH > 0.0f) {
				localX = mousePos.x - gameMin.x;
				localY = mousePos.y - gameMin.y;
			}
#else
			input->GetMousePos(localX, localY);
			tW = (float)Engine::WindowDX::kW;
			tH = (float)Engine::WindowDX::kH;
#endif
			auto& camera = scene->GetCamera();
			DirectX::XMVECTOR rayOrig, rayDir;
			EditorUI::ScreenToWorldRay(localX, localY, tW, tH, camera.View(), camera.Proj(), rayOrig, rayDir);

			float bestDist = FLT_MAX;
			entt::entity hoverEntity = entt::null;
			auto& registry = scene->GetRegistry();

			registry.view<TransformComponent>().each([&](entt::entity e, const TransformComponent& tc) {
				if (registry.all_of<NameComponent>(e)) {
					const auto& name = registry.get<NameComponent>(e).name;
					// 地形などは削除できないようにする
					if (name.find("Terrain") != std::string::npos || name.find("Plane") != std::string::npos || name.find("Core") != std::string::npos || name.find("Floor") != std::string::npos) return;
					// PipeConnectionはパイプのつなぎ目（導線）なのでレイキャスト対象から除外する
					// パイプ本体を削除すれば PipeScript::OnDestroy が自動的に消してくれる
					if (name.find("PipeConnection") != std::string::npos) return;
				}

				Engine::Model* model = nullptr;
				if (registry.all_of<GpuMeshColliderComponent>(e)) {
					model = scene->GetRenderer()->GetModel(registry.get<GpuMeshColliderComponent>(e).meshHandle);
				} else if (registry.all_of<MeshRendererComponent>(e)) {
					model = scene->GetRenderer()->GetModel(registry.get<MeshRendererComponent>(e).modelHandle);
				}

				if (model) {
					float d; Engine::Vector3 hp;
					if (model->RayCast(rayOrig, rayDir, tc.ToMatrix(), d, hp) && d < bestDist) {
						bestDist = d;
						hoverEntity = e;
					}
				}
			});

			if (hoverEntity != entt::null) {
				if (registry.all_of<TransformComponent>(hoverEntity)) {
					auto tc = registry.get<TransformComponent>(hoverEntity);
					// ルートの取得
					if (registry.all_of<HierarchyComponent>(hoverEntity)) {
						auto root = hoverEntity;
						while (registry.get<HierarchyComponent>(root).parentId != entt::null) {
							root = registry.get<HierarchyComponent>(root).parentId;
						}
						tc = registry.get<TransformComponent>(root);
					}

					Engine::Matrix4x4 mat = tc.ToMatrix();
					// Matrix4x4をXMMATRIXに変換
					DirectX::XMMATRIX xmat = DirectX::XMMatrixSet(
						mat.m[0][0], mat.m[0][1], mat.m[0][2], mat.m[0][3],
						mat.m[1][0], mat.m[1][1], mat.m[1][2], mat.m[1][3],
						mat.m[2][0], mat.m[2][1], mat.m[2][2], mat.m[2][3],
						mat.m[3][0], mat.m[3][1], mat.m[3][2], mat.m[3][3]
					);

					float hs = 1.0f; // 大体の大きさ
					// 少し外側に枠を描画（赤色で）
					Engine::Vector3 cv[8] = {
						{-hs, -hs, -hs}, {hs,  -hs, -hs}, {hs,  hs,  -hs}, {-hs, hs,  -hs},
						{-hs, -hs, hs }, {hs,  -hs, hs }, {hs,  hs,  hs }, {-hs, hs,  hs }
					};
					int edges[12][2] = {
						{0,1},{1,2},{2,3},{3,0},
						{4,5},{5,6},{6,7},{7,4},
						{0,4},{1,5},{2,6},{3,7}
					};
					for (int i = 0; i < 8; ++i) {
						DirectX::XMVECTOR p = DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(cv[i].x, cv[i].y, cv[i].z, 1.0f), xmat);
						DirectX::XMStoreFloat3(reinterpret_cast<DirectX::XMFLOAT3*>(&cv[i]), p);
					}
					for (int i = 0; i < 12; ++i) {
						scene->GetRenderer()->DrawLine3D(cv[edges[i][0]], cv[edges[i][1]], {1.0f, 0.0f, 0.0f, 1.0f}, true);
					}
				}
			}


			// 右クリックでキャンセルは上部で処理済み
			if (input->IsMouseTrigger(0)) {
				Engine::Vector3 hitPoint;
				if (TryGetTerrainHitPoint(scene, hitPoint)) { // TerrainHitPoint関数ですが実際レイキャストで地形を探す

					if (hoverEntity != entt::null) {
						// コストの推測 (prefabの復元情報がないため名前等から推測)
						int refundCost = 0;
						if (registry.all_of<NameComponent>(hoverEntity)) {
							const auto& name = registry.get<NameComponent>(hoverEntity).name;
							if (name.find("Tank") != std::string::npos) { refundCost = tankCost_; }
							else if (name.find("Pipe") != std::string::npos) { refundCost = pipeCost_; }
							else if (name.find("Canon") != std::string::npos || name.find("Cannon") != std::string::npos) { refundCost = canonCost_; }
							else if (name.find("Missile") != std::string::npos) { refundCost = missileCost_; }
							else if (name.find("Poison") != std::string::npos) { refundCost = poisonCost_; }
							else if (name.find("Ice") != std::string::npos) { refundCost = iceCanonCost_; }
							else { refundCost = 0; } // 未知のオブジェクト
						}


						if (refundCost > 0) {
							int getRefundAmount = CalculateRefund(refundCost);
							CoinCount += getRefundAmount;

							// 親オブジェクト等があれば再帰的に削除するか、単純にエンティティをデストロイする
							// GameObjectの削除
							if (registry.all_of<HierarchyComponent>(hoverEntity)) {
								auto root = hoverEntity;
								while(registry.get<HierarchyComponent>(root).parentId != entt::null) {
									root = registry.get<HierarchyComponent>(root).parentId;
								}
								scene->DestroyObject(static_cast<uint32_t>(root));
							} else {
								scene->DestroyObject(static_cast<uint32_t>(hoverEntity));
							}

							EditorUI::Log("Object sold for " + std::to_string(getRefundAmount));
						}
					}
				}
			}
		}

		if (keySpace) {
			RequestPhaseChange(BattlePhase);
			isPlacementMode_ = false;
			hasPipeStartPoint_ = false;
			skillTree_.Close(scene); // フェーズ移行時にスキルツリーを閉じる
		}

	} else if (isPhase_ == BattlePhase) {
		if (keyP) {
			RequestPhaseChange(PreparationPhase);
		}
		isPlacementMode_ = false;
		isSellMode_ = false;
		hasPipeStartPoint_ = false;
	} else {
		isPlacementMode_ = false;
		isSellMode_ = false;
		hasPipeStartPoint_ = false;
	}

	UpdatePhaseTransition();

	// フェーズが切り替わった瞬間の検知
	if (isPhase_ != preIsPhase_) {
		auto& nav = scene->GetNavigationManager();

		if (isPhase_ == BattlePhase) {
			// 準備から戦闘に切り替わった瞬間
			// 設置物を反映するためにコストマップを更新

			nav.UpdateCostMap(scene);

			// 敵が目指すコアをゴールの位置としてフローフィールドを計算
			auto core = scene->FindObjectByName("Core");
			if (scene->GetRegistry().valid(core)) {
				auto& tc = scene->GetRegistry().get<TransformComponent>(core);
				nav.GenerateFlowField(tc.translate.x, tc.translate.z);
			}

			// 敵のスポーン地点の生成
			currentPhase_++;
			WaveManagement::SetWave(currentPhase_ - 1);
		} else if (isPhase_ == PreparationPhase) {
			// 準備フェーズに戻った場合はウェーブを待機状態（スポナー無し）にする
			WaveManagement::SetWave(-1);
		}

		// 状態を同期
		preIsPhase_ = isPhase_;
	}

	if (scene->GetRegistry().all_of<UITextComponent>(entity))
		scene->GetRegistry().get<UITextComponent>(entity).text = "$"+ std::to_string(CoinCount);

	// ★ 敵の数UIの更新
	if (isPhase_ == BattlePhase) {
		auto waveManagerEntity = WaveManagement::GetManagerEntity();
		if (scene->GetRegistry().valid(waveManagerEntity)) {
			auto* sc = scene->GetRegistry().try_get<ScriptComponent>(waveManagerEntity);
			if (sc) {
				for (auto& entry : sc->scripts) {
					if (entry.scriptPath == "WaveManagement" && entry.instance) {
						auto* wm = static_cast<WaveManagement*>(entry.instance.get());
						int total = wm->GetTotalMaxEnemies(scene);
						int remaining = wm->GetTotalRemainingEnemies(scene);

						// UIエンティティの作成（まだなければ）
						if (enemyCountUI_ == entt::null || !scene->GetRegistry().valid(enemyCountUI_)) {
							enemyCountUI_ = scene->CreateEntity("EnemyCountUI");
							auto& rect = scene->GetRegistry().emplace<RectTransformComponent>(enemyCountUI_);
							rect.pos = { 0, -450 }; // 画面上部中央
							rect.anchor = { 0.5f, 0.5f }; // 中央基準で上へ
							rect.pivot = { 0.5f, 0.5f };

							auto& text = scene->GetRegistry().emplace<UITextComponent>(enemyCountUI_);
							text.fontSize = 64.0f;
							text.color = { 1, 1, 1, 1 };
							text.outlineEnabled = true;
						}

						auto& text = scene->GetRegistry().get<UITextComponent>(enemyCountUI_);
						text.text = std::to_string(remaining) + " / " + std::to_string(total);
						scene->GetRegistry().get<RectTransformComponent>(enemyCountUI_).enabled = true;
					}
				}
			}
		}
	} else {
		// 戦闘フェーズ以外では非表示
		if (enemyCountUI_ != entt::null && scene->GetRegistry().valid(enemyCountUI_)) {
			scene->GetRegistry().get<RectTransformComponent>(enemyCountUI_).enabled = false;
		}
	}

	// ★ 設置コストUIの更新
	if (isPlacementMode_) {
		if (installationCostUI_ == entt::null || !scene->GetRegistry().valid(installationCostUI_)) {
			installationCostUI_ = scene->CreateEntity("InstallationCostUI");
			auto& rect = scene->GetRegistry().emplace<RectTransformComponent>(installationCostUI_);
			rect.pos = { 0, -400 }; // 画面上部
			rect.anchor = { 0.5f, 0.5f };
			rect.pivot = { 0.5f, 0.5f };

			auto& text = scene->GetRegistry().emplace<UITextComponent>(installationCostUI_);
			text.fontSize = 48.0f;
			text.color = { 1, 1, 1, 1 };
			text.outlineEnabled = true;
		}

		auto& text = scene->GetRegistry().get<UITextComponent>(installationCostUI_);
		text.text = "Cost: " + std::to_string(currentInstallationCost_);
		// お金が足りない場合は赤色にする
		if (CoinCount < currentInstallationCost_) {
			text.color = { 1, 0, 0, 1 };
		}
		else {
			text.color = { 1, 1, 1, 1 };
		}
		scene->GetRegistry().get<RectTransformComponent>(installationCostUI_).enabled = true;
	}
	else {
		if (installationCostUI_ != entt::null && scene->GetRegistry().valid(installationCostUI_)) {
			scene->GetRegistry().get<RectTransformComponent>(installationCostUI_).enabled = false;
		}
	}

	preKeyN_ = keyN;
}

void PhaseSystemScript::RequestPhaseChange(PhaseState nextPhase) {
	if (isPhase_ == Transition || isPhaseTransitioning_)
		return;
	if (isPhase_ == nextPhase)
		return;

	NextPhase_ = nextPhase;
	isPhase_ = Transition;
	isPhaseTransitioning_ = true;
	isFadeFinished_ = false;

	if (PhaseTransition::IsAvailable()) {
		PhaseTransition::RequestFade();
	}
}

void PhaseSystemScript::UpdatePhaseTransition() {
	if (!isPhaseTransitioning_)
		return;

	if (PhaseTransition::IsAvailable()) {
		isFadeFinished_ = PhaseTransition::ConsumeSwitchPoint();
	} else {
		isFadeFinished_ = true;
	}

	if (isFadeFinished_) {
		isPhase_ = NextPhase_;
		isPhaseTransitioning_ = false;
		isFadeFinished_ = false;
	}
}

void PhaseSystemScript::Installation(GameScene* scene, const std::string& objPath) {
	if (!isPlacementMode_)
		return;

	auto* input = Engine::Input::GetInstance();
	if (!input)
		return;
	Engine::Vector3 hitPoint{};
	if (!TryGetTerrainHitPoint(scene, hitPoint)) {
		currentInstallationCost_ = 0;
		return;
	}

	Engine::Vector3 snappedHitPoint = hitPoint;
	snappedHitPoint.x = SnapTo2x2Grid(snappedHitPoint.x);
	snappedHitPoint.z = SnapTo2x2Grid(snappedHitPoint.z);

	if (isPipeSet_) {
		snappedHitPoint.x = SnapTo2x2Grid(snappedHitPoint.x);
		snappedHitPoint.z = SnapTo2x2Grid(snappedHitPoint.z);

		if (!hasPipeStartPoint_) {
			const bool canPlaceStart = (!IsPlacementBlocked(scene, snappedHitPoint) && (CoinCount >= selectedObjCost_));
			DrawPlacementPreview(scene, snappedHitPoint, objPath, canPlaceStart);

			if (input->IsMouseTrigger(0) && canPlaceStart) {
				pipeStartX_ = snappedHitPoint.x;
				pipeStartY_ = snappedHitPoint.y;
				pipeStartZ_ = snappedHitPoint.z;
				hasPipeStartPoint_ = true;
			}
			currentInstallationCost_ = selectedObjCost_;
			return;
		}

		Engine::Vector3 startPoint{pipeStartX_, pipeStartY_, pipeStartZ_};
		auto pathPoints = BuildPipePathPoints(startPoint, snappedHitPoint);
     for (auto& p : pathPoints) {
			float surfaceY = p.y;
			if (TryGetPlacementSurfaceYAt(scene, p.x, p.z, surfaceY)) {
				p.y = surfaceY;
			}
		}
		bool canPlaceAll = !pathPoints.empty();
		if (CoinCount < pathPoints.size() * selectedObjCost_) {
			canPlaceAll = false;
		}

		for (const auto& p : pathPoints) {
			bool canPlacePoint = !IsPlacementBlocked(scene, p);
			if (CoinCount < pathPoints.size() * selectedObjCost_) {
				canPlacePoint = false; // お金が足りない場合は赤く（配置不可として）描画
			}
			DrawPlacementPreview(scene, p, objPath, canPlacePoint);
			if (!canPlacePoint && CoinCount >= pathPoints.size() * selectedObjCost_) {
				canPlaceAll = false;
			}
		}
		currentInstallationCost_ = static_cast<int>(pathPoints.size()) * selectedObjCost_;

		// パイプ間の接続ラインを描画
		auto* pipeRenderer = scene->GetRenderer();
		if (pipeRenderer && pathPoints.size() >= 2) {
			for (size_t i = 0; i + 1 < pathPoints.size(); ++i) {
				Engine::Vector3 p1 = {pathPoints[i].x, pathPoints[i].y + 0.5f, pathPoints[i].z};
				Engine::Vector3 p2 = {pathPoints[i+1].x, pathPoints[i+1].y + 0.5f, pathPoints[i+1].z};
				Engine::Vector4 lineColor = canPlaceAll ? Engine::Vector4{0.6f, 1.0f, 0.6f, 1.0f} : Engine::Vector4{1.0f, 0.3f, 0.3f, 1.0f};
				pipeRenderer->DrawLine3D(p1, p2, lineColor, true);
			}
		}

		if (input->IsMouseTrigger(0)) {
			if (canPlaceAll) {
				if (CoinCount >= pathPoints.size() * selectedObjCost_) {
					CoinCount -= static_cast<int>(pathPoints.size()) * selectedObjCost_;
					for (const auto& p : pathPoints) {
						SpawnPlacedObject(scene, p, objPath);
					}
					isPlacementMode_ = false;
					isPipeSet_ = false;
				}
			}
			hasPipeStartPoint_ = false;
		}
		return;
	}

	const bool canPlace = (!IsPlacementBlocked(scene, snappedHitPoint) && (CoinCount >= selectedObjCost_));

	DrawPlacementPreview(scene, snappedHitPoint, objPath, canPlace);

	if (input->IsMouseTrigger(0) && canPlace) {
		SpawnPlacedObject(scene, snappedHitPoint, objPath);
		CoinCount -= selectedObjCost_;
		isPlacementMode_ = false;
	}
	currentInstallationCost_ = selectedObjCost_;
}

bool PhaseSystemScript::TryGetTerrainHitPoint(GameScene* scene, Engine::Vector3& outHitPoint) const {
	float localX = 0, localY = 0;
	float tW = 0, tH = 0;

#if defined(USE_IMGUI) && !defined(NDEBUG)
	ImVec2 mousePos = ImGui::GetMousePos();
	ImVec2 gameMin = EditorUI::GetGameImageMin();
	ImVec2 gameMax = EditorUI::GetGameImageMax();
	tW = gameMax.x - gameMin.x;
	tH = gameMax.y - gameMin.y;
	if (tW <= 0.0f || tH <= 0.0f)
		return false;

	localX = mousePos.x - gameMin.x;
	localY = mousePos.y - gameMin.y;
	bool insideImage = (localX >= 0.0f && localY >= 0.0f && localX <= tW && localY <= tH);
	if (!insideImage)
		return false;
#else
	auto* input = Engine::Input::GetInstance();
	if (!input)
		return false;
	input->GetMousePos(localX, localY);
	tW = (float)Engine::WindowDX::kW;
	tH = (float)Engine::WindowDX::kH;
#endif

	auto& camera = scene->GetCamera();
	DirectX::XMMATRIX view = camera.View();
	DirectX::XMMATRIX proj = camera.Proj();

	DirectX::XMVECTOR rayOrig, rayDir;
	EditorUI::ScreenToWorldRay(localX, localY, tW, tH, view, proj, rayOrig, rayDir);

	auto* renderer = scene->GetRenderer();
	if (!renderer)
		return false;

	float bestDist = FLT_MAX;
	bool hitTerrain = false;

	auto& registry = scene->GetRegistry();
	registry.view<NameComponent, TransformComponent>().each([&](entt::entity entity, const NameComponent& nc, const TransformComponent& tc) {
     const bool isWallTag = registry.all_of<TagComponent>(entity) && registry.get<TagComponent>(entity).tag == TagType::Wall;
		bool isTerrain = (nc.name.find("Terrain") != std::string::npos) || (nc.name.find("Floor") != std::string::npos) || (nc.name.find("Ground") != std::string::npos) ||
		                 (nc.name.find("Stage") != std::string::npos) || (nc.name.find("Plane") != std::string::npos);
     if (!isTerrain && !isWallTag)
			return;

		Engine::Model* model = nullptr;
		// GpuMeshCollider か MeshRenderer からモデルを取得
		if (registry.all_of<GpuMeshColliderComponent>(entity)) {
			auto& gmc = registry.get<GpuMeshColliderComponent>(entity);
			if (gmc.meshHandle != 0) {
				model = renderer->GetModel(gmc.meshHandle);
			}
		}

		if (!model && registry.all_of<MeshRendererComponent>(entity)) {
			auto& mr = registry.get<MeshRendererComponent>(entity);
			if (mr.modelHandle != 0) {
				model = renderer->GetModel(mr.modelHandle);
			}
		}

		if (!model)
			return;

		float d;
		Engine::Vector3 hp;
		if (model->RayCast(rayOrig, rayDir, tc.ToMatrix(), d, hp) && d < bestDist) {
			bestDist = d;
			outHitPoint = hp;
			hitTerrain = true;
		}
	});

	// --- フォールバック: 仮想的な y=0 平面との交差判定 ---
	if (!hitTerrain) {
		DirectX::XMFLOAT3 orig, dir;
		DirectX::XMStoreFloat3(&orig, rayOrig);
		DirectX::XMStoreFloat3(&dir, rayDir);

		if (std::abs(dir.y) > 0.0001f) {
			float t = -orig.y / dir.y;
			if (t > 0) {
				outHitPoint = {orig.x + dir.x * t, 0.0f, orig.z + dir.z * t};
				hitTerrain = true;
			}
		}
	}

	return hitTerrain;
}

void PhaseSystemScript::DrawPlacementPreview(GameScene* scene, const Engine::Vector3& hitPoint, const std::string& objPath, bool canPlace) {
	auto* renderer = scene->GetRenderer();
	if (!renderer)
		return;

	std::string previewModelP = objPath; // Changed name to avoid shadowing class member
	std::string previewTexturePath = "Resources/Textures/white1x1.png";
	if (IsPrefabPath(objPath)) {
		ExtractPrefabRenderPaths(objPath, previewModelP, previewTexturePath);
	}

	if (previewModelHandle_ == 0 || previewObjPath_ != previewModelP) {
		previewModelHandle_ = renderer->LoadObjMesh(previewModelP);
		previewObjPath_ = previewModelP;
		previewTextureHandle_ = 0;
	}
	if (previewTextureHandle_ == 0) {
		previewTextureHandle_ = renderer->LoadTexture2D(previewTexturePath);
	}

	Engine::Transform tr;
	tr.translate = {hitPoint.x, hitPoint.y + 0.5f, hitPoint.z};
	tr.scale = {1.0f, 1.0f, 1.0f};
	const Engine::Vector4 previewColor = canPlace ? Engine::Vector4{0.6f, 1.0f, 0.6f, 0.6f} : Engine::Vector4{1.0f, 0.3f, 0.3f, 0.6f};
	renderer->DrawMesh(previewModelHandle_, previewTextureHandle_, tr, previewColor, "Toon");

	// パイプ設置時のみ、既存のタンク・大砲・ミサイル・ポイズンの接続エリア（緑の平面十字）を描画する
	if (objPath.find("Pipe") != std::string::npos) {
		static uint32_t crossPlaneHandle = 0;
		if (crossPlaneHandle == 0) {
			crossPlaneHandle = renderer->LoadObjMesh("Resources/Models/cube/cube.obj");
		}
		auto& registry = scene->GetRegistry();
		registry.view<NameComponent, TransformComponent>().each([&](entt::entity, const NameComponent& nc, const TransformComponent& tc) {
			if (nc.name.find("Canon") != std::string::npos || nc.name.find("Cannon") != std::string::npos || nc.name.find("Tank") != std::string::npos || nc.name.find("Missile") != std::string::npos || nc.name.find("Poison") != std::string::npos) {
				Engine::Transform planeTr;
				planeTr.scale = {1.0f, 0.05f, 1.0f};
				Engine::Vector4 colorPlane = {0.0f, 1.0f, 0.0f, 0.4f};

				// X+ direction
				planeTr.translate = {tc.translate.x + 2.0f, tc.translate.y + 0.05f, tc.translate.z};
				renderer->DrawMesh(crossPlaneHandle, previewTextureHandle_, planeTr, colorPlane, "Toon");
				// X- direction
				planeTr.translate = {tc.translate.x - 2.0f, tc.translate.y + 0.05f, tc.translate.z};
				renderer->DrawMesh(crossPlaneHandle, previewTextureHandle_, planeTr, colorPlane, "Toon");
				// Z+ direction
				planeTr.translate = {tc.translate.x, tc.translate.y + 0.05f, tc.translate.z + 2.0f};
				renderer->DrawMesh(crossPlaneHandle, previewTextureHandle_, planeTr, colorPlane, "Toon");
				// Z- direction
				planeTr.translate = {tc.translate.x, tc.translate.y + 0.05f, tc.translate.z - 2.0f};
				renderer->DrawMesh(crossPlaneHandle, previewTextureHandle_, planeTr, colorPlane, "Toon");
			}
		});
	}

	// 大砲の場合は攻撃範囲も描画する
	if (objPath.find("Canon") != std::string::npos) {
		float attackRange = 50.0f;
		for (int i = 0; i < 72; ++i) {
			float theta1 = (i * 2.0f * 3.1415926f) / 72.0f;
			float theta2 = ((i + 1) * 2.0f * 3.1415926f) / 72.0f;
			Engine::Vector3 p1 = {hitPoint.x + std::cos(theta1) * attackRange, hitPoint.y + 0.05f, hitPoint.z + std::sin(theta1) * attackRange};
			Engine::Vector3 p2 = {hitPoint.x + std::cos(theta2) * attackRange, hitPoint.y + 0.05f, hitPoint.z + std::sin(theta2) * attackRange};
			renderer->DrawLine3D(p1, p2, {0.0f, 0.8f, 0.0f, 1.0f}, true); // やや暗めの緑などに
		}
	}
}

bool PhaseSystemScript::IsPrefabPath(const std::string& path) const {
	if (path.size() < 7)
		return false;
	return path.compare(path.size() - 7, 7, ".prefab") == 0;
}

bool PhaseSystemScript::ExtractPrefabRenderPaths(const std::string& prefabPath, std::string& outModelPath, std::string& outTexturePath) const {
	static std::unordered_map<std::string, std::pair<std::string, std::string>> cache;
	if (cache.find(prefabPath) != cache.end()) {
		outModelPath = cache[prefabPath].first;
		outTexturePath = cache[prefabPath].second;
		return true;
	}

	std::string absPath = EditorUI::GetUnifiedProjectPath(prefabPath);
	// ★修正: UTF-8パスをFromUTF8経由でワイドパスに変換してオープン
	std::ifstream f(Engine::PathUtils::FromUTF8(absPath));
	if (!f.is_open()) {
		f.open(Engine::PathUtils::FromUTF8(prefabPath));
		if (!f.is_open())
			return false;
	}

	std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
	f.close();

	auto extractValue = [&](const char* key, std::string& outValue) {
		size_t keyPos = content.find(key);
		if (keyPos == std::string::npos)
			return;
		size_t colonPos = content.find(':', keyPos);
		if (colonPos == std::string::npos)
			return;
		size_t firstQuote = content.find('"', colonPos);
		if (firstQuote == std::string::npos)
			return;
		size_t secondQuote = content.find('"', firstQuote + 1);
		if (secondQuote == std::string::npos)
			return;
		outValue = content.substr(firstQuote + 1, secondQuote - firstQuote - 1);
	};

	extractValue("\"modelPath\"", outModelPath);
	extractValue("\"texturePath\"", outTexturePath);

	if (!outModelPath.empty()) {
		cache[prefabPath] = {outModelPath, outTexturePath};
		return true;
	}
	return false;
}

bool PhaseSystemScript::IsPlacementBlocked(GameScene* scene, const Engine::Vector3& hitPoint) const {
	constexpr float kBlockHalfExtent = 2.0f; // 2x2 square

	auto& registry = scene->GetRegistry();
	auto view = registry.view<TransformComponent>();
	for (auto entity : view) {
		// MeshRenderer, BoxCollider, GpuMeshCollider のいずれも持たないエンティティ（不可視のシステムオブジェクトなど）は無視する
		if (!registry.any_of<MeshRendererComponent, BoxColliderComponent, GpuMeshColliderComponent>(entity)) {
			continue;
		}

		if (registry.all_of<NameComponent>(entity)) {
			const auto& nc = registry.get<NameComponent>(entity);
			const bool isTerrain = (nc.name.find("Terrain") != std::string::npos) || (nc.name.find("Floor") != std::string::npos) || (nc.name.find("Ground") != std::string::npos) ||
			                       (nc.name.find("Stage") != std::string::npos) || (nc.name.find("Plane") != std::string::npos);
          if (isTerrain)
				continue;
		}

		if (registry.all_of<TagComponent>(entity) && registry.get<TagComponent>(entity).tag == TagType::Wall) {
			continue;
		}

		const auto& tc = view.get<TransformComponent>(entity);
		const float dx = tc.translate.x - hitPoint.x;
		const float dz = tc.translate.z - hitPoint.z;
		if (std::abs(dx) < kBlockHalfExtent && std::abs(dz) < kBlockHalfExtent) {
			return true;
		}
	}

	return false;
}

void PhaseSystemScript::SpawnPlacedObject(GameScene* scene, const Engine::Vector3& hitPoint, const std::string& objPath) {
	auto* renderer = scene->GetRenderer();
	if (!renderer)
		return;

	auto& registry = scene->GetRegistry();

	if (IsPrefabPath(objPath)) {
		EditorUI::Log("Spawning prefab: " + objPath);
		std::vector<entt::entity> createdEntities = EditorUI::LoadPrefab(scene, objPath);

		if (createdEntities.empty()) {
			EditorUI::LogError("SpawnPlacedObject: LoadPrefab returned 0 entities for " + objPath);
			return;
		}

		// 新しく追加されたエンティティの座標をセット
		int movedCount = 0;
		for (auto entity : createdEntities) {
			if (registry.all_of<TransformComponent>(entity)) {
				auto& tc = registry.get<TransformComponent>(entity);
				// 親がいない（ルート）のエンティティのみ座標を更新
				if (!registry.all_of<HierarchyComponent>(entity) || registry.get<HierarchyComponent>(entity).parentId == entt::null) {
					tc.translate = {hitPoint.x, hitPoint.y + 0.5f, hitPoint.z};
					movedCount++;
				}
			}
		}
		EditorUI::Log("Prefab spawned and positioned. Root entities moved: " + std::to_string(movedCount));
		return;
	}

	if (previewModelHandle_ == 0 || previewObjPath_ != objPath) {
		previewModelHandle_ = renderer->LoadObjMesh(objPath);
		previewObjPath_ = objPath;
	}
	if (previewTextureHandle_ == 0) {
		previewTextureHandle_ = renderer->LoadTexture2D("Resources/Textures/white1x1.png");
	}

	entt::entity newEntity = scene->CreateEntity((objPath.find("cylinder") != std::string::npos || objPath.find("Cylinder") != std::string::npos) ? "PlacedCylinder" : "PlacedCube");

	auto& tc = registry.get<TransformComponent>(newEntity);
	tc.translate = {hitPoint.x, hitPoint.y + 0.5f, hitPoint.z};
	tc.scale = {1.0f, 1.0f, 1.0f};

	auto& mr = registry.emplace<MeshRendererComponent>(newEntity);
	mr.modelHandle = previewModelHandle_;
	mr.textureHandle = previewTextureHandle_;
	mr.modelPath = objPath;
	mr.texturePath = "Resources/Textures/white1x1.png";
	mr.shaderName = "Toon";
}

void PhaseSystemScript::Draw(entt::entity entity, GameScene* scene) {
	(void)entity;
	(void)scene;
	// 既に Update のフェーズで skillTree_.Update() が呼ばれ、描画コマンドも積まれているため
	// ここで再度呼ぶと入力処理が 1フレームで2回走ってしまい、ページが2重にめくられる原因になる。
}

void PhaseSystemScript::OnEditorUI() {
#ifdef USE_IMGUI
	if (ImGui::Button(skillTree_.IsOpen() ? "Close SkillTree Preview" : "Open SkillTree Preview")) {
		// EditorUIからの呼び出しはsceneが不明なためnullを渡す
		skillTree_.Toggle(nullptr);
	}
#endif
}

void PhaseSystemScript::OnDestroy(entt::entity /*entity*/, GameScene* /*scene*/) {}

REGISTER_SCRIPT(PhaseSystemScript);

} // namespace Game
