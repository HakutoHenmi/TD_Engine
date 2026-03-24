#include "EditorUI.h"
#include "../../Engine/PathUtils.h"
#include "../../externals/imgui/imgui.h"
#include "../../externals/imgui/imgui_internal.h"
#include "../Scenes/GameScene.h"
#include "../Systems/RiverSystem.h" 
#include "../Systems/UISystem.h"    
#include "../Scripts/IScript.h"     
#include "../Scripts/ScriptEngine.h" 

#include "Audio.h"
#include <tuple> // 追加
#include "PipeEditor.h"
#include "EnemySpawnerEditor.h"
#include "SceneManager.h"
#include "WindowDX.h"
#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <Windows.h>
#include <commdlg.h>
#include "../../Engine/ThirdParty/nlohmann/json.hpp"
using json = nlohmann::json;

namespace Game {
namespace fs = std::filesystem;

std::string EditorUI::currentScenePath = "Resources/scene.json";

// シーン復元ヘルパー (ID保持とヒエラルキー解決)
static std::vector<entt::entity> RestoreSceneFromJson(GameScene* scene, const json& j, bool append) {
	if (!scene) return {};
	auto& reg = scene->GetRegistry();
	if (!append) {
		OutputDebugStringA("[EditorUI] RestoreSceneFromJson: Clearing registry (FULL RELOAD)\n");
		reg.clear();
		scene->GetSelectedEntities().clear();
		scene->SetSelectedEntity(entt::null);
	}

	// settings の復元
	if (j.contains("settings") && j["settings"].is_object()) {
		auto& s = j["settings"];
		auto* r = Engine::Renderer::GetInstance();
		if (s.contains("postProcessEnabled")) r->SetPostProcessEnabled(s["postProcessEnabled"].get<bool>());
		if (s.contains("ambientColor") && s["ambientColor"].size() >= 3) {
			Engine::Vector3 ambient = {s["ambientColor"][0], s["ambientColor"][1], s["ambientColor"][2]};
			r->SetAmbientColor(ambient);
		}
	}

	std::map<uint32_t, entt::entity> idMap;
	std::vector<entt::entity> entitiesInJson;

	// Pass 1: Entity Creation & ID Mapping
	if (j.contains("objects") && j["objects"].is_array()) {
		for (auto& obj : j["objects"]) {
			std::string name = obj.value("name", "Object");
			entt::entity newEntity = scene->CreateEntity(name);
			entitiesInJson.push_back(newEntity);
			uint32_t savedId = obj.value("id", (uint32_t)entt::null);
			if (savedId != (uint32_t)entt::null) idMap[savedId] = newEntity;
		}
	} else {
		return {};
	}

	// Pass 2: Component Restoration & Hierarchy Linking
	for (size_t i = 0; i < j["objects"].size(); ++i) {
		auto& obj = j["objects"][i];
		entt::entity entity = entitiesInJson[i];

		(void)reg.get_or_emplace<NameComponent>(entity, obj.value("name", "Object"));
		auto& tc = reg.get_or_emplace<TransformComponent>(entity);
		if (obj.contains("translate") && obj["translate"].size() >= 3)
			tc.translate = {obj["translate"][0], obj["translate"][1], obj["translate"][2]};
		if (obj.contains("rotate") && obj["rotate"].size() >= 3)
			tc.rotate = {obj["rotate"][0], obj["rotate"][1], obj["rotate"][2]};
		if (obj.contains("scale") && obj["scale"].size() >= 3)
			tc.scale = {obj["scale"][0], obj["scale"][1], obj["scale"][2]};

		reg.get_or_emplace<EditorStateComponent>(entity).locked = obj.value("locked", false);
		
		auto& hc = reg.get_or_emplace<HierarchyComponent>(entity);
		uint32_t savedParentId = obj.value("parentId", (uint32_t)entt::null);
		if (savedParentId != (uint32_t)entt::null && idMap.find(savedParentId) != idMap.end()) {
			hc.parentId = idMap[savedParentId];
		} else {
			hc.parentId = entt::null;
		}

		if (obj.contains("components") && obj["components"].is_array()) {
			for (auto& comp : obj["components"]) {
				std::string type = comp.value("type", "");
				bool en = comp.value("enabled", true);

				if (type == "MeshRenderer") {
					auto& c = reg.get_or_emplace<MeshRendererComponent>(entity);
					c.enabled = en;
					c.modelPath = comp.value("modelPath", "");
					if (c.modelPath.empty()) c.modelPath = obj.value("modelPath", "");
					c.texturePath = comp.value("texturePath", "");
					if (c.texturePath.empty()) c.texturePath = obj.value("texturePath", "");
					c.shaderName = comp.value("shaderName", "");
					if (c.shaderName.empty()) c.shaderName = obj.value("shaderName", "Default");

					if (comp.contains("color")) c.color = {comp["color"][0], comp["color"][1], comp["color"][2], comp["color"][3]};
					if (comp.contains("uvTiling")) c.uvTiling = {comp["uvTiling"][0], comp["uvTiling"][1]};
					if (comp.contains("uvOffset")) c.uvOffset = {comp["uvOffset"][0], comp["uvOffset"][1]};
					if (!c.modelPath.empty()) c.modelHandle = Engine::Renderer::GetInstance()->LoadObjMesh(c.modelPath);
					if (!c.texturePath.empty()) c.textureHandle = Engine::Renderer::GetInstance()->LoadTexture2D(c.texturePath);
				} else if (type == "BoxCollider") {
					auto& c = reg.get_or_emplace<BoxColliderComponent>(entity);
					c.enabled = en;
					if (comp.contains("center")) c.center = {comp["center"][0], comp["center"][1], comp["center"][2]};
					if (comp.contains("size")) c.size = {comp["size"][0], comp["size"][1], comp["size"][2]};
					c.isTrigger = comp.value("isTrigger", false);
				} else if (type == "Rigidbody") {
					auto& c = reg.get_or_emplace<RigidbodyComponent>(entity);
					c.enabled = en;
					if (comp.contains("velocity")) c.velocity = {comp["velocity"][0], comp["velocity"][1], comp["velocity"][2]};
					c.useGravity = comp.value("useGravity", true); c.isKinematic = comp.value("isKinematic", false);
				} else if (type == "Script") {
					auto& c = reg.get_or_emplace<ScriptComponent>(entity);
					c.enabled = en;
					if (comp.contains("scripts") && comp["scripts"].is_array()) {
						for (auto& s : comp["scripts"]) {
							std::string scriptPath = s.value("path", "");
							std::string paramData = s.value("param", "");
							OutputDebugStringA(("[EditorUI] Restoring script: " + scriptPath + " params=" + paramData + "\n").c_str());
							c.scripts.push_back({ scriptPath, paramData, nullptr });
						}
					} else {
						// Backward compatibility: old format
						std::string path = comp.value("scriptPath", "");
						if (!path.empty()) {
							c.scripts.push_back({ path, comp.value("parameterData", ""), nullptr });
						}
					}
				} else if (type == "Variable") {
					auto& c = reg.get_or_emplace<VariableComponent>(entity);
					c.enabled = en;
					if (comp.contains("values")) for (auto& [k, v] : comp["values"].items()) c.values[k] = v;
					if (comp.contains("strings")) for (auto& [k, v] : comp["strings"].items()) c.strings[k] = v;
				} else if (type == "DirectionalLight") {
					auto& c = reg.get_or_emplace<DirectionalLightComponent>(entity);
					c.enabled = en; if (comp.contains("color")) c.color = {comp["color"][0], comp["color"][1], comp["color"][2]};
					c.intensity = comp.value("intensity", 1.0f);
				} else if (type == "PointLight") {
					auto& c = reg.get_or_emplace<PointLightComponent>(entity);
					c.enabled = en; if (comp.contains("color")) c.color = {comp["color"][0], comp["color"][1], comp["color"][2]};
					c.intensity = comp.value("intensity", 1.0f); c.range = comp.value("range", 10.0f);
				} else if (type == "SpotLight") {
					auto& c = reg.get_or_emplace<SpotLightComponent>(entity);
					c.enabled = en; if (comp.contains("color")) c.color = {comp["color"][0], comp["color"][1], comp["color"][2]};
					c.intensity = comp.value("intensity", 1.0f); c.range = comp.value("range", 10.0f);
				} else if (type == "Health") {
					auto& c = reg.get_or_emplace<HealthComponent>(entity);
					c.enabled = en; c.hp = comp.value("hp", 100.0f); c.maxHp = comp.value("maxHp", 100.0f); c.isDead = comp.value("isDead", false);
				} else if (type == "Tag") {
					auto& c = reg.get_or_emplace<TagComponent>(entity);
					c.enabled = en; c.tag = comp.value("tag", "");
				} else if (type == "AudioSource") {
					auto& c = reg.get_or_emplace<AudioSourceComponent>(entity);
					c.enabled = en; c.soundPath = comp.value("soundPath", ""); c.volume = comp.value("volume", 1.0f);
					c.loop = comp.value("loop", false); c.playOnStart = comp.value("playOnStart", false);
				} else if (type == "PlayerInput") {
					reg.get_or_emplace<PlayerInputComponent>(entity).enabled = en;
				} else if (type == "CharacterMovement") {
					auto& c = reg.get_or_emplace<CharacterMovementComponent>(entity);
					c.enabled = en; c.speed = comp.value("speed", 5.0f); c.jumpPower = comp.value("jumpPower", 6.0f); c.gravity = comp.value("gravity", 9.8f);
				} else if (type == "CameraTarget") {
					auto& c = reg.get_or_emplace<CameraTargetComponent>(entity);
					c.enabled = en; c.distance = comp.value("distance", 10.0f); c.height = comp.value("height", 3.0f); c.smoothSpeed = comp.value("smoothSpeed", 5.0f);
				} else if (type == "River") {
					auto& c = reg.get_or_emplace<RiverComponent>(entity);
					c.enabled = en; c.width = comp.value("width", 5.0f); c.flowSpeed = comp.value("flowSpeed", 1.0f);
				} else if (type == "WorldSpaceUI") {
					auto& c = reg.get_or_emplace<WorldSpaceUIComponent>(entity);
					c.enabled = en; c.showHealthBar = comp.value("showHealthBar", true);
				} else if (type == "GpuMeshCollider") {
					auto& c = reg.get_or_emplace<GpuMeshColliderComponent>(entity);
					c.enabled = en;
					c.meshPath = comp.value("meshPath", "");
					if (c.meshPath.empty()) c.meshPath = obj.value("modelPath", "");
					c.isTrigger = comp.value("isTrigger", false);
					if (!c.meshPath.empty()) c.meshHandle = Engine::Renderer::GetInstance()->LoadObjMesh(c.meshPath);
				} else if (type == "Animator") {
					auto& c = reg.get_or_emplace<AnimatorComponent>(entity);
					c.enabled = en; c.currentAnimation = comp.value("currentAnimation", ""); c.speed = comp.value("speed", 1.0f);
					c.isPlaying = comp.value("isPlaying", false); c.loop = comp.value("loop", true);
				} else if (type == "ParticleEmitter") {
					auto& c = reg.get_or_emplace<ParticleEmitterComponent>(entity);
					c.enabled = en; c.assetPath = comp.value("assetPath", "");
				} else if (type == "RectTransform") {
					auto& c = reg.get_or_emplace<RectTransformComponent>(entity);
					c.enabled = en; if (comp.contains("pos")) c.pos = {comp["pos"][0], comp["pos"][1]};
					if (comp.contains("size")) c.size = {comp["size"][0], comp["size"][1]};
					if (comp.contains("anchor")) c.anchor = {comp["anchor"][0], comp["anchor"][1]};
				} else if (type == "UIImage") {
					auto& c = reg.get_or_emplace<UIImageComponent>(entity);
					c.enabled = en; c.texturePath = comp.value("texturePath", "");
					if (comp.contains("color")) c.color = {comp["color"][0], comp["color"][1], comp["color"][2], comp["color"][3]};
				} else if (type == "UIText") {
					auto& c = reg.get_or_emplace<UITextComponent>(entity);
					c.enabled = en; c.text = comp.value("text", ""); c.fontSize = comp.value("fontSize", 24.0f);
				} else if (type == "UIButton") {
					reg.get_or_emplace<UIButtonComponent>(entity).enabled = en;
				} else if (type == "AudioListener") {
					reg.get_or_emplace<AudioListenerComponent>(entity).enabled = en;
				}
			}
		}
	}
	return entitiesInJson;
}

// ====== Static State ======
static std::deque<UndoCommand> undoStack;
static std::deque<UndoCommand> redoStack;
static constexpr size_t kMaxUndoDepth = 100;
static int currentAspect = 0;
static const float aspectValues[] = { 0.0f, 16.0f/9.0f, 4.0f/3.0f, 1.0f/1.0f, -1.0f };
static const char* aspects[] = { "Free", "16:9", "4:3", "1:1", "Auto" };

GizmoMode currentGizmoMode = GizmoMode::Translate;
static std::deque<LogEntry> consoleLog;
static constexpr size_t kMaxConsoleLines = 500;
static float globalTime = 0.0f;

bool gizmoDragging = false;
int gizmoDragAxis = -1; 
static ImVec2 gizmoDragStartMouse = {};
static DirectX::XMFLOAT3 gizmoStartTranslate;
static DirectX::XMFLOAT3 gizmoStartRotate;
static DirectX::XMFLOAT3 gizmoStartScale;
static DirectX::XMFLOAT3 s_inspectorStartTranslate;
static DirectX::XMFLOAT3 s_inspectorStartRotate;
static DirectX::XMFLOAT3 s_inspectorStartScale;
static bool objectDragging = false;
static std::vector<entt::entity> clipboardEntities; 
static bool s_riverPlaceMode = false;             
static int s_riverPlaceCompIdx = 0;               

static bool uiDragging = false;
static int uiDragHandle = -1; 
static DirectX::XMFLOAT2 uiDragStartPos = {};
static DirectX::XMFLOAT2 uiDragStartSize = {};
static DirectX::XMFLOAT2 uiDragStartHitOffset = {};
static DirectX::XMFLOAT2 uiDragStartHitScale = {};
static bool uiHoveredAny = false; 
static int uiHoveredHandle = -1;

static ImVec2 gameImageMin = {};
static ImVec2 gameImageMax = {};

static PipeEditor s_pipeEditor;
static EnemySpawnerEditor s_spawnerEditor;
static uint32_t nextObjectId = 1;
EditorUI::Icons EditorUI::s_icons;
static uint32_t GenerateId() { return nextObjectId++; }

static std::string GenerateCopyName(const std::string& baseName, entt::registry& registry) {
	std::string base = baseName;
	while (base.size() > 7 && base.substr(base.size() - 7) == " (Copy)")
		base = base.substr(0, base.size() - 7);
	{
		auto pos = base.rfind('_');
		if (pos != std::string::npos && pos + 1 < base.size()) {
			bool allDigit = true;
			for (size_t i = pos + 1; i < base.size(); ++i)
				if (!isdigit((unsigned char)base[i])) {
					allDigit = false;
					break;
				}
			if (allDigit)
				base = base.substr(0, pos);
		}
	}
	if (base.empty()) base = "Object";
	int maxNum = 0;
	auto view = registry.view<NameComponent>();
	for (auto entity : view) {
		const auto& name = view.get<NameComponent>(entity).name;
		if (name.size() > base.size() + 1 && name.substr(0, base.size()) == base && name[base.size()] == '_') {
			std::string numPart = name.substr(base.size() + 1);
			bool allDigit = true;
			for (char c : numPart) if (!isdigit((unsigned char)c)) { allDigit = false; break; }
			if (allDigit && !numPart.empty()) {
				int n = std::stoi(numPart);
				if (n > maxNum) maxNum = n;
			}
		}
	}
	return base + "_" + std::to_string(maxNum + 1);
}

// ====== Undo/Redo ======
void EditorUI::PushUndo(const UndoCommand& cmd) {
	undoStack.push_back(cmd);
	if (undoStack.size() > kMaxUndoDepth) undoStack.pop_front();
	redoStack.clear();
}
void EditorUI::Undo() {
	if (undoStack.empty()) return;
	auto c = undoStack.back();
	undoStack.pop_back();
	c.undo();
	redoStack.push_back(c);
}
void EditorUI::Redo() {
	if (redoStack.empty()) return;
	auto c = redoStack.back();
	redoStack.pop_back();
	c.redo();
	undoStack.push_back(c);
}

// ====== Console ======
void EditorUI::Log(const std::string& msg) {
	consoleLog.push_back({LogLevel::Info, msg, globalTime});
	if (consoleLog.size() > kMaxConsoleLines) consoleLog.pop_front();
}
void EditorUI::LogWarning(const std::string& msg) {
	consoleLog.push_back({LogLevel::Warning, msg, globalTime});
	if (consoleLog.size() > kMaxConsoleLines) consoleLog.pop_front();
}
void EditorUI::LogError(const std::string& msg) {
	consoleLog.push_back({LogLevel::Error, msg, globalTime});
	if (consoleLog.size() > kMaxConsoleLines) consoleLog.pop_front();
}

ImVec2 EditorUI::GetGameImageMin() { return gameImageMin; }
ImVec2 EditorUI::GetGameImageMax() { return gameImageMax; }

// ====== JSON Serialization ======
static std::string EscapeJson(const std::string& s) {
	std::string o;
	for (char c : s) {
		switch (c) {
		case '"':  o += "\\\""; break;
		case '\\': o += "\\\\"; break;
		case '\b': o += "\\b";  break;
		case '\f': o += "\\f";  break;
		case '\n': o += "\\n";  break;
		case '\r': o += "\\r";  break;
		case '\t': o += "\\t";  break;
		default:
			if (static_cast<unsigned char>(c) < 32) {
				char buf[8];
				sprintf_s(buf, "\\u%04x", static_cast<int>(c));
				o += buf;
			} else {
				o += c;
			}
		}
	}
	return o;
}

static std::string SerializeEntity(entt::registry& registry, entt::entity entity) {
	std::stringstream ss;
	ss << "    {\n";
	uint32_t id = static_cast<uint32_t>(entity);
	ss << "      \"id\": " << id << ",\n";
	
	uint32_t parentId = 0;
	if (auto* hc = registry.try_get<HierarchyComponent>(entity)) {
		parentId = static_cast<uint32_t>(hc->parentId);
	}
	ss << "      \"parentId\": " << parentId << ",\n";

	std::string name = "Object";
	if (auto* nc = registry.try_get<NameComponent>(entity)) name = nc->name;
	ss << "      \"name\": \"" << EscapeJson(name) << "\",\n";

	bool locked = false;
	if (auto* es = registry.try_get<EditorStateComponent>(entity)) locked = es->locked;
	ss << "      \"locked\": " << (locked ? "true" : "false") << ",\n";

	auto* tc = registry.try_get<TransformComponent>(entity);
	if (tc) {
		ss << "      \"translate\": [" << tc->translate.x << ", " << tc->translate.y << ", " << tc->translate.z << "],\n";
		ss << "      \"rotate\": [" << tc->rotate.x << ", " << tc->rotate.y << ", " << tc->rotate.z << "],\n";
		ss << "      \"scale\": [" << tc->scale.x << ", " << tc->scale.y << ", " << tc->scale.z << "],\n";
	} else {
		ss << "      \"translate\": [0,0,0], \"rotate\": [0,0,0], \"scale\": [1,1,1],\n";
	}

	ss << "      \"components\": [\n";
	bool first = true;
	auto addComma = [&]() { if (!first) ss << ",\n"; first = false; };

	if (auto* cp = registry.try_get<MeshRendererComponent>(entity)) {
		addComma();
		ss << "        {\"type\": \"MeshRenderer\", \"enabled\": " << (cp->enabled ? "true" : "false") << ", \"modelPath\": \"" << EscapeJson(cp->modelPath) << "\", \"texturePath\": \""
		   << EscapeJson(cp->texturePath) << "\", \"shaderName\": \"" << EscapeJson(cp->shaderName) << "\", \"color\": [" << cp->color.x << "," << cp->color.y << "," << cp->color.z << "," << cp->color.w << "], \"uvTiling\": [" << cp->uvTiling.x << "," << cp->uvTiling.y << "], \"uvOffset\": [" << cp->uvOffset.x << "," << cp->uvOffset.y << "]}";
	}
	if (auto* cp = registry.try_get<BoxColliderComponent>(entity)) {
		addComma();
		ss << "        {\"type\": \"BoxCollider\", \"enabled\": " << (cp->enabled ? "true" : "false") << ", \"center\": [" << cp->center.x << "," << cp->center.y << "," << cp->center.z << "], \"size\": ["
		   << cp->size.x << "," << cp->size.y << "," << cp->size.z << "], \"isTrigger\": " << (cp->isTrigger ? "true" : "false") << "}";
	}
	if (auto* cp = registry.try_get<RigidbodyComponent>(entity)) {
		addComma();
		ss << "        {\"type\": \"Rigidbody\", \"enabled\": " << (cp->enabled ? "true" : "false") << ", \"velocity\": [" << cp->velocity.x << "," << cp->velocity.y << "," << cp->velocity.z
		   << "], \"useGravity\": " << (cp->useGravity ? "true" : "false") << ", \"isKinematic\": " << (cp->isKinematic ? "true" : "false") << "}";
	}
	if (auto* cp = registry.try_get<AudioSourceComponent>(entity)) {
		addComma();
		ss << "        {\"type\": \"AudioSource\", \"enabled\": " << (cp->enabled ? "true" : "false") << ", \"soundPath\": \"" << EscapeJson(cp->soundPath) << "\", \"volume\": " << cp->volume
		   << ", \"loop\": " << (cp->loop ? "true" : "false") << ", \"playOnStart\": " << (cp->playOnStart ? "true" : "false") << "}";
	}
	if (auto* cp = registry.try_get<ScriptComponent>(entity)) {
		addComma();
		ss << "        {\"type\": \"Script\", \"enabled\": " << (cp->enabled ? "true" : "false") << ", \"scripts\": [";
		for (size_t i = 0; i < cp->scripts.size(); ++i) {
			auto& entry = cp->scripts[i];
			std::string params = entry.parameterData;
			if (entry.instance) params = entry.instance->SerializeParameters();
			ss << "{\"path\": \"" << EscapeJson(entry.scriptPath) << "\", \"param\": \"" << EscapeJson(params) << "\"}";
			if (i < cp->scripts.size() - 1) ss << ", ";
		}
		ss << "]}";
	}
	if (auto* cp = registry.try_get<DirectionalLightComponent>(entity)) {
		addComma();
		ss << "        {\"type\": \"DirectionalLight\", \"enabled\": " << (cp->enabled ? "true" : "false") << ", \"color\": [" << cp->color.x << "," << cp->color.y << "," << cp->color.z << "], \"intensity\": " << cp->intensity << "}";
	}
	if (auto* cp = registry.try_get<PointLightComponent>(entity)) {
		addComma();
		ss << "        {\"type\": \"PointLight\", \"enabled\": " << (cp->enabled ? "true" : "false") << ", \"color\": [" << cp->color.x << "," << cp->color.y << "," << cp->color.z << "], \"intensity\": " << cp->intensity << ", \"range\": " << cp->range << "}";
	}
	if (auto* cp = registry.try_get<SpotLightComponent>(entity)) {
		addComma();
		ss << "        {\"type\": \"SpotLight\", \"enabled\": " << (cp->enabled ? "true" : "false") << ", \"color\": [" << cp->color.x << "," << cp->color.y << "," << cp->color.z << "], \"intensity\": " << cp->intensity << ", \"range\": " << cp->range << "}";
	}
	if (auto* cp = registry.try_get<HealthComponent>(entity)) {
		addComma();
		ss << "        {\"type\": \"Health\", \"enabled\": " << (cp->enabled ? "true" : "false") << ", \"hp\": " << cp->hp << ", \"maxHp\": " << cp->maxHp << ", \"isDead\": " << (cp->isDead ? "true" : "false") << "}";
	}
	if (auto* cp = registry.try_get<TagComponent>(entity)) {
		addComma();
		ss << "        {\"type\": \"Tag\", \"enabled\": " << (cp->enabled ? "true" : "false") << ", \"tag\": \"" << EscapeJson(cp->tag) << "\"}";
	}
	if (auto* cp = registry.try_get<PlayerInputComponent>(entity)) {
		addComma();
		ss << "        {\"type\": \"PlayerInput\", \"enabled\": " << (cp->enabled ? "true" : "false") << "}";
	}
	if (auto* cp = registry.try_get<CharacterMovementComponent>(entity)) {
		addComma();
		ss << "        {\"type\": \"CharacterMovement\", \"enabled\": " << (cp->enabled ? "true" : "false") << ", \"speed\": " << cp->speed << ", \"jumpPower\": " << cp->jumpPower << ", \"gravity\": " << cp->gravity << "}";
	}
	if (auto* cp = registry.try_get<CameraTargetComponent>(entity)) {
		addComma();
		ss << "        {\"type\": \"CameraTarget\", \"enabled\": " << (cp->enabled ? "true" : "false") << ", \"distance\": " << cp->distance << ", \"height\": " << cp->height << ", \"smoothSpeed\": " << cp->smoothSpeed << "}";
	}
	if (auto* cp = registry.try_get<RiverComponent>(entity)) {
		addComma();
		ss << "        {\"type\": \"River\", \"enabled\": " << (cp->enabled ? "true" : "false") << ", \"width\": " << cp->width << ", \"flowSpeed\": " << cp->flowSpeed << "}";
	}
	if (auto* cp = registry.try_get<WorldSpaceUIComponent>(entity)) {
		addComma();
		ss << "        {\"type\": \"WorldSpaceUI\", \"enabled\": " << (cp->enabled ? "true" : "false") << ", \"showHealthBar\": " << (cp->showHealthBar ? "true" : "false") << "}";
	}
	if (auto* cp = registry.try_get<GpuMeshColliderComponent>(entity)) {
		addComma();
		ss << "        {\"type\": \"GpuMeshCollider\", \"enabled\": " << (cp->enabled ? "true" : "false") << ", \"meshPath\": \"" << EscapeJson(cp->meshPath) << "\", \"isTrigger\": " << (cp->isTrigger ? "true" : "false") << "}";
	}
	if (auto* cp = registry.try_get<AnimatorComponent>(entity)) {
		addComma();
		ss << "        {\"type\": \"Animator\", \"enabled\": " << (cp->enabled ? "true" : "false") << ", \"currentAnimation\": \"" << EscapeJson(cp->currentAnimation)
		   << "\", \"speed\": " << cp->speed << ", \"isPlaying\": " << (cp->isPlaying ? "true" : "false") << ", \"loop\": " << (cp->loop ? "true" : "false") << "}";
	}
	if (auto* cp = registry.try_get<VariableComponent>(entity)) {
		addComma();
		ss << "        {\"type\": \"Variable\", \"enabled\": " << (cp->enabled ? "true" : "false") << ", \"values\": {";
		bool firstVar = true;
		for (auto& [k, v] : cp->values) {
			if (!firstVar) ss << ",";
			ss << "\"" << EscapeJson(k) << "\": " << v;
			firstVar = false;
		}
		ss << "}, \"strings\": {";
		firstVar = true;
		for (auto& [k, v] : cp->strings) {
			if (!firstVar) ss << ",";
			ss << "\"" << EscapeJson(k) << "\": \"" << EscapeJson(v) << "\"";
			firstVar = false;
		}
		ss << "}}";
	}
	if (auto* cp = registry.try_get<ParticleEmitterComponent>(entity)) {
		addComma();
		ss << "        {\"type\": \"ParticleEmitter\", \"enabled\": " << (cp->enabled ? "true" : "false") << ", \"assetPath\": \"" << EscapeJson(cp->assetPath) << "\"}";
	}
	if (auto* cp = registry.try_get<RectTransformComponent>(entity)) {
		addComma();
		ss << "        {\"type\": \"RectTransform\", \"enabled\": " << (cp->enabled ? "true" : "false") << ", \"pos\": [" << cp->pos.x << "," << cp->pos.y << "], \"size\": [" << cp->size.x << "," << cp->size.y << "], \"anchor\": [" << cp->anchor.x << "," << cp->anchor.y << "]}";
	}
	if (auto* cp = registry.try_get<UIImageComponent>(entity)) {
		addComma();
		ss << "        {\"type\": \"UIImage\", \"enabled\": " << (cp->enabled ? "true" : "false") << ", \"texturePath\": \"" << EscapeJson(cp->texturePath) << "\", \"color\": [" << cp->color.x << "," << cp->color.y << "," << cp->color.z << "," << cp->color.w << "]}";
	}
	if (auto* cp = registry.try_get<UITextComponent>(entity)) {
		addComma();
		ss << "        {\"type\": \"UIText\", \"enabled\": " << (cp->enabled ? "true" : "false") << ", \"text\": \"" << EscapeJson(cp->text) << "\", \"fontSize\": " << cp->fontSize << "}";
	}
	if (auto* cp = registry.try_get<UIButtonComponent>(entity)) {
		addComma();
		ss << "        {\"type\": \"UIButton\", \"enabled\": " << (cp->enabled ? "true" : "false") << "}";
	}
	if (auto* cp = registry.try_get<AudioListenerComponent>(entity)) {
		addComma();
		ss << "        {\"type\": \"AudioListener\", \"enabled\": " << (cp->enabled ? "true" : "false") << "}";
	}

	ss << "\n      ]\n";
	ss << "    }";
	return ss.str();
}

std::string EditorUI::SaveToMemory(GameScene* scene) {
	if (!scene) return "";
	std::stringstream ss;
	ss << "{\n  \"settings\": {\n";
	auto* r = Engine::Renderer::GetInstance();
	auto pp = r->GetPostProcessParams();
	ss << "    \"postProcessEnabled\": " << (r->GetPostProcessEnabled() ? "true" : "false") << ",\n";
	auto ambient = r->GetLightCB().ambientColor;
	ss << "    \"ambientColor\": [" << ambient.x << ", " << ambient.y << ", " << ambient.z << "],\n";
	ss << "    \"vignette\": " << pp.vignette << ",\n";
	ss << "    \"noiseStrength\": " << pp.noiseStrength << "\n";
	ss << "  },\n";
	ss << "  \"objects\": [\n";

	auto& registry = scene->GetRegistry();
	auto view = registry.view<NameComponent>();
	std::vector<entt::entity> sortedEntities(view.begin(), view.end());
	for (size_t i = 0; i < sortedEntities.size(); ++i) {
		if (i > 0) ss << ",\n";
		ss << SerializeEntity(registry, sortedEntities[i]);
	}
	ss << "\n  ]\n}\n";
	return ss.str();
}

void EditorUI::SaveScene(GameScene* scene, const std::string& path) {
	if (!scene) return;
	std::string targetPath = path;
	if (targetPath.empty()) targetPath = currentScenePath;
	else currentScenePath = targetPath;

	std::string absPath = GetUnifiedProjectPath(targetPath);
	OutputDebugStringA(("[EditorUI] Saving scene to: " + absPath + "\n").c_str());
	std::ofstream f(absPath);
	if (!f.is_open()) { LogError("Save failed: " + absPath); return; }
	f << SaveToMemory(scene);
	f.close();
	Log("Scene saved: " + absPath);
}

// ====== Utility Functions & File Dialogs ======


static std::string OpenFileDialog(const char* filter) {
	OPENFILENAMEA ofn;
	CHAR szFile[260] = {0};
	ZeroMemory(&ofn, sizeof(OPENFILENAMEA));
	ofn.lStructSize = sizeof(OPENFILENAMEA);
	ofn.hwndOwner = nullptr;
	ofn.lpstrFile = szFile;
	ofn.nMaxFile = sizeof(szFile);
	ofn.lpstrFilter = filter;
	ofn.nFilterIndex = 1;
	ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
	if (GetOpenFileNameA(&ofn) == TRUE) return ofn.lpstrFile;
	return "";
}

static std::string SaveFileDialog(const char* filter, const char* defExt) {
	OPENFILENAMEA ofn;
	CHAR szFile[260] = {0};
	ZeroMemory(&ofn, sizeof(OPENFILENAMEA));
	ofn.lStructSize = sizeof(OPENFILENAMEA);
	ofn.hwndOwner = nullptr;
	ofn.lpstrFile = szFile;
	ofn.nMaxFile = sizeof(szFile);
	ofn.lpstrFilter = filter;
	ofn.nFilterIndex = 1;
	ofn.lpstrDefExt = defExt;
	ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
	if (GetSaveFileNameA(&ofn) == TRUE) return ofn.lpstrFile;
	return "";
}

static void LoadSceneInternal(GameScene* scene, const std::string& path, bool append) {
	if (!scene) return;
	std::string absPath = EditorUI::GetUnifiedProjectPath(path);
	std::ifstream f(absPath);
	if (!f.is_open()) {
		absPath = path; f.open(absPath);
		if (!f.is_open()) { EditorUI::LogError("Load failed: " + absPath); return; }
	}
	json j;
	try { f >> j; } catch (...) { EditorUI::LogError("JSON Syntax Error: " + path); return; }
	RestoreSceneFromJson(scene, j, append);
	EditorUI::Log((append ? "Scene appended: " : "Scene loaded: ") + absPath);
}

void EditorUI::LoadScene(GameScene* scene, const std::string& path) {
	currentScenePath = path;
	(void)LoadSceneInternal(scene, path, false);
}
void EditorUI::AddScene(GameScene* scene, const std::string& path) { (void)LoadSceneInternal(scene, path, true); }

void EditorUI::LoadFromMemory(GameScene* scene, const std::string& data) {
	if (!scene) return;
	json j;
	OutputDebugStringA(("[EditorUI] LoadFromMemory: data size = " + std::to_string(data.size()) + "\n").c_str());
	try { j = json::parse(data); } catch (const std::exception& e) { 
		LogError("JSON Parse Error in memory snapshot: " + std::string(e.what())); 
		return; 
	}
	(void)RestoreSceneFromJson(scene, j, false);
}
std::vector<entt::entity> EditorUI::LoadPrefab(GameScene* scene, const std::string& path) {
	if (!scene) return {};
	std::string absPath = GetUnifiedProjectPath(path);
	std::ifstream f(absPath);
	if (!f.is_open()) {
		absPath = path; f.open(absPath);
		if (!f.is_open()) { LogError("Prefab load failed: " + absPath); return {}; }
	}
	json j;
	try { f >> j; } catch (...) { LogError("Prefab JSON error: " + path); return {}; }
	
	std::vector<entt::entity> createdEntities;
	if (j.contains("objects") && j["objects"].is_array()) {
		createdEntities = RestoreSceneFromJson(scene, j, true); // Append mode
	} else if (j.contains("prefab")) {
		// Support "prefab" root key
		json wrapper = json::object();
		wrapper["objects"] = json::array();
		wrapper["objects"].push_back(j["prefab"]);
		createdEntities = RestoreSceneFromJson(scene, wrapper, true);
	} else {
		// Single entity prefab
		json wrapper = json::object();
		wrapper["objects"] = json::array();
		wrapper["objects"].push_back(j);
		createdEntities = RestoreSceneFromJson(scene, wrapper, true);
	}

	if (createdEntities.empty()) {
		LogError("Prefab created no entities: " + absPath);
	} else {
		Log("Prefab loaded: " + absPath + " (" + std::to_string(createdEntities.size()) + " entities)");
	}
	return createdEntities;
}

std::string EditorUI::GetUnifiedProjectPath(const std::string& path) {
	return Engine::PathUtils::GetUnifiedPath(path);
}

void EditorUI::Initialize(Engine::Renderer* renderer) {
	if (!renderer) return;
	s_icons.folder = renderer->LoadTexture2D("Resources/Editor/Icons/folder.png");
	s_icons.file = renderer->LoadTexture2D("Resources/Editor/Icons/file.png");
	s_icons.model = renderer->LoadTexture2D("Resources/Editor/Icons/model.png");
	s_icons.prefab = renderer->LoadTexture2D("Resources/Editor/Icons/prefab.png");
	s_icons.audio = renderer->LoadTexture2D("Resources/Editor/Icons/audio.png");
	s_icons.script = renderer->LoadTexture2D("Resources/Editor/Icons/script.png");
}

// ====== Main UI ======
void EditorUI::Show(Engine::Renderer* renderer, GameScene* gameScene) {
	globalTime += ImGui::GetIO().DeltaTime;
	
	ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;
	ImGuiViewport* viewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(viewport->WorkPos);
	ImGui::SetNextWindowSize(viewport->WorkSize);
	ImGui::SetNextWindowViewport(viewport->ID);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
	window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

	ImGui::Begin("EditorMain", nullptr, window_flags);
	ImGui::PopStyleVar(2);

	ImGuiIO& io = ImGui::GetIO();
	if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable) {
		ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
		ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);
	}

	// Global Shortcuts
	if (!io.WantTextInput) {
		if (io.KeyCtrl) {
			if (ImGui::IsKeyPressed(ImGuiKey_S)) SaveScene(gameScene, "Resources/scene.json");
			if (ImGui::IsKeyPressed(ImGuiKey_Z)) Undo();
			if (ImGui::IsKeyPressed(ImGuiKey_Y)) Redo();
			if (ImGui::IsKeyPressed(ImGuiKey_N)) {
				gameScene->GetRegistry().clear();
				gameScene->GetSelectedEntities().clear();
				gameScene->SetSelectedEntity(entt::null);
			}
			if (ImGui::IsKeyPressed(ImGuiKey_O)) {
				std::string path = OpenFileDialog("JSON Files (*.json)\0*.json\0All Files (*.*)\0*.*\0");
				if (!path.empty()) LoadScene(gameScene, path);
			}
		} else {
			if (ImGui::IsKeyPressed(ImGuiKey_W)) currentGizmoMode = GizmoMode::Translate;
			if (ImGui::IsKeyPressed(ImGuiKey_E)) currentGizmoMode = GizmoMode::Rotate;
			if (ImGui::IsKeyPressed(ImGuiKey_R)) currentGizmoMode = GizmoMode::Scale;
		}
	}

	if (ImGui::BeginMenuBar()) {
		if (ImGui::BeginMenu("File")) {
			if (ImGui::MenuItem("New Scene", "Ctrl+N")) {
				gameScene->GetRegistry().clear();
				gameScene->GetSelectedEntities().clear();
				gameScene->SetSelectedEntity(entt::null);
				currentScenePath = "Resources/scene.json";
			}
			ImGui::Separator();
			if (ImGui::MenuItem("Open Scene", "Ctrl+O")) {
				std::string path = OpenFileDialog("JSON Files (*.json)\0*.json\0All Files (*.*)\0*.*\0");
				if (!path.empty()) LoadScene(gameScene, path);
			}
			if (ImGui::MenuItem("Append Scene", "Ctrl+Shift+O")) {
				std::string path = OpenFileDialog("JSON Files (*.json)\0*.json\0All Files (*.*)\0*.*\0");
				if (!path.empty()) AddScene(gameScene, path);
			}
			ImGui::Separator();
			if (ImGui::MenuItem("Save Scene", "Ctrl+S")) {
				SaveScene(gameScene);
			}
			if (ImGui::MenuItem("Save As...", "Ctrl+Shift+S")) {
				std::string path = SaveFileDialog("JSON Files (*.json)\0*.json\0All Files (*.*)\0*.*\0", "json");
				if (!path.empty()) SaveScene(gameScene, path);
			}
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Scenes")) {
			for (const auto& entry : fs::directory_iterator(GetUnifiedProjectPath("Resources"))) {
				if (entry.path().extension() == ".json") {
					std::string fileName = entry.path().filename().string();
					std::string fullPath = "Resources/" + fileName;
					bool selected = (currentScenePath == fullPath);
					if (ImGui::MenuItem(fileName.c_str(), nullptr, selected)) {
						LoadScene(gameScene, fullPath);
					}
				}
			}
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Edit")) {
			if (ImGui::MenuItem("Undo", "Ctrl+Z")) Undo();
			if (ImGui::MenuItem("Redo", "Ctrl+Y")) Redo();
			ImGui::Separator();
			if (ImGui::MenuItem("Translate", "W", currentGizmoMode == GizmoMode::Translate)) currentGizmoMode = GizmoMode::Translate;
			if (ImGui::MenuItem("Rotate", "E", currentGizmoMode == GizmoMode::Rotate)) currentGizmoMode = GizmoMode::Rotate;
			if (ImGui::MenuItem("Scale", "R", currentGizmoMode == GizmoMode::Scale)) currentGizmoMode = GizmoMode::Scale;
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Tools")) {
			static bool pipeOpen = false;
			if (ImGui::MenuItem("Pipe Mode", nullptr, &pipeOpen)) {
				s_pipeEditor.SetPipeMode(pipeOpen);
			}
			static bool spawnerOpen = false;
			if (ImGui::MenuItem("Enemy Spawner", nullptr, &spawnerOpen)) {
				s_spawnerEditor.SetSpawnerMode(spawnerOpen);
			}
			ImGui::EndMenu();
		}
		
		// Tool Specific UI (Snaps etc)
		ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
		s_pipeEditor.DrawUI();
		s_spawnerEditor.DrawUI();
		ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
		
		// Spacing
		ImGui::SameLine(ImGui::GetWindowWidth() * 0.5f - 100.0f);

		// Aspect Ratio
		ImGui::SetNextItemWidth(120);
		if (ImGui::BeginCombo("##Aspect", aspects[currentAspect])) {
			for (int n = 0; n < IM_ARRAYSIZE(aspects); n++) {
				if (ImGui::Selectable(aspects[n], currentAspect == n)) currentAspect = n;
			}
			ImGui::EndCombo();
		}

		// Play/Stop button
		ImGui::SameLine();
		if (gameScene->GetIsPlaying()) {
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
			if (ImGui::Button(" STOP ")) gameScene->SetIsPlaying(false);
			ImGui::PopStyleColor(2);
		} else {
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.7f, 0.2f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.9f, 0.3f, 1.0f));
			if (ImGui::Button(" PLAY ")) gameScene->SetIsPlaying(true);
			ImGui::PopStyleColor(2);
		}

		ImGui::EndMenuBar();
	}

	// Toolbar removed (Integrated into MenuBar)

	ShowHierarchy(gameScene);
	ShowConsole();

	// Right Sidebar Tabs
	ImGui::Begin("Inspector / Tools");
	if (ImGui::BeginTabBar("RightSideTabs")) {
		if (ImGui::BeginTabItem("Inspector")) {
			// PLAY 中はインスペクターの編集を無効化
			ImGui::BeginDisabled(gameScene->GetIsPlaying());
			ShowInspector(gameScene);
			ImGui::EndDisabled();
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Animation")) {
			ShowAnimationWindow(renderer, gameScene);
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Scene Settings")) {
			ShowSceneSettings(renderer);
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Particle Editor")) {
			gameScene->GetParticleEditor().DrawUI();
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Play Monitor")) {
			ShowPlayModeMonitor(gameScene);
			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}
	ImGui::End();

	ShowProject(renderer, gameScene);

	// Game Viewport
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
	ImGui::Begin("Game");
	
	ImVec2 av = ImGui::GetContentRegionAvail();
	float tW = av.x, tH = av.y;
	
	// Apply Aspect Ratio & Update Camera
	float nativeAspect = (float)Engine::WindowDX::kW / (float)Engine::WindowDX::kH;
	float targetAspect = nativeAspect; // Default to native to prevent squashing 2D/3D
	
	if (currentAspect > 0 && aspectValues[currentAspect] > 0.0f) {
		targetAspect = aspectValues[currentAspect];
	}

	float renderW = tW, renderH = tH;
	float currentAvailAspect = tW / tH;
	
	// Always letterbox to prevent any stretching/squashing of the underlying Fixed-Size Render Target.
	if (currentAvailAspect > targetAspect) {
		renderW = tH * targetAspect;
		renderH = tH;
	} else {
		renderW = tW;
		renderH = tW / targetAspect;
	}
	gameScene->GetCamera().SetProjection(0.7854f, targetAspect, 0.1f, 1000.0f);
	
	// Center the image
	ImVec2 cursorPadding((tW - renderW) * 0.5f, (tH - renderH) * 0.5f);
	ImVec2 currentCursorPos = ImGui::GetCursorPos();
	float targetIdxX = currentCursorPos.x + cursorPadding.x;
	float targetIdxY = currentCursorPos.y + cursorPadding.y;
	ImGui::SetCursorPos(ImVec2(targetIdxX, targetIdxY));
	
	// Pick game image region
	gameImageMin = ImGui::GetCursorScreenPos();
	gameImageMax = ImVec2(gameImageMin.x + renderW, gameImageMin.y + renderH);
	
	// Tool Editors Update & Draw (Overlays)
	s_pipeEditor.UpdateAndDraw(gameScene, renderer, gameImageMin, gameImageMax, renderW, renderH);
	s_spawnerEditor.UpdateAndDraw(gameScene, renderer, gameImageMin, gameImageMax, renderW, renderH);

	ImGui::Image((ImTextureID)renderer->GetGameFinalSRV().ptr, ImVec2(renderW, renderH));

	// Project to Scene Drop
	if (ImGui::BeginDragDropTarget()) {
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("PROJECT_ASSET")) {
			std::string sPath = (const char*)payload->Data;
			std::replace(sPath.begin(), sPath.end(), '\\', '/'); // 正規化
			if (sPath.find(".obj") != std::string::npos || sPath.find(".fbx") != std::string::npos) {
				auto e = gameScene->CreateEntity(fs::path(sPath).stem().string());
				auto& mr = gameScene->GetRegistry().emplace<MeshRendererComponent>(e);
				mr.modelPath = sPath;
				mr.modelHandle = renderer->LoadObjMesh(sPath);
				mr.texturePath = "Resources/white1x1.png";
				mr.textureHandle = renderer->LoadTexture2D(mr.texturePath);
				gameScene->SetSelectedEntity(e);
			}
		}
		ImGui::EndDragDropTarget();
	}
	// Picking and Gizmo Logic
	bool gizmoHovered = false;
	bool isPlaying = gameScene->GetIsPlaying();
	auto selectedEnt = gameScene->GetSelectedEntity();

	if (!isPlaying && selectedEnt != entt::null && gameScene->GetRegistry().valid(selectedEnt)) {
		auto& tc = gameScene->GetRegistry().get<TransformComponent>(selectedEnt);
		DirectX::XMVECTOR objPos3D = DirectX::XMLoadFloat3(&tc.translate);
		DirectX::XMMATRIX view = gameScene->GetCamera().View();
		DirectX::XMMATRIX proj = gameScene->GetCamera().Proj();
		
		// Project object position to 2D screen coordinate
		DirectX::XMVECTOR projP = DirectX::XMVector3Project(objPos3D, 0, 0, renderW, renderH, 0.0f, 1.0f, proj, view, DirectX::XMMatrixIdentity());
		float px = DirectX::XMVectorGetX(projP) + gameImageMin.x;
		float py = DirectX::XMVectorGetY(projP) + gameImageMin.y;
		float pz = DirectX::XMVectorGetZ(projP);

		if (pz > 0.0f && pz < 1.0f) { // If behind camera or too far, don't draw
			ImDrawList* drawList = ImGui::GetWindowDrawList();
			ImVec2 origin(px, py);
			float gizmoLen = 60.0f;
			float hitRadius = 15.0f;
			ImVec2 mousePos = ImGui::GetMousePos();
			
			// Projected Unit Vectors for X, Y, Z axes
			DirectX::XMVECTOR pX = DirectX::XMVector3Project(DirectX::XMVectorAdd(objPos3D, DirectX::XMVectorSet(1,0,0,0)), 0, 0, renderW, renderH, 0.0f, 1.0f, proj, view, DirectX::XMMatrixIdentity());
			DirectX::XMVECTOR pY = DirectX::XMVector3Project(DirectX::XMVectorAdd(objPos3D, DirectX::XMVectorSet(0,1,0,0)), 0, 0, renderW, renderH, 0.0f, 1.0f, proj, view, DirectX::XMMatrixIdentity());
			DirectX::XMVECTOR pZ = DirectX::XMVector3Project(DirectX::XMVectorAdd(objPos3D, DirectX::XMVectorSet(0,0,1,0)), 0, 0, renderW, renderH, 0.0f, 1.0f, proj, view, DirectX::XMMatrixIdentity());
			
			ImVec2 dirX(DirectX::XMVectorGetX(pX) + gameImageMin.x - px, DirectX::XMVectorGetY(pX) + gameImageMin.y - py);
			ImVec2 dirY(DirectX::XMVectorGetX(pY) + gameImageMin.x - px, DirectX::XMVectorGetY(pY) + gameImageMin.y - py);
			ImVec2 dirZ(DirectX::XMVectorGetX(pZ) + gameImageMin.x - px, DirectX::XMVectorGetY(pZ) + gameImageMin.y - py);
			
			// Normalize for screen drawing
			auto normalize = [](ImVec2 v) { float len = std::sqrt(v.x*v.x + v.y*v.y); if (len>0) { v.x/=len; v.y/=len; } return v; };
			dirX = normalize(dirX); dirY = normalize(dirY); dirZ = normalize(dirZ);

			ImVec2 endX(origin.x + dirX.x * gizmoLen, origin.y + dirX.y * gizmoLen);
			ImVec2 endY(origin.x + dirY.x * gizmoLen, origin.y + dirY.y * gizmoLen);
			ImVec2 endZ(origin.x + dirZ.x * gizmoLen, origin.y + dirZ.y * gizmoLen);

			auto distToSegment = [](ImVec2 p, ImVec2 a, ImVec2 b) {
				ImVec2 pa(p.x - a.x, p.y - a.y), ba(b.x - a.x, b.y - a.y);
				float h = std::clamp((pa.x*ba.x + pa.y*ba.y) / (ba.x*ba.x + ba.y*ba.y), 0.0f, 1.0f);
				float dx = pa.x - ba.x * h, dy = pa.y - ba.y * h;
				return std::sqrt(dx*dx + dy*dy);
			};

			int hoveredAxis = -1;
			if (!gizmoDragging) {
				if (distToSegment(mousePos, origin, endX) < hitRadius) hoveredAxis = 0;
				else if (distToSegment(mousePos, origin, endY) < hitRadius) hoveredAxis = 1;
				else if (distToSegment(mousePos, origin, endZ) < hitRadius) hoveredAxis = 2;
			} else {
				hoveredAxis = gizmoDragAxis;
			}

			if (hoveredAxis != -1) gizmoHovered = true;

			// Handle input
			if (gizmoHovered && ImGui::IsMouseClicked(0)) {
				gizmoDragging = true;
				gizmoDragAxis = hoveredAxis;
				gizmoDragStartMouse = mousePos;
				gizmoStartTranslate = tc.translate;
				gizmoStartRotate = tc.rotate;
				gizmoStartScale = tc.scale;
			}

			if (gizmoDragging) {
				if (ImGui::IsMouseReleased(0)) {
					// Undo support
					DirectX::XMFLOAT3 endT = tc.translate;
					DirectX::XMFLOAT3 endR = tc.rotate;
					DirectX::XMFLOAT3 endS = tc.scale;
					DirectX::XMFLOAT3 startT = gizmoStartTranslate;
					DirectX::XMFLOAT3 startR = gizmoStartRotate;
					DirectX::XMFLOAT3 startS = gizmoStartScale;
					entt::entity e = selectedEnt;

					PushUndo({
						"Transform",
						[=, &reg = gameScene->GetRegistry()]() {
							if (reg.valid(e)) {
								auto& t = reg.get<TransformComponent>(e);
								t.translate = startT; t.rotate = startR; t.scale = startS;
							}
						},
						[=, &reg = gameScene->GetRegistry()]() {
							if (reg.valid(e)) {
								auto& t = reg.get<TransformComponent>(e);
								t.translate = endT; t.rotate = endR; t.scale = endS;
							}
						}
					});

					gizmoDragging = false;
					gizmoDragAxis = -1;
				} else if (ImGui::IsMouseDragging(0)) {
					ImVec2 delta = ImGui::GetMouseDragDelta(0);
					ImGui::ResetMouseDragDelta(0);
					
					ImVec2 axisDir = (gizmoDragAxis == 0) ? dirX : ((gizmoDragAxis == 1) ? dirY : dirZ);
					float projMovement = delta.x * axisDir.x + delta.y * axisDir.y;
					
					// Simple sensitivity heuristic
					float sensitivity = (currentGizmoMode == GizmoMode::Rotate) ? 0.02f : 0.05f;
					if (currentGizmoMode == GizmoMode::Scale) sensitivity = 0.01f;

					if (currentGizmoMode == GizmoMode::Translate) {
						if (gizmoDragAxis == 0) tc.translate.x += projMovement * sensitivity;
						if (gizmoDragAxis == 1) tc.translate.y -= projMovement * sensitivity; // Screen Y is inverted
						if (gizmoDragAxis == 2) tc.translate.z += projMovement * sensitivity;
					} else if (currentGizmoMode == GizmoMode::Rotate) {
						if (gizmoDragAxis == 0) tc.rotate.x += projMovement * sensitivity;
						if (gizmoDragAxis == 1) tc.rotate.y -= projMovement * sensitivity;
						if (gizmoDragAxis == 2) tc.rotate.z += projMovement * sensitivity;
					} else if (currentGizmoMode == GizmoMode::Scale) {
						if (gizmoDragAxis == 0) tc.scale.x += projMovement * sensitivity;
						if (gizmoDragAxis == 1) tc.scale.y -= projMovement * sensitivity;
						if (gizmoDragAxis == 2) tc.scale.z += projMovement * sensitivity;
					}
				}
			}

			// Draw
			ImU32 colX = (hoveredAxis == 0) ? IM_COL32(255,255,0,255) : IM_COL32(255,50,50,255);
			ImU32 colY = (hoveredAxis == 1) ? IM_COL32(255,255,0,255) : IM_COL32(50,255,50,255);
			ImU32 colZ = (hoveredAxis == 2) ? IM_COL32(255,255,0,255) : IM_COL32(50,50,255,255);

			drawList->AddLine(origin, endX, colX, 3.0f);
			drawList->AddLine(origin, endY, colY, 3.0f);
			drawList->AddLine(origin, endZ, colZ, 3.0f);
			
			if (currentGizmoMode == GizmoMode::Translate) {
				drawList->AddTriangleFilled(endX, ImVec2(endX.x - dirX.x*10 - dirX.y*5, endX.y - dirX.y*10 + dirX.x*5), ImVec2(endX.x - dirX.x*10 + dirX.y*5, endX.y - dirX.y*10 - dirX.x*5), colX);
				drawList->AddTriangleFilled(endY, ImVec2(endY.x - dirY.x*10 - dirY.y*5, endY.y - dirY.y*10 + dirY.x*5), ImVec2(endY.x - dirY.x*10 + dirY.y*5, endY.y - dirY.y*10 - dirY.x*5), colY);
				drawList->AddTriangleFilled(endZ, ImVec2(endZ.x - dirZ.x*10 - dirZ.y*5, endZ.y - dirZ.y*10 + dirZ.x*5), ImVec2(endZ.x - dirZ.x*10 + dirZ.y*5, endZ.y - dirZ.y*10 - dirZ.x*5), colZ);
			} else if (currentGizmoMode == GizmoMode::Scale) {
				drawList->AddRectFilled(ImVec2(endX.x-4, endX.y-4), ImVec2(endX.x+4, endX.y+4), colX);
				drawList->AddRectFilled(ImVec2(endY.x-4, endY.y-4), ImVec2(endY.x+4, endY.y+4), colY);
				drawList->AddRectFilled(ImVec2(endZ.x-4, endZ.y-4), ImVec2(endZ.x+4, endZ.y+4), colZ);
			} else { // Rotate
				drawList->AddCircle(origin, gizmoLen, IM_COL32(200,200,200,100), 32, 1.0f);
			}
		}
	}

	// Hotkeys for Gizmo - Also disabled during PLAY
	if (!isPlaying && ImGui::IsWindowFocused() && !ImGui::IsAnyItemActive()) {
		if (ImGui::IsKeyPressed(ImGuiKey_W)) currentGizmoMode = GizmoMode::Translate;
		if (ImGui::IsKeyPressed(ImGuiKey_E)) currentGizmoMode = GizmoMode::Rotate;
		if (ImGui::IsKeyPressed(ImGuiKey_R)) currentGizmoMode = GizmoMode::Scale;
	}

	// Scene Picking - Also disabled during PLAY (optional, but requested "nothing can be done")
	if (!isPlaying && ImGui::IsItemHovered() && !gizmoHovered) {
		if (ImGui::IsMouseClicked(0)) {
			ImVec2 mousePos = ImGui::GetMousePos();
			float sx = mousePos.x - gameImageMin.x;
			float sy = mousePos.y - gameImageMin.y;
			
			if (sx >= 0 && sx <= renderW && sy >= 0 && sy <= renderH) {
				DirectX::XMVECTOR rayOrig, rayDir;
				ScreenToWorldRay(sx, sy, (float)renderW, (float)renderH, gameScene->GetCamera().View(), gameScene->GetCamera().Proj(), rayOrig, rayDir);
				
				float minD = FLT_MAX;
				entt::entity hitE = entt::null;
				auto v = gameScene->GetRegistry().view<MeshRendererComponent, TransformComponent>();
				for (auto e : v) {
					auto& mr = v.get<MeshRendererComponent>(e);
					if (mr.modelHandle == 0) continue;
					auto* m = renderer->GetModel(mr.modelHandle);
					if (!m) continue;

					float d; Engine::Vector3 p;
					// ロックされているオブジェクトはピッキング（クリック選択）させない
					if (auto* esc = gameScene->GetRegistry().try_get<EditorStateComponent>(e)) {
						if (esc->locked) continue;
					}

					if (m->RayCast(rayOrig, rayDir, gameScene->GetWorldMatrix(static_cast<int>(e)), d, p)) {
						if (d < minD) { minD = d; hitE = e; }
					}
				}
				if (hitE != entt::null) {
					gameScene->SetSelectedEntity(hitE);
				} else {
					gameScene->SetSelectedEntity(entt::null);
				}
			}
		}
	}

	// -- Overlay removed (Clean Viewport) --
	ImGui::End();
	ImGui::PopStyleVar();

	ImGui::End(); // EditorMain
}

