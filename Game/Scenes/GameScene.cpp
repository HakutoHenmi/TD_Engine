#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "GameScene.h"
#include "../../Engine/Audio.h"
#include "../../Engine/PathUtils.h"
#include "../../Engine/Frustum.h"
#include "../../Engine/SceneManager.h"
#include "../Editor/EditorUI.h"
#include "../Scripts/ScriptEngine.h"
#include "../Systems/AudioSystem.h"
#include "../Systems/CameraFollowSystem.h"
#include "Editor/EditorUI.h" // ★追加
#include "../Systems/CharacterMovementSystem.h"
#include "../Systems/CleanupSystem.h"
#include "../Systems/CombatSystem.h"
#include "../Systems/HealthSystem.h"
#include "../Systems/MotionSystem.h" // ★追加
#include "../Systems/PhysicsSystem.h"
#include "../Systems/PlayerInputSystem.h"
#include "../Systems/RiverSystem.h" // ★追加
#include "../Systems/ScriptSystem.h"
#include "../Systems/UISystem.h"
#include "../Scripts/WaveManagement.h"
#include "../Scripts/TitleManagerScript.h"
#include "../Scripts/SelectManagerScript.h"
#include "../Scripts/ResultManagerScript.h"
#include "imgui.h"
#include <Windows.h> // OutputDebugStringA
#include <algorithm>
#include <cmath>

