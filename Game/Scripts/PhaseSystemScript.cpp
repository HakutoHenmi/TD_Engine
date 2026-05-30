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

#include "../../Engine/SceneManager.h"
#include "../../Engine/ThirdParty/nlohmann/json.hpp"
#include "ResultManagerScript.h"
#include "TutorialScript.h"
#include "WaveManagement.h"
#include "PlayerScript.h" // ★追加

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

	s_gameOverPhase_ = 0;
	s_gameClearPhase_ = 0; // ★追加: ゲームクリア状態も確実に初期化する
	gameOverTimer_ = 0.0f;
	if (scene) {
		scene->SetGameTimeScale(1.0f);
	}

	// チュートリアルシーン以外ならインサートカメラ演出から開始
	isTutorialScene_ = false;
	if (scene) {
		const auto& path = scene->GetStagePath();
		if (path.find("Tutorial") != std::string::npos || path.find("tutorial") != std::string::npos) {
			isTutorialScene_ = true;
		}
	}

	if (!isTutorialScene_) {
		isPhase_ = InsertPhase;
		NextPhase_ = InsertPhase;
	} else {
		isPhase_ = PreparationPhase;
		NextPhase_ = PreparationPhase;
	}
	
	// ★修正: 初回Update時に確実に初期化処理(カメラ位置設定など)が走るように、preIsPhase_を現在と違う値にする
	preIsPhase_ = static_cast<PhaseState>(-1);

	// 初回BGM再生のトリガー (Update内の判定で再生されるように preIsPhase_ をズラしてあるが、Startでも念押し)
	if (auto* audio = Engine::Audio::GetInstance()) {
		if (isPhase_ == BattlePhase) {
			currentBgmVoiceHandle_ = audio->Play(battleBgmHandle_, true, 0.4f);
		} else {
			currentBgmVoiceHandle_ = audio->Play(preparationBgmHandle_, true, 0.4f);
		}
	}

	currentPhase_ = 0;
	CoinCount = StartCoinCount_;

	// 状態フラグの初期化
	isPhaseTransitioning_ = false;
	isFadeFinished_ = false;
	isPlacementMode_ = false;
	isSellMode_ = false;

	enemyCountUI_ = entt::null;
	installationCostUI_ = entt::null;

	// インサートカメラ変数の初期化
	isInsertInitialized_ = false;
	currentWaypointIndex_ = 0;
	waypointTime_ = 0.0f;
	skipHoldTime_ = 0.0f;
	skipPromptUI_ = entt::null;
	skipProgressUI_ = entt::null;

	// キー入力の初期化 (trueにして初回フレームの残留入力による誤検出を防ぐ)
	preKeyP_ = true;
	preKeySpace_ = true;
	preKeyN_ = true;

	// グローバルなフラグの初期化
	isSkillTreeOpen_ = false;
	skillTree_.Close(scene);

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

		// その他のテクスチャをロード（リソース管理は別途行うことを想定）
		startButtonFrameTextureHandle_ = renderer->LoadTexture2D("Resources/Textures/GamaUI/successs.png");
	}

	// BGMのロード
	if (auto* audio = Engine::Audio::GetInstance()) {
		battleBgmHandle_ = audio->Load("Resources/Audio/BGM/Battle.mp3");
		preparationBgmHandle_ = audio->Load("Resources/Audio/BGM/Preparation.mp3");
		resultBgmHandle_ = audio->Load("Resources/Audio/BGM/Result.mp3");
		installationSeHandle_ = audio->Load("Resources/Audio/SE/installation.mp3");
	}

	isResultBgmPlaying_ = false;

	// 設置開始イベントの購読
	SubscribeString(scene, "StartInstallation", [this](const std::string& dataStr) {
		try {
			json data = json::parse(dataStr);
			selectedObjPath_ = data.value("prefab", "");
			selectedObjCost_ = data.value("cost", 0);
			isPlacementMode_ = true;
			isSellMode_ = false;
		} catch (...) {
		}
	});

	if (scene->GetRegistry().all_of<UITextComponent>(entity))
		scene->GetRegistry().get<UITextComponent>(entity).text = std::to_string(CoinCount);
	
	// BGMの再生 (シーン開始時のフェーズに応じて即座に再生)
	if (auto* audio = Engine::Audio::GetInstance()) {
		if (isPhase_ == BattlePhase) {
			currentBgmVoiceHandle_ = audio->Play(battleBgmHandle_, true, 0.4f);
		} else if (isPhase_ == PreparationPhase || isPhase_ == InsertPhase) {
			currentBgmVoiceHandle_ = audio->Play(preparationBgmHandle_, true, 0.4f);
		}
	}
}

