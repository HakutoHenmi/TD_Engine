#include "SelectManagerScript.h"
#include "ScriptEngine.h"
#include "../../Engine/SceneManager.h"
#include "../../Engine/Input.h"
#include "../../Engine/Renderer.h"
#include "../../Engine/WindowDX.h"

namespace Game {

void SelectManagerScript::Start(entt::entity entity, GameScene* scene) {
	(void)entity;
	if (!scene) return;

	// ステージリスト
	stages_.clear();
	stages_.push_back({"Stage 1: Main City", "Resources/Scenes/PhaseSystem.json", "Standard TD map"});
	stages_.push_back({"Stage 2: TPS Arena", "Resources/Scenes/PhaseSystem.json", "Action oriented map"});
	stages_.push_back({"Stage 3: Tower Defense", "Resources/Scenes/PhaseSystem.json", "Defend the core"});

	// UIが存在するかチェック
	auto backBtn = scene->FindObjectByName("BackButton");
	if (backBtn == entt::null) {
		CreateFallbackUI(scene);
		uiInitialized_ = true;
	} else {
		uiInitialized_ = true;
	}
}

void SelectManagerScript::Update(entt::entity entity, GameScene* scene, float dt) {
	(void)entity;
	(void)dt;
	if (!scene || !uiInitialized_) return;
	auto& reg = scene->GetRegistry();
	auto* input = Engine::Input::GetInstance();
	if (!input) return;

	if (input->IsMouseTrigger(0)) {
		auto view = reg.view<UIButtonComponent, NameComponent>();
		for (auto e : view) {
			auto& btn = reg.get<UIButtonComponent>(e);
			if (btn.isHovered) {
				const auto& name = reg.get<NameComponent>(e).name;
				if (name.find("StageButton_") != std::string::npos) {
					if (reg.all_of<VariableComponent>(e)) {
						std::string path = reg.get<VariableComponent>(e).GetString("Path");
						// ★一時対応: どのボタンを押しても確実に機能するPhaseSystem.jsonへ飛ばす
						path = "Resources/Scenes/PhaseSystem.json";
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
	struct StageInfo {
		std::string name;
		std::string path;
	};
	std::vector<StageInfo> defaultStages = {
		{"Stage 1: Main City", "Resources/Scenes/PhaseSystem.json"},
		{"Stage 2: TPS Arena", "Resources/Scenes/PhaseSystem.json"},
		{"Stage 3: Tower Defense", "Resources/Scenes/PhaseSystem.json"}
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