namespace Game {

namespace {

bool TryGetLocalMeshBounds(Engine::Renderer* renderer, entt::registry& registry, entt::entity entity,
                           uint32_t modelHandle, Engine::Vector3& outMin, Engine::Vector3& outMax) {
	if (renderer) {
		if (Engine::Model* model = renderer->GetModel(modelHandle)) {
			const auto& data = model->GetData();
			if (data.min.x <= data.max.x && data.min.y <= data.max.y && data.min.z <= data.max.z) {
				outMin = data.min;
				outMax = data.max;
				return true;
			}
		}
	}
	if (auto* bc = registry.try_get<BoxColliderComponent>(entity)) {
		float hx = bc->size.x * 0.5f;
		float hy = bc->size.y * 0.5f;
		float hz = bc->size.z * 0.5f;
		outMin = {bc->center.x - hx, bc->center.y - hy, bc->center.z - hz};
		outMax = {bc->center.x + hx, bc->center.y + hy, bc->center.z + hz};
		return true;
	}
	outMin = {-0.5f, -0.5f, -0.5f};
	outMax = {0.5f, 0.5f, 0.5f};
	return true;
}

bool IsEntityVisibleInFrustum(const Engine::Frustum& frustum, Engine::Renderer* renderer, entt::registry& registry,
                              entt::entity entity, uint32_t modelHandle, const Engine::Matrix4x4& world) {
	Engine::Vector3 localMin, localMax;
	if (!TryGetLocalMeshBounds(renderer, registry, entity, modelHandle, localMin, localMax))
		return true;

	// ローカルAABBに小さな余白（細いメッシュの誤カリング防止）
	const float localMargin = 1.0f;
	localMin.x -= localMargin;
	localMin.y -= localMargin;
	localMin.z -= localMargin;
	localMax.x += localMargin;
	localMax.y += localMargin;
	localMax.z += localMargin;

	return frustum.IntersectsLocalAABB(world, localMin, localMax);
}

} // namespace

GameScene::~GameScene() {
	// ★追加: 破棄時にシグナルを解除し、安全にレジストリをクリアする
	registry_.on_construct<TagComponent>().disconnect<&GameScene::OnTagAdded>(this);
	registry_.on_destroy<TagComponent>().disconnect<&GameScene::OnTagRemoved>(this);
	registry_.on_destroy<ScriptComponent>().disconnect<&GameScene::OnScriptDestroyed>(this); // ★追加
	registry_.clear();
}

void GameScene::Initialize(Engine::WindowDX* dx, const Engine::SceneParameters& params) {
	dx_ = dx;
	renderer_ = Engine::Renderer::GetInstance();
	eventSystem_.Clear(); // ★追加: イベントリスナーをクリア
	WaveManagement::ResetState(); // ★追加: ゲーム状態を完全にリセット
	playTime_ = 0.0f;
	camera_.Initialize();
	// ★追加: 明示的にプロジェクションを設定 (1920x1080のアスペクト比)
	camera_.SetProjection(0.7854f, (float)Engine::WindowDX::kW / (float)Engine::WindowDX::kH, 0.1f, 1000.0f);
	camera_.SetPosition(0, 2, -5);
	camera_.SetRotation(0.2f, 0, 0);
	renderer_->SetAmbientColor({0.4f, 0.4f, 0.45f});
	
	// ★追加: デフォルトで絵画風（Painterly）ポストプロセスを有効にする
	renderer_->SetPostEffect("Painterly");
	renderer_->SetPostProcessEnabled(true);
	bool loaded = false;
	// ★変更: シーン名に応じてロードするJSONパスを決定
	std::string sceneName = params.sceneName;
	sceneName_ = sceneName; // ★追加: メンバーに保存
	std::string scenePath;
	try {
		// シーン名に応じたデフォルトJSONパスのマッピング
		if (!params.stagePath.empty()) {
			scenePath = EditorUI::GetUnifiedProjectPath(params.stagePath);
		} else if (sceneName == "Title") {
			scenePath = EditorUI::GetUnifiedProjectPath("Resources/Scenes/title.json");
		} else if (sceneName == "Select") {
			scenePath = EditorUI::GetUnifiedProjectPath("Resources/Scenes/select.json");
		} else if (sceneName == "Result") {
			scenePath = EditorUI::GetUnifiedProjectPath("Resources/Scenes/result.json");
		} else {
			scenePath = EditorUI::GetUnifiedProjectPath("Resources/Scenes/scene.json");
		}
		
		bool useSnapshot = false;
#ifdef USE_IMGUI
		if (auto* sm = Engine::SceneManager::GetInstance()) {
			isPlaying_ = sm->IsGlobalPlaying();
			if (isPlaying_ && !sm->GetGlobalSnapshot().empty() && 
			    (EditorUI::GetUnifiedProjectPath(scenePath) == EditorUI::GetUnifiedProjectPath(sm->GetGlobalScenePath()))) {
				useSnapshot = true;
			}
		}
#endif

		if (useSnapshot) {
			auto* sm = Engine::SceneManager::GetInstance();
			OutputDebugStringA(("[GameScene] Restoring from global snapshot for " + scenePath + "...\n").c_str());
			EditorUI::LoadFromMemory(this, sm->GetGlobalSnapshot());
			EditorUI::currentScenePath = sm->GetGlobalScenePath();
			sceneSnapshot_ = sm->GetGlobalSnapshot(); // 現在のスナップショットとして保持
			loaded = true;
		} else if (std::filesystem::exists(Engine::PathUtils::FromUTF8(scenePath))) {
			OutputDebugStringA(("[GameScene] " + scenePath + " found. Loading...\n").c_str());
			EditorUI::LoadScene(this, scenePath);
			sceneSnapshot_ = EditorUI::SaveToMemory(this); // ★追加: 初期ロード直後の状態を保存
			loaded = true;
		} else {
			OutputDebugStringA(("[GameScene] " + scenePath + " NOT found.\n").c_str());
		}
	} catch (const std::exception& e) {
		std::string msg = "[GameScene] EXCEPTION during scene load: " + std::string(e.what()) + "\n";
		OutputDebugStringA(msg.c_str());
		MessageBoxA(NULL, msg.c_str(), "Scene Load Error", MB_OK | MB_ICONERROR);
	}

	// ★変更: JSONが存在しない場合のフォールバック処理（シーン名に応じて分岐）
	if (registry_.storage<entt::entity>().empty() || !loaded) {
		if (sceneName == "Title" || sceneName == "Select" || sceneName == "Result") {
			// Title/Select/Result: マネージャースクリプトのみ生成（UIは直接作成）
			std::string scriptName;
			if (sceneName == "Title") {
				scriptName = "TitleManagerScript";
				TitleManagerScript::CreateFallbackUI(this);
			} else if (sceneName == "Select") {
				scriptName = "SelectManagerScript";
				SelectManagerScript::CreateFallbackUI(this);
			} else if (sceneName == "Result") {
				scriptName = "ResultManagerScript";
				ResultManagerScript::CreateFallbackUI(this, params.isWin, params.score, params.clearTime);
			}

			auto manager = registry_.create();
			registry_.emplace<NameComponent>(manager, sceneName + "Manager");
			auto& sc = registry_.emplace<ScriptComponent>(manager);
			sc.scripts.push_back({scriptName});

			// Resultシーンの場合、パラメータをVariableComponentで渡す
			if (sceneName == "Result") {
				auto& vars = registry_.emplace<VariableComponent>(manager);
				vars.SetValue("isWin", params.isWin ? 1.0f : 0.0f);
				vars.SetValue("score", static_cast<float>(params.score));
				vars.SetValue("clearTime", params.clearTime);
			}

			// ★変更: スクリプトのインスタンス化と初期化を即座に行い、STOP中でもUIとして機能させる
			if (auto instance = ScriptEngine::GetInstance()->CreateScript(scriptName)) {
				sc.scripts[0].instance = instance;
				instance->Start(manager, this);
				sc.scripts[0].parameterData = instance->SerializeParameters();
			}

			// ★追加: 保存時に誤ってscene.jsonを上書きしないよう、パスを同期
			EditorUI::currentScenePath = scenePath;
			sceneSnapshot_ = EditorUI::SaveToMemory(this); // フォールバック生成直後も初期状態として保存
		} else {
			// Game: 従来のフォールバック（Sun + Plane + PhaseSystem）
			auto sun = registry_.create();
			registry_.emplace<NameComponent>(sun, "Sun");
			registry_.emplace<TransformComponent>(
			    sun, DirectX::XMFLOAT3{0, 10, 0}, DirectX::XMFLOAT3{DirectX::XMConvertToRadians(45.0f), DirectX::XMConvertToRadians(30.0f), 0}, DirectX::XMFLOAT3{1, 1, 1});
			registry_.emplace<DirectionalLightComponent>(sun);

			auto plane = registry_.create();
			registry_.emplace<NameComponent>(plane, "Plane");

			auto& mesh = registry_.emplace<MeshRendererComponent>(plane);
			mesh.modelHandle = renderer_->LoadObjMesh("Resources/Models/plane.obj");
			mesh.textureHandle = renderer_->LoadTexture2D("Resources/Textures/white1x1.png");
			mesh.modelPath = "Resources/Models/plane.obj";
			mesh.texturePath = "Resources/Textures/white1x1.png";

			registry_.emplace<TransformComponent>(plane, DirectX::XMFLOAT3{0, 0, 0}, DirectX::XMFLOAT3{0, 0, 0}, DirectX::XMFLOAT3{20, 1, 20});

			// ★追加: 物理判定用にGpuMeshColliderを付与
			auto& gmc = registry_.emplace<GpuMeshColliderComponent>(plane);
			gmc.meshHandle = mesh.modelHandle;
			gmc.enabled = true;

			// 準備フェーズシステムの作成 (フォールバック)
			auto ps = registry_.create();
			registry_.emplace<NameComponent>(ps, "PhaseSystem");
			auto& sc = registry_.emplace<ScriptComponent>(ps);
			sc.scripts.push_back({"PhaseSystemScript"});

			// ★追加: プレイヤーの作成 (フォールバック)
			auto player = registry_.create();
			registry_.emplace<NameComponent>(player, "Player");
			registry_.emplace<TagComponent>(player, TagType::Player);
			auto& pTc = registry_.emplace<TransformComponent>(player);
			pTc.translate = DirectX::XMFLOAT3{0, 1, 0};

			auto& pSc = registry_.emplace<ScriptComponent>(player);
			pSc.scripts.push_back({"PlayerScript"});
			// プレイヤーに必要な他コンポーネント
			registry_.emplace<PlayerInputComponent>(player);
			registry_.emplace<CharacterMovementComponent>(player);
			auto& pHc = registry_.emplace<HealthComponent>(player);
			pHc.hp = 100.0f; pHc.maxHp = 100.0f;
			
			registry_.emplace<CameraTargetComponent>(player);
			auto& pMr = registry_.emplace<MeshRendererComponent>(player);
			pMr.modelHandle = renderer_->LoadObjMesh("Resources/Models/cube/cube.obj");
			pMr.textureHandle = renderer_->LoadTexture2D("Resources/Textures/white1x1.png");
		}
	}

	// エディターUIの初期化
	EditorUI::Initialize(renderer_);

	// パーティクルエディターの初期化
	particleEditor_.Initialize();

	// スクリプトエンジンの初期化
	ScriptEngine::GetInstance()->Initialize();

	// ★ Systemの登録（順序が重要：ScriptSystemをカメラや物理の前に持ってくる）
	systems_.clear();
	systems_.push_back(std::make_unique<PlayerInputSystem>());

	auto scriptSys = std::make_unique<ScriptSystem>();
	scriptSys->SetScene(this);
	systems_.push_back(std::move(scriptSys));

	systems_.push_back(std::make_unique<CharacterMovementSystem>());
	systems_.push_back(std::make_unique<PhysicsSystem>());
	systems_.push_back(std::make_unique<CameraFollowSystem>());
	systems_.push_back(std::make_unique<HealthSystem>());
	systems_.push_back(std::make_unique<CombatSystem>());
	systems_.push_back(std::make_unique<AudioSystem>());
	systems_.push_back(std::make_unique<UISystem>());
	systems_.push_back(std::make_unique<MotionSystem>());
	systems_.push_back(std::make_unique<CleanupSystem>());

	// ★追加: プロファイラー初期化
	profiler_.Initialize((int)systems_.size());
	const char* sysNames[] = { "PlayerInput", "ScriptSystem", "CharMove", "Physics", "CameraFollow", "Health", "Combat", "Audio", "UISystem", "Motion", "Cleanup" };
	for (int i = 0; i < (int)systems_.size() && i < 11; ++i) {
		profiler_.SetSystemName(i, sysNames[i]);
	}

	// ★追加: 起動直後の状態を初期スナップショットとして保存
	initialSceneSnapshot_ = EditorUI::SaveToMemory(this);

	// 前回プレイで動的に生成されたオブジェクトの削除
	auto bulletView = registry_.view<TagComponent>();
	for (auto entity : bulletView) {
		if (bulletView.get<TagComponent>(entity).tag == TagType::Bullet) {
			registry_.destroy(entity);
		}
	}
	auto nameView = registry_.view<NameComponent>();
	for (auto entity : nameView) {
		if (nameView.get<NameComponent>(entity).name == "Bullet" && registry_.valid(entity)) {
			registry_.destroy(entity);
		}
	}

	// 各Systemのリセット
	for (auto& sys : systems_) {
		sys->Reset(registry_);
	}

	// ★追加: 川の初期メッシュ生成
	registry_.view<RiverComponent, TransformComponent>().each([&](RiverComponent& rv, TransformComponent& tc) {
		if (rv.enabled && rv.meshHandle == 0) {
			RiverSystem::BuildRiverMesh(rv, renderer_, registry_, tc.translate);
		}
	});

	// ★追加: タグシステムの初期化
	tagCache_.clear();
	pendingTagSync_.clear();
	pendingTagRemoved_.clear();
	auto tagInitView = registry_.view<TagComponent>();
	for (auto entity : tagInitView) {
		const auto tag = tagInitView.get<TagComponent>(entity).tag;
		tagCache_[tag].push_back(entity);
	}
	// リスナー登録
	registry_.on_construct<TagComponent>().disconnect<&GameScene::OnTagAdded>(this);
	registry_.on_construct<TagComponent>().connect<&GameScene::OnTagAdded>(this);
	registry_.on_destroy<TagComponent>().disconnect<&GameScene::OnTagRemoved>(this);
	registry_.on_destroy<TagComponent>().connect<&GameScene::OnTagRemoved>(this);
	
	registry_.on_destroy<ScriptComponent>().disconnect<&GameScene::OnScriptDestroyed>(this); // ★追加
	registry_.on_destroy<ScriptComponent>().connect<&GameScene::OnScriptDestroyed>(this); // ★追加

	// NavigationManagerの初期化
	flowField_ = std::make_unique<NavigationManager>();
	flowField_->Initialize(350, 350, 2.0f, -350, -350);

	//ステージロード直後に一度地形を読み込む
	flowField_->UpdateCostMap(this);

	// ★追加: ポーズメニューの初期化
	isPaused_ = false;
	pauseMenuState_ = PauseMenuState::Main;
	pauseRegistry_.clear();
	pauseMainEntities_.clear();
	pauseSettingsEntities_.clear();
	pauseUISystem_ = std::make_unique<UISystem>();
	pauseCtx_.dt = 1.0f / 60.0f;
	pauseCtx_.camera = &camera_;
	pauseCtx_.renderer = renderer_;
	pauseCtx_.input = Engine::Input::GetInstance();
	pauseCtx_.isPlaying = true;
	pauseCtx_.scene = nullptr;
	pauseCtx_.viewportOffset = {0.0f, 0.0f};
	pauseCtx_.viewportSize = {(float)Engine::WindowDX::kW, (float)Engine::WindowDX::kH};
	CreatePauseMenu();
	CreatePauseSettingsMenu();
	// 初期状態はメニュー非表示
	for (auto e : pauseMainEntities_) pauseRegistry_.get<RectTransformComponent>(e).enabled = false;
	for (auto e : pauseSettingsEntities_) pauseRegistry_.get<RectTransformComponent>(e).enabled = false;

#ifndef _DEBUG
	// リリースビルドではシーンファイルの有無に関わらず確実にプレイ状態から開始する
	isPlaying_ = true;
#endif
}

// =====================================================
// ★ Update: 各Systemに処理を委譲
// =====================================================
void GameScene::Update() {
	if (!renderer_) return;

	// ★パフォーマンス検証: ポストプロセスを強制的に無効化
	// renderer_->SetPostProcessEnabled(false);

	// ★追加: プロファイラー フレーム開始
	profiler_.BeginFrame(std::chrono::duration<float>(std::chrono::steady_clock::now() - std::chrono::steady_clock::now()).count());
	if (dx_) {
		profiler_.frameStats.gpuPresentMs = dx_->GetLastPresentMs();
		profiler_.frameStats.gpuWaitMs = dx_->GetLastWaitGPUMs();
	}
	auto updateStart = std::chrono::high_resolution_clock::now();

	// ★追加: 行列キャッシュを毎フレームクリア
	{ ScopedTimer _t(profiler_.frameStats.matrixCacheClearMs); ClearMatrixCache(); }

	// ★追加: タグの遅延同期および削除（生成直後や破棄時の同期待ちを処理）
	// リストが空でない場合のみ処理
	{
	ScopedTimer _tagTimer(profiler_.frameStats.tagSyncMs);
	if (!pendingTagRemoved_.empty() || !pendingTagSync_.empty()) {
		// 1. 削除・変更予定のエンティティを全キャッシュから取り除く
		std::vector<entt::entity> toRemove = std::move(pendingTagRemoved_);
		// pendingTagSync に入っているものは「タグが変わる」可能性があるので、一旦古いキャッシュから消しておく
		for (auto e : pendingTagSync_) {
			toRemove.push_back(e);
		}

		for (auto e : toRemove) {
			for (auto& pair : tagCache_) {
				auto& vec = pair.second;
				// すべてのタグリストから、そのエンティティを削除
				vec.erase(std::remove(vec.begin(), vec.end(), e), vec.end());
			}
		}

		// 2. 最新のタグで同期
		std::vector<entt::entity> toSync = std::move(pendingTagSync_);
		for (auto e : toSync) {
			if (registry_.valid(e)) {
				SyncTag(e);
			}
		}
	}
	}

	static auto last = std::chrono::steady_clock::now();
	auto now = std::chrono::steady_clock::now();
	float dt = std::chrono::duration<float>(now - last).count();
	last = now;

	if (dt > 1.0f / 10.0f)
		dt = 1.0f / 60.0f; // 極端なラグ対策

	// コンテキストを更新
	ctx_.dt = dt;

	// ★追加: ESCキーでポーズ切り替え (プレイ中のみ)
	if (isPlaying_ && Engine::Input::GetInstance()->Trigger(DIK_ESCAPE)) {
		isPaused_ = !isPaused_;
		if (isPaused_) {
			// ポーズ開始: メインメニュー表示
			pauseMenuState_ = PauseMenuState::Main;
			for (auto e : pauseMainEntities_) pauseRegistry_.get<RectTransformComponent>(e).enabled = true;
			for (auto e : pauseSettingsEntities_) pauseRegistry_.get<RectTransformComponent>(e).enabled = false;
			
			// ★追加: ポーズ開始時は確実にマウスカーソルを表示する
			while (ShowCursor(TRUE) < 0);
		} else {
			// ポーズ解除: 全メニュー非表示
			for (auto e : pauseMainEntities_) pauseRegistry_.get<RectTransformComponent>(e).enabled = false;
			for (auto e : pauseSettingsEntities_) pauseRegistry_.get<RectTransformComponent>(e).enabled = false;
			
			// ★追加: ポーズ解除時は、自動的にPlayerScriptがカーソルを制御するため、
			// ここで一度カーソルを非表示にして同期を取る（非戦闘時はPlayerScriptで再表示される）
			while (ShowCursor(FALSE) >= 0);
		}
	}

	// ★追加: ポーズ中はゲームロジックをスキップし、メニューのみ更新
	if (isPaused_) {
		UpdatePauseMenu();
		return;
	}

	if (isPlaying_)
		playTime_ += dt;

	// ★ 勝利/敗北判定 (テスト用) - ★変更: Gameシーンのみ
	if (isPlaying_ && sceneName_ == "Game") {
		bool inputBlocked = Engine::Input::GetInstance()->IsGameInputBlocked();
		bool win = (!inputBlocked && Engine::Input::GetInstance()->Trigger(DIK_G));
		if (auto waveEntity = WaveManagement::GetManagerEntity(); waveEntity != entt::null && registry_.valid(waveEntity)) {
			if (WaveManagement::IsWaveEnded()) {
				win = true;
			}
		}
		bool loss = (!inputBlocked && Engine::Input::GetInstance()->Trigger(DIK_J));

		// プレイヤーの生存確認 (Viewを直接参照して同期ズレを防ぐ)
		// プレイヤーの生存確認
		const auto& players = GetEntitiesByTag(TagType::Player);
		for (auto entity : players) {
			if (registry_.valid(entity) && registry_.all_of<HealthComponent>(entity)) {
				if (registry_.get<HealthComponent>(entity).hp <= 0) {
					loss = true;
					break;
				}
			}
		}

		if (win || loss) {
			Engine::SceneParameters res;
			res.isWin = win;
			res.score = win ? 1500 : 300;
			res.clearTime = playTime_;
			Engine::SceneManager::GetInstance()->RequestChange("Result", res);
			isPlaying_ = false;
			// 修正: 即座に return せず、以降のガード (!isPlaying_) でシステムをスキップさせつつ、
			// フレーム末尾のクリーンアップ処理（pendingDestroys等）まで到達させる
		}
	}

	ctx_.camera = &camera_;
	ctx_.renderer = renderer_;
	ctx_.input = Engine::Input::GetInstance();
	ctx_.isPlaying = isPlaying_;

	// ★追加: プレイ中はゲーム入力ブロックを解除（エディタパネルが表示されていてもゲーム操作を優先）
	if (isPlaying_ && ctx_.input) {
		ctx_.input->SetGameInputBlocked(false);
	}
	ctx_.scene = this;
	ctx_.eventSystem = &eventSystem_;
	ctx_.pendingSpawns = &pendingSpawns_;

	// ★追加: ビューポート情報をデフォルトのウィンドウサイズで初期設定 (エディタ非実行時のレイアウト崩れ防止)
	ctx_.viewportOffset = { 0.0f, 0.0f };
	ctx_.viewportSize = { (float)Engine::WindowDX::kW, (float)Engine::WindowDX::kH };

	// GPU Collision Dispatch（エンジンの汎用 PhysicsSystem.h に移行したため、ここでは何もしない）

	// Animation（エンジン固有処理のため残留）
	{
	ScopedTimer _animTimer(profiler_.frameStats.animationMs);
	auto animView = registry_.view<AnimatorComponent, MeshRendererComponent>();
	if (isPlaying_) {
		std::vector<entt::entity> animEntities;
		animView.each([&](entt::entity entity, auto&, auto&) { animEntities.push_back(entity); });

		if (!animEntities.empty()) {
			Engine::JobSystem::Dispatch((uint32_t)animEntities.size(), 64, [&](uint32_t i) {
				auto entity = animEntities[i];
				auto& anim = registry_.get<AnimatorComponent>(entity);
				auto& meshWrapper = registry_.get<MeshRendererComponent>(entity);

				if (anim.enabled && anim.isPlaying) {
					anim.time += dt * 60.0f * anim.speed;
					auto* m = renderer_->GetModel(meshWrapper.modelHandle);
					if (m) {
						const auto& data = m->GetData();
						for (const auto& a : data.animations) {
							if (a.name == anim.currentAnimation) {
								if (anim.time > a.duration) {
									if (anim.loop)
										anim.time = std::fmod(anim.time, a.duration);
									else {
										anim.time = a.duration;
										anim.isPlaying = false;
									}
								}
								break;
							}
						}
					}
				}
			});
			Engine::JobSystem::Wait();
		}
	}
	}

	// パーティクルエディター
	particleEditor_.Update(dt);

	// ★ カメラ状態の切り替え (Scene/Game)
	int currentViewMode = (int)EditorUI::GetViewMode();
	if (!isPlaying_ && currentViewMode != lastViewMode_) {
		if (currentViewMode == 1) { // Scene -> Game
			editorCameraPos_ = camera_.Position();
			editorCameraRot_ = camera_.Rotation();
		} else { // Game -> Scene
			camera_.SetPosition(editorCameraPos_);
			camera_.SetRotation(editorCameraRot_);
		}
		lastViewMode_ = currentViewMode;
	}

	// ★ 全Systemを順に実行 (プロファイラー計測付き)
	for (int sysIdx = 0; sysIdx < (int)systems_.size(); ++sysIdx) {
		auto& system = systems_[sysIdx];
		if (!isPlaying_) {
			// Gameビュー時はカメラシステムだけ例外的に動かしてプレビューさせる
			if (currentViewMode == 1) {
				if (dynamic_cast<CameraFollowSystem*>(system.get())) {
					bool oldIsPlaying = ctx_.isPlaying;
					ctx_.isPlaying = true;
					float sysMs = 0.0f;
					{ ScopedTimer _st(sysMs); system->Update(registry_, ctx_); }
					profiler_.RecordSystemTime(sysIdx, sysMs);
					ctx_.isPlaying = oldIsPlaying;
				}
			}
			continue;
		}
		float sysMs = 0.0f;
		{ ScopedTimer _st(sysMs); system->Update(registry_, ctx_); }
		profiler_.RecordSystemTime(sysIdx, sysMs);
	}

	// ★ 追加: 手動デバッグカメラ操作はSceneビューかつ停止中かつビューポートにマウスがある時のみ
	if (!isPlaying_ && currentViewMode == 0 && EditorUI::IsViewportHovered()) {
		camera_.Update(*Engine::Input::GetInstance());
	}
	camera_.Tick(dt);

	// ★ ペンディングオブジェクト（弾など）をflushし、破棄要求を処理
	{
		ScopedTimer _destroyTimer(profiler_.frameStats.pendingDestroyMs);
		std::lock_guard<std::recursive_mutex> lock(spawnMutex_);

		if (!pendingSpawns_.storage<entt::entity>().empty()) {
			// 一旦、pendingSpawns_ をダミーとして運用するか、直接 `registry_.create()` するのでここは実質空になる
			pendingSpawns_.clear();
		}

		while (!pendingDestroys_.empty()) {
			std::vector<entt::entity> currentDestroys;
			currentDestroys.swap(pendingDestroys_);
			for (auto id : currentDestroys) {
				if (registry_.valid(id)) {
					registry_.destroy(id);
				}
			}
		}
	}

	// Light System（レンダリング設定のため残留）
	{
	ScopedTimer _lightTimer(profiler_.frameStats.lightSystemMs);
	if (renderer_) {
		int plCount = 0;
		int slCount = 0;
		bool hasDirLight = false;

		auto dirLightView = registry_.view<DirectionalLightComponent, TransformComponent>();
		dirLightView.each([&](auto, const DirectionalLightComponent& dl, const TransformComponent& tc) {
			if (dl.enabled && !hasDirLight) {
				Engine::Matrix4x4 mat = tc.GetTransform().ToMatrix();
				Engine::Vector3 dir = {mat.m[2][0], mat.m[2][1], mat.m[2][2]};
				Engine::Vector3 color = {dl.color.x * dl.intensity, dl.color.y * dl.intensity, dl.color.z * dl.intensity};
				renderer_->SetDirectionalLight(dir, color, true);
				hasDirLight = true;
			}
		});

		auto plView = registry_.view<PointLightComponent, TransformComponent>();
		plView.each([&](auto, const PointLightComponent& pl, const TransformComponent& tc) {
			if (pl.enabled && plCount < Engine::Renderer::kMaxPointLights) {
				Engine::Vector3 pos = {tc.translate.x, tc.translate.y, tc.translate.z};
				Engine::Vector3 color = {pl.color.x * pl.intensity, pl.color.y * pl.intensity, pl.color.z * pl.intensity};
				Engine::Vector3 atten = {pl.atten.x, pl.atten.y, pl.atten.z};
				renderer_->SetPointLight(plCount, pos, color, pl.range, atten, true);
				plCount++;
			}
		});

		auto slView = registry_.view<SpotLightComponent, TransformComponent>();
		slView.each([&](auto, const SpotLightComponent& sl, const TransformComponent& tc) {
			if (sl.enabled && slCount < Engine::Renderer::kMaxSpotLights) {
				Engine::Matrix4x4 mat = tc.GetTransform().ToMatrix();
				Engine::Vector3 dir = {mat.m[2][0], mat.m[2][1], mat.m[2][2]};
				Engine::Vector3 pos = {tc.translate.x, tc.translate.y, tc.translate.z};
				Engine::Vector3 color = {sl.color.x * sl.intensity, sl.color.y * sl.intensity, sl.color.z * sl.intensity};
				Engine::Vector3 atten = {sl.atten.x, sl.atten.y, sl.atten.z};
				renderer_->SetSpotLight(slCount, pos, dir, color, sl.range, sl.innerCos, sl.outerCos, atten, true);
				slCount++;
			}
		});

		if (!hasDirLight) {
			renderer_->SetDirectionalLight({0, -1, 0}, {0, 0, 0}, false);
		}
		for (int i = plCount; i < Engine::Renderer::kMaxPointLights; ++i) {
			renderer_->SetPointLight(i, {0, 0, 0}, {0, 0, 0}, 0, {1, 0, 0}, false);
		}
		for (int i = slCount; i < Engine::Renderer::kMaxSpotLights; ++i) {
			renderer_->SetSpotLight(i, {0, 0, 0}, {0, -1, 0}, {0, 0, 0}, 0, 0.0f, 0.0f, {1, 0, 0}, false);
		}
	}
	} // lightTimer scope end

	// パーティクルエミッターコンポーネント
	{
	ScopedTimer _particleTimer(profiler_.frameStats.particleUpdateMs);
	auto peView = registry_.view<ParticleEmitterComponent, TransformComponent, NameComponent>();
	peView.each([&](auto, ParticleEmitterComponent& pe, const TransformComponent& tc, const NameComponent& nc) {
		if (!pe.enabled)
			return;

		if (!pe.isInitialized && renderer_) {
			pe.emitter.Initialize(*renderer_, nc.name + "_Emitter");
			if (!pe.assetPath.empty()) {
				pe.emitter.LoadFromJson(pe.assetPath);
			}
			pe.isInitialized = true;
		}

		pe.emitter.params.position = {tc.translate.x, tc.translate.y, tc.translate.z};
		pe.emitter.Update(dt);
	});
	}

	// ★追加: プロファイラー データ収集 & フレーム終了
	{
		auto updateEnd = std::chrono::high_resolution_clock::now();
		profiler_.frameStats.totalUpdateMs = std::chrono::duration<float, std::milli>(updateEnd - updateStart).count();
		profiler_.BeginFrame(dt);
		profiler_.CollectEntityStats(registry_, tagCache_);
	}
}

// ★ 汎用スポーン
entt::entity GameScene::CreateEntity(const std::string& name) {
	std::lock_guard<std::recursive_mutex> lock(spawnMutex_);
	auto entity = registry_.create();
	registry_.emplace<NameComponent>(entity, name);
	registry_.emplace<TransformComponent>(entity);
	// staticTerrainDirty_ = true; // 動的エンティティ(弾や敵)の生成で地形キャッシュを無効化しないようにする
	return entity;
}

// ★追加: IDでオブジェクトを検索し、破棄フラグを立てる
void GameScene::DestroyObject(uint32_t id) {
	std::lock_guard<std::recursive_mutex> lock(spawnMutex_);
	// IDをそのままentt::entityとして扱う（ダウンキャスト）
	pendingDestroys_.push_back(static_cast<entt::entity>(id));
	// staticTerrainDirty_ = true; // キャッシュ無効化はGetHeightAt内でのvalid判定に任せる
}


// ★追加: 名前でオブジェクトを検索
entt::entity GameScene::FindObjectByName(const std::string& name) {
	auto view = registry_.view<NameComponent>();
	for (auto entity : view) {
		if (view.get<NameComponent>(entity).name == name) {
			return entity;
		}
	}
	return entt::null;
}

// ★追加: 指定座標のメッシュ表面的高さを取得
float GameScene::GetHeightAt(float x, float z, float startY, uint32_t excludeId) {
	float maxHeight = -1000.0f;
	bool hitAny = false;

	// 指定された startY (またはデフォルト 1000) から下向きにレイを飛ばす
	DirectX::XMVECTOR rayPos = DirectX::XMVectorSet(x, startY, z, 1.0f);
	DirectX::XMVECTOR rayDir = DirectX::XMVectorSet(0.0f, -1.0f, 0.0f, 0.0f);

	if (staticTerrainDirty_) {
		staticTerrainEntities_.clear();
		auto view = registry_.view<TransformComponent>();
		for (auto entity : view) {
			bool isEnemyOrBullet = false;
			if (registry_.all_of<TagComponent>(entity)) {
				const auto tag = registry_.get<TagComponent>(entity).tag;
				if (tag == TagType::Enemy || tag == TagType::Bullet || tag == TagType::Player || tag == TagType::Sword || tag == TagType::PlayerSword || tag == TagType::Projectile || tag == TagType::Pipe || tag == TagType::Canon || tag == TagType::BulletTank ||
					tag == TagType::PipeCannon || tag == TagType::VFX || tag == TagType::HitDistortion_VFX || tag == TagType::Missile || tag == TagType::Experience || tag == TagType::ExperienceOrb) {
					isEnemyOrBullet = true;
				}
			}
			if (isEnemyOrBullet)
				continue;

			uint32_t modelHandle = 0;
			if (registry_.all_of<GpuMeshColliderComponent>(entity)) {
				auto& mc = registry_.get<GpuMeshColliderComponent>(entity);
				if (mc.enabled)
					modelHandle = mc.meshHandle;
			}
			if (modelHandle == 0 && registry_.all_of<MeshRendererComponent>(entity)) {
				auto& mr = registry_.get<MeshRendererComponent>(entity);
				if (mr.enabled)
					modelHandle = mr.modelHandle;
			}

			if (modelHandle != 0) {
				staticTerrainEntities_.push_back(entity);
			}
		}
		staticTerrainDirty_ = false;
	}

	for (auto entity : staticTerrainEntities_) {
		if (excludeId != 0 && static_cast<uint32_t>(entity) == excludeId)
			continue;

		if (!registry_.valid(entity)) {
			staticTerrainDirty_ = true; // 次回再構築
			continue;
		}

		uint32_t modelHandle = 0;
		if (registry_.all_of<GpuMeshColliderComponent>(entity)) {
			auto& mc = registry_.get<GpuMeshColliderComponent>(entity);
			if (mc.enabled)
				modelHandle = mc.meshHandle;
		} else if (registry_.all_of<MeshRendererComponent>(entity)) {
			auto& mr = registry_.get<MeshRendererComponent>(entity);
			if (mr.enabled)
				modelHandle = mr.modelHandle;
		}

		if (modelHandle == 0)
			continue;

		auto* model = renderer_->GetModel(modelHandle);
		if (!model)
			continue;

		float dist = 0.0f;
		Engine::Vector3 hitPoint;
		Engine::Matrix4x4 worldMat = this->GetWorldMatrix(static_cast<int>(entity));

		// 高速化：ワールド空間での簡易な円柱バウンディング判定
		float scaleX = std::sqrt(worldMat.m[0][0]*worldMat.m[0][0] + worldMat.m[0][1]*worldMat.m[0][1] + worldMat.m[0][2]*worldMat.m[0][2]);
		float scaleZ = std::sqrt(worldMat.m[2][0]*worldMat.m[2][0] + worldMat.m[2][1]*worldMat.m[2][1] + worldMat.m[2][2]*worldMat.m[2][2]);
		float radiusX = (model->GetData().max.x - model->GetData().min.x) * scaleX * 0.5f;
		float radiusZ = (model->GetData().max.z - model->GetData().min.z) * scaleZ * 0.5f;
		float maxRadius = std::max(radiusX, radiusZ) * 1.5f; // 回転を考慮した余裕
		
		float cx = worldMat.m[3][0];
		float cz = worldMat.m[3][2];
		
		// 地形のような巨大メッシュ(maxRadius > 100)以外は、範囲外ならスキップ
		if (maxRadius < 100.0f) {
			if (std::abs(x - cx) > maxRadius || std::abs(z - cz) > maxRadius) {
				continue;
			}
		}

		if (model->RayCast(rayPos, rayDir, worldMat, dist, hitPoint)) {
			if (hitPoint.y > maxHeight) {
				maxHeight = hitPoint.y;
				hitAny = true;
			}
		}
	}

	return hitAny ? maxHeight : -10000.0f;
}

bool GameScene::RayCast(const Engine::Vector3& origin, const Engine::Vector3& direction, float maxDist, uint32_t excludeId, float& outDist) {
	bool hitAny = false;
	float minDist = maxDist;

	DirectX::XMVECTOR rayPos = DirectX::XMLoadFloat3(reinterpret_cast<const DirectX::XMFLOAT3*>(&origin));
	DirectX::XMVECTOR rayDir = DirectX::XMLoadFloat3(reinterpret_cast<const DirectX::XMFLOAT3*>(&direction));

	// staticTerrainEntities_ は GetHeightAt で構築されていると仮定（または同じキャッシュを利用）
	// もし空なら、ここで構築が必要だが、通常は GetHeightAt が先に呼ばれるため構築済み
	if (staticTerrainDirty_) {
		// 再構築トリガー (GetHeightAt側と同じロジックを呼ぶか、ここで簡易的に再構築)
		// 面倒な場合はここで直接再構築
		staticTerrainEntities_.clear();
		auto view = registry_.view<TransformComponent>();
		for (auto entity : view) {
			bool isEnemyOrBullet = false;
			if (registry_.all_of<TagComponent>(entity)) {
				const auto tag = registry_.get<TagComponent>(entity).tag;
				if (tag == TagType::Enemy || tag == TagType::Bullet || tag == TagType::Player || tag == TagType::Sword || tag == TagType::PlayerSword || tag == TagType::Projectile || tag == TagType::Pipe || tag == TagType::Canon || tag == TagType::BulletTank ||
					tag == TagType::PipeCannon || tag == TagType::VFX || tag == TagType::HitDistortion_VFX || tag == TagType::Missile || tag == TagType::Experience || tag == TagType::ExperienceOrb) {
					isEnemyOrBullet = true;
				}
			}
			if (isEnemyOrBullet)
				continue;

			uint32_t modelHandle = 0;
			if (registry_.all_of<GpuMeshColliderComponent>(entity)) {
				auto& mc = registry_.get<GpuMeshColliderComponent>(entity);
				if (mc.enabled)
					modelHandle = mc.meshHandle;
			}
			if (modelHandle == 0 && registry_.all_of<MeshRendererComponent>(entity)) {
				auto& mr = registry_.get<MeshRendererComponent>(entity);
				if (mr.enabled)
					modelHandle = mr.modelHandle;
			}

			if (modelHandle != 0) {
				staticTerrainEntities_.push_back(entity);
			}
		}
		staticTerrainDirty_ = false;
	}

	for (auto entity : staticTerrainEntities_) {
		if (excludeId != 0 && static_cast<uint32_t>(entity) == excludeId)
			continue;

		if (!registry_.valid(entity)) {
			staticTerrainDirty_ = true;
			continue;
		}

		uint32_t modelHandle = 0;
		if (registry_.all_of<GpuMeshColliderComponent>(entity)) {
			auto& mc = registry_.get<GpuMeshColliderComponent>(entity);
			if (mc.enabled)
				modelHandle = mc.meshHandle;
		} else if (registry_.all_of<MeshRendererComponent>(entity)) {
			auto& mr = registry_.get<MeshRendererComponent>(entity);
			if (mr.enabled)
				modelHandle = mr.modelHandle;
		}

		if (modelHandle == 0)
			continue;

		auto* model = renderer_->GetModel(modelHandle);
		if (!model)
			continue;

		float dist = 0.0f;
		Engine::Vector3 hitPoint;
		Engine::Matrix4x4 worldMat = GetWorldMatrix(static_cast<int>(entity));

		// 高速化：ワールド空間での簡易な球バウンディング判定
		float scaleX = std::sqrt(worldMat.m[0][0]*worldMat.m[0][0] + worldMat.m[0][1]*worldMat.m[0][1] + worldMat.m[0][2]*worldMat.m[0][2]);
		float scaleY = std::sqrt(worldMat.m[1][0]*worldMat.m[1][0] + worldMat.m[1][1]*worldMat.m[1][1] + worldMat.m[1][2]*worldMat.m[1][2]);
		float scaleZ = std::sqrt(worldMat.m[2][0]*worldMat.m[2][0] + worldMat.m[2][1]*worldMat.m[2][1] + worldMat.m[2][2]*worldMat.m[2][2]);
		float radiusX = (model->GetData().max.x - model->GetData().min.x) * scaleX * 0.5f;
		float radiusY = (model->GetData().max.y - model->GetData().min.y) * scaleY * 0.5f;
		float radiusZ = (model->GetData().max.z - model->GetData().min.z) * scaleZ * 0.5f;
		float maxRadius = std::max({radiusX, radiusY, radiusZ}) * 1.5f;
		
		float cx = worldMat.m[3][0];
		float cy = worldMat.m[3][1];
		float cz = worldMat.m[3][2];
		
		if (maxRadius < 100.0f) {
			// レイの始点とオブジェクトの中心距離をチェック (rayDirは正規化されている前提)
			float dx = origin.x - cx;
			float dy = origin.y - cy;
			float dz = origin.z - cz;
			float sqDist = dx*dx + dy*dy + dz*dz;
			// もし始点がオブジェクトのバウンディングスフィア + maxDist よりも遠ければ絶対に当たらない
			if (sqDist > (maxRadius + maxDist) * (maxRadius + maxDist)) {
				continue;
			}
		}

		if (model->RayCast(rayPos, rayDir, worldMat, dist, hitPoint)) {
			if (dist < minDist) {
				minDist = dist;
				hitAny = true;
			}
		}
	}

	if (hitAny) {
		outDist = minDist;
		return true;
	}
	return false;
}

Engine::Matrix4x4 GameScene::GetWorldMatrix(int entityId) const { return GetWorldMatrixRecursive(static_cast<entt::entity>(entityId), 0); }

Engine::Matrix4x4 GameScene::GetWorldMatrixRecursive(entt::entity e, int depth) const {
	if (depth > 32)
		return Engine::Matrix4x4::Identity();

	auto it = matrixCache_.find(e);
	if (it != matrixCache_.end())
		return it->second;

	if (!registry_.valid(e) || !registry_.all_of<TransformComponent>(e))
		return Engine::Matrix4x4::Identity();
	const auto& tc = registry_.get<TransformComponent>(e);
	Engine::Matrix4x4 local = tc.ToMatrix();

	Engine::Matrix4x4 world = local;
	if (registry_.all_of<HierarchyComponent>(e)) {
		const auto& hc = registry_.get<HierarchyComponent>(e);
		if (hc.parentId != entt::null && registry_.valid(hc.parentId)) {
			world = Engine::Matrix4x4::Multiply(local, GetWorldMatrixRecursive(hc.parentId, depth + 1));
		}
	}
	matrixCache_[e] = world;
	return world;
}

void GameScene::Draw() {
	if (!renderer_)
		return;
	auto drawStart = std::chrono::high_resolution_clock::now();

	// ★★★ GPU負荷テスト: Hキーで大量オブジェクト生成 ★★★
	{
		static int stressTestGridSize = 0;
		if (!isPlaying_)
			stressTestGridSize = 0; // Stop時にリセット
		static bool prevH = false;
		bool currH = (GetAsyncKeyState('H') & 0x8000) != 0;
		// ★修正: エディタUI操作中は誤発火を防止
		if (Engine::Input::GetInstance()->IsGameInputBlocked()) currH = false;
		if (currH && !prevH) {
			stressTestGridSize += 32; // add 32x32 = 1024 objects each press
			std::string msg = "[StressTest] Triggered! Grid size: " + std::to_string(stressTestGridSize) + "x" + std::to_string(stressTestGridSize) + " (" +
			                  std::to_string(stressTestGridSize * stressTestGridSize) + " objects)\n";
			OutputDebugStringA(msg.c_str());
		}
		prevH = currH;

		if (stressTestGridSize > 0) {
			uint32_t cubeModel = renderer_->LoadObjMesh("Resources/Models/cube/cube.obj");
			uint32_t whiteTex = renderer_->LoadTexture2D("Resources/Textures/white1x1.png");

			float spacing = 2.0f;
			float startOffset = -(stressTestGridSize / 2.0f) * spacing;

			for (int z = 0; z < stressTestGridSize; ++z) {
				for (int x = 0; x < stressTestGridSize; ++x) {
					Engine::Transform t;
					t.translate = {startOffset + x * spacing, 10.0f, startOffset + z * spacing};
					t.rotate = {0, 0, 0};
					t.scale = {0.5f, 0.5f, 0.5f};
					Engine::Vector4 color = {0.3f + (x % 5) * 0.15f, 0.3f + (z % 5) * 0.15f, 0.5f + ((x + z) % 3) * 0.2f, 1.0f};
					renderer_->DrawMeshInstanced(cubeModel, whiteTex, t, color, "Default");
				}
			}
		}
	}
	// ★★★ GPU負荷テスト ここまで ★★★

	renderer_->SetCamera(camera_);
#ifdef USE_IMGUI
	if (!isPlaying_) {
		DrawEditorGizmos();
	}
	// デバッグ時のみフローフィールドを表示
	if (!isPlaying_ && flowField_) {
		flowField_->DrawDebug(this);
	}
#endif

	// ★ 高速タグ検索を用いてプレイヤー位置を同期（O(N) -> O(1)）
	const auto& players = GetEntitiesByTag(TagType::Player);
	if (!players.empty()) {
		entt::entity playerEntity = players[0];
		if (registry_.valid(playerEntity) && registry_.all_of<TransformComponent>(playerEntity)) {
			auto& tc = registry_.get<TransformComponent>(playerEntity);
			renderer_->SetPlayerPos(Engine::Vector3{tc.translate.x, tc.translate.y, tc.translate.z});
		}
	}

	auto renderView = registry_.view<TransformComponent, MeshRendererComponent>();
	int iterCount = 0, meshCount = 0, skinnedCount = 0, culledCount = 0;
	float meshLoopMs = 0.0f;
	{
	ScopedTimer _meshLoop(meshLoopMs);

	Engine::FrustumCullSettings cullSettings;
	// 描画FOVより広めにカリング（画面端のポップイン/アウト軽減）
	cullSettings.fovScale = 1.25f;
	cullSettings.padHorizontal = 0.45f;
	cullSettings.padTop = 0.35f;
	cullSettings.padBottom = 0.70f; // 画面下（手前の地面・経路）を優先的に残す
	cullSettings.padNear = 0.25f;
	cullSettings.padFar = 0.15f;
	Engine::Frustum frustum = Engine::Frustum::FromCamera(camera_, cullSettings);

	std::vector<entt::entity> sortedEntities;
	for (auto entity : renderView) {
		++iterCount;
		const auto& mr = renderView.get<MeshRendererComponent>(entity);
		if (!mr.enabled || mr.modelHandle == 0)
			continue;

		Engine::Matrix4x4 world = this->GetWorldMatrix(static_cast<int>(entity));
		
		bool shouldCull = true;
		if (mr.shaderName == "Distortion" || mr.shaderName == "GlassShatter" || mr.shaderName == "ProceduralSmoke" || mr.shaderName == "ProceduralSmokeInstanced") {
			shouldCull = false; // エフェクト系は常に描画
		} else if (registry_.try_get<TagComponent>(entity) && registry_.get<TagComponent>(entity).tag == TagType::VFX) {
			shouldCull = false;
		} else if (auto* nc = registry_.try_get<NameComponent>(entity)) {
			// ★追加: 巨大なステージ・地形・スカイボックスなどはカリング対象外にし、視点角度による突然の消失を防ぐ
			if (nc->name == "Ground" || nc->name == "Terrain" || nc->name == "SkyBox" || nc->name == "Skybox" || nc->name == "Stage") {
				shouldCull = false;
			}
		}

		if (shouldCull && !IsEntityVisibleInFrustum(frustum, renderer_, registry_, entity, mr.modelHandle, world)) {
			++culledCount;
			continue;
		}
		sortedEntities.push_back(entity);
	}

	// バッチ効率化のため、シェーダー名 -> メッシュハンドル -> テクスチャハンドル でソート
	std::sort(sortedEntities.begin(), sortedEntities.end(), [&](entt::entity a, entt::entity b) {
		const auto& mrA = renderView.get<MeshRendererComponent>(a);
		const auto& mrB = renderView.get<MeshRendererComponent>(b);
		if (mrA.shaderName != mrB.shaderName) return mrA.shaderName < mrB.shaderName;
		if (mrA.modelHandle != mrB.modelHandle) return mrA.modelHandle < mrB.modelHandle;
		if (mrA.textureHandle != mrB.textureHandle) return mrA.textureHandle < mrB.textureHandle;
		return mrA.extraTextureHandles < mrB.extraTextureHandles;
	});

	for (auto entity : sortedEntities) {
		Engine::Vector4 color = {1, 1, 1, 1};
		if (auto* cc = registry_.try_get<ColorComponent>(entity)) {
			color = {cc->color.x, cc->color.y, cc->color.z, cc->color.w};
		}

		const auto& mr = renderView.get<MeshRendererComponent>(entity);
		if (mr.enabled && mr.modelHandle != 0) {
			bool hasAnim = false;
			std::vector<Engine::Matrix4x4> bonePalette;

			if (auto* anim = registry_.try_get<AnimatorComponent>(entity)) {
				if (anim->enabled && !anim->currentAnimation.empty()) {
					auto* m = renderer_->GetModel(mr.modelHandle);
					if (m) {
						const auto& data = m->GetData();
						const Engine::Animation* currAnim = nullptr;
						for (const auto& a : data.animations) {
							if (a.name == anim->currentAnimation) {
								currAnim = &a;
								break;
							}
						}
						if (currAnim) {
							bonePalette.resize(data.bones.size());
							for (auto& b : bonePalette)
								b = Engine::Matrix4x4::Identity();
							m->UpdateSkeleton(data.rootNode, Engine::Matrix4x4::Identity(), *currAnim, anim->time, bonePalette);
							hasAnim = true;
						}
					}
				}
			}

			Engine::Matrix4x4 world = this->GetWorldMatrix(static_cast<int>(entity));
			if (hasAnim) {
				++skinnedCount;
				renderer_->DrawSkinnedMesh(mr.modelHandle, mr.textureHandle, world, bonePalette, {color.x * mr.color.x, color.y * mr.color.y, color.z * mr.color.z, color.w * mr.color.w});
			} else {
				++meshCount;
				if (mr.shaderName == "Distortion" || mr.shaderName == "GlassShatter") {
					// ★ 特殊エフェクト系のみ個別描画
					renderer_->DrawMesh(mr.modelHandle, mr.textureHandle, world, {color.x * mr.color.x, color.y * mr.color.y, color.z * mr.color.z, color.w * mr.color.w}, mr.shaderName);
				} else {
					// ★ Toon含む通常シェーダーはインスタンシング（一括描画）
					renderer_->DrawMeshInstanced(
						mr.modelHandle, mr.textureHandle, world, {color.x * mr.color.x, color.y * mr.color.y, color.z * mr.color.z, color.w * mr.color.w}, mr.shaderName, mr.extraTextureHandles);
				}
			}
		}
	}

	// ★追加: 川コンポーネントの描画 (独立したループで処理し、テクスチャロードをキャッシュ)
	auto riverView = registry_.view<RiverComponent>();
	for (auto entity : riverView) {
		auto& rv = riverView.get<RiverComponent>(entity);
		if (rv.enabled && rv.meshHandle != 0) {
			if (rv.textureHandle == 0 && !rv.texturePath.empty()) {
				rv.textureHandle = renderer_->LoadTexture2D(rv.texturePath);
			}
			Engine::Transform identity;
			identity.translate = {0, 0, 0};
			identity.rotate = {0, 0, 0};
			identity.scale = {1, 1, 1};
			renderer_->DrawMesh(rv.meshHandle, rv.textureHandle, identity, {rv.flowSpeed, rv.uvScale, 0.0f, 0.0f}, "River");
		}
	}

	} // meshLoop scope end
	profiler_.frameStats.drawMeshLoopMs = meshLoopMs;
	profiler_.frameStats.drawIteratedCount = iterCount;
	profiler_.frameStats.drawMeshCount = meshCount;
	profiler_.frameStats.drawSkinnedCount = skinnedCount;
	profiler_.frameStats.drawCulledCount = culledCount;

#ifdef USE_IMGUI
	if (!isPlaying_) {
		ScopedTimer _gizmo(profiler_.frameStats.drawGizmoMs);
		DrawSelectionHighlight();
		DrawLightGizmos();
	}
#endif
	{
	ScopedTimer _particle(profiler_.frameStats.drawParticleMs);
	auto peView = registry_.view<ParticleEmitterComponent>();
	peView.each([&](auto, ParticleEmitterComponent& pe) {
		if (pe.enabled) {
			pe.emitter.Draw(camera_);
		}
	});
	} // particle scope end

	// ★ 各Systemの描画処理を呼び出す（UISystem等）
	{
	ScopedTimer _sysDraw(profiler_.frameStats.drawSystemMs);
	for (auto& system : systems_) {
		system->Draw(registry_, ctx_);
	}
	}

	// ★追加: Draw時間計測
	auto drawEnd = std::chrono::high_resolution_clock::now();
	profiler_.frameStats.totalDrawMs = std::chrono::duration<float, std::milli>(drawEnd - drawStart).count();
}

extern GizmoMode currentGizmoMode;
void GameScene::DrawUI() {
	auto uiStart = std::chrono::high_resolution_clock::now();
	if (!isPlaying_ && EditorUI::GetViewMode() != ViewMode::Game)
		return;

	bool oldIsPlaying = ctx_.isPlaying;
	if (!isPlaying_) ctx_.isPlaying = true;

	for (auto& sys : systems_) {
		sys->DrawUI(registry_, ctx_);
	}

	ctx_.isPlaying = oldIsPlaying;

	// ★追加: ポーズメニューの描画
	if (isPaused_ && pauseUISystem_) {
		// 半透明の暗い背景オーバーレイ
		if (renderer_) {
			Engine::Renderer::SpriteDesc overlay;
			overlay.x = 0.0f;
			overlay.y = 0.0f;
			overlay.w = (float)Engine::WindowDX::kW;
			overlay.h = (float)Engine::WindowDX::kH;
			overlay.color = {0.0f, 0.0f, 0.0f, 0.6f};
			overlay.layer = 100; // 最前面に描画
			renderer_->DrawSprite(0, overlay);
		}
		pauseUISystem_->DrawUI(pauseRegistry_, pauseCtx_);
	}

	// ★追加: DrawUI時間計測 & プロファイラーオーバーレイ表示
	auto uiEnd = std::chrono::high_resolution_clock::now();
	profiler_.frameStats.totalDrawUIMs = std::chrono::duration<float, std::milli>(uiEnd - uiStart).count();
	profiler_.EndFrame();
#ifndef NDEBUG
	profiler_.DrawImGui();
#endif
}

extern bool gizmoDragging;
extern int gizmoDragAxis;

void GameScene::DrawSelectionHighlight() {
	if (!renderer_)
		return;

	for (auto entity : selectedEntities_) {
		if (!registry_.valid(entity) || !registry_.all_of<TransformComponent>(entity))
			continue;

		auto& tc = registry_.get<TransformComponent>(entity);
		Engine::Vector3 pos = {tc.translate.x, tc.translate.y, tc.translate.z};

		Engine::Matrix4x4 mat = this->GetWorldMatrix(static_cast<int>(entity));
		DirectX::XMMATRIX worldMat = DirectX::XMLoadFloat4x4(reinterpret_cast<DirectX::XMFLOAT4X4*>(&mat));

		Engine::Vector4 hlColor = {1.0f, 0.85f, 0.0f, 1.0f};
		Engine::Vector3 v[8] = {
		    {-1.0f, -1.0f, -1.0f},
            {1.0f,  -1.0f, -1.0f},
            {1.0f,  1.0f,  -1.0f},
            {-1.0f, 1.0f,  -1.0f},
            {-1.0f, -1.0f, 1.0f },
            {1.0f,  -1.0f, 1.0f },
            {1.0f,  1.0f,  1.0f },
            {-1.0f, 1.0f,  1.0f },
		};

		for (int i = 0; i < 8; ++i) {
			DirectX::XMVECTOR p = DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(v[i].x, v[i].y, v[i].z, 1.0f), worldMat);
			DirectX::XMStoreFloat3(reinterpret_cast<DirectX::XMFLOAT3*>(&v[i]), p);
		}
		int edges[][2] = {
		    {0, 1},
            {1, 2},
            {2, 3},
            {3, 0},
            {4, 5},
            {5, 6},
            {6, 7},
            {7, 4},
            {0, 4},
            {1, 5},
            {2, 6},
            {3, 7}
        };
		for (auto& eg : edges)
			renderer_->DrawLine3D(v[eg[0]], v[eg[1]], hlColor, true);

		if (registry_.all_of<BoxColliderComponent>(entity)) {
			const auto& bc = registry_.get<BoxColliderComponent>(entity);
			if (bc.enabled) {
				float hx = bc.size.x * 0.5f, hy = bc.size.y * 0.5f, hz = bc.size.z * 0.5f;
				// ... Draw lines ...
				Engine::Vector3 cp = {bc.center.x, bc.center.y, bc.center.z};
				Engine::Vector4 colColor = {0.2f, 1.0f, 0.2f, 0.8f};
				Engine::Vector3 cv[8] = {
				    {cp.x - hx, cp.y - hy, cp.z - hz},
                    {cp.x + hx, cp.y - hy, cp.z - hz},
                    {cp.x + hx, cp.y + hy, cp.z - hz},
                    {cp.x - hx, cp.y + hy, cp.z - hz},
				    {cp.x - hx, cp.y - hy, cp.z + hz},
                    {cp.x + hx, cp.y - hy, cp.z + hz},
                    {cp.x + hx, cp.y + hy, cp.z + hz},
                    {cp.x - hx, cp.y + hy, cp.z + hz},
				};
				for (int i = 0; i < 8; ++i) {
					DirectX::XMVECTOR p = DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(cv[i].x, cv[i].y, cv[i].z, 1.0f), worldMat);
					DirectX::XMStoreFloat3(reinterpret_cast<DirectX::XMFLOAT3*>(&cv[i]), p);
				}
				for (auto& eg : edges)
					renderer_->DrawLine3D(cv[eg[0]], cv[eg[1]], colColor, true);
			}
		}