void PhaseSystemScript::Update(entt::entity entity, GameScene* scene, float dt) {
	(void)entity;

	// === Game Over Sequence Update ===
	if (s_gameOverPhase_ > 0) {
		static auto lastTime = std::chrono::steady_clock::now();
		auto nowTime = std::chrono::steady_clock::now();
		float realDt = std::chrono::duration<float>(nowTime - lastTime).count();
		lastTime = nowTime;
		if (realDt > 0.1f) realDt = 1.0f / 60.0f;

		gameOverTimer_ += realDt;
		auto& camera = scene->GetCamera();

		// Find core position
		DirectX::XMFLOAT3 corePos = {0,0,0};
		auto core = scene->FindObjectByName("Core");
		if (scene->GetRegistry().valid(core) && scene->GetRegistry().all_of<TransformComponent>(core)) {
			corePos = scene->GetRegistry().get<TransformComponent>(core).translate;
		} else {
			const auto& cores = scene->GetEntitiesByTag(TagType::Core);
			if (!cores.empty() && scene->GetRegistry().valid(cores[0]) && scene->GetRegistry().all_of<TransformComponent>(cores[0])) {
				corePos = scene->GetRegistry().get<TransformComponent>(cores[0]).translate;
			}
		}

		if (s_gameOverPhase_ == 1) {
			// Phase 1: カメラをコアに近づけ、コアが爆発する様子を見せる (2秒間)
			float t = (std::min)(gameOverTimer_ / 2.0f, 1.0f);
			float ease = t * t * (3.0f - 2.0f * t);

			// 現在のカメラとコアの方向ベクトルを計算
			float dirX = goStartCamPos_.x - corePos.x;
			float dirZ = goStartCamPos_.z - corePos.z;
			float dist = std::sqrt(dirX * dirX + dirZ * dirZ);
			if (dist < 0.1f) { dirX = 0; dirZ = -1; dist = 1; }
			dirX /= dist;
			dirZ /= dist;

			// Target: close to core from current direction
			DirectX::XMFLOAT3 targetCamPos = {corePos.x + dirX * 15.0f, corePos.y + 5.0f, corePos.z + dirZ * 15.0f}; 

			// コアを正確に見るための角度を計算
			DirectX::XMFLOAT3 targetCamRot;
			float ldx = corePos.x - targetCamPos.x;
			float ldy = corePos.y - targetCamPos.y;
			float ldz = corePos.z - targetCamPos.z;
			targetCamRot.y = std::atan2(ldx, ldz);
			float xzDist = std::sqrt(ldx * ldx + ldz * ldz);
			targetCamRot.x = -std::atan2(ldy, xzDist);
			targetCamRot.z = 0.0f;

			DirectX::XMFLOAT3 newPos;
			newPos.x = goStartCamPos_.x + (targetCamPos.x - goStartCamPos_.x) * ease;
			newPos.y = goStartCamPos_.y + (targetCamPos.y - goStartCamPos_.y) * ease;
			newPos.z = goStartCamPos_.z + (targetCamPos.z - goStartCamPos_.z) * ease;
			
			DirectX::XMFLOAT3 newRot;
			newRot.x = goStartCamRot_.x + (targetCamRot.x - goStartCamRot_.x) * ease;
			
			float diff = targetCamRot.y - goStartCamRot_.y;
			while (diff >  DirectX::XM_PI) diff -= DirectX::XM_2PI;
			while (diff < -DirectX::XM_PI) diff += DirectX::XM_2PI;
			newRot.y = goStartCamRot_.y + diff * ease;

			newRot.z = goStartCamRot_.z + (targetCamRot.z - goStartCamRot_.z) * ease;

			camera.SetPosition({newPos.x, newPos.y, newPos.z});
			camera.SetRotation(newRot);

			// 爆発エフェクトを定期的に発生させる
			static float expTimer = 0;
			expTimer += realDt;
			if (expTimer > 0.1f) {
				expTimer = 0;
				entt::entity expEntity = scene->CreateEntity("GameOverExplosion");
				auto& tc = scene->GetRegistry().get<TransformComponent>(expEntity);
				tc.translate = {
					corePos.x + (-2.0f + 4.0f * (rand() / (float)RAND_MAX)),
					corePos.y + (0.0f + 4.0f * (rand() / (float)RAND_MAX)),
					corePos.z + (-2.0f + 4.0f * (rand() / (float)RAND_MAX))
				};
				auto& vc = scene->GetRegistry().emplace<VariableComponent>(expEntity);
				vc.SetValue("NormalX", 0.0f);
				vc.SetValue("NormalY", 1.0f); // 真上に噴き上がらせる
				vc.SetValue("NormalZ", 0.0f);
				vc.SetValue("Radius", 6.0f);
				vc.SetValue("Duration", 2.0f);
				vc.SetValue("ScatterMode", 0.0f); // 大砲と同じく法線方向へ伸びる
				vc.SetValue("ScatterSpeed", 20.0f);
				vc.SetValue("Count", 40.0f); // 煙の量
				vc.SetValue("ColorMode", 0.0f); // 通常の白/茶色スチーム
				vc.SetValue("IsFlight", 1.0f); // キューブ破片を出さない
				vc.SetValue("IgnoreTimeScale", 1.0f); // 時間停止中も動かす

				auto& sc = scene->GetRegistry().emplace<ScriptComponent>(expEntity);
				sc.scripts.push_back({"SpaceShatterScript", "", nullptr});
			}

			if (gameOverTimer_ >= 2.5f) {
				s_gameOverPhase_ = 2;
				gameOverTimer_ = 0.0f;
				goStartCamPos_ = camera.Position();
				goStartCamRot_ = camera.Rotation();
			}
		} else if (s_gameOverPhase_ == 2) {
			// Phase 2: カメラを空に向けてリザルトUIを表示
			float t = (std::min)(gameOverTimer_ / 1.5f, 1.0f);
			float ease = t * t * (3.0f - 2.0f * t);

			// Target: looking up at sky
			DirectX::XMFLOAT3 targetCamRot = { -1.5f, goStartCamRot_.y, 0.0f }; // -1.5 rad is roughly looking straight up

			DirectX::XMFLOAT3 newRot;
			newRot.x = goStartCamRot_.x + (targetCamRot.x - goStartCamRot_.x) * ease;
			newRot.y = goStartCamRot_.y + (targetCamRot.y - goStartCamRot_.y) * ease;
			newRot.z = goStartCamRot_.z + (targetCamRot.z - goStartCamRot_.z) * ease;

			camera.SetRotation(newRot);

			if (gameOverTimer_ >= 1.5f && resultManagerEntity_ == entt::null) {
				ResultManagerScript::pendingIsWin = false;
				ResultManagerScript::pendingOriginalScene = scene->GetStagePath();
				
				resultManagerEntity_ = scene->CreateEntity("SkyResultManager");
				
				// リザルトUI用のスコアとタイムをセット
				scene->GetRegistry().emplace<VariableComponent>(resultManagerEntity_);
				scene->SetVar(resultManagerEntity_, "isWin", 0.0f);
				scene->SetVar(resultManagerEntity_, "score", 300.0f);
				scene->SetVar(resultManagerEntity_, "clearTime", scene->GetPlayTime());

				auto& sc = scene->GetRegistry().emplace<ScriptComponent>(resultManagerEntity_);
				ScriptEntry entry;
				entry.scriptPath = "ResultManagerScript";
				entry.instance = ScriptEngine::GetInstance()->CreateScript("ResultManagerScript");
				if (entry.instance) {
					entry.instance->Start(resultManagerEntity_, scene);
				}
				sc.scripts.push_back(std::move(entry));
			}
		}
		
		// ゲームオーバー中は通常のフェーズ処理をスキップ
		return;
	}

	// === Game Clear Sequence Update ===
	if (s_gameClearPhase_ > 0) {
		static auto lastTime = std::chrono::steady_clock::now();
		auto nowTime = std::chrono::steady_clock::now();
		float realDt = std::chrono::duration<float>(nowTime - lastTime).count();
		lastTime = nowTime;
		if (realDt > 0.1f) realDt = 1.0f / 60.0f;

		gameOverTimer_ += realDt;
		Engine::Camera& camera = scene->GetCamera();

		DirectX::XMFLOAT3 corePos = {0,0,0};
		auto core = scene->FindObjectByName("Core");
		if (scene->GetRegistry().valid(core) && scene->GetRegistry().all_of<TransformComponent>(core)) {
			corePos = scene->GetRegistry().get<TransformComponent>(core).translate;
		}

		if (s_gameClearPhase_ == 1) {
			// Phase 1: カメラをコアに近づけ、花火を見上げる
			float t = (std::min)(gameOverTimer_ / 1.2f, 1.0f);
			float ease = t * t * (3.0f - 2.0f * t);

			// 現在のカメラとコアの方向ベクトルを計算
			float dirX = goStartCamPos_.x - corePos.x;
			float dirZ = goStartCamPos_.z - corePos.z;
			float dist = std::sqrt(dirX * dirX + dirZ * dirZ);
			if (dist < 0.1f) { dirX = 0; dirZ = -1; dist = 1; }
			dirX /= dist;
			dirZ /= dist;

			DirectX::XMFLOAT3 targetCamPos = {corePos.x + dirX * 15.0f, corePos.y + 5.0f, corePos.z + dirZ * 15.0f}; 
			
			// コアを正確に見るための角度を計算
			DirectX::XMFLOAT3 targetCamRot;
			float ldx = corePos.x - targetCamPos.x;
			float ldy = corePos.y - targetCamPos.y;
			float ldz = corePos.z - targetCamPos.z;
			targetCamRot.y = std::atan2(ldx, ldz);
			float xzDist = std::sqrt(ldx * ldx + ldz * ldz);
			targetCamRot.x = -std::atan2(ldy, xzDist);
			targetCamRot.z = 0.0f;

			DirectX::XMFLOAT3 newPos;
			newPos.x = goStartCamPos_.x + (targetCamPos.x - goStartCamPos_.x) * ease;
			newPos.y = goStartCamPos_.y + (targetCamPos.y - goStartCamPos_.y) * ease;
			newPos.z = goStartCamPos_.z + (targetCamPos.z - goStartCamPos_.z) * ease;
			
			DirectX::XMFLOAT3 newRot;
			newRot.x = goStartCamRot_.x + (targetCamRot.x - goStartCamRot_.x) * ease;
			// Y軸回転は最短距離で補間
			float diff = targetCamRot.y - goStartCamRot_.y;
			while (diff >  DirectX::XM_PI) diff -= DirectX::XM_2PI;
			while (diff < -DirectX::XM_PI) diff += DirectX::XM_2PI;
			newRot.y = goStartCamRot_.y + diff * ease;
			newRot.z = goStartCamRot_.z + (targetCamRot.z - goStartCamRot_.z) * ease;

			camera.SetPosition({newPos.x, newPos.y, newPos.z});
			camera.SetRotation(newRot);

			// 追加の花火をランダムに打ち上げる
			static float fwTimer = 0;
			fwTimer += realDt;
			if (fwTimer > 0.15f) {
				fwTimer = 0;
				entt::entity fwEntity = scene->CreateEntity("GameClearFirework");
				auto& tc = scene->GetRegistry().get_or_emplace<TransformComponent>(fwEntity);
				tc.translate = {
					corePos.x + (-30.0f + 60.0f * (rand() / (float)RAND_MAX)),
					corePos.y,
					corePos.z + (-10.0f + 40.0f * (rand() / (float)RAND_MAX))
				};
				auto& sc = scene->GetRegistry().emplace<ScriptComponent>(fwEntity);
				sc.scripts.push_back({"FireworkScript", "", nullptr});
				sc.enabled = true;
			}

			if (gameOverTimer_ >= 1.2f) { // 最初の花火が爆発するくらいのタイミング
				s_gameClearPhase_ = 2;
				gameOverTimer_ = 0.0f;
				goStartCamPos_ = camera.Position();
				goStartCamRot_ = camera.Rotation();
			}
		} else if (s_gameClearPhase_ == 2) {
			// Phase 2: カメラを空に向けてリザルトUIを表示しつつ、引き続き花火を打ち上げる
			float t = (std::min)(gameOverTimer_ / 1.5f, 1.0f);
			float ease = t * t * (3.0f - 2.0f * t);

			// Target: 花火が画面にしっかり収まるように、真上ではなく約57度(-1.0f)上を見上げる
			DirectX::XMFLOAT3 targetCamRot = { -1.0f, goStartCamRot_.y, 0.0f }; 

			DirectX::XMFLOAT3 newRot;
			newRot.x = goStartCamRot_.x + (targetCamRot.x - goStartCamRot_.x) * ease;
			newRot.y = goStartCamRot_.y + (targetCamRot.y - goStartCamRot_.y) * ease;
			newRot.z = goStartCamRot_.z + (targetCamRot.z - goStartCamRot_.z) * ease;

			camera.SetRotation(newRot);

			static float fwTimer2 = 0;
			fwTimer2 += realDt;
			if (fwTimer2 > 0.12f) {
				fwTimer2 = 0;
				entt::entity fwEntity = scene->CreateEntity("GameClearFirework");
				auto& tc = scene->GetRegistry().get_or_emplace<TransformComponent>(fwEntity);
				tc.translate = {
					corePos.x + (-40.0f + 80.0f * (rand() / (float)RAND_MAX)),
					corePos.y,
					corePos.z + (-10.0f + 40.0f * (rand() / (float)RAND_MAX))
				};
				auto& sc = scene->GetRegistry().emplace<ScriptComponent>(fwEntity);
				sc.scripts.push_back({"FireworkScript", "", nullptr});
				sc.enabled = true;
			}

			if (gameOverTimer_ >= 1.5f && resultManagerEntity_ == entt::null) {
				ResultManagerScript::pendingIsWin = true;
				ResultManagerScript::pendingOriginalScene = scene->GetStagePath();
				
				resultManagerEntity_ = scene->CreateEntity("SkyResultManager");
				
				scene->GetRegistry().emplace<VariableComponent>(resultManagerEntity_);
				scene->SetVar(resultManagerEntity_, "isWin", 1.0f);
				scene->SetVar(resultManagerEntity_, "score", 1500.0f);
				scene->SetVar(resultManagerEntity_, "clearTime", scene->GetPlayTime());

				auto& sc = scene->GetRegistry().emplace<ScriptComponent>(resultManagerEntity_);
				ScriptEntry entry;
				entry.scriptPath = "ResultManagerScript";
				entry.instance = ScriptEngine::GetInstance()->CreateScript("ResultManagerScript");
				if (entry.instance) {
					entry.instance->Start(resultManagerEntity_, scene);
				}
				sc.scripts.push_back(std::move(entry));
			}
		}

		// クリア演出中は通常のフェーズ処理をスキップ
		return;
	}

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

	bool key3 = input->Trigger(DIK_3) || (GetAsyncKeyState('3') & 0x8001);
	bool key4 = input->Trigger(DIK_4) || (GetAsyncKeyState('4') & 0x8001);
	bool key5 = input->Trigger(DIK_5) || (GetAsyncKeyState('5') & 0x8001);
	bool key6 = input->Trigger(DIK_6) || (GetAsyncKeyState('6') & 0x8001);
	bool keyX = input->Trigger(DIK_X) || (GetAsyncKeyState('X') & 0x8001); // 削除モード用
	bool keyP = false;
	bool keySpace = input->Trigger(DIK_SPACE) || (GetAsyncKeyState(VK_SPACE) & 0x8001);

	// ★ スキルツリーの入力処理: スキルツリーのUI開閉処理 (NキーまたはコントローラーのBACKボタン)
	bool keyN = input->Trigger(DIK_N) || (GetAsyncKeyState('N') & 0x8001) || input->IsControllerButtonTrigger(XINPUT_GAMEPAD_BACK);
	bool keyEsc = input->Trigger(DIK_ESCAPE) || (GetAsyncKeyState(VK_ESCAPE) & 0x8001); // ★追加: ESCキー

	// 外部(EnemySpawnerScript など)からのフェーズ変更要求を反映
	if (!isPhaseTransitioning_ && isPhase_ != Transition && NextPhase_ != isPhase_) {
		RequestPhaseChange(NextPhase_);
	}

	if (isPhase_ == PreparationPhase) {
		bool placementSelectionChangedThisFrame = false;
		const bool clickedInstallationButtonThisFrame = input->IsMouseTrigger(0) && IsPointerOverInstallationButton(scene);

		bool isTutorial = false;
		if (scene) {
			const auto& path = scene->GetStagePath();
			if (path.find("Tutorial") != std::string::npos || path.find("tutorial") != std::string::npos) {
				isTutorial = true;
			}
		}

		if (!isTutorial) {
			// NキーまたはESCキー(開いている場合)でスキルツリーの開閉
			if (keyN && !preKeyN_) {
				skillTree_.Toggle(scene);
			} else if (skillTree_.IsOpen() && keyEsc) {
				skillTree_.Toggle(scene);
			}

			// スキルツリーが開いている間はスキルツリーの更新のみ
			if (skillTree_.IsOpen()) {
				SetVar(entity, scene, "IsSkillTreeOpen", 1.0f);
				PhaseSystemScript::isSkillTreeOpen_ = true;
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
			PhaseSystemScript::isSkillTreeOpen_ = false;
			SetVar(entity, scene, "IsSkillTreeOpen", 0.0f);
		}

		if (!isTutorial) {
			// 設置モードへの切り替え
			if (key3 || InstallationManager::IsButtonPressed("Resources/Prefabs/Canon.prefab")) {
				selectedObjPath_ = "Resources/Prefabs/NewCannon.prefab";
				selectedObjCost_ = InstallationManager::GetCost(selectedObjPath_);
				if (selectedObjCost_ == 0)
					selectedObjCost_ = canonCost_;
				isPlacementMode_ = true;
				isSellMode_ = false;
				placementSelectionChangedThisFrame = true;
			}

			if (key4 || InstallationManager::IsButtonPressed("Resources/Prefabs/Missile.prefab")) {
				selectedObjPath_ = "Resources/Prefabs/Missile.prefab";
				selectedObjCost_ = InstallationManager::GetCost(selectedObjPath_);
				if (selectedObjCost_ == 0)
					selectedObjCost_ = missileCost_;
				isPlacementMode_ = true;
				isSellMode_ = false;
				placementSelectionChangedThisFrame = true;
			}

			if (key5 || InstallationManager::IsButtonPressed("Resources/Prefabs/Poison.prefab")) {
				selectedObjPath_ = "Resources/Prefabs/Poison.prefab";
				selectedObjCost_ = InstallationManager::GetCost(selectedObjPath_);
				if (selectedObjCost_ == 0)
					selectedObjCost_ = poisonCost_;
				isPlacementMode_ = true;
				isSellMode_ = false;
				placementSelectionChangedThisFrame = true;
			}

			if (key6 || InstallationManager::IsButtonPressed("Resources/Prefabs/IceCanon.prefab")) {
				selectedObjPath_ = "Resources/Prefabs/IceCanon.prefab";
				selectedObjCost_ = InstallationManager::GetCost(selectedObjPath_);
				if (selectedObjCost_ == 0)
					selectedObjCost_ = iceCanonCost_;
				isPlacementMode_ = true;
				isSellMode_ = false;
				placementSelectionChangedThisFrame = true;
			}

			// Xキーまたは「削除機能ボタン」クリックで削除(売却)モードへの切り替え
			if (keyX || InstallationManager::IsButtonPressedByName("DeleteButton")) {
				isSellMode_ = true;
				isPlacementMode_ = false;
				placementSelectionChangedThisFrame = true;
				EditorUI::Log("Sell Mode Activated");
			}
		}

		if (input->IsMouseTrigger(1) && isPlacementMode_) {
			isPlacementMode_ = false;
		}

		if (input->IsMouseTrigger(1) && isSellMode_) {
			isSellMode_ = false;
			EditorUI::Log("Sell Mode Deactivated");
		}

		if (!placementSelectionChangedThisFrame && !clickedInstallationButtonThisFrame && !isSellMode_) {
			if (!isTutorial) {
				Installation(scene, selectedObjPath_);
			}
		}

		if (isSellMode_ && !clickedInstallationButtonThisFrame && !isTutorial) {
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
					if (name.find("Terrain") != std::string::npos || name.find("Plane") != std::string::npos || name.find("Core") != std::string::npos || name.find("Floor") != std::string::npos)
						return;
					// PipeConnectionはパイプのつなぎ目（導線）なのでレイキャスト対象から除外する
					// パイプ本体を削除すれば PipeScript::OnDestroy が自動的に消してくれる
					if (name.find("PipeConnection") != std::string::npos)
						return;
				}

				Engine::Model* model = nullptr;
				if (registry.all_of<GpuMeshColliderComponent>(e)) {
					model = scene->GetRenderer()->GetModel(registry.get<GpuMeshColliderComponent>(e).meshHandle);
				} else if (registry.all_of<MeshRendererComponent>(e)) {
					model = scene->GetRenderer()->GetModel(registry.get<MeshRendererComponent>(e).modelHandle);
				}

				if (model) {
					float d;
					Engine::Vector3 hp;
					if (model->RayCast(rayOrig, rayDir, tc.ToMatrix(), d, hp) && d < bestDist) {
						bestDist = d;
						hoverEntity = e;
					}
				}
			});

			// ★修正: ヒットがある場合のみハイライト表示（空中での描画を防止）
			if (hoverEntity != entt::null && registry.all_of<TransformComponent>(hoverEntity)) {
				auto tc = registry.get<TransformComponent>(hoverEntity);

				// ルートの取得
				entt::entity rootEntity = hoverEntity;
				if (registry.all_of<HierarchyComponent>(hoverEntity)) {
					auto root = hoverEntity;
					while (registry.get<HierarchyComponent>(root).parentId != entt::null) {
						root = registry.get<HierarchyComponent>(root).parentId;
					}
					rootEntity = root;
					if (registry.all_of<TransformComponent>(rootEntity)) {
						tc = registry.get<TransformComponent>(rootEntity);
					}
				}

				// グリッド座標に変換（削除ロジックと同じ）
				constexpr float gridSize = 2.0f;
				int gridX = static_cast<int>(std::floor(tc.translate.x / gridSize));
				int gridZ = static_cast<int>(std::floor(tc.translate.z / gridSize));

				// 同じグリッド内のすべてのエンティティを集める
				std::vector<entt::entity> highlightEntities;
				auto view = registry.view<TransformComponent>();
				for (auto e : view) {
					if (registry.all_of<NameComponent>(e)) {
						const auto& name = registry.get<NameComponent>(e).name;
						// 削除不可オブジェクトはスキップ
						if (name.find("Terrain") != std::string::npos || name.find("Plane") != std::string::npos || 
							name.find("Core") != std::string::npos || name.find("Floor") != std::string::npos ||
							name.find("PipeConnection") != std::string::npos) {
							continue;
						}
					}

					// ★修正: レイキャスト対象となるメッシュを持つかチェック
					// UI ボタンなど削除対象でないオブジェクトをフィルタリング
					Engine::Model* model = nullptr;
					if (registry.all_of<GpuMeshColliderComponent>(e)) {
						model = scene->GetRenderer()->GetModel(registry.get<GpuMeshColliderComponent>(e).meshHandle);
					} else if (registry.all_of<MeshRendererComponent>(e)) {
						model = scene->GetRenderer()->GetModel(registry.get<MeshRendererComponent>(e).modelHandle);
					}

					// モデルがない = レイキャスト対象でない = 削除対象ではない
					if (!model) {
						continue;
					}

					const auto& eTc = registry.get<TransformComponent>(e);
					int eGridX = static_cast<int>(std::floor(eTc.translate.x / gridSize));
					int eGridZ = static_cast<int>(std::floor(eTc.translate.z / gridSize));

					// 同じグリッドマス内か判定
					if (eGridX == gridX && eGridZ == gridZ) {
						// ルートエンティティを取得
						entt::entity highlightEntity = e;
						if (registry.all_of<HierarchyComponent>(e)) {
							auto root = e;
							while (registry.get<HierarchyComponent>(root).parentId != entt::null) {
								root = registry.get<HierarchyComponent>(root).parentId;
							}
							highlightEntity = root;
						}

						// 重複チェック
						bool alreadyAdded = false;
						for (auto& added : highlightEntities) {
							if (added == highlightEntity) {
								alreadyAdded = true;
								break;
							}
						}
						if (!alreadyAdded) {
							highlightEntities.push_back(highlightEntity);
						}
					}
				}

				// ★修正: ボタンで出せる施設（大砲、ミサイル、ポイズン、アイスキャノン）のみを赤色で描画
				for (auto highlightEntity : highlightEntities) {
					if (!registry.all_of<TransformComponent>(highlightEntity))
						continue;

					// 土台は赤くハイライトしない（名前で判定）
					bool isBase = false;
					if (registry.all_of<NameComponent>(highlightEntity)) {
						const auto& name = registry.get<NameComponent>(highlightEntity).name;
						if (name.find("Base") != std::string::npos || name.find("base") != std::string::npos) {
							isBase = true;
						}
					}
					if (isBase) {
						continue;
					}

					// ボタンで出せる施設かチェック（大砲、ミサイル、ポイズン、アイスキャノンのみハイライト）
					bool isHighlightableBuilding = false;
					if (registry.all_of<NameComponent>(highlightEntity)) {
						const auto& name = registry.get<NameComponent>(highlightEntity).name;
						if (name.find("Canon") != std::string::npos || name.find("Cannon") != std::string::npos ||
							name.find("Missile") != std::string::npos ||
							name.find("Poison") != std::string::npos ||
							name.find("Ice") != std::string::npos) {
							isHighlightableBuilding = true;
						}
					}
					if (!isHighlightableBuilding) {
						continue;
					}

					auto hlTc = registry.get<TransformComponent>(highlightEntity);
					uint32_t meshHandle = 0;
					if (registry.all_of<MeshRendererComponent>(highlightEntity)) {
						meshHandle = registry.get<MeshRendererComponent>(highlightEntity).modelHandle;
					} else if (registry.all_of<GpuMeshColliderComponent>(highlightEntity)) {
						meshHandle = registry.get<GpuMeshColliderComponent>(highlightEntity).meshHandle;
					}

					if (meshHandle != 0) {
						Engine::Transform tr;
						tr.translate = {hlTc.translate.x, hlTc.translate.y, hlTc.translate.z};
						tr.rotate = {hlTc.rotate.x, hlTc.rotate.y, hlTc.rotate.z};
						tr.scale = {hlTc.scale.x, hlTc.scale.y, hlTc.scale.z};
						scene->GetRenderer()->DrawMesh(meshHandle, 0, tr, {1.0f, 0.0f, 0.0f, 0.7f}, "Toon");
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
							if (name.find("Canon") != std::string::npos || name.find("Cannon") != std::string::npos) {
								refundCost = canonCost_;
							} else if (name.find("Missile") != std::string::npos) {
								refundCost = missileCost_;
							} else if (name.find("Poison") != std::string::npos) {
								refundCost = poisonCost_;
							} else if (name.find("Ice") != std::string::npos) {
								refundCost = iceCanonCost_;
							} else {
								refundCost = 0;
							} // 未知のオブジェクト
						}

						if (refundCost > 0) {
							// 削除対象エンティティを決定（親がいればそれを削除対象にする）
							entt::entity targetEntity = hoverEntity;
							if (registry.all_of<HierarchyComponent>(hoverEntity)) {
								auto root = hoverEntity;
								while (registry.get<HierarchyComponent>(root).parentId != entt::null) {
									root = registry.get<HierarchyComponent>(root).parentId;
								}
								targetEntity = root;
							}

							// ★修正: 同じマス（2x2グリッド）内にあるすべてのオブジェクトを収集して削除
							std::vector<entt::entity> entitiesToDelete;

							// グリッド座標に変換
							constexpr float gridSize = 2.0f;
							if (registry.all_of<TransformComponent>(targetEntity)) {
								auto& targetTc = registry.get<TransformComponent>(targetEntity);
								int gridX = static_cast<int>(std::floor(targetTc.translate.x / gridSize));
								int gridZ = static_cast<int>(std::floor(targetTc.translate.z / gridSize));

								// 同じグリッド内のすべてのエンティティを探す
								auto view = registry.view<TransformComponent>();
								for (auto e : view) {
									if (registry.all_of<NameComponent>(e)) {
										const auto& name = registry.get<NameComponent>(e).name;
										// 削除不可オブジェクトはスキップ
										if (name.find("Terrain") != std::string::npos || name.find("Plane") != std::string::npos || 
											name.find("Core") != std::string::npos || name.find("Floor") != std::string::npos ||
											name.find("PipeConnection") != std::string::npos) {
											continue;
										}
									}

									const auto& eTc = registry.get<TransformComponent>(e);
									int eGridX = static_cast<int>(std::floor(eTc.translate.x / gridSize));
									int eGridZ = static_cast<int>(std::floor(eTc.translate.z / gridSize));

									// 同じグリッドマス内か判定
									if (eGridX == gridX && eGridZ == gridZ) {
										entitiesToDelete.push_back(e);
									}
								}
							}

							// 収集したエンティティをすべて削除し、返金を合計する
							int totalRefund = 0;
							std::vector<entt::entity> deletedRoots;

							for (auto deleteEntity : entitiesToDelete) {
								// ルートエンティティを取得
								entt::entity rootToDelete = deleteEntity;
								if (registry.all_of<HierarchyComponent>(deleteEntity)) {
									auto root = deleteEntity;
									while (registry.get<HierarchyComponent>(root).parentId != entt::null) {
										root = registry.get<HierarchyComponent>(root).parentId;
									}
									rootToDelete = root;
								}

								// 重複チェック（既に削除リストにあれば追加しない）
								bool alreadyAdded = false;
								for (auto& deleted : deletedRoots) {
									if (deleted == rootToDelete) {
										alreadyAdded = true;
										break;
									}
								}
								if (alreadyAdded) continue;

								deletedRoots.push_back(rootToDelete);

								// この削除対象の返金額を計算
								int entityRefund = 0;
								if (registry.all_of<NameComponent>(rootToDelete)) {
									const auto& name = registry.get<NameComponent>(rootToDelete).name;
									if (name.find("Canon") != std::string::npos || name.find("Cannon") != std::string::npos) {
										entityRefund = CalculateRefund(canonCost_);
									} else if (name.find("Missile") != std::string::npos) {
										entityRefund = CalculateRefund(missileCost_);
									} else if (name.find("Poison") != std::string::npos) {
										entityRefund = CalculateRefund(poisonCost_);
									} else if (name.find("Ice") != std::string::npos) {
										entityRefund = CalculateRefund(iceCanonCost_);
									}
								}
								totalRefund += entityRefund;

								// スクリプトの OnDestroy を呼び出す
								if (registry.all_of<ScriptComponent>(rootToDelete)) {
									auto& sc = registry.get<ScriptComponent>(rootToDelete);
									for (auto& entry : sc.scripts) {
										if (entry.instance) {
											entry.instance->OnDestroy(rootToDelete, scene);
										}
									}
								}

								// エンティティを削除
								scene->DestroyObject(static_cast<uint32_t>(rootToDelete));
							}

							CoinCount += totalRefund;
							EditorUI::Log("Objects sold for " + std::to_string(totalRefund));
						}
					}
				}
			}
		}

		if (!isTutorial) {
			if (keySpace) {
				battleStartHoldTime_ += dt;
				if (battleStartHoldTime_ >= 1.0f) {
					RequestPhaseChange(BattlePhase);
				 isPlacementMode_ = false;
					skillTree_.Close(scene); // フェーズ移行時にスキルツリーを閉じる
					battleStartHoldTime_ = 0.0f;
				}
			} else {
				battleStartHoldTime_ -= dt * 3.0f; // 離すと少し早く戻る
				if (battleStartHoldTime_ < 0.0f)
					battleStartHoldTime_ = 0.0f;
			}

			// 長押し開始UIの描画 (画面右端の中央付近)
			if (renderer && !PlayerScript::IsHelpOpen()) {
				float cx = (float)Engine::WindowDX::kW - 140.0f;
				float cy = (float)Engine::WindowDX::kH * 0.5f;

				// 背景の円
				Engine::Renderer::SdfUIDesc bg;
				bg.centerPx = {cx, cy};
				bg.sizePx = {100.0f, 100.0f}; // 少し小さく
				bg.shape = 1;                 // 円
				bg.color = {0.1f, 0.1f, 0.1f, 0.8f};
				bg.fill = 1.0f;
				//renderer->DrawSDFUI(bg);

				// 外枠 (ボーダー)
				Engine::Renderer::SdfUIDesc border;
				border.centerPx = {cx, cy};
				border.sizePx = {100.0f, 100.0f};
				border.shape = 1; // 円
				border.color = {0.2f, 0.2f, 0.2f, 0.9f};
				border.lineWidth = 3.0f;
				border.fill = 0.0f;
				//renderer->DrawSDFUI(border);

			
				// 進捗ゲージ (扇形クリッピングから下から上へ)
				if (battleStartHoldTime_ > 0.0f) {
					Engine::Renderer::SdfUIDesc bar;
					bar.centerPx = {cx, cy};
					bar.sizePx = {75.0f, 75.0f};
					bar.shape = 5;                        // 下から上へ埋まる円
					bar.color = {1.0f, 0.7f, 0.1f, 0.9f}; // オレンジ色
					bar.progress = battleStartHoldTime_ / 1.0f;
					bar.fill = 1.0f;
					renderer->DrawSDFUI(bar);
				}
				Engine::Renderer::SpriteDesc desc;

				desc.x = cx - 50.0f;
				desc.y = cy - 50.0f;
				desc.w = 360.0f;
				desc.h = 360.0f;

				desc.x = cx - desc.w * 0.5f+8.8f;
				desc.y = cy - desc.h * 0.5f-6.0f;

				renderer->DrawSprite(startButtonFrameTextureHandle_, desc);
				// テキスト表示
				std::string promptStr = "[SPACE]長押しで開始";
				float tw = renderer->MeasureTextWidth(promptStr, 0.35f);
				renderer->DrawString(promptStr, cx - tw * 0.5f, cy + 65.0f, 0.35f, {1.0f, 1.0f, 1.0f, 1.0f});

				// ゲージ中央のアイコン的なテキスト
				std::string centerStr = "開始";
				float cw = renderer->MeasureTextWidth(centerStr, 0.3f);
				renderer->DrawString(centerStr, cx - cw * 0.5f, cy - 8.0f, 0.3f, {0.9f, 0.9f, 0.9f, 1.0f});
			}
		}

	} else if (isPhase_ == BattlePhase) {
		if (keyP) {
			RequestPhaseChange(PreparationPhase);
		}
		isPlacementMode_ = false;
		isSellMode_ = false;
	} else {
		isPlacementMode_ = false;
		isSellMode_ = false;
	}

	UpdatePhaseTransition();

	if (isPhase_ != preIsPhase_) {
		auto& nav = scene->GetNavigationManager();

		// BGMの切り替え
		if (auto* audio = Engine::Audio::GetInstance()) {
			if (currentBgmVoiceHandle_ != 0) {
				audio->Stop(currentBgmVoiceHandle_);
				currentBgmVoiceHandle_ = 0;
			}

			if (isPhase_ == BattlePhase) {
				currentBgmVoiceHandle_ = audio->Play(battleBgmHandle_, true, 0.4f);
			} else if (isPhase_ == PreparationPhase || isPhase_ == InsertPhase) {
				currentBgmVoiceHandle_ = audio->Play(preparationBgmHandle_, true, 0.4f);
			}
		}

		// フェーズに応じたカメラ追従対象の切り替え
		auto player = scene->FindObjectByName("Player");
		auto prepCam = scene->FindObjectByName("PreparationCamera");

		if (isPhase_ == BattlePhase) {
			// バトル中はプレイヤーにカメラを追従させる
			if (scene->GetRegistry().valid(player) && scene->GetRegistry().all_of<CameraTargetComponent>(player)) {
				scene->GetRegistry().get<CameraTargetComponent>(player).enabled = true;
			}
			if (scene->GetRegistry().valid(prepCam) && scene->GetRegistry().all_of<CameraTargetComponent>(prepCam)) {
				scene->GetRegistry().get<CameraTargetComponent>(prepCam).enabled = false;
			}
			if (scene->GetRegistry().valid(prepCam) && scene->GetRegistry().all_of<PlayerInputComponent>(prepCam)) {
				scene->GetRegistry().get<PlayerInputComponent>(prepCam).enabled = false;
			}

			// 準備から戦闘に切り替わった瞬間
			// 設置物を反映するためにコストマップを更新

			nav.UpdateCostMap(scene);

			// 敵が目指すコアをゴールの位置としてフローフィールドを計算
			entt::entity coreEntity = entt::null;
			const auto& cores = scene->GetEntitiesByTag(TagType::Core);
			if (!cores.empty())
				coreEntity = cores[0];

			if (scene->GetRegistry().valid(coreEntity)) {
				auto& tc = scene->GetRegistry().get<TransformComponent>(coreEntity);
				nav.GenerateFlowField(tc.translate.x, tc.translate.z);
			} else {
				// フォールバック
				nav.GenerateFlowField(0.0f, 0.0f);
			}

			currentPhase_++;

			bool skipSpawn = false;
			if (isTutorialScene_) {
				if (auto* tutorial = TutorialScript::GetInstance()) {
					if (static_cast<int>(tutorial->GetCurrentStep()) <= static_cast<int>(TutorialScript::TutorialStep::Step13_SkillTree)) {
						skipSpawn = true;
					}
				}
			}
			if (!skipSpawn) {
				WaveManagement::SetWave(currentPhase_ - 1);
			}

			// 戦闘中は絵画風エフェクトをオンにする
			Engine::Renderer::GetInstance()->SetPostEffect("Painterly");

		} else if (isPhase_ == PreparationPhase) {
			// 準備中はコア見下ろしカメラを有効にする
			if (scene->GetRegistry().valid(player) && scene->GetRegistry().all_of<CameraTargetComponent>(player)) {
				scene->GetRegistry().get<CameraTargetComponent>(player).enabled = false;
			}
			if (scene->GetRegistry().valid(prepCam) && scene->GetRegistry().all_of<CameraTargetComponent>(prepCam)) {
				scene->GetRegistry().get<CameraTargetComponent>(prepCam).enabled = true;
			}
			if (scene->GetRegistry().valid(prepCam) && scene->GetRegistry().all_of<PlayerInputComponent>(prepCam)) {
				scene->GetRegistry().get<PlayerInputComponent>(prepCam).enabled = true;
			}

			// 準備フェーズに戻った場合はウェーブを待機状態（スポナー無し）にする
			WaveManagement::SetWave(-1);

			// 準備フェーズも絵画風にする（DOFピンボケ付き）
			Engine::Renderer::GetInstance()->SetPostEffect("Painterly");

			// ★ カメラをコアの上に戻して見下ろす視点にする
			auto core = scene->FindObjectByName("Core");
			if (scene->GetRegistry().valid(core) && scene->GetRegistry().valid(prepCam)) {
				// コアの座標を取得
				auto& coreTc = scene->GetRegistry().get<TransformComponent>(core);

				// PreparationCameraの座標をコアに移動
				if (scene->GetRegistry().all_of<TransformComponent>(prepCam)) {
					auto& camTc = scene->GetRegistry().get<TransformComponent>(prepCam);
					camTc.translate = coreTc.translate; // コアと同じ位置へ（CameraFollowSystemがここをターゲットにする）
				}

				// 視点の角度（見下ろし）をリセット
				float targetYaw = 0.0f;
				DirectX::XMFLOAT3 spawnerPos = {25.0f, 0.0f, 25.0f};
				auto spawnerObj = scene->FindObjectByName("Spawner_W1_1");
				if (!scene->GetRegistry().valid(spawnerObj)) {
					auto view = scene->GetRegistry().view<NameComponent, TransformComponent>();
					for (auto e : view) {
						if (view.get<NameComponent>(e).name.find("Spawner") != std::string::npos) {
							spawnerPos = view.get<TransformComponent>(e).translate;
							break;
						}
					}
				} else if (scene->GetRegistry().all_of<TransformComponent>(spawnerObj)) {
					spawnerPos = scene->GetRegistry().get<TransformComponent>(spawnerObj).translate;
				}

				DirectX::XMFLOAT3 dir = { spawnerPos.x - coreTc.translate.x, 0.0f, spawnerPos.z - coreTc.translate.z };
				float len = std::sqrt(dir.x * dir.x + dir.z * dir.z);
				if (scene && scene->GetStagePath().find("Stage1") != std::string::npos) {
					targetYaw = DirectX::XM_PIDIV4;
				} else if (len > 0.001f) {
					targetYaw = std::atan2(dir.x, dir.z);
					float snapInterval = DirectX::XM_PIDIV4; // 45度スナップ
					targetYaw = std::round(targetYaw / snapInterval) * snapInterval;
				}

				if (scene->GetRegistry().all_of<PlayerInputComponent>(prepCam)) {
					auto& camPi = scene->GetRegistry().get<PlayerInputComponent>(prepCam);
					camPi.cameraPitch = 1.2f; // さらに高く見下ろす角度（約68度）
					camPi.cameraYaw = targetYaw;

					// 直接カメラの回転も変更して即座に反映
					auto& camera = scene->GetCamera();
					auto rot = camera.Rotation();
					rot.x = 1.2f;
					rot.y = targetYaw;
					camera.SetRotation(rot);
				}

				// ズーム距離の初期化
				if (scene->GetRegistry().all_of<CameraTargetComponent>(prepCam)) {
					auto& ct = scene->GetRegistry().get<CameraTargetComponent>(prepCam);
					ct.distance = 35.0f; // 高さを出すために距離を大きく離す (上限と一致)
					ct.height = 0.0f;    // 視線のズレを防ぐため追加の高さはゼロにする
				}
			}
		}

		// 状態を同期
		preIsPhase_ = isPhase_;
	}

	if (scene->GetRegistry().all_of<UITextComponent>(entity))
		scene->GetRegistry().get<UITextComponent>(entity).text = std::to_string(CoinCount);

	// ★ 敵の数UIの更新
	bool showEnemyCount = (isPhase_ == BattlePhase);
	if (showEnemyCount) {
		if (auto* tutorial = TutorialScript::GetInstance()) {
			auto step = tutorial->GetCurrentStep();
			if (static_cast<int>(step) < static_cast<int>(TutorialScript::TutorialStep::Step11_PlayerAttack)) {
				showEnemyCount = false; // Step11より前は敵数UIを非表示にする
			}
		}
	}

	if (showEnemyCount) {
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
							rect.pos = {0, -450};       // 画面上部中央
							rect.anchor = {0.5f, 0.5f}; // 中央基準で上へ
							rect.pivot = {0.5f, 0.5f};

							auto& text = scene->GetRegistry().emplace<UITextComponent>(enemyCountUI_);
							text.fontSize = 64.0f;
							text.color = {1, 1, 1, 1};
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
			rect.pos = {0, -400}; // 画面上部
			rect.anchor = {0.5f, 0.5f};
			rect.pivot = {0.5f, 0.5f};

			auto& text = scene->GetRegistry().emplace<UITextComponent>(installationCostUI_);
			text.fontSize = 48.0f;
			text.color = {1, 1, 1, 1};
			text.outlineEnabled = true;
		}

		auto& text = scene->GetRegistry().get<UITextComponent>(installationCostUI_);
		text.text = "Cost: " + std::to_string(currentInstallationCost_);
		// お金が足りない場合は赤色にする
		if (CoinCount < currentInstallationCost_) {
			text.color = {1, 0, 0, 1};
		} else {
			text.color = {1, 1, 1, 1};
		}
		scene->GetRegistry().get<RectTransformComponent>(installationCostUI_).enabled = true;
	} else {
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

		// ★修正: プレイヤーはリスポーン仕様になったため、プレイヤー死亡によるゲームオーバー判定を削除
		// （コアの死亡のみでゲームオーバーになるようにする）

		if (isCoreDead && s_gameOverPhase_ == 0) {
			s_gameOverPhase_ = 1;
			gameOverTimer_ = 0.0f;
			scene->SetGameTimeScale(0.0f); // 全ての時間を止める

			// BGMの切り替え
			if (auto* audio = Engine::Audio::GetInstance()) {
				if (currentBgmVoiceHandle_ != 0) {
					audio->Stop(currentBgmVoiceHandle_);
				}
				currentBgmVoiceHandle_ = audio->Play(resultBgmHandle_, true, 0.5f);
				isResultBgmPlaying_ = true;
			}

			// ★追加: リザルト画面や演出に不要なワールドUIを非表示にする
			auto wsUIView = scene->GetRegistry().view<WorldSpaceUIComponent>();
			for (auto e : wsUIView) {
				scene->GetRegistry().get<WorldSpaceUIComponent>(e).enabled = false;
			}

			// ★追加: ゲーム中のHUD（RectTransformを持つすべてのUI）を非表示にする
			auto uiView = scene->GetRegistry().view<RectTransformComponent>();
			for (auto e : uiView) {
				scene->GetRegistry().get<RectTransformComponent>(e).enabled = false;
			}
			
			// ★追加: テキストや画像もすべて無効化 (残存UIを確実に消去)
			auto txtView = scene->GetRegistry().view<UITextComponent>();
			for (auto e : txtView) {
				scene->GetRegistry().get<UITextComponent>(e).enabled = false;
			}
			auto imgView = scene->GetRegistry().view<UIImageComponent>();
			for (auto e : imgView) {
				scene->GetRegistry().get<UIImageComponent>(e).enabled = false;
			}
			
			// カメラの初期位置を保存
			goStartCamPos_ = scene->GetCamera().Position();
			goStartCamRot_ = scene->GetCamera().Rotation();

			// カメラ追従などの入力を無効化
			auto targetView = scene->GetRegistry().view<CameraTargetComponent>();
			for (auto e : targetView) {
				scene->GetRegistry().get<CameraTargetComponent>(e).enabled = false;
			}
			auto inputView = scene->GetRegistry().view<PlayerInputComponent>();
			for (auto e : inputView) {
				scene->GetRegistry().get<PlayerInputComponent>(e).enabled = false;
			}
		} else if (WaveManagement::IsWaveEnded()) {
			bool isTutorial = false;
			if (scene) {
				const auto& path = scene->GetStagePath();
				if (path.find("Tutorial") != std::string::npos || path.find("tutorial") != std::string::npos) {
					isTutorial = true;
				}
			}

			if (!isTutorial && s_gameClearPhase_ == 0) {
				s_gameClearPhase_ = 1;
				gameOverTimer_ = 0.0f;
				scene->SetGameTimeScale(0.0f); // 全ての時間を止める

				// BGMの切り替え
				if (auto* audio = Engine::Audio::GetInstance()) {
					if (currentBgmVoiceHandle_ != 0) {
						audio->Stop(currentBgmVoiceHandle_);
					}
					currentBgmVoiceHandle_ = audio->Play(resultBgmHandle_, true, 0.5f);
					isResultBgmPlaying_ = true;
				}

				// リザルト画面や演出に不要なワールドUIを非表示にする
				auto wsUIView = scene->GetRegistry().view<WorldSpaceUIComponent>();
				for (auto e : wsUIView) {
					scene->GetRegistry().get<WorldSpaceUIComponent>(e).enabled = false;
				}
				// ゲーム中のHUD（RectTransformを持つすべてのUI）を非表示にする
				auto uiView = scene->GetRegistry().view<RectTransformComponent>();
				for (auto e : uiView) {
					scene->GetRegistry().get<RectTransformComponent>(e).enabled = false;
				}
				// テキストや画像もすべて無効化
				auto txtView = scene->GetRegistry().view<UITextComponent>();
				for (auto e : txtView) {
					scene->GetRegistry().get<UITextComponent>(e).enabled = false;
				}
				auto imgView = scene->GetRegistry().view<UIImageComponent>();
				for (auto e : imgView) {
					scene->GetRegistry().get<UIImageComponent>(e).enabled = false;
				}

				// カメラの初期位置を保存
				goStartCamPos_ = scene->GetCamera().Position();
				goStartCamRot_ = scene->GetCamera().Rotation();

				// コアの場所を特定
				DirectX::XMFLOAT3 corePos = {0,0,0};
				auto core = scene->FindObjectByName("Core");
				if (scene->GetRegistry().valid(core) && scene->GetRegistry().all_of<TransformComponent>(core)) {
					corePos = scene->GetRegistry().get<TransformComponent>(core).translate;
				}

				// 最初の花火を1つ打ち上げる
				entt::entity fwEntity = scene->CreateEntity("GameClearFirework");
				auto& tc = scene->GetRegistry().get_or_emplace<TransformComponent>(fwEntity);
				tc.translate = corePos;
				auto& sc = scene->GetRegistry().emplace<ScriptComponent>(fwEntity);
				sc.scripts.push_back({"FireworkScript", "", nullptr});
				sc.enabled = true;
			}
		}
	}

	if (renderer) {
		auto ppParams = renderer->GetPostProcessParams();
		if (isPhase_ == PreparationPhase) {
			ppParams.prepModeBorder = 1.0f;
			ppParams.deleteModeBorder = isSellMode_ ? 1.0f : 0.0f;
		} else {
			ppParams.prepModeBorder = 0.0f;
			ppParams.deleteModeBorder = 0.0f;
		}
		renderer->SetPostProcessParams(ppParams);
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

	const bool canPlace = (!IsPlacementBlocked(scene, snappedHitPoint) && (CoinCount >= selectedObjCost_) && !IsPointerOverInstallationButton(scene));

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

	// シーン遷移中はグリッドやプレビューを一切描画しない
	auto* sm = Engine::SceneManager::GetInstance();
	if (sm && sm->GetTransitionState() != Engine::SceneManager::TransitionState::None) {
		return;
	}

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
	tr.translate = {hitPoint.x, hitPoint.y + 0.5f, hitPoint.z};
	tr.scale = {1.0f, 1.0f, 1.0f};
	const Engine::Vector4 previewColor = canPlace ? Engine::Vector4{0.6f, 1.0f, 0.6f, 0.6f} : Engine::Vector4{1.0f, 0.3f, 0.3f, 0.6f};
	renderer->DrawMesh(previewModelHandle_, previewTextureHandle_, tr, previewColor, "Toon");

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

	// SEの再生
	if (auto* audio = Engine::Audio::GetInstance()) {
		audio->Play(installationSeHandle_, false, 0.6f);
	}

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

void PhaseSystemScript::OnDestroy(entt::entity /*entity*/, GameScene* /*scene*/) {
	// BGMの停止
	if (auto* audio = Engine::Audio::GetInstance()) {
		if (currentBgmVoiceHandle_ != 0) {
			audio->Stop(currentBgmVoiceHandle_);
			currentBgmVoiceHandle_ = 0;
		}
	}

	// 次のシーンロード時に初期化順序の問題で古いフェーズ情報（特にBattlePhase）を
	// スポナーなどが誤認しないように、シーン破棄時に安全なフェーズへリセットしておく
	isPhase_ = PreparationPhase;
	NextPhase_ = PreparationPhase;
	currentPhase_ = 0;
	s_gameOverPhase_ = 0;
	s_gameClearPhase_ = 0;
}

void PhaseSystemScript::InitializeInsertPhase(GameScene* scene) {
	if (isInsertInitialized_)
		return;

	insertWaypoints_.clear();

	// 1. コアと最初のスポナーの座標を検索
	DirectX::XMFLOAT3 corePos = {0.0f, 0.0f, 0.0f};
	entt::entity coreObj = scene->FindObjectByName("Core");
	const auto& cores = scene->GetEntitiesByTag(TagType::Core);
	if (!cores.empty())
		coreObj = cores[0];

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
				spawnerPos = view.get<TransformComponent>(e).translate;
				break;
			}
		}
	} else if (scene->GetRegistry().all_of<TransformComponent>(spawnerObj)) {
		spawnerPos = scene->GetRegistry().get<TransformComponent>(spawnerObj).translate;
	}

	// 2. カメラの現在位置・回転を保存
	auto& camera = scene->GetCamera();
	originalCameraPos_ = {camera.Position().x, camera.Position().y, camera.Position().z};
	originalCameraRot_ = {camera.Rotation().x, camera.Rotation().y, camera.Rotation().z};

	// 3. ウェイポイントの構築 (コア -> 鳥瞰 -> スポナー -> 元に戻る)
	// コアからスポナーへの方向ベクトルを計算し、Yaw角を求める
	DirectX::XMFLOAT3 dir = {
		spawnerPos.x - corePos.x,
		0.0f,
		spawnerPos.z - corePos.z
	};
	float len = std::sqrt(dir.x * dir.x + dir.z * dir.z);
	float yaw = 0.0f;
	if (scene && scene->GetStagePath().find("Stage1") != std::string::npos) {
		yaw = DirectX::XM_PIDIV4;
	} else if (len > 0.001f) {
		dir.x /= len;
		dir.z /= len;
		yaw = std::atan2(dir.x, dir.z);
		
		// 45度(PI/4)単位でスナップする
		float snapInterval = DirectX::XM_PIDIV4; // 45度スナップ
		yaw = std::round(yaw / snapInterval) * snapInterval;
	}

	// Y軸回転を適用するヘルパーラムダ関数
	auto rotateOffset = [yaw](float ox, float oy, float oz) -> DirectX::XMFLOAT3 {
		return {
			ox * std::cos(yaw) + oz * std::sin(yaw),
			oy,
			-ox * std::sin(yaw) + oz * std::cos(yaw)
		};
	};

	// WP0: コアを見下ろす視点 (開始)
	DirectX::XMFLOAT3 offset0 = rotateOffset(0.0f, 35.0f, -65.0f);
	insertWaypoints_.push_back({
	    {corePos.x + offset0.x, corePos.y + offset0.y, corePos.z + offset0.z},
	    {0.55f, yaw, 0.0f}, // Pitch 31度下向き, Yawはスポナー方向
	    3.5f
    });

	// 地面の高さを取得して基準にする
	float coreGroundY = scene->GetHeightAt(corePos.x, corePos.z, 1000.0f);
	if (coreGroundY <= -999.0f) coreGroundY = corePos.y;

	float spawnerGroundY = scene->GetHeightAt(spawnerPos.x, spawnerPos.z, 1000.0f);
	if (spawnerGroundY <= -999.0f) spawnerGroundY = spawnerPos.y;

	// WP1: ステージ全体を見下ろす鳥瞰視点
	DirectX::XMFLOAT3 offset1 = rotateOffset(0.0f, 40.0f, -45.0f);
	insertWaypoints_.push_back({
	    {corePos.x + offset1.x, coreGroundY + offset1.y, corePos.z + offset1.z},
	    {0.7f, yaw, 0.0f}, // Pitch 40度下向き
	    4.0f
    });

	// WP2: 最初の敵出現地点（スポナー）にクローズアップする視点
	DirectX::XMFLOAT3 offset2 = rotateOffset(0.0f, 15.0f, -20.0f);
	insertWaypoints_.push_back({
	    {spawnerPos.x + offset2.x, spawnerGroundY + offset2.y, spawnerPos.z + offset2.z},
	    {0.4f, yaw, 0.0f}, // Pitch 22度下向き
	    4.0f
    });

	// WP3: プレイヤー操作開始位置にスムーズに戻る（EndInsertPhaseのカメラ位置と一致させる）
	// PreparationCameraのデフォルトの向き (スポナー方向のYaw) に戻す
	// ※ distance=35.0f, pitch=1.2f に合わせてオフセットを調整
	DirectX::XMFLOAT3 offset3 = rotateOffset(0.0f, 32.6f, -12.7f);
	insertWaypoints_.push_back({
	    {corePos.x + offset3.x, coreGroundY + offset3.y, corePos.z + offset3.z},
        {1.2f, yaw, 0.0f},
        2.0f
    });

	currentWaypointIndex_ = 0;
	waypointTime_ = 0.0f;
	skipHoldTime_ = 0.0f;

	// 4. スキップ案内UIを生成
	CreateSkipUI(scene);

	// 演出中はカーソルを非表示
	while (ShowCursor(FALSE) >= 0)
		;

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
	if (!input)
		return;

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
	if (t > 1.0f)
		t = 1.0f;

	// Smooth Step (3t^2 - 2t^3) による滑らかな加減速補間
	float smoothT = t * t * (3.0f - 2.0f * t);

	DirectX::XMFLOAT3 currentPos;
	currentPos.x = startPos.x + (target.position.x - startPos.x) * smoothT;
	currentPos.y = startPos.y + (target.position.y - startPos.y) * smoothT;
	currentPos.z = startPos.z + (target.position.z - startPos.z) * smoothT;

	DirectX::XMFLOAT3 currentRot;
	currentRot.x = startRot.x + (target.rotation.x - startRot.x) * smoothT;
	
	// Y軸回転の最短距離補間
	float diffY = target.rotation.y - startRot.y;
	while (diffY > DirectX::XM_PI) diffY -= DirectX::XM_2PI;
	while (diffY < -DirectX::XM_PI) diffY += DirectX::XM_2PI;
	currentRot.y = startRot.y + diffY * smoothT;

	currentRot.z = startRot.z + (target.rotation.z - startRot.z) * smoothT;

	camera.SetPosition({currentPos.x, currentPos.y, currentPos.z});
	camera.SetRotation({currentRot.x, currentRot.y, currentRot.z});

	if (t >= 1.0f) {
		currentWaypointIndex_++;
		waypointTime_ = 0.0f;
	}
}

