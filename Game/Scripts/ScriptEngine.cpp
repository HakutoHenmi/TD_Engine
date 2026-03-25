#include "ScriptEngine.h"
#include "Scenes/GameScene.h"
#include <iostream>
#include <Windows.h> // OutputDebugStringA
#include "PhaseSystemScript.h"
#include "PreparationCamera.h"

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
	// ★変更: マクロによる自動登録に加え、リリースビルドでのリンク漏れ防止のため明示的に登録
	RegisterScript("PhaseSystemScript", []() { return std::make_shared<PhaseSystemScript>(); });
	RegisterScript("PreparationCamera", []() { return std::make_shared<PreparationCamera>(); });
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
	if (!comp.enabled) return;

	for (auto& entry : comp.scripts) {
		if (entry.scriptPath.empty()) continue;

		if (!entry.instance) {
			entry.instance = CreateScript(entry.scriptPath);
			if (entry.instance) {
				// ★追加: 保持されているパラメータをデシリアライズして反映
				if (!entry.parameterData.empty()) {
					entry.instance->DeserializeParameters(entry.parameterData);
				}
			} else {
				continue;
			}
		}

		if (entry.instance && !entry.isStarted) {
			entry.instance->Start(entity, scene);
			entry.isStarted = true;
		}

		if (entry.instance) {
			entry.instance->Update(entity, scene, dt);
		}
	}
}

} // namespace Game