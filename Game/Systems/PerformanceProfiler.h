#pragma once
// ★パフォーマンス計測用プロファイラー (デバッグ専用)
// ImGuiオーバーレイで各システムの処理時間・エンティティ数・メモリ使用量をリアルタイム表示

#include <chrono>
#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <cstdio>
#include <numeric>
#include <Windows.h>
#include <Psapi.h>
#include "../ObjectTypes.h"
#include "../../externals/entt/entt.hpp"

#ifdef USE_IMGUI
#include "../../externals/imgui/imgui.h"
#endif

namespace Game {

// =====================================================
// ScopedTimer: 自動計測用RAII
// =====================================================
class ScopedTimer {
public:
	ScopedTimer(float& outMs) : outMs_(outMs), start_(std::chrono::high_resolution_clock::now()) {}
	~ScopedTimer() {
		auto end = std::chrono::high_resolution_clock::now();
		outMs_ = std::chrono::duration<float, std::milli>(end - start_).count();
	}
private:
	float& outMs_;
	std::chrono::high_resolution_clock::time_point start_;
};

// =====================================================
// PerformanceProfiler: 全データ収集 & ImGui表示
// =====================================================
class PerformanceProfiler {
public:
	static constexpr int HISTORY_SIZE = 300; // 5秒分 (60fps)
	static constexpr int LOG_INTERVAL_FRAMES = 60; // OutputDebugStringAの出力間隔

	// --- 計測結果格納 ---
	struct SystemTiming {
		std::string name;
		float currentMs = 0.0f;
		float history[HISTORY_SIZE] = {};
		int historyIdx = 0;
		float avgMs = 0.0f;
		float maxMs = 0.0f;
	};

	struct EntityStats {
		int totalEntities = 0;
		int withTransform = 0;
		int withMeshRenderer = 0;
		int withScript = 0;
		int withRigidbody = 0;
		int withBoxCollider = 0;
		int withHealth = 0;
		int withHitbox = 0;
		int withHurtbox = 0;
		int withMotion = 0;
		int withAnimator = 0;
		int withParticle = 0;
		int withVariable = 0;

		// タグ別カウント
		std::unordered_map<uint32_t, int> tagCounts;
	};

	struct FrameStats {
		float totalUpdateMs = 0.0f;
		float totalDrawMs = 0.0f;
		float totalDrawUIMs = 0.0f;
		float tagSyncMs = 0.0f;
		float animationMs = 0.0f;
		float lightSystemMs = 0.0f;
		float particleUpdateMs = 0.0f;
		float pendingDestroyMs = 0.0f;
		float matrixCacheClearMs = 0.0f;
		float fps = 0.0f;
		float dt = 0.0f;
		// Draw内訳
		float drawMeshLoopMs = 0.0f;    // メッシュ描画ループ全体
		float drawParticleMs = 0.0f;    // パーティクル描画
		float drawSystemMs = 0.0f;      // System::Draw
		float drawGizmoMs = 0.0f;       // エディタギズモ
		
		// ★追加: GPU/コマンド実行時間
		float gpuPresentMs = 0.0f;
		float gpuWaitMs = 0.0f;

		int   drawMeshCount = 0;        // 実際に描画したメッシュ数
		int   drawSkinnedCount = 0;     // スキンメッシュ数
		int   drawIteratedCount = 0;    // ループで走査したエンティティ数
		int   drawCulledCount = 0;      // 視錐台カリングで除外した数
	};

	// --- 公開メンバ ---
	bool enabled = true;  // P キーで切り替え
	bool showOverlay = true;
	bool showDetails = false;
	bool logToDebugOutput = true;

	std::vector<SystemTiming> systemTimings;
	EntityStats entityStats;
	FrameStats frameStats;
	
	// FPS履歴
	float fpsHistory[HISTORY_SIZE] = {};
	int fpsHistoryIdx = 0;

	// エンティティ数履歴 (蓄積リーク検出用)
	float entityHistory[HISTORY_SIZE] = {};
	float vfxHistory[HISTORY_SIZE] = {};
	float enemyHistory[HISTORY_SIZE] = {};
	float bulletHistory[HISTORY_SIZE] = {};
	int entityHistoryIdx = 0;

	// メモリ使用量
	float memoryMB = 0.0f;
	float peakMemoryMB = 0.0f;

	int frameCount = 0;

