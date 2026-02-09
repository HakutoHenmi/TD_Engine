#include "TitleScene.h"
#include "WindowDX.h"
#include <DirectXMath.h>
#include <algorithm>
#include <cmath>
#include <random>

// Modelクラスのメソッドを使うために必要
#include "../../Engine/Model.h"

namespace Game {

using namespace DirectX;

// 静的変数の実体定義（初期値は0）
int TitleScene::SelectedStageIndex = 0;
// 静的変数の初期化
bool TitleScene::IsSelectStart = false;

static float EaseOutCubic(float x) { return 1.0f - std::pow(1.0f - x, 3.0f); }

static float EaseOutBounce(float x) {
	const float n1 = 7.5625f;
	const float d1 = 2.75f;
	if (x < 1.0f / d1)
		return n1 * x * x;
	else if (x < 2.0f / d1)
		return n1 * (x -= 1.5f / d1) * x + 0.75f;
	else if (x < 2.5f / d1)
		return n1 * (x -= 2.25f / d1) * x + 0.9375f;
	else
		return n1 * (x -= 2.625f / d1) * x + 0.984375f;
}

static float Lerp(float a, float b, float t) { return a + (b - a) * t; }
static Engine::Vector3 LerpVec3(const Engine::Vector3& a, const Engine::Vector3& b, float t) { return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t}; }

TitleScene::~TitleScene() {
	// シングルトン管理になったため、個別のdeleteは不要
}

