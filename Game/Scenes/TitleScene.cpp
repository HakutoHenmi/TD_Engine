#include "TitleScene.h"
#include "../../Engine/SceneManager.h"
#include "../../Engine/Input.h"
#include "../../Engine/Audio.h"
#include "../Editor/EditorUI.h"
#include "imgui.h"

namespace Game {

void TitleScene::Initialize(Engine::WindowDX* dx, const Engine::SceneParameters& params) {
	(void)params;
	dx_ = dx;
	renderer_ = Engine::Renderer::GetInstance();

	camera_.Initialize();
	camera_.SetProjection(0.7854f, (float)Engine::WindowDX::kW / (float)Engine::WindowDX::kH, 0.1f, 1000.0f);

	uiSystem_ = std::make_unique<UISystem>();
	registry_.clear();
	mainEntities_.clear();
	settingsEntities_.clear();

	// コンテキストの初期化
	ctx_.dt = 1.0f / 60.0f;
	ctx_.camera = &camera_;
	ctx_.renderer = renderer_;
	ctx_.input = Engine::Input::GetInstance();
	ctx_.isPlaying = true;
	ctx_.scene = nullptr;
	ctx_.viewportOffset = {0.0f, 0.0f};
	ctx_.viewportSize = {(float)Engine::WindowDX::kW, (float)Engine::WindowDX::kH};

	CreateMainMenu();
	CreateSettingsMenu();

	// 初期状態はメインメニュー表示
	state_ = MenuState::Main;
	for (auto e : mainEntities_) registry_.get<RectTransformComponent>(e).enabled = true;
	for (auto e : settingsEntities_) registry_.get<RectTransformComponent>(e).enabled = false;
}

entt::entity TitleScene::CreateButton(const std::string& text, float yPos, entt::entity parent) {
	auto entity = registry_.create();
	
	// Transformer
	auto& rect = registry_.emplace<RectTransformComponent>(entity);
	rect.pos = {0.0f, yPos};
	rect.size = {300.0f, 60.0f};
	rect.anchor = {0.0f, 0.0f}; // 左端基準
	rect.pivot = {0.0f, 0.5f};
	rect.enabled = true;

	if (parent != entt::null) {
		registry_.emplace<HierarchyComponent>(entity, parent);
	}

	// Button
	auto& btn = registry_.emplace<UIButtonComponent>(entity);
	btn.normalColor = {0.1f, 0.1f, 0.12f, 0.95f};
	btn.hoverColor = {0.18f, 0.15f, 0.12f, 0.98f};
	btn.pressedColor = {0.1f, 0.1f, 0.15f, 1.0f};

	// Text
	auto& txt = registry_.emplace<UITextComponent>(entity);
	txt.text = text;
	txt.fontSize = 30.0f;
	txt.color = {0.9f, 0.9f, 0.9f, 1.0f};
	txt.fontPath = "Resources\\Fonts\\Kiwi_Maru\\KiwiMaru-Regular.ttf";
	txt.outlineEnabled = true;
	txt.outlineColor = {0.0f, 0.0f, 0.0f, 0.95f};
	txt.outlineThickness = 1.5f;

	// Image (Background)
	auto& img = registry_.emplace<UIImageComponent>(entity);
	img.color = {1.0f, 1.0f, 1.0f, 1.0f}; // Multiply with button color
	img.layer = 160;
	if (renderer_) img.textureHandle = renderer_->LoadTexture2D("Resources/Textures/white1x1.png");

	return entity;
}

void TitleScene::CreateMainMenu() {
	auto parent = registry_.create();
	auto& pRect = registry_.emplace<RectTransformComponent>(parent);
	pRect.pos = {100.0f, 300.0f}; // 左寄せで下げた位置
	pRect.size = {0,0};
	pRect.anchor = {0.0f, 0.0f};
	mainEntities_.push_back(parent);

	// タイトルテキスト
	auto titleText = registry_.create();
	auto& titleRect = registry_.emplace<RectTransformComponent>(titleText);
	titleRect.pos = {50.0f, -150.0f}; // タイトルは上部に配置
	registry_.emplace<HierarchyComponent>(titleText, parent);
	auto& txt = registry_.emplace<UITextComponent>(titleText);
	txt.text = "TD Engine Project";
	txt.fontSize = 80.0f;
	txt.color = {1.0f, 0.8f, 0.2f, 1.0f};
	mainEntities_.push_back(titleText);

	btnStart_ = CreateButton("Start Game", 0.0f, parent);
	btnSettings_ = CreateButton("Settings", 80.0f, parent);
	btnExit_ = CreateButton("Exit", 160.0f, parent);

	mainEntities_.push_back(btnStart_);
	mainEntities_.push_back(btnSettings_);
	mainEntities_.push_back(btnExit_);
}

void TitleScene::CreateSettingsMenu() {
	auto parent = registry_.create();
	auto& pRect = registry_.emplace<RectTransformComponent>(parent);
	pRect.pos = {0.0f, 0.0f};
	pRect.size = {0, 0};
	pRect.anchor = {0.0f, 0.0f};
	pRect.pivot = {0.0f, 0.0f};
	pRect.enabled = false;
	settingsEntities_.push_back(parent);

	// 1. 全面半透明オーバーレイ
	auto overlay = registry_.create();
	auto& oRect = registry_.emplace<RectTransformComponent>(overlay);
	oRect.pos = {0.0f, 0.0f};
	oRect.size = {1920.0f, 1080.0f};
	oRect.anchor = {0.0f, 0.0f};
	oRect.pivot = {0.0f, 0.0f};
	oRect.enabled = false;
	registry_.emplace<HierarchyComponent>(overlay, parent);
	auto& oImg = registry_.emplace<UIImageComponent>(overlay);
	oImg.color = {0.05f, 0.05f, 0.07f, 0.85f};
	oImg.layer = 150;
	if (renderer_) oImg.textureHandle = renderer_->LoadTexture2D("Resources/Textures/white1x1.png");
	settingsEntities_.push_back(overlay);

	// 2. 中央の掲示板外枠（真鍮ゴールド）
	auto border = registry_.create();
	auto& bRect = registry_.emplace<RectTransformComponent>(border);
	bRect.pos = {460.0f, 160.0f};
	bRect.size = {1000.0f, 760.0f};
	bRect.anchor = {0.0f, 0.0f};
	bRect.pivot = {0.0f, 0.0f};
	bRect.enabled = false;
	registry_.emplace<HierarchyComponent>(border, parent);
	auto& bImg = registry_.emplace<UIImageComponent>(border);
	bImg.color = {0.8f, 0.6f, 0.25f, 0.9f};
	bImg.layer = 151;
	if (renderer_) bImg.textureHandle = renderer_->LoadTexture2D("Resources/Textures/white1x1.png");
	settingsEntities_.push_back(border);

	// 3. 中央の掲示板内側（ダークグレー）
	auto board = registry_.create();
	auto& boardRect = registry_.emplace<RectTransformComponent>(board);
	boardRect.pos = {464.0f, 164.0f};
	boardRect.size = {992.0f, 752.0f};
	boardRect.anchor = {0.0f, 0.0f};
	boardRect.pivot = {0.0f, 0.0f};
	boardRect.enabled = false;
	registry_.emplace<HierarchyComponent>(board, parent);
	auto& boardImg = registry_.emplace<UIImageComponent>(board);
	boardImg.color = {0.08f, 0.08f, 0.1f, 0.95f};
	boardImg.layer = 152;
	if (renderer_) boardImg.textureHandle = renderer_->LoadTexture2D("Resources/Textures/white1x1.png");
	settingsEntities_.push_back(board);

	// タイトルテキスト
	auto titleText = registry_.create();
	auto& titleRect = registry_.emplace<RectTransformComponent>(titleText);
	titleRect.pos = {640.0f, 220.0f};
	titleRect.anchor = {0.0f, 0.0f};
	titleRect.pivot = {0.0f, 0.0f};
	titleRect.enabled = false;
	registry_.emplace<HierarchyComponent>(titleText, parent);
	auto& txt = registry_.emplace<UITextComponent>(titleText);
	txt.text = reinterpret_cast<const char*>(u8"\u3010 Settings / \u8a2d\u5b9a \u3011");
	txt.fontSize = 54.0f;
	txt.color = {1.0f, 0.9f, 0.4f, 1.0f};
	txt.fontPath = "Resources\\Fonts\\Kiwi_Maru\\KiwiMaru-Regular.ttf";
	txt.outlineEnabled = true;
	txt.outlineColor = {0.0f, 0.0f, 0.0f, 0.95f};
	txt.outlineThickness = 1.5f;
	settingsEntities_.push_back(titleText);

	// 区切り線
	auto divider = registry_.create();
	auto& dRect = registry_.emplace<RectTransformComponent>(divider);
	dRect.pos = {510.0f, 300.0f};
	dRect.size = {900.0f, 2.0f};
	dRect.anchor = {0.0f, 0.0f};
	dRect.pivot = {0.0f, 0.0f};
	dRect.enabled = false;
	registry_.emplace<HierarchyComponent>(divider, parent);
	auto& dImg = registry_.emplace<UIImageComponent>(divider);
	dImg.color = {0.8f, 0.6f, 0.25f, 0.5f};
	dImg.layer = 153;
	if (renderer_) dImg.textureHandle = renderer_->LoadTexture2D("Resources/Textures/white1x1.png");
	settingsEntities_.push_back(divider);

	// BGM 音量ラベル
	auto bgmLabel = registry_.create();
	auto& bgmRect = registry_.emplace<RectTransformComponent>(bgmLabel);
	bgmRect.pos = {560.0f, 380.0f};
	bgmRect.anchor = {0.0f, 0.0f};
	bgmRect.pivot = {0.0f, 0.0f};
	bgmRect.enabled = false;
	registry_.emplace<HierarchyComponent>(bgmLabel, parent);
	auto& bgmTxt = registry_.emplace<UITextComponent>(bgmLabel);
	bgmTxt.text = "BGM Volume";
	bgmTxt.fontSize = 40.0f;
	bgmTxt.color = {0.9f, 0.9f, 0.9f, 1.0f};
	bgmTxt.fontPath = "Resources\\Fonts\\Kiwi_Maru\\KiwiMaru-Regular.ttf";
	bgmTxt.outlineEnabled = true;
	bgmTxt.outlineColor = {0.0f, 0.0f, 0.0f, 0.95f};
	bgmTxt.outlineThickness = 1.5f;
	textBGM_ = bgmLabel;
	settingsEntities_.push_back(bgmLabel);

	btnBGMMinus_ = CreateButton("-", 380.0f, parent);
	registry_.get<RectTransformComponent>(btnBGMMinus_).size = {70.0f, 70.0f};
	registry_.get<RectTransformComponent>(btnBGMMinus_).pos = {1150.0f, 370.0f};
	settingsEntities_.push_back(btnBGMMinus_);
	
	btnBGMPlus_ = CreateButton("+", 380.0f, parent);
	registry_.get<RectTransformComponent>(btnBGMPlus_).size = {70.0f, 70.0f};
	registry_.get<RectTransformComponent>(btnBGMPlus_).pos = {1250.0f, 370.0f};
	settingsEntities_.push_back(btnBGMPlus_);

	// SE 音量ラベル
	auto seLabel = registry_.create();
	auto& seRect = registry_.emplace<RectTransformComponent>(seLabel);
	seRect.pos = {560.0f, 480.0f};
	seRect.anchor = {0.0f, 0.0f};
	seRect.pivot = {0.0f, 0.0f};
	seRect.enabled = false;
	registry_.emplace<HierarchyComponent>(seLabel, parent);
	auto& seTxt = registry_.emplace<UITextComponent>(seLabel);
	seTxt.text = "SE Volume";
	seTxt.fontSize = 40.0f;
	seTxt.color = {0.9f, 0.9f, 0.9f, 1.0f};
	seTxt.fontPath = "Resources\\Fonts\\Kiwi_Maru\\KiwiMaru-Regular.ttf";
	seTxt.outlineEnabled = true;
	seTxt.outlineColor = {0.0f, 0.0f, 0.0f, 0.95f};
	seTxt.outlineThickness = 1.5f;
	textSE_ = seLabel;
	settingsEntities_.push_back(seLabel);

	btnSEMinus_ = CreateButton("-", 480.0f, parent);
	registry_.get<RectTransformComponent>(btnSEMinus_).size = {70.0f, 70.0f};
	registry_.get<RectTransformComponent>(btnSEMinus_).pos = {1150.0f, 470.0f};
	settingsEntities_.push_back(btnSEMinus_);
	
	btnSEPlus_ = CreateButton("+", 480.0f, parent);
	registry_.get<RectTransformComponent>(btnSEPlus_).size = {70.0f, 70.0f};
	registry_.get<RectTransformComponent>(btnSEPlus_).pos = {1250.0f, 470.0f};
	settingsEntities_.push_back(btnSEPlus_);

	// フルスクリーン
	btnFullscreen_ = CreateButton("Fullscreen: OFF", 600.0f, parent);
	registry_.get<RectTransformComponent>(btnFullscreen_).size = {400.0f, 70.0f};
	registry_.get<RectTransformComponent>(btnFullscreen_).pos = {760.0f, 600.0f};
	textFullscreen_ = btnFullscreen_;
	settingsEntities_.push_back(btnFullscreen_);

	btnBack_ = CreateButton("Back / " + std::string(reinterpret_cast<const char*>(u8"\u623B\u308B")), 780.0f, parent);
	registry_.get<RectTransformComponent>(btnBack_).size = {360.0f, 70.0f};
	registry_.get<RectTransformComponent>(btnBack_).pos = {780.0f, 780.0f};
	{
		auto& btn = registry_.get<UIButtonComponent>(btnBack_);
		btn.normalColor = {0.8f, 0.6f, 0.25f, 0.9f}; // ゴールド色
		btn.hoverColor = {1.0f, 0.85f, 0.3f, 1.0f};
		btn.pressedColor = {0.6f, 0.4f, 0.15f, 1.0f};
	}
	settingsEntities_.push_back(btnBack_);
}

void TitleScene::Update() {
	// UIシステムの更新（主にボタン押下フラグの更新）
	uiSystem_->Draw(registry_, ctx_); 
	
	auto* input = Engine::Input::GetInstance();
	bool isClicked = input->IsMouseTrigger(0);

	if (state_ == MenuState::Main) {
		if (isClicked) {
			if (registry_.get<UIButtonComponent>(btnStart_).isHovered) {
				Engine::SceneManager::GetInstance()->RequestChange("Select");
			} else if (registry_.get<UIButtonComponent>(btnSettings_).isHovered) {
				state_ = MenuState::Settings;
				for (auto e : mainEntities_) registry_.get<RectTransformComponent>(e).enabled = false;
				for (auto e : settingsEntities_) registry_.get<RectTransformComponent>(e).enabled = true;
			} else if (registry_.get<UIButtonComponent>(btnExit_).isHovered) {
				PostQuitMessage(0);
			}
		}
	} else if (state_ == MenuState::Settings) {
		auto* audio = Engine::Audio::GetInstance();
		
		// フルスクリーンボタンテキストの更新
		if (dx_) {
			std::string fsText = dx_->IsFullscreen() ? "Fullscreen: ON" : "Fullscreen: OFF";
			registry_.get<UITextComponent>(textFullscreen_).text = fsText;
		}

		// 音量テキストの更新
		if (audio) {
			int bgmVol = static_cast<int>(std::round(audio->GetMasterBGMVolume() * 100.0f));
			registry_.get<UITextComponent>(textBGM_).text = "BGM Volume: " + std::to_string(bgmVol) + "%";
			
			int seVol = static_cast<int>(std::round(audio->GetMasterSEVolume() * 100.0f));
			registry_.get<UITextComponent>(textSE_).text = "SE Volume: " + std::to_string(seVol) + "%";
		}

		if (isClicked) {
			if (registry_.get<UIButtonComponent>(btnBack_).isHovered) {
				state_ = MenuState::Main;
				for (auto e : mainEntities_) registry_.get<RectTransformComponent>(e).enabled = true;
				for (auto e : settingsEntities_) registry_.get<RectTransformComponent>(e).enabled = false;
			} else if (registry_.get<UIButtonComponent>(btnFullscreen_).isHovered) {
				if (dx_) dx_->ToggleFullscreen();
			} else if (registry_.get<UIButtonComponent>(btnBGMMinus_).isHovered) {
				if (audio) audio->SetMasterBGMVolume(audio->GetMasterBGMVolume() - 0.1f);
			} else if (registry_.get<UIButtonComponent>(btnBGMPlus_).isHovered) {
				if (audio) audio->SetMasterBGMVolume(audio->GetMasterBGMVolume() + 0.1f);
			} else if (registry_.get<UIButtonComponent>(btnSEMinus_).isHovered) {
				if (audio) audio->SetMasterSEVolume(audio->GetMasterSEVolume() - 0.1f);
			} else if (registry_.get<UIButtonComponent>(btnSEPlus_).isHovered) {
				if (audio) audio->SetMasterSEVolume(audio->GetMasterSEVolume() + 0.1f);
			}
		}
	}
}

void TitleScene::Draw() {
	if (renderer_) {
		renderer_->ResetGameViewport();
		// ★追加: ポストエフェクトを無効化
		renderer_->SetPostProcessEnabled(false);
	}
}

void TitleScene::DrawEditor() {
}

} // namespace Game