		if (registry_.all_of<GpuMeshColliderComponent>(entity)) {
			const auto& gmc = registry_.get<GpuMeshColliderComponent>(entity);
			if (gmc.enabled) {
				Engine::Vector4 gColor = gmc.isIntersecting ? Engine::Vector4{1.0f, 0.2f, 0.2f, 0.8f} : Engine::Vector4{0.2f, 0.2f, 1.0f, 0.8f};
				float hs = 1.0f;
				Engine::Vector3 cv[8] = {
				    {-hs, -hs, -hs},
                    {hs,  -hs, -hs},
                    {hs,  hs,  -hs},
                    {-hs, hs,  -hs},
                    {-hs, -hs, hs },
                    {hs,  -hs, hs },
                    {hs,  hs,  hs },
                    {-hs, hs,  hs }
                };
				for (int i = 0; i < 8; ++i) {
					DirectX::XMVECTOR p = DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(cv[i].x, cv[i].y, cv[i].z, 1.0f), worldMat);
					DirectX::XMStoreFloat3(reinterpret_cast<DirectX::XMFLOAT3*>(&cv[i]), p);
				}
				for (auto& eg : edges)
					renderer_->DrawLine3D(cv[eg[0]], cv[eg[1]], gColor, true);
			}
		}

		if (registry_.all_of<HitboxComponent>(entity)) {
			const auto& hb = registry_.get<HitboxComponent>(entity);
			if (hb.enabled) {
				float hx = hb.size.x * 0.5f, hy = hb.size.y * 0.5f, hz = hb.size.z * 0.5f;
				Engine::Vector3 cp = {hb.center.x, hb.center.y, hb.center.z};
				Engine::Vector4 hbColor = hb.isActive ? Engine::Vector4{1.0f, 0.2f, 0.2f, 1.0f} : Engine::Vector4{1.0f, 0.2f, 0.2f, 0.3f};
				Engine::Vector3 hv[8] = {
				    {cp.x - hx, cp.y - hy, cp.z - hz},
                    {cp.x + hx, cp.y - hy, cp.z - hz},
                    {cp.x + hx, cp.y + hy, cp.z - hz},
                    {cp.x - hx, cp.y + hy, cp.z - hz},
				    {cp.x - hx, cp.y - hy, cp.z + hz},
                    {cp.x + hx, cp.y - hy, cp.z + hz},
                    {cp.x + hx, cp.y + hy, cp.z + hz},
                    {cp.x - hx, cp.y + hy, cp.z + hz},
				};
				for (int i = 0; i < 8; ++i) {
					DirectX::XMVECTOR p = DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(hv[i].x, hv[i].y, hv[i].z, 1.0f), worldMat);
					DirectX::XMStoreFloat3(reinterpret_cast<DirectX::XMFLOAT3*>(&hv[i]), p);
				}
				for (auto& eg : edges)
					renderer_->DrawLine3D(hv[eg[0]], hv[eg[1]], hbColor, true);
			}
		}

		if (registry_.all_of<HurtboxComponent>(entity)) {
			const auto& hb = registry_.get<HurtboxComponent>(entity);
			if (hb.enabled) {
				float hx = hb.size.x * 0.5f, hy = hb.size.y * 0.5f, hz = hb.size.z * 0.5f;
				Engine::Vector3 cp = {hb.center.x, hb.center.y, hb.center.z};
				Engine::Vector4 hbColor = {0.2f, 1.0f, 0.5f, 0.6f};
				Engine::Vector3 hv[8] = {
				    {cp.x - hx, cp.y - hy, cp.z - hz},
                    {cp.x + hx, cp.y - hy, cp.z - hz},
                    {cp.x + hx, cp.y + hy, cp.z - hz},
                    {cp.x - hx, cp.y + hy, cp.z - hz},
				    {cp.x - hx, cp.y - hy, cp.z + hz},
                    {cp.x + hx, cp.y - hy, cp.z + hz},
                    {cp.x + hx, cp.y + hy, cp.z + hz},
                    {cp.x - hx, cp.y + hy, cp.z + hz},
				};
				for (int i = 0; i < 8; ++i) {
					DirectX::XMVECTOR p = DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(hv[i].x, hv[i].y, hv[i].z, 1.0f), worldMat);
					DirectX::XMStoreFloat3(reinterpret_cast<DirectX::XMFLOAT3*>(&hv[i]), p);
				}
				for (auto& eg : edges)
					renderer_->DrawLine3D(hv[eg[0]], hv[eg[1]], hbColor, true);
			}
		}

		DirectX::XMMATRIX gizmoMat = DirectX::XMMatrixRotationRollPitchYaw(tc.rotate.x, tc.rotate.y, tc.rotate.z) * DirectX::XMMatrixTranslation(tc.translate.x, tc.translate.y, tc.translate.z);
		auto drawLocalLine = [&](const Engine::Vector3& localP0, const Engine::Vector3& localP1, const Engine::Vector4& col) {
			DirectX::XMVECTOR p0 = DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(localP0.x, localP0.y, localP0.z, 1.0f), gizmoMat);
			DirectX::XMVECTOR p1 = DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(localP1.x, localP1.y, localP1.z, 1.0f), gizmoMat);
			Engine::Vector3 wp0, wp1;
			DirectX::XMStoreFloat3(reinterpret_cast<DirectX::XMFLOAT3*>(&wp0), p0);
			DirectX::XMStoreFloat3(reinterpret_cast<DirectX::XMFLOAT3*>(&wp1), p1);
			renderer_->DrawLine3D(wp0, wp1, col, true);
		};

		const float al = 2.0f, ar = 0.3f;
		int dAxis = (gizmoDragging && entity == selectedEntity_) ? gizmoDragAxis : -1;
		auto axCol = [](int axis, int drag) -> Engine::Vector4 {
			bool a = (drag == axis);
			switch (axis) {
			case 0:
				return a ? Engine::Vector4{1, .6f, .6f, 1} : Engine::Vector4{1, .2f, .2f, 1};
			case 1:
				return a ? Engine::Vector4{.6f, 1, .6f, 1} : Engine::Vector4{.2f, 1, .2f, 1};
			case 2:
				return a ? Engine::Vector4{.6f, .6f, 1, 1} : Engine::Vector4{.2f, .2f, 1, 1};
			default:
				return {1, 1, 1, 1};
			}
		};
		auto cX = axCol(0, dAxis), cY = axCol(1, dAxis), cZ = axCol(2, dAxis);

		if (currentGizmoMode == GizmoMode::Translate) {
			drawLocalLine({0, 0, 0}, {al, 0, 0}, cX);
			drawLocalLine({al, 0, 0}, {al - ar, ar * .4f, 0}, cX);
			drawLocalLine({al, 0, 0}, {al - ar, -ar * .4f, 0}, cX);
			drawLocalLine({0, 0, 0}, {0, al, 0}, cY);
			drawLocalLine({0, al, 0}, {ar * .4f, al - ar, 0}, cY);
			drawLocalLine({0, al, 0}, {-ar * .4f, al - ar, 0}, cY);
			drawLocalLine({0, 0, 0}, {0, 0, al}, cZ);
			drawLocalLine({0, 0, al}, {0, ar * .4f, al - ar}, cZ);
			drawLocalLine({0, 0, al}, {0, -ar * .4f, al - ar}, cZ);
		} else if (currentGizmoMode == GizmoMode::Rotate) {
			const int seg = 32;
			const float rad = 1.5f;
			for (int i = 0; i < seg; ++i) {
				float a0 = (float)i / seg * DirectX::XM_2PI, a1 = (float)(i + 1) / seg * DirectX::XM_2PI;
				drawLocalLine({0, cosf(a0) * rad, sinf(a0) * rad}, {0, cosf(a1) * rad, sinf(a1) * rad}, cX);
				drawLocalLine({cosf(a0) * rad, 0, sinf(a0) * rad}, {cosf(a1) * rad, 0, sinf(a1) * rad}, cY);
				drawLocalLine({cosf(a0) * rad, sinf(a0) * rad, 0}, {cosf(a1) * rad, sinf(a1) * rad, 0}, cZ);
			}
		} else {
			float e = 0.15f;
			drawLocalLine({0, 0, 0}, {al, 0, 0}, cX);
			drawLocalLine({al - e, -e, 0}, {al + e, e, 0}, cX);
			drawLocalLine({al + e, -e, 0}, {al - e, e, 0}, cX);
			drawLocalLine({0, 0, 0}, {0, al, 0}, cY);
			drawLocalLine({-e, al - e, 0}, {e, al + e, 0}, cY);
			drawLocalLine({e, al - e, 0}, {-e, al + e, 0}, cY);
			drawLocalLine({0, 0, 0}, {0, 0, al}, cZ);
			drawLocalLine({0, -e, al - e}, {0, e, al + e}, cZ);
			drawLocalLine({0, e, al - e}, {0, -e, al + e}, cZ);
		}
	}
}

