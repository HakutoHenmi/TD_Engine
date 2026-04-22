#include "TitleManagerScript.h"
#include "ScriptEngine.h"
#include "../../Engine/SceneManager.h"
#include "../../Engine/Input.h"
#include "../../Engine/Audio.h"
#include "../../Engine/Renderer.h"
#include "../../Engine/WindowDX.h"
#include "../Editor/EditorUI.h"

namespace Game {

void TitleManagerScript::Start(entt::entity entity, GameScene* scene) {
	(void)entity;
	if (!scene) return;
	auto& reg = scene->GetRegistry();

	// "TitleManager" エンティティ自身がアタッチされたことを前提に
	// ボタンエンティティを名前で検索
	btnStart_    = scene->FindObjectByName("Btn_Start");
	btnSettings_ = scene->FindObjectByName("Btn_Settings");
	btnExit_     = scene->FindObjectByName("Btn_Exit");

	btnFullscreen_ = scene->FindObjectByName("Btn_Fullscreen");
	btnBGMMinus_   = scene->FindObjectByName("Btn_BGMMinus");
	btnBGMPlus_    = scene->FindObjectByName("Btn_BGMPlus");
	btnSEMinus_    = scene->FindObjectByName("Btn_SEMinus");
	btnSEPlus_     = scene->FindObjectByName("Btn_SEPlus");
	btnBack_       = scene->FindObjectByName("Btn_Back");
	textFullscreen_ = scene->FindObjectByName("Text_Fullscreen");
	textBGM_       = scene->FindObjectByName("Text_BGM");
	textSE_        = scene->FindObjectByName("Text_SE");

	auto mainParent = scene->FindObjectByName("MainMenuParent");
	auto settingsParent = scene->FindObjectByName("SettingsMenuParent");

	// UIがまだ存在しない場合（初回起動 or JSONが空の場合）、フォールバックで自動生成
	if (btnStart_ == entt::null) {
		CreateFallbackUI(scene);
		uiInitialized_ = true;
	} else {
		uiInitialized_ = true;
	}

	mainEntities_.clear();
	if (mainParent != entt::null) mainEntities_.push_back(mainParent);
	
	settingsEntities_.clear();
	if (settingsParent != entt::null) settingsEntities_.push_back(settingsParent);

	// 初期状態: メインメニュー表示
	state_ = MenuState::Main;
	if (mainParent != entt::null && reg.valid(mainParent) && reg.all_of<RectTransformComponent>(mainParent))
		reg.get<RectTransformComponent>(mainParent).enabled = true;
	if (settingsParent != entt::null && reg.valid(settingsParent) && reg.all_of<RectTransformComponent>(settingsParent))
		reg.get<RectTransformComponent>(settingsParent).enabled = false;
}

void TitleManagerScript::Update(entt::entity entity, GameScene* scene, float dt) {
	(void)entity;
	(void)dt;
	if (!scene || !uiInitialized_) return;
	auto& reg = scene->GetRegistry();
	auto* input = Engine::Input::GetInstance();
	if (!input) return;

	bool isClicked = input->IsMouseTrigger(0);

	if (state_ == MenuState::Main) {
		if (isClicked) {
			if (btnStart_ != entt::null && reg.valid(btnStart_) && reg.all_of<UIButtonComponent>(btnStart_) &&
				reg.get<UIButtonComponent>(btnStart_).isHovered) {
				Engine::SceneParameters p;
				p.sceneName = "Select";
				Engine::SceneManager::GetInstance()->RequestChange("Select", p);
			} else if (btnSettings_ != entt::null && reg.valid(btnSettings_) && reg.all_of<UIButtonComponent>(btnSettings_) &&
				reg.get<UIButtonComponent>(btnSettings_).isHovered) {
				state_ = MenuState::Settings;
				if (!mainEntities_.empty() && reg.valid(mainEntities_[0]) && reg.all_of<RectTransformComponent>(mainEntities_[0]))
					reg.get<RectTransformComponent>(mainEntities_[0]).enabled = false;
				if (!settingsEntities_.empty() && reg.valid(settingsEntities_[0]) && reg.all_of<RectTransformComponent>(settingsEntities_[0]))
					reg.get<RectTransformComponent>(settingsEntities_[0]).enabled = true;
			} else if (btnExit_ != entt::null && reg.valid(btnExit_) && reg.all_of<UIButtonComponent>(btnExit_) &&
				reg.get<UIButtonComponent>(btnExit_).isHovered) {
				PostQuitMessage(0);
			}
		}
	} else if (state_ == MenuState::Settings) {
		auto* audio = Engine::Audio::GetInstance();

		// テキスト更新
		if (textFullscreen_ != entt::null && reg.valid(textFullscreen_) && reg.all_of<UITextComponent>(textFullscreen_)) {
			bool isFS = false;
			// WindowDXへのアクセスはRendererから間接的に取得するか、直接取得
			reg.get<UITextComponent>(textFullscreen_).text = isFS ? "Fullscreen: ON" : "Fullscreen: OFF";
		}
		if (audio && textBGM_ != entt::null && reg.valid(textBGM_) && reg.all_of<UITextComponent>(textBGM_)) {
			int bgmVol = static_cast<int>(audio->GetMasterBGMVolume() * 100);
			reg.get<UITextComponent>(textBGM_).text = "BGM Volume: " + std::to_string(bgmVol) + "%";
		}
		if (audio && textSE_ != entt::null && reg.valid(textSE_) && reg.all_of<UITextComponent>(textSE_)) {
			int seVol = static_cast<int>(audio->GetMasterSEVolume() * 100);
			reg.get<UITextComponent>(textSE_).text = "SE Volume: " + std::to_string(seVol) + "%";
		}

		if (isClicked) {
			if (btnBack_ != entt::null && reg.valid(btnBack_) && reg.all_of<UIButtonComponent>(btnBack_) &&
				reg.get<UIButtonComponent>(btnBack_).isHovered) {
				state_ = MenuState::Main;
				if (!mainEntities_.empty() && reg.valid(mainEntities_[0]) && reg.all_of<RectTransformComponent>(mainEntities_[0]))
					reg.get<RectTransformComponent>(mainEntities_[0]).enabled = true;
				if (!settingsEntities_.empty() && reg.valid(settingsEntities_[0]) && reg.all_of<RectTransformComponent>(settingsEntities_[0]))
					reg.get<RectTransformComponent>(settingsEntities_[0]).enabled = false;
			} else if (btnFullscreen_ != entt::null && reg.valid(btnFullscreen_) && reg.all_of<UIButtonComponent>(btnFullscreen_) &&
				reg.get<UIButtonComponent>(btnFullscreen_).isHovered) {
				// フルスクリーン切り替え (WindowDXへのアクセスが必要)
			} else if (audio) {
				if (btnBGMMinus_ != entt::null && reg.valid(btnBGMMinus_) && reg.all_of<UIButtonComponent>(btnBGMMinus_) &&
					reg.get<UIButtonComponent>(btnBGMMinus_).isHovered) {
					audio->SetMasterBGMVolume(audio->GetMasterBGMVolume() - 0.1f);
				} else if (btnBGMPlus_ != entt::null && reg.valid(btnBGMPlus_) && reg.all_of<UIButtonComponent>(btnBGMPlus_) &&
					reg.get<UIButtonComponent>(btnBGMPlus_).isHovered) {
					audio->SetMasterBGMVolume(audio->GetMasterBGMVolume() + 0.1f);
				} else if (btnSEMinus_ != entt::null && reg.valid(btnSEMinus_) && reg.all_of<UIButtonComponent>(btnSEMinus_) &&
					reg.get<UIButtonComponent>(btnSEMinus_).isHovered) {
					audio->SetMasterSEVolume(audio->GetMasterSEVolume() - 0.1f);
				} else if (btnSEPlus_ != entt::null && reg.valid(btnSEPlus_) && reg.all_of<UIButtonComponent>(btnSEPlus_) &&
					reg.get<UIButtonComponent>(btnSEPlus_).isHovered) {
					audio->SetMasterSEVolume(audio->GetMasterSEVolume() + 0.1f);
				}
			}
		}
	}
}

void TitleManagerScript::DrawUI(entt::entity entity, GameScene* scene) {
	(void)entity;
	(void)scene;
	// UISystemが描画を担当するため、ここでは何もしない
}

void TitleManagerScript::OnEditorUI() {
	// エディタUI（パラメータ調整など）が必要な場合にここに追加
}

std::string TitleManagerScript::SerializeParameters() { return ""; }
void TitleManagerScript::DeserializeParameters(const std::string& data) { (void)data; }

// =============================================
// フォールバック: UIが存在しない場合に自動生成
// =============================================

entt::entity TitleManagerScript::CreateTitleButton(entt::registry& reg, const std::string& text, float yPos, entt::entity parent) {
	auto entity = reg.create();
	auto& rect = reg.emplace<RectTransformComponent>(entity);
	rect.pos = {0.0f, yPos};
	rect.size = {300.0f, 60.0f};
	rect.anchor = {0.0f, 0.0f};
	rect.pivot = {0.0f, 0.5f};
	rect.enabled = true;

	if (parent != entt::null) {
		reg.emplace<HierarchyComponent>(entity, parent);
	}

	auto& btn = reg.emplace<UIButtonComponent>(entity);
	btn.normalColor = {0.2f, 0.2f, 0.2f, 0.8f};
	btn.hoverColor = {0.4f, 0.4f, 0.4f, 1.0f};
	btn.pressedColor = {0.1f, 0.1f, 0.1f, 1.0f};

	auto& txt = reg.emplace<UITextComponent>(entity);
	txt.text = text;
	txt.fontSize = 32.0f;
	txt.color = {1.0f, 1.0f, 1.0f, 1.0f};

	auto& img = reg.emplace<UIImageComponent>(entity);
	img.color = {1.0f, 1.0f, 1.0f, 1.0f};

	return entity;
}

void TitleManagerScript::CreateFallbackUI(GameScene* scene) {
	auto& reg = scene->GetRegistry();

	// メインメニュー
	auto mainParent = reg.create();
	reg.emplace<NameComponent>(mainParent, "MainMenuParent");
	auto& pRect = reg.emplace<RectTransformComponent>(mainParent);
	pRect.pos = {100.0f, 300.0f};
	pRect.size = {0, 0};
	pRect.anchor = {0.0f, 0.0f};

	// タイトルテキスト
	auto titleText = reg.create();
	reg.emplace<NameComponent>(titleText, "TitleText");
	auto& titleRect = reg.emplace<RectTransformComponent>(titleText);
	titleRect.pos = {50.0f, -150.0f};
	reg.emplace<HierarchyComponent>(titleText, mainParent);
	auto& txt = reg.emplace<UITextComponent>(titleText);
	txt.text = "Engine Project"; // ★文字変更
	txt.fontSize = 80.0f;
	txt.color = {1.0f, 0.8f, 0.2f, 1.0f};

	// ボタン生成
	auto btnStart_ = CreateTitleButton(reg, "Start Game", 0.0f, mainParent);
	reg.emplace<NameComponent>(btnStart_, "Btn_Start");

	auto btnSettings_ = CreateTitleButton(reg, "Settings", 80.0f, mainParent);
	reg.emplace<NameComponent>(btnSettings_, "Btn_Settings");

	auto btnExit_ = CreateTitleButton(reg, "Exit", 160.0f, mainParent);
	reg.emplace<NameComponent>(btnExit_, "Btn_Exit");

	// 設定メニュー
	auto settingsParent = reg.create();
	reg.emplace<NameComponent>(settingsParent, "SettingsMenuParent");
	auto& spRect = reg.emplace<RectTransformComponent>(settingsParent);
	spRect.pos = {100.0f, 300.0f};
	spRect.size = {0, 0};
	spRect.anchor = {0.0f, 0.0f};
	spRect.enabled = false;

	// 設定タイトル
	auto settingsTitle = reg.create();
	reg.emplace<NameComponent>(settingsTitle, "SettingsTitle");
	auto& stRect = reg.emplace<RectTransformComponent>(settingsTitle);
	stRect.pos = {50.0f, -150.0f};
	reg.emplace<HierarchyComponent>(settingsTitle, settingsParent);
	auto& stTxt = reg.emplace<UITextComponent>(settingsTitle);
	stTxt.text = "Settings";
	stTxt.fontSize = 80.0f;

	// フルスクリーン
	auto btnFullscreen_ = CreateTitleButton(reg, "Fullscreen: OFF", 0.0f, settingsParent);
	reg.emplace<NameComponent>(btnFullscreen_, "Btn_Fullscreen");

	// BGM
	auto bgmLabel = reg.create();
	reg.emplace<NameComponent>(bgmLabel, "Text_BGM");
	auto& bgmRect = reg.emplace<RectTransformComponent>(bgmLabel);
	bgmRect.pos = {0.0f, 80.0f};
	reg.emplace<HierarchyComponent>(bgmLabel, settingsParent);
	auto& bgmTxt = reg.emplace<UITextComponent>(bgmLabel);
	bgmTxt.text = "BGM Volume";
	bgmTxt.fontSize = 32.0f;

	auto btnBGMMinus_ = CreateTitleButton(reg, "-", 80.0f, settingsParent);
	reg.emplace<NameComponent>(btnBGMMinus_, "Btn_BGMMinus");
	reg.get<RectTransformComponent>(btnBGMMinus_).size = {60.0f, 60.0f};
	reg.get<RectTransformComponent>(btnBGMMinus_).pos = {310.0f, 80.0f};

	auto btnBGMPlus_ = CreateTitleButton(reg, "+", 80.0f, settingsParent);
	reg.emplace<NameComponent>(btnBGMPlus_, "Btn_BGMPlus");
	reg.get<RectTransformComponent>(btnBGMPlus_).size = {60.0f, 60.0f};
	reg.get<RectTransformComponent>(btnBGMPlus_).pos = {380.0f, 80.0f};

	// SE
	auto seLabel = reg.create();
	reg.emplace<NameComponent>(seLabel, "Text_SE");
	auto& seRect = reg.emplace<RectTransformComponent>(seLabel);
	seRect.pos = {0.0f, 160.0f};
	reg.emplace<HierarchyComponent>(seLabel, settingsParent);
	auto& seTxt = reg.emplace<UITextComponent>(seLabel);
	seTxt.text = "SE Volume";
	seTxt.fontSize = 32.0f;

	auto btnSEMinus_ = CreateTitleButton(reg, "-", 160.0f, settingsParent);
	reg.emplace<NameComponent>(btnSEMinus_, "Btn_SEMinus");
	reg.get<RectTransformComponent>(btnSEMinus_).size = {60.0f, 60.0f};
	reg.get<RectTransformComponent>(btnSEMinus_).pos = {310.0f, 160.0f};

	auto btnSEPlus_ = CreateTitleButton(reg, "+", 160.0f, settingsParent);
	reg.emplace<NameComponent>(btnSEPlus_, "Btn_SEPlus");
	reg.get<RectTransformComponent>(btnSEPlus_).size = {60.0f, 60.0f};
	reg.get<RectTransformComponent>(btnSEPlus_).pos = {380.0f, 160.0f};

	// 戻るボタン
	auto btnBack_ = CreateTitleButton(reg, "Back", 260.0f, settingsParent);
	reg.emplace<NameComponent>(btnBack_, "Btn_Back");
}

REGISTER_SCRIPT(TitleManagerScript)

} // namespace Game
