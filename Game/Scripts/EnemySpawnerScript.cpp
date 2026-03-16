#include "EnemySpawnerScript.h"
#include "ScriptEngine.h"
#include "../Scenes/GameScene.h"
#include "../../Engine/Renderer.h"
#include "../../externals/imgui/imgui.h"
#include "../../Engine/ThirdParty/nlohmann/json.hpp"
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

using json = nlohmann::json;

namespace Game {

// ========== ランタイム ==========

float EnemySpawnerScript::CalcInterval() const {
	if (maxCount <= 1) return spawnDuration;
	return spawnDuration / static_cast<float>(maxCount - 1);
}

void EnemySpawnerScript::Start(SceneObject& /*obj*/, GameScene* /*scene*/) {
	currentWave_ = startWave;
	spawnedThisWave_ = 0;
	elapsedTime_ = 0.0f;
	isWaitingDelay_ = true;
}

void EnemySpawnerScript::Update(SceneObject& obj, GameScene* scene, float dt) {
	if (currentWave_ >= waveCount) return;

	elapsedTime_ += dt;

	// ウェーブ開始前の遅延
	if (isWaitingDelay_) {
		if (elapsedTime_ < waveDelay) {
			return;
		}
		elapsedTime_ = 0.0f; // 遅延終了後、出現タイマーリセット
		isWaitingDelay_ = false;
	}

	float interval = CalcInterval();
	int shouldHaveSpawned = (interval > 0.0001f) ? static_cast<int>(elapsedTime_ / interval) + 1 : maxCount;
	if (shouldHaveSpawned > maxCount) shouldHaveSpawned = maxCount;

	while (spawnedThisWave_ < shouldHaveSpawned) {
		// パターンに応じた出現位置を計算
		// ★修正: 親子関係を考慮し、スポナーのワールド座標を取得する
		DirectX::XMFLOAT3 spawnerWorldPos = obj.translate;
		for (int i = 0; i < (int)scene->GetObjects().size(); ++i) {
			if (&scene->GetObjects()[i] == &obj) {
				Engine::Matrix4x4 wm = scene->GetWorldMatrix(i);
				spawnerWorldPos = { wm.m[3][0], wm.m[3][1], wm.m[3][2] };
				break;
			}
		}

		DirectX::XMFLOAT3 spawnPos = spawnerWorldPos;

		float t = (maxCount <= 1) ? 0.0f : static_cast<float>(spawnedThisWave_) / static_cast<float>(maxCount - 1);

		switch (pattern) {
		case SpawnPattern::Circle: {
			float angle = t * 2.0f * static_cast<float>(M_PI);
			spawnPos.x += patternRadius * cosf(angle);
			spawnPos.z += patternRadius * sinf(angle);
			break;
		}
		case SpawnPattern::Line: {
			float offset = (t - 0.5f) * 2.0f * patternRadius;
			spawnPos.x += offset;
			break;
		}
		case SpawnPattern::Point:
		default:
			break;
		}

		SceneObject enemy;
		enemy.name = "SpawnedEnemy_W" + std::to_string(currentWave_) + "_" + std::to_string(spawnedThisWave_);
		enemy.translate = spawnPos;

		auto* renderer = Engine::Renderer::GetInstance();

		// エネミーの基本共通設定
		enemy.scale = {1.0f, 1.0f, 1.0f};
		if (renderer) {
			enemy.modelHandle = renderer->LoadObjMesh("Resources/cube/cube.obj");
			enemy.textureHandle = renderer->LoadTexture2D("Resources/white1x1.png");
			MeshRendererComponent mr;
			mr.modelHandle = enemy.modelHandle;
			mr.textureHandle = enemy.textureHandle;
			mr.color = (enemyType == 42) ? DirectX::XMFLOAT4{1.0f, 0.2f, 0.2f, 1.0f} : DirectX::XMFLOAT4{1.0f, 0.4f, 0.2f, 1.0f};
			mr.shaderName = "Toon";
			enemy.meshRenderers.push_back(mr);
		}

		BoxColliderComponent bc;
		bc.size = {1.0f, 2.0f, 1.0f};
		enemy.boxColliders.push_back(bc);

		RigidbodyComponent rb;
		// ★修正: Flyタイプの場合は重力を無効化する
		// (スクリプト側でも制御するが、初期化時の挙動を安定させるため)
		bool isFly = (enemyScriptPath == "EnemyBehavior" && enemyScriptParams.find("\"moveType\":1") != std::string::npos);
		rb.useGravity = !isFly;
		enemy.rigidbodies.push_back(rb);

		HealthComponent health;
		health.hp = 100.0f;
		health.maxHp = 100.0f;
		enemy.healths.push_back(health);

		TagComponent tag;
		tag.tag = "Enemy";
		enemy.tags.push_back(tag);

		// スクリプトとパラメータの設定
		ScriptComponent sc;
		sc.scriptPath = enemyScriptPath;
		sc.enabled = true;
		sc.parameterData = enemyScriptParams; // ★ 保存されていたパラメータを渡す
		enemy.scripts.push_back(sc);

		scene->SpawnObject(enemy);

		spawnedThisWave_++;
	}

	// ウェーブ完了チェック
	if (spawnedThisWave_ >= maxCount) {
		currentWave_++;
		spawnedThisWave_ = 0;
		elapsedTime_ = 0.0f;
		isWaitingDelay_ = true; // 次のウェーブも遅延から開始
	}
}

// ========== エディターUI ==========

void EnemySpawnerScript::OnEditorUI() {
	ImGui::SeparatorText("Enemy Spawner");

	// --- 基本パラメータ ---
	ImGui::DragInt("ウェーブ総数", &waveCount, 0.1f, 1, 100);
	ImGui::DragInt("開始ウェーブ", &startWave, 0.1f, 0, waveCount - 1);
	ImGui::SliderFloat("各ウェーブ開始遅延", &waveDelay, 0.0f, 60.0f, "%.1f s");
	
	// エネミー種類のスクリプト選択 (ドロップダウン)
	auto scripts = ScriptEngine::GetInstance()->GetRegisteredScriptNames();
	if (ImGui::BeginCombo("エネミー種類", enemyScriptPath.c_str())) {
		for (const auto& s : scripts) {
			bool isSelected = (enemyScriptPath == s);
			if (ImGui::Selectable(s.c_str(), isSelected)) {
				enemyScriptPath = s;
			}
			if (isSelected) ImGui::SetItemDefaultFocus();
		}
		ImGui::EndCombo();
	}

	// --- 出現敵のパラメータ調整 (さらに表示) ---
	if (!enemyScriptPath.empty()) {
		// キャッシュが古いか無い場合は生成
		if (lastLoadedPath_ != enemyScriptPath || !editorScriptInstance_) {
			editorScriptInstance_ = ScriptEngine::GetInstance()->CreateScript(enemyScriptPath);
			if (editorScriptInstance_) {
				editorScriptInstance_->DeserializeParameters(enemyScriptParams);
			}
			lastLoadedPath_ = enemyScriptPath;
		}

		if (editorScriptInstance_) {
			if (ImGui::TreeNodeEx("出現敵スクリプトの調整", ImGuiTreeNodeFlags_DefaultOpen)) {
				editorScriptInstance_->OnEditorUI();
				// 変更があった場合にシリアライズして保存
				enemyScriptParams = editorScriptInstance_->SerializeParameters();
				ImGui::TreePop();
			}
		}
	}
	
	ImGui::DragInt("1ウェーブ出現数", &maxCount, 0.1f, 1, 200);

	// --- 全体時間 (直感スライダー) ---
	ImGui::Spacing();
	ImGui::SeparatorText("ウェーブ全体時間");
	ImGui::SliderFloat("全体時間 (秒)", &spawnDuration, 0.5f, 120.0f, "%.1f s");

	// 自動計算された間隔を表示
	float interval = CalcInterval();
	ImGui::Text("-> 1体ごとの間隔: %.2f 秒", interval);

	// タイムラインプレビュー: ウェーブの進行を可視化
	{
		ImVec2 avail = ImGui::GetContentRegionAvail();
		float barW = avail.x - 10.0f;
		if (barW < 50.0f) barW = 50.0f;
		float barH = 24.0f;
		ImVec2 p = ImGui::GetCursorScreenPos();
		ImDrawList* dl = ImGui::GetWindowDrawList();

		// 背景バー
		dl->AddRectFilled(ImVec2(p.x, p.y), ImVec2(p.x + barW, p.y + barH), IM_COL32(40, 40, 50, 255), 4.0f);

		// 各出現タイミングのマーカー
		for (int i = 0; i < maxCount; i++) {
			float t = (maxCount <= 1) ? 0.5f : static_cast<float>(i) / static_cast<float>(maxCount - 1);
			float x = p.x + t * barW;
			ImU32 col = IM_COL32(255, 80, 80, 255);
			dl->AddRectFilled(ImVec2(x - 2, p.y + 2), ImVec2(x + 2, p.y + barH - 2), col, 2.0f);
		}

		// 枠
		dl->AddRect(ImVec2(p.x, p.y), ImVec2(p.x + barW, p.y + barH), IM_COL32(120, 120, 140, 255), 4.0f);
		ImGui::Dummy(ImVec2(barW, barH + 4));

		// ラベル
		ImGui::TextDisabled("[ 0 s ] ---- タイムライン ---- [ %.1f s ]", spawnDuration);
	}

	// --- 配列パターン (直感的なドロップダウン + 3Dプレビュー) ---
	ImGui::Spacing();
	ImGui::SeparatorText("配列パターン");
	const char* patternNames[] = {"Point (単点)", "Circle (円形)", "Line (直線)"};
	int patIdx = static_cast<int>(pattern);
	if (ImGui::Combo("パターン", &patIdx, patternNames, IM_ARRAYSIZE(patternNames))) {
		pattern = static_cast<SpawnPattern>(patIdx);
	}

	if (pattern == SpawnPattern::Circle) {
		ImGui::SliderFloat("半径", &patternRadius, 0.5f, 50.0f, "%.1f m");
	} else if (pattern == SpawnPattern::Line) {
		ImGui::SliderFloat("配列長さ", &patternRadius, 0.5f, 50.0f, "%.1f m");
	}

	ImGui::TextDisabled("※ シーンビューにプレビューが表示されます");
}

// ========== 3Dプレビュー描画 ==========

void EnemySpawnerScript::DrawSpawnPreview(const DirectX::XMFLOAT3& worldPos) const {
	auto* renderer = Engine::Renderer::GetInstance();
	if (!renderer) return;

	Engine::Vector4 colorRed = {1.0f, 0.2f, 0.2f, 1.0f};
	Engine::Vector4 colorYellow = {1.0f, 1.0f, 0.3f, 1.0f};
	Engine::Vector4 colorGreen = {0.3f, 1.0f, 0.3f, 1.0f};

	// スポナー位置にクロスマーカーを描画
	float sz = 0.5f;
	renderer->DrawLine3D({worldPos.x - sz, worldPos.y, worldPos.z}, {worldPos.x + sz, worldPos.y, worldPos.z}, colorGreen, true);
	renderer->DrawLine3D({worldPos.x, worldPos.y - sz, worldPos.z}, {worldPos.x, worldPos.y + sz, worldPos.z}, colorGreen, true);
	renderer->DrawLine3D({worldPos.x, worldPos.y, worldPos.z - sz}, {worldPos.x, worldPos.y, worldPos.z + sz}, colorGreen, true);

	// 各出現予定位置に赤い十字マーカーを描画
	for (int i = 0; i < maxCount; i++) {
		float t = (maxCount <= 1) ? 0.0f : static_cast<float>(i) / static_cast<float>(maxCount - 1);
		DirectX::XMFLOAT3 pos = worldPos;

		switch (pattern) {
		case SpawnPattern::Circle: {
			float angle = t * 2.0f * static_cast<float>(M_PI);
			pos.x += patternRadius * cosf(angle);
			pos.z += patternRadius * sinf(angle);
			break;
		}
		case SpawnPattern::Line: {
			float offset = (t - 0.5f) * 2.0f * patternRadius;
			pos.x += offset;
			break;
		}
		case SpawnPattern::Point:
		default:
			break;
		}

		// 十字マーカー
		float m = 0.3f;
		renderer->DrawLine3D({pos.x - m, pos.y, pos.z}, {pos.x + m, pos.y, pos.z}, colorRed, true);
		renderer->DrawLine3D({pos.x, pos.y, pos.z - m}, {pos.x, pos.y, pos.z + m}, colorRed, true);
		// 垂直ライン (地面からの高さを表す)
		renderer->DrawLine3D({pos.x, pos.y, pos.z}, {pos.x, pos.y + 1.0f, pos.z}, colorYellow, true);

		// スポナー中心から各ポイントへの接続線
		if (pattern != SpawnPattern::Point) {
			renderer->DrawLine3D(
			    {worldPos.x, worldPos.y + 0.1f, worldPos.z},
			    {pos.x, pos.y + 0.1f, pos.z},
			    {0.5f, 0.5f, 0.5f, 0.4f}, true);
		}
	}

	// 円形パターンの場合、円を描画
	if (pattern == SpawnPattern::Circle && patternRadius > 0.01f) {
		const int segments = 32;
		for (int i = 0; i < segments; i++) {
			float a0 = static_cast<float>(i) / segments * 2.0f * static_cast<float>(M_PI);
			float a1 = static_cast<float>(i + 1) / segments * 2.0f * static_cast<float>(M_PI);
			Engine::Vector3 p0 = {worldPos.x + patternRadius * cosf(a0), worldPos.y + 0.05f, worldPos.z + patternRadius * sinf(a0)};
			Engine::Vector3 p1 = {worldPos.x + patternRadius * cosf(a1), worldPos.y + 0.05f, worldPos.z + patternRadius * sinf(a1)};
			renderer->DrawLine3D(p0, p1, {1.0f, 0.5f, 0.2f, 0.6f}, true);
		}
	}

	// 直線パターンの場合、直線を描画
	if (pattern == SpawnPattern::Line && patternRadius > 0.01f) {
		Engine::Vector3 p0 = {worldPos.x - patternRadius, worldPos.y + 0.05f, worldPos.z};
		Engine::Vector3 p1 = {worldPos.x + patternRadius, worldPos.y + 0.05f, worldPos.z};
		renderer->DrawLine3D(p0, p1, {1.0f, 0.5f, 0.2f, 0.6f}, true);
	}
}

// ========== シリアライズ ==========

std::string EnemySpawnerScript::SerializeParameters() {
	json j;
	j["waveCount"] = waveCount;
	j["startWave"] = startWave;
	j["waveDelay"] = waveDelay;
	j["enemyType"] = enemyType;
	j["enemyScriptPath"] = enemyScriptPath;
	j["enemyScriptParams"] = enemyScriptParams;
	j["spawnDuration"] = spawnDuration;
	j["pattern"] = static_cast<int>(pattern);
	j["patternRadius"] = patternRadius;
	j["maxCount"] = maxCount;
	return j.dump();
}

void EnemySpawnerScript::DeserializeParameters(const std::string& data) {
	if (data.empty()) return;
	try {
		json j = json::parse(data);
		if (j.contains("waveCount")) waveCount = j["waveCount"].get<int>();
		if (j.contains("startWave")) startWave = j["startWave"].get<int>();
		if (j.contains("waveDelay")) waveDelay = j["waveDelay"].get<float>();
		if (j.contains("enemyType")) enemyType = j["enemyType"].get<int>();
		if (j.contains("enemyScriptPath")) enemyScriptPath = j["enemyScriptPath"].get<std::string>();
		if (j.contains("enemyScriptParams")) enemyScriptParams = j["enemyScriptParams"].get<std::string>();
		if (j.contains("spawnDuration")) spawnDuration = j["spawnDuration"].get<float>();
		if (j.contains("pattern")) pattern = static_cast<SpawnPattern>(j["pattern"].get<int>());
		if (j.contains("patternRadius")) patternRadius = j["patternRadius"].get<float>();
		if (j.contains("maxCount")) maxCount = j["maxCount"].get<int>();
	} catch (...) {
	}
}

// ★ 自動登録マクロ
REGISTER_SCRIPT(EnemySpawnerScript)

} // namespace Game