void GameScene::DrawEditorGizmos() {
	if (!renderer_)
		return;
	const float gridSize = 100.0f, step = 1.0f;
	for (float i = -gridSize; i <= gridSize; i += step) {
		if (std::fabs(i) < 0.01f)
			continue;
		bool isMajor = std::fmod(std::fabs(i), 10.0f) < 0.01f;
		float alpha = isMajor ? 0.35f : 0.15f;
		Engine::Vector4 gc = {0.6f, 0.6f, 0.6f, alpha};
		renderer_->DrawLine3D({-gridSize, 0.0f, i}, {gridSize, 0.0f, i}, gc, false);
		renderer_->DrawLine3D({i, 0.0f, -gridSize}, {i, 0.0f, gridSize}, gc, false);
	}
	renderer_->DrawLine3D({-gridSize, 0.0f, 0.0f}, {gridSize, 0.0f, 0.0f}, {0.8f, 0.2f, 0.2f, 0.7f}, false);
	renderer_->DrawLine3D({0.0f, 0.0f, -gridSize}, {0.0f, 0.0f, gridSize}, {0.2f, 0.2f, 0.8f, 0.7f}, false);
	renderer_->DrawLine3D({0, 0, 0}, {1.5f, 0, 0}, {1.f, 0.2f, 0.2f, 1.f}, true);
	renderer_->DrawLine3D({0, 0, 0}, {0, 1.5f, 0}, {0.2f, 1.f, 0.2f, 1.f}, true);
	renderer_->DrawLine3D({0, 0, 0}, {0, 0, 1.5f}, {0.2f, 0.2f, 1.f, 1.f}, true);
}

