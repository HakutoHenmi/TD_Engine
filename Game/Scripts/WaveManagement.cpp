#include "WaveManagement.h"
#include "ObjectTypes.h"
#include "Scenes/GameScene.h"
#include "ScriptEngine.h"
#include "EnemySpawnerScript.h"
#include "../../Engine/Renderer.h"
#include "../../Engine/SceneManager.h"
#include "../../Engine/ThirdParty/nlohmann/json.hpp"
#ifdef USE_IMGUI
#include "../../externals/imgui/imgui.h"
#endif
#include <cmath>
#include <iostream>

#include "PhaseSystemScript.h"

using json = nlohmann::json;

namespace Game {

int WaveManagement::currentWave_ = 0;

void WaveManagement::Start(entt::entity /*entity*/, GameScene* scene) {
	if (!scene) return;

	// 名前に基づいてエンティティを解決
	enemySpawners_.clear();
	for (const auto& waveNames : enemySpawnerNames_) {
		std::vector<entt::entity> waveSpawners;
		for (const auto& name : waveNames) {
			entt::entity e = scene->FindObjectByName(name);
			if (scene->GetRegistry().valid(e)) {
				waveSpawners.push_back(e);
			}
		}
		enemySpawners_.push_back(waveSpawners);
	}
}

void WaveManagement::Update(entt::entity /*entity*/, GameScene* scene, float /*dt*/) {
	cachedScene_ = scene;

	#if defined(USE_IMGUI) && !defined(NDEBUG) 
	auto* renderer = Engine::Renderer::GetInstance();
	if (renderer && scene) {
		for (size_t wi = 0; wi < enemySpawners_.size(); ++wi) {
			for (entt::entity spawnerEntity : enemySpawners_[wi]) {
				if (scene->GetRegistry().valid(spawnerEntity)) {
					// スポナーの位置にキューブを描画
					if (auto* tc = scene->GetRegistry().try_get<TransformComponent>(spawnerEntity)) {
						float s = 0.5f;
						Engine::Vector3 p = { tc->translate.x, tc->translate.y, tc->translate.z };
						Engine::Vector4 c = { 1.0f, 0.5f, 0.0f, 1.0f };
						// 底面
						renderer->DrawLine3D({p.x - s, p.y - s, p.z - s}, {p.x + s, p.y - s, p.z - s}, c, true);
						renderer->DrawLine3D({p.x + s, p.y - s, p.z - s}, {p.x + s, p.y - s, p.z + s}, c, true);
						renderer->DrawLine3D({p.x + s, p.y - s, p.z + s}, {p.x - s, p.y - s, p.z + s}, c, true);
						renderer->DrawLine3D({p.x - s, p.y - s, p.z + s}, {p.x - s, p.y - s, p.z - s}, c, true);
						// 上面
						renderer->DrawLine3D({p.x - s, p.y + s, p.z - s}, {p.x + s, p.y + s, p.z - s}, c, true);
						renderer->DrawLine3D({p.x + s, p.y + s, p.z - s}, {p.x + s, p.y + s, p.z + s}, c, true);
						renderer->DrawLine3D({p.x + s, p.y + s, p.z + s}, {p.x - s, p.y + s, p.z + s}, c, true);
						renderer->DrawLine3D({p.x - s, p.y + s, p.z + s}, {p.x - s, p.y + s, p.z - s}, c, true);
						// 側面
						renderer->DrawLine3D({p.x - s, p.y - s, p.z - s}, {p.x - s, p.y + s, p.z - s}, c, true);
						renderer->DrawLine3D({p.x + s, p.y - s, p.z - s}, {p.x + s, p.y + s, p.z - s}, c, true);
						renderer->DrawLine3D({p.x + s, p.y - s, p.z + s}, {p.x + s, p.y + s, p.z + s}, c, true);
						renderer->DrawLine3D({p.x - s, p.y - s, p.z + s}, {p.x - s, p.y + s, p.z + s}, c, true);
					}
				}
			}
		}
	}
	#endif

	if (currentWave_ != previousWave_) {
		SpawnSpanner(currentWave_, scene);
	}
	previousWave_ = currentWave_;
}

void WaveManagement::OnDestroy(entt::entity /*entity*/, GameScene* /*scene*/) {}


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

	ImGui::SeparatorText("ウェーブ管理 (Wave Management)");

	if (ImGui::Button("ウェーブを追加 (Add Wave)")) {
		enemySpawners_.push_back({});
		enemySpawnerNames_.push_back({});
	}

	for (size_t wi = 0; wi < enemySpawners_.size(); ++wi) {
		ImGui::PushID(static_cast<int>(wi));
		bool isNodeOpen = ImGui::TreeNodeEx(("Wave " + std::to_string(wi)).c_str(), ImGuiTreeNodeFlags_DefaultOpen);

		if (isNodeOpen) {
			if (ImGui::Button("スポナーを追加 (Add Spawner)")) {
				std::string name = "Spawner_W" + std::to_string(wi) + "_" + std::to_string(enemySpawners_[wi].size());
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
				// スポナーエンティティは破棄せずリストからのみ外す（必要ならDestroyObjectを呼ぶ）
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
				if (ImGui::TreeNode(sname.c_str())) {
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

	// 最新のスポナー名リストを構築してから保存
	enemySpawnerNames_.clear();
	if (cachedScene_) {
		for (const auto& wave : enemySpawners_) {
			std::vector<std::string> names;
			for (auto e : wave) {
				if (cachedScene_->GetRegistry().valid(e)) {
					if (auto* nc = cachedScene_->GetRegistry().try_get<NameComponent>(e)) {
						names.push_back(nc->name);
					} else {
						names.push_back("Unknown");
					}
				}
			}
			enemySpawnerNames_.push_back(names);
		}
	}

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