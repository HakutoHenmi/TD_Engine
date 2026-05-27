#include "SelectManagerScript.h"
#include "ScriptEngine.h"
#include "../../Engine/SceneManager.h"
#include "../../Engine/Input.h"
#include "../../Engine/Renderer.h"
#include "../../Engine/WindowDX.h"
#include <Windows.h>

namespace Game {

void SelectManagerScript::Start(entt::entity entity, GameScene* scene) {
	(void)entity;
	if (!scene) return;

	// ★追加: セレクト画面ではUI操作のためにカーソルを強制表示
	while (ShowCursor(TRUE) < 0);

	// ステージリスト
	stages_.clear();
	stages_.push_back({"チュートリアル", "Resources/Scenes/TutorialScene.json", "Standard TD map"});
	stages_.push_back({"ステージ１", "Resources/Scenes/Stage1.json", "Standard TD map"});
	stages_.push_back({"ステージ２", "Resources/Scenes/Stage2.json", "Advanced challenge"});

	// ★追加: 奥の背景ギアとパイプを動的に生成
	auto bgPipe = scene->FindObjectByName("Pipe_Bg");
	if (bgPipe == entt::null) {
		bgPipe = scene->CreateEntity("Pipe_Bg");
		auto& t = scene->GetRegistry().get<TransformComponent>(bgPipe);
		t.translate = {0.0f, 5.0f, 28.0f};
		t.rotate = {0.0f, 0.0f, 1.5708f};
		t.scale = {80.0f, 5.0f, 5.0f};
		auto& m = scene->GetRegistry().emplace<MeshRendererComponent>(bgPipe);
		m.modelPath = "Resources/Models/3Dmodel/pipe/pipe1.obj";
		m.texturePath = "Resources/Textures/white1x1.png";
		m.modelHandle = scene->GetRenderer()->LoadObjMesh(m.modelPath);
		m.textureHandle = scene->GetRenderer()->LoadTexture2D(m.texturePath);
		m.shaderName = "Steampunk";
		m.color = {0.25f, 0.2f, 0.15f, 1.0f};
	}
	
	struct BgGearInfo {
		std::string name;
		DirectX::XMFLOAT3 pos;
		float scale;
		DirectX::XMFLOAT4 color;
	};
	BgGearInfo bgGears[] = {
		{"BgGear1", {-15.0f, 2.0f, 18.0f}, 4.5f, {0.35f, 0.3f, 0.25f, 1.0f}},
		{"BgGear2", {-8.0f, 14.0f, 24.0f}, 5.5f, {0.25f, 0.25f, 0.3f, 1.0f}},
		{"BgGear3", {-6.0f, -4.0f, 22.0f}, 4.0f, {0.4f, 0.35f, 0.3f, 1.0f}},
		{"BgGear4", {4.0f, 16.0f, 26.0f}, 6.5f, {0.3f, 0.25f, 0.2f, 1.0f}},
	};
	
	for (const auto& info : bgGears) {
		if (scene->FindObjectByName(info.name) == entt::null) {
			auto pivot = scene->CreateEntity(info.name + "Pivot");
			auto& pt = scene->GetRegistry().get<TransformComponent>(pivot);
			pt.translate = info.pos;
			pt.rotate = {1.5708f, 0.0f, 0.0f};
			pt.scale = {info.scale, info.scale, info.scale};
			
			auto gear = scene->CreateEntity(info.name);
			scene->SetParent(gear, pivot);
			auto& gm = scene->GetRegistry().emplace<MeshRendererComponent>(gear);
			gm.modelPath = "Resources/Models/3Dmodel/gear/gear.obj";
			gm.texturePath = "Resources/Textures/white1x1.png";
			gm.modelHandle = scene->GetRenderer()->LoadObjMesh(gm.modelPath);
			gm.textureHandle = scene->GetRenderer()->LoadTexture2D(gm.texturePath);
			gm.shaderName = "Steampunk";
			gm.color = info.color;
		}
	}

	// UIが存在するかチェック
	auto backBtn = scene->FindObjectByName("BackButton");
	if (backBtn == entt::null) {
		CreateFallbackUI(scene);
		uiInitialized_ = true;
	} else {
		uiInitialized_ = true;
	}

	// 既存UIがロードされた場合も考慮して、ボタンのテキストを更新する
	auto btn0 = scene->FindObjectByName("StageButton_0");
	if (btn0 != entt::null) {
		if (scene->GetRegistry().all_of<UITextComponent>(btn0)) {
			scene->GetRegistry().get<UITextComponent>(btn0).text = "チュートリアル";
		}
		if (scene->GetRegistry().all_of<VariableComponent>(btn0)) {
			scene->GetRegistry().get<VariableComponent>(btn0).SetString("Path", "Resources/Scenes/TutorialScene.json");
		}
	}
	auto btn1 = scene->FindObjectByName("StageButton_1");
	if (btn1 != entt::null) {
		if (scene->GetRegistry().all_of<UITextComponent>(btn1)) {
			scene->GetRegistry().get<UITextComponent>(btn1).text = "ステージ１";
		}
		if (scene->GetRegistry().all_of<VariableComponent>(btn1)) {
			scene->GetRegistry().get<VariableComponent>(btn1).SetString("Path", "Resources/Scenes/Stage1.json");
		}
	}
	auto btn2 = scene->FindObjectByName("StageButton_2");
	if (btn2 != entt::null) {
		if (scene->GetRegistry().all_of<UITextComponent>(btn2)) {
			scene->GetRegistry().get<UITextComponent>(btn2).text = "ステージ２";
		}
		if (scene->GetRegistry().all_of<VariableComponent>(btn2)) {
			scene->GetRegistry().get<VariableComponent>(btn2).SetString("Path", "Resources/Scenes/Stage2.json");
		}
	}

	// 上下矢印ボタンの生成（存在しなければ）
	auto btnUp = scene->FindObjectByName("SelectUpButton");
	if (btnUp == entt::null) {
		btnUp = scene->GetRegistry().create();
		scene->GetRegistry().emplace<NameComponent>(btnUp, "SelectUpButton");
		auto& rect = scene->GetRegistry().emplace<RectTransformComponent>(btnUp);
		rect.pos = {620.0f, -120.0f}; // もう少し右に寄せて外側に
		rect.size = {96.0f, 96.0f};   // 大きくする
		rect.anchor = {0.5f, 0.5f};
		rect.pivot = {0.5f, 0.5f};
		rect.rotation = 90.0f; // 外側（上）を向くように反転

		auto& btn = scene->GetRegistry().emplace<UIButtonComponent>(btnUp);
		btn.normalColor = {0.85f, 0.70f, 0.40f, 1.0f}; // スチームパンク風の落ち着いた真鍮色（金色）
		btn.hoverColor = {1.0f, 0.90f, 0.60f, 1.0f};   // マウスカーソルを乗せると明るく光る
		btn.pressedColor = {0.60f, 0.50f, 0.25f, 1.0f}; // 押した時は少し暗く沈む

		auto& img = scene->GetRegistry().emplace<UIImageComponent>(btnUp);
		img.texturePath = "Resources/Textures/Button/Arrow.png";
		if (auto* renderer = Engine::Renderer::GetInstance()) {
			img.textureHandle = renderer->LoadTexture2D(img.texturePath);
		}
		img.color = {1.0f, 1.0f, 1.0f, 1.0f};
	}

	auto btnDown = scene->FindObjectByName("SelectDownButton");
	if (btnDown == entt::null) {
		btnDown = scene->GetRegistry().create();
		scene->GetRegistry().emplace<NameComponent>(btnDown, "SelectDownButton");
		auto& rect = scene->GetRegistry().emplace<RectTransformComponent>(btnDown);
		rect.pos = {620.0f, 120.0f}; // もう少し右に寄せて外側に
		rect.size = {96.0f, 96.0f};  // 大きくする
		rect.anchor = {0.5f, 0.5f};
		rect.pivot = {0.5f, 0.5f};
		rect.rotation = -90.0f; // 外側（下）を向くように反転

		auto& btn = scene->GetRegistry().emplace<UIButtonComponent>(btnDown);
		btn.normalColor = {0.85f, 0.70f, 0.40f, 1.0f}; // 上と同じ真鍮色
		btn.hoverColor = {1.0f, 0.90f, 0.60f, 1.0f};
		btn.pressedColor = {0.60f, 0.50f, 0.25f, 1.0f};

		auto& img = scene->GetRegistry().emplace<UIImageComponent>(btnDown);
		img.texturePath = "Resources/Textures/Button/Arrow.png";
		if (auto* renderer = Engine::Renderer::GetInstance()) {
			img.textureHandle = renderer->LoadTexture2D(img.texturePath);
		}
		img.color = {1.0f, 1.0f, 1.0f, 1.0f};
	}
}

void SelectManagerScript::Update(entt::entity entity, GameScene* scene, float dt) {
	(void)entity;
	if (!scene || !uiInitialized_) return;
	auto& reg = scene->GetRegistry();
	auto* input = Engine::Input::GetInstance();
	if (!input) return;

	// 右クリックで時計回りに回る (選択インデックスを進める)
	if (input->IsMouseTrigger(1)) {
		selectedIndex_ = (selectedIndex_ + 1) % static_cast<int>(stages_.size());
		targetAngle_ = selectedIndex_ * 45.0f; // 歯車(8枚歯)に合わせて45度単位にする
	}
	// マウスホイールでのスクロールもサポート
	if (input->GetMouseWheelDelta() < 0) {
		selectedIndex_ = (selectedIndex_ + 1) % static_cast<int>(stages_.size());
		targetAngle_ = selectedIndex_ * 45.0f;
	} else if (input->GetMouseWheelDelta() > 0) {
		selectedIndex_ = (selectedIndex_ - 1 + static_cast<int>(stages_.size())) % static_cast<int>(stages_.size());
		targetAngle_ = selectedIndex_ * 45.0f;
	}

	// 角度を滑らかに補間
	currentAngle_ += (targetAngle_ - currentAngle_) * 10.0f * dt;

	// 左クリック：決定処理
	if (input->IsMouseTrigger(0)) {
		auto view = reg.view<UIButtonComponent, NameComponent>();
		bool buttonClicked = false;
		for (auto e : view) {
			auto& btn = reg.get<UIButtonComponent>(e);
			if (btn.isHovered && btn.enabled) {
				const auto& name = reg.get<NameComponent>(e).name;
				
				if (name == "SelectUpButton") {
					selectedIndex_ = (selectedIndex_ - 1 + static_cast<int>(stages_.size())) % static_cast<int>(stages_.size());
					targetAngle_ = selectedIndex_ * 45.0f;
					buttonClicked = true;
				} else if (name == "SelectDownButton") {
					selectedIndex_ = (selectedIndex_ + 1) % static_cast<int>(stages_.size());
					targetAngle_ = selectedIndex_ * 45.0f;
					buttonClicked = true;
				} else if (name.find("StageButton_") != std::string::npos) {
					if (reg.all_of<VariableComponent>(e)) {
						std::string path = reg.get<VariableComponent>(e).GetString("Path");
						if (path.empty()) path = "Resources/Scenes/PhaseSystem.json";
						
						Engine::SceneParameters p;
						p.stagePath = path;
						p.sceneName = "Game";
						Engine::SceneManager::GetInstance()->RequestChange("Game", p);
						return;
					}
				} else if (name == "BackButton") {
					Engine::SceneParameters p;
					p.sceneName = "Title";
					Engine::SceneManager::GetInstance()->RequestChange("Title", p);
					return;
				}
			}
		}
	}

	// UIテキストの配置更新（ギアに沿った円弧状）
	float spacingAngle = 45.0f; // 歯車(8枚歯)の突起に合わせるために45度間隔
	float cx = 750.0f; // ギアの円弧の仮想中心(少し右にずらす)
	float cy = 0.0f;
	float radius = 550.0f; // 半径を調整し、テキストがギアの少し「内側」を通るようにする

	for (int i = 0; i < static_cast<int>(stages_.size()); ++i) {
		auto e = scene->FindObjectByName("StageButton_" + std::to_string(i));
		if (e != entt::null && reg.all_of<RectTransformComponent>(e)) {
			float angleDeg = currentAngle_ - i * spacingAngle;
			float rad = (angleDeg + 180.0f) * 3.14159265f / 180.0f;
			float x = cx + radius * cos(rad);
			float y = cy + radius * sin(rad);
			
			auto& rect = reg.get<RectTransformComponent>(e);
			rect.pos = {x, y};
			rect.rotation = angleDeg; // ギアの円周に合わせてテキストも傾ける

			// 選択中のものはハイライト表示、それ以外は画面端に向かってフェードアウト
			if (reg.all_of<UIImageComponent>(e)) {
				auto& imgComp = reg.get<UIImageComponent>(e);
				// 現在の角度と目標の角度（i * spacingAngle）の差分を計算
				float diff = std::abs(angleDeg); 
				// 差分が大きいほど透明にする（45度以上で完全透明になるような計算）
				float alpha = 1.0f - (diff / 45.0f);
				if (alpha < 0.0f) alpha = 0.0f;
				if (alpha > 1.0f) alpha = 1.0f;

				if (i == selectedIndex_) {
					imgComp.color = {1.0f, 1.0f, 1.0f, alpha}; // 通常の色
					rect.size = {500.0f, 125.0f}; // 強調して大きくする
					if (reg.all_of<UIButtonComponent>(e)) {
						reg.get<UIButtonComponent>(e).enabled = true;
					}
				} else {
					imgComp.color = {0.5f, 0.5f, 0.5f, alpha * 0.7f}; // 暗くして透明度を下げる
					rect.size = {320.0f, 80.0f}; // 小さくする
					if (reg.all_of<UIButtonComponent>(e)) {
						reg.get<UIButtonComponent>(e).enabled = false;
					}
				}
			}
		}
	}

	// 3Dギアモデルの回転をUIに同期
	auto gear = scene->FindObjectByName("MainGear");
	if (gear != entt::null && reg.all_of<TransformComponent>(gear)) {
		auto& t = reg.get<TransformComponent>(gear);
		// ギアは親オブジェクト(MainGearPivot)によって既にX軸で90度立てられています。
		// そのため、子オブジェクトであるMainGear自体のY軸を回すことで、コマのようにならず
		// ハンドルのように綺麗にスピンします。
		t.rotate.y = currentAngle_ * 3.14159265f / 180.0f; 
	}

	// 小さいギアの回転同期（歯車の比率に合わせて逆回転など）
	auto smallGear1 = scene->FindObjectByName("SmallGear1");
	if (smallGear1 != entt::null && reg.all_of<TransformComponent>(smallGear1)) {
		auto& t = reg.get<TransformComponent>(smallGear1);
		// MainGearがScale 16, SmallGear1がScale 5 なので 16/5 = 3.2 倍速で逆回転
		t.rotate.y = -currentAngle_ * 3.2f * 3.14159265f / 180.0f;
	}

	auto smallGear2 = scene->FindObjectByName("SmallGear2");
	if (smallGear2 != entt::null && reg.all_of<TransformComponent>(smallGear2)) {
		auto& t = reg.get<TransformComponent>(smallGear2);
		// MainGearがScale 16, SmallGear2がScale 3 なので 16/3 = 5.33 倍速で順回転（別ギア経由の想定）
		t.rotate.y = currentAngle_ * 5.33f * 3.14159265f / 180.0f;
	}

	auto smallGear3 = scene->FindObjectByName("SmallGear3");
	if (smallGear3 != entt::null && reg.all_of<TransformComponent>(smallGear3)) {
		auto& t = reg.get<TransformComponent>(smallGear3);
		// Scale 4 なので 16/4 = 4.0倍速で逆回転
		t.rotate.y = -currentAngle_ * 4.0f * 3.14159265f / 180.0f;
	}

	auto smallGear4 = scene->FindObjectByName("SmallGear4");
	if (smallGear4 != entt::null && reg.all_of<TransformComponent>(smallGear4)) {
		auto& t = reg.get<TransformComponent>(smallGear4);
		// Scale 4.5 なので 16/4.5 = 3.55倍速で順回転
		t.rotate.y = currentAngle_ * 3.55f * 3.14159265f / 180.0f;
	}

	// ★追加: 奥の背景ギアの回転
	const float bgSpeeds[] = {2.1f, -1.5f, 1.8f, -1.2f};
	for (int i = 0; i < 4; ++i) {
		auto bgGear = scene->FindObjectByName("BgGear" + std::to_string(i + 1));
		if (bgGear != entt::null && reg.all_of<TransformComponent>(bgGear)) {
			reg.get<TransformComponent>(bgGear).rotate.y = currentAngle_ * bgSpeeds[i] * 3.14159265f / 180.0f;
		}
	}

	// 水平パイプ(Pipe_H)からランダムにスチームを出す
	steamTimerH_ -= dt;
	if (steamTimerH_ <= 0.0f) {
		steamTimerH_ = 0.5f + (rand() % 150) / 100.0f; // 0.5秒〜2.0秒間隔

		DirectX::XMFLOAT3 steamPos = {-10.0f + (rand() % 20), -4.0f, 12.0f};
		DirectX::XMFLOAT3 steamFwd = {0.0f, 1.0f, -0.5f}; // 上＋手前方向

		entt::entity steamVfx = scene->CreateEntity("BackgroundSteam_H");
		auto& sTc = scene->GetRegistry().get<TransformComponent>(steamVfx);
		sTc.translate = steamPos;
		scene->SetTag(steamVfx, TagType::VFX);

		auto& sVc = scene->GetRegistry().emplace<VariableComponent>(steamVfx);
		sVc.SetValue("NormalX", steamFwd.x);
		sVc.SetValue("NormalY", steamFwd.y);
		sVc.SetValue("NormalZ", steamFwd.z);
		sVc.SetValue("Radius", 4.0f);
		sVc.SetValue("Duration", 0.4f);
		sVc.SetValue("ScatterMode", 0.0f);
		sVc.SetValue("ScatterSpeed", 13.0f);
		sVc.SetValue("Count", 40.0f);

		auto& sSc = scene->GetRegistry().emplace<ScriptComponent>(steamVfx);
		sSc.scripts.push_back({"SpaceShatterScript", "", nullptr});
	}

	// 垂直パイプ(Pipe_V)からも独立してスチームを出す
	steamTimerV_ -= dt;
	if (steamTimerV_ <= 0.0f) {
		steamTimerV_ = 0.8f + (rand() % 150) / 100.0f; // 少し間隔を変える

		// 垂直パイプは左端（X = -6）にあるため、右向き（画面中央向き）に豪快に噴出させる
		DirectX::XMFLOAT3 steamPos = {-6.0f, (float)(rand() % 12 - 4), 10.0f}; 
		DirectX::XMFLOAT3 steamFwd = {1.5f, 0.2f, -0.2f}; // 右方向（Xプラス）を強くする

		entt::entity steamVfx = scene->CreateEntity("BackgroundSteam_V");
		auto& sTc = scene->GetRegistry().get<TransformComponent>(steamVfx);
		sTc.translate = steamPos;
		scene->SetTag(steamVfx, TagType::VFX);

		auto& sVc = scene->GetRegistry().emplace<VariableComponent>(steamVfx);
		sVc.SetValue("NormalX", steamFwd.x);
		sVc.SetValue("NormalY", steamFwd.y);
		sVc.SetValue("NormalZ", steamFwd.z);
		sVc.SetValue("Radius", 5.0f); // 水平より少し太め
		sVc.SetValue("Duration", 0.5f);
		sVc.SetValue("ScatterMode", 0.0f);
		sVc.SetValue("ScatterSpeed", 15.0f); // 勢いを強く
		sVc.SetValue("Count", 50.0f); // パーティクル数を多めに

		auto& sSc = scene->GetRegistry().emplace<ScriptComponent>(steamVfx);
		sSc.scripts.push_back({"SpaceShatterScript", "", nullptr});
	}
}

void SelectManagerScript::OnEditorUI() {}
std::string SelectManagerScript::SerializeParameters() { return ""; }
void SelectManagerScript::DeserializeParameters(const std::string& data) { (void)data; }

void SelectManagerScript::CreateFallbackUI(GameScene* scene) {
	auto& reg = scene->GetRegistry();
	auto* renderer = scene->GetRenderer();

	// 背景
	auto bg = reg.create();
	reg.emplace<NameComponent>(bg, "Background");
	auto& rectBg = reg.emplace<RectTransformComponent>(bg);
	rectBg.pos = {0, 0};
	rectBg.size = {(float)Engine::WindowDX::kW, (float)Engine::WindowDX::kH};
	rectBg.anchor = {0.5f, 0.5f};
	rectBg.pivot = {0.5f, 0.5f};
	auto& imgBg = reg.emplace<UIImageComponent>(bg);
	if (renderer) imgBg.textureHandle = renderer->LoadTexture2D("Resources/Textures/white1x1.png");
	imgBg.color = {0.1f, 0.1f, 0.15f, 1.0f};

	// タイトル
	auto title = reg.create();
	reg.emplace<NameComponent>(title, "TitleText");
	auto& rectTitle = reg.emplace<RectTransformComponent>(title);
	rectTitle.pos = {0, -400};
	rectTitle.size = {800, 100};
	rectTitle.anchor = {0.5f, 0.5f};
	rectTitle.pivot = {0.5f, 0.5f};
	auto& textTitle = reg.emplace<UITextComponent>(title);
	textTitle.text = "SELECT STAGE";
	textTitle.fontSize = 64.0f;
	textTitle.color = {1, 1, 1, 1};

	// デフォルトのステージリスト（Fallback用）
	struct LocalStageInfo {
		std::string name;
		std::string path;
	};
	std::vector<LocalStageInfo> defaultStages = {
		{"チュートリアル", "Resources/Scenes/TutorialScene.json"},
		{"ステージ１", "Resources/Scenes/Stage1.json"},
		{"ステージ２", "Resources/Scenes/Stage2.json"}
	};

	// ステージボタン
	float startY = -150.0f;
	float spacing = 120.0f;
	for (size_t i = 0; i < defaultStages.size(); ++i) {
		auto btn = reg.create();
		reg.emplace<NameComponent>(btn, "StageButton_" + std::to_string(i));

		auto& rect = reg.emplace<RectTransformComponent>(btn);
		rect.pos = {0, startY + i * spacing};
		rect.size = {500, 60};
		rect.anchor = {0.5f, 0.5f};
		rect.pivot = {0.5f, 0.5f};

		auto& uiBtn = reg.emplace<UIButtonComponent>(btn);
		uiBtn.normalColor = {0.25f, 0.25f, 0.35f, 1.0f};
		uiBtn.hoverColor = {0.4f, 0.4f, 0.6f, 1.0f};
		uiBtn.pressedColor = {0.15f, 0.15f, 0.25f, 1.0f};

		auto& img = reg.emplace<UIImageComponent>(btn);
		if (renderer) img.textureHandle = renderer->LoadTexture2D("Resources/Textures/white1x1.png");

		auto& text = reg.emplace<UITextComponent>(btn);
		text.text = defaultStages[i].name;
		text.fontSize = 32.0f;
		text.color = {1, 1, 1, 1};

		reg.emplace<TagComponent>(btn, TagType::Default);
		
		auto& vComp = reg.emplace<VariableComponent>(btn);
		vComp.SetString("Path", defaultStages[i].path);
	}

	// 戻るボタン
	auto backBtn = reg.create();
	reg.emplace<NameComponent>(backBtn, "BackButton");
	auto& rectBack = reg.emplace<RectTransformComponent>(backBtn);
	rectBack.pos = {-800, 450};
	rectBack.size = {200, 60};
	rectBack.anchor = {0.5f, 0.5f};
	rectBack.pivot = {0.5f, 0.5f};

	auto& btn = reg.emplace<UIButtonComponent>(backBtn);
	btn.normalColor = {0.4f, 0.15f, 0.15f, 1.0f};
	btn.hoverColor = {0.6f, 0.2f, 0.2f, 1.0f};
	btn.pressedColor = {0.3f, 0.1f, 0.1f, 1.0f};

	if (renderer) reg.emplace<UIImageComponent>(backBtn).textureHandle = renderer->LoadTexture2D("Resources/Textures/white1x1.png");
	else reg.emplace<UIImageComponent>(backBtn);
	reg.emplace<UITextComponent>(backBtn).text = "BACK";
}

REGISTER_SCRIPT(SelectManagerScript)

} // namespace Game
