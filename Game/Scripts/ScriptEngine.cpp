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

void ScriptEngine::Execute(entt::entity entity, GameScene* scene, float dt) {
	if (!scene) return;
	auto& registry = scene->GetRegistry();
	if (!registry.valid(entity) || !registry.all_of<ScriptComponent>(entity)) return;

	auto& comp = registry.get<ScriptComponent>(entity);
	if (!comp.enabled || comp.scriptPath.empty()) return;

	if (!comp.instance) {
		comp.instance = CreateScript(comp.scriptPath);
		if (comp.instance) {
			// ★追加: 保持されているパラメータをデシリアライズして反映
			if (!comp.parameterData.empty()) {
				comp.instance->DeserializeParameters(comp.parameterData);
			}
			comp.instance->Start(entity, scene);
		} else {
			return;
		}
	}

	if (comp.instance) {
		comp.instance->Update(entity, scene, dt);
	}
}

} // namespace Game