void EditorUI::ShowHierarchy(GameScene* scene) {
	ImGui::Begin("Hierarchy");
	if (scene) {
		auto& registry = scene->GetRegistry();
		
		auto view = registry.view<NameComponent>();
		for (auto entity : view) {
			ImGui::PushID((int)entity);
			
			// Icons
			auto& esc = registry.get_or_emplace<EditorStateComponent>(entity);
			if (ImGui::SmallButton(esc.locked ? "L" : "U")) {
				esc.locked = !esc.locked;
			}
			ImGui::SameLine();

			bool isUI = registry.any_of<UIImageComponent, UITextComponent, UIButtonComponent, WorldSpaceUIComponent>(entity);
			if (isUI) ImGui::TextColored(ImVec4(0.4f, 0.6f, 1.0f, 1), "U");
			else ImGui::TextDisabled(" ");
			ImGui::SameLine();

			bool selected = (scene->GetSelectedEntity() == entity);
			std::string name = view.get<NameComponent>(entity).name;
			if (ImGui::Selectable((name + "##" + std::to_string((uint32_t)entity)).c_str(), selected)) {
				scene->SetSelectedEntity(entity);
				scene->GetSelectedEntities() = {entity};
			}

			// Right-click menu for a specific entity
			if (ImGui::BeginPopupContextItem()) {
				if (ImGui::MenuItem("Delete")) {
					uint32_t id = static_cast<uint32_t>(entity);
					std::string snapshot = SerializeEntity(registry, entity);
					PushUndo({"Delete Entity",
						[=, &reg = scene->GetRegistry()](){ 
							// Restore from snapshot
							json j = json::parse("{\"objects\": [" + snapshot + "]}");
							RestoreSceneFromJson(scene, j, true);
						},
						[=](){ scene->DestroyObject(id); }
					});
					scene->DestroyObject(id);
					if (scene->GetSelectedEntity() == entity) scene->SetSelectedEntity(entt::null);
				}
				ImGui::EndPopup();
			}

			ImGui::PopID();
		}

		// Right-click menu for the background (creating new entities)
		// We use ImGuiPopupFlags_MouseButtonRight so it can open even if over an item, 
		// but since BeginPopupContextItem is used above, it'll prioritize the item if clicked on it.
		// Wait, BeginPopupContextWindow overlaps. Let's do a generic context menu at the end.
		if (ImGui::BeginPopupContextWindow("HierarchyContextMenu", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
			if (ImGui::MenuItem("Create Empty")) {
				auto e = scene->CreateEntity("Empty");
				scene->SetSelectedEntity(e);
			}
			if (ImGui::MenuItem("Create Cube")) {
				auto e = scene->CreateEntity("Cube");
				auto& mr = registry.emplace<MeshRendererComponent>(e);
				mr.modelPath = "Resources/cube/cube.obj";
				mr.modelHandle = Engine::Renderer::GetInstance()->LoadObjMesh(mr.modelPath);
				mr.texturePath = "Resources/white1x1.png";
				mr.textureHandle = Engine::Renderer::GetInstance()->LoadTexture2D(mr.texturePath);
				scene->SetSelectedEntity(e);
			}
			if (ImGui::MenuItem("Create Sphere")) {
				auto e = scene->CreateEntity("Sphere");
				auto& mr = registry.emplace<MeshRendererComponent>(e);
				mr.modelPath = "Resources/sphere/sphere.obj";
				mr.modelHandle = Engine::Renderer::GetInstance()->LoadObjMesh(mr.modelPath);
				mr.texturePath = "Resources/white1x1.png";
				mr.textureHandle = Engine::Renderer::GetInstance()->LoadTexture2D(mr.texturePath);
				scene->SetSelectedEntity(e);
			}
			ImGui::EndPopup();
		}

		// ImGui workaround for NoOpenOverItems: if the user clicks below the items, 
		// the window background receives the click.
		// Let's add an invisible button that takes up the remaining space to ensure
		// there is always a clickable area for the context menu.
		ImGui::InvisibleButton("HierarchyContextMenuArea", ImGui::GetContentRegionAvail());
		if (ImGui::BeginPopupContextItem("HierarchyContextMenuArea_Context")) {
			if (ImGui::MenuItem("Create Empty")) {
				auto e = scene->CreateEntity("Empty");
				scene->SetSelectedEntity(e);
			}
			if (ImGui::MenuItem("Create Cube")) {
				auto e = scene->CreateEntity("Cube");
				auto& mr = registry.emplace<MeshRendererComponent>(e);
				mr.modelPath = "Resources/cube/cube.obj";
				mr.modelHandle = Engine::Renderer::GetInstance()->LoadObjMesh(mr.modelPath);
				mr.texturePath = "Resources/white1x1.png";
				mr.textureHandle = Engine::Renderer::GetInstance()->LoadTexture2D(mr.texturePath);
				scene->SetSelectedEntity(e);
			}
			ImGui::EndPopup();
		}
	}
	ImGui::End();
}

void EditorUI::ShowInspector(GameScene* scene) {
	// Removed ImGui::Begin("Inspector") to support tab embedding
	auto selected = scene->GetSelectedEntity();
	if (scene && selected != entt::null && scene->GetRegistry().valid(selected)) {
		auto entity = selected;
		auto& registry = scene->GetRegistry();

		if (auto* nc = registry.try_get<NameComponent>(entity)) {
			char buf[256]; strcpy_s(buf, nc->name.c_str());
			ImGui::SetNextItemWidth(-130);
			if (ImGui::InputText("##Name", buf, sizeof(buf))) nc->name = buf;
			ImGui::SameLine();
			auto& esc = registry.get_or_emplace<EditorStateComponent>(entity);
			ImGui::Checkbox("Locked", &esc.locked);
		}
		ImGui::TextDisabled("ID: %d", (uint32_t)entity);
		ImGui::SameLine();
		if (ImGui::Button("Save Prefab")) {
			std::string name = (registry.all_of<NameComponent>(entity) ? registry.get<NameComponent>(entity).name : "object");
			std::string p = "Resources/" + name + ".prefab";
			std::string fullPath = GetUnifiedProjectPath(p);
			std::ofstream f(fullPath);
			if (f.is_open()) {
				f << "{\n  \"prefab\":\n";
				f << SerializeEntity(registry, entity);
				f << "\n}\n";
				f.close();
				Log("Prefab saved: " + p);
			} else {
				LogError("Failed to save prefab: " + fullPath);
			}
		}

		if (auto* tc = registry.try_get<TransformComponent>(entity)) {
			ImGui::Separator();
			ImGui::Text("Transform");
			if (ImGui::DragFloat3("Pos", &tc->translate.x, 0.1f)) {}
			if (ImGui::IsItemActivated()) s_inspectorStartTranslate = tc->translate;
			if (ImGui::IsItemDeactivatedAfterEdit()) {
				DirectX::XMFLOAT3 start = s_inspectorStartTranslate;
				DirectX::XMFLOAT3 end = tc->translate;
				entt::entity e = entity;
				PushUndo({"Change Position",
					[=, &reg = scene->GetRegistry()](){ if(reg.valid(e)) reg.get<TransformComponent>(e).translate = start; },
					[=, &reg = scene->GetRegistry()](){ if(reg.valid(e)) reg.get<TransformComponent>(e).translate = end; }
				});
			}

			if (ImGui::DragFloat3("Rot", &tc->rotate.x, 0.01f)) {}
			if (ImGui::IsItemActivated()) s_inspectorStartRotate = tc->rotate;
			if (ImGui::IsItemDeactivatedAfterEdit()) {
				DirectX::XMFLOAT3 start = s_inspectorStartRotate;
				DirectX::XMFLOAT3 end = tc->rotate;
				entt::entity e = entity;
				PushUndo({"Change Rotation",
					[=, &reg = scene->GetRegistry()](){ if(reg.valid(e)) reg.get<TransformComponent>(e).rotate = start; },
					[=, &reg = scene->GetRegistry()](){ if(reg.valid(e)) reg.get<TransformComponent>(e).rotate = end; }
				});
			}

			if (ImGui::DragFloat3("Scale", &tc->scale.x, 0.01f)) {}
			if (ImGui::IsItemActivated()) s_inspectorStartScale = tc->scale;
			if (ImGui::IsItemDeactivatedAfterEdit()) {
				DirectX::XMFLOAT3 start = s_inspectorStartScale;
				DirectX::XMFLOAT3 end = tc->scale;
				entt::entity e = entity;
				PushUndo({"Change Scale",
					[=, &reg = scene->GetRegistry()](){ if(reg.valid(e)) reg.get<TransformComponent>(e).scale = start; },
					[=, &reg = scene->GetRegistry()](){ if(reg.valid(e)) reg.get<TransformComponent>(e).scale = end; }
				});
			}
		}

		ImGui::Separator();
		if (ImGui::CollapsingHeader("Components", ImGuiTreeNodeFlags_DefaultOpen)) {
			if (auto* cp = registry.try_get<MeshRendererComponent>(selected)) {
				if (ImGui::CollapsingHeader("Mesh Renderer", ImGuiTreeNodeFlags_DefaultOpen)) {
					ImGui::Checkbox("Enabled##MR", &cp->enabled);
					
					char modelBuf[256]; strcpy_s(modelBuf, cp->modelPath.c_str());
					if (ImGui::InputText("Model Path", modelBuf, sizeof(modelBuf))) {
						cp->modelPath = modelBuf;
						cp->modelHandle = Engine::Renderer::GetInstance()->LoadObjMesh(cp->modelPath);
					}
					if (ImGui::BeginDragDropTarget()) {
						if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("PROJECT_ASSET")) {
							std::string path = (const char*)payload->Data;
							std::replace(path.begin(), path.end(), '\\', '/'); // 正規化
							if (path.find(".obj") != std::string::npos || path.find(".fbx") != std::string::npos || path.find(".gltf") != std::string::npos) {
								cp->modelPath = path;
								cp->modelHandle = Engine::Renderer::GetInstance()->LoadObjMesh(cp->modelPath);
							}
						}
						ImGui::EndDragDropTarget();
					}

					char texBuf[256]; strcpy_s(texBuf, cp->texturePath.c_str());
					if (ImGui::InputText("Texture Path", texBuf, sizeof(texBuf))) {
						cp->texturePath = texBuf;
						cp->textureHandle = Engine::Renderer::GetInstance()->LoadTexture2D(cp->texturePath);
					}
					if (ImGui::BeginDragDropTarget()) {
						if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("PROJECT_ASSET")) {
							std::string path = (const char*)payload->Data;
							std::replace(path.begin(), path.end(), '\\', '/'); // 正規化
							if (path.find(".png") != std::string::npos || path.find(".jpg") != std::string::npos) {
								cp->texturePath = path;
								cp->textureHandle = Engine::Renderer::GetInstance()->LoadTexture2D(cp->texturePath);
							}
						}
						ImGui::EndDragDropTarget();
					}
					ImGui::ColorEdit4("Base Color", &cp->color.x);
					ImGui::DragFloat2("UV Tiling", &cp->uvTiling.x, 0.01f);
					ImGui::DragFloat2("UV Offset", &cp->uvOffset.x, 0.01f);
					if (ImGui::Button("Remove##MR")) registry.remove<MeshRendererComponent>(entity);
				}
			}
			if (auto* bc = registry.try_get<BoxColliderComponent>(entity)) {
				if (ImGui::CollapsingHeader("BoxCollider", ImGuiTreeNodeFlags_DefaultOpen)) {
					ImGui::Checkbox("Enabled##BC", &bc->enabled);
					ImGui::DragFloat3("Center", &bc->center.x, 0.1f);
					ImGui::DragFloat3("Size", &bc->size.x, 0.1f);
					ImGui::Checkbox("Is Trigger", &bc->isTrigger);
					if (ImGui::Button("Remove##BC")) registry.remove<BoxColliderComponent>(entity);
				}
			}
			if (auto* gmc = registry.try_get<GpuMeshColliderComponent>(entity)) {
				if (ImGui::CollapsingHeader("GpuMeshCollider", ImGuiTreeNodeFlags_DefaultOpen)) {
					ImGui::Checkbox("Enabled##GMC", &gmc->enabled);
					ImGui::Text("Mesh Path: %s", gmc->meshPath.c_str());
					if (ImGui::BeginDragDropTarget()) {
						if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("PROJECT_ASSET")) {
							std::string path = (const char*)payload->Data;
							std::replace(path.begin(), path.end(), '\\', '/'); // 正規化
							if (path.find(".obj") != std::string::npos || path.find(".fbx") != std::string::npos) {
								gmc->meshPath = path;
								gmc->meshHandle = Engine::Renderer::GetInstance()->LoadObjMesh(gmc->meshPath);
							}
						}
						ImGui::EndDragDropTarget();
					}
					ImGui::Checkbox("Is Trigger", &gmc->isTrigger);
					if (ImGui::Button("Remove##GMC")) registry.remove<GpuMeshColliderComponent>(entity);
				}
			}
			if (auto* rb = registry.try_get<RigidbodyComponent>(entity)) {
				if (ImGui::CollapsingHeader("Rigidbody", ImGuiTreeNodeFlags_DefaultOpen)) {
					ImGui::Checkbox("Enabled##RB", &rb->enabled);
					ImGui::DragFloat3("Velocity", &rb->velocity.x, 0.1f);
					ImGui::Checkbox("Use Gravity", &rb->useGravity);
					ImGui::Checkbox("Is Kinematic", &rb->isKinematic);
					if (ImGui::Button("Remove##RB")) registry.remove<RigidbodyComponent>(entity);
				}
			}
			if (auto* tag = registry.try_get<TagComponent>(entity)) {
				if (ImGui::CollapsingHeader("Tag", ImGuiTreeNodeFlags_DefaultOpen)) {
					ImGui::Checkbox("Enabled##Tag", &tag->enabled);
					char buf[256]; strcpy_s(buf, tag->tag.c_str());
					if (ImGui::InputText("Tag##TagInput", buf, sizeof(buf))) tag->tag = buf;
					if (ImGui::Button("Remove##Tag")) registry.remove<TagComponent>(entity);
				}
			}
			if (auto* as = registry.try_get<AudioSourceComponent>(entity)) {
				if (ImGui::CollapsingHeader("AudioSource", ImGuiTreeNodeFlags_DefaultOpen)) {
					ImGui::Checkbox("Enabled##AS", &as->enabled);
					ImGui::Text("File: %s", as->soundPath.c_str());
					ImGui::DragFloat("Volume", &as->volume, 0.01f, 0, 1);
					ImGui::Checkbox("Loop", &as->loop);
					ImGui::Checkbox("Play on Start", &as->playOnStart);
					ImGui::Checkbox("Is 3D", &as->is3D);
					if (ImGui::Button("Remove##AS")) registry.remove<AudioSourceComponent>(entity);
				}
			}
			if (auto* hp = registry.try_get<HealthComponent>(entity)) {
				if (ImGui::CollapsingHeader("Health", ImGuiTreeNodeFlags_DefaultOpen)) {
					ImGui::Checkbox("Enabled##HP", &hp->enabled);
					ImGui::DragFloat("HP", &hp->hp, 1.0f, 0, hp->maxHp);
					ImGui::DragFloat("Max HP", &hp->maxHp, 1.0f, 1, 10000);
					ImGui::Checkbox("Is Dead", &hp->isDead);
					if (ImGui::Button("Remove##HP")) registry.remove<HealthComponent>(entity);
				}
			}
			if (auto* cp = registry.try_get<ScriptComponent>(selected)) {
				if (ImGui::CollapsingHeader("Scripts", ImGuiTreeNodeFlags_DefaultOpen)) {
					ImGui::Checkbox("Enabled##Scripts", &cp->enabled);
					
					auto scriptNames = ScriptEngine::GetInstance()->GetRegisteredScriptNames();

					for (int i = 0; i < (int)cp->scripts.size(); ++i) {
						auto& entry = cp->scripts[i];
						ImGui::PushID(i);
						std::string treeLabel = (entry.scriptPath.empty() ? "None" : entry.scriptPath) + "##Tree";
						if (ImGui::TreeNodeEx(treeLabel.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
							int current = -1;
							for (int j = 0; j < (int)scriptNames.size(); ++j) {
								if (scriptNames[j] == entry.scriptPath) { current = j; break; }
							}

							std::string comboName = entry.scriptPath.empty() ? "None" : entry.scriptPath;
							if (ImGui::BeginCombo("Class", comboName.c_str())) {
								for (int j = 0; j < (int)scriptNames.size(); ++j) {
									bool isSelected = (current == j);
									if (ImGui::Selectable(scriptNames[j].c_str(), isSelected)) {
										if (entry.scriptPath != scriptNames[j]) {
											entry.scriptPath = scriptNames[j];
											entry.instance = nullptr;
											entry.parameterData = "{}"; // Reset parameters for new script type
										}
									}
								}
								ImGui::EndCombo();
							}

							if (!entry.instance && !entry.scriptPath.empty()) {
								entry.instance = ScriptEngine::GetInstance()->CreateScript(entry.scriptPath);
								if (entry.instance && !entry.parameterData.empty()) {
									entry.instance->DeserializeParameters(entry.parameterData);
								}
							}

							if (entry.instance) {
								ImGui::PushID("ScriptEditor");
								entry.instance->OnEditorUI();
								// エディタでの変更を即座に文字列パラメータに反映 (同期漏れ防止)
								// ただし PLAY 中は実行状態を書き戻さないようにガード
								if (!scene->GetIsPlaying()) {
									entry.parameterData = entry.instance->SerializeParameters();
								}
								ImGui::PopID();
							}

							if (ImGui::Button("Remove Script")) {
								cp->scripts.erase(cp->scripts.begin() + i);
								// i-- is not needed if we break or handle loop index correctly, but let's be safe
								ImGui::TreePop();
								ImGui::PopID();
								break; 
							}
							ImGui::TreePop();
						}
						ImGui::PopID();
						ImGui::Separator();
					}

					if (ImGui::Button("Add Script")) {
						cp->scripts.push_back({});
					}
					ImGui::SameLine();
					if (ImGui::Button("Remove Component##SC")) registry.remove<ScriptComponent>(entity);
				}
			}
			if (auto* dl = registry.try_get<DirectionalLightComponent>(entity)) {
				if (ImGui::CollapsingHeader("DirectionalLight", ImGuiTreeNodeFlags_DefaultOpen)) {
					ImGui::Checkbox("Enabled##DL", &dl->enabled);
					ImGui::ColorEdit3("Light Color", &dl->color.x);
					ImGui::DragFloat("Intensity", &dl->intensity, 0.1f, 0, 100);
					if (ImGui::Button("Remove##DL")) registry.remove<DirectionalLightComponent>(entity);
				}
			}
			if (auto* pl = registry.try_get<PointLightComponent>(entity)) {
				if (ImGui::CollapsingHeader("PointLight", ImGuiTreeNodeFlags_DefaultOpen)) {
					ImGui::Checkbox("Enabled##PL", &pl->enabled);
					ImGui::ColorEdit3("Light Color", &pl->color.x);
					ImGui::DragFloat("Intensity", &pl->intensity, 0.1f, 0, 100);
					ImGui::DragFloat("Range", &pl->range, 0.1f, 0, 1000);
					if (ImGui::Button("Remove##PL")) registry.remove<PointLightComponent>(entity);
				}
			}
			if (auto* pe = registry.try_get<ParticleEmitterComponent>(entity)) {
				if (ImGui::CollapsingHeader("ParticleEmitter", ImGuiTreeNodeFlags_DefaultOpen)) {
					ImGui::Checkbox("Enabled##PE", &pe->enabled);
					ImGui::Text("Asset: %s", pe->assetPath.c_str());
					if (ImGui::Button("Remove##PE")) registry.remove<ParticleEmitterComponent>(entity);
				}
			}
			if (auto* pi = registry.try_get<PlayerInputComponent>(entity)) {
				if (ImGui::CollapsingHeader("PlayerInput", ImGuiTreeNodeFlags_DefaultOpen)) {
					ImGui::Checkbox("Enabled##PI", &pi->enabled);
					ImGui::Text("MoveDir: (%.2f, %.2f)", pi->moveDir.x, pi->moveDir.y);
					if (ImGui::Button("Remove##PI")) registry.remove<PlayerInputComponent>(entity);
				}
			}
			if (auto* cm = registry.try_get<CharacterMovementComponent>(entity)) {
				if (ImGui::CollapsingHeader("CharacterMovement", ImGuiTreeNodeFlags_DefaultOpen)) {
					ImGui::Checkbox("Enabled##CM", &cm->enabled);
					ImGui::DragFloat("Speed", &cm->speed, 0.1f);
					ImGui::DragFloat("Jump Power", &cm->jumpPower, 0.1f);
					ImGui::DragFloat("Gravity", &cm->gravity, 0.1f);
					if (ImGui::Button("Remove##CM")) registry.remove<CharacterMovementComponent>(entity);
				}
			}
			if (auto* sl = registry.try_get<SpotLightComponent>(entity)) {
				if (ImGui::CollapsingHeader("SpotLight", ImGuiTreeNodeFlags_DefaultOpen)) {
					ImGui::Checkbox("Enabled##SL", &sl->enabled);
					ImGui::ColorEdit3("Color", &sl->color.x);
					ImGui::DragFloat("Intensity", &sl->intensity, 0.1f);
					ImGui::DragFloat("Range", &sl->range, 0.1f);
					if (ImGui::Button("Remove##SL")) registry.remove<SpotLightComponent>(entity);
				}
			}
			if (auto* rt = registry.try_get<RectTransformComponent>(entity)) {
				if (ImGui::CollapsingHeader("RectTransform", ImGuiTreeNodeFlags_DefaultOpen)) {
					ImGui::Checkbox("Enabled##RT", &rt->enabled);
					ImGui::DragFloat2("Pos", &rt->pos.x);
					ImGui::DragFloat2("Size", &rt->size.x);
					ImGui::DragFloat2("Anchor", &rt->anchor.x, 0.01f, 0, 1);
					if (ImGui::Button("Remove##RT")) registry.remove<RectTransformComponent>(entity);
				}
			}
			if (auto* img = registry.try_get<UIImageComponent>(entity)) {
				if (ImGui::CollapsingHeader("UIImage", ImGuiTreeNodeFlags_DefaultOpen)) {
					ImGui::Checkbox("Enabled##IMG", &img->enabled);
					ImGui::Text("Texture: %s", img->texturePath.c_str());
					ImGui::ColorEdit4("Color", &img->color.x);
					if (ImGui::Button("Remove##IMG")) registry.remove<UIImageComponent>(entity);
				}
			}
			if (auto* txt = registry.try_get<UITextComponent>(entity)) {
				if (ImGui::CollapsingHeader("UIText", ImGuiTreeNodeFlags_DefaultOpen)) {
					ImGui::Checkbox("Enabled##TXT", &txt->enabled);
					char tbuf[256]; strcpy_s(tbuf, txt->text.c_str());
					if (ImGui::InputText("Text", tbuf, sizeof(tbuf))) txt->text = tbuf;
					ImGui::DragFloat("Font Size", &txt->fontSize, 1.0f, 1, 100);
					if (ImGui::Button("Remove##TXT")) registry.remove<UITextComponent>(entity);
				}
			}
			if (auto* btn = registry.try_get<UIButtonComponent>(entity)) {
				if (ImGui::CollapsingHeader("UIButton", ImGuiTreeNodeFlags_DefaultOpen)) {
					ImGui::Checkbox("Enabled##BTN", &btn->enabled);
					ImGui::Text("Hovered: %s", btn->isHovered ? "Yes" : "No");
					if (ImGui::Button("Remove##BTN")) registry.remove<UIButtonComponent>(entity);
				}
			}
			if (auto* riv = registry.try_get<RiverComponent>(entity)) {
				if (ImGui::CollapsingHeader("River", ImGuiTreeNodeFlags_DefaultOpen)) {
					ImGui::Checkbox("Enabled##RIV", &riv->enabled);
					ImGui::DragFloat("Width", &riv->width, 0.1f);
					ImGui::DragFloat("Speed", &riv->flowSpeed, 0.1f);
					if (ImGui::Button("Remove##RIV")) registry.remove<RiverComponent>(entity);
				}
			}
			if (auto* anim = registry.try_get<AnimatorComponent>(entity)) {
				if (ImGui::CollapsingHeader("Animator", ImGuiTreeNodeFlags_DefaultOpen)) {
					ImGui::Checkbox("Enabled##ANIM", &anim->enabled);
					ImGui::Text("Animation: %s", anim->currentAnimation.c_str());
					ImGui::DragFloat("Speed", &anim->speed, 0.1f);
					ImGui::Checkbox("Playing", &anim->isPlaying);
					ImGui::Checkbox("Loop", &anim->loop);
					if (ImGui::Button("Remove##ANIM")) registry.remove<AnimatorComponent>(entity);
				}
			}
			if (auto* ct = registry.try_get<CameraTargetComponent>(entity)) {
				if (ImGui::CollapsingHeader("CameraTarget", ImGuiTreeNodeFlags_DefaultOpen)) {
					ImGui::Checkbox("Enabled##CT", &ct->enabled);
					ImGui::DragFloat("Dist", &ct->distance, 0.1f);
					ImGui::DragFloat("Height", &ct->height, 0.1f);
					ImGui::DragFloat("Smooth", &ct->smoothSpeed, 0.1f);
					if (ImGui::Button("Remove##CT")) registry.remove<CameraTargetComponent>(entity);
				}
			}
			if (auto* hb = registry.try_get<HitboxComponent>(entity)) {
				if (ImGui::CollapsingHeader("Hitbox", ImGuiTreeNodeFlags_DefaultOpen)) {
					ImGui::Checkbox("Enabled##HB", &hb->enabled);
					ImGui::DragFloat3("Ofs", &hb->center.x, 0.1f);
					ImGui::DragFloat3("Size", &hb->size.x, 0.1f);
					ImGui::DragFloat("DMG", &hb->damage, 1.0f);
					ImGui::Checkbox("Active", &hb->isActive);
					if (ImGui::Button("Remove##HB")) registry.remove<HitboxComponent>(entity);
				}
			}
			if (auto* hb = registry.try_get<HurtboxComponent>(entity)) {
				if (ImGui::CollapsingHeader("Hurtbox", ImGuiTreeNodeFlags_DefaultOpen)) {
					ImGui::Checkbox("Enabled##HurtB", &hb->enabled);
					ImGui::DragFloat3("Ofs", &hb->center.x, 0.1f);
					ImGui::DragFloat3("Size", &hb->size.x, 0.1f);
					ImGui::DragFloat("Mult", &hb->damageMultiplier, 0.1f);
					if (ImGui::Button("Remove##HurtB")) registry.remove<HurtboxComponent>(entity);
				}
			}
			if (auto* ws = registry.try_get<WorldSpaceUIComponent>(entity)) {
				if (ImGui::CollapsingHeader("WorldSpaceUI", ImGuiTreeNodeFlags_DefaultOpen)) {
					ImGui::Checkbox("Enabled##WS", &ws->enabled);
					ImGui::Checkbox("HP Bar", &ws->showHealthBar);
					ImGui::DragFloat3("Ofs", &ws->offset.x, 0.1f);
					if (ImGui::Button("Remove##WS")) registry.remove<WorldSpaceUIComponent>(entity);
				}
			}
			if (auto* al = registry.try_get<AudioListenerComponent>(entity)) {
				if (ImGui::CollapsingHeader("AudioListener", ImGuiTreeNodeFlags_DefaultOpen)) {
					ImGui::Checkbox("Enabled##AL", &al->enabled);
					ImGui::Text("Listening...");
					if (ImGui::Button("Remove##AL")) registry.remove<AudioListenerComponent>(entity);
				}
			}
			if (auto* var = registry.try_get<VariableComponent>(entity)) {
				if (ImGui::CollapsingHeader("Variables", ImGuiTreeNodeFlags_DefaultOpen)) {
					ImGui::Checkbox("Enabled##VAR", &var->enabled);
					for (auto& [k, v] : var->values) {
						ImGui::PushID(k.c_str());
						ImGui::DragFloat(k.c_str(), &v, 0.1f);
						ImGui::PopID();
					}
					if (ImGui::Button("Remove##VAR")) registry.remove<VariableComponent>(entity);
				}
			}
		}
		
		if (ImGui::Button("Add Component")) ImGui::OpenPopup("AddComp");
		if (ImGui::BeginPopup("AddComp")) {
			if (ImGui::MenuItem("MeshRenderer")) std::ignore = registry.get_or_emplace<MeshRendererComponent>(entity);
			if (ImGui::MenuItem("BoxCollider")) std::ignore = registry.get_or_emplace<BoxColliderComponent>(entity);
			if (ImGui::MenuItem("Rigidbody")) std::ignore = registry.get_or_emplace<RigidbodyComponent>(entity);
			if (ImGui::MenuItem("GpuMeshCollider")) {
				auto& gmc = registry.get_or_emplace<GpuMeshColliderComponent>(entity);
				if (auto* mr = registry.try_get<MeshRendererComponent>(entity)) {
					gmc.meshPath = mr->modelPath;
					gmc.meshHandle = mr->modelHandle;
				}
			}
			if (ImGui::MenuItem("AudioSource")) std::ignore = registry.get_or_emplace<AudioSourceComponent>(entity);
			if (ImGui::MenuItem("Health")) std::ignore = registry.get_or_emplace<HealthComponent>(entity);
			if (ImGui::MenuItem("Tag")) std::ignore = registry.get_or_emplace<TagComponent>(entity);
			if (ImGui::MenuItem("Script")) std::ignore = registry.get_or_emplace<ScriptComponent>(entity);
			if (ImGui::MenuItem("DirectionalLight")) std::ignore = registry.get_or_emplace<DirectionalLightComponent>(entity);
			if (ImGui::MenuItem("PointLight")) std::ignore = registry.get_or_emplace<PointLightComponent>(entity);
			if (ImGui::MenuItem("SpotLight")) std::ignore = registry.get_or_emplace<SpotLightComponent>(entity);
			if (ImGui::MenuItem("ParticleEmitter")) std::ignore = registry.get_or_emplace<ParticleEmitterComponent>(entity);
			if (ImGui::MenuItem("PlayerInput")) std::ignore = registry.get_or_emplace<PlayerInputComponent>(entity);
			if (ImGui::MenuItem("CharacterMovement")) std::ignore = registry.get_or_emplace<CharacterMovementComponent>(entity);
			if (ImGui::MenuItem("RectTransform")) std::ignore = registry.get_or_emplace<RectTransformComponent>(entity);
			if (ImGui::MenuItem("UIImage")) std::ignore = registry.get_or_emplace<UIImageComponent>(entity);
			if (ImGui::MenuItem("UIText")) std::ignore = registry.get_or_emplace<UITextComponent>(entity);
			if (ImGui::MenuItem("UIButton")) std::ignore = registry.get_or_emplace<UIButtonComponent>(entity);
			if (ImGui::MenuItem("River")) std::ignore = registry.get_or_emplace<RiverComponent>(entity);
			if (ImGui::MenuItem("Animator")) std::ignore = registry.get_or_emplace<AnimatorComponent>(entity);
			if (ImGui::MenuItem("CameraTarget")) std::ignore = registry.get_or_emplace<CameraTargetComponent>(entity);
			if (ImGui::MenuItem("Hitbox")) std::ignore = registry.get_or_emplace<HitboxComponent>(entity);
			if (ImGui::MenuItem("Hurtbox")) std::ignore = registry.get_or_emplace<HurtboxComponent>(entity);
			if (ImGui::MenuItem("WorldSpaceUI")) std::ignore = registry.get_or_emplace<WorldSpaceUIComponent>(entity);
			if (ImGui::MenuItem("AudioListener")) std::ignore = registry.get_or_emplace<AudioListenerComponent>(entity);
			if (ImGui::MenuItem("Variables")) std::ignore = registry.get_or_emplace<VariableComponent>(entity);
			ImGui::EndPopup();
		}
	} else {
		ImGui::Text("No Selection");
	}
	// Removed ImGui::End() to support tab embedding
}