void PhaseSystemScript::CreateSkipUI(GameScene* scene) {
	if (!scene)
		return;
	auto& reg = scene->GetRegistry();

	// 1. テキストUI
	skipPromptUI_ = scene->CreateEntity("SkipPromptUI");
	auto& rectPrompt = reg.emplace<RectTransformComponent>(skipPromptUI_);
	rectPrompt.pos = {0.0f, 400.0f}; // 画面中央下に配置
	rectPrompt.anchor = {0.5f, 0.5f};
	rectPrompt.pivot = {0.5f, 0.5f};
	rectPrompt.size = {400.0f, 50.0f};

	auto& textPrompt = reg.emplace<UITextComponent>(skipPromptUI_);
	textPrompt.text = "[S]長押しでスキップ";
	textPrompt.fontSize = 32.0f;
	textPrompt.color = {1.0f, 1.0f, 1.0f, 1.0f};
	textPrompt.fontPath = "Resources\\Fonts\\Kiwi_Maru\\KiwiMaru-Regular.ttf";
	textPrompt.outlineEnabled = true;
	textPrompt.outlineColor = {0.0f, 0.0f, 0.0f, 1.0f};
	textPrompt.outlineThickness = 2.0f;

	// 2. プログレスバー背景UI (文字の上に配置)
	skipProgressBgUI_ = scene->CreateEntity("SkipProgressBgUI");
	auto& rectBg = reg.emplace<RectTransformComponent>(skipProgressBgUI_);
	rectBg.pos = {0.0f, 360.0f}; // テキスト(400)の上
	rectBg.anchor = {0.5f, 0.5f};
	rectBg.pivot = {0.5f, 0.5f};
	rectBg.size = {254.0f, 12.0f};

	auto& imgBg = reg.emplace<UIImageComponent>(skipProgressBgUI_);
	imgBg.color = {0.1f, 0.1f, 0.1f, 0.8f};

	// 3. プログレスバー中身UI
	skipProgressUI_ = scene->CreateEntity("SkipProgressUI");
	auto& rectBar = reg.emplace<RectTransformComponent>(skipProgressUI_);
	rectBar.pos = {-125.0f, 360.0f}; // 背景の左端(-125)からスタート
	rectBar.anchor = {0.5f, 0.5f};
	rectBar.pivot = {0.0f, 0.5f}; // 左端基準でスケール
	rectBar.size = {0.0f, 8.0f};

	auto& imgBar = reg.emplace<UIImageComponent>(skipProgressUI_);
	imgBar.color = {1.0f, 0.3f, 0.3f, 0.9f};
}

