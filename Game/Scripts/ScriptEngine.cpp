#include "ScriptEngine.h"
#include "Scenes/GameScene.h"
#include <iostream>
#include <Windows.h> // OutputDebugStringA

// ★ 個別のスクリプトの include はもう不要です！

namespace Game {

ScriptEngine* ScriptEngine::instance_ = nullptr;

ScriptEngine* ScriptEngine::GetInstance() {
	if (!instance_) {
		instance_ = new ScriptEngine();
	}
	return instance_;
}

void ScriptEngine::Initialize() {
	// ★変更: マクロによってプログラム起動時に自動登録されるため、
	// ここでの手動登録処理はすべて削除して空にします。
}

void ScriptEngine::Shutdown() {
	scriptFactory_.clear();
	if (instance_) {
		delete instance_;
		instance_ = nullptr;
	}
}

void ScriptEngine::RegisterScript(const std::string& className, ScriptCreator creator) { scriptFactory_[className] = creator; }

std::shared_ptr<IScript> ScriptEngine::CreateScript(const std::string& className) {
	auto it = scriptFactory_.find(className);
	if (it != scriptFactory_.end()) {
		return it->second();
	}
	// ★ エラーログ強化: クラスが見つからない場合
	std::string msg = "[ScriptEngine] CRITICAL ERROR: Script class '" + className + "' is NOT registered!\n";
	msg += "  -> Did you write REGISTER_SCRIPT(" + className + "); in your .cpp file?\n";
	msg += "  -> Is the .cpp file included in your Visual Studio project?\n";
	OutputDebugStringA(msg.c_str());
	return nullptr;
}

void ScriptEngine::Execute(SceneObject& obj, GameScene* scene, float dt) {
	if (obj.scripts.empty() || !obj.scripts[0].enabled || obj.scripts[0].scriptPath.empty()) {
		return;
	}

	auto& comp = obj.scripts[0];

	if (!comp.instance) {
		comp.instance = CreateScript(comp.scriptPath);
		if (comp.instance) {
			comp.instance->Start(obj, scene);
		} else {
			return;
		}
	}

	if (comp.instance) {
		comp.instance->Update(obj, scene, dt);
	}
}

} // namespace Game