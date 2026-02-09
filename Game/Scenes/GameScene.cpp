// Game/GameScene.cpp
#define NOMINMAX

#include "GameScene.h"
#include "imgui.h"

// TitleSceneの静的変数を使うためにインクルード
#include "TitleScene.h"

#include "../Game/gimmick/BoostRingGimmick.h"
#include "../Game/gimmick/GimmickBase.h"
#include "../ObjectTypes.h"
#include <DirectXMath.h>
#include <algorithm>
#include <cmath>
#include <d3d12.h>
#include <unordered_map>
#include <vector>

// Modelクラスのメソッドを使うために必要
#include "../../Engine/Model.h"

namespace Game {

using namespace DirectX;

// 演算子定義関数
static Engine::Vector3 VAdd(const Engine::Vector3& a, const Engine::Vector3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
static Engine::Vector3 VSub(const Engine::Vector3& a, const Engine::Vector3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
static Engine::Vector3 VScale(const Engine::Vector3& a, float s) { return {a.x * s, a.y * s, a.z * s}; }

// 角度の正規化
static float NormalizeAngle(float a) {
	const float pi = 3.14159265358979f;
	while (a <= -pi)
		a += 2.0f * pi;
	while (a > pi)
		a -= 2.0f * pi;
	return a;
}

// ---- Respawn System ----
static Engine::Vector3 gRespawnPos = {0.0f, 2.0f, 0.0f};
void SetRespawnPos(const Engine::Vector3& pos) { gRespawnPos = pos; }

static std::unordered_map<int, Engine::Vector3> gRespawnTable;
static int gCurrentRespawnId = 0;
static Engine::Vector3 gRespawnPosFallback = {0.0f, 2.0f, 0.0f};

static const Engine::Vector3& GetRespawnPos() {
	auto it = gRespawnTable.find(gCurrentRespawnId);
	if (it != gRespawnTable.end())
		return it->second;
	return gRespawnPosFallback;
}

void RegisterRespawnPoint(int id, const Engine::Vector3& pos) { gRespawnTable[id] = pos; }
void ActivateRespawnPoint(int id, const Engine::Vector3& pos) {
	gRespawnTable[id] = pos;
	if (id >= gCurrentRespawnId)
		gCurrentRespawnId = id;
}

static bool gGoalReached = false;
void NotifyGoalReached() { gGoalReached = true; }
static bool ConsumeGoalReached() {
	if (!gGoalReached)
		return false;
	gGoalReached = false;
	return true;
}

// -----------------------------------------------------------
//  Math Helpers
// -----------------------------------------------------------
static bool IntersectRayAABB(const XMVECTOR& o, const XMVECTOR& d, const Engine::AABB& box) {
	float tmin = 0.0f;
	float tmax = 100000.0f;
	{
		float invD = 1.0f / XMVectorGetX(d);
		float t1 = (box.min.x - XMVectorGetX(o)) * invD;
		float t2 = (box.max.x - XMVectorGetX(o)) * invD;
		tmin = (std::max)(tmin, (std::min)(t1, t2));
		tmax = (std::min)(tmax, (std::max)(t1, t2));
	}
	{
		float invD = 1.0f / XMVectorGetY(d);
		float t1 = (box.min.y - XMVectorGetY(o)) * invD;
		float t2 = (box.max.y - XMVectorGetY(o)) * invD;
		tmin = (std::max)(tmin, (std::min)(t1, t2));
		tmax = (std::min)(tmax, (std::max)(t1, t2));
	}
	{
		float invD = 1.0f / XMVectorGetZ(d);
		float t1 = (box.min.z - XMVectorGetZ(o)) * invD;
		float t2 = (box.max.z - XMVectorGetZ(o)) * invD;
		tmin = (std::max)(tmin, (std::min)(t1, t2));
		tmax = (std::min)(tmax, (std::max)(t1, t2));
	}
	return tmax >= tmin;
}

static float GetClosestPointOnAxis(const XMVECTOR& rayOrigin, const XMVECTOR& rayDir, const XMVECTOR& axisOrigin, const XMVECTOR& axisDir) {
	XMVECTOR w0 = XMVectorSubtract(rayOrigin, axisOrigin);
	float a = XMVectorGetX(XMVector3Dot(rayDir, rayDir));
	float b = XMVectorGetX(XMVector3Dot(rayDir, axisDir));
	float c = XMVectorGetX(XMVector3Dot(axisDir, axisDir));
	float d = XMVectorGetX(XMVector3Dot(rayDir, w0));
	float e = XMVectorGetX(XMVector3Dot(axisDir, w0));
	float denom = a * c - b * b;
	if (denom < 1e-5f)
		return 0.0f;
	return (a * e - b * d) / denom;
}

static void GetRayFromNDC(const Engine::Camera& cam, float ndcX, float ndcY, Engine::Vector3& origin, Engine::Vector3& dir) {
	XMMATRIX P = cam.Proj();
	XMMATRIX V = cam.View();
	XMMATRIX InvVP = XMMatrixInverse(nullptr, V * P);
	XMVECTOR n = XMVectorSet(ndcX, ndcY, 0, 1);
	XMVECTOR f = XMVectorSet(ndcX, ndcY, 1, 1);
	XMVECTOR vn = XMVector3TransformCoord(n, InvVP);
	XMVECTOR vf = XMVector3TransformCoord(f, InvVP);
	XMVECTOR vDir = XMVector3Normalize(XMVectorSubtract(vf, vn));
	origin = {XMVectorGetX(vn), XMVectorGetY(vn), XMVectorGetZ(vn)};
	dir = {XMVectorGetX(vDir), XMVectorGetY(vDir), XMVectorGetZ(vDir)};
}

static void DrawWireframeBox(Engine::Renderer* renderer, uint32_t mesh, uint32_t tex, const Engine::GameObject* obj, const Engine::Vector4& color) {
	if (!obj)
		return;
	Engine::Vector3 minL = obj->localAABBMin;
	Engine::Vector3 maxL = obj->localAABBMax;
	float margin = 0.02f;
	minL.x -= margin;
	minL.y -= margin;
	minL.z -= margin;
	maxL.x += margin;
	maxL.y += margin;
	maxL.z += margin;

	Engine::Vector3 worldSize = {(maxL.x - minL.x) * obj->transform.scale.x, (maxL.y - minL.y) * obj->transform.scale.y, (maxL.z - minL.z) * obj->transform.scale.z};
	XMMATRIX matRot = XMMatrixRotationRollPitchYaw(obj->transform.rotate.x, obj->transform.rotate.y, obj->transform.rotate.z);
	XMVECTOR vCenterL = XMVectorSet((minL.x + maxL.x) * 0.5f, (minL.y + maxL.y) * 0.5f, (minL.z + maxL.z) * 0.5f, 0.0f);
	XMVECTOR vOffset = XMVector3TransformNormal(XMVectorMultiply(vCenterL, XMLoadFloat3((XMFLOAT3*)&obj->transform.scale)), matRot);
	XMVECTOR vBoxCenter = XMVectorAdd(XMLoadFloat3((XMFLOAT3*)&obj->transform.translate), vOffset);

	float thk = 0.03f;
	float hx = worldSize.x * 0.5f;
	float hy = worldSize.y * 0.5f;
	float hz = worldSize.z * 0.5f;
	float lenScaleFix = 0.5f;

	struct Edge {
		Engine::Vector3 offset;
		Engine::Vector3 scale;
	};
	std::vector<Edge> edges = {
	    {{0, -hy, -hz}, {worldSize.x * lenScaleFix, thk, thk}},
        {{0, -hy, hz},  {worldSize.x * lenScaleFix, thk, thk}},
        {{0, hy, -hz},  {worldSize.x * lenScaleFix, thk, thk}},
	    {{0, hy, hz},   {worldSize.x * lenScaleFix, thk, thk}},
        {{-hx, 0, -hz}, {thk, worldSize.y * lenScaleFix, thk}},
        {{hx, 0, -hz},  {thk, worldSize.y * lenScaleFix, thk}},
	    {{-hx, 0, hz},  {thk, worldSize.y * lenScaleFix, thk}},
        {{hx, 0, hz},   {thk, worldSize.y * lenScaleFix, thk}},
        {{-hx, -hy, 0}, {thk, thk, worldSize.z * lenScaleFix}},
	    {{hx, -hy, 0},  {thk, thk, worldSize.z * lenScaleFix}},
        {{-hx, hy, 0},  {thk, thk, worldSize.z * lenScaleFix}},
        {{hx, hy, 0},   {thk, thk, worldSize.z * lenScaleFix}}
    };

	for (const auto& edge : edges) {
		Engine::Transform t;
		t.rotate = obj->transform.rotate;
		t.scale = edge.scale;
		XMStoreFloat3((XMFLOAT3*)&t.translate, XMVectorAdd(vBoxCenter, XMVector3TransformNormal(XMLoadFloat3((XMFLOAT3*)&edge.offset), matRot)));
		renderer->DrawMesh(mesh, tex, t, color);
	}
}

// -----------------------------------------------------------
//  GameScene Implementation
// -----------------------------------------------------------

bool GameScene::WasPressed_(int vk) {
	SHORT now = GetAsyncKeyState(vk);
	bool pressed = ((now & 0x8000) != 0) && ((prevKey_[vk] & 0x8000) == 0);
	prevKey_[vk] = now;
	return pressed;
}

// ボタンが押された瞬間を判定
bool GameScene::WasButtonPressed_(WORD button) {
	bool now = (currentPadState_.Gamepad.wButtons & button) != 0;
	bool prev = (prevPadState_.Gamepad.wButtons & button) != 0;
	return now && !prev;
}

void GameScene::Initialize(Engine::WindowDX* dx) {
	dx_ = dx;
	renderer_ = Engine::Renderer::GetInstance();
	if (!renderer_)
		return;

	world_.Initialize(renderer_);
	camera_.Initialize();
	camera_.SetProjection(XMConvertToRadians(60.0f), (float)Engine::WindowDX::kW / (float)Engine::WindowDX::kH, 0.1f, 500.0f);

	ballMesh_ = renderer_->LoadObjMesh("Resources/player_ball/ball.obj");
	uvTex_ = renderer_->LoadTexture2D("Resources/ball.png", true);
	ballTex_ = uvTex_;
	whiteTex_ = renderer_->LoadTexture2D("Resources/white1x1.png");
	azarasiTex_ = renderer_->LoadTexture2D("Resources/basecolor.jpg");

	uiPauseTex_ = renderer_->LoadTexture2D("Resources/UI/po-zu.png");

	// ★追加: ポーズメニュー画像のロード
	uiPauseGameTex_ = renderer_->LoadTexture2D("Resources/UI/ge-munimodoru.png");
	uiPauseSelectTex_ = renderer_->LoadTexture2D("Resources/UI/serekutonimodoru.png");
	uiPauseTitleTex_ = renderer_->LoadTexture2D("Resources/UI/taitorunimodoru.png");

	// =========================================================
	// ★追加: 音声ファイルのロードとBGM再生
	// =========================================================
	auto* audio = Engine::Audio::GetInstance();
	// 音声ファイルをロード
	bgmHandle_ = audio->Load("Resources/Sound/BGM2.mp3");
	seMoveHandle_ = audio->Load("Resources/Sound/idou.mp3");
	seDecideHandle_ = audio->Load("Resources/Sound/kettei.mp3");
	seSelectHandle_ = audio->Load("Resources/Sound/serekutosenntaku.mp3");

	// ゲームBGM再生 (ループ=true) ハンドルを保存
	bgmVoiceHandle_ = audio->Play(bgmHandle_, true, 0.5f);

	// 移動音変数の初期化
	isMovingSePlaying_ = false;
	movingVoiceId_ = 0;
	// =========================================================

	clearMojiTex_ = renderer_->LoadTexture2D("Resources/UI/moji.png");

	renderer_->CreateShaderPipeline("Heathaze", L"Resources/shaders/HeathazeVS.hlsl", L"Resources/shaders/HeathazePS.hlsl");
	renderer_->CreateShaderPipeline("Toon", L"Resources/shaders/ToonVS.hlsl", L"Resources/shaders/ToonPS.hlsl");
	renderer_->CreateShaderPipeline("JumpPadEnergy", L"Resources/shaders/JumpPadEnergyVS.hlsl", L"Resources/shaders/JumpPadEnergyPS.hlsl");
	renderer_->CreateShaderPipelineTransparent("EnergyCylinder", L"Resources/shaders/EnergyCylinderVS.hlsl", L"Resources/shaders/EnergyCylinderPS.hlsl", true);
	renderer_->CreateShaderPipelineTransparent("TimedBlock", L"Resources/shaders/TimedBlockVS.hlsl", L"Resources/shaders/TimedBlockPS.hlsl", true);
	renderer_->CreateShaderPipelineTransparent("MV_Path", L"Resources/shaders/PathMV_VS.hlsl", L"Resources/shaders/PathMV_PS.hlsl", true);
	renderer_->CreateShaderPipelineTransparent("Rock_Mid", L"Resources/shaders/RockCommonVS.hlsl", L"Resources/shaders/Rock_MidPS.hlsl", true);
	renderer_->CreateShaderPipelineTransparent("OnOffShader", L"Resources/shaders/OnOffObject3d.VS.hlsl", L"Resources/shaders/OnOffObject3d.PS.hlsl", true);

	renderer_->CreateShaderPipeline("StageLow", L"Resources/shaders/StageCommonVS.hlsl", L"Resources/shaders/StageLowPS.hlsl");
	renderer_->CreateShaderPipeline("StageMid", L"Resources/shaders/StageCommonVS.hlsl", L"Resources/shaders/StageMidPS.hlsl");
	renderer_->CreateShaderPipeline("StageHigh", L"Resources/shaders/StageCommonVS.hlsl", L"Resources/shaders/StageHighPS.hlsl");
	renderer_->CreateShaderPipeline("Stagewall", L"Resources/shaders/StageCommonVS.hlsl", L"Resources/shaders/WallPS.hlsl");
	renderer_->CreateShaderPipeline("Lift", L"Resources/shaders/LiftVS.hlsl", L"Resources/shaders/LiftPS.hlsl");

	player_.Initialize(renderer_, ballMesh_, ballTex_);

	renderer_->SetDirectionalLight({0.5f, -1.0f, 0.5f}, {1.0f, 0.95f, 0.8f});
	renderer_->SetAmbientColor({0.4f, 0.4f, 0.5f});

	Engine::Vector3 start = {0.0f, 2.0f, 0.0f};
	player_.Reset(start);
	SetRespawnPos(start);

	if (renderer_)
		renderer_->SetPostProcessEnabled(true);

#ifdef _DEBUG
	isEditorMode_ = true;
#else
	isEditorMode_ = false;
#endif

	for (int i = 0; i < 256; ++i)
		prevKey_[i] = GetAsyncKeyState(i);

	// パッドの状態を初期化
	ZeroMemory(&prevPadState_, sizeof(XINPUT_STATE));
	XInputGetState(0, &prevPadState_);
	currentPadState_ = prevPadState_;

	end_ = false;
	next_.clear();

	walkModelHandle_ = renderer_->LoadObjMesh("Resources/azarasi_walk.gltf");
	Engine::Model* model = renderer_->GetModel(walkModelHandle_);
	if (model) {
		bonePalette_.resize(model->GetData().bones.size(), Engine::Matrix4x4::Identity());
	}

	charPos_ = {3.0f, 0.0f, 0.0f};
	charRotateY_ = 3.14159f;

	state_ = SceneState::Play;
	clearAnimTimer_ = 0.0f;

	confettiParticles_.Initialize(*renderer_, *dx_, 3000, "Resources/plane.obj", "Resources/white1x1.png", true, false);
	hitParticles_.Initialize(*renderer_, *dx_, 500, "Resources/plane.obj", "Resources/white1x1.png", true, false);
	player_.SetParticleSystem(&hitParticles_);
	dustParticles_.Initialize(*renderer_, *dx_, 2000, "Resources/plane.obj", "Resources/white1x1.png", true, true);
	respawnParticles_.Initialize(*renderer_, *dx_, 100, "Resources/plane.obj", "Resources/white1x1.png", true, true);

	isPlayerPositioned_ = false;

	isPaused_ = false;
	pauseCursor_ = 0;

	// ★遷移待機変数の初期化
	isWaitingForRelease_ = false;
	pendingNext_ = "";
	pendingSelectStart_ = false;

	Update();
}

void GameScene::Update() {
	// フレーム冒頭でパッド状態を取得
	XInputGetState(0, &currentPadState_);

#ifdef _DEBUG
	if (WasPressed_(VK_TAB)) {
		isEditorMode_ = !isEditorMode_;
		ImGui::SetWindowFocus(nullptr);
	}
#endif

	// ★遷移待機中（ボタン離し待ち）の処理
	// ボタンが離されるまで遷移を保留する
	if (isWaitingForRelease_) {
		bool released = true;

		// キーボードのSpace/Enterが押されていないか
		if ((GetKeyState(VK_SPACE) & 0x8000) || (GetKeyState(VK_RETURN) & 0x8000)) {
			released = false;
		}
		// Aボタンが押されていないか
		if (currentPadState_.Gamepad.wButtons & XINPUT_GAMEPAD_A) {
			released = false;
		}

		// 完全に離されたら遷移実行
		if (released) {
			// 遷移前にポストエフェクトを確実に切る
			renderer_->SetPostEffect("");

			// タイトルへ渡すフラグを設定
			TitleScene::IsSelectStart = pendingSelectStart_;

			end_ = true;
			next_ = pendingNext_;
		}

		// 待機中もパッドの「前フレーム」を更新し続ける
		prevPadState_ = currentPadState_;
		return;
	}

	// ポーズ切り替え (ESC or Pad-Y)
	if (WasPressed_(VK_ESCAPE) || WasButtonPressed_(XINPUT_GAMEPAD_Y)) {
		isPaused_ = !isPaused_;
	}

	// ポーズ中の処理
	if (isPaused_) {
		bool movedCursor = false;

		// カーソル移動
		if (WasPressed_('W') || WasPressed_(VK_UP) || WasButtonPressed_(XINPUT_GAMEPAD_DPAD_UP)) {
			pauseCursor_ = (pauseCursor_ + 2) % 3;
			movedCursor = true;
		}
		if (WasPressed_('S') || WasPressed_(VK_DOWN) || WasButtonPressed_(XINPUT_GAMEPAD_DPAD_DOWN)) {
			pauseCursor_ = (pauseCursor_ + 1) % 3;
			movedCursor = true;
		}

		// ★追加: カーソル移動音の再生
		if (movedCursor) {
			Engine::Audio::GetInstance()->Play(seSelectHandle_);
		}

		// 決定
		if (WasPressed_(VK_SPACE) || WasPressed_(VK_RETURN) || WasButtonPressed_(XINPUT_GAMEPAD_A)) {
			// ★追加: 決定音の再生
			Engine::Audio::GetInstance()->Play(seDecideHandle_);

			if (pauseCursor_ == 0) {
				// ゲーム再開
				isPaused_ = false;
			} else if (pauseCursor_ == 1) {
				// セレクトに戻る (遷移予約)
				isWaitingForRelease_ = true;
				pendingNext_ = "Title";
				pendingSelectStart_ = true; // セレクト画面から開始

				// ★追加: 音を停止
				Engine::Audio::GetInstance()->Stop(bgmVoiceHandle_);
				if (isMovingSePlaying_) {
					Engine::Audio::GetInstance()->Stop(movingVoiceId_);
				}

			} else if (pauseCursor_ == 2) {
				// タイトルに戻る (遷移予約)
				isWaitingForRelease_ = true;
				pendingNext_ = "Title";
				pendingSelectStart_ = false; // タイトル画面から開始

				// ★追加: 音を停止
				Engine::Audio::GetInstance()->Stop(bgmVoiceHandle_);
				if (isMovingSePlaying_) {
					Engine::Audio::GetInstance()->Stop(movingVoiceId_);
				}
			}
		}

		// パッド状態更新
		prevPadState_ = currentPadState_;
		return;
	}

	if (!isEditorMode_) {
		std::vector<Engine::GameObject>& objects = world_.GetObjects();
		for (auto& obj : objects) {
			if (obj.gimmick != nullptr)
				obj.gimmick->Update();
		}

		if (!isPlayerPositioned_) {
			int targetId = TitleScene::SelectedStageIndex;
			auto it = gRespawnTable.find(targetId);
			if (it != gRespawnTable.end()) {
				Engine::Vector3 targetPos = it->second;
				targetPos.y += 0.5f;
				player_.Reset(targetPos);
				gCurrentRespawnId = targetId;
				float cx = -std::sin(camYaw_) * camBack_;
				float cz = -std::cos(camYaw_) * camBack_;
				float cy = camHeight_;
				camera_.SetPosition(targetPos.x + cx, targetPos.y + cy, targetPos.z + cz);
			}
			isPlayerPositioned_ = true;
		}

		// -------------------------------------------------------------
		if (state_ == SceneState::Play) {
			if (ConsumeGoalReached()) {
				state_ = SceneState::ClearAnim;
				clearAnimTimer_ = 0.0f;

				hitParticles_.Clear();
				dustParticles_.Clear();

				Engine::Vector3 pPos = player_.GetPosition();
				float groundY = pPos.y - player_.GetRadius();

				clearAnimBasePos_ = pPos;
				clearAnimBasePos_.z -= 2.5f;
				clearAnimBasePos_.y = groundY + 1.0f;

				player_.SetVelocity({0, 0, 0});
				player_.SetInputEnabled(false);

				clearAnimCamAngle_ = camYaw_ + 3.14159f;
			}
		}

		if (state_ == SceneState::Play) {
			player_.Update(world_.GetObjects());
			const Engine::Vector3 pNew = player_.GetPosition();

			const Engine::Vector3 dir = player_.GetDirection();
			float targetYaw = std::atan2(dir.x, dir.z);
			float diff = NormalizeAngle(targetYaw - camYaw_);
			camYawVel_ += diff * (0.012f + (std::min)(0.20f, 0.6f * std::abs(diff)));
			camYawVel_ *= 0.86f;
			camYaw_ = NormalizeAngle(camYaw_ + (std::max)(-0.10f, (std::min)(0.10f, camYawVel_)));

			float cx = -std::sin(camYaw_) * camBack_;
			float cz = -std::cos(camYaw_) * camBack_;
			float cy = camHeight_;

			Engine::Vector3 camDir = {0, 0, 0};
			float camDistIdeal = std::sqrt(cx * cx + cy * cy + cz * cz);
			if (camDistIdeal > 0.0001f) {
				camDir = {cx / camDistIdeal, cy / camDistIdeal, cz / camDistIdeal};
			}

			float startOffset = 0.6f;
			Engine::Vector3 rayOrigin = VAdd(pNew, VScale(camDir, startOffset));
			float hitDist = 0.0f;
			Engine::GameObject* hitObj = world_.CastRay(rayOrigin, camDir, hitDist, false);

			float finalDist = camDistIdeal;
			if (hitObj != nullptr) {
				if (hitDist > 0.1f) {
					float realHitDist = hitDist + startOffset;
					if (realHitDist < camDistIdeal) {
						finalDist = std::max(0.2f, realHitDist - 0.2f);
					}
				}
			}

			Engine::Vector3 targetPos = VAdd(pNew, VScale(camDir, finalDist));
			Engine::Vector3 newCamPos = Engine::Lerp(camera_.GetPosition(), targetPos, 0.2f);
			camera_.SetPosition(newCamPos.x, newCamPos.y, newCamPos.z);
			camera_.SetRotation(camera_.Rotation().x + (camPitch_ - camera_.Rotation().x) * 0.08f, camYaw_, 0.0f);

			charPos_ = pNew;
			charPos_.y += 1.5f;
			Engine::Vector3 pDir = player_.GetDirection();
			if (std::abs(pDir.x) > 0.001f || std::abs(pDir.z) > 0.001f)
				charRotateY_ = std::atan2(pDir.x, pDir.z);

			Engine::Vector3 pVel = player_.GetVelocity();
			charSpeed_ = std::sqrt(pVel.x * pVel.x + pVel.z * pVel.z);

			// =========================================================
			// ★追加: 移動音(idou.mp3)の制御
			// =========================================================
			// 速度が出ていて、かつ地面に接地している場合のみ鳴らす
			bool isMovingNow = (charSpeed_ > 0.1f && player_.IsGrounded());
			auto* audio = Engine::Audio::GetInstance();

			if (isMovingNow) {
				// 移動中なのに音が鳴っていなければ再生開始
				if (!isMovingSePlaying_) {
					// ループ再生で鳴らし、停止用にハンドル(movingVoiceId_)を保存
					movingVoiceId_ = audio->Play(seMoveHandle_, true, 0.8f);
					isMovingSePlaying_ = true;
				}
			} else {
				// 停止中なのに音が鳴っていたら止める
				if (isMovingSePlaying_) {
					audio->Stop(movingVoiceId_);
					isMovingSePlaying_ = false;
				}
			}
			// =========================================================

			float maxSpeedReference = 3.0f;
			float speedRatio = std::clamp(charSpeed_ / maxSpeedReference, 0.0f, 1.0f);
			float currentFov = 60.0f + (100.0f - 60.0f) * (speedRatio * speedRatio);
			camera_.SetProjection(XMConvertToRadians(currentFov), (float)Engine::WindowDX::kW / (float)Engine::WindowDX::kH, 0.1f, 500.0f);

			if (renderer_) {
				auto params = renderer_->GetPostProcessParams();
				params.chromaShift += (0.002f + 0.015f * speedRatio - params.chromaShift) * 0.1f;
				params.distortion += (0.0025f - 0.1f * speedRatio - params.distortion) * 0.1f;
				renderer_->SetPostProcessParams(params);
			}
			if (speedRatio > 0.8f && !camera_.IsShaking()) {
				camera_.StartShake(0.1f, 0.05f * speedRatio, 0.0f);
			}

			dustParticles_.Update(1.0f / 60.0f);

			if (charSpeed_ > 0.3f) {
				if (player_.IsGrounded()) {
					int count = (charSpeed_ > 1.5f) ? 6 : 3;
					for (int i = 0; i < count; ++i) {
						Engine::Vector3 spawnPos = pNew;
						spawnPos.x += ((rand() % 100) - 50) * 0.01f;
						spawnPos.y -= player_.GetRadius() * 0.95f;
						spawnPos.z += ((rand() % 100) - 50) * 0.01f;

						Engine::Vector3 dVel = VScale(pDir, -charSpeed_ * 0.2f);
						dVel.x += ((rand() % 100) - 50) * 0.03f;
						dVel.y += ((rand() % 100)) * 0.01f;
						dVel.z += ((rand() % 100) - 50) * 0.03f;

						float cV = 0.8f + (float)(rand() % 20) * 0.01f;
						float s = 0.04f + (float)(rand() % 40) * 0.001f;

						dustParticles_.Emit(spawnPos, dVel, {s, s, s}, {cV, cV, cV, 0.3f}, 0.8f);
					}
				}
			}

			hitParticles_.Update(1.0f / 60.0f);
			respawnParticles_.Update(1.0f / 60.0f);

			if (WasPressed_('R') || WasButtonPressed_(XINPUT_GAMEPAD_X)) {
				player_.Reset(GetRespawnPos());
				camera_.SetPosition(player_.GetPosition().x, player_.GetPosition().y + camHeight_, player_.GetPosition().z - camBack_);
				camera_.SetRotation(camPitch_, 0.0f, 0.0f);
				ReSpawnParticle();
				kiete_ = true;
			}
			if (kiete_) {
				kieteTime_++;
			}

			if (kieteTime_ > 30.0f) {
				kiete_ = false;
				kieteTime_ = 0;
			}


		} else if (state_ == SceneState::ClearAnim) {
			clearAnimTimer_ += 1.0f / 60.0f;
			float t = std::fmod(clearAnimTimer_, 1.0f);
			float jumpHeight = (t < 0.4f) ? std::sin((t / 0.4f) * 3.14159f) * 1.5f : 0.0f;
			charPos_ = clearAnimBasePos_;
			charPos_.y += jumpHeight;
			clearAnimCamAngle_ += 0.5f * (1.0f / 60.0f);
			float cx = charPos_.x + std::sin(clearAnimCamAngle_) * 7.0f, cz = charPos_.z + std::cos(clearAnimCamAngle_) * 7.0f, cy = charPos_.y + 3.0f;
			camera_.SetPosition(cx, cy, cz);
			float dx = charPos_.x - cx, dy = charPos_.y - cy, dz = charPos_.z - cz, hD = std::sqrt(dx * dx + dz * dz);
			camera_.SetRotation(-std::atan2(dy, hD), std::atan2(dx, dz), 0.0f);
			charSpeed_ = 5.0f;
			confettiParticles_.Update(1.0f / 60.0f);
			confettiSpawnTimer_ += 1.0f / 60.0f;

			if (confettiSpawnTimer_ > 0.08f) {
				confettiSpawnTimer_ = 0.0f;
				Engine::Vector3 cPos = camera_.GetPosition();
				Engine::Vector3 fwd = VSub(charPos_, cPos);
				float flen = std::sqrt(fwd.x * fwd.x + fwd.y * fwd.y + fwd.z * fwd.z);
				if (flen > 0)
					fwd = VScale(fwd, 1.0f / flen);
				Engine::Vector3 up = {0, 1, 0};
				Engine::Vector3 right;
				right.x = up.y * fwd.z - up.z * fwd.y;
				right.y = up.z * fwd.x - up.x * fwd.z;
				right.z = up.x * fwd.y - up.y * fwd.x;
				float rLen = std::sqrt(right.x * right.x + right.y * right.y + right.z * right.z);
				if (rLen > 0)
					right = VScale(right, 1.0f / rLen);

				for (int i = 0; i < 4; ++i) {
					float dirSign = (i % 2 == 0) ? -1.0f : 1.0f;
					float rx = (float)(rand() % 100 - 50) * 0.02f;
					float ry = (float)(rand() % 100 - 50) * 0.02f;
					Engine::Vector3 sPos = cPos;
					sPos = VAdd(sPos, VScale(fwd, 8.0f));
					sPos = VAdd(sPos, VScale(right, dirSign * 8.0f + rx));
					sPos = VAdd(sPos, VScale(up, 6.0f + ry));
					Engine::Vector4 col = {1.0f, 1.0f, 1.0f, 1.0f};
					int ct = rand() % 5;
					if (ct == 0)
						col = {1.0f, 0.4f, 0.4f, 1.0f};
					else if (ct == 1)
						col = {0.4f, 1.0f, 0.4f, 1.0f};
					else if (ct == 2)
						col = {0.4f, 0.4f, 1.0f, 1.0f};
					else if (ct == 3)
						col = {1.0f, 1.0f, 0.4f, 1.0f};
					else
						col = {1.0f, 0.6f, 1.0f, 1.0f};
					Engine::Vector3 av = {(float)(rand() % 100 - 50) * 0.1f, (float)(rand() % 100 - 50) * 0.1f, (float)(rand() % 100 - 50) * 0.1f};
					confettiParticles_.Emit(sPos, {0, -1.5f, 0}, {0.2f, 0.2f, 0.2f}, col, 4.0f, av);
				}
			}
			// ★修正: クリア時もボタン離し待ちを導入して誤遷移を防止
			if (clearAnimTimer_ > 0.5f && (WasPressed_(VK_SPACE) || WasPressed_(VK_RETURN) || WasButtonPressed_(XINPUT_GAMEPAD_A))) {
				isWaitingForRelease_ = true;
				// ★修正: "Title" シーンへ戻るように変更
				pendingNext_ = "Title";
				TitleScene::IsSelectStart = false;

				// ★追加: クリア時にBGMと移動音を停止
				Engine::Audio::GetInstance()->Stop(bgmVoiceHandle_);
				if (isMovingSePlaying_) {
					Engine::Audio::GetInstance()->Stop(movingVoiceId_);
				}
			}
		}

		Engine::Model* model = renderer_->GetModel(walkModelHandle_);
		if (model && !model->GetData().animations.empty()) {
			const auto& anim = model->GetData().animations[0];
			float pR = charSpeed_ * 5.0f;
			if (state_ == SceneState::ClearAnim)
				pR = 20.0f;
			animationTime_ += (1.0f / 60.0f) * anim.ticksPerSecond * pR;
			animationTime_ = std::fmod(animationTime_, anim.duration);
			Engine::Matrix4x4 id = Engine::Matrix4x4::Identity();
			model->UpdateSkeleton(model->GetData().rootNode, id, anim, animationTime_, bonePalette_);
		}
	}
	// 最後にパッド状態を更新
	prevPadState_ = currentPadState_;
}

void GameScene::Draw() {
	renderer_->SetCamera(camera_);
	world_.Draw(renderer_);
	if (!kiete_) {
		player_.Draw();
	}
	if (state_ == SceneState::ClearAnim)
		confettiParticles_.Draw(camera_);
	hitParticles_.Draw(camera_);
	dustParticles_.Draw(camera_);
	respawnParticles_.Draw(camera_, "TimedBlock");

	// ゴール演出中なら画像を画面下に描画
	if (state_ == SceneState::ClearAnim && clearMojiTex_) {
		Engine::Renderer::SpriteDesc spr;
		spr.w = 600.0f;
		spr.h = 200.0f;
		spr.x = (Engine::WindowDX::kW - spr.w) * 0.5f;
		spr.y = (float)Engine::WindowDX::kH - 250.0f;
		spr.color = {1.0f, 1.0f, 1.0f, 1.0f};
		renderer_->DrawSprite(clearMojiTex_, spr);
	}

	if (walkModelHandle_ != 0) {
		Engine::Transform walkT;
		walkT.translate = charPos_;
		walkT.scale = {1.5f, 1.5f, 1.5f};
		walkT.rotate = {0, charRotateY_, 0};

		if (player_.IsPerformanceActive()) {
			float t = player_.GetPerformanceProgress();
			float spin = t * 3.14159f * 2.0f;
			walkT.rotate.x -= spin;
			float localJump = std::sin(t * 3.14159f) * 1.5f;
			walkT.translate.y += localJump;
		}
		if (!kiete_) {
			renderer_->DrawSkinnedMesh(walkModelHandle_, azarasiTex_, walkT, bonePalette_, {1, 1, 1, 1});
		}
	}
	// ポーズUIの描画
	if (uiPauseTex_) {
		if (isPaused_) {
			Engine::Renderer::SpriteDesc spr;
			spr.w = 400.0f;
			spr.h = 80.0f; // 少し高さを確保
			spr.x = (Engine::WindowDX::kW - spr.w) * 0.5f;

			float time = static_cast<float>(GetTickCount64()) / 400.0f;
			float flash = 0.6f + 0.4f * std::sin(time);

			// ★修正: 各項目のテクスチャを配列で定義
			// 0:一番上(ゲーム), 1:真ん中(セレクト), 2:一番下(タイトル)
			uint32_t menuTextures[3] = {uiPauseGameTex_, uiPauseSelectTex_, uiPauseTitleTex_};

			for (int i = 0; i < 3; ++i) {
				// 位置計算 (上から順に並べる)
				spr.y = (float)Engine::WindowDX::kH * 0.35f + i * 100.0f;

				// 選択中の色は点滅、それ以外は暗く
				if (i == pauseCursor_) {
					spr.color = {1.0f, flash, flash, 1.0f};
				} else {
					spr.color = {0.5f, 0.5f, 0.5f, 1.0f};
				}

				// テクスチャが存在すれば描画
				if (menuTextures[i] != 0) {
					renderer_->DrawSprite(menuTextures[i], spr);
				}
			}
		} else {
			// 通常プレイ中は左下に操作説明(po-zu.png)のみ
			Engine::Renderer::SpriteDesc spr;
			spr.w = 250.0f;
			spr.h = 40.0f;
			spr.x = 20.0f;
			spr.y = (float)Engine::WindowDX::kH - spr.h - 20.0f;
			spr.color = {1.0f, 1.0f, 1.0f, 1.0f};
			renderer_->DrawSprite(uiPauseTex_, spr);
		}
	}

#ifdef _DEBUG
	if (isEditorMode_) {
		dx_->List()->ClearDepthStencilView(dx_->DSV_CPU(0), D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
		for (auto* obj : selectedObjects_)
			if (obj)
				DrawWireframeBox(renderer_, world_.GetCubeMesh(), whiteTex_, obj, {1, 1, 0, 1});
		Engine::GameObject* activeObj = GetActiveObject();
		if (activeObj && !activeObj->isLocked) {
			Engine::Vector3 p = activeObj->transform.translate;
			float len = 2.0f, thk = 0.05f;
			Engine::Transform tX, tY, tZ;
			tX.translate = {p.x + len * 0.5f, p.y, p.z};
			tX.scale = {len, thk, thk};
			renderer_->DrawMesh(world_.GetCubeMesh(), whiteTex_, tX, (hoverAxis_ == GizmoAxis::X || draggingAxis_ == GizmoAxis::X) ? Engine::Vector4{1, 1, 0, 1} : Engine::Vector4{1, 0, 0, 1});
			tY.translate = {p.x, p.y + len * 0.5f, p.z};
			tY.scale = {thk, len, thk};
			renderer_->DrawMesh(world_.GetCubeMesh(), whiteTex_, tY, (hoverAxis_ == GizmoAxis::Y || draggingAxis_ == GizmoAxis::Y) ? Engine::Vector4{1, 1, 0, 1} : Engine::Vector4{0, 1, 0, 1});
			tZ.translate = {p.x, p.y, p.z + len * 0.5f};
			tZ.scale = {thk, thk, len};
			renderer_->DrawMesh(world_.GetCubeMesh(), whiteTex_, tZ, (hoverAxis_ == GizmoAxis::Z || draggingAxis_ == GizmoAxis::Z) ? Engine::Vector4{1, 1, 0, 1} : Engine::Vector4{0, 0, 1, 1});
		}
	}
#endif
}

// ... (以下ヘルパー関数は変更なし) ...
Engine::GameObject* GameScene::GetActiveObject() const { return selectedObjects_.empty() ? nullptr : selectedObjects_.back(); }
bool GameScene::IsSelected(Engine::GameObject* obj) const {
	for (auto* ptr : selectedObjects_)
		if (ptr == obj)
			return true;
	return false;
}
void GameScene::SelectObject(Engine::GameObject* obj, bool additive) {
	if (!obj) {
		if (!additive)
			ClearSelection();
		return;
	}
	if (additive) {
		auto it = std::find(selectedObjects_.begin(), selectedObjects_.end(), obj);
		if (it != selectedObjects_.end())
			selectedObjects_.erase(it);
		else
			selectedObjects_.push_back(obj);
	} else {
		if (selectedObjects_.size() == 1 && selectedObjects_[0] == obj)
			return;
		ClearSelection();
		selectedObjects_.push_back(obj);
	}
}
void GameScene::ClearSelection() { selectedObjects_.clear(); }
void GameScene::RemoveFromSelection(Engine::GameObject* obj) {
	auto it = std::remove(selectedObjects_.begin(), selectedObjects_.end(), obj);
	selectedObjects_.erase(it, selectedObjects_.end());
}

void GameScene::EditorUpdate(bool isHovered, float ndcX, float ndcY) {
	Engine::Vector3 ro, rd;
	GetRayFromNDC(camera_, ndcX, ndcY, ro, rd);
	XMVECTOR rayOrg = XMLoadFloat3((XMFLOAT3*)&ro), rayDir = XMLoadFloat3((XMFLOAT3*)&rd);
	Engine::GameObject* activeObj = GetActiveObject();
	hoverAxis_ = GizmoAxis::None;
	if (activeObj && !activeObj->isLocked && draggingAxis_ == GizmoAxis::None && draggingObject_ == nullptr) {
		Engine::Vector3 p = activeObj->transform.translate;
		float len = 2.0f, thk = 0.2f;
		Engine::AABB boxX = {
		    {p.x,       p.y - thk, p.z - thk},
            {p.x + len, p.y + thk, p.z + thk}
        };
		Engine::AABB boxY = {
		    {p.x - thk, p.y,       p.z - thk},
            {p.x + thk, p.y + len, p.z + thk}
        };
		Engine::AABB boxZ = {
		    {p.x - thk, p.y - thk, p.z      },
            {p.x + thk, p.y + thk, p.z + len}
        };
		if (IntersectRayAABB(rayOrg, rayDir, boxX))
			hoverAxis_ = GizmoAxis::X;
		else if (IntersectRayAABB(rayOrg, rayDir, boxY))
			hoverAxis_ = GizmoAxis::Y;
		else if (IntersectRayAABB(rayOrg, rayDir, boxZ))
			hoverAxis_ = GizmoAxis::Z;
	}
	if (isHovered && ImGui::IsMouseClicked(0)) {
		if (hoverAxis_ != GizmoAxis::None && activeObj && !activeObj->isLocked) {
			draggingAxis_ = hoverAxis_;
			draggingObject_ = activeObj;
			XMVECTOR axisDir = (draggingAxis_ == GizmoAxis::X) ? XMVectorSet(1, 0, 0, 0) : ((draggingAxis_ == GizmoAxis::Y) ? XMVectorSet(0, 1, 0, 0) : XMVectorSet(0, 0, 1, 0));
			axisDragOffset_ = GetClosestPointOnAxis(rayOrg, rayDir, XMLoadFloat3((XMFLOAT3*)&activeObj->transform.translate), axisDir);
		} else {
			float dist = 0;
			Engine::GameObject* hitObj = world_.CastRay(ro, rd, dist, true);
			SelectObject(hitObj, ImGui::GetIO().KeyCtrl);
			if (hitObj && IsSelected(hitObj) && !hitObj->isLocked) {
				draggingObject_ = hitObj;
				dragPlaneY_ = hitObj->transform.translate.y;
				if (std::abs(rd.y) > 1e-5) {
					float t = (dragPlaneY_ - ro.y) / rd.y;
					if (t > 0)
						dragOffset_ = VSub(hitObj->transform.translate, VAdd(ro, VScale(rd, t)));
				}
			}
		}
	}
	if (ImGui::IsMouseReleased(0)) {
		draggingObject_ = nullptr;
		draggingAxis_ = GizmoAxis::None;
	}
	if (draggingObject_) {
		if (draggingObject_->isLocked)
			draggingObject_ = nullptr;
		else {
			Engine::Vector3 prevPos = draggingObject_->transform.translate, newPos = prevPos;
			bool moved = false;
			if (draggingAxis_ != GizmoAxis::None && draggingObject_ == activeObj) {
				XMVECTOR aD = (draggingAxis_ == GizmoAxis::X) ? XMVectorSet(1, 0, 0, 0) : ((draggingAxis_ == GizmoAxis::Y) ? XMVectorSet(0, 1, 0, 0) : XMVectorSet(0, 0, 1, 0));
				float cT = GetClosestPointOnAxis(rayOrg, rayDir, XMLoadFloat3((XMFLOAT3*)&prevPos), aD);
				float d = cT - axisDragOffset_;
				if (draggingAxis_ == GizmoAxis::X)
					newPos.x += d;
				if (draggingAxis_ == GizmoAxis::Y)
					newPos.y += d;
				if (draggingAxis_ == GizmoAxis::Z)
					newPos.z += d;
				moved = true;
			} else if (draggingAxis_ == GizmoAxis::None && std::abs(rd.y) > 1e-5) {
				float t = (dragPlaneY_ - ro.y) / rd.y;
				if (t > 0) {
					newPos = VAdd(VAdd(ro, VScale(rd, t)), dragOffset_);
					moved = true;
				}
			}
			if (moved) {
				newPos.x = std::round(newPos.x);
				newPos.y = std::round(newPos.y);
				newPos.z = std::round(newPos.z);
				Engine::Vector3 delta = VSub(newPos, prevPos);
				if (std::abs(delta.x) > 0.001f || std::abs(delta.y) > 0.001f || std::abs(delta.z) > 0.001f) {
					draggingObject_->transform.translate = newPos;
					for (auto* obj : selectedObjects_)
						if (obj != draggingObject_ && !obj->isLocked) {
							obj->transform.translate = VAdd(obj->transform.translate, delta);
							obj->transform.translate.x = std::round(obj->transform.translate.x);
							obj->transform.translate.y = std::round(obj->transform.translate.y);
							obj->transform.translate.z = std::round(obj->transform.translate.z);
						}
				}
			}
		}
	}
	if (isHovered && ImGui::IsMouseDown(1)) {
		ImVec2 d = ImGui::GetIO().MouseDelta;
		debugCamRot_.y += d.x * 0.005f;
		debugCamRot_.x += d.y * 0.005f;
		float sp = (GetAsyncKeyState(VK_SHIFT) & 0x8000) ? 1.5f : 0.5f;
		Engine::Vector3 f = {std::sin(debugCamRot_.y), 0, std::cos(debugCamRot_.y)}, r = {std::cos(debugCamRot_.y), 0, -std::sin(debugCamRot_.y)};
		if (GetAsyncKeyState('W') & 0x8000)
			debugCamPos_ = VAdd(debugCamPos_, VScale(f, sp));
		if (GetAsyncKeyState('S') & 0x8000)
			debugCamPos_ = VSub(debugCamPos_, VScale(f, sp));
		if (GetAsyncKeyState('D') & 0x8000)
			debugCamPos_ = VAdd(debugCamPos_, VScale(r, sp));
		if (GetAsyncKeyState('A') & 0x8000)
			debugCamPos_ = VSub(debugCamPos_, VScale(r, sp));
		if (GetAsyncKeyState('E') & 0x8000)
			debugCamPos_.y += sp;
		if (GetAsyncKeyState('Q') & 0x8000)
			debugCamPos_.y -= sp;
	}
	camera_.SetPosition(debugCamPos_.x, debugCamPos_.y, debugCamPos_.z);
	camera_.SetRotation(debugCamRot_.x, debugCamRot_.y, 0.0f);
}

void GameScene::SpawnModelAtNDC(const std::string& filename, float ndcX, float ndcY) {
	Engine::Vector3 ro, rd;
	GetRayFromNDC(camera_, ndcX, ndcY, ro, rd);
	if (std::abs(rd.y) > 1e-5) {
		float t = -ro.y / rd.y;
		if (t > 0) {
			Engine::Vector3 hit = VAdd(ro, VScale(rd, t));
			world_.CreateObjectFromFile(filename, {std::round(hit.x), 0.0f, std::round(hit.z)});
		}
	}
}
void GameScene::SpawnBlockAtNDC(Game::ObjectType type, float ndcX, float ndcY) {
	Engine::Vector3 ro, rd;
	GetRayFromNDC(camera_, ndcX, ndcY, ro, rd);
	if (std::abs(rd.y) > 1e-5) {
		float t = -ro.y / rd.y;
		if (t > 0) {
			Engine::Vector3 hit = VAdd(ro, VScale(rd, t));
			world_.CreateObject(type, {std::round(hit.x), 0.0f, std::round(hit.z)});
		}
	}
}
void GameScene::SetCameraDirection(CameraDirection dir) {
	float dist = std::sqrt(debugCamPos_.x * debugCamPos_.x + debugCamPos_.y * debugCamPos_.y + debugCamPos_.z * debugCamPos_.z),
	      hDist = std::sqrt(debugCamPos_.x * debugCamPos_.x + debugCamPos_.z * debugCamPos_.z);
	if (hDist < 0.1f) {
		hDist = dist;
		debugCamPos_.y = 0;
		debugCamRot_.x = 0;
	}
	switch (dir) {
	case CameraDirection::Front:
		debugCamRot_ = {debugCamRot_.x, 0, 0};
		debugCamPos_ = {0, debugCamPos_.y, -hDist};
		break;
	case CameraDirection::Back:
		debugCamRot_ = {debugCamRot_.x, XM_PI, 0};
		debugCamPos_ = {0, debugCamPos_.y, hDist};
		break;
	case CameraDirection::Right:
		debugCamRot_ = {debugCamRot_.x, -XM_PIDIV2, 0};
		debugCamPos_ = {hDist, debugCamPos_.y, 0};
		break;
	case CameraDirection::Left:
		debugCamRot_ = {debugCamRot_.x, XM_PIDIV2, 0};
		debugCamPos_ = {-hDist, debugCamPos_.y, 0};
		break;
	case CameraDirection::Top:
		debugCamRot_ = {XM_PIDIV2, 0, 0};
		debugCamPos_ = {0, dist, 0};
		break;
	case CameraDirection::Bottom:
		debugCamRot_ = {-XM_PIDIV2, 0, 0};
		debugCamPos_ = {0, -dist, 0};
		break;
	}
	camera_.SetPosition(debugCamPos_.x, debugCamPos_.y, debugCamPos_.z);
	camera_.SetRotation(debugCamRot_.x, debugCamRot_.y, 0.0f);
}

void GameScene::ReSpawnParticle() {
	// プレイヤー位置を取得
	Engine::Vector3 playerPos = player_.GetPosition();
	Engine::Vector3 camPos = camera_.GetPosition();

	// カメラとプレイヤーの間（プレイヤー寄り 90%）を煙の発生中心にする
	float blendRatio = 0.9f; // 0=カメラ, 1=プレイヤー
	Engine::Vector3 smokeCenter;
	smokeCenter.x = camPos.x + (playerPos.x - camPos.x) * blendRatio;
	smokeCenter.z = camPos.z + (playerPos.z - camPos.z) * blendRatio;

	// 足元の高さと中心の高さを定義
	float footY = playerPos.y - player_.GetRadius(); // 足元
	float centerY = playerPos.y;                      // 中心

	// 乱数準備
	std::random_device rd;
	std::mt19937 gen(rd());
	const float PI = 3.14159265358979f;
	std::uniform_real_distribution<float> distAngle(0.0f, 2.0f * PI);
	std::uniform_real_distribution<float> distRadius(0.0f, 1.0f);
	std::uniform_real_distribution<float> distGray(0.7f, 0.95f);   // 煙の色（グレー系）
	std::uniform_real_distribution<float> distScale(0.8f, 1.5f);
	std::uniform_real_distribution<float> distLife(0.5f, 1.2f);
	std::uniform_real_distribution<float> distUpSpeed(1.5f, 3.5f); // 上昇速度
	std::uniform_real_distribution<float> distHeight(0.0f, 1.0f);  // 高さのランダム（0=足元, 1=中心）

	const int count = 50;
	const float spreadRadius = 0.8f; // 水平方向の広がり

	for (int i = 0; i < count; ++i) {
		// ランダムな角度と半径で水平方向に広がる
		float ang = distAngle(gen);
		float r = std::sqrt(distRadius(gen)) * spreadRadius;
		float rx = std::cos(ang) * r;
		float rz = std::sin(ang) * r;

		// 高さをランダムに（足元〜中心の間）
		float heightRatio = distHeight(gen);
		float baseY = footY + (centerY - footY) * heightRatio;

		// 発生位置
		Engine::Vector3 pos;
		pos.x = smokeCenter.x + rx;
		pos.y = baseY;
		pos.z = smokeCenter.z + rz;

		// 速度：主に上方向へ、少し横に広がる
		float upSpeed = distUpSpeed(gen);
		float lateralSpeed = 0.3f + distRadius(gen) * 0.5f;
		Engine::Vector3 vel;
		vel.x = std::cos(ang) * lateralSpeed * 0.5f;
		vel.y = upSpeed;
		vel.z = std::sin(ang) * lateralSpeed * 0.5f;

		// スケール（煙は大きめ）
		float s = 0.6f * distScale(gen);
		Engine::Vector3 scale = {s, s, s};

		// 色：グレー系の煙（半透明）
		float gray = distGray(gen);
		Engine::Vector4 color = {gray, gray, gray, 0.6f};

		float life = distLife(gen);

		// 回転（煙らしくゆっくり回転）
		Engine::Vector3 angVel = {
			((std::rand() % 100) - 50) * 0.01f,
			((std::rand() % 100) - 50) * 0.01f,
			((std::rand() % 100) - 50) * 0.01f
		};

		respawnParticles_.Emit(pos, vel, scale, color, life, angVel);
	}
}

} // namespace Game