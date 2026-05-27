#include "TitleManagerScript.h"
#include "ScriptEngine.h"
#include "../../Engine/SceneManager.h"
#include "../../Engine/Input.h"
#include "../../Engine/Audio.h"
#include "../../Engine/Renderer.h"
#include "../../Engine/WindowDX.h"
#include "../Editor/EditorUI.h"
#include <cmath>

namespace Game {

void TitleManagerScript::Start(entt::entity entity, GameScene* scene) {
	(void)entity;
	if (!scene) return;
	auto& reg = scene->GetRegistry();

	// "TitleManager" エンティティ自身がアタッチされたことを前提に
	// ボタンエンティティを名前で検索
	btnStart_    = scene->FindObjectByName("Btn_Start");
	btnSettings_ = scene->FindObjectByName("Btn_Settings");
	btnCredits_  = scene->FindObjectByName("Btn_Credits");
	btnExit_     = scene->FindObjectByName("Btn_Exit");

	btnFullscreen_ = scene->FindObjectByName("Btn_Fullscreen");
	btnBGMMinus_   = scene->FindObjectByName("Btn_BGMMinus");
	btnBGMPlus_    = scene->FindObjectByName("Btn_BGMPlus");
	btnSEMinus_    = scene->FindObjectByName("Btn_SEMinus");
	btnSEPlus_     = scene->FindObjectByName("Btn_SEPlus");
	btnBack_       = scene->FindObjectByName("Btn_Back");
	textFullscreen_ = scene->FindObjectByName("Text_Fullscreen");
	if (textFullscreen_ == entt::null) {
		textFullscreen_ = btnFullscreen_; // ボタン自体にテキストコンポーネントがある場合のフォールバック
	}
	textBGM_       = scene->FindObjectByName("Text_BGM");
	textSE_        = scene->FindObjectByName("Text_SE");

	auto mainParent = scene->FindObjectByName("MainMenuParent");
	auto settingsParent = scene->FindObjectByName("SettingsMenuParent");
	auto creditsParent = scene->FindObjectByName("CreditsMenuParent");
	btnCreditsBack_ = scene->FindObjectByName("Btn_CreditsBack");

	// UIがまだ存在しない場合（初回起動 or JSONが空の場合）、フォールバックで自動生成
	if (btnStart_ == entt::null) {
		CreateFallbackUI(scene);
		// 生成後に再取得
		mainParent = scene->FindObjectByName("MainMenuParent");
		settingsParent = scene->FindObjectByName("SettingsMenuParent");
		creditsParent = scene->FindObjectByName("CreditsMenuParent");
		btnCreditsBack_ = scene->FindObjectByName("Btn_CreditsBack");
		btnCredits_ = scene->FindObjectByName("Btn_Credits");
		uiInitialized_ = true;
	} else {
		// btnStart_ はあるが btnCredits_ が無い場合（既存シーンのアップデート対応）
		if (btnCredits_ == entt::null && mainParent != entt::null) {
			btnCredits_ = CreateTitleButton(scene, reg, "クレジット", 160.0f, mainParent);
			reg.emplace<NameComponent>(btnCredits_, "Btn_Credits");
		}

		// UIの位置とテキストの修正（クレジットボタンをきれいに並べる）
		if (btnStart_ != entt::null && btnSettings_ != entt::null && btnCredits_ != entt::null && btnExit_ != entt::null) {
			if (reg.all_of<RectTransformComponent>(btnStart_) && reg.all_of<RectTransformComponent>(btnSettings_)) {
				auto& startRect = reg.get<RectTransformComponent>(btnStart_);
				auto& setRect = reg.get<RectTransformComponent>(btnSettings_);
				float spacing = setRect.pos.y - startRect.pos.y;

				if (reg.all_of<RectTransformComponent>(btnCredits_)) {
					auto& credRect = reg.get<RectTransformComponent>(btnCredits_);
					credRect.pos.x = setRect.pos.x;
					credRect.pos.y = setRect.pos.y + spacing;
					credRect.anchor = setRect.anchor;
					credRect.pivot = setRect.pivot;
					credRect.size = setRect.size;
				}

				if (reg.all_of<RectTransformComponent>(btnExit_)) {
					auto& exitRect = reg.get<RectTransformComponent>(btnExit_);
					exitRect.pos.x = setRect.pos.x;
					exitRect.pos.y = setRect.pos.y + spacing * 2.0f;
				}
			}

			// 余計なテキストコンポーネントがあれば無効化（画像UIのボタンの上に文字が重ならないようにする）
			if (reg.all_of<UITextComponent>(btnStart_)) reg.get<UITextComponent>(btnStart_).text = "";
			if (reg.all_of<UITextComponent>(btnSettings_)) reg.get<UITextComponent>(btnSettings_).text = "";
			if (reg.all_of<UITextComponent>(btnExit_)) reg.get<UITextComponent>(btnExit_).text = "";
			if (reg.all_of<UITextComponent>(btnCredits_)) reg.get<UITextComponent>(btnCredits_).text = "";

			// クレジットボタンの画像設定
			if (reg.all_of<UIImageComponent>(btnCredits_)) {
				auto& img = reg.get<UIImageComponent>(btnCredits_);
				img.texturePath = "Resources/Textures/Button/creditt.png";
				if (auto* renderer = Engine::Renderer::GetInstance()) {
					img.textureHandle = renderer->LoadTexture2D(img.texturePath);
				}
				img.color = {1.0f, 1.0f, 1.0f, 1.0f};
			}

			// 色味の調整（すべてのボタンを強制的に同じ明るさに統一する）
			std::vector<entt::entity> titleBtns = { btnStart_, btnSettings_, btnCredits_, btnExit_ };
			for (auto e : titleBtns) {
				if (e != entt::null) {
					if (reg.all_of<UIImageComponent>(e)) {
						reg.get<UIImageComponent>(e).color = {1.0f, 1.0f, 1.0f, 1.0f};
					}
					if (reg.all_of<UIButtonComponent>(e)) {
						auto& btn = reg.get<UIButtonComponent>(e);
						btn.normalColor = {1.0f, 1.0f, 1.0f, 1.0f};
						btn.hoverColor = {0.8f, 0.8f, 0.8f, 1.0f};
						btn.pressedColor = {0.5f, 0.5f, 0.5f, 1.0f};
					}
				}
			}
		}
		if (creditsParent == entt::null) {
			creditsParent = reg.create();
			reg.emplace<NameComponent>(creditsParent, "CreditsMenuParent");
			auto& crRect = reg.emplace<RectTransformComponent>(creditsParent);
			crRect.pos = {0.0f, 0.0f};
			crRect.size = {1920.0f, 1080.0f};
			crRect.anchor = {0.0f, 0.0f};
			crRect.pivot = {0.0f, 0.0f};
			crRect.enabled = false;

			auto creditsTitle = reg.create();
			reg.emplace<NameComponent>(creditsTitle, "CreditsTitle");
			auto& cstRect = reg.emplace<RectTransformComponent>(creditsTitle);
			cstRect.pos = {0.0f, 250.0f};
			cstRect.size = {1920.0f, 100.0f};
			cstRect.anchor = {0.0f, 0.0f};
			cstRect.pivot = {0.0f, 0.0f};
			scene->SetParent(creditsTitle, creditsParent);
			auto& cstTxt = reg.emplace<UITextComponent>(creditsTitle);
			cstTxt.text = "Special Thanks";
			cstTxt.fontSize = 64.0f;
			cstTxt.fontPath = "Resources\\\\Fonts\\\\Kiwi_Maru\\\\KiwiMaru-Regular.ttf";

			auto creditsText = reg.create();
			reg.emplace<NameComponent>(creditsText, "CreditsText");
			auto& ctxRect = reg.emplace<RectTransformComponent>(creditsText);
			ctxRect.pos = {0.0f, 400.0f};
			ctxRect.size = {1920.0f, 200.0f};
			ctxRect.anchor = {0.0f, 0.0f};
			ctxRect.pivot = {0.0f, 0.0f};
			scene->SetParent(creditsText, creditsParent);
			auto& ctxTxt = reg.emplace<UITextComponent>(creditsText);
			ctxTxt.text = "Steampunk Font\nCreated by hannarb";
			ctxTxt.fontSize = 48.0f;
			ctxTxt.fontPath = "Resources\\\\Fonts\\\\Kiwi_Maru\\\\KiwiMaru-Regular.ttf";

			btnCreditsBack_ = CreateTitleButton(scene, reg, "Back", 800.0f, creditsParent);
			reg.emplace<NameComponent>(btnCreditsBack_, "Btn_CreditsBack");
		}
		
		// 古いJSONからロードされた場合でも強制的に中央配置を適用する
		auto creditsTitle = scene->FindObjectByName("CreditsTitle");
		auto creditsText = scene->FindObjectByName("CreditsText");
		if (creditsParent != entt::null && reg.all_of<RectTransformComponent>(creditsParent)) {
			auto& r = reg.get<RectTransformComponent>(creditsParent);
			r.pos = {0.0f, 0.0f};
			r.size = {1920.0f, 1080.0f};
			r.anchor = {0.0f, 0.0f};
			r.pivot = {0.0f, 0.0f};
		}
		if (creditsTitle != entt::null && reg.all_of<RectTransformComponent>(creditsTitle)) {
			auto& r = reg.get<RectTransformComponent>(creditsTitle);
			r.pos = {0.0f, 250.0f};
			r.size = {1920.0f, 100.0f};
			r.anchor = {0.0f, 0.0f};
			r.pivot = {0.0f, 0.0f};
		}
		if (creditsText != entt::null && reg.all_of<RectTransformComponent>(creditsText)) {
			auto& r = reg.get<RectTransformComponent>(creditsText);
			r.pos = {0.0f, 400.0f};
			r.size = {1920.0f, 200.0f};
			r.anchor = {0.0f, 0.0f};
			r.pivot = {0.0f, 0.0f};
		}
		if (btnCreditsBack_ != entt::null && reg.all_of<RectTransformComponent>(btnCreditsBack_)) {
			auto& r = reg.get<RectTransformComponent>(btnCreditsBack_);
			r.pos.x = 960.0f;
			r.pos.y = 800.0f;
			r.anchor = {0.0f, 0.0f};
			r.pivot = {0.5f, 0.5f};
		}

		uiInitialized_ = true;
	}

	mainEntities_.clear();
	if (mainParent != entt::null) mainEntities_.push_back(mainParent);
	
	settingsEntities_.clear();
	if (settingsParent != entt::null) settingsEntities_.push_back(settingsParent);

	creditsEntities_.clear();
	if (creditsParent != entt::null) creditsEntities_.push_back(creditsParent);

	// ★追加: スチームパンク3Dオブジェクト（ギア・パイプ）の検索
	gears_.clear();
	pipes_.clear();
	titleTime_ = 0.0f;
	cameraOrbitAngle_ = 0.0f;

	// ギアを名前で検索（Gear_0〜Gear_9）し、異なる回転速度を設定
	const float gearSpeeds[] = { 0.8f, -1.2f, 0.5f, -0.7f, 1.0f, -0.6f, 0.9f, -1.1f, 0.4f, -0.8f };
	const int gearAxes[] = { 2, 2, 2, 2, 2, 2, 2, 2, 2, 2 }; // 全てZ軸中心回転に変更
	for (int i = 0; i < 10; ++i) {
		std::string name = "Gear_" + std::to_string(i);
		auto e = scene->FindObjectByName(name);
		if (e != entt::null) {
			GearInfo gi;
			gi.entity = e;
			gi.speed = gearSpeeds[i];
			gi.axis = gearAxes[i];
			gears_.push_back(gi);
		}
	}

	// パイプを検索（Pipe_0〜Pipe_4）
	for (int i = 0; i < 5; ++i) {
		std::string name = "Pipe_" + std::to_string(i);
		auto e = scene->FindObjectByName(name);
		if (e != entt::null) pipes_.push_back(e);
	}

	// タイトルカメラを検索
	titleCamera_ = scene->FindObjectByName("TitleCamera");

	// 初期状態: メインメニュー表示
	state_ = MenuState::Main;
	if (mainParent != entt::null && reg.valid(mainParent) && reg.all_of<RectTransformComponent>(mainParent))
		reg.get<RectTransformComponent>(mainParent).enabled = true;
	if (settingsParent != entt::null && reg.valid(settingsParent) && reg.all_of<RectTransformComponent>(settingsParent))
		reg.get<RectTransformComponent>(settingsParent).enabled = false;
	if (creditsParent != entt::null && reg.valid(creditsParent) && reg.all_of<RectTransformComponent>(creditsParent))
		reg.get<RectTransformComponent>(creditsParent).enabled = false;

	// ★追加: 背景とUIを分離する半透明黒パネルの作成
	auto uiOverlay = scene->FindObjectByName("UI_DarkOverlay");
	if (uiOverlay == entt::null) {
		uiOverlay = reg.create();
		reg.emplace<NameComponent>(uiOverlay, "UI_DarkOverlay");
		// 中央列のみを暗くして、左右の歯車は明るく見せる
		auto& rect = reg.emplace<RectTransformComponent>(uiOverlay);
		rect.pos = {960.0f, 540.0f};    // 画面中央
		rect.size = {800.0f, 1080.0f};  // 幅800pxに少し狭める
		rect.anchor = {0.0f, 0.0f};
		rect.pivot = {0.5f, 0.5f};
		auto& img = reg.emplace<UIImageComponent>(uiOverlay);
		img.texturePath = "Resources/Textures/white1x1.png";
		img.color = {0.0f, 0.0f, 0.0f, 0.60f}; // コントラストを稼ぐため60%黒に濃くする
		img.layer = -100; // 最背面に描画（UIの中で一番奥）
	}
}

void TitleManagerScript::Update(entt::entity entity, GameScene* scene, float dt) {
	(void)entity;
	if (!scene || !uiInitialized_) return;
	auto& reg = scene->GetRegistry();
	auto* input = Engine::Input::GetInstance();
	if (!input) return;

	// ★追加: スチームパンク ギア回転アニメーション
	titleTime_ += dt;
	for (auto& gi : gears_) {
		if (gi.entity != entt::null && reg.valid(gi.entity) && reg.all_of<TransformComponent>(gi.entity)) {
			auto& tf = reg.get<TransformComponent>(gi.entity);
			switch (gi.axis) {
			case 0: tf.rotate.x += gi.speed * dt; break;
			case 1: tf.rotate.y += gi.speed * dt; break;
			case 2: tf.rotate.z += gi.speed * dt; break;
			}
		}
	}

	// ★追加: カメラの微妙な揺れと、マウス追従（パララックス）による傾き
	if (titleCamera_ != entt::null && reg.valid(titleCamera_) && reg.all_of<TransformComponent>(titleCamera_)) {
		auto& camTf = reg.get<TransformComponent>(titleCamera_);
		
		// マウスの絶対座標を取得し、-1.0(左/上) 〜 1.0(右/下) に正規化
		// エディタ内のViewportに対応するため、GameContextのoverrideMouseを利用する
		float mx = 0.0f, my = 0.0f;
		auto& ctx = scene->GetContext();
		if (ctx.useOverrideMouse) {
			mx = ctx.overrideMouseX;
			my = ctx.overrideMouseY;
		} else {
			float fmx, fmy;
			input->GetMousePos(fmx, fmy);
			float rx = fmx - ctx.viewportOffset.x;
			float ry = fmy - ctx.viewportOffset.y;
			if (ctx.viewportSize.x > 0 && ctx.viewportSize.y > 0) {
				mx = rx * 1920.0f / ctx.viewportSize.x;
				my = ry * 1080.0f / ctx.viewportSize.y;
			} else {
				mx = rx;
				my = ry;
			}
		}
		
		float targetX = std::max(-1.0f, std::min(1.0f, (mx - 960.0f) / 960.0f));
		float targetY = std::max(-1.0f, std::min(1.0f, (my - 540.0f) / 540.0f));
		
		// なめらかな動き（線形補間）で目標値に近づける
		currentParallaxX_ += (targetX - currentParallaxX_) * 5.0f * dt;
		currentParallaxY_ += (targetY - currentParallaxY_) * 5.0f * dt;

		// 1. ゆったりとした呼吸運動（自動の揺れ） + マウス方向への並行移動
		// カメラを歯車に近づけ（Z = -6.0f）、高さを元の見栄えの良い位置（Y = 2.0f）に調整
		camTf.translate.x = std::sin(titleTime_ * 0.15f) * 0.5f + (currentParallaxX_ * 1.5f);
		camTf.translate.y = 2.0f + std::sin(titleTime_ * 0.3f) * 0.3f - (currentParallaxY_ * 1.0f);
		camTf.translate.z = -6.0f; 
		
		// 2. マウスの方向へカメラ自体を少し「傾ける（回転させる）」おしゃれな演出
		// X回転: 元の0.1fを基準に、上下にマウスを動かすと少し上を向いたり下を向いたりする
		camTf.rotate.x = 0.1f + (currentParallaxY_ * 0.05f);
		// Y回転: 左右にマウスを動かすと少しそちらを向く
		camTf.rotate.y = currentParallaxX_ * 0.05f;
		camTf.rotate.z = 0.0f;

		// ★重要: エンジンの実際のレンダリングカメラに計算した値を適用する
		if (ctx.camera) {
			ctx.camera->SetPosition({camTf.translate.x, camTf.translate.y, camTf.translate.z});
			ctx.camera->SetRotation({camTf.rotate.x, camTf.rotate.y, camTf.rotate.z});
		}
	}

	// ★追加: ポストエフェクトの適用（ヴィネットと擬似ぼかし）
	auto* renderer = Engine::Renderer::GetInstance();
	if (renderer) {
		auto pp = renderer->GetPostProcessParams();
		// ビネットを大幅に弱めて画面全体の重さを取り除く
		pp.vignette = 0.2f; 
		
		// 白飛びを防ぎつつ柔らかさを出すためブルームを控えめに調整
		pp.bloomIntensity = 0.5f;   // 少し明るく
		pp.bloomThreshold = 0.6f;   
		pp.bloomRadius = 1.5f;      
		
		// 空気遠近法（フォグ）も濃すぎると白っぽくなるため少し抑える
		pp.fogDensity = 0.012f;
		pp.fogStart = 10.0f;
		
		renderer->SetPostProcessParams(pp);
		renderer->SetPostProcessEnabled(true);
		renderer->SetPostEffect("HDRComposite");
	}

	// ★追加: 影の主張を弱めるため、環境光（Ambient）を強くし、方向光（DirLight）を弱める
	// こうすることで、歯車同士の深い影がなくなり、画面全体の「ごちゃごちゃ感」が緩和されます。
	renderer->SetAmbientColor({0.8f, 0.75f, 0.7f});

	auto dirLight = scene->FindObjectByName("DirLight");
	if (dirLight != entt::null && reg.valid(dirLight) && reg.all_of<DirectionalLightComponent>(dirLight)) {
		auto& light = reg.get<DirectionalLightComponent>(dirLight);
		// 主光源を極端に弱くすることで、落ちる影を非常に薄くする
		light.intensity = 0.6f; 
		light.color = {1.0f, 0.95f, 0.85f};
	}

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
			} else if (btnCredits_ != entt::null && reg.valid(btnCredits_) && reg.all_of<UIButtonComponent>(btnCredits_) &&
				reg.get<UIButtonComponent>(btnCredits_).isHovered) {
				state_ = MenuState::Credits;
				if (!mainEntities_.empty() && reg.valid(mainEntities_[0]) && reg.all_of<RectTransformComponent>(mainEntities_[0]))
					reg.get<RectTransformComponent>(mainEntities_[0]).enabled = false;
				if (!creditsEntities_.empty() && reg.valid(creditsEntities_[0]) && reg.all_of<RectTransformComponent>(creditsEntities_[0]))
					reg.get<RectTransformComponent>(creditsEntities_[0]).enabled = true;
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
			if (auto* dx = scene->GetWindow()) {
				isFS = dx->IsFullscreen();
			}
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
				if (auto* dx = scene->GetWindow()) {
					dx->ToggleFullscreen();
				}
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
	} else if (state_ == MenuState::Credits) {
		if (isClicked) {
			if (btnCreditsBack_ != entt::null && reg.valid(btnCreditsBack_) && reg.all_of<UIButtonComponent>(btnCreditsBack_) &&
				reg.get<UIButtonComponent>(btnCreditsBack_).isHovered) {
				state_ = MenuState::Main;
				if (!mainEntities_.empty() && reg.valid(mainEntities_[0]) && reg.all_of<RectTransformComponent>(mainEntities_[0]))
					reg.get<RectTransformComponent>(mainEntities_[0]).enabled = true;
				if (!creditsEntities_.empty() && reg.valid(creditsEntities_[0]) && reg.all_of<RectTransformComponent>(creditsEntities_[0]))
					reg.get<RectTransformComponent>(creditsEntities_[0]).enabled = false;
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

entt::entity TitleManagerScript::CreateTitleButton(GameScene* scene, entt::registry& reg, const std::string& text, float yPos, entt::entity parent) {
	auto entity = reg.create();
	auto& rect = reg.emplace<RectTransformComponent>(entity);
	rect.pos = {0.0f, yPos};
	rect.size = {300.0f, 60.0f};
	rect.anchor = {0.0f, 0.0f};
	rect.pivot = {0.0f, 0.5f};
	rect.enabled = true;

	if (parent != entt::null) {
		scene->SetParent(entity, parent); // ★修正: SetParentを使用する
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
	scene->SetParent(titleText, mainParent); // ★修正
	auto& txt = reg.emplace<UITextComponent>(titleText);
	txt.text = "Engine Project"; // ★文字変更
	txt.fontSize = 80.0f;
	txt.color = {1.0f, 0.8f, 0.2f, 1.0f};

	// ボタン生成
	auto btnStart_ = CreateTitleButton(scene, reg, "スタート", 0.0f, mainParent);
	reg.emplace<NameComponent>(btnStart_, "Btn_Start");

	auto btnSettings_ = CreateTitleButton(scene, reg, "設定", 80.0f, mainParent);
	reg.emplace<NameComponent>(btnSettings_, "Btn_Settings");

	auto btnCredits_ = CreateTitleButton(scene, reg, "クレジット", 160.0f, mainParent);
	reg.emplace<NameComponent>(btnCredits_, "Btn_Credits");

	auto btnExit_ = CreateTitleButton(scene, reg, "ゲーム終了", 240.0f, mainParent);
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
	scene->SetParent(settingsTitle, settingsParent); // ★修正
	auto& stTxt = reg.emplace<UITextComponent>(settingsTitle);
	stTxt.text = "Settings";
	stTxt.fontSize = 80.0f;

	// フルスクリーン
	auto btnFullscreen_ = CreateTitleButton(scene, reg, "Fullscreen: OFF", 0.0f, settingsParent);
	reg.emplace<NameComponent>(btnFullscreen_, "Btn_Fullscreen");

	// BGM
	auto bgmLabel = reg.create();
	reg.emplace<NameComponent>(bgmLabel, "Text_BGM");
	auto& bgmRect = reg.emplace<RectTransformComponent>(bgmLabel);
	bgmRect.pos = {0.0f, 80.0f};
	scene->SetParent(bgmLabel, settingsParent); // ★修正
	auto& bgmTxt = reg.emplace<UITextComponent>(bgmLabel);
	bgmTxt.text = "BGM Volume";
	bgmTxt.fontSize = 32.0f;

	auto btnBGMMinus_ = CreateTitleButton(scene, reg, "-", 80.0f, settingsParent);
	reg.emplace<NameComponent>(btnBGMMinus_, "Btn_BGMMinus");
	reg.get<RectTransformComponent>(btnBGMMinus_).size = {60.0f, 60.0f};
	reg.get<RectTransformComponent>(btnBGMMinus_).pos = {310.0f, 80.0f};

	auto btnBGMPlus_ = CreateTitleButton(scene, reg, "+", 80.0f, settingsParent);
	reg.emplace<NameComponent>(btnBGMPlus_, "Btn_BGMPlus");
	reg.get<RectTransformComponent>(btnBGMPlus_).size = {60.0f, 60.0f};
	reg.get<RectTransformComponent>(btnBGMPlus_).pos = {380.0f, 80.0f};

	// SE
	auto seLabel = reg.create();
	reg.emplace<NameComponent>(seLabel, "Text_SE");
	auto& seRect = reg.emplace<RectTransformComponent>(seLabel);
	seRect.pos = {0.0f, 160.0f};
	scene->SetParent(seLabel, settingsParent); // ★修正
	auto& seTxt = reg.emplace<UITextComponent>(seLabel);
	seTxt.text = "SE Volume";
	seTxt.fontSize = 32.0f;

	auto btnSEMinus_ = CreateTitleButton(scene, reg, "-", 160.0f, settingsParent);
	reg.emplace<NameComponent>(btnSEMinus_, "Btn_SEMinus");
	reg.get<RectTransformComponent>(btnSEMinus_).size = {60.0f, 60.0f};
	reg.get<RectTransformComponent>(btnSEMinus_).pos = {310.0f, 160.0f};

	auto btnSEPlus_ = CreateTitleButton(scene, reg, "+", 160.0f, settingsParent);
	reg.emplace<NameComponent>(btnSEPlus_, "Btn_SEPlus");
	reg.get<RectTransformComponent>(btnSEPlus_).size = {60.0f, 60.0f};
	reg.get<RectTransformComponent>(btnSEPlus_).pos = {380.0f, 160.0f};

	// 戻るボタン
	auto btnBack_ = CreateTitleButton(scene, reg, "Back", 260.0f, settingsParent);
	reg.emplace<NameComponent>(btnBack_, "Btn_Back");

	// クレジットメニュー
	auto creditsParent = reg.create();
	reg.emplace<NameComponent>(creditsParent, "CreditsMenuParent");
	auto& crRect = reg.emplace<RectTransformComponent>(creditsParent);
	crRect.pos = {0.0f, 0.0f};
	crRect.size = {1920.0f, 1080.0f};
	crRect.anchor = {0.0f, 0.0f};
	crRect.pivot = {0.0f, 0.0f};
	crRect.enabled = false;

	auto creditsTitle = reg.create();
	reg.emplace<NameComponent>(creditsTitle, "CreditsTitle");
	auto& cstRect = reg.emplace<RectTransformComponent>(creditsTitle);
	cstRect.pos = {0.0f, 250.0f};
	cstRect.size = {1920.0f, 100.0f};
	cstRect.anchor = {0.0f, 0.0f};
	cstRect.pivot = {0.0f, 0.0f};
	scene->SetParent(creditsTitle, creditsParent);
	auto& cstTxt = reg.emplace<UITextComponent>(creditsTitle);
	cstTxt.text = "Special Thanks";
	cstTxt.fontSize = 64.0f;
	cstTxt.fontPath = "Resources\\\\Fonts\\\\Kiwi_Maru\\\\KiwiMaru-Regular.ttf";

	auto creditsText = reg.create();
	reg.emplace<NameComponent>(creditsText, "CreditsText");
	auto& ctxRect = reg.emplace<RectTransformComponent>(creditsText);
	ctxRect.pos = {0.0f, 400.0f};
	ctxRect.size = {1920.0f, 200.0f};
	ctxRect.anchor = {0.0f, 0.0f};
	ctxRect.pivot = {0.0f, 0.0f};
	scene->SetParent(creditsText, creditsParent);
	auto& ctxTxt = reg.emplace<UITextComponent>(creditsText);
	ctxTxt.text = "Steampunk Font\nCreated by hannarb";
	ctxTxt.fontSize = 48.0f;
	ctxTxt.fontPath = "Resources\\\\Fonts\\\\Kiwi_Maru\\\\KiwiMaru-Regular.ttf";

	auto btnCreditsBack_ = CreateTitleButton(scene, reg, "Back", 800.0f, creditsParent);
	reg.emplace<NameComponent>(btnCreditsBack_, "Btn_CreditsBack");
	auto& cbRect = reg.get<RectTransformComponent>(btnCreditsBack_);
	cbRect.pos.x = 960.0f;
	cbRect.anchor = {0.0f, 0.0f};
	cbRect.pivot = {0.5f, 0.5f};
}

REGISTER_SCRIPT(TitleManagerScript)

} // namespace Game