void TitleScene::Initialize(Engine::WindowDX*) {

	// ★修正: BGMのロードと再生
	auto* audio = Engine::Audio::GetInstance();

	// データをロード (ここは uint32_t)
	bgmHandle_ = audio->Load("Resources/Sound/BGM1.mp3");

	// ★修正: 再生し、戻り値の「ボイスハンドル」を保存する (size_t)
	bgmVoiceHandle_ = audio->Play(bgmHandle_, true, 0.5f);

	end_ = false;
	next_.clear();
	for (int i = 0; i < 256; ++i)
		prevKey_[i] = GetAsyncKeyState(i);

	// コントローラー状態の初期化
	ZeroMemory(&currentPadState_, sizeof(XINPUT_STATE));
	ZeroMemory(&prevPadState_, sizeof(XINPUT_STATE));

	renderer_ = Engine::Renderer::GetInstance();
	state_ = TitleState::WaitInput;
	animTimer_ = 0.0f;
	zoomTimer_ = 0.0f;
	cameraAngle_ = XM_PI;
	currentStage_ = 0;

	camera_.Initialize();
	const float aspect = (float)Engine::WindowDX::kW / (float)Engine::WindowDX::kH;
	camera_.SetProjection(XMConvertToRadians(60.0f), aspect, 0.1f, 500.0f);

	// --- リソースロード ---
	peakMesh_ = renderer_->LoadObjMesh("Resources/TitleParts/peak.obj");
	peakTex_ = renderer_->LoadTexture2D("Resources/UI/PEAK.png", true);

	titleBodyMesh_ = renderer_->LoadObjMesh("Resources/TitleParts/title.obj");
	titleFutiMesh_ = renderer_->LoadObjMesh("Resources/TitleParts/title_futi.obj");
	titleBodyTex_ = renderer_->LoadTexture2D("Resources/TitleParts/title.png");
	titleFutiTex_ = renderer_->LoadTexture2D("Resources/TitleParts/title_futi.png");

	// ステージ番号用メッシュロード
	stageMeshes_.clear();
	for (int i = 0; i < kStageCount; ++i) {
		std::string fileName = "Resources/TitleParts/" + std::to_string(i) + ".obj";
		stageMeshes_.push_back(renderer_->LoadObjMesh(fileName));
	}

	cloudMesh_ = renderer_->LoadObjMesh("Resources/TitleParts/kumo.obj");
	cloudTex_ = renderer_->LoadTexture2D("Resources/TitleParts/siro.png", true);

	treeMesh_ = renderer_->LoadObjMesh("Resources/parts/ki.obj");
	treeTex_ = renderer_->LoadTexture2D("Resources/parts/ki.png", true);

	planeMesh_ = renderer_->LoadObjMesh("Resources/plane.obj");
	planeTex_ = renderer_->LoadTexture2D("Resources/white1x1.png", true);

	// 魚のgltfをロード
	fishMesh_ = renderer_->LoadObjMesh("Resources/fish.gltf");

	// ボーンパレットの準備
	Engine::Model* model = renderer_->GetModel(fishMesh_);
	if (model) {
		fishBonePalette_.resize(model->GetData().bones.size(), Engine::Matrix4x4::Identity());
	}

	pressTex_ = renderer_->LoadTexture2D("Resources/UI/moji2.png");

	// UIロード
	uiSelectMoveTex_ = renderer_->LoadTexture2D("Resources/UI/senntakuUI.png");
	uiSelectDecideTex_ = renderer_->LoadTexture2D("Resources/UI/serekutoUI2.png");

	// =========================================================
	// ★修正: 効果音ファイルの読み込み
	// =========================================================
	// 決定音 (kettei.mp3)
	seDecisionHandle_ = audio->Load("Resources/Sound/kettei.mp3");

	// 選択移動音 (serekutosenntaku.mp3)
	seSelectHandle_ = audio->Load("Resources/Sound/serekutosenntaku.mp3");
	// =========================================================

	pressTimer_ = 0.0f;

	// --- 配置設定 ---
	peakTransform_.translate = {0.0f, -4.5f, 0.0f};
	peakTransform_.rotate = {0.0f, 0.0f, 0.0f};
	peakTransform_.scale = {1.0f, 1.0f, 1.0f};

	titleTransform_.translate = {0.0f, 40.0f, 0.0f};
	titleTransform_.rotate = {1.57f, 0.0f, 0.0f};
	titleTransform_.scale = {10.0f, 10.0f, 10.0f};

	planeTransform_.translate = {0.0f, -5.6f, 0.0f};
	planeTransform_.rotate = {-XM_PIDIV2, 0.0f, 0.0f};
	planeTransform_.scale = {500.0f, 500.0f, 1.0f};

	// --- ステージの螺旋配置とカメラ設定 ---
	stageTransforms_.clear();
	stageSettings_.clear();

	float startY = -3.8f;
	float endY = 3.5f;

	float startRadius = 7.2f; // 下の方はそのまま
	float endRadius = 1.2f;   // 上の方を山に近づける

	float startAngle = 3.6f;
	float totalWind = XM_2PI * 1.5f;

	for (int i = 0; i < kStageCount; ++i) {
		float t = (float)i / (float)(kStageCount - 1); // 0.0 〜 1.0

		// 位置計算
		float y = Lerp(startY, endY, t);
		float r = Lerp(startRadius, endRadius, t);
		float angle = startAngle + t * totalWind;

		float x = r * std::sin(angle);
		float z = r * std::cos(angle);

		Engine::Transform tr;
		tr.translate = {x, y, z};
		// ステージの向き
		tr.rotate = {1.57f - 0.3f, angle, 0.0f};
		tr.scale = {0.6f, 0.6f, 0.6f};
		stageTransforms_.push_back(tr);

		// カメラ設定計算
		StageViewSettings set;
		set.angle = angle;
		set.height = y + 1.5f;
		set.distance = 12.0f;
		set.lookAtOffset = {0.0f, 0.5f, 0.0f};
		stageSettings_.push_back(set);
	}

	currentViewAngle_ = XM_PI;
	currentViewDist_ = 5.0f;
	currentViewHeight_ = -2.0f;
	currentLookAt_ = {0.0f, 2.0f, 0.0f};

	// --- ランダム生成 ---
	std::random_device rd;
	std::mt19937 gen(rd());

	// 雲
	std::uniform_real_distribution<float> distPosXZ(-40.0f, 40.0f);
	std::uniform_real_distribution<float> distPosY(8.0f, 20.0f);
	std::uniform_real_distribution<float> distRot(0.0f, XM_2PI);
	for (int i = 0; i < 30; ++i) {
		CloudData c;
		c.transform.translate = {distPosXZ(gen), distPosY(gen), distPosXZ(gen)};
		c.transform.rotate = {0.0f, distRot(gen), 0.0f};
		float s = std::uniform_real_distribution<float>(0.8f, 2.0f)(gen);
		c.transform.scale = {s, s, s};
		c.speed = std::uniform_real_distribution<float>(0.01f, 0.05f)(gen);
		clouds_.push_back(c);
	}

	// 木
	for (int i = 0; i < 3000; ++i) {
		TreeData t;
		float angle = std::uniform_real_distribution<float>(0.0f, XM_2PI)(gen);
		float radius = std::uniform_real_distribution<float>(12.0f, 230.0f)(gen);
		t.transform.translate = {radius * std::cos(angle), -4.5f, radius * std::sin(angle)};
		t.transform.rotate = {0.0f, distRot(gen), 0.0f};
		float s = std::uniform_real_distribution<float>(0.7f, 1.3f)(gen);
		t.transform.scale = {s, s, s};
		trees_.push_back(t);
	}

	// 山
	for (int i = 0; i < 20; ++i) {
		MountainData m;
		float angle = (XM_2PI / 20) * i + distRot(gen) * 0.1f;
		float r = std::uniform_real_distribution<float>(180.0f, 220.0f)(gen);
		float s = std::uniform_real_distribution<float>(15.0f, 25.0f)(gen);
		m.transform.translate = {r * std::cos(angle), -4.5f * (s * 0.4f), r * std::sin(angle)};
		m.transform.rotate = {0.0f, distRot(gen), 0.0f};
		m.transform.scale = {s, s, s};
		bgMountains_.push_back(m);
	}

	// 魚パーティクル
	std::uniform_real_distribution<float> distPartXZ(-60.0f, 60.0f);
	std::uniform_real_distribution<float> distPartY(0.0f, 30.0f);
	std::uniform_real_distribution<float> distColor(0.4f, 1.0f);

	for (int i = 0; i < 150; ++i) {
		Particle p;
		p.transform.translate = {distPartXZ(gen), distPartY(gen), distPartXZ(gen)};
		p.transform.rotate = {0.0f, distRot(gen), 0.0f};
		float s = std::uniform_real_distribution<float>(0.4f, 0.8f)(gen);
		p.transform.scale = {s, s, s};
		p.velocity = {0.0f, -std::uniform_real_distribution<float>(0.01f, 0.03f)(gen), 0.0f};
		p.color = {distColor(gen), distColor(gen), distColor(gen), 1.0f};
		p.swayPhase = std::uniform_real_distribution<float>(0.0f, XM_2PI)(gen);
		p.swaySpeed = 1.0f + std::abs(p.velocity.y) * 15.0f;
		p.animTime = std::uniform_real_distribution<float>(0.0f, 10.0f)(gen);
		particles_.push_back(p);
	}

	// セレクト開始フラグが立っていたら、即座にセレクト画面の状態にする
	if (IsSelectStart) {
		state_ = TitleState::SelectStage;
		currentStage_ = SelectedStageIndex; // 前回選んだステージに合わせる

		// カメラをターゲット位置へスナップ
		const auto& setting = stageSettings_[currentStage_];
		currentViewAngle_ = setting.angle;
		currentViewDist_ = setting.distance;
		currentViewHeight_ = setting.height;
		currentLookAt_ = stageTransforms_[currentStage_].translate + setting.lookAtOffset;

		Engine::Vector3 camPos = {std::sin(currentViewAngle_) * currentViewDist_, currentViewHeight_, std::cos(currentViewAngle_) * currentViewDist_};
		camera_.SetPosition(camPos.x, camPos.y, camPos.z);
		Engine::Vector3 dir = {currentLookAt_.x - camPos.x, currentLookAt_.y - camPos.y, currentLookAt_.z - camPos.z};
		camera_.SetRotation(std::atan2(-dir.y, std::sqrt(dir.x * dir.x + dir.z * dir.z)), std::atan2(dir.x, dir.z), 0.0f);

		// フラグをリセットしておく
		IsSelectStart = false;
	}

	if (renderer_) {
		renderer_->SetPostProcessEnabled(false);
		renderer_->SetAmbientColor({0.5f, 0.5f, 0.6f});
		renderer_->SetDirectionalLight({0.5f, -1.0f, -0.5f}, {1.0f, 0.98f, 0.95f}, true);
	}
}

