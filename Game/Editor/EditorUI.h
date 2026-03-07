#pragma once
#include "Renderer.h"
#include "Audio.h"
#include "../ObjectTypes.h"
#include <vector>
#include <functional>
#include <string>
#include <deque>
#include <DirectXMath.h>

namespace Game {

class GameScene;

// Undo/Redo用のコマンド
struct UndoCommand {
	std::string description;
	std::function<void()> undo;
	std::function<void()> redo;
};

// ギズモ操作モード
enum class GizmoMode {
	Translate,
	Rotate,
	Scale,
};

// ★ Consoleログエントリ
enum class LogLevel { Info, Warning, Error };
struct LogEntry {
	LogLevel level;
	std::string message;
	float time;
};

class EditorUI {
public:
	static void Show(Engine::Renderer* renderer, GameScene* gameScene);

	// Undo/Redo API
	static void PushUndo(const UndoCommand& cmd);
	static void Undo();
	static void Redo();

	// レンダリングサポート用
	static void ScreenToWorldRay(float screenX, float screenY, float imageW, float imageH, DirectX::XMMATRIX view, DirectX::XMMATRIX proj, DirectX::XMVECTOR& outOrig, DirectX::XMVECTOR& outDir);

	// ★ Console API
	static void Log(const std::string& msg);
	static void LogWarning(const std::string& msg);
	static void LogError(const std::string& msg);

	// ★ シーン保存/読み込み
	static void SaveScene(GameScene* scene, const std::string& path);
	static void LoadScene(GameScene* scene, const std::string& path);
	
	// ★追加: 実行ファイルの場所に関わらず必ずTD_Engineプロジェクトを指す絶対パスを取得
	static std::string GetUnifiedProjectPath(const std::string& path);
	static void AddScene(GameScene* scene, const std::string& path); // ★追加
	static void LoadPrefab(GameScene* scene, const std::string& path);

private:
	static void ShowHierarchy(GameScene* scene);
	static void ShowInspector(GameScene* scene);
	static void ShowProject(Engine::Renderer* renderer, GameScene* scene);
	static void ShowSceneSettings(Engine::Renderer* renderer);
	static void ShowAnimationWindow(Engine::Renderer* renderer, GameScene* scene);
	static void ShowConsole();
	static void ShowPlayModeMonitor(GameScene* scene); // ★追加
	static void DrawSelectionGizmo(Engine::Renderer* renderer, GameScene* scene);
};

} // namespace Game