void GameScene::DrawEditor() {
#ifdef USE_IMGUI
	EditorUI::Show(renderer_, this);
#endif
}

void GameScene::DrawLightGizmos() {
	if (!renderer_)
		return;
	auto dlView = registry_.view<DirectionalLightComponent, TransformComponent>();
	dlView.each([&](auto entity, const DirectionalLightComponent& dl, const TransformComponent& tc) {
		if (!dl.enabled)
			return;
		Engine::Vector3 pos = {tc.translate.x, tc.translate.y, tc.translate.z};
		Engine::Matrix4x4 mat = tc.GetTransform().ToMatrix();
		Engine::Vector3 fwd = {mat.m[2][0], mat.m[2][1], mat.m[2][2]};
		bool isSelected = (selectedEntities_.find(entity) != selectedEntities_.end());
		float alpha = isSelected ? 1.0f : 0.4f;

		Engine::Vector4 col = {1.0f, 0.9f, 0.2f, alpha};
		renderer_->DrawLine3D(pos, {pos.x + fwd.x * 5.0f, pos.y + fwd.y * 5.0f, pos.z + fwd.z * 5.0f}, col, true);
		float s = 0.5f;
		renderer_->DrawLine3D({pos.x - s, pos.y, pos.z}, {pos.x + s, pos.y, pos.z}, col, true);
		renderer_->DrawLine3D({pos.x, pos.y - s, pos.z}, {pos.x, pos.y + s, pos.z}, col, true);
	});

	auto plView = registry_.view<PointLightComponent, TransformComponent>();
	plView.each([&](auto entity, const PointLightComponent& pl, const TransformComponent& tc) {
		if (!pl.enabled)
			return;
		Engine::Vector3 pos = {tc.translate.x, tc.translate.y, tc.translate.z};
		bool isSelected = (selectedEntities_.find(entity) != selectedEntities_.end());
		float alpha = isSelected ? 1.0f : 0.4f;

		Engine::Vector4 col = {0.2f, 0.9f, 0.2f, alpha};
		float s = 0.5f;
		renderer_->DrawLine3D({pos.x - s, pos.y, pos.z}, {pos.x + s, pos.y, pos.z}, col, true);
		renderer_->DrawLine3D({pos.x, pos.y - s, pos.z}, {pos.x, pos.y + s, pos.z}, col, true);
		renderer_->DrawLine3D({pos.x, pos.y, pos.z - s}, {pos.x, pos.y, pos.z + s}, col, true);
	});

	auto slView = registry_.view<SpotLightComponent, TransformComponent>();
	slView.each([&](auto entity, const SpotLightComponent& sl, const TransformComponent& tc) {
		if (!sl.enabled)
			return;
		Engine::Vector3 pos = {tc.translate.x, tc.translate.y, tc.translate.z};
		Engine::Matrix4x4 mat = tc.GetTransform().ToMatrix();
		Engine::Vector3 fwd = {mat.m[2][0], mat.m[2][1], mat.m[2][2]};
		bool isSelected = (selectedEntities_.find(entity) != selectedEntities_.end());
		float alpha = isSelected ? 1.0f : 0.4f;

		Engine::Vector4 col = {0.2f, 0.8f, 1.0f, alpha};
		renderer_->DrawLine3D(pos, {pos.x + fwd.x * 5.0f, pos.y + fwd.y * 5.0f, pos.z + fwd.z * 5.0f}, col, true);
		float s = 0.5f;
		renderer_->DrawLine3D({pos.x - s, pos.y, pos.z}, {pos.x + s, pos.y, pos.z}, col, true);
		renderer_->DrawLine3D({pos.x, pos.y - s, pos.z}, {pos.x, pos.y + s, pos.z}, col, true);
	});
}

