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

#include "PhaseSystemScript.h"

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

	currentWave_ = 0;
	previousWave_ = -1;
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
			if (phase == PhaseSystemScript::PreparationPhase || phase == PhaseSystemScript::BattlePhase) {
				isPrepOrBattle = true;
			}
		}
	}

	if (renderer && (isEditorMode || isPrepOrBattle)) {
		for (size_t wi = 0; wi < enemySpawners_.size(); ++wi) {
			// ゲームプレイ中は現在の（次に来る）ウェーブのものだけ表示する
			if (!isEditorMode) {
				int targetWave = 0;
				auto phase = PhaseSystemScript::IsPhase();
				if (phase == PhaseSystemScript::PreparationPhase) {
					targetWave = PhaseSystemScript::GetCurrentPhase();
				} else if (phase == PhaseSystemScript::BattlePhase) {
					targetWave = PhaseSystemScript::GetCurrentPhase() - 1;
				}

				if (static_cast<int>(wi) != targetWave) {
					continue;
				}
			}

			for (entt::entity spawnerEntity : enemySpawners_[wi]) {
				if (scene->GetRegistry().valid(spawnerEntity)) {
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
						auto cp = cam.Position();
						Engine::Vector3 camPos = { cp.x, cp.y, cp.z };
						Engine::Vector3 d = { camPos.x - p.x, camPos.y - p.y, camPos.z - p.z };
						float len = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
						if (len > 1e-6f) { d.x /= len; d.y /= len; d.z /= len; } else { d = {0,0,1}; }
						float yaw = std::atan2(d.x, d.z);
						float pitch = std::atan2(-d.y, std::sqrt(d.x * d.x + d.z * d.z));

						Engine::Vector4 planeColor = {1.0f, 1.0f, 1.0f, 1.0f};

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

						// スクリーン投影座標を計算
						DirectX::XMFLOAT3 pFloat3 = { p.x, p.y, p.z };
						DirectX::XMMATRIX view = cam.View();
						DirectX::XMMATRIX proj = cam.Proj();
						DirectX::XMMATRIX world = DirectX::XMMatrixIdentity();
						DirectX::XMVECTOR worldPos = DirectX::XMLoadFloat3(&pFloat3);
						DirectX::XMVECTOR screenPos = DirectX::XMVector3Project(worldPos, 0, 0, (float)Engine::WindowDX::kW, (float)Engine::WindowDX::kH, 0.0f, 1.0f, proj, view, world);

						DirectX::XMFLOAT3 screenPosFl;
						DirectX::XMStoreFloat3(&screenPosFl, screenPos);

						// スクリーン座標が有効な範囲かチェック
						DirectX::XMMATRIX vp = view * proj;
						DirectX::XMVECTOR clipPos = DirectX::XMVector3TransformCoord(worldPos, vp);
						float cz = DirectX::XMVectorGetZ(clipPos);
						bool isOnScreen = (cz >= 0.0f && cz <= 1.0f && screenPosFl.x >= 0.0f && screenPosFl.x < (float)Engine::WindowDX::kW && screenPosFl.y >= 0.0f && screenPosFl.y < (float)Engine::WindowDX::kH);

						float scale = 2.0f;  // デフォルトスケール

						// スクリーン外の場合、アイコンを小さくして画面の端に配置
						if (!isOnScreen) {
							scale = 0.5f;  // 縮小スケール

							// スクリーン座標を画面の端にクランプ
							float margin = 50.0f;  // 画面端からのマージン
							float clampedX = screenPosFl.x;
							float clampedY = screenPosFl.y;

							// X座標のクランプ
							if (clampedX < margin) {
								clampedX = margin;
							} else if (clampedX > (float)Engine::WindowDX::kW - margin) {
								clampedX = (float)Engine::WindowDX::kW - margin;
							}

							// Y座標のクランプ
							if (clampedY < margin) {
								clampedY = margin;
							} else if (clampedY > (float)Engine::WindowDX::kH - margin) {
								clampedY = (float)Engine::WindowDX::kH - margin;
							}

							// スクリーン座標をワールド座標に逆投影するための計算
							DirectX::XMMATRIX invProj = DirectX::XMMatrixInverse(nullptr, proj);
							DirectX::XMMATRIX invView = DirectX::XMMatrixInverse(nullptr, view);

							// NDC座標に変換
							float ndcX = (clampedX / (float)Engine::WindowDX::kW) * 2.0f - 1.0f;
							float ndcY = 1.0f - (clampedY / (float)Engine::WindowDX::kH) * 2.0f;

							DirectX::XMVECTOR ndcPos = DirectX::XMVectorSet(ndcX, ndcY, cz, 1.0f);
							DirectX::XMVECTOR projPos = DirectX::XMVector3Transform(ndcPos, invProj);
							DirectX::XMVECTOR worldEdgePos = DirectX::XMVector3Transform(projPos, invView);

							DirectX::XMFLOAT3 edgePosFloat;
							DirectX::XMStoreFloat3(&edgePosFloat, worldEdgePos);
							p.x = edgePosFloat.x;
							p.y = edgePosFloat.y;
							p.z = edgePosFloat.z;
						}

						Engine::Transform planeTr;
						planeTr.translate = { p.x, p.y, p.z };
						planeTr.rotate = { pitch, yaw, 0.0f };
						planeTr.scale = { tc->scale.x * scale, tc->scale.y * scale, tc->scale.z * scale };
						renderer->DrawMesh(planeMeshHandle, spawnerTexHandle, planeTr, planeColor, "Toon");
					}
				}
			}
		}
	}

	if (currentWave_ != previousWave_) {
		if (scene->IsPlaying()) {
			// ウェーブ開始時の初期化
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

	// 現在のウェーブのスポナーだけを有効化する
	for (entt::entity e : enemySpawners_[currentWave]) {
		if (scene->GetRegistry().valid(e)) {
			if (auto* sc = scene->GetRegistry().try_get<ScriptComponent>(e)) {
				sc->enabled = true;
				for (auto& entry : sc->scripts) {
					if (entry.instance) {
						entry.instance->Start(e, scene);
					}
				}
			}
		}
	}
}

int WaveManagement::GetTotalMaxEnemies(GameScene* /*scene*/) {
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