void TitleScene::Update() {
	// フレーム開始時にコントローラー入力を更新
	prevPadState_ = currentPadState_;
	if (XInputGetState(0, &currentPadState_) != ERROR_SUCCESS) {
		ZeroMemory(&currentPadState_, sizeof(XINPUT_STATE));
	}

	float targetAngle = 0.0f;
	float targetHeight = 0.0f;
	float targetDist = 0.0f;
	Engine::Vector3 targetLookAt = {0.0f, 0.0f, 0.0f};

	// 現在のステージ設定を取得
	const auto& setting = stageSettings_[currentStage_];
	Engine::Vector3 stagePos = stageTransforms_[currentStage_].translate;

	if (animTimer_ < 10.0f)
		animTimer_ += 1.0f / 60.0f;

	// タイトル落下演出
	float dropStart = 0.5f;
	if (animTimer_ > dropStart) {
		float t = std::min((animTimer_ - dropStart) / 2.0f, 1.0f);
		titleTransform_.translate.y = 40.0f + (7.0f - 40.0f) * EaseOutBounce(t);
	}
	titleTransform_.rotate.y = cameraAngle_;

	// カメラ演出（オープニング）
	float easeInit = EaseOutCubic(std::min(animTimer_ / 10.0f, 1.0f));
	float introDist = 5.0f + (25.0f - 5.0f) * easeInit;
	float introHeight = -2.0f + (12.0f - (-2.0f)) * easeInit;
	float introWave = std::sin(cameraAngle_ * 2.0f) * 2.0f * easeInit;

	switch (state_) {
	case TitleState::WaitInput:
		// 待機中はカメラを回転させる
		cameraAngle_ += 0.001f;
		if (cameraAngle_ > XM_2PI)
			cameraAngle_ -= XM_2PI;
		targetAngle = cameraAngle_;
		targetDist = introDist;
		targetHeight = introHeight + introWave;
		targetLookAt = {0.0f, 2.0f, 0.0f};

		// 決定操作: キーボード(Return/Space) または パッド(Aボタン)
		if (WasPressed_(VK_RETURN) || WasPressed_(VK_SPACE) || WasPadPressed(XINPUT_GAMEPAD_A)) {

			// ★修正: 決定音を再生 (WaitInput -> Zooming)
			Engine::Audio::GetInstance()->Play(seDecisionHandle_);

			state_ = TitleState::Zooming;
			zoomTimer_ = 0.0f;
			currentStage_ = 0;
		}
		break;

	case TitleState::Zooming:
		zoomTimer_ += 1.0f / 60.0f;
		{
			float t = std::min(zoomTimer_ / kZoomDuration_, 1.0f);
			targetAngle = setting.angle;
			targetDist = setting.distance;
			targetHeight = setting.height;
			targetLookAt = stagePos + setting.lookAtOffset;
			if (t >= 1.0f)
				state_ = TitleState::SelectStage;
		}
		break;

	case TitleState::SelectStage:
		// ステージ選択ロジック
		bool moved = false;

		// Dキー または 十字キー右 (ステージ番号を戻す/右回転)
		if (WasPressed_('D') || WasPadPressed(XINPUT_GAMEPAD_DPAD_RIGHT)) {
			currentStage_ = (currentStage_ - 1 + kStageCount) % kStageCount;
			moved = true; // 右に移動した
		}
		// Aキー または 十字キー左 (ステージ番号を進める/左回転)
		if (WasPressed_('A') || WasPadPressed(XINPUT_GAMEPAD_DPAD_LEFT)) {
			currentStage_ = (currentStage_ + 1) % kStageCount;
			moved = true; // 左に移動した
		}

		// ★修正: 移動していれば「選択音」を再生
		if (moved) {
			Engine::Audio::GetInstance()->Play(seSelectHandle_);
		}

		targetAngle = setting.angle;
		targetDist = setting.distance;
		targetHeight = setting.height;
		targetLookAt = stagePos + setting.lookAtOffset;

		// 決定操作
		if (WasPressed_(VK_RETURN) || WasPressed_(VK_SPACE) || WasPadPressed(XINPUT_GAMEPAD_A)) {

			// ★修正: 決定音を再生 (SelectStage -> GameScene)
			Engine::Audio::GetInstance()->Play(seDecisionHandle_);

			// ★追加: ゲームへ行く前にBGMを止める
			Engine::Audio::GetInstance()->Stop(bgmVoiceHandle_);

			SelectedStageIndex = currentStage_;
			end_ = true;
			next_ = "Game";
		}
		break;
	}

	// カメラ補間処理
	float smooth = (state_ == TitleState::WaitInput) ? 1.0f : ((state_ == TitleState::Zooming) ? 0.05f : 0.08f);
	float diff = targetAngle - currentViewAngle_;
	while (diff <= -XM_PI)
		diff += XM_2PI;
	while (diff > XM_PI)
		diff -= XM_2PI;
	currentViewAngle_ += diff * smooth;
	currentViewHeight_ = Lerp(currentViewHeight_, targetHeight, smooth);
	currentViewDist_ = Lerp(currentViewDist_, targetDist, smooth);
	currentLookAt_ = LerpVec3(currentLookAt_, targetLookAt, smooth);

	// カメラ座標の計算と設定
	Engine::Vector3 camPos = {std::sin(currentViewAngle_) * currentViewDist_, currentViewHeight_, std::cos(currentViewAngle_) * currentViewDist_};
	camera_.SetPosition(camPos.x, camPos.y, camPos.z);
	Engine::Vector3 dir = {currentLookAt_.x - camPos.x, currentLookAt_.y - camPos.y, currentLookAt_.z - camPos.z};
	camera_.SetRotation(std::atan2(-dir.y, std::sqrt(dir.x * dir.x + dir.z * dir.z)), std::atan2(dir.x, dir.z), 0.0f);

	// 雲の移動更新
	for (auto& cloud : clouds_) {
		cloud.transform.translate.x += cloud.speed;
		if (cloud.transform.translate.x > 45.0f)
			cloud.transform.translate.x = -45.0f;
	}

	// 魚パーティクルの移動更新 (WaitInput時のみ)
	if (state_ == TitleState::WaitInput) {
		for (auto& p : particles_) {
			p.transform.translate.y += p.velocity.y;
			p.animTime += 1.0f / 60.0f;

			float sway = std::sin(p.animTime * p.swaySpeed + p.swayPhase);
			p.transform.translate.x += sway * 0.02f;
			p.transform.rotate.y += 0.01f;
			p.transform.rotate.z = sway * 0.3f;

			if (p.transform.translate.y < -5.0f)
				p.transform.translate.y = 30.0f;
		}
	}
	pressTimer_ += 1.0f / 60.0f;

	// 選択中のステージマーカーのアニメーション
	if (state_ == TitleState::SelectStage) {
		for (int i = 0; i < kStageCount; ++i) {
			float baseScale = 0.6f;
			if (i == currentStage_) {
				// 選択中は少し大きくして回転
				stageTransforms_[i].scale = {0.8f, 0.8f, 0.8f};
				stageTransforms_[i].rotate.y += 0.02f;
			} else {
				stageTransforms_[i].scale = {baseScale, baseScale, baseScale};
			}
		}
	}
}