void GameScene::SetIsPlaying(bool play) {
	if (isPlaying_ == play)
		return;

	if (play) {
		// プレイ開始時: スクリプトの現在の設定（インスペクターでの変更）をコンポーネントに確実に反映 (Flush)
		auto scView = registry_.view<ScriptComponent>();
		for (auto entity : scView) {
			auto& sc = scView.get<ScriptComponent>(entity);
			for (auto& entry : sc.scripts) {
				if (entry.instance) {
					std::string oldParam = entry.parameterData;
					entry.parameterData = entry.instance->SerializeParameters();
					if (entry.parameterData != oldParam) {
						char logBuf[2048];
						sprintf_s(logBuf, "[GameScene] Script synced: %s from %s to %s\n", entry.scriptPath.c_str(), oldParam.c_str(), entry.parameterData.c_str());
						OutputDebugStringA(logBuf);
					}
				}
			}
		}

		// 各Systemのリセット（スクリプトの再初期化、インスタンスのクリアなど）を先に実行
		for (auto& sys : systems_) {
			sys->Reset(registry_);
		}

		// リセット後のクリーンな状態をスナップショット保存（Stop時にこの状態に完全に戻すため）
		sceneSnapshot_ = EditorUI::SaveToMemory(this);
		{
			char logBuf[128];
			sprintf_s(logBuf, "[GameScene] Saved snapshot for PLAY mode (size: %zu)\n", sceneSnapshot_.size());
			OutputDebugStringA(logBuf);
		}

		// ★追加: SceneManagerにも保存（シーン遷移を跨いで保持するため）
		if (auto* sm = Engine::SceneManager::GetInstance()) {
			sm->SetGlobalPlaying(true);
			sm->SetGlobalSnapshot(sceneSnapshot_);
			sm->SetGlobalScenePath(EditorUI::currentScenePath);
		}

		isPlaying_ = true;
	} else {
		// プレイ停止時: Play ボタンを押した直前の状態 (`sceneSnapshot_`) に戻す
		// 選択状態のエンティティ名を一時保存
		std::vector<std::string> selectedNames;
		auto& reg = GetRegistry();
		for (auto entity : selectedEntities_) {
			if (reg.valid(entity) && reg.all_of<NameComponent>(entity)) {
				selectedNames.push_back(reg.get<NameComponent>(entity).name);
			}
		}

		isPlaying_ = false;

		// ★修正: SceneManagerのグローバルスナップショットを優先（シーン遷移を跨いだ復元のため）
		std::string restoreSnapshot = sceneSnapshot_;
		std::string restorePath;
		if (auto* sm = Engine::SceneManager::GetInstance()) {
			if (!sm->GetGlobalSnapshot().empty()) {
				restoreSnapshot = sm->GetGlobalSnapshot();
				restorePath = sm->GetGlobalScenePath();
			}
			sm->ClearGlobalPlayData();
		}

		if (!restoreSnapshot.empty()) {
			OutputDebugStringA(("[GameScene] Restoring from snapshot (size: " + std::to_string(restoreSnapshot.size()) + ")...\n").c_str());
			EditorUI::LoadFromMemory(this, restoreSnapshot);
			if (!restorePath.empty()) {
				EditorUI::currentScenePath = restorePath;
				std::string p = restorePath;
				if (p.find("title.json") != std::string::npos) sceneName_ = "Title";
				else if (p.find("select.json") != std::string::npos) sceneName_ = "Select";
				else if (p.find("result.json") != std::string::npos) sceneName_ = "Result";
				else sceneName_ = "Game";
			}

			// 保存しておいた名前を元に選択状態を復元
			selectedEntities_.clear();
			selectedEntity_ = entt::null;
			auto view = reg.view<NameComponent>();
			for (const auto& name : selectedNames) {
				for (auto entity : view) {
					if (view.get<NameComponent>(entity).name == name) {
						selectedEntities_.insert(entity);
						if (selectedEntity_ == entt::null)
							selectedEntity_ = entity;
						break;
					}
				}
			}
			if (!selectedNames.empty()) {
				OutputDebugStringA(("[GameScene] Restored selection for " + std::to_string(selectedEntities_.size()) + " entities.\n").c_str());
			}
		} else {
			OutputDebugStringA("[GameScene] ERROR: No snapshot available on STOP! Falling back to initial state.\n");
			if (!initialSceneSnapshot_.empty()) {
				EditorUI::LoadFromMemory(this, initialSceneSnapshot_);
			}
		}
		sceneSnapshot_ = "";

		// ★追加: 川のメッシュなど、動的メッシュの再生成
		registry_.view<RiverComponent, TransformComponent>().each([&](RiverComponent& rv, TransformComponent& tc) {
			if (rv.enabled && rv.meshHandle == 0) {
				RiverSystem::BuildRiverMesh(rv, renderer_, registry_, tc.translate);
			}
		});

		// ペンディングデータのクリア
		std::lock_guard<std::recursive_mutex> lock(spawnMutex_);
		pendingDestroys_.clear();
		pendingSpawns_.clear();

		// タグキャッシュの完全リセットと再構築（大量のエンティティ削除・追加によるフリーズ防止）
		pendingTagSync_.clear();
		pendingTagRemoved_.clear();
		tagCache_.clear();
		auto tagView = registry_.view<TagComponent>();
		for (auto entity : tagView) {
			const auto tag = tagView.get<TagComponent>(entity).tag;
			tagCache_[tag].push_back(entity);
		}
	}
}

