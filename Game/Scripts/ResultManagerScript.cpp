#include "ResultManagerScript.h"
#include "ScriptEngine.h"
#include "../../Engine/SceneManager.h"
#include "../../Engine/Input.h"
#include "../../Engine/Renderer.h"
#include "../../Engine/WindowDX.h"
#include <Windows.h>

namespace Game {

void ResultManagerScript::Start(entt::entity entity, GameScene* scene) {
	if (!scene) return;

	// ★追加: リザルト画面ではUI操作のためにカーソルを強制表示
	while (ShowCursor(TRUE) < 0);

	// SceneParametersからデータを取得
	// パラメータはVariableComponent経由でやり取りする
	auto& reg = scene->GetRegistry();
	if (reg.valid(entity) && reg.all_of<VariableComponent>(entity)) {
		auto& vars = reg.get<VariableComponent>(entity);
		isWin_ = vars.GetValue("isWin", 0.0f) > 0.5f;
		score_ = static_cast<int>(vars.GetValue("score", 0.0f));
		clearTime_ = vars.GetValue("clearTime", 0.0f);
	}

	// static変数からの引き継ぎを優先する（設定されている場合）
	isWin_ = pendingIsWin;
	originalScene_ = pendingOriginalScene;


	// UIが存在するかチェック
	auto toTitle = scene->FindObjectByName("ToTitleButton");
	if (toTitle == entt::null) {
		CreateFallbackUI(scene, isWin_, score_, clearTime_);
		uiInitialized_ = true;
	} else {
		uiInitialized_ = true;
	}
}

void ResultManagerScript::Update(entt::entity entity, GameScene* scene, float dt) {
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
				if (name == "ToSelectButton") {
					Engine::SceneParameters p;
					p.sceneName = "Select";
					Engine::SceneManager::GetInstance()->RequestChange("Select", p);
					return;
				} else if (name == "ToTitleButton") {
					Engine::SceneParameters p;
					p.sceneName = "Title";
					Engine::SceneManager::GetInstance()->RequestChange("Title", p);
					return;
				} else if (name == "ToRetryButton") {
					Engine::SceneParameters p;
					p.stagePath = originalScene_;
					p.sceneName = "Game";
					Engine::SceneManager::GetInstance()->RequestChange("Game", p);
					return;
				}
			}
		}
	}
}

void ResultManagerScript::OnEditorUI() {}
std::string ResultManagerScript::SerializeParameters() { return ""; }
void ResultManagerScript::DeserializeParameters(const std::string& data) { (void)data; }

void ResultManagerScript::CreateFallbackUI(GameScene* scene, bool isWin, int score, float clearTime) {
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
	imgBg.color = {0.05f, 0.05f, 0.05f, 0.8f};

	// 勝敗テキスト
	auto resText = reg.create();
	reg.emplace<NameComponent>(resText, "ResultText");
	auto& rectRes = reg.emplace<RectTransformComponent>(resText);
	rectRes.pos = {0, -300};
	rectRes.anchor = {0.5f, 0.5f};
	rectRes.pivot = {0.5f, 0.5f};
	auto& txtRes = reg.emplace<UITextComponent>(resText);
	txtRes.text = isWin ? "STAGE CLEAR!" : "GAME OVER";
	txtRes.fontSize = 90.0f;
	txtRes.color = isWin ? DirectX::XMFLOAT4{0.2f, 1.0f, 0.4f, 1.0f} : DirectX::XMFLOAT4{1.0f, 0.2f, 0.2f, 1.0f};

	// スコア
	auto scoreText = reg.create();
	reg.emplace<NameComponent>(scoreText, "ScoreText");
	auto& rectScore = reg.emplace<RectTransformComponent>(scoreText);
	rectScore.pos = {0, 0};
	rectScore.anchor = {0.5f, 0.5f};
	rectScore.pivot = {0.5f, 0.5f};
	auto& txtScore = reg.emplace<UITextComponent>(scoreText);
	char buf[128];
	sprintf_s(buf, "SCORE: %d", score);
	txtScore.text = buf;
	txtScore.fontSize = 50.0f;
	txtScore.color = {1, 1, 1, 1};

	// タイム
	auto timeText = reg.create();
	reg.emplace<NameComponent>(timeText, "TimeText");
	auto& rectTime = reg.emplace<RectTransformComponent>(timeText);
	rectTime.pos = {0, 100};
	rectTime.anchor = {0.5f, 0.5f};
	rectTime.pivot = {0.5f, 0.5f};
	auto& txtTime = reg.emplace<UITextComponent>(timeText);
	sprintf_s(buf, "TIME: %.1f s", clearTime);
	txtTime.text = buf;
	txtTime.fontSize = 40.0f;
	txtTime.color = {0.8f, 0.8f, 0.8f, 1.0f};

	// タイトルへ戻るボタン
	auto toTitle = reg.create();
	reg.emplace<NameComponent>(toTitle, "ToTitleButton");
	auto& rectBtn = reg.emplace<RectTransformComponent>(toTitle);
	rectBtn.pos = {200, 300}; // 位置をずらす
	rectBtn.size = {300, 80};
	rectBtn.anchor = {0.5f, 0.5f};
	rectBtn.pivot = {0.5f, 0.5f};
	auto& uiBtnTit = reg.emplace<UIButtonComponent>(toTitle);
	uiBtnTit.normalColor = {0.35f, 0.35f, 0.35f, 1.0f};
	uiBtnTit.hoverColor = {0.55f, 0.55f, 0.55f, 1.0f};
	uiBtnTit.pressedColor = {0.2f, 0.2f, 0.2f, 1.0f};
	auto& imgTit = reg.emplace<UIImageComponent>(toTitle);
	if (renderer) imgTit.textureHandle = renderer->LoadTexture2D("Resources/Textures/white1x1.png");
	reg.emplace<UITextComponent>(toTitle).text = "RETURN TITLE";

	// リトライボタン
	auto toRetry = reg.create();
	reg.emplace<NameComponent>(toRetry, "ToRetryButton");
	auto& rectRetry = reg.emplace<RectTransformComponent>(toRetry);
	rectRetry.pos = {-200, 300}; // 左側に配置
	rectRetry.size = {300, 80};
	rectRetry.anchor = {0.5f, 0.5f};
	rectRetry.pivot = {0.5f, 0.5f};
	auto& uiBtnRetry = reg.emplace<UIButtonComponent>(toRetry);
	uiBtnRetry.normalColor = {0.35f, 0.35f, 0.35f, 1.0f};
	uiBtnRetry.hoverColor = {0.55f, 0.55f, 0.55f, 1.0f};
	uiBtnRetry.pressedColor = {0.2f, 0.2f, 0.2f, 1.0f};
	auto& imgRetry = reg.emplace<UIImageComponent>(toRetry);
	if (renderer) imgRetry.textureHandle = renderer->LoadTexture2D("Resources/Textures/white1x1.png");
	reg.emplace<UITextComponent>(toRetry).text = "RETRY";
}

REGISTER_SCRIPT(ResultManagerScript)

} // namespace Game