void TitleScene::Draw() {
	renderer_->SetCamera(camera_);

	renderer_->DrawMesh(planeMesh_, planeTex_, planeTransform_, {0.55f, 0.40f, 0.25f, 1.0f});
	for (const auto& m : bgMountains_)
		renderer_->DrawMesh(peakMesh_, peakTex_, m.transform, {0.6f, 0.6f, 0.75f, 1.0f});
	renderer_->DrawMesh(peakMesh_, peakTex_, peakTransform_, {1.0f, 1.0f, 1.0f, 1.0f});

	// タイトルロゴ (セレクト画面では非表示)
	if (state_ != TitleState::SelectStage) {
		Engine::Vector3 camPos = camera_.GetPosition();
		Engine::Vector3 toTitle = {titleTransform_.translate.x - camPos.x, titleTransform_.translate.y - camPos.y, titleTransform_.translate.z - camPos.z};
		float len = std::sqrt(toTitle.x * toTitle.x + toTitle.y * toTitle.y + toTitle.z * toTitle.z);
		if (len > 0.0001f) {
			toTitle.x /= len;
			toTitle.y /= len;
			toTitle.z /= len;
		}

		Engine::Transform futiTrans = titleTransform_;
		futiTrans.translate.x += toTitle.x * 0.1f;
		futiTrans.translate.y += toTitle.y * 0.1f;
		futiTrans.translate.z += toTitle.z * 0.1f;

		renderer_->DrawMesh(titleFutiMesh_, titleFutiTex_, futiTrans, {1.0f, 1.0f, 1.0f, 1.0f});
		renderer_->DrawMesh(titleBodyMesh_, titleBodyTex_, titleTransform_, {1.0f, 1.0f, 1.0f, 1.0f});
	}

	// ステージマーカー描画
	if (state_ != TitleState::WaitInput) {
		for (int i = 0; i < kStageCount; ++i) {
			Engine::Vector4 color;
			if (i == currentStage_) {
				color = {1.0f, 0.5f, 0.5f, 1.0f}; // 選択中：明るい赤
			} else {
				color = {1.0f, 0.2f, 0.2f, 1.0f}; // 非選択：暗い赤
			}
			renderer_->DrawMesh(stageMeshes_[i], planeTex_, stageTransforms_[i], color);
		}
	}

	for (const auto& cloud : clouds_)
		renderer_->DrawMesh(cloudMesh_, cloudTex_, cloud.transform, {1.0f, 1.0f, 1.0f, 0.9f});
	for (const auto& tree : trees_)
		renderer_->DrawMesh(treeMesh_, treeTex_, tree.transform, {1.0f, 1.0f, 1.0f, 1.0f});

	// 魚描画
	if (state_ == TitleState::WaitInput && fishMesh_ != 0) {
		Engine::Model* model = renderer_->GetModel(fishMesh_);

		if (model && !model->GetData().animations.empty()) {
			const auto& anim = model->GetData().animations[0];

			for (const auto& p : particles_) {
				float localTime = std::fmod(p.animTime * anim.ticksPerSecond, anim.duration);
				Engine::Matrix4x4 id = Engine::Matrix4x4::Identity();
				model->UpdateSkeleton(model->GetData().rootNode, id, anim, localTime, fishBonePalette_);
				renderer_->DrawSkinnedMesh(fishMesh_, planeTex_, p.transform, fishBonePalette_, p.color);
			}
		} else {
			for (const auto& p : particles_) {
				renderer_->DrawMesh(fishMesh_, planeTex_, p.transform, p.color);
			}
		}
	}

	// UI描画
	if (state_ == TitleState::WaitInput) {
		// 待機中: PRESS START
		if (pressTex_) {
			Engine::Renderer::SpriteDesc spr;
			spr.w = 600.0f;
			spr.h = 100.0f;
			spr.x = (Engine::WindowDX::kW - spr.w) * 0.5f;
			spr.y = (float)Engine::WindowDX::kH - 200.0f;
			spr.color = {1.0f, 1.0f, 1.0f, (0.6f + 0.4f * std::sin(pressTimer_ * 3.0f))};
			renderer_->DrawSprite(pressTex_, spr);
		}
	} else if (state_ == TitleState::SelectStage) {
		// セレクト中UI
		if (uiSelectMoveTex_) {
			Engine::Renderer::SpriteDesc spr;
			spr.w = 800.0f;
			spr.h = 100.0f;
			spr.x = (Engine::WindowDX::kW - spr.w) * 0.5f;
			spr.y = (float)Engine::WindowDX::kH - 250.0f;
			spr.color = {1.0f, 1.0f, 1.0f, 1.0f};
			renderer_->DrawSprite(uiSelectMoveTex_, spr);
		}
		if (uiSelectDecideTex_) {
			Engine::Renderer::SpriteDesc spr;
			spr.w = 800.0f;
			spr.h = 100.0f;
			spr.x = (Engine::WindowDX::kW - spr.w) * 0.5f;
			spr.y = (float)Engine::WindowDX::kH - 150.0f;
			spr.color = {1.0f, 1.0f, 1.0f, 1.0f};
			renderer_->DrawSprite(uiSelectDecideTex_, spr);
		}
	}
}

} // namespace Game