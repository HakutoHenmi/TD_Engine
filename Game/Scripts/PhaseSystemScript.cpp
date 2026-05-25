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
#include "ResultManagerScript.h"
#include "../../Engine/SceneManager.h"
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

// 高さキャッシュ用（配置フェーズ中は地形が頻繁に変わらないためキャッシュして毎フレームのRayCastを省く）
static std::unordered_map<int64_t, float> s_heightCache;
static void ClearHeightCache() { s_heightCache.clear(); }

bool TryGetPlacementSurfaceYAt(GameScene* scene, float x, float z, float& outY) {
	if (!scene)
		return false;

	// グリッド座標にスナップしてキーを作成（小数点以下2桁程度で丸める）
	int64_t ix = static_cast<int64_t>(std::round(x * 100.0f));
	int64_t iz = static_cast<int64_t>(std::round(z * 100.0f));
	int64_t key = (ix << 32) | (iz & 0xFFFFFFFF);

	auto it = s_heightCache.find(key);
	if (it != s_heightCache.end()) {
		outY = it->second;
		return true;
	}

	float y = scene->GetHeightAt(x, z, 1000.0f);
	if (y > -9999.0f) {
		outY = y;
		s_heightCache[key] = y;
		return true;
	}
	
	outY = 0.0f;
	return false;
}
} // namespace