void EditorUI::ShowProject([[maybe_unused]] Engine::Renderer* renderer, [[maybe_unused]] GameScene* scene) {
	ImGui::Begin("Project");
	static std::string currentDir = "Resources";
	static char searchBuf[128] = "";
	static std::map<std::string, uint32_t> s_thumbnails; // Static map to store loaded thumbnails

	// Top Bar
	if (ImGui::Button("Back") && currentDir != "Resources") {
		currentDir = fs::path(currentDir).parent_path().string();
	}
	ImGui::SameLine();
	ImGui::Text("Dir: %s", currentDir.c_str());
	ImGui::SameLine(ImGui::GetWindowWidth() - 200);
	ImGui::SetNextItemWidth(150);
	ImGui::InputTextWithHint("##Search", "Search Assets...", searchBuf, sizeof(searchBuf));
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("Filter files in current directory");
	ImGui::SameLine();
	if (ImGui::Button("Clear")) searchBuf[0] = '\0';

	ImGui::Separator();

	// Grid setup
	float padding = 16.0f;
	float thumbnailSize = 64.0f;
	float cellSize = thumbnailSize + padding;
	float panelWidth = ImGui::GetContentRegionAvail().x;
	int columnCount = (int)(panelWidth / cellSize);
	if (columnCount < 1) columnCount = 1;

	ImGui::BeginChild("ScrollingRegion", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
	ImGui::Columns(columnCount, 0, false);

	std::string searchStr = searchBuf;
	std::transform(searchStr.begin(), searchStr.end(), searchStr.begin(), [](unsigned char c) { return (char)std::tolower(c); });

	int i = 0; // Use an integer for PushID to avoid issues with identical filenames in different directories
	for (auto& entry : fs::directory_iterator(currentDir)) {
		std::string name = entry.path().filename().string();
		std::string lowerName = name;
		std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), [](unsigned char c) { return (char)std::tolower(c); });

		if (!searchStr.empty() && lowerName.find(searchStr) == std::string::npos)
			continue;

		ImGui::PushID(i++);
		
		uint32_t icon = s_icons.file;
		if (entry.is_directory()) {
			icon = s_icons.folder;
		} else {
			std::string ext = entry.path().extension().string();
			if (ext == ".obj" || ext == ".fbx" || ext == ".gltf") icon = s_icons.model;
			else if (ext == ".prefab") icon = s_icons.prefab;
			else if (ext == ".wav" || ext == ".mp3") icon = s_icons.audio;
			else if (ext == ".h" || ext == ".cpp" || ext == ".json") icon = s_icons.script;
			else if (ext == ".png" || ext == ".jpg") {
				// Thumbnail attempt
				std::string fullPath = entry.path().string();
				if (s_thumbnails.find(fullPath) == s_thumbnails.end()) {
					s_thumbnails[fullPath] = Engine::Renderer::GetInstance()->LoadTexture2D(fullPath);
				}
				icon = s_thumbnails[fullPath];
			}
		}

		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
		std::string strId = "btn_" + std::to_string(i);
		if (ImGui::ImageButton(strId.c_str(), (ImTextureID)renderer->GetTextureSrvGpu(icon).ptr, ImVec2(thumbnailSize, thumbnailSize))) {
			if (entry.is_directory()) {
				currentDir = entry.path().string();
			} else {
				// Select logic
			}
		}
		ImGui::PopStyleColor();

		if (ImGui::BeginDragDropSource()) {
			std::string path = entry.path().string();
			std::replace(path.begin(), path.end(), '\\', '/'); // パスを正規化
			ImGui::SetDragDropPayload("PROJECT_ASSET", path.c_str(), path.size() + 1);
			ImGui::Text("%s", name.c_str());
			ImGui::EndDragDropSource();
		}

		ImGui::TextWrapped("%s", name.c_str());
		ImGui::NextColumn();
		ImGui::PopID();
	}

	ImGui::Columns(1);
	ImGui::EndChild();
	ImGui::End();
}

