#pragma once
// ==================================================
// SceneManager.h
//   ・シーン登録（名前→Factory）
//   ・現在シーンの保持/切替/更新/描画
//   ・即時切替と「次フレーム切替（リクエスト）」に対応
//   ・デバッグ/自動起動用ユーティリティ追加
// ==================================================
#include "IScene.h"
#include "WindowDX.h"
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "Matrix4x4.h"

namespace Engine {

class SceneManager {
public:
	SceneManager() { instance_ = this; }
	~SceneManager() { if (instance_ == this) instance_ = nullptr; }

public:
	static SceneManager* GetInstance();

	// フェード状態
	enum class TransitionState {
		None,
		Closing,
		Opening
	};

	using Factory = std::function<std::unique_ptr<IScene>()>;

public:
	void Register(const std::string& name, Factory factory);

	bool Change(const std::string& name, const SceneParameters& params = {});
	void RequestChange(const std::string& name, const SceneParameters& params = {});

	void Update();
	void Draw();
	void DrawOverlay(); // ★追加: 全ての最前面に描画する用

	IScene* Current() const { return current_.get(); }
	const std::string& CurrentName() const { return currentName_; }
	TransitionState GetTransitionState() const { return transitionState_; }

	void SetDX(WindowDX* dx) { dx_ = dx; }

	void Clear();

	// ★追加: グローバルPlay状態管理（シーン遷移を跨いでPlay状態とスナップショットを保持）
	bool IsGlobalPlaying() const { return isGlobalPlaying_; }
	void SetGlobalPlaying(bool playing) { isGlobalPlaying_ = playing; }
	void SetGlobalSnapshot(const std::string& snapshot) { globalSnapshot_ = snapshot; }
	const std::string& GetGlobalSnapshot() const { return globalSnapshot_; }
	void SetGlobalScenePath(const std::string& path) { globalScenePath_ = path; }
	const std::string& GetGlobalScenePath() const { return globalScenePath_; }

	// ★追加: 原因調査＆自動起動用
	void ClearGlobalPlayData() { isGlobalPlaying_ = false; globalSnapshot_.clear(); globalScenePath_.clear(); }
	bool Has(const std::string& name) const;
	std::string FirstRegisteredName() const; // 登録済みの先頭（無ければ空）
	std::vector<std::string> RegisteredNames() const;

private:
	static SceneManager* instance_;

	std::unordered_map<std::string, Factory> factories_;
	std::unique_ptr<IScene> current_;
	std::string currentName_;

	std::string pendingNext_;
	SceneParameters pendingParams_;

	WindowDX* dx_ = nullptr;

	bool isGlobalPlaying_ = false;
	std::string globalSnapshot_ = "";
	std::string globalScenePath_ = "";

	// トランジション用
	TransitionState transitionState_ = TransitionState::None;
	float transitionTimer_ = 0.0f;
	float transitionDuration_ = 1.2f; // 閉まる・開くそれぞれ1.2秒ずつ
	
	bool isShutterLoaded_ = false;
	uint32_t shutterTex_ = 0;
	uint32_t sparkTex_ = 0;

	// ★フェード用パーティクル
	struct ShutterParticle {
		float x, y;
		float vx, vy;
		float life, maxLife;
		float size;
		float angle;
		Vector4 color;
		bool isSmoke;
	};
	std::vector<ShutterParticle> shutterParticles_;
};

} // namespace Engine