void PhaseSystemScript::Start(entt::entity entity, GameScene* scene) {
	(void)entity;
	
	// チュートリアルシーン以外ならインサートカメラ演出から開始
	bool isTutorial = false;
	if (scene) {
		const auto& path = scene->GetStagePath();
		if (path.find("Tutorial") != std::string::npos || path.find("tutorial") != std::string::npos) {
			isTutorial = true;
		}
	}

	if (!isTutorial) {
		isPhase_ = InsertPhase;
		NextPhase_ = InsertPhase;
		preIsPhase_ = InsertPhase;
	} else {
		isPhase_ = PreparationPhase;
		NextPhase_ = PreparationPhase;
		preIsPhase_ = PreparationPhase;
	}

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
	
	// インサートカメラ変数の初期化
	isInsertInitialized_ = false;
	currentWaypointIndex_ = 0;
	waypointTime_ = 0.0f;
	skipHoldTime_ = 0.0f;
	skipPromptUI_ = entt::null;
	skipProgressUI_ = entt::null;

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
	
	if (isPhase_ == InsertPhase) {
		UpdateInsertPhase(scene, dt);
		return;
	}

	auto* input = Engine::Input::GetInstance();
	if (!input)
		return;

	// スクリプト動作確認用の白い線 (常に表示)
	auto* renderer = scene->GetRenderer();
	if (renderer) {
	// デバッグ用の線は削除しました
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
			isSellMode_ = false;
			hasPipeStartPoint_ = false;
			placementSelectionChangedThisFrame = true;
		}

		if (key2 || InstallationManager::IsButtonPressed("Resources/Prefabs/Pipe.prefab")) {
			selectedObjPath_ = "Resources/Prefabs/Pipe.prefab";
			selectedObjCost_ = InstallationManager::GetCost(selectedObjPath_);
			if (selectedObjCost_ == 0) selectedObjCost_ = pipeCost_;
			isPipeSet_ = true;
			isPlacementMode_ = true;
			isSellMode_ = false;
			hasPipeStartPoint_ = false;
			placementSelectionChangedThisFrame = true;
		}

		if (key3 || InstallationManager::IsButtonPressed("Resources/Prefabs/Canon.prefab")) {
			selectedObjPath_ = "Resources/Prefabs/NewCannon.prefab";
			selectedObjCost_ = InstallationManager::GetCost(selectedObjPath_);
			if (selectedObjCost_ == 0) selectedObjCost_ = canonCost_;
			isPlacementMode_ = true;
			isPipeSet_ = false;
			isSellMode_ = false;
			hasPipeStartPoint_ = false;
			placementSelectionChangedThisFrame = true;
		}

		if (key4 || InstallationManager::IsButtonPressed("Resources/Prefabs/Missile.prefab")) {
			selectedObjPath_ = "Resources/Prefabs/Missile.prefab";
			selectedObjCost_ = InstallationManager::GetCost(selectedObjPath_);
			if (selectedObjCost_ == 0) selectedObjCost_ = missileCost_;
			isPlacementMode_ = true;
			isPipeSet_ = false;
			isSellMode_ = false;
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

		// Xキーまたは「削除機能ボタン」クリックで削除(売却)モードへの切り替え
		if (keyX || InstallationManager::IsButtonPressedByName("DeleteButton")) {
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

			// ★修正: preIsPhase_が準備フェーズから来たかチェックして、初めてのみ++する
			if (preIsPhase_ == PreparationPhase) {
				// 準備→戦闘への遷移（新しいウェーブ開始）
				currentPhase_++;
			}
			
			WaveManagement::SetWave(currentPhase_);  // ★修正: currentPhase_-1ではなく、currentPhase_を使用

			// 戦闘中は絵画風エフェクトをオンにする	
			Engine::Renderer::GetInstance()->SetPostEffect("Painterly");

		} else if (isPhase_ == PreparationPhase) {
			// 準備フェーズに戻った場合はウェーブを待機状態（スポナー無し）にする
			WaveManagement::SetWave(-1);

			// 準備フェーズも絵画風にする（DOFピンボケ付き）
			Engine::Renderer::GetInstance()->SetPostEffect("Painterly");

			// ★ カメラをコアの上に戻して見下ろす視点にする
			auto core = scene->FindObjectByName("Core");
			auto prepCam = scene->FindObjectByName("PreparationCamera");
			if (scene->GetRegistry().valid(core) && scene->GetRegistry().valid(prepCam)) {
				// コアの座標を取得
				auto& coreTc = scene->GetRegistry().get<TransformComponent>(core);
				
				// PreparationCameraの座標をコアに移動
				if (scene->GetRegistry().all_of<TransformComponent>(prepCam)) {
					auto& camTc = scene->GetRegistry().get<TransformComponent>(prepCam);
					camTc.translate = coreTc.translate; // コアと同じ位置へ（CameraFollowSystemがここをターゲットにする）
				}
				
				// 視点の角度（見下ろし）をリセット
				if (scene->GetRegistry().all_of<PlayerInputComponent>(prepCam)) {
					auto& camPi = scene->GetRegistry().get<PlayerInputComponent>(prepCam);
					camPi.cameraPitch = 0.5f; // 下を向く（X軸回転）
					camPi.cameraYaw = 0.0f;
					
					// 直接カメラの回転も変更して即座に反映
					auto& camera = scene->GetCamera();
					auto rot = camera.Rotation();
					rot.x = 0.5f;
					rot.y = 0.0f;
					camera.SetRotation(rot);
				}
				
				// ズーム距離の初期化
				if (scene->GetRegistry().all_of<CameraTargetComponent>(prepCam)) {
					auto& ct = scene->GetRegistry().get<CameraTargetComponent>(prepCam);
					ct.distance = 25.0f; // 少し引いた視点
					ct.height = 10.0f;   // 少し高めの視点
				}
			}
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

	// ★ ゲームオーバー / リザルトへの遷移チェック
	if (isPhase_ == BattlePhase && scene->IsPlaying()) {
		// コア破壊判定
		bool isCoreDead = false;
		
		// 1. 名前で検索
		auto coreByName = scene->FindObjectByName("Core");
		if (scene->GetRegistry().valid(coreByName)) {
			if (auto* hc = scene->GetRegistry().try_get<HealthComponent>(coreByName)) {
				if (hc->hp <= 0.0f || hc->isDead) {
					isCoreDead = true;
				}
			}
		}

		// 2. タグで検索 (保険)
		if (!isCoreDead) {
			const auto& cores = scene->GetEntitiesByTag(TagType::Core);
			for (auto c : cores) {
				if (scene->GetRegistry().valid(c)) {
					if (auto* hc = scene->GetRegistry().try_get<HealthComponent>(c)) {
						if (hc->hp <= 0.0f || hc->isDead) {
							isCoreDead = true;
							break;
						}
					}
				}
			}
		}

		if (isCoreDead) {
			ResultManagerScript::pendingIsWin = false;
			ResultManagerScript::pendingOriginalScene = scene->GetStagePath(); 
			Engine::SceneParameters p;
			p.sceneName = "Result";
			p.isWin = false;
			p.score = 300;
			p.clearTime = scene->GetPlayTime();
			Engine::SceneManager::GetInstance()->RequestChange("Result", p);
			
			isPhaseTransitioning_ = true;
			isPhase_ = Transition;
		} else if (WaveManagement::IsWaveEnded()) {
			ResultManagerScript::pendingIsWin = true;
			ResultManagerScript::pendingOriginalScene = scene->GetStagePath();
			Engine::SceneParameters p;
			p.sceneName = "Result";
			p.isWin = true;
			p.score = 1500;
			p.clearTime = scene->GetPlayTime();
			Engine::SceneManager::GetInstance()->RequestChange("Result", p);
			
			isPhaseTransitioning_ = true;
			isPhase_ = Transition;
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

	float surfaceY = 0.0f;
	if (TryGetPlacementSurfaceYAt(scene, snappedHitPoint.x, snappedHitPoint.z, surfaceY)) {
		snappedHitPoint.y = surfaceY;
	}

	if (isPipeSet_) {
		if (!hasPipeStartPoint_) {
			bool isBlockedByOtherThanPipe = false;
			constexpr float kBlockHalfExtent = 2.0f;
			auto& registry = scene->GetRegistry();
			for (auto entity : registry.view<TransformComponent>()) {
				if (!registry.any_of<MeshRendererComponent, BoxColliderComponent, GpuMeshColliderComponent>(entity)) continue;
				if (registry.all_of<TagComponent>(entity) && registry.get<TagComponent>(entity).tag == TagType::Pipe) continue;
				if (registry.all_of<NameComponent>(entity)) {
					const auto& nc = registry.get<NameComponent>(entity);
					if ((nc.name.find("Terrain") != std::string::npos) || (nc.name.find("Floor") != std::string::npos) || (nc.name.find("Ground") != std::string::npos) ||
						(nc.name.find("Stage") != std::string::npos) || (nc.name.find("Plane") != std::string::npos)) continue;
				}
				if (registry.all_of<TagComponent>(entity) && registry.get<TagComponent>(entity).tag == TagType::Wall) continue;

				const auto& tc = registry.get<TransformComponent>(entity);
				const float dx = tc.translate.x - snappedHitPoint.x;
				const float dz = tc.translate.z - snappedHitPoint.z;
				if (std::abs(dx) < kBlockHalfExtent && std::abs(dz) < kBlockHalfExtent) {
				 isBlockedByOtherThanPipe = true;
				 break;
				}
			}
			const bool canPlaceStart = (!isBlockedByOtherThanPipe && (CoinCount >= selectedObjCost_));
			DrawPlacementPreview(scene, snappedHitPoint, objPath, canPlaceStart);

			if (input->IsMouseTrigger(0) && canPlaceStart) {
				pipeStartX_ = snappedHitPoint.x;
				pipeStartY_ = snappedHitPoint.y;
				pipeStartZ_ = snappedHitPoint.z;
				hasPipeStartPoint_ = true;
				SpawnPlacedObject(scene, snappedHitPoint, objPath);
				CoinCount -= selectedObjCost_;
			}
			currentInstallationCost_ = selectedObjCost_;
			return;
		}

		// 始点が決まっている状態（ドラッグ中、または2クリック目の移動中）
		Engine::Vector3 startPoint{pipeStartX_, pipeStartY_, pipeStartZ_};
		auto pathPoints = BuildPipePathPoints(startPoint, snappedHitPoint);
		std::vector<Engine::Vector3> validPreviewPoints;
		auto& registry = scene->GetRegistry();

		// ★最適化: 文字列検索を完全に排除し、TagComponentを利用して高速に障害物を抽出する
		std::vector<Engine::Vector3> obstaclePositions;
		obstaclePositions.reserve(128);
		for (auto entity : registry.view<TransformComponent, TagComponent>()) {
			auto tag = registry.get<TagComponent>(entity).tag;
			
			// 障害物とみなさないタグはスキップ
			// 壁、パイプ、プレイヤー、敵、弾、エフェクトなどは障害物としない
			if (tag == TagType::Wall || tag == TagType::Pipe || tag == TagType::Player || 
				tag == TagType::Enemy || tag == TagType::Bullet || tag == TagType::VFX || 
				tag == TagType::HitDistortion_VFX || tag == TagType::Experience || 
				tag == TagType::ExperienceOrb || tag == TagType::Untagged) {
				continue;
			}

			// それ以外のタグ（Canon, BulletTank, Missile, Poisonなど）は障害物として登録
			const auto& t = registry.get<TransformComponent>(entity).translate;
			obstaclePositions.push_back({t.x, t.y, t.z});
		}

		for (size_t i = 1; i < pathPoints.size(); ++i) {
			auto p = pathPoints[i];
			float sY = p.y;
			if (TryGetPlacementSurfaceYAt(scene, p.x, p.z, sY)) {
				p.y = sY;
			}
			bool isBlocked = false;
			constexpr float kBlockHalfExtent = 1.0f;

			for (const auto& obsPos : obstaclePositions) {
				const float dx = obsPos.x - p.x;
				const float dz = obsPos.z - p.z;
				if (std::abs(dx) < kBlockHalfExtent && std::abs(dz) < kBlockHalfExtent) {
					isBlocked = true;
					break;
				}
			}

			if (isBlocked) {
				break; // 障害物にぶつかったら以降は繋がない
			}
			validPreviewPoints.push_back(p);
		}

		// 配置するパイプのうち、すでに存在しないものの数をカウントしてコスト計算
		// ★最適化: unordered_setを使ってO(1)で超高速検索できるようにする
		std::unordered_set<int64_t> existingPipeGrid;
		existingPipeGrid.reserve(1024);
		for (auto entity : registry.view<TransformComponent, TagComponent>()) {
			if (registry.get<TagComponent>(entity).tag == TagType::Pipe) {
				const auto& t = registry.get<TransformComponent>(entity).translate;
				int64_t ix = static_cast<int64_t>(std::round(t.x));
				int64_t iz = static_cast<int64_t>(std::round(t.z));
				existingPipeGrid.insert((ix << 32) | (iz & 0xFFFFFFFF));
			}
		}

		int actualNewPipes = 0;
		for (const auto& p : validPreviewPoints) {
			int64_t px = static_cast<int64_t>(std::round(p.x));
			int64_t pz = static_cast<int64_t>(std::round(p.z));
			bool alreadyHasPipe = (existingPipeGrid.count((px << 32) | (pz & 0xFFFFFFFF)) > 0);
			if (!alreadyHasPipe) {
				actualNewPipes++;
			}
		}

		int requiredCost = actualNewPipes * selectedObjCost_;
		bool canPlaceAll = (CoinCount >= requiredCost);
		currentInstallationCost_ = requiredCost;

		// プレビュー描画（大量プレビュー時は重いグリッドや十字ハイライトを最後の1回だけ描画する）
		// ★最適化: 600個などの大量プレビュー時、半透明メッシュが600個重なるとGPUのオーバードローで激重になるため描画を間引く
		size_t maxPreviewMeshes = 50;
		size_t step = 1;
		if (validPreviewPoints.size() > maxPreviewMeshes) {
			step = validPreviewPoints.size() / maxPreviewMeshes;
		}

		for (size_t i = 0; i < validPreviewPoints.size(); ++i) {
			bool drawExtras = (i == validPreviewPoints.size() - 1);
			
			// 始点、終点、および間引かれた間隔のポイントのみメッシュを描画
			if (i == 0 || drawExtras || (i % step == 0)) {
				DrawPlacementPreview(scene, validPreviewPoints[i], objPath, canPlaceAll, drawExtras);
			}
		}
		
		// 接続ラインの描画
		auto* pipeRenderer = scene->GetRenderer();
		if (pipeRenderer && !validPreviewPoints.empty()) {
			Engine::Vector3 pPrev = {startPoint.x, startPoint.y + 0.5f, startPoint.z};
			for (size_t i = 0; i < validPreviewPoints.size(); ++i) {
				Engine::Vector3 pCurr = {validPreviewPoints[i].x, validPreviewPoints[i].y + 0.5f, validPreviewPoints[i].z};
				Engine::Vector4 lineColor = canPlaceAll ? Engine::Vector4{0.6f, 1.0f, 0.6f, 1.0f} : Engine::Vector4{1.0f, 0.3f, 0.3f, 1.0f};
				pipeRenderer->DrawLine3D(pPrev, pCurr, lineColor, true);
				pPrev = pCurr;
			}
		}

		// 設置処理
		if (input->IsMouseDown(0)) {
			if (canPlaceAll && !validPreviewPoints.empty()) {
				CoinCount -= requiredCost;
				for (const auto& p : validPreviewPoints) {
					int64_t px = static_cast<int64_t>(std::round(p.x));
					int64_t pz = static_cast<int64_t>(std::round(p.z));
					bool alreadyHasPipe = (existingPipeGrid.count((px << 32) | (pz & 0xFFFFFFFF)) > 0);
					if (!alreadyHasPipe) {
						SpawnPlacedObject(scene, p, objPath);
					}
				}
				// ドラッグ中は常に最新の末端を次の始点にする
				pipeStartX_ = validPreviewPoints.back().x;
				pipeStartY_ = validPreviewPoints.back().y;
				pipeStartZ_ = validPreviewPoints.back().z;
			}
			
			// もし「クリック」によって設置したなら、そこで一区切りつける（2クリック操作への対応）
			if (input->IsMouseTrigger(0)) {
				hasPipeStartPoint_ = false;
			}
		}
		
		return;
	}

	const bool canPlace = (!IsPlacementBlocked(scene, snappedHitPoint) && (CoinCount >= selectedObjCost_));

	DrawPlacementPreview(scene, snappedHitPoint, objPath, canPlace);

	if (input->IsMouseDown(0) && canPlace) {
		SpawnPlacedObject(scene, snappedHitPoint, objPath);
		CoinCount -= selectedObjCost_;
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

	DirectX::XMFLOAT3 orig, dir;
	DirectX::XMStoreFloat3(&orig, rayOrig);
	DirectX::XMStoreFloat3(&dir, rayDir);

	// ★ Y=0 の仮想平面との交差判定（地形の凹凸や壁を無視して確実にマスを取得）
	if (std::abs(dir.y) > 0.0001f) {
		float t = -orig.y / dir.y;
		if (t > 0) {
			outHitPoint = {orig.x + dir.x * t, 0.0f, orig.z + dir.z * t};
			return true;
		}
	}

	return false;
}

void PhaseSystemScript::DrawPlacementPreview(GameScene* scene, const Engine::Vector3& hitPoint, const std::string& objPath, bool canPlace, bool drawExtras) {
	auto* renderer = scene->GetRenderer();
	if (!renderer)
		return;

	// ★ 床のマス目ハイライトとグリッドの描画
	float hs = 1.0f; // 2x2マスなので半径1.0f
	
	// ハイライト用の半透明パネル（メッシュ）を描画
	static uint32_t highlightPlaneHandle = 0;
	if (highlightPlaneHandle == 0) {
		highlightPlaneHandle = renderer->LoadObjMesh("Resources/Models/cube/cube.obj");
	}
	Engine::Transform highlightTr;
	highlightTr.translate = {hitPoint.x, hitPoint.y + 0.025f, hitPoint.z};
	highlightTr.scale = {1.0f, 0.01f, 1.0f}; // 2x2 flat plane
	Engine::Vector4 planeColor = canPlace ? Engine::Vector4{0.0f, 1.0f, 0.0f, 0.4f} : Engine::Vector4{1.0f, 0.0f, 0.0f, 0.4f};
	
	// ダミーのテクスチャハンドルがあればそれを使う（なければプレビューの使い回しでもOKですが、まだロードされてないので白テクスチャを後で使う）

	// 外枠の線を少し太く（多重に）描画して強調
	Engine::Vector4 highlightLineColor = canPlace ? Engine::Vector4{0.0f, 1.0f, 0.0f, 1.0f} : Engine::Vector4{1.0f, 0.0f, 0.0f, 1.0f};
	for (int k = -1; k <= 1; ++k) {
		float o = k * 0.03f;
		Engine::Vector3 cv[4] = {
			{hitPoint.x - hs - o, hitPoint.y + 0.05f, hitPoint.z - hs - o},
			{hitPoint.x + hs + o, hitPoint.y + 0.05f, hitPoint.z - hs - o},
			{hitPoint.x + hs + o, hitPoint.y + 0.05f, hitPoint.z + hs + o},
			{hitPoint.x - hs - o, hitPoint.y + 0.05f, hitPoint.z + hs + o}
		};
		renderer->DrawLine3D(cv[0], cv[1], highlightLineColor, true);
		renderer->DrawLine3D(cv[1], cv[2], highlightLineColor, true);
		renderer->DrawLine3D(cv[2], cv[3], highlightLineColor, true);
		renderer->DrawLine3D(cv[3], cv[0], highlightLineColor, true);
	}

	// 広範囲のグリッド線も描画する（アルファ値を高くして見やすく）
	if (drawExtras) {
		const int gridLines = 15;
		Engine::Vector4 gridColor = {1.0f, 1.0f, 1.0f, 0.6f};
		for (int i = -gridLines; i <= gridLines; ++i) {
			float offset = i * 2.0f;
			// X方向の線
			Engine::Vector3 p1 = {hitPoint.x - gridLines * 2.0f, hitPoint.y + 0.02f, hitPoint.z + offset};
			Engine::Vector3 p2 = {hitPoint.x + gridLines * 2.0f, hitPoint.y + 0.02f, hitPoint.z + offset};
			renderer->DrawLine3D(p1, p2, gridColor, true);
			// Z方向の線
			p1 = {hitPoint.x + offset, hitPoint.y + 0.02f, hitPoint.z - gridLines * 2.0f};
			p2 = {hitPoint.x + offset, hitPoint.y + 0.02f, hitPoint.z + gridLines * 2.0f};
			renderer->DrawLine3D(p1, p2, gridColor, true);
		}
	}


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

	// さきほどのハイライトメッシュを描画
	renderer->DrawMesh(highlightPlaneHandle, previewTextureHandle_, highlightTr, planeColor, "Toon");

	Engine::Transform tr;
	if (objPath.find("Pipe") != std::string::npos) {
		tr.translate = {hitPoint.x, hitPoint.y + 0.4f, hitPoint.z};
		tr.rotate = {-1.570796f, 0.0f, 0.0f};
		tr.scale = {0.35f, 0.35f, 0.8f};
	} else {
		tr.translate = {hitPoint.x, hitPoint.y + 0.5f, hitPoint.z};
		tr.scale = {1.0f, 1.0f, 1.0f};
	}
	const Engine::Vector4 previewColor = canPlace ? Engine::Vector4{0.6f, 1.0f, 0.6f, 0.6f} : Engine::Vector4{1.0f, 0.3f, 0.3f, 0.6f};
	renderer->DrawMesh(previewModelHandle_, previewTextureHandle_, tr, previewColor, "Toon");

	// パイプ設置時のみ、既存のタンク・大砲・ミサイル・ポイズンの接続エリア（緑の平面十字）を描画する
	if (objPath.find("Pipe") != std::string::npos && drawExtras) {
		static uint32_t crossPlaneHandle = 0;
		if (crossPlaneHandle == 0) {
			crossPlaneHandle = renderer->LoadObjMesh("Resources/Models/cube/cube.obj");
		}
		auto& registry = scene->GetRegistry();
		for (auto entity : registry.view<TransformComponent, TagComponent>()) {
			auto tag = registry.get<TagComponent>(entity).tag;
			// 接続可能な施設（タンク、大砲、ミサイル、ポイズンなど）の場合のみ十字を描画
			if (tag == TagType::Canon || tag == TagType::Cannon || tag == TagType::BulletTank || 
				tag == TagType::Missile || tag == TagType::Poison || tag == TagType::PipeCannon||
				tag == TagType::IceCanon) {
				const auto& tc = registry.get<TransformComponent>(entity);
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
		}
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

	// 何かオブジェクトを設置した場合は地形が変わった可能性があるため高さキャッシュをクリアする
	ClearHeightCache();

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

void PhaseSystemScript::InitializeInsertPhase(GameScene* scene) {
	if (isInsertInitialized_) return;

	insertWaypoints_.clear();

	// 1. コアと最初のスポナーの座標を検索
	DirectX::XMFLOAT3 corePos = {0.0f, 0.0f, 0.0f};
	auto coreObj = scene->FindObjectByName("Core");
	if (scene->GetRegistry().valid(coreObj) && scene->GetRegistry().all_of<TransformComponent>(coreObj)) {
		auto& tc = scene->GetRegistry().get<TransformComponent>(coreObj);
		corePos = {tc.translate.x, tc.translate.y, tc.translate.z};
	}

	DirectX::XMFLOAT3 spawnerPos = {25.0f, 0.0f, 25.0f}; // フォールバック値
	auto spawnerObj = scene->FindObjectByName("Spawner_W1_1");
	if (!scene->GetRegistry().valid(spawnerObj)) {
		// スポナー名に "Spawner" が入っているものを探す
		auto view = scene->GetRegistry().view<NameComponent, TransformComponent>();
		for (auto e : view) {
			const auto& name = view.get<NameComponent>(e).name;
			if (name.find("Spawner") != std::string::npos) {
				auto& tc = view.get<TransformComponent>(e);
				spawnerPos = {tc.translate.x, tc.translate.y, tc.translate.z};
				break;
			}
		}
	} else if (scene->GetRegistry().all_of<TransformComponent>(spawnerObj)) {
		auto& tc = scene->GetRegistry().get<TransformComponent>(spawnerObj);
		spawnerPos = {tc.translate.x, tc.translate.y, tc.translate.z};
	}

	// 2. カメラの現在位置・回転を保存
	auto& camera = scene->GetCamera();
	originalCameraPos_ = {camera.Position().x, camera.Position().y, camera.Position().z};
	originalCameraRot_ = {camera.Rotation().x, camera.Rotation().y, camera.Rotation().z};

	// 3. ウェイポイントの構築 (コア -> 鳥瞰 -> スポナー -> 元に戻る)
	// WP0: コアを見下ろす視点 (開始)
	insertWaypoints_.push_back({
		{corePos.x, corePos.y + 12.0f, corePos.z - 20.0f},
		{0.45f, 0.0f, 0.0f}, // Pitch 25度下向き
		3.5f
	});

	// WP1: ステージ全体を見下ろす鳥瞰視点
	insertWaypoints_.push_back({
		{corePos.x, corePos.y + 40.0f, corePos.z - 45.0f},
		{0.7f, 0.0f, 0.0f}, // Pitch 40度下向き
		4.0f
	});

	// WP2: 最初の敵出現地点（スポナー）にクローズアップする視点
	insertWaypoints_.push_back({
		{spawnerPos.x, spawnerPos.y + 8.0f, spawnerPos.z - 15.0f},
		{0.4f, 0.0f, 0.0f}, // Pitch 22度下向き
		4.0f
	});

	// WP3: プレイヤー操作開始位置にスムーズに戻る
	insertWaypoints_.push_back({
		{corePos.x, corePos.y + 25.0f, corePos.z - 25.0f},
		{0.5f, 0.0f, 0.0f},
		2.0f
	});

	currentWaypointIndex_ = 0;
	waypointTime_ = 0.0f;
	skipHoldTime_ = 0.0f;

	// 4. スキップ案内UIを生成
	CreateSkipUI(scene);

	// 演出中はカーソルを非表示
	while (ShowCursor(FALSE) >= 0);

	// 5. インサート演出中の不要な入力と追従を無効化する
	auto player = scene->FindObjectByName("Player");
	if (scene->GetRegistry().valid(player) && scene->GetRegistry().all_of<PlayerInputComponent>(player)) {
		scene->GetRegistry().get<PlayerInputComponent>(player).enabled = false;
	}
	auto prepCam = scene->FindObjectByName("PreparationCamera");
	if (scene->GetRegistry().valid(prepCam)) {
		if (scene->GetRegistry().all_of<PlayerInputComponent>(prepCam)) {
			scene->GetRegistry().get<PlayerInputComponent>(prepCam).enabled = false;
		}
		if (scene->GetRegistry().all_of<CameraTargetComponent>(prepCam)) {
			scene->GetRegistry().get<CameraTargetComponent>(prepCam).enabled = false;
		}
	}

	isInsertInitialized_ = true;
}

void PhaseSystemScript::UpdateInsertPhase(GameScene* scene, float dt) {
	auto* input = Engine::Input::GetInstance();
	if (!input) return;

	if (!isInsertInitialized_) {
		InitializeInsertPhase(scene);
	}

	// Sキー長押しでスキップ
	bool isHoldingSkip = input->Down(DIK_S) || (GetAsyncKeyState('S') & 0x8000);
	if (isHoldingSkip) {
		skipHoldTime_ += dt;
	} else {
		skipHoldTime_ = 0.0f;
	}

	UpdateSkipUIProgress(scene);

	if (skipHoldTime_ >= 1.0f || currentWaypointIndex_ >= (int)insertWaypoints_.size()) {
		EndInsertPhase(scene);
		return;
	}

	auto& camera = scene->GetCamera();
	const auto& target = insertWaypoints_[currentWaypointIndex_];

	DirectX::XMFLOAT3 startPos = originalCameraPos_;
	DirectX::XMFLOAT3 startRot = originalCameraRot_;
	if (currentWaypointIndex_ > 0) {
		startPos = insertWaypoints_[currentWaypointIndex_ - 1].position;
		startRot = insertWaypoints_[currentWaypointIndex_ - 1].rotation;
	}

	waypointTime_ += dt;
	float t = waypointTime_ / target.duration;
	if (t > 1.0f) t = 1.0f;

	// Smooth Step (3t^2 - 2t^3) による滑らかな加減速補間
	float smoothT = t * t * (3.0f - 2.0f * t);

	DirectX::XMFLOAT3 currentPos;
	currentPos.x = startPos.x + (target.position.x - startPos.x) * smoothT;
	currentPos.y = startPos.y + (target.position.y - startPos.y) * smoothT;
	currentPos.z = startPos.z + (target.position.z - startPos.z) * smoothT;

	DirectX::XMFLOAT3 currentRot;
	currentRot.x = startRot.x + (target.rotation.x - startRot.x) * smoothT;
	currentRot.y = startRot.y + (target.rotation.y - startRot.y) * smoothT;
	currentRot.z = startRot.z + (target.rotation.z - startRot.z) * smoothT;

	camera.SetPosition({currentPos.x, currentPos.y, currentPos.z});
	camera.SetRotation({currentRot.x, currentRot.y, currentRot.z});

	if (t >= 1.0f) {
		currentWaypointIndex_++;
		waypointTime_ = 0.0f;
	}
}

void PhaseSystemScript::CreateSkipUI(GameScene* scene) {
	if (!scene) return;
	auto& reg = scene->GetRegistry();

	// 1. テキストUI
	skipPromptUI_ = scene->CreateEntity("SkipPromptUI");
	auto& rectPrompt = reg.emplace<RectTransformComponent>(skipPromptUI_);
	rectPrompt.pos = {650.0f, 400.0f}; // 画面右下
	rectPrompt.anchor = {0.5f, 0.5f};
	rectPrompt.pivot = {0.5f, 0.5f};
	rectPrompt.size = {400.0f, 50.0f};

	auto& textPrompt = reg.emplace<UITextComponent>(skipPromptUI_);
	textPrompt.text = "[S]長押しでスキップ";
	textPrompt.fontSize = 32.0f;
	textPrompt.color = {1.0f, 1.0f, 1.0f, 1.0f};
	textPrompt.fontPath = "Resources\\Fonts\\ZenAntique-Regular.ttf";
	textPrompt.outlineEnabled = true;
	textPrompt.outlineColor = {0.0f, 0.0f, 0.0f, 1.0f};
	textPrompt.outlineThickness = 2.0f;

	// 2. プログレスバーUI
	skipProgressUI_ = scene->CreateEntity("SkipProgressUI");
	auto& rectBar = reg.emplace<RectTransformComponent>(skipProgressUI_);
	rectBar.pos = {650.0f, 440.0f};
	rectBar.anchor = {0.5f, 0.5f};
	rectBar.pivot = {0.5f, 0.5f};
	rectBar.size = {0.0f, 8.0f};

	auto& imgBar = reg.emplace<UIImageComponent>(skipProgressUI_);
	imgBar.color = {1.0f, 0.3f, 0.3f, 0.8f};
}

void PhaseSystemScript::UpdateSkipUIProgress(GameScene* scene) {
	if (!scene || skipProgressUI_ == entt::null) return;
	auto& reg = scene->GetRegistry();

	if (reg.valid(skipProgressUI_) && reg.all_of<RectTransformComponent>(skipProgressUI_)) {
		auto& rect = reg.get<RectTransformComponent>(skipProgressUI_);
		float progress = skipHoldTime_ / 1.0f;
		if (progress > 1.0f) progress = 1.0f;
		rect.size.x = progress * 250.0f;
	}
}

void PhaseSystemScript::EndInsertPhase(GameScene* scene) {
	if (!scene) return;

	// UIオブジェクトの破棄
	if (skipPromptUI_ != entt::null && scene->GetRegistry().valid(skipPromptUI_)) {
		scene->DestroyObject(static_cast<uint32_t>(skipPromptUI_));
		skipPromptUI_ = entt::null;
	}
	if (skipProgressUI_ != entt::null && scene->GetRegistry().valid(skipProgressUI_)) {
		scene->DestroyObject(static_cast<uint32_t>(skipProgressUI_));
		skipProgressUI_ = entt::null;
	}

	// 終了時に標準の準備フェーズ用カメラ（コア見下ろし）に移行
	auto prepCam = scene->FindObjectByName("PreparationCamera");
	auto core = scene->FindObjectByName("Core");
	if (scene->GetRegistry().valid(core) && scene->GetRegistry().valid(prepCam)) {
		auto& coreTc = scene->GetRegistry().get<TransformComponent>(core);
		
		if (scene->GetRegistry().all_of<TransformComponent>(prepCam)) {
			auto& camTc = scene->GetRegistry().get<TransformComponent>(prepCam);
			camTc.translate = coreTc.translate;
		}
		
		if (scene->GetRegistry().all_of<PlayerInputComponent>(prepCam)) {
			auto& camPi = scene->GetRegistry().get<PlayerInputComponent>(prepCam);
			camPi.cameraPitch = 0.5f;
			camPi.cameraYaw = 0.0f;
			
			auto& camera = scene->GetCamera();
			auto rot = camera.Rotation();
			rot.x = 0.5f;
			rot.y = 0.0f;
			camera.SetRotation(rot);
		}
		
		if (scene->GetRegistry().all_of<CameraTargetComponent>(prepCam)) {
			auto& ct = scene->GetRegistry().get<CameraTargetComponent>(prepCam);
			ct.distance = 25.0f;
			ct.height = 10.0f;
		}

		auto& camera = scene->GetCamera();
		camera.SetPosition({coreTc.translate.x, coreTc.translate.y + 25.0f, coreTc.translate.z - 25.0f});
		camera.SetRotation({0.5f, 0.0f, 0.0f});
	}

	// 演出中に無効化していた入力を復旧
	auto player = scene->FindObjectByName("Player");
	if (scene->GetRegistry().valid(player) && scene->GetRegistry().all_of<PlayerInputComponent>(player)) {
		scene->GetRegistry().get<PlayerInputComponent>(player).enabled = true;
	}
	if (scene->GetRegistry().valid(prepCam)) {
		if (scene->GetRegistry().all_of<PlayerInputComponent>(prepCam)) {
			scene->GetRegistry().get<PlayerInputComponent>(prepCam).enabled = true;
		}
		if (scene->GetRegistry().all_of<CameraTargetComponent>(prepCam)) {
			scene->GetRegistry().get<CameraTargetComponent>(prepCam).enabled = true;
		}
	}

	// カーソルを強制表示
	while (ShowCursor(TRUE) < 0);

	// 通常の準備フェーズに移行
	isPhase_ = PreparationPhase;
	NextPhase_ = PreparationPhase;
	preIsPhase_ = PreparationPhase;

	isInsertInitialized_ = false;
}

REGISTER_SCRIPT(PhaseSystemScript);

} // namespace Game
