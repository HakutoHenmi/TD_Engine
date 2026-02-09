#include <Windows.h>
#include <memory>

// Engine
#include "App.h"

// Scenes（あなたの実際の配置に合わせてパス調整してください）
#include "Game/Scenes/GameOverScene.h"
#include "Game/Scenes/GameScene.h"
#include "Game/Scenes/ResultScene.h"
#include "Game/Scenes/SelectScene.h"
#include "Game/Scenes/TitleScene.h"

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int cmdShow) {

	SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

	Engine::App app;

	app.SetSceneRegistrar([](Engine::SceneManager& sm, Engine::WindowDX& dx) {
		(void)dx;

		// make_unique が使えない環境でもOKな書き方（C++11でも動く）
		sm.Register("Title", []() -> std::unique_ptr<Engine::IScene> { return std::unique_ptr<Engine::IScene>(new Game::TitleScene()); });
		sm.Register("Select", []() -> std::unique_ptr<Engine::IScene> { return std::unique_ptr<Engine::IScene>(new Game::SelectScene()); });
		sm.Register("Game", []() -> std::unique_ptr<Engine::IScene> { return std::unique_ptr<Engine::IScene>(new Game::GameScene()); });
		sm.Register("GameOver", []() -> std::unique_ptr<Engine::IScene> { return std::unique_ptr<Engine::IScene>(new Game::GameOverScene()); });
		sm.Register("Result", []() -> std::unique_ptr<Engine::IScene> { return std::unique_ptr<Engine::IScene>(new Game::ResultScene()); });
	});

	// 初期シーン
	app.SetInitialSceneKey("Title");

	if (!app.Initialize(hInst, cmdShow)) {
		return -1;
	}

	app.Run();
	app.Shutdown();
	return 0;
}