	// --- 初期化 ---
	void Initialize(int systemCount) {
		systemTimings.resize(systemCount);
		memset(fpsHistory, 0, sizeof(fpsHistory));
		memset(entityHistory, 0, sizeof(entityHistory));
		memset(vfxHistory, 0, sizeof(vfxHistory));
		memset(enemyHistory, 0, sizeof(enemyHistory));
		memset(bulletHistory, 0, sizeof(bulletHistory));
	}

	// --- システム名の設定 ---
	void SetSystemName(int index, const std::string& name) {
		if (index >= 0 && index < (int)systemTimings.size()) {
			systemTimings[index].name = name;
		}
	}

	// --- フレーム開始 ---
	void BeginFrame(float dt) {
		frameStats.dt = dt;
		if (dt > 0.0001f) {
			frameStats.fps = 1.0f / dt;
		}
		
		// FPS履歴
		fpsHistory[fpsHistoryIdx] = frameStats.fps;
		fpsHistoryIdx = (fpsHistoryIdx + 1) % HISTORY_SIZE;

		// メモリ計測 (30フレームごと)
		if (frameCount % 30 == 0) {
			PROCESS_MEMORY_COUNTERS_EX pmc;
			if (GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc))) {
				memoryMB = (float)(pmc.WorkingSetSize / (1024.0 * 1024.0));
				peakMemoryMB = (float)(pmc.PeakWorkingSetSize / (1024.0 * 1024.0));
			}
		}
	}

	// --- システム計測結果の記録 ---
	void RecordSystemTime(int index, float ms) {
		if (index < 0 || index >= (int)systemTimings.size()) return;
		auto& st = systemTimings[index];
		st.currentMs = ms;
		st.history[st.historyIdx] = ms;
		st.historyIdx = (st.historyIdx + 1) % HISTORY_SIZE;

		// 平均・最大の計算
		float sum = 0.0f;
		float maxVal = 0.0f;
		for (int i = 0; i < HISTORY_SIZE; ++i) {
			sum += st.history[i];
			if (st.history[i] > maxVal) maxVal = st.history[i];
		}
		st.avgMs = sum / HISTORY_SIZE;
		st.maxMs = maxVal;
	}

	// --- エンティティ統計の収集 ---
	void CollectEntityStats(entt::registry& registry, const std::unordered_map<TagType, std::vector<entt::entity>>& tagCache) {
		entityStats = EntityStats{};

		// 総エンティティ数 (alive)
		int aliveCount = 0;
		auto& storage = registry.storage<entt::entity>();
		for (auto it = storage.begin(); it != storage.end(); ++it) {
			if (registry.valid(*it)) aliveCount++;
		}
		entityStats.totalEntities = aliveCount;

		// コンポーネント別カウント (range-forで数える)
		auto countView = [](auto view) -> int {
			int c = 0;
			for (auto e : view) { (void)e; ++c; }
			return c;
		};
		entityStats.withTransform = countView(registry.view<TransformComponent>());
		entityStats.withMeshRenderer = countView(registry.view<MeshRendererComponent>());
		entityStats.withScript = countView(registry.view<ScriptComponent>());
		entityStats.withRigidbody = countView(registry.view<RigidbodyComponent>());
		entityStats.withBoxCollider = countView(registry.view<BoxColliderComponent>());
		entityStats.withHealth = countView(registry.view<HealthComponent>());
		entityStats.withHitbox = countView(registry.view<HitboxComponent>());
		entityStats.withHurtbox = countView(registry.view<HurtboxComponent>());
		entityStats.withMotion = countView(registry.view<MotionComponent>());
		entityStats.withAnimator = countView(registry.view<AnimatorComponent>());
		entityStats.withParticle = countView(registry.view<ParticleEmitterComponent>());
		entityStats.withVariable = countView(registry.view<VariableComponent>());

		// タグ別カウント (tagCacheを利用)
		for (auto& [tag, entities] : tagCache) {
			int validCount = 0;
			for (auto e : entities) {
				if (registry.valid(e)) validCount++;
			}
			entityStats.tagCounts[(uint32_t)tag] = validCount;
		}

		// 履歴に記録
		entityHistory[entityHistoryIdx] = (float)entityStats.totalEntities;
		
		int vfxCount = 0;
		auto itVfx = entityStats.tagCounts.find((uint32_t)TagType::VFX);
		if (itVfx != entityStats.tagCounts.end()) vfxCount += itVfx->second;
		auto itHit = entityStats.tagCounts.find((uint32_t)TagType::HitDistortion_VFX);
		if (itHit != entityStats.tagCounts.end()) vfxCount += itHit->second;
		vfxHistory[entityHistoryIdx] = (float)vfxCount;

		auto itEnemy = entityStats.tagCounts.find((uint32_t)TagType::Enemy);
		enemyHistory[entityHistoryIdx] = itEnemy != entityStats.tagCounts.end() ? (float)itEnemy->second : 0.0f;

		auto itBullet = entityStats.tagCounts.find((uint32_t)TagType::Bullet);
		bulletHistory[entityHistoryIdx] = itBullet != entityStats.tagCounts.end() ? (float)itBullet->second : 0.0f;

		entityHistoryIdx = (entityHistoryIdx + 1) % HISTORY_SIZE;
	}

	// --- フレーム終了 & ログ出力 ---
	void EndFrame() {
		frameCount++;

		// OutputDebugStringA へのログ出力 (1秒ごと)
		if (logToDebugOutput && (frameCount % LOG_INTERVAL_FRAMES == 0)) {
			char buf[2048];
			sprintf_s(buf,
				"[PERF] FPS=%.1f | dt=%.2fms | Update=%.2fms | Draw=%.2fms | "
				"Entities=%d | VFX=%d | Enemy=%d | Bullet=%d | Scripts=%d | Meshes=%d | "
				"Mem=%.0fMB\n",
				frameStats.fps, frameStats.dt * 1000.0f,
				frameStats.totalUpdateMs, frameStats.totalDrawMs,
				entityStats.totalEntities,
				(int)vfxHistory[(entityHistoryIdx - 1 + HISTORY_SIZE) % HISTORY_SIZE],
				(int)enemyHistory[(entityHistoryIdx - 1 + HISTORY_SIZE) % HISTORY_SIZE],
				(int)bulletHistory[(entityHistoryIdx - 1 + HISTORY_SIZE) % HISTORY_SIZE],
				entityStats.withScript, entityStats.withMeshRenderer,
				memoryMB);
			OutputDebugStringA(buf);

			// 各システムの時間
			std::string sysLog = "[PERF-SYS] ";
			for (auto& st : systemTimings) {
				char sbuf[128];
				sprintf_s(sbuf, "%s=%.2fms ", st.name.c_str(), st.currentMs);
				sysLog += sbuf;
			}
			sysLog += "\n";
			OutputDebugStringA(sysLog.c_str());

			// 追加計測
			char buf2[512];
			sprintf_s(buf2,
				"[PERF-EXTRA] TagSync=%.2fms | Anim=%.2fms | Lights=%.2fms | "
				"Particles=%.2fms | Destroy=%.2fms\n",
				frameStats.tagSyncMs, frameStats.animationMs,
				frameStats.lightSystemMs, frameStats.particleUpdateMs,
				frameStats.pendingDestroyMs);
			OutputDebugStringA(buf2);
		}
	}

	// --- ImGui 表示 ---
	void DrawImGui() {
#ifdef USE_IMGUI
		if (!enabled) return;

		// ★ Pキーで表示/非表示切り替え
		if (GetAsyncKeyState('P') & 1) {
			showOverlay = !showOverlay;
		}
		if (!showOverlay) return;

		// === 半透明オーバーレイ (画面右上) ===
		ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x - 420, 10), ImGuiCond_Always);
		ImGui::SetNextWindowSize(ImVec2(410, 0), ImGuiCond_Always);
		ImGui::SetNextWindowBgAlpha(0.75f);
		if (ImGui::Begin("##PerfOverlay", nullptr,
			ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
			ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
			ImGuiWindowFlags_AlwaysAutoResize)) {

			// --- FPS ---
			ImVec4 fpsColor = frameStats.fps >= 55.0f ? ImVec4(0.2f, 1.0f, 0.2f, 1.0f) :
			                  frameStats.fps >= 30.0f ? ImVec4(1.0f, 1.0f, 0.2f, 1.0f) :
			                                            ImVec4(1.0f, 0.2f, 0.2f, 1.0f);
			ImGui::TextColored(fpsColor, "FPS: %.1f", frameStats.fps);
			ImGui::SameLine();
			ImGui::TextDisabled("(dt: %.2fms)", frameStats.dt * 1000.0f);
			ImGui::SameLine();
			ImGui::TextDisabled("Mem: %.0fMB", memoryMB);

			// FPS グラフ
			float fpsPlot[HISTORY_SIZE];
			for (int i = 0; i < HISTORY_SIZE; ++i) {
				fpsPlot[i] = fpsHistory[(fpsHistoryIdx + i) % HISTORY_SIZE];
			}
			ImGui::PlotLines("##fps", fpsPlot, HISTORY_SIZE, 0, nullptr, 0.0f, 120.0f, ImVec2(395, 40));

			ImGui::Separator();

			// --- エンティティ数 ---
			ImGui::Text("Entities: %d", entityStats.totalEntities);
			ImGui::SameLine();

			int vfxCount = (int)vfxHistory[(entityHistoryIdx - 1 + HISTORY_SIZE) % HISTORY_SIZE];
			ImVec4 vfxColor = vfxCount > 30 ? ImVec4(1,0.2f,0.2f,1) : ImVec4(0.8f,0.8f,0.8f,1);
			ImGui::TextColored(vfxColor, "VFX:%d", vfxCount);
			ImGui::SameLine();
			ImGui::Text("Enemy:%d", (int)enemyHistory[(entityHistoryIdx - 1 + HISTORY_SIZE) % HISTORY_SIZE]);
			ImGui::SameLine();
			ImGui::Text("Bullet:%d", (int)bulletHistory[(entityHistoryIdx - 1 + HISTORY_SIZE) % HISTORY_SIZE]);

			// エンティティ数グラフ (蓄積リーク検出用)
			float entityPlot[HISTORY_SIZE];
			for (int i = 0; i < HISTORY_SIZE; ++i) {
				entityPlot[i] = entityHistory[(entityHistoryIdx + i) % HISTORY_SIZE];
			}
			float maxEntity = *std::max_element(entityPlot, entityPlot + HISTORY_SIZE);
			ImGui::PlotLines("##entities", entityPlot, HISTORY_SIZE, 0, "Total Entities", 0.0f, maxEntity * 1.2f, ImVec2(395, 35));

			// VFX数グラフ
			float vfxPlot[HISTORY_SIZE];
			for (int i = 0; i < HISTORY_SIZE; ++i) {
				vfxPlot[i] = vfxHistory[(entityHistoryIdx + i) % HISTORY_SIZE];
			}
			float maxVfx = *std::max_element(vfxPlot, vfxPlot + HISTORY_SIZE);
			if (maxVfx < 1.0f) maxVfx = 10.0f;
			ImGui::PlotLines("##vfx", vfxPlot, HISTORY_SIZE, 0, "VFX Count", 0.0f, maxVfx * 1.2f, ImVec2(395, 30));

			ImGui::Separator();

			// --- システム別タイミング (バーグラフ) ---
			ImGui::Text("System Timings:");
			float totalSysMs = 0.0f;
			for (auto& st : systemTimings) {
				totalSysMs += st.currentMs;
			}

			for (auto& st : systemTimings) {
				float fraction = (totalSysMs > 0.01f) ? st.currentMs / totalSysMs : 0.0f;
				ImVec4 barColor = st.currentMs > 5.0f ? ImVec4(1,0.3f,0.3f,1) :
				                  st.currentMs > 2.0f ? ImVec4(1,1,0.3f,1) :
				                                         ImVec4(0.3f,0.8f,0.3f,1);
				
				ImGui::PushStyleColor(ImGuiCol_PlotHistogram, barColor);
				char label[128];
				sprintf_s(label, "%.2fms (avg:%.2f max:%.2f)", st.currentMs, st.avgMs, st.maxMs);
				ImGui::ProgressBar(fraction, ImVec2(280, 16), label);
				ImGui::PopStyleColor();
				ImGui::SameLine();
				ImGui::Text("%s", st.name.c_str());
			}

			// 追加計測項目
			ImGui::Separator();
			ImGui::Text("Extra: TagSync=%.2f Anim=%.2f Lights=%.2f",
				frameStats.tagSyncMs, frameStats.animationMs, frameStats.lightSystemMs);
			ImGui::Text("       Particles=%.2f Destroy=%.2f",
				frameStats.particleUpdateMs, frameStats.pendingDestroyMs);
			ImGui::Text("Total: Update=%.2fms Draw=%.2fms UI=%.2fms",
				frameStats.totalUpdateMs, frameStats.totalDrawMs, frameStats.totalDrawUIMs);
			// Draw内訳
			ImGui::TextColored(ImVec4(1,0.8f,0.3f,1), "Draw breakdown: MeshLoop=%.2fms Particle=%.2fms SysDraw=%.2fms Gizmo=%.2fms",
				frameStats.drawMeshLoopMs, frameStats.drawParticleMs, frameStats.drawSystemMs, frameStats.drawGizmoMs);
			
			// ★追加: GPU待機時間の表示 (GPU負荷過多の確認用)
			ImVec4 gpuColor = (frameStats.gpuPresentMs + frameStats.gpuWaitMs) > 10.0f ? ImVec4(1.0f, 0.4f, 0.4f, 1.0f) : ImVec4(0.4f, 1.0f, 0.4f, 1.0f);
			ImGui::TextColored(gpuColor, "GPU/Driver: Present=%.2fms Wait=%.2fms", frameStats.gpuPresentMs, frameStats.gpuWaitMs);
			
			ImGui::TextColored(ImVec4(1,0.8f,0.3f,1), "  Iterated:%d  DrawnMesh:%d  Skinned:%d  Culled:%d",
				frameStats.drawIteratedCount, frameStats.drawMeshCount, frameStats.drawSkinnedCount, frameStats.drawCulledCount);

			// --- 詳細表示トグル ---
			ImGui::Separator();
			ImGui::Checkbox("Show Details", &showDetails);

			if (showDetails) {
				ImGui::Separator();
				ImGui::Text("=== Component Counts ===");
				ImGui::Columns(3, "compCols", true);
				ImGui::Text("Transform: %d", entityStats.withTransform);
				ImGui::Text("MeshRenderer: %d", entityStats.withMeshRenderer);
				ImGui::Text("Script: %d", entityStats.withScript);
				ImGui::Text("Rigidbody: %d", entityStats.withRigidbody);
				ImGui::NextColumn();
				ImGui::Text("BoxCollider: %d", entityStats.withBoxCollider);
				ImGui::Text("Health: %d", entityStats.withHealth);
				ImGui::Text("Hitbox: %d", entityStats.withHitbox);
				ImGui::Text("Hurtbox: %d", entityStats.withHurtbox);
				ImGui::NextColumn();
				ImGui::Text("Motion: %d", entityStats.withMotion);
				ImGui::Text("Animator: %d", entityStats.withAnimator);
				ImGui::Text("Particle: %d", entityStats.withParticle);
				ImGui::Text("Variable: %d", entityStats.withVariable);
				ImGui::Columns(1);

				ImGui::Separator();
				ImGui::Text("=== Tag Breakdown ===");
				ImGui::Columns(3, "tagCols", true);
				for (auto& [tagVal, count] : entityStats.tagCounts) {
					if (count > 0) {
						TagType tag = (TagType)tagVal;
						const char* tagName = TagToString(tag);
						ImVec4 col = (tag == TagType::VFX || tag == TagType::HitDistortion_VFX) 
							? ImVec4(1,0.5f,0.5f,1) : ImVec4(0.8f,0.8f,0.8f,1);
						ImGui::TextColored(col, "%s: %d", tagName, count);
					}
				}
				ImGui::Columns(1);

				ImGui::Separator();
				ImGui::Text("Memory: %.1f MB (Peak: %.1f MB)", memoryMB, peakMemoryMB);

				// 蓄積リーク警告
				float first = entityHistory[entityHistoryIdx];
				float last = entityHistory[(entityHistoryIdx - 1 + HISTORY_SIZE) % HISTORY_SIZE];
				if (last > first + 20.0f && first > 0.0f) {
					ImGui::TextColored(ImVec4(1, 0.2f, 0.2f, 1),
						"!! LEAK WARNING: Entity count increased by %.0f in last 5 sec !!", last - first);
				}
			}

			ImGui::Text("[P] Toggle | [O] Log:%s", logToDebugOutput ? "ON" : "OFF");
		}
		ImGui::End();

		// Oキーでログ出力切り替え
		if (GetAsyncKeyState('O') & 1) {
			logToDebugOutput = !logToDebugOutput;
		}
#endif
	}
};

} // namespace Game