// =====================================================
// ★ 高速タグアクセス実装
// =====================================================

const std::vector<entt::entity>& GameScene::GetEntitiesByTag(TagType tag) {
	static const std::vector<entt::entity> emptyResult;
	auto it = tagCache_.find(tag);
	if (it != tagCache_.end()) {
		return it->second;
	}
	return emptyResult;
}

void GameScene::SetTag(entt::entity entity, TagType tag) {
	if (!registry_.valid(entity)) {
		return;
	}
	auto& tc = registry_.get_or_emplace<TagComponent>(entity);
	tc.tag = tag;
	// 直接 SyncTag せず、遅延更新リストに追加
	pendingTagSync_.push_back(entity);
}

void GameScene::OnTagAdded(entt::registry& /*registry*/, entt::entity entity) {
	// 即座に同期せず、次フレーム等の適切なタイミングで同期
	pendingTagSync_.push_back(entity);
}

void GameScene::OnTagRemoved(entt::registry& /*registry*/, entt::entity entity) {
	// 即座に削除せず、遅延リストに追加して次フレーム開始時に削除を行う
	// これにより、イテレーション中のコンテナ変更による例外を防止する
	pendingTagRemoved_.push_back(entity);
}

void GameScene::SyncTag(entt::entity entity) {
	if (!registry_.valid(entity) || !registry_.all_of<TagComponent>(entity)) {
		return;
	}

	// 新しいキャッシュに追加（削除は Update の開始時に一括して行われる前提）
	const TagType tag = registry_.get<TagComponent>(entity).tag;
	
	// 重複チェックを一件ずつ行うと遅いため、基本的には Update 側の全削除を信頼する
	tagCache_[tag].push_back(entity);
}

const std::vector<entt::entity>& GameScene::GetEntitiesByTag(const std::string& tag) {
	return GetEntitiesByTag(StringToTag(tag));
}

void GameScene::SetTag(entt::entity entity, const std::string& tagStr) {
	SetTag(entity, StringToTag(tagStr));
}

// =====================================================
// ★追加: ポーズメニュー
// =====================================================

entt::entity GameScene::CreatePauseButton(const std::string& text, float yPos, entt::entity parent) {
	auto entity = pauseRegistry_.create();

	auto& rect = pauseRegistry_.emplace<RectTransformComponent>(entity);
	rect.pos = {0.0f, yPos};
	rect.size = {300.0f, 60.0f};
	rect.anchor = {0.0f, 0.0f};
	rect.pivot = {0.0f, 0.5f};
	rect.enabled = true;

	if (parent != entt::null) {
		pauseRegistry_.emplace<HierarchyComponent>(entity, parent);
	}

	auto& btn = pauseRegistry_.emplace<UIButtonComponent>(entity);
	btn.normalColor = {0.15f, 0.15f, 0.2f, 0.9f};
	btn.hoverColor = {0.3f, 0.3f, 0.45f, 1.0f};
	btn.pressedColor = {0.1f, 0.1f, 0.15f, 1.0f};

	auto& txt = pauseRegistry_.emplace<UITextComponent>(entity);
	txt.text = text;
	txt.fontSize = 32.0f;
	txt.color = {1.0f, 1.0f, 1.0f, 1.0f};

	pauseRegistry_.emplace<UIImageComponent>(entity).color = {1.0f, 1.0f, 1.0f, 1.0f};

	return entity;
}

