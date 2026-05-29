#include "WaveManagement.h"
#include "ObjectTypes.h"
#include "Scenes/GameScene.h"
#include "ScriptEngine.h"
#include "EnemySpawnerScript.h"
#include "../../Engine/Renderer.h"
#include "../../Engine/SceneManager.h"
#include "../../Engine/WindowDX.h"
#include "../../Engine/ThirdParty/nlohmann/json.hpp"
#ifdef USE_IMGUI
#include "../../externals/imgui/imgui.h"
#endif
#include <cmath>
#include <iostream>
#include <algorithm>

#include "PhaseSystemScript.h"
#include "TutorialScript.h"
#include "PlayerScript.h" // ★追加

using json = nlohmann::json;

namespace Game {

int WaveManagement::currentWave_ = -1;

void WaveManagement::Start(entt::entity entity, GameScene* scene) {
	if (!scene) return;

	instance_ = this;

	// ↓これを追加してシーンの参照を正しく持たせる
	cachedScene_ = scene;

	managerEntity_ = entity;
	currentWave_ = -1;
	previousWave_ = -2;
	isEnded_ = false;


	// 名前に基づいてエンティティを解決
	enemySpawners_.clear();
	for (const auto& waveNames : enemySpawnerNames_) {
		std::vector<entt::entity> waveSpawners;
		for (const auto& name : waveNames) {
			entt::entity e = scene->FindObjectByName(name);
			if (scene->GetRegistry().valid(e)) {
				if (auto* sc = scene->GetRegistry().try_get<ScriptComponent>(e)) {
					sc->enabled = false;
				}
				waveSpawners.push_back(e);
			}
		}
		enemySpawners_.push_back(waveSpawners);
	}

	currentWave_ = -1;
	previousWave_ = -1;
	isWaveInitialized_ = false;
}

void WaveManagement::Update(entt::entity entity, GameScene* scene, float /*dt*/) {
	cachedScene_ = scene;
	managerEntity_ = entity;

	auto* renderer = Engine::Renderer::GetInstance();
	bool isEditorMode = false;
	bool isPrepOrBattle = false;
	if (scene) {
		if (!scene->IsPlaying()) {
			isEditorMode = true;
		} else {
			auto phase = PhaseSystemScript::IsPhase();
			if (phase == PhaseSystemScript::PreparationPhase || phase == PhaseSystemScript::BattlePhase || phase == PhaseSystemScript::InsertPhase) {
				isPrepOrBattle = true;
			}
		}
	}

	// 毎フレーム方角バーの描画フラグをリセット
	static bool compassBarDrawn = false;
	compassBarDrawn = false;

	if (renderer && (isEditorMode || isPrepOrBattle) && !PhaseSystemScript::IsResultSequenceActive() && !PlayerScript::IsHelpOpen()) {
		for (size_t wi = 0; wi < enemySpawners_.size(); ++wi) {
			// ゲームプレイ中は現在の（次に来る）ウェーブのものだけ表示する
				if (!isEditorMode) {
					int targetWave = 0;
					auto phase = PhaseSystemScript::IsPhase();
					if (phase == PhaseSystemScript::PreparationPhase || phase == PhaseSystemScript::InsertPhase) {
						targetWave = PhaseSystemScript::GetCurrentPhase();
					} else if (phase == PhaseSystemScript::BattlePhase) {
						targetWave = currentWave_;
					}

					// (チュートリアル中のアイコン非表示処理は削除しました)

					if (static_cast<int>(wi) != targetWave) {
						continue;
					}
				}

			for (entt::entity spawnerEntity : enemySpawners_[wi]) {
				if (scene->GetRegistry().valid(spawnerEntity)) {
					
					// ★追加: スポナーの足元を照らすポイントライトを付与（黄色/オレンジ系）
					if (!scene->GetRegistry().all_of<PointLightComponent>(spawnerEntity)) {
						auto& pl = scene->GetRegistry().emplace<PointLightComponent>(spawnerEntity);
						pl.color = { 1.0f, 0.8f, 0.2f };
						pl.intensity = 3.5f;
						pl.range = 8.0f;
						pl.offset = { 0.0f, 1.5f, 0.0f }; // ★ ライトを少し上にオフセット
						pl.enabled = true;
					}

					// スポナーの位置にプレビューを描画
					if (auto* tc = scene->GetRegistry().try_get<TransformComponent>(spawnerEntity)) {
						Engine::Matrix4x4 wm = scene->GetWorldMatrix(static_cast<int>(spawnerEntity));
						Engine::Vector3 p = { wm.m[3][0], wm.m[3][1], wm.m[3][2] };

						// ビルボード付きのplaneを描画
						static uint32_t planeMeshHandle = 0;
						static uint32_t defaultTexHandle = 0;
						if (planeMeshHandle == 0) {
							planeMeshHandle = renderer->LoadObjMesh("Resources/Models/plane.obj");
							defaultTexHandle = renderer->LoadTexture2D("Resources/Textures/white1x1.png");
						}

						uint32_t spawnerTexHandle = defaultTexHandle;

						Engine::Camera& cam = scene->GetCamera();
						DirectX::XMFLOAT3 camRot = cam.Rotation();
						float yaw = camRot.y + 3.1415926535f;
						float pitch = -camRot.x;

						// ★変更: アイコン自体を発光させるため、HDRカラー（1.0以上の値）を設定
						Engine::Vector4 planeColor = { 2.5f, 2.2f, 0.5f, 1.0f };

						if (auto* sc = scene->GetRegistry().try_get<ScriptComponent>(spawnerEntity)) {
							for (auto& entry : sc->scripts) {
								if (entry.scriptPath == "EnemySpawnerScript") {
									if (!entry.instance) {
										entry.instance = ScriptEngine::GetInstance()->CreateScript(entry.scriptPath);
										if (entry.instance) {
											entry.instance->DeserializeParameters(entry.parameterData);
										}
									}
									if (entry.instance) {
										auto* spawner = static_cast<EnemySpawnerScript*>(entry.instance.get());
										if (spawner->enemyScriptPath == "Warrior") {
											spawnerTexHandle = renderer->LoadTexture2D("Resources/Textures/EnemyLogo/Warriar.png", false);
										} else if (spawner->enemyScriptPath == "Guardian") {
											spawnerTexHandle = renderer->LoadTexture2D("Resources/Textures/EnemyLogo/Guardian.png", false);
										} else if (spawner->enemyScriptPath == "Gunner") {
											spawnerTexHandle = renderer->LoadTexture2D("Resources/Textures/EnemyLogo/Gunner.png", false);
										}
									}
								}
							}
						}

						DirectX::XMFLOAT3 pFloat3 = { p.x, p.y, p.z };
						DirectX::XMMATRIX view = cam.View();

						// 距離に応じた縮尺の計算 (元のスポナーの座標 pFloat3 とカメラの距離)
						DirectX::XMFLOAT3 camPos = cam.Position();
						float dx = camPos.x - pFloat3.x;
						float dy = camPos.y - pFloat3.y;
						float dz = camPos.z - pFloat3.z;
						float distance = std::sqrt(dx * dx + dy * dy + dz * dz);

						// 3Dアイコンは常にスポナーの位置に描画する (画面に寄せる処理は廃止)
						Engine::Transform planeTr;
						
						// 地形の高さを取得し、地面から一定の高さ(5.0f)にアイコンを表示して埋まらないようにする
						float groundY = scene->GetHeightAt(pFloat3.x, pFloat3.z, 1000.0f);
						if (groundY <= -999.0f) groundY = pFloat3.y; // 地形がない場合はフォールバック
						
						planeTr.translate = { pFloat3.x, groundY + 5.0f, pFloat3.z };
						planeTr.rotate = { pitch, yaw, 0.0f };
						planeTr.scale = { tc->scale.x * 2.0f, tc->scale.y * 2.0f, tc->scale.z * 2.0f };
						renderer->DrawMesh(planeMeshHandle, spawnerTexHandle, planeTr, planeColor, "Unlit");

						// 2D Sprite表示 (距離が30m以上のとき画面上部にコンパスバー＋アイコン表示)
						if (distance >= 30.0f) {
							// ====== テクスチャパス設定 (ここを変えるだけで差し替え可能) ======
							static const char* kCompassBarTexPath  = "Resources/Textures/Direction.png";  // 方角バー背景
							static const char* kIndicatorTexPath   = "Resources/Textures/white1x1.png";  // 縦線インジケーター

							// ====== テクスチャハンドルのロード (初回のみ) ======
							static uint32_t compassBarTexHandle = 0;
							static uint32_t indicatorTexHandle  = 0;
							if (compassBarTexHandle == 0) {
								compassBarTexHandle = renderer->LoadTexture2D(kCompassBarTexPath, false);
							}
							if (indicatorTexHandle == 0) {
								indicatorTexHandle = renderer->LoadTexture2D(kIndicatorTexPath, false);
							}

							// カメラの前方ベクトルを求める (ビュー行列の逆行列の3行目)
							DirectX::XMMATRIX invView = DirectX::XMMatrixInverse(nullptr, view);
							DirectX::XMVECTOR camForwardVec = invView.r[2];

							DirectX::XMFLOAT3 camForward;
							DirectX::XMStoreFloat3(&camForward, camForwardVec);

							// カメラからスポナーへのベクトル
							float sdx = pFloat3.x - camPos.x;
							float sdz = pFloat3.z - camPos.z;

							// 水平面(X-Z)における角度を計算
							float camAngle = std::atan2(camForward.z, camForward.x);
							float spawnerAngle = std::atan2(sdz, sdx);

							float angleDiff = spawnerAngle - camAngle;
							// 角度差を [-PI, PI] にクランプ
							const float PI_VAL = 3.1415926535f;
							while (angleDiff < -PI_VAL) angleDiff += 2.0f * PI_VAL;
							while (angleDiff > PI_VAL) angleDiff -= 2.0f * PI_VAL;

							// 画面幅の 30% から 70% の範囲に角度をマッピングする
							float screenW = (float)Engine::WindowDX::kW;
							float margin = screenW * 0.3f;
							float startX = margin;
							float endX = screenW - margin;
							float barWidth = endX - startX;

							// 左右の逆転を修正するため、マッピング方向を反転
							float t = (PI_VAL - angleDiff) / (2.0f * PI_VAL);
							float targetX = startX + t * barWidth;

							// ====== レイアウト定数 ======
							const float barH       = 70.0f;    // 方角バーの高さ
							const float barY       = 20.0f;   // 方角バーのY位置
							const float iconSize   = 50.0f;   // 敵アイコンのサイズ
							const float iconY      = barY + (barH - iconSize) * 0.5f; // アイコンをバーの中央に配置

							// ① 方角バー背景を描画 (全スポナーで共通、1回だけ描画)
							//    カメラのヨー角に応じてUVをスクロールさせ、方角を反映する
							if (!compassBarDrawn) {
								// カメラの+Z（北）方向を向いたときにテクスチャの真ん中（N）が中央に来るようにオフセットを調整
								float camYawNorm = (camAngle - (PI_VAL / 2.0f)) / (2.0f * PI_VAL);

								Engine::Renderer::SpriteDesc barDesc;
								barDesc.x = startX;
								barDesc.y = barY;
								barDesc.w = barWidth;
								barDesc.h = barH;
								barDesc.color = {1.0f, 1.0f, 1.0f, 0.6f};
								barDesc.uvScaleOffset = {1.0f, 1.0f, camYawNorm, 0.0f}; // X方向にUVスクロール
								barDesc.rotationRad = 0.0f;
								barDesc.layer = 99;
								renderer->DrawSprite(compassBarTexHandle, barDesc);
								compassBarDrawn = true;
							}

							// ② 敵アイコンを描画 (バーの中央)
							{
								Engine::Renderer::SpriteDesc iconDesc;
								iconDesc.x = targetX - iconSize * 0.5f;
								iconDesc.y = iconY;
								iconDesc.w = iconSize;
								iconDesc.h = iconSize;
								iconDesc.color = {1.0f, 1.0f, 1.0f, 1.0f};
								iconDesc.rotationRad = 0.0f;
								iconDesc.layer = 101; // バーより手前
								renderer->DrawSprite(spawnerTexHandle, iconDesc);
							}
						}
					}
				}
			}
		}
	}

	if (currentWave_ != previousWave_) {
		isWaveInitialized_ = false;
		if (scene->IsPlaying()) {
			currentWaveKilled_ = 0;
			lastAliveCount_ = 0;
			lastTotalSpawned_ = 0;
		}

		if (scene->IsPlaying() && currentWave_ == -1) {
			// 準備フェーズなどに移行した場合、残存している敵を全て消去する
			const auto& enemies = scene->GetEntitiesByTag(TagType::Enemy);
			std::vector<entt::entity> toDestroy(enemies.begin(), enemies.end());
			for (auto e : toDestroy) {
				if (scene->GetRegistry().valid(e)) {
					scene->DestroyObject(static_cast<uint32_t>(e));
				}
			}

			// ★ 戦闘フェーズ終わり（ウェーブ終了時）で、直前のウェーブが最終ウェーブだったらクリアにする
			if (previousWave_ >= static_cast<int>(enemySpawners_.size()) - 1 && !enemySpawners_.empty()) {
				isEnded_ = true;
			}
		}

		SpawnSpanner(currentWave_, scene);
	}

	// 敵の死亡トラッキング
	if (scene->IsPlaying() && currentWave_ >= 0) {
		int aliveCount = 0;
		{
			auto view = scene->GetRegistry().view<TagComponent>();
			for (auto e : view) {
				if (view.get<TagComponent>(e).tag == TagType::Enemy) {
					aliveCount++;
				}
			}
		}
		int currentTotalSpawned = 0;

		if (currentWave_ < static_cast<int>(enemySpawners_.size())) {
			for (entt::entity e : enemySpawners_[currentWave_]) {
				if (scene->GetRegistry().valid(e)) {
					if (auto* sc = scene->GetRegistry().try_get<ScriptComponent>(e)) {
						for (auto& entry : sc->scripts) {
							if (entry.scriptPath == "EnemySpawnerScript" && entry.instance) {
								auto* sp = static_cast<EnemySpawnerScript*>(entry.instance.get());
								if (sp->GetCurrentWave() > currentWave_) {
									currentTotalSpawned += sp->GetMaxCount();
								} else if (sp->GetCurrentWave() == currentWave_) {
									currentTotalSpawned += sp->GetSpawnedCount();
								}
							}
						}
					}
				}
			}
		}

		int spawnedThisFrame = currentTotalSpawned - lastTotalSpawned_;
		if (spawnedThisFrame < 0) spawnedThisFrame = 0;

		int killedThisFrame = (lastAliveCount_ + spawnedThisFrame) - aliveCount;
		if (killedThisFrame > 0) {
			currentWaveKilled_ += killedThisFrame;
		}

		lastAliveCount_ = aliveCount;
		lastTotalSpawned_ = currentTotalSpawned;
	}

	previousWave_ = currentWave_;
}

void WaveManagement::OnDestroy(entt::entity /*entity*/, GameScene* /*scene*/) {
	if (instance_ == this) {
		instance_ = nullptr;
	}
}

void WaveManagement::SpawnSpanner(int currentWave, GameScene* scene) {
	if (!scene) return;

	// すべてのスポナーを無効化する
	for (auto& waveSpawners : enemySpawners_) {
		for (entt::entity e : waveSpawners) {
			if (scene->GetRegistry().valid(e)) {
				if (auto* sc = scene->GetRegistry().try_get<ScriptComponent>(e)) {
					sc->enabled = false;
				}
			}
		}
	}

	if (currentWave < 0 || currentWave >= static_cast<int>(enemySpawners_.size())) return;

	// 現在のウェーブのスポナーだけを有効化し、スクリプトを初期化する
	for (entt::entity e : enemySpawners_[currentWave]) {
		if (scene->GetRegistry().valid(e)) {
			if (auto* sc = scene->GetRegistry().try_get<ScriptComponent>(e)) {
				sc->enabled = true;
				for (auto& entry : sc->scripts) {
					if (entry.scriptPath == "EnemySpawnerScript") {
						if (!entry.instance) {
							entry.instance = ScriptEngine::GetInstance()->CreateScript(entry.scriptPath);
							if (entry.instance) {
								entry.instance->DeserializeParameters(entry.parameterData);
							}
						}
						if (entry.instance) {
							entry.instance->Start(e, scene);
						}
					}
				}
			}
		}
	}

	// スクリプト初期化後に敵数を計算する
	if (scene->IsPlaying()) {
		currentWaveMax_ = 0;
		for (entt::entity e : enemySpawners_[currentWave]) {
			if (scene->GetRegistry().valid(e)) {
				if (auto* sc = scene->GetRegistry().try_get<ScriptComponent>(e)) {
					for (auto& entry : sc->scripts) {
						if (entry.scriptPath == "EnemySpawnerScript" && entry.instance) {
							currentWaveMax_ += static_cast<EnemySpawnerScript*>(entry.instance.get())->GetMaxCount();
						}
					}
				}
			}
		}
		isWaveInitialized_ = true;
	}
}

int WaveManagement::GetTotalMaxEnemies(GameScene* scene) {
	// ウェーブが初期化されていない場合は再計算
	if (!isWaveInitialized_ && scene && scene->IsPlaying()) {
		currentWaveMax_ = 0;
		if (currentWave_ >= 0 && currentWave_ < static_cast<int>(enemySpawners_.size())) {
			for (entt::entity e : enemySpawners_[currentWave_]) {
				if (scene->GetRegistry().valid(e)) {
					if (auto* sc = scene->GetRegistry().try_get<ScriptComponent>(e)) {
						for (auto& entry : sc->scripts) {
							if (entry.scriptPath == "EnemySpawnerScript" && entry.instance) {
								currentWaveMax_ += static_cast<EnemySpawnerScript*>(entry.instance.get())->GetMaxCount();
							}
						}
					}
				}
			}
		}
		isWaveInitialized_ = true;
	}
	return currentWaveMax_;
}

int WaveManagement::GetTotalRemainingEnemies(GameScene* /*scene*/) {
	int remaining = currentWaveMax_ - currentWaveKilled_;
	return (remaining > 0) ? remaining : 0;
}

#if defined(USE_IMGUI) && !defined(NDEBUG)
void WaveManagement::OnEditorUI() {
	if (!cachedScene_) {
		auto* sm = Engine::SceneManager::GetInstance();
		if (sm && sm->Current()) {
			cachedScene_ = dynamic_cast<GameScene*>(sm->Current());
		}
	}

	if (!cachedScene_) {
		ImGui::Text("シーンがキャッシュされていません。再生するかエディタで更新してください。");
		return;
	}

	// エディタ起動時やプレイ終了時など、エンティティのリストが未構築・無効な場合に名前から復元する
	if (enemySpawners_.size() != enemySpawnerNames_.size()) {
		enemySpawners_.resize(enemySpawnerNames_.size());
	}
	for (size_t wi = 0; wi < enemySpawners_.size(); ++wi) {
		if (enemySpawners_[wi].size() != enemySpawnerNames_[wi].size()) {
			enemySpawners_[wi].resize(enemySpawnerNames_[wi].size(), static_cast<entt::entity>(entt::null));
		}
		for (size_t si = 0; si < enemySpawners_[wi].size(); ++si) {
			if (!cachedScene_->GetRegistry().valid(enemySpawners_[wi][si])) {
				enemySpawners_[wi][si] = cachedScene_->FindObjectByName(enemySpawnerNames_[wi][si]);
			}
		}
	}

	ImGui::SeparatorText("ウェーブ管理 (Wave Management)");

	if (ImGui::Button("ウェーブを追加 (Add Wave)")) {
		enemySpawners_.push_back({});
		enemySpawnerNames_.push_back({});
	}

	for (size_t wi = 0; wi < enemySpawners_.size(); ++wi) {
		ImGui::PushID(static_cast<int>(wi));
		bool isNodeOpen = ImGui::TreeNodeEx(("Wave " + std::to_string(wi + 1)).c_str(), ImGuiTreeNodeFlags_DefaultOpen);

		if (isNodeOpen) {
			if (ImGui::Button("スポナーを追加 (Add Spawner)")) {
				std::string name = "Spawner_W" + std::to_string(wi + 1) + "_" + std::to_string(enemySpawners_[wi].size() + 1);
				entt::entity spawner = cachedScene_->CreateEntity(name);

				if (auto* tc = cachedScene_->GetRegistry().try_get<TransformComponent>(spawner)) {
					tc->translate = {0, 0, 0};
				}

				auto& sc = cachedScene_->GetRegistry().emplace<ScriptComponent>(spawner);
				sc.scripts.push_back({"EnemySpawnerScript", "", nullptr});
				sc.enabled = true;

				enemySpawners_[wi].push_back(spawner);
				enemySpawnerNames_[wi].push_back(name);
			}
			ImGui::SameLine();
			if (ImGui::Button("ウェーブを削除")) {
				// ウェーブに含まれるすべてのスポナーエンティティを破棄する
				for (entt::entity spawner : enemySpawners_[wi]) {
					if (cachedScene_->GetRegistry().valid(spawner)) {
						cachedScene_->DestroyObject(static_cast<uint32_t>(spawner));
					}
				}
				enemySpawners_.erase(enemySpawners_.begin() + wi);
				enemySpawnerNames_.erase(enemySpawnerNames_.begin() + wi);
				ImGui::TreePop();
				ImGui::PopID();
				--wi;
				continue;
			}

			// スポナーリストの描画
			for (size_t si = 0; si < enemySpawners_[wi].size(); ++si) {
				entt::entity spawner = enemySpawners_[wi][si];
				if (!cachedScene_->GetRegistry().valid(spawner)) continue;

				auto* nc = cachedScene_->GetRegistry().try_get<NameComponent>(spawner);
				std::string sname = nc ? nc->name : "Spawner";

				ImGui::PushID(static_cast<int>(si));
				bool isSpawnerNodeOpen = ImGui::TreeNode(sname.c_str());

				ImGui::SameLine();
				if (ImGui::Button("選択(Select)")) {
					cachedScene_->SetSelectedEntity(spawner);
					cachedScene_->GetSelectedEntities().clear();
					cachedScene_->GetSelectedEntities().insert(spawner);
				}

				if (isSpawnerNodeOpen) {
					// 座標設定
					if (auto* tc = cachedScene_->GetRegistry().try_get<TransformComponent>(spawner)) {
						ImGui::DragFloat3("Position", &tc->translate.x, 0.1f);
					}

					if (ImGui::Button("スポナーを削除")) {
						cachedScene_->DestroyObject(static_cast<uint32_t>(spawner));
						enemySpawners_[wi].erase(enemySpawners_[wi].begin() + si);
						enemySpawnerNames_[wi].erase(enemySpawnerNames_[wi].begin() + si);
						ImGui::TreePop();
						ImGui::PopID();
						--si;
						continue;
					}

					// EnemySpawnerScript の設定を呼び出す
					if (auto* sc = cachedScene_->GetRegistry().try_get<ScriptComponent>(spawner)) {
						for (auto& entry : sc->scripts) {
							if (entry.scriptPath == "EnemySpawnerScript") {
								if (!entry.instance) {
									entry.instance = ScriptEngine::GetInstance()->CreateScript(entry.scriptPath);
									if (entry.instance) {
										entry.instance->DeserializeParameters(entry.parameterData);
									}
								}
								if (entry.instance) {
									entry.instance->OnEditorUI();
									entry.parameterData = entry.instance->SerializeParameters();
								}
							}
						}
					}

					ImGui::TreePop();
				}
				ImGui::PopID();
			}
			ImGui::TreePop();
		}
		ImGui::PopID();
	}
}
#else
void WaveManagement::OnEditorUI() {}
#endif

std::string WaveManagement::SerializeParameters() {
	json j;

	// キャッシュされているシーンが存在し、エンティティが有効であれば名前を最新のものに更新する
	// （ヒエラルキー等でスポナーの名前が変更された場合に対応するため）
	if (cachedScene_) {
		for (size_t wi = 0; wi < enemySpawners_.size(); ++wi) {
			for (size_t si = 0; si < enemySpawners_[wi].size(); ++si) {
				entt::entity spawner = enemySpawners_[wi][si];
				if (cachedScene_->GetRegistry().valid(spawner)) {
					if (auto* nc = cachedScene_->GetRegistry().try_get<NameComponent>(spawner)) {
						enemySpawnerNames_[wi][si] = nc->name;
					}
				}
			}
		}
	}

	// 最新のスポナー名リストを保存する
	j["spawners"] = enemySpawnerNames_;
	return j.dump();
}

void WaveManagement::DeserializeParameters(const std::string& data) {
	if (data.empty()) return;
	try {
		json j = json::parse(data);
		if (j.contains("spawners")) {
			enemySpawnerNames_ = j["spawners"].get<std::vector<std::vector<std::string>>>();
		}
	} catch (const std::exception& e) {
		std::cerr << "WaveManagement Deserialize Error: " << e.what() << "\n";
	}
}

REGISTER_SCRIPT(WaveManagement);

} // namespace Game