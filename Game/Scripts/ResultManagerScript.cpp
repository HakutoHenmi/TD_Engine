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

	// 背景は作成せず、現在のシーン（空）をそのまま見せる

	// 勝敗画像 (テキストの代わり)
	auto resImage = reg.create();
	reg.emplace<NameComponent>(resImage, "ResultImage");
	auto& rectRes = reg.emplace<RectTransformComponent>(resImage);
	rectRes.pos = {0, -380};
	rectRes.size = {1020, 204};
	rectRes.anchor = {0.5f, 0.5f};
	rectRes.pivot = {0.5f, 0.5f};
	auto& imgRes = reg.emplace<UIImageComponent>(resImage);
	if (renderer) {
		std::string path = isWin ? "Resources/Textures/Button/gameClear.png" : "Resources/Textures/Button/gameOver.png";
		imgRes.textureHandle = renderer->LoadTexture2D(path);
	}
	// 色は元の画像をそのまま表示
	imgRes.color = {1, 1, 1, 1};

	// --- スコア ---
	// スコアラベル画像
	auto scoreLabel = reg.create();
	reg.emplace<NameComponent>(scoreLabel, "ScoreLabel");
	auto& rectScoreL = reg.emplace<RectTransformComponent>(scoreLabel);
	rectScoreL.pos = {0, -120};
	rectScoreL.size = {765, 153};
	rectScoreL.anchor = {0.5f, 0.5f};
	rectScoreL.pivot = {0.5f, 0.5f};
	auto& imgScore = reg.emplace<UIImageComponent>(scoreLabel);
	if (renderer) imgScore.textureHandle = renderer->LoadTexture2D("Resources/Textures/Button/score.png");

	// スコア数値テキスト
	auto scoreText = reg.create();
	reg.emplace<NameComponent>(scoreText, "ScoreText");
	auto& rectScoreT = reg.emplace<RectTransformComponent>(scoreText);
	rectScoreT.pos = {187, -120}; // バランス良く中央寄りに
	rectScoreT.anchor = {0.5f, 0.5f};
	rectScoreT.pivot = {0.5f, 0.5f};
	auto& txtScore = reg.emplace<UITextComponent>(scoreText);
	char buf[128];
	sprintf_s(buf, "%d", score);
	txtScore.text = buf;
	txtScore.fontSize = 85.0f;
	txtScore.color = {0.1f, 0.1f, 0.1f, 1.0f}; // 背景に合わせて黒に近い色にする

	// --- タイム ---
	// タイムラベル画像
	auto timeLabel = reg.create();
	reg.emplace<NameComponent>(timeLabel, "TimeLabel");
	auto& rectTimeL = reg.emplace<RectTransformComponent>(timeLabel);
	rectTimeL.pos = {0, 120};
	rectTimeL.size = {765, 153};
	rectTimeL.anchor = {0.5f, 0.5f};
	rectTimeL.pivot = {0.5f, 0.5f};
	auto& imgTime = reg.emplace<UIImageComponent>(timeLabel);
	if (renderer) imgTime.textureHandle = renderer->LoadTexture2D("Resources/Textures/Button/time.png");

	// タイム数値テキスト
	auto timeText = reg.create();
	reg.emplace<NameComponent>(timeText, "TimeText");
	auto& rectTimeT = reg.emplace<RectTransformComponent>(timeText);
	rectTimeT.pos = {187, 120}; // バランス良く中央寄りに
	rectTimeT.anchor = {0.5f, 0.5f};
	rectTimeT.pivot = {0.5f, 0.5f};
	auto& txtTime = reg.emplace<UITextComponent>(timeText);
	sprintf_s(buf, "%.1fs", clearTime); // 小文字のsを追加
	txtTime.text = buf;
	txtTime.fontSize = 85.0f;
	txtTime.color = {0.1f, 0.1f, 0.1f, 1.0f}; // 背景に合わせて黒に近い色にする

	// タイトルへ戻るボタン
	auto toTitle = reg.create();
	reg.emplace<NameComponent>(toTitle, "ToTitleButton");
	auto& rectBtn = reg.emplace<RectTransformComponent>(toTitle);
	rectBtn.pos = {340, 360}; 
	rectBtn.size = {510, 136};
	rectBtn.anchor = {0.5f, 0.5f};
	rectBtn.pivot = {0.5f, 0.5f};
	auto& uiBtnTit = reg.emplace<UIButtonComponent>(toTitle);
	uiBtnTit.normalColor = {1.0f, 1.0f, 1.0f, 1.0f}; // 画像をそのまま出す
	uiBtnTit.hoverColor = {0.8f, 0.8f, 0.8f, 1.0f};
	uiBtnTit.pressedColor = {0.5f, 0.5f, 0.5f, 1.0f};
	auto& imgTit = reg.emplace<UIImageComponent>(toTitle);
	if (renderer) imgTit.textureHandle = renderer->LoadTexture2D("Resources/Textures/Button/returnToTiltle2.png");
	// テキストは不要なので追加しない

	// リトライボタン
	auto toRetry = reg.create();
	reg.emplace<NameComponent>(toRetry, "ToRetryButton");
	auto& rectRetry = reg.emplace<RectTransformComponent>(toRetry);
	rectRetry.pos = {-340, 360}; 
	rectRetry.size = {510, 136};
	rectRetry.anchor = {0.5f, 0.5f};
	rectRetry.pivot = {0.5f, 0.5f};
	auto& uiBtnRetry = reg.emplace<UIButtonComponent>(toRetry);
	uiBtnRetry.normalColor = {1.0f, 1.0f, 1.0f, 1.0f};
	uiBtnRetry.hoverColor = {0.8f, 0.8f, 0.8f, 1.0f};
	uiBtnRetry.pressedColor = {0.5f, 0.5f, 0.5f, 1.0f};
	auto& imgRetry = reg.emplace<UIImageComponent>(toRetry);
	if (renderer) imgRetry.textureHandle = renderer->LoadTexture2D("Resources/Textures/Button/retry.png");
	// テキストは不要なので追加しない
}

REGISTER_SCRIPT(ResultManagerScript)

} // namespace Game