void GameScene::CreatePauseMenu() {
	auto parent = pauseRegistry_.create();
	auto& pRect = pauseRegistry_.emplace<RectTransformComponent>(parent);
	// 画面中央に配置
	pRect.pos = {(float)Engine::WindowDX::kW / 2.0f - 150.0f, (float)Engine::WindowDX::kH / 2.0f - 100.0f};
	pRect.size = {0, 0};
	pRect.anchor = {0.0f, 0.0f};
	pauseMainEntities_.push_back(parent);

	// タイトルテキスト
	auto titleText = pauseRegistry_.create();
	auto& titleRect = pauseRegistry_.emplace<RectTransformComponent>(titleText);
	titleRect.pos = {30.0f, -80.0f};
	pauseRegistry_.emplace<HierarchyComponent>(titleText, parent);
	auto& txt = pauseRegistry_.emplace<UITextComponent>(titleText);
	txt.text = "PAUSE";
	txt.fontSize = 64.0f;
	txt.color = {1.0f, 0.9f, 0.3f, 1.0f};
	pauseMainEntities_.push_back(titleText);

	pauseBtnResume_ = CreatePauseButton(reinterpret_cast<const char*>(u8"\u30B2\u30FC\u30E0\u306B\u623B\u308B"), 0.0f, parent);
	pauseBtnSettings_ = CreatePauseButton(reinterpret_cast<const char*>(u8"\u8A2D\u5B9A"), 80.0f, parent);
	pauseBtnTitle_ = CreatePauseButton(reinterpret_cast<const char*>(u8"\u30BF\u30A4\u30C8\u30EB\u306B\u623B\u308B"), 160.0f, parent);

	pauseMainEntities_.push_back(pauseBtnResume_);
	pauseMainEntities_.push_back(pauseBtnSettings_);
	pauseMainEntities_.push_back(pauseBtnTitle_);
}

void GameScene::CreatePauseSettingsMenu() {
	auto parent = pauseRegistry_.create();
	auto& pRect = pauseRegistry_.emplace<RectTransformComponent>(parent);
	pRect.pos = {(float)Engine::WindowDX::kW / 2.0f - 150.0f, (float)Engine::WindowDX::kH / 2.0f - 100.0f};
	pRect.size = {0, 0};
	pRect.anchor = {0.0f, 0.0f};
	pRect.enabled = false;
	pauseSettingsEntities_.push_back(parent);

	// タイトルテキスト
	auto titleText = pauseRegistry_.create();
	auto& titleRect = pauseRegistry_.emplace<RectTransformComponent>(titleText);
	titleRect.pos = {30.0f, -80.0f};
	titleRect.enabled = false;
	pauseRegistry_.emplace<HierarchyComponent>(titleText, parent);
	auto& txt = pauseRegistry_.emplace<UITextComponent>(titleText);
	txt.text = "Settings";
	txt.fontSize = 64.0f;
	txt.color = {1.0f, 0.9f, 0.3f, 1.0f};
	pauseSettingsEntities_.push_back(titleText);

	// フルスクリーン
	pauseBtnFullscreen_ = CreatePauseButton("Fullscreen: OFF", 0.0f, parent);
	pauseRegistry_.get<RectTransformComponent>(pauseBtnFullscreen_).enabled = false;
	pauseTextFullscreen_ = pauseBtnFullscreen_;

	// BGM 音量
	auto bgmLabel = pauseRegistry_.create();
	auto& bgmRect = pauseRegistry_.emplace<RectTransformComponent>(bgmLabel);
	bgmRect.pos = {0.0f, 80.0f};
	bgmRect.enabled = false;
	pauseRegistry_.emplace<HierarchyComponent>(bgmLabel, parent);
	auto& bgmTxt = pauseRegistry_.emplace<UITextComponent>(bgmLabel);
	bgmTxt.text = "BGM Volume";
	bgmTxt.fontSize = 32.0f;
	pauseTextBGM_ = bgmLabel;

	pauseBtnBGMMinus_ = CreatePauseButton("-", 80.0f, parent);
	pauseRegistry_.get<RectTransformComponent>(pauseBtnBGMMinus_).size = {60.0f, 60.0f};
	pauseRegistry_.get<RectTransformComponent>(pauseBtnBGMMinus_).pos = {310.0f, 80.0f};
	pauseRegistry_.get<RectTransformComponent>(pauseBtnBGMMinus_).enabled = false;

	pauseBtnBGMPlus_ = CreatePauseButton("+", 80.0f, parent);
	pauseRegistry_.get<RectTransformComponent>(pauseBtnBGMPlus_).size = {60.0f, 60.0f};
	pauseRegistry_.get<RectTransformComponent>(pauseBtnBGMPlus_).pos = {380.0f, 80.0f};
	pauseRegistry_.get<RectTransformComponent>(pauseBtnBGMPlus_).enabled = false;

	// SE 音量
	auto seLabel = pauseRegistry_.create();
	auto& seRect = pauseRegistry_.emplace<RectTransformComponent>(seLabel);
	seRect.pos = {0.0f, 160.0f};
	seRect.enabled = false;
	pauseRegistry_.emplace<HierarchyComponent>(seLabel, parent);
	auto& seTxt = pauseRegistry_.emplace<UITextComponent>(seLabel);
	seTxt.text = "SE Volume";
	seTxt.fontSize = 32.0f;
	pauseTextSE_ = seLabel;

	pauseBtnSEMinus_ = CreatePauseButton("-", 160.0f, parent);
	pauseRegistry_.get<RectTransformComponent>(pauseBtnSEMinus_).size = {60.0f, 60.0f};
	pauseRegistry_.get<RectTransformComponent>(pauseBtnSEMinus_).pos = {310.0f, 160.0f};
	pauseRegistry_.get<RectTransformComponent>(pauseBtnSEMinus_).enabled = false;

	pauseBtnSEPlus_ = CreatePauseButton("+", 160.0f, parent);
	pauseRegistry_.get<RectTransformComponent>(pauseBtnSEPlus_).size = {60.0f, 60.0f};
	pauseRegistry_.get<RectTransformComponent>(pauseBtnSEPlus_).pos = {380.0f, 160.0f};
	pauseRegistry_.get<RectTransformComponent>(pauseBtnSEPlus_).enabled = false;

	pauseBtnBack_ = CreatePauseButton(reinterpret_cast<const char*>(u8"\u623B\u308B"), 260.0f, parent);
	pauseRegistry_.get<RectTransformComponent>(pauseBtnBack_).enabled = false;

	pauseSettingsEntities_.push_back(bgmLabel);
	pauseSettingsEntities_.push_back(pauseBtnFullscreen_);
	pauseSettingsEntities_.push_back(pauseBtnBGMMinus_);
	pauseSettingsEntities_.push_back(pauseBtnBGMPlus_);
	pauseSettingsEntities_.push_back(seLabel);
	pauseSettingsEntities_.push_back(pauseBtnSEMinus_);
	pauseSettingsEntities_.push_back(pauseBtnSEPlus_);
	pauseSettingsEntities_.push_back(pauseBtnBack_);
}

void GameScene::UpdatePauseMenu() {
	// ポーズメニューのUIシステム更新（ボタンhover状態等）
	pauseCtx_.dt = 1.0f / 60.0f;
	pauseCtx_.input = Engine::Input::GetInstance();
	pauseCtx_.viewportOffset = ctx_.viewportOffset;
	pauseCtx_.viewportSize = ctx_.viewportSize;
	pauseUISystem_->Draw(pauseRegistry_, pauseCtx_);

	auto* input = Engine::Input::GetInstance();
	bool isClicked = input->IsMouseTrigger(0);

	if (pauseMenuState_ == PauseMenuState::Main) {
		if (isClicked) {
			if (pauseRegistry_.get<UIButtonComponent>(pauseBtnResume_).isHovered) {
				// ゲームに戻る
				isPaused_ = false;
				for (auto e : pauseMainEntities_) pauseRegistry_.get<RectTransformComponent>(e).enabled = false;
				for (auto e : pauseSettingsEntities_) pauseRegistry_.get<RectTransformComponent>(e).enabled = false;
				
				// ★追加: ボタンから解除した時も確実にカーソルを非表示にする
				while (ShowCursor(FALSE) >= 0);
			} else if (pauseRegistry_.get<UIButtonComponent>(pauseBtnSettings_).isHovered) {
				// 設定画面へ
				pauseMenuState_ = PauseMenuState::Settings;
				for (auto e : pauseMainEntities_) pauseRegistry_.get<RectTransformComponent>(e).enabled = false;
				for (auto e : pauseSettingsEntities_) pauseRegistry_.get<RectTransformComponent>(e).enabled = true;
			} else if (pauseRegistry_.get<UIButtonComponent>(pauseBtnTitle_).isHovered) {
				// タイトルに戻る
				isPaused_ = false;
				isPlaying_ = false;
				Engine::SceneManager::GetInstance()->RequestChange("Title");
			}
		}
	} else if (pauseMenuState_ == PauseMenuState::Settings) {
		auto* audio = Engine::Audio::GetInstance();

		// フルスクリーンテキスト更新
		if (dx_) {
			std::string fsText = dx_->IsFullscreen() ? "Fullscreen: ON" : "Fullscreen: OFF";
			pauseRegistry_.get<UITextComponent>(pauseTextFullscreen_).text = fsText;
		}

		// 音量テキスト更新
		if (audio) {
			int bgmVol = static_cast<int>(audio->GetMasterBGMVolume() * 100);
			pauseRegistry_.get<UITextComponent>(pauseTextBGM_).text = "BGM Volume: " + std::to_string(bgmVol) + "%";
			int seVol = static_cast<int>(audio->GetMasterSEVolume() * 100);
			pauseRegistry_.get<UITextComponent>(pauseTextSE_).text = "SE Volume: " + std::to_string(seVol) + "%";
		}

		if (isClicked) {
			if (pauseRegistry_.get<UIButtonComponent>(pauseBtnBack_).isHovered) {
				// メインメニューに戻る
				pauseMenuState_ = PauseMenuState::Main;
				for (auto e : pauseMainEntities_) pauseRegistry_.get<RectTransformComponent>(e).enabled = true;
				for (auto e : pauseSettingsEntities_) pauseRegistry_.get<RectTransformComponent>(e).enabled = false;
			} else if (pauseRegistry_.get<UIButtonComponent>(pauseBtnFullscreen_).isHovered) {
				if (dx_) dx_->ToggleFullscreen();
			} else if (pauseRegistry_.get<UIButtonComponent>(pauseBtnBGMMinus_).isHovered) {
				if (audio) audio->SetMasterBGMVolume(audio->GetMasterBGMVolume() - 0.1f);
			} else if (pauseRegistry_.get<UIButtonComponent>(pauseBtnBGMPlus_).isHovered) {
				if (audio) audio->SetMasterBGMVolume(audio->GetMasterBGMVolume() + 0.1f);
			} else if (pauseRegistry_.get<UIButtonComponent>(pauseBtnSEMinus_).isHovered) {
				if (audio) audio->SetMasterSEVolume(audio->GetMasterSEVolume() - 0.1f);
			} else if (pauseRegistry_.get<UIButtonComponent>(pauseBtnSEPlus_).isHovered) {
				if (audio) audio->SetMasterSEVolume(audio->GetMasterSEVolume() + 0.1f);
			}
		}
	}
}

void GameScene::OnScriptDestroyed(entt::registry& /*reg*/, entt::entity entity) {
	ScriptEngine::GetInstance()->ExecuteDestroy(entity, this);
}

float GameScene::GetVar(entt::entity entity, const std::string& key, float defaultVal) {
	if (registry_.all_of<VariableComponent>(entity)) {
		return registry_.get<VariableComponent>(entity).GetValue(key, defaultVal);
	}
	return defaultVal;
}

void GameScene::SetVar(entt::entity entity, const std::string& key, float value) {
	if (!registry_.all_of<VariableComponent>(entity)) {
		registry_.emplace<VariableComponent>(entity);
	}
	registry_.get<VariableComponent>(entity).SetValue(key, value);
}

std::string GameScene::GetVarString(entt::entity entity, const std::string& key, const std::string& defaultVal) {
	if (registry_.all_of<VariableComponent>(entity)) {
		return registry_.get<VariableComponent>(entity).GetString(key, defaultVal);
	}
	return defaultVal;
}

void GameScene::SetVarString(entt::entity entity, const std::string& key, const std::string& value) {
	if (!registry_.all_of<VariableComponent>(entity)) {
		registry_.emplace<VariableComponent>(entity);
	}
	registry_.get<VariableComponent>(entity).SetString(key, value);
}

} // namespace Game