void EditorUI::ShowSceneSettings(Engine::Renderer* renderer) {
	// Removed ImGui::Begin("Scene Settings")
	bool en = renderer->GetPostProcessEnabled();
	if (ImGui::Checkbox("Post Process", &en)) renderer->SetPostProcessEnabled(en);
}

void EditorUI::ShowConsole() {
	ImGui::Begin("Console");
	if (ImGui::Button("Clear")) consoleLog.clear();
	for (const auto& log : consoleLog) ImGui::TextUnformatted(log.message.c_str());
	ImGui::End();
}

void EditorUI::DrawSelectionGizmo([[maybe_unused]] Engine::Renderer* renderer, [[maybe_unused]] GameScene* scene) {} // WIP
void EditorUI::ShowAnimationWindow([[maybe_unused]] Engine::Renderer* renderer, [[maybe_unused]] GameScene* scene) { 
	// Removed ImGui::Begin("Animation")
	ImGui::Text("Animation Controls (WIP)");
}
void EditorUI::ShowPlayModeMonitor([[maybe_unused]] GameScene* scene) { 
	ImGui::TextColored(ImVec4(0, 1, 0, 1), "FPS: %.1f", ImGui::GetIO().Framerate);
	ImGui::Separator();
	
	if (scene) {
		auto& reg = scene->GetRegistry();
		auto view = reg.view<PlayerInputComponent, TransformComponent>();
		bool hasPlayer = false;
		view.each([&hasPlayer](auto, auto&, auto&) { hasPlayer = true; });
		if (!hasPlayer) ImGui::TextDisabled("No Player Activity detected...");
		view.each([&](auto e, [[maybe_unused]] auto& pic, auto& tc) {
			std::string name = "Player";
			if (auto* nc = reg.try_get<NameComponent>(e)) name = nc->name;
			
			if (ImGui::CollapsingHeader((name + " Stats").c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
				ImGui::Text("Position: (%.2f, %.2f, %.2f)", tc.translate.x, tc.translate.y, tc.translate.z);
				ImGui::Text("Rotation: (%.2f, %.2f, %.2f)", tc.rotate.x, tc.rotate.y, tc.rotate.z);
				
				if (auto* rb = reg.try_get<RigidbodyComponent>(e)) {
					ImGui::Text("Velocity: (%.2f, %.2f, %.2f)", rb->velocity.x, rb->velocity.y, rb->velocity.z);
					float speed = std::sqrt(rb->velocity.x*rb->velocity.x + rb->velocity.y*rb->velocity.y + rb->velocity.z*rb->velocity.z);
					ImGui::Text("Speed: %.2f", speed);
				}
				if (auto* hp = reg.try_get<HealthComponent>(e)) {
					ImGui::Text("HP: %.0f / %.0f", hp->hp, hp->maxHp);
					ImGui::ProgressBar(hp->hp / hp->maxHp, ImVec2(-1, 0), "Health");
				}
			}
		});
	}
}

void EditorUI::ScreenToWorldRay(float screenX, float screenY, float imageW, float imageH, const DirectX::XMMATRIX& view, const DirectX::XMMATRIX& proj, DirectX::XMVECTOR& outOrig, DirectX::XMVECTOR& outDir) {
	DirectX::XMMATRIX invView = DirectX::XMMatrixInverse(nullptr, view);
	DirectX::XMMATRIX invProj = DirectX::XMMatrixInverse(nullptr, proj);

	float vx = (2.0f * screenX / imageW - 1.0f);
	float vy = (1.0f - 2.0f * screenY / imageH);

	DirectX::XMVECTOR rayEndNDC = DirectX::XMVectorSet(vx, vy, 1.0f, 1.0f);
	DirectX::XMVECTOR rayEndView = DirectX::XMVector3TransformCoord(rayEndNDC, invProj);
	DirectX::XMVECTOR rayEndWorld = DirectX::XMVector3TransformCoord(rayEndView, invView);

	outOrig = invView.r[3];
	outDir = DirectX::XMVector3Normalize(DirectX::XMVectorSubtract(rayEndWorld, outOrig));
}

} // namespace Game
