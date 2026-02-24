#include <Windows.h>
#include <memory>
#include "App.h"
#include "Game/Scenes/GameScene.h"
#include "Game/Scenes/TitleScene.h"

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int cmdShow) {
	SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

	// ★追加: 実行ファイルのディレクトリにCWDを設定
	// （ビルド後のコピー先に Resources/ があるため、相対パスが正しく解決される）
	{
		wchar_t exePath[MAX_PATH];
		GetModuleFileNameW(nullptr, exePath, MAX_PATH);
		wchar_t* lastSlash = wcsrchr(exePath, L'\\');
		if (lastSlash) *lastSlash = L'\0';
		SetCurrentDirectoryW(exePath);
	}

	Engine::App app;

	app.SetSceneRegistrar([](Engine::SceneManager& sm, Engine::WindowDX& dx) {
		(void)dx;
		sm.Register("Title", []() -> std::unique_ptr<Engine::IScene> { return std::unique_ptr<Engine::IScene>(new Game::TitleScene()); });
		sm.Register("Game", []() -> std::unique_ptr<Engine::IScene> { return std::unique_ptr<Engine::IScene>(new Game::GameScene()); });
	});

	// Default Scene
	app.SetInitialSceneKey("Title");

	if (!app.Initialize(hInst, cmdShow)) {
		return -1;
	}

	app.Run();
	app.Shutdown();
	return 0;
}