void PhaseSystemScript::UpdateSkipUIProgress(GameScene* scene) {
	if (!scene || skipProgressUI_ == entt::null)
		return;
	auto& reg = scene->GetRegistry();

	if (reg.valid(skipProgressUI_) && reg.all_of<RectTransformComponent>(skipProgressUI_)) {
		auto& rect = reg.get<RectTransformComponent>(skipProgressUI_);
		float progress = skipHoldTime_ / 1.0f;
		if (progress > 1.0f)
			progress = 1.0f;
		rect.size.x = progress * 250.0f;
	}
}

void PhaseSystemScript::EndInsertPhase(GameScene* scene) {
	if (!scene)
		return;

	// UIオブジェクトの破棄
	if (skipPromptUI_ != entt::null && scene->GetRegistry().valid(skipPromptUI_)) {
		scene->DestroyObject(static_cast<uint32_t>(skipPromptUI_));
		skipPromptUI_ = entt::null;
	}
	if (skipProgressUI_ != entt::null && scene->GetRegistry().valid(skipProgressUI_)) {
		scene->DestroyObject(static_cast<uint32_t>(skipProgressUI_));
		skipProgressUI_ = entt::null;
	}
	if (skipProgressBgUI_ != entt::null && scene->GetRegistry().valid(skipProgressBgUI_)) {
		scene->DestroyObject(static_cast<uint32_t>(skipProgressBgUI_));
		skipProgressBgUI_ = entt::null;
	}

	// 終了時に標準の準備フェーズ用カメラ（コア見下ろし）に移行
	auto prepCam = scene->FindObjectByName("PreparationCamera");
	entt::entity core = scene->FindObjectByName("Core");
	const auto& cores = scene->GetEntitiesByTag(TagType::Core);
	if (!cores.empty())
		core = cores[0];

	if (scene->GetRegistry().valid(core) && scene->GetRegistry().valid(prepCam)) {
		auto& coreTc = scene->GetRegistry().get<TransformComponent>(core);

		if (scene->GetRegistry().all_of<TransformComponent>(prepCam)) {
			auto& camTc = scene->GetRegistry().get<TransformComponent>(prepCam);
			camTc.translate = coreTc.translate;
		}

		// コアからスポナーへの方向ベクトルを計算し、Yaw角を求める
		float targetYaw = 0.0f;
		DirectX::XMFLOAT3 spawnerPos = {25.0f, 0.0f, 25.0f};
		auto spawnerObj = scene->FindObjectByName("Spawner_W1_1");
		if (!scene->GetRegistry().valid(spawnerObj)) {
			auto view = scene->GetRegistry().view<NameComponent, TransformComponent>();
			for (auto e : view) {
				if (view.get<NameComponent>(e).name.find("Spawner") != std::string::npos) {
					spawnerPos = view.get<TransformComponent>(e).translate;
					break;
				}
			}
		} else if (scene->GetRegistry().all_of<TransformComponent>(spawnerObj)) {
			spawnerPos = scene->GetRegistry().get<TransformComponent>(spawnerObj).translate;
		}

		DirectX::XMFLOAT3 dir = { spawnerPos.x - coreTc.translate.x, 0.0f, spawnerPos.z - coreTc.translate.z };
		float len = std::sqrt(dir.x * dir.x + dir.z * dir.z);
		if (scene && scene->GetStagePath().find("Stage1") != std::string::npos) {
			targetYaw = DirectX::XM_PIDIV4;
		} else if (len > 0.001f) {
			targetYaw = std::atan2(dir.x, dir.z);
			float snapInterval = DirectX::XM_PIDIV4; // 45度スナップ
			targetYaw = std::round(targetYaw / snapInterval) * snapInterval;
		}

		if (scene->GetRegistry().all_of<PlayerInputComponent>(prepCam)) {
			auto& camPi = scene->GetRegistry().get<PlayerInputComponent>(prepCam);
			camPi.cameraPitch = 1.2f;
			camPi.cameraYaw = targetYaw;

			auto& camera = scene->GetCamera();
			auto rot = camera.Rotation();
			rot.x = 1.2f;
			rot.y = targetYaw;
			camera.SetRotation(rot);
		}

		if (scene->GetRegistry().all_of<CameraTargetComponent>(prepCam)) {
			auto& ct = scene->GetRegistry().get<CameraTargetComponent>(prepCam);
			ct.distance = 35.0f;
			ct.height = 0.0f;    // ★統一: 全準備フェーズで同じ高さ
		}

		// 地面の高さを取得
		float coreGroundY = scene->GetHeightAt(coreTc.translate.x, coreTc.translate.z, 1000.0f);
		if (coreGroundY <= -999.0f) coreGroundY = coreTc.translate.y;

		// ★修正: CameraFollowSystemの計算結果と一致させてカメラの跳ね上がりを防止
		// pitch=1.2, targetYaw, distance=35, height=0 から算出:
		auto rotateOffset = [targetYaw](float ox, float oy, float oz) -> DirectX::XMFLOAT3 {
			return {
				ox * std::cos(targetYaw) + oz * std::sin(targetYaw),
				oy,
				-ox * std::sin(targetYaw) + oz * std::cos(targetYaw)
			};
		};
		DirectX::XMFLOAT3 offset = rotateOffset(0.0f, 32.6f, -12.7f);

		auto& camera = scene->GetCamera();
		camera.SetPosition({coreTc.translate.x + offset.x, coreGroundY + offset.y, coreTc.translate.z + offset.z});
		camera.SetRotation({1.2f, targetYaw, 0.0f});
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
	while (ShowCursor(TRUE) < 0)
		;

	// 通常の準備フェーズに移行
	isPhase_ = PreparationPhase;
	NextPhase_ = PreparationPhase;
	preIsPhase_ = PreparationPhase;

	isInsertInitialized_ = false;
}

void PhaseSystemScript::DrawUI(entt::entity /*entity*/, GameScene* /*scene*/) {
	// SDFUIなどの描画は Update フェーズ内で処理されるため、
	// ここは空実装としておく
}
bool Game::PhaseSystemScript::isSkillTreeOpen_ = false;
REGISTER_SCRIPT(PhaseSystemScript);

} // namespace Game
