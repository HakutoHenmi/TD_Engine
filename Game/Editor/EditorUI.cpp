#include "EditorUI.h"
#include "../../Engine/PathUtils.h"
#include "../../externals/imgui/imgui.h"
#include "../../externals/imgui/imgui_internal.h"
#include "../Scenes/GameScene.h"
#include "../Systems/RiverSystem.h" 
#include "../Systems/UISystem.h"    
#include "../Scripts/IScript.h"     
#include "../Scripts/ScriptEngine.h" 
#ifdef _MSC_VER
#pragma warning(disable: 4865)
#endif

#include "Audio.h"
#include <tuple> // 追加
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
#include <Psapi.h>
#include <dxgi1_4.h>
#pragma comment(lib, "Psapi.lib")
#pragma comment(lib, "dxgi.lib")
#include <commdlg.h>
#include "../../Engine/ThirdParty/nlohmann/json.hpp"
using json = nlohmann::json;

namespace Game {
namespace fs = std::filesystem;

std::string EditorUI::currentScenePath = "Resources/Scenes/scene.json";

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
		
		(void)reg.get_or_emplace<HierarchyComponent>(entity);
		uint32_t savedParentId = obj.value("parentId", (uint32_t)entt::null);
		if (savedParentId != (uint32_t)entt::null && idMap.find(savedParentId) != idMap.end()) {
			scene->SetParent(entity, idMap[savedParentId]); // ★修正: SetParent経由で子リストも更新する
		} else {
			scene->RemoveParent(entity);
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
					
					if (comp.contains("extraTexturePaths") && comp["extraTexturePaths"].is_array()) {
						c.extraTexturePaths.clear();
						c.extraTextureHandles.clear();
						for (auto& pathJ : comp["extraTexturePaths"]) {
							std::string path = pathJ.get<std::string>();
							c.extraTexturePaths.push_back(path);
							if (!path.empty()) {
								c.extraTextureHandles.push_back(Engine::Renderer::GetInstance()->LoadTexture2D(path));
							} else {
								c.extraTextureHandles.push_back(0);
							}
						}
					}
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
							
							ScriptEntry entry;
							entry.scriptPath = scriptPath;
							entry.parameterData = paramData;
							entry.instance = ScriptEngine::GetInstance()->CreateScript(scriptPath);
							entry.isStarted = false;
							
							if (entry.instance && !entry.parameterData.empty()) {
								char logBuf2[2048];
								sprintf_s(logBuf2, "[EditorUI] Immediate Restore: Applying params to %s: %s\n", scriptPath.c_str(), paramData.c_str());
								OutputDebugStringA(logBuf2);
								entry.instance->DeserializeParameters(entry.parameterData);
							}
							
							c.scripts.push_back(entry);
						}
					} else {
						// Backward compatibility: old format
						std::string path = comp.value("scriptPath", "");
						if (!path.empty()) {
							std::string paramData = comp.value("parameterData", "");
							ScriptEntry entry;
							entry.scriptPath = path;
							entry.parameterData = paramData;
							entry.instance = ScriptEngine::GetInstance()->CreateScript(path);
							entry.isStarted = false;
							
							if (entry.instance && !entry.parameterData.empty()) {
								entry.instance->DeserializeParameters(entry.parameterData);
							}
							
							c.scripts.push_back(entry);
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
					c.enabled = en; c.tag = StringToTag(comp.value("tag", "Untagged"));
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
					c.uvScale = comp.value("uvScale", 1.0f);
					c.texturePath = comp.value("texturePath", "Resources/Water/water.png");
					if (comp.contains("points") && comp["points"].is_array()) {
						c.points.clear();
						for (auto& pt : comp["points"]) {
							if (pt.is_array() && pt.size() >= 3)
								c.points.push_back({pt[0], pt[1], pt[2]});
						}
					}
				} else if (type == "WorldSpaceUI") {
					auto& c = reg.get_or_emplace<WorldSpaceUIComponent>(entity);
					c.enabled = en; c.showHealthBar = comp.value("showHealthBar", true);
					c.showDamageNumbers = comp.value("showDamageNumbers", true);
					if (comp.contains("offset")) c.offset = {comp["offset"][0], comp["offset"][1], comp["offset"][2]};
					c.barWidth = comp.value("barWidth", 60.0f);
					c.barHeight = comp.value("barHeight", 6.0f);
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
					
					auto& p = c.emitter.params;
					if (comp.contains("emitRate")) p.emitRate = comp["emitRate"].get<float>();
					if (comp.contains("burstCount")) p.burstCount = comp["burstCount"].get<int>();
					if (comp.contains("shape")) p.shape = static_cast<Engine::EmissionShape>(comp["shape"].get<int>());
					if (comp.contains("shapeRadius")) p.shapeRadius = comp["shapeRadius"].get<float>();
					if (comp.contains("shapeAngle")) p.shapeAngle = comp["shapeAngle"].get<float>();
					if (comp.contains("lifeTime")) p.lifeTime = comp["lifeTime"].get<float>();
					if (comp.contains("lifeTimeVariance")) p.lifeTimeVariance = comp["lifeTimeVariance"].get<float>();
					
					if (comp.contains("startVelocity") && comp["startVelocity"].size() >= 3)
						p.startVelocity = {comp["startVelocity"][0], comp["startVelocity"][1], comp["startVelocity"][2]};
					if (comp.contains("velocityVariance") && comp["velocityVariance"].size() >= 3)
						p.velocityVariance = {comp["velocityVariance"][0], comp["velocityVariance"][1], comp["velocityVariance"][2]};
					if (comp.contains("acceleration") && comp["acceleration"].size() >= 3)
						p.acceleration = {comp["acceleration"][0], comp["acceleration"][1], comp["acceleration"][2]};
					if (comp.contains("damping")) p.damping = comp["damping"].get<float>();
					
					if (comp.contains("startSize") && comp["startSize"].size() >= 3)
						p.startSize = {comp["startSize"][0], comp["startSize"][1], comp["startSize"][2]};
					if (comp.contains("startSizeVariance") && comp["startSizeVariance"].size() >= 3)
						p.startSizeVariance = {comp["startSizeVariance"][0], comp["startSizeVariance"][1], comp["startSizeVariance"][2]};
					if (comp.contains("endSize") && comp["endSize"].size() >= 3)
						p.endSize = {comp["endSize"][0], comp["endSize"][1], comp["endSize"][2]};
					if (comp.contains("endSizeVariance") && comp["endSizeVariance"].size() >= 3)
						p.endSizeVariance = {comp["endSizeVariance"][0], comp["endSizeVariance"][1], comp["endSizeVariance"][2]};
						
					if (comp.contains("startColor") && comp["startColor"].size() >= 4)
						p.startColor = {comp["startColor"][0], comp["startColor"][1], comp["startColor"][2], comp["startColor"][3]};
					if (comp.contains("endColor") && comp["endColor"].size() >= 4)
						p.endColor = {comp["endColor"][0], comp["endColor"][1], comp["endColor"][2], comp["endColor"][3]};
						
					if (comp.contains("angularVelocity") && comp["angularVelocity"].size() >= 3)
						p.angularVelocity = {comp["angularVelocity"][0], comp["angularVelocity"][1], comp["angularVelocity"][2]};
					if (comp.contains("angularVelocityVariance") && comp["angularVelocityVariance"].size() >= 3)
						p.angularVelocityVariance = {comp["angularVelocityVariance"][0], comp["angularVelocityVariance"][1], comp["angularVelocityVariance"][2]};
						
					if (comp.contains("texturePath")) p.texturePath = comp["texturePath"].get<std::string>();
					if (comp.contains("shaderName")) p.shaderName = comp["shaderName"].get<std::string>();
					if (comp.contains("useBillboard")) p.useBillboard = comp["useBillboard"].get<bool>();
					if (comp.contains("isAdditive")) p.isAdditive = comp["isAdditive"].get<bool>();
					if (comp.contains("useUvAnim")) p.useUvAnim = comp["useUvAnim"].get<bool>();
					if (comp.contains("uvAnimCols")) p.uvAnimCols = comp["uvAnimCols"].get<int>();
					if (comp.contains("uvAnimRows")) p.uvAnimRows = comp["uvAnimRows"].get<int>();
					if (comp.contains("uvAnimFps")) p.uvAnimFps = comp["uvAnimFps"].get<float>();
					
					c.isInitialized = false; // 強制的に再初期化・適用させる
				} else if (type == "RectTransform") {
					auto& c = reg.get_or_emplace<RectTransformComponent>(entity);
					c.enabled = en; if (comp.contains("pos")) c.pos = {comp["pos"][0], comp["pos"][1]};
					if (comp.contains("size")) c.size = {comp["size"][0], comp["size"][1]};
					if (comp.contains("anchor")) c.anchor = {comp["anchor"][0], comp["anchor"][1]};
					if (comp.contains("pivot")) c.pivot = {comp["pivot"][0], comp["pivot"][1]};
					c.rotation = comp.value("rotation", 0.0f);
				} else if (type == "UIImage") {
					auto& c = reg.get_or_emplace<UIImageComponent>(entity);
					c.enabled = en; c.texturePath = comp.value("texturePath", "");
					if (comp.contains("color")) c.color = {comp["color"][0], comp["color"][1], comp["color"][2], comp["color"][3]};
					c.layer = comp.value("layer", 0);
					c.is9Slice = comp.value("is9Slice", false);
					c.borderTop = comp.value("borderTop", 10.0f);
					c.borderBottom = comp.value("borderBottom", 10.0f);
					c.borderLeft = comp.value("borderLeft", 10.0f);
					c.borderRight = comp.value("borderRight", 10.0f);
					// ★UIテクスチャはリニア変換を避けるため sRGB=false でロード
					if (!c.texturePath.empty()) c.textureHandle = Engine::Renderer::GetInstance()->LoadTexture2D(c.texturePath, false);
				} else if (type == "UIText") {
					auto& c = reg.get_or_emplace<UITextComponent>(entity);
					c.enabled = en; 
					c.text = comp.value("text", ""); 
					c.fontSize = comp.value("fontSize", 24.0f);
					c.fontPath = comp.value("fontPath", "Resources\\Fonts\\Kiwi_Maru\\KiwiMaru-Regular.ttf");
					if (comp.contains("color")) c.color = {comp["color"][0], comp["color"][1], comp["color"][2], comp["color"][3]};
					c.outlineEnabled = comp.value("outlineEnabled", false);
					if (comp.contains("outlineColor")) c.outlineColor = {comp["outlineColor"][0], comp["outlineColor"][1], comp["outlineColor"][2], comp["outlineColor"][3]};
					c.outlineThickness = comp.value("outlineThickness", 2.0f);
				} else if (type == "UIButton") {
					auto& c = reg.get_or_emplace<UIButtonComponent>(entity);
					c.enabled = en;
					if (comp.contains("normalColor")) c.normalColor = {comp["normalColor"][0], comp["normalColor"][1], comp["normalColor"][2], comp["normalColor"][3]};
					if (comp.contains("hoverColor")) c.hoverColor = {comp["hoverColor"][0], comp["hoverColor"][1], comp["hoverColor"][2], comp["hoverColor"][3]};
					if (comp.contains("pressedColor")) c.pressedColor = {comp["pressedColor"][0], comp["pressedColor"][1], comp["pressedColor"][2], comp["pressedColor"][3]};
					c.onClickCallback = comp.value("onClickCallback", "");
					if (comp.contains("hitboxOffset")) c.hitboxOffset = {comp["hitboxOffset"][0], comp["hitboxOffset"][1]};
					if (comp.contains("hitboxScale")) c.hitboxScale = {comp["hitboxScale"][0], comp["hitboxScale"][1]};
				} else if (type == "AudioListener") {
					reg.get_or_emplace<AudioListenerComponent>(entity).enabled = en;
				} else if (type == "Motion") {
					auto& c = reg.get_or_emplace<MotionComponent>(entity);
					c.enabled = en;
					if (comp.contains("clips") && comp["clips"].is_object()) {
						c.clips.clear();
						for (auto& [name, clipJ] : comp["clips"].items()) {
							MotionComponent::MotionClip clip;
							clip.name = name;
							clip.totalDuration = clipJ.value("totalDuration", 1.0f);
							clip.loop = clipJ.value("loop", false);
							if (clipJ.contains("keyframes") && clipJ["keyframes"].is_array()) {
								for (auto& kf : clipJ["keyframes"]) {
									MotionComponent::Keyframe k;
									k.time = kf.value("time", 0.0f);
									if (kf.contains("translate")) k.translate = {kf["translate"][0], kf["translate"][1], kf["translate"][2]};
									if (kf.contains("rotate")) k.rotate = {kf["rotate"][0], kf["rotate"][1], kf["rotate"][2]};
									if (kf.contains("scale")) k.scale = {kf["scale"][0], kf["scale"][1], kf["scale"][2]};
									clip.keyframes.push_back(k);
								}
							}
							c.clips[name] = clip;
						}
					} else if (comp.contains("keyframes")) {
						// 互換用: 旧フォーマット
						MotionComponent::MotionClip clip;
						clip.name = "Default";
						clip.totalDuration = comp.value("totalDuration", 1.0f);
						clip.loop = comp.value("loop", true);
						for (auto& kf : comp["keyframes"]) {
							MotionComponent::Keyframe k;
							k.time = kf.value("time", 0.0f);
							if (kf.contains("translate")) k.translate = {kf["translate"][0], kf["translate"][1], kf["translate"][2]};
							if (kf.contains("rotate")) k.rotate = {kf["rotate"][0], kf["rotate"][1], kf["rotate"][2]};
							if (kf.contains("scale")) k.scale = {kf["scale"][0], kf["scale"][1], kf["scale"][2]};
							clip.keyframes.push_back(k);
						}
						c.clips["Default"] = clip;
					}
					c.activeClip = comp.value("activeClip", "Default");
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

static ViewMode currentViewMode = ViewMode::Scene;
static DirectX::XMFLOAT3 editorCamPos = {0, 2, -5};
static DirectX::XMFLOAT3 editorCamRot = {0.2f, 0, 0};

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

ViewMode EditorUI::GetViewMode() { return currentViewMode; }
static bool s_viewportHovered = false;
bool EditorUI::IsViewportHovered() { return s_viewportHovered; }

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
	
	uint32_t parentId = static_cast<uint32_t>(entt::null);
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
		   << EscapeJson(cp->texturePath) << "\", \"shaderName\": \"" << EscapeJson(cp->shaderName) << "\", \"color\": [" << cp->color.x << "," << cp->color.y << "," << cp->color.z << "," << cp->color.w << "], \"uvTiling\": [" << cp->uvTiling.x << "," << cp->uvTiling.y << "], \"uvOffset\": [" << cp->uvOffset.x << "," << cp->uvOffset.y << "]";
		if (!cp->extraTexturePaths.empty()) {
			ss << ", \"extraTexturePaths\": [";
			for (size_t i = 0; i < cp->extraTexturePaths.size(); ++i) {
				if (i > 0) ss << ", ";
				ss << "\"" << EscapeJson(cp->extraTexturePaths[i]) << "\"";
			}
			ss << "]";
		}
		ss << "}";
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
		ss << "        {\"type\": \"Tag\", \"enabled\": " << (cp->enabled ? "true" : "false") << ", \"tag\": \"" << EscapeJson(TagToString(cp->tag)) << "\"}";
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
		ss << "        {\"type\": \"River\", \"enabled\": " << (cp->enabled ? "true" : "false") << ", \"width\": " << cp->width << ", \"flowSpeed\": " << cp->flowSpeed << ", \"uvScale\": " << cp->uvScale << ", \"texturePath\": \"" << EscapeJson(cp->texturePath) << "\", \"points\": [";
		for (size_t pi = 0; pi < cp->points.size(); ++pi) {
			if (pi > 0) ss << ", ";
			ss << "[" << cp->points[pi].x << "," << cp->points[pi].y << "," << cp->points[pi].z << "]";
		}
		ss << "]}";
	}
	if (auto* cp = registry.try_get<WorldSpaceUIComponent>(entity)) {
		addComma();
		ss << "        {\"type\": \"WorldSpaceUI\", \"enabled\": " << (cp->enabled ? "true" : "false") << ", \"showHealthBar\": " << (cp->showHealthBar ? "true" : "false") << ", \"showDamageNumbers\": " << (cp->showDamageNumbers ? "true" : "false") << ", \"offset\": [" << cp->offset.x << "," << cp->offset.y << "," << cp->offset.z << "], \"barWidth\": " << cp->barWidth << ", \"barHeight\": " << cp->barHeight << "}";
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
		ss << "        {\"type\": \"ParticleEmitter\", \"enabled\": " << (cp->enabled ? "true" : "false") << ", \"assetPath\": \"" << EscapeJson(cp->assetPath) << "\"";
		
		auto& p = cp->emitter.params;
		ss << ", \"emitRate\": " << p.emitRate << ", \"burstCount\": " << p.burstCount;
		ss << ", \"shape\": " << static_cast<int>(p.shape) << ", \"shapeRadius\": " << p.shapeRadius << ", \"shapeAngle\": " << p.shapeAngle;
		ss << ", \"lifeTime\": " << p.lifeTime << ", \"lifeTimeVariance\": " << p.lifeTimeVariance;
		ss << ", \"startVelocity\": [" << p.startVelocity.x << "," << p.startVelocity.y << "," << p.startVelocity.z << "]";
		ss << ", \"velocityVariance\": [" << p.velocityVariance.x << "," << p.velocityVariance.y << "," << p.velocityVariance.z << "]";
		ss << ", \"acceleration\": [" << p.acceleration.x << "," << p.acceleration.y << "," << p.acceleration.z << "]";
		ss << ", \"damping\": " << p.damping;
		ss << ", \"startSize\": [" << p.startSize.x << "," << p.startSize.y << "," << p.startSize.z << "]";
		ss << ", \"startSizeVariance\": [" << p.startSizeVariance.x << "," << p.startSizeVariance.y << "," << p.startSizeVariance.z << "]";
		ss << ", \"endSize\": [" << p.endSize.x << "," << p.endSize.y << "," << p.endSize.z << "]";
		ss << ", \"endSizeVariance\": [" << p.endSizeVariance.x << "," << p.endSizeVariance.y << "," << p.endSizeVariance.z << "]";
		ss << ", \"startColor\": [" << p.startColor.x << "," << p.startColor.y << "," << p.startColor.z << "," << p.startColor.w << "]";
		ss << ", \"endColor\": [" << p.endColor.x << "," << p.endColor.y << "," << p.endColor.z << "," << p.endColor.w << "]";
		ss << ", \"angularVelocity\": [" << p.angularVelocity.x << "," << p.angularVelocity.y << "," << p.angularVelocity.z << "]";
		ss << ", \"angularVelocityVariance\": [" << p.angularVelocityVariance.x << "," << p.angularVelocityVariance.y << "," << p.angularVelocityVariance.z << "]";
		ss << ", \"texturePath\": \"" << EscapeJson(p.texturePath) << "\"";
		ss << ", \"shaderName\": \"" << EscapeJson(p.shaderName) << "\"";
		ss << ", \"useBillboard\": " << (p.useBillboard ? "true" : "false");
		ss << ", \"isAdditive\": " << (p.isAdditive ? "true" : "false");
		ss << ", \"useUvAnim\": " << (p.useUvAnim ? "true" : "false");
		ss << ", \"uvAnimCols\": " << p.uvAnimCols << ", \"uvAnimRows\": " << p.uvAnimRows << ", \"uvAnimFps\": " << p.uvAnimFps;
		
		ss << "}";
	}
	if (auto* cp = registry.try_get<RectTransformComponent>(entity)) {
		addComma();
		ss << "        {\"type\": \"RectTransform\", \"enabled\": " << (cp->enabled ? "true" : "false") << ", \"pos\": [" << cp->pos.x << "," << cp->pos.y << "], \"size\": [" << cp->size.x << "," << cp->size.y << "], \"anchor\": [" << cp->anchor.x << "," << cp->anchor.y << "], \"pivot\": [" << cp->pivot.x << "," << cp->pivot.y << "], \"rotation\": " << cp->rotation << "}";
	}
	if (auto* cp = registry.try_get<UIImageComponent>(entity)) {
		addComma();
		ss << "        {\"type\": \"UIImage\", \"enabled\": " << (cp->enabled ? "true" : "false") << ", \"texturePath\": \"" << EscapeJson(cp->texturePath) << "\", \"color\": [" << cp->color.x << "," << cp->color.y << "," << cp->color.z << "," << cp->color.w << "], \"layer\": " << cp->layer << ", \"is9Slice\": " << (cp->is9Slice ? "true" : "false") << ", \"borderTop\": " << cp->borderTop << ", \"borderBottom\": " << cp->borderBottom << ", \"borderLeft\": " << cp->borderLeft << ", \"borderRight\": " << cp->borderRight << "}";
	}
	if (auto* cp = registry.try_get<UITextComponent>(entity)) {
		addComma();
		ss << "        {\"type\": \"UIText\", \"enabled\": " << (cp->enabled ? "true" : "false") << ", \"text\": \"" << EscapeJson(cp->text) << "\", \"fontSize\": " << cp->fontSize << ", \"fontPath\": \"" << EscapeJson(cp->fontPath) << "\", \"color\": [" << cp->color.x << "," << cp->color.y << "," << cp->color.z << "," << cp->color.w << "], \"outlineEnabled\": " << (cp->outlineEnabled ? "true" : "false") << ", \"outlineThickness\": " << cp->outlineThickness << ", \"outlineColor\": [" << cp->outlineColor.x << "," << cp->outlineColor.y << "," << cp->outlineColor.z << "," << cp->outlineColor.w << "]}";
	}
	if (auto* cp = registry.try_get<UIButtonComponent>(entity)) {
		addComma();
		ss << "        {\"type\": \"UIButton\", \"enabled\": " << (cp->enabled ? "true" : "false") << ", \"normalColor\": [" << cp->normalColor.x << "," << cp->normalColor.y << "," << cp->normalColor.z << "," << cp->normalColor.w << "], \"hoverColor\": [" << cp->hoverColor.x << "," << cp->hoverColor.y << "," << cp->hoverColor.z << "," << cp->hoverColor.w << "], \"pressedColor\": [" << cp->pressedColor.x << "," << cp->pressedColor.y << "," << cp->pressedColor.z << "," << cp->pressedColor.w << "], \"onClickCallback\": \"" << EscapeJson(cp->onClickCallback) << "\", \"hitboxOffset\": [" << cp->hitboxOffset.x << "," << cp->hitboxOffset.y << "], \"hitboxScale\": [" << cp->hitboxScale.x << "," << cp->hitboxScale.y << "]}";
	}
	if (auto* cp = registry.try_get<AudioListenerComponent>(entity)) {
		addComma();
		ss << "        {\"type\": \"AudioListener\", \"enabled\": " << (cp->enabled ? "true" : "false") << "}";
	}
	if (auto* cp = registry.try_get<MotionComponent>(entity)) {
		addComma();
		ss << "        {\"type\": \"Motion\", \"enabled\": " << (cp->enabled ? "true" : "false") << ", \"activeClip\": \"" << EscapeJson(cp->activeClip) << "\", \"clips\": {\n";
		bool firstClip = true;
		for (auto& [clipName, clip] : cp->clips) {
			if (!firstClip) ss << ",\n";
			firstClip = false;
			ss << "          \"" << EscapeJson(clipName) << "\": {\"totalDuration\": " << clip.totalDuration << ", \"loop\": " << (clip.loop ? "true" : "false") << ", \"keyframes\": [\n";
			for (size_t i = 0; i < clip.keyframes.size(); ++i) {
				auto& kf = clip.keyframes[i];
				ss << "            {\"time\": " << kf.time << ", \"translate\": [" << kf.translate.x << "," << kf.translate.y << "," << kf.translate.z << "], \"rotate\": [" << kf.rotate.x << "," << kf.rotate.y << "," << kf.rotate.z << "], \"scale\": [" << kf.scale.x << "," << kf.scale.y << "," << kf.scale.z << "]}";
				if (i < clip.keyframes.size() - 1) ss << ",\n";
			}
			ss << "\n          ]}";
		}
		ss << "\n        }}";
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
	std::vector<entt::entity> sortedEntities;
	view.each([&](entt::entity e, auto&) {
		sortedEntities.push_back(e);
	});
	for (size_t i = 0; i < sortedEntities.size(); ++i) {
		if (i > 0) ss << ",\n";
		ss << SerializeEntity(registry, sortedEntities[i]);
	}
	ss << "\n  ]\n}\n";
	return ss.str();
}

static std::string s_clipboardJson;

static void ExecuteCopy(GameScene* scene) {
	if (!scene) return;
	auto ent = scene->GetSelectedEntity();
	if (ent == entt::null || !scene->GetRegistry().valid(ent)) return;
	s_clipboardJson = SerializeEntity(scene->GetRegistry(), ent);
	EditorUI::Log("Copied Entity to Clipboard.");
}

static void ExecutePaste(GameScene* scene) {
	if (!scene || s_clipboardJson.empty()) return;
	try {
		json j = json::parse("{\"objects\": [" + s_clipboardJson + "]}");
		auto createdEnts = RestoreSceneFromJson(scene, j, true);
		if (!createdEnts.empty()) {
			auto e = createdEnts[0];
			auto& reg = scene->GetRegistry();
			if (reg.all_of<NameComponent>(e)) {
				auto& nc = reg.get<NameComponent>(e);
				nc.name = GenerateCopyName(nc.name, reg);
			}
			if (reg.all_of<TransformComponent>(e)) {
				auto& tc = reg.get<TransformComponent>(e);
				tc.translate.x += 0.5f;
				tc.translate.y += 0.5f;
				tc.translate.z -= 0.5f;
			}
			scene->SetSelectedEntity(e);
			scene->GetSelectedEntities() = {e};
			
			std::string snap = s_clipboardJson;
			uint32_t id = static_cast<uint32_t>(e);
			EditorUI::PushUndo({"Paste Entity", 
				[scene, id]() { scene->DestroyObject(id); },
				[scene, snap]() {
					try {
						json j2 = json::parse("{\"objects\": [" + snap + "]}");
						RestoreSceneFromJson(scene, j2, true);
					} catch (...) {}
				}
			});
			EditorUI::Log("Pasted Entity: " + (reg.all_of<NameComponent>(e) ? reg.get<NameComponent>(e).name : "Unknown"));
		}
	} catch (...) { EditorUI::LogError("Failed to paste entity"); }
}

static void ExecuteDuplicate(GameScene* scene) {
	if (!scene) return;
	ExecuteCopy(scene);
	ExecutePaste(scene);
}

// ★ 複数選択対応: まとめて削除
static void ExecuteDeleteSelected(GameScene* scene) {
	if (!scene) return;
	auto& selSet = scene->GetSelectedEntities();
	if (selSet.empty()) {
		// 単一選択のフォールバック
		auto ent = scene->GetSelectedEntity();
		if (ent != entt::null && scene->GetRegistry().valid(ent)) {
			scene->DestroyObject(static_cast<uint32_t>(ent));
			scene->SetSelectedEntity(entt::null);
		}
		return;
	}
	// コピーしてイテレート（削除中にsetが変わるため）
	std::vector<entt::entity> toDelete(selSet.begin(), selSet.end());
	selSet.clear();
	scene->SetSelectedEntity(entt::null);
	for (auto e : toDelete) {
		if (scene->GetRegistry().valid(e)) {
			scene->DestroyObject(static_cast<uint32_t>(e));
		}
	}
	EditorUI::Log("Deleted " + std::to_string(toDelete.size()) + " entities.");
}

// ★ 複数選択対応: まとめてコピー
static void ExecuteCopySelected(GameScene* scene) {
	if (!scene) return;
	auto& selSet = scene->GetSelectedEntities();
	if (selSet.size() <= 1) { ExecuteCopy(scene); return; }
	std::string combined;
	for (auto e : selSet) {
		if (!scene->GetRegistry().valid(e)) continue;
		if (!combined.empty()) combined += ",\n";
		combined += SerializeEntity(scene->GetRegistry(), e);
	}
	s_clipboardJson = combined;
	EditorUI::Log("Copied " + std::to_string(selSet.size()) + " entities.");
}

// ★ 複数選択対応: まとめてペースト
static void ExecutePasteSelected(GameScene* scene) {
	if (!scene || s_clipboardJson.empty()) return;
	try {
		json j = json::parse("{\"objects\": [" + s_clipboardJson + "]}");
		auto createdEnts = RestoreSceneFromJson(scene, j, true);
		auto& selSet = scene->GetSelectedEntities();
		selSet.clear();
		auto& reg = scene->GetRegistry();
		for (auto e : createdEnts) {
			if (reg.all_of<NameComponent>(e)) {
				auto& nc = reg.get<NameComponent>(e);
				nc.name = GenerateCopyName(nc.name, reg);
			}
			if (reg.all_of<TransformComponent>(e)) {
				auto& tc = reg.get<TransformComponent>(e);
				tc.translate.x += 0.5f;
				tc.translate.y += 0.5f;
				tc.translate.z -= 0.5f;
			}
			selSet.insert(e);
		}
		if (!selSet.empty()) {
			scene->SetSelectedEntity(*selSet.rbegin());
		}
		EditorUI::Log("Pasted " + std::to_string(createdEnts.size()) + " entities.");
	} catch (...) { EditorUI::LogError("Failed to paste entities"); }
}

// ★ 複数選択対応: まとめて複製
static void ExecuteDuplicateSelected(GameScene* scene) {
	if (!scene) return;
	ExecuteCopySelected(scene);
	ExecutePasteSelected(scene);
}

static std::string SaveFileDialog(const char* filter, const char* defExt);

void EditorUI::SaveScene(GameScene* scene, const std::string& path) {
	if (!scene) return;
	std::string targetPath = path;
	if (targetPath.empty()) targetPath = currentScenePath;
	
	if (targetPath.empty()) {
		std::string userPath = SaveFileDialog("JSON Files (*.json)|*.json|All Files (*.*)|*.*|", "json");
		if (userPath.empty()) return;
		targetPath = userPath;
	}

	currentScenePath = targetPath;

	std::string absPath = GetUnifiedProjectPath(targetPath);
	OutputDebugStringA(("[EditorUI] Saving scene to: " + absPath + "\n").c_str());
	
	// ★ 安全性向上: 再生中(PLAY中)にセーブした場合は、現在の動的ゴミオブジェクトを含む状態ではなく、プレイ開始時のクリーンなスナップショットを保存する
	std::string content;
	if (scene->IsPlaying() && !scene->GetSceneSnapshot().empty()) {
		content = scene->GetSceneSnapshot();
		OutputDebugStringA("[EditorUI] WARNING: Game is playing. Saving snapshot instead of active gameplay state to prevent zombie entities!\n");
	} else {
		content = SaveToMemory(scene);
	}
	
	if (content.empty()) { LogError("Save failed: empty content"); return; }

	std::ofstream f(Engine::PathUtils::FromUTF8(absPath), std::ios::out | std::ios::trunc);
	if (!f.is_open()) { LogError("Save failed (cannot open): " + absPath); return; }
	f << content;
	f.flush(); // ★追加: 確実にディスクに書き出す
	if (f.bad() || f.fail()) {
		LogError("Save failed (write error): " + absPath);
		f.close();
		return;
	}
	f.close();
	Log("Scene saved: " + absPath);
}

// ====== Utility Functions & File Dialogs ======


static std::string OpenFileDialog(const char* filter) {
	OPENFILENAMEW ofn;
	WCHAR szFile[260] = {0};
	std::wstring wfilter = Engine::PathUtils::FromUTF8(filter);
	for (auto& c : wfilter) { if (c == L'|') c = L'\0'; }
	ZeroMemory(&ofn, sizeof(OPENFILENAMEW));
	ofn.lStructSize = sizeof(OPENFILENAMEW);
	ofn.hwndOwner = nullptr;
	ofn.lpstrFile = szFile;
	ofn.nMaxFile = sizeof(szFile) / sizeof(WCHAR);
	ofn.lpstrFilter = wfilter.c_str();
	ofn.nFilterIndex = 1;
	ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
	if (GetOpenFileNameW(&ofn) == TRUE) return Engine::PathUtils::ToUTF8(ofn.lpstrFile);
	return "";
}

static std::string SaveFileDialog(const char* filter, const char* defExt) {
	OPENFILENAMEW ofn;
	WCHAR szFile[260] = {0};
	std::wstring wfilter = Engine::PathUtils::FromUTF8(filter);
	for (auto& c : wfilter) { if (c == L'|') c = L'\0'; }
	std::wstring wdefExt = Engine::PathUtils::FromUTF8(defExt);
	ZeroMemory(&ofn, sizeof(OPENFILENAMEW));
	ofn.lStructSize = sizeof(OPENFILENAMEW);
	ofn.hwndOwner = nullptr;
	ofn.lpstrFile = szFile;
	ofn.nMaxFile = sizeof(szFile) / sizeof(WCHAR);
	ofn.lpstrFilter = wfilter.c_str();
	ofn.nFilterIndex = 1;
	ofn.lpstrDefExt = wdefExt.c_str();
	ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
	if (GetSaveFileNameW(&ofn) == TRUE) return Engine::PathUtils::ToUTF8(ofn.lpstrFile);
	return "";
}

static bool LoadSceneInternal(GameScene* scene, const std::string& path, bool append) {
	if (!scene) return false;
	std::string absPath = EditorUI::GetUnifiedProjectPath(path);
	OutputDebugStringA(("[EditorUI] LoadSceneInternal: " + absPath + "\n").c_str());

	std::ifstream f(Engine::PathUtils::FromUTF8(absPath));
	if (!f.is_open()) {
		absPath = path; f.open(Engine::PathUtils::FromUTF8(absPath));
		if (!f.is_open()) {
			EditorUI::LogError("Load failed: " + absPath);
			MessageBoxA(NULL, ("Failed to open scene file:\n" + absPath).c_str(), "Load Error", MB_OK | MB_ICONERROR);
			return false;
		}
	}
	json j;
	try {
		f >> j;
		
		// データ用JSON等をシーン扱いで読み込まないようにチェック
		if (!j.contains("objects") && !j.contains("settings")) {
			std::string msg = "This JSON file is not a valid scene format:\n" + absPath;
			EditorUI::LogError(msg);
			MessageBoxA(NULL, msg.c_str(), "Format Error", MB_OK | MB_ICONWARNING);
			return false;
		}

		RestoreSceneFromJson(scene, j, append);
		EditorUI::Log((append ? "Scene appended: " : "Scene loaded: ") + absPath);
		return true;
	} catch (const std::exception& e) {
		std::string msg = "JSON Parse Error in " + absPath + ": " + std::string(e.what());
		EditorUI::LogError(msg);
		MessageBoxA(NULL, msg.c_str(), "JSON Error", MB_OK | MB_ICONERROR);
		return false;
	}
}

void EditorUI::LoadScene(GameScene* scene, const std::string& path) {
	if (LoadSceneInternal(scene, path, false)) {
		currentScenePath = path;
	}
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
	std::ifstream f(Engine::PathUtils::FromUTF8(absPath));
	if (!f.is_open()) {
		absPath = path; f.open(Engine::PathUtils::FromUTF8(absPath));
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
	if (path.empty()) return "";
	// ★修正: UTF-8文字列を直接 fs::path に渡すと Windows で文字化けするため FromUTF8 を経由
	if (std::filesystem::path(Engine::PathUtils::FromUTF8(path)).is_absolute()) return path;

	// Engine::PathUtils を通じてルートを取得
	std::string root = Engine::PathUtils::GetRootPath();
	std::filesystem::path combined = std::filesystem::path(Engine::PathUtils::FromUTF8(root)) / Engine::PathUtils::FromUTF8(path);
	std::string res = Engine::PathUtils::ToUTF8(combined.wstring());
	std::replace(res.begin(), res.end(), '\\', '/');
	return res;
}

void EditorUI::Initialize(Engine::Renderer* renderer) {
	if (!renderer) return;
	// ★エディタUI用アイコンは sRGB=false でロード
	s_icons.folder = renderer->LoadTexture2D("Resources/Textures/Editor/Icons/folder.png", false);
	s_icons.file = renderer->LoadTexture2D("Resources/Textures/Editor/Icons/file.png", false);
	s_icons.model = renderer->LoadTexture2D("Resources/Textures/Editor/Icons/model.png", false);
	s_icons.prefab = renderer->LoadTexture2D("Resources/Textures/Editor/Icons/prefab.png", false);
	s_icons.audio = renderer->LoadTexture2D("Resources/Textures/Editor/Icons/audio.png", false);
	s_icons.script = renderer->LoadTexture2D("Resources/Textures/Editor/Icons/script.png", false);
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

	static bool s_showStartupSceneSelect = true;
	if (s_showStartupSceneSelect) {
		ImGui::OpenPopup("Select Startup Scene");
		s_showStartupSceneSelect = false;
	}

	if (ImGui::BeginPopupModal("Select Startup Scene", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
		ImGui::Text((const char*)u8"開くシーンを選択してください:");
		ImGui::Separator();
		std::string sceneDir = "Resources/Scenes";
		std::string absSceneDir = GetUnifiedProjectPath(sceneDir);
		if (std::filesystem::exists(Engine::PathUtils::FromUTF8(absSceneDir))) {
			for (const auto& entry : std::filesystem::directory_iterator(Engine::PathUtils::FromUTF8(absSceneDir))) {
				if (entry.path().extension() == ".json") {
					std::string fileName = entry.path().filename().string();
					if (fileName == "skills.json" || fileName == "level_data.json" || fileName == "settings.json" || fileName == ".temp_play.json") continue;
					if (ImGui::Button(fileName.c_str(), ImVec2(250, 0))) {
						LoadScene(gameScene, sceneDir + "/" + fileName);
						ImGui::CloseCurrentPopup();
					}
				}
			}
		}
		ImGui::Separator();
		if (ImGui::Button((const char*)u8"新規シーン作成", ImVec2(250, 0))) {
			gameScene->GetRegistry().clear();
			gameScene->GetSelectedEntities().clear();
			gameScene->SetSelectedEntity(entt::null);
			currentScenePath = ""; // 初回保存時にダイアログを出す
			ImGui::CloseCurrentPopup();
		}
		if (ImGui::Button((const char*)u8"キャンセル", ImVec2(250, 0))) {
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}

	// Global Shortcuts
	// ★ Ctrl+S は常に有効（テキスト入力中でも保存可能にする）
	if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S)) {
		SaveScene(gameScene, currentScenePath);
	}

	// テキスト入力中は他のショートカットを無効化（誤操作防止）
	if (!io.WantTextInput) {
		if (io.KeyCtrl) {
			if (ImGui::IsKeyPressed(ImGuiKey_Z)) Undo();
			if (ImGui::IsKeyPressed(ImGuiKey_Y)) Redo();
			if (ImGui::IsKeyPressed(ImGuiKey_N)) {
				gameScene->GetRegistry().clear();
				gameScene->GetSelectedEntities().clear();
				gameScene->SetSelectedEntity(entt::null);
			}
			if (ImGui::IsKeyPressed(ImGuiKey_O)) {
				std::string path = OpenFileDialog("JSON Files (*.json)|*.json|All Files (*.*)|*.*|");
				if (!path.empty()) LoadScene(gameScene, path);
			}
			if (ImGui::IsKeyPressed(ImGuiKey_C)) ExecuteCopySelected(gameScene);
			if (ImGui::IsKeyPressed(ImGuiKey_V)) ExecutePasteSelected(gameScene);
			if (ImGui::IsKeyPressed(ImGuiKey_D)) ExecuteDuplicateSelected(gameScene);
			// Ctrl+A: 全選択
			if (ImGui::IsKeyPressed(ImGuiKey_A)) {
				auto& selSet = gameScene->GetSelectedEntities();
				selSet.clear();
				auto view = gameScene->GetRegistry().view<NameComponent>();
				for (auto e : view) { selSet.insert(e); }
				if (!selSet.empty()) gameScene->SetSelectedEntity(*selSet.rbegin());
				Log("Selected all (" + std::to_string(selSet.size()) + " entities)");
			}
		} else {
			if (ImGui::IsKeyPressed(ImGuiKey_W)) currentGizmoMode = GizmoMode::Translate;
			if (ImGui::IsKeyPressed(ImGuiKey_E)) currentGizmoMode = GizmoMode::Rotate;
			if (ImGui::IsKeyPressed(ImGuiKey_R)) currentGizmoMode = GizmoMode::Scale;
			// Delete: まとめて削除
			if (ImGui::IsKeyPressed(ImGuiKey_Delete)) ExecuteDeleteSelected(gameScene);
			// Escape: 選択解除
			if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
				gameScene->GetSelectedEntities().clear();
				gameScene->SetSelectedEntity(entt::null);
			}
		}
	}

	if (ImGui::BeginMenuBar()) {
		if (ImGui::BeginMenu("File")) {
			// ★追加: 地面だけ残した軽量シーンを作るボタン
			if (ImGui::MenuItem("Create Light Scene (tesuto_light.json)", nullptr)) {
				auto view = gameScene->GetRegistry().view<NameComponent>();
				std::vector<entt::entity> toDelete;
				for (auto e : view) {
					const std::string& name = view.get<NameComponent>(e).name;
					if (name.find("Route_") != std::string::npos ||
						name.find("Mountain_") != std::string::npos ||
						name.find("Hill_") != std::string::npos ||
						name.find("garasu") != std::string::npos) {
						toDelete.push_back(e);
					}
				}
				for (auto e : toDelete) gameScene->GetRegistry().destroy(e);
				
				std::string newPath = currentScenePath;
				size_t pos = newPath.find("tesuto.json");
				if (pos != std::string::npos) {
					newPath.replace(pos, 11, "tesuto_light.json");
				} else if (newPath.empty()) {
					newPath = "Resources/Scenes/tesuto_light.json";
				} else {
					newPath += "_light.json"; // 別のシーンの場合
				}
				SaveScene(gameScene, newPath);
				EditorUI::Log("tesuto_light.json has been created and saved!");
			}
			ImGui::Separator();

			if (ImGui::MenuItem("New Scene", "Ctrl+N")) {
				gameScene->GetRegistry().clear();
				gameScene->GetSelectedEntities().clear();
				gameScene->SetSelectedEntity(entt::null);
				currentScenePath = "";
			}
			ImGui::Separator();
			if (ImGui::MenuItem("Open Scene", "Ctrl+O")) {
				std::string path = OpenFileDialog("JSON Files (*.json)|*.json|All Files (*.*)|*.*|");
				if (!path.empty()) LoadScene(gameScene, path);
			}
			if (ImGui::MenuItem("Append Scene", "Ctrl+Shift+O")) {
				std::string path = OpenFileDialog("JSON Files (*.json)|*.json|All Files (*.*)|*.*|");
				if (!path.empty()) AddScene(gameScene, path);
			}
			ImGui::Separator();
			if (ImGui::MenuItem("Import TD Map Layout", "")) {
				std::string path = OpenFileDialog("JSON Files (*.json)|*.json|All Files (*.*)|*.*|");
				if (!path.empty()) {
					auto fsPath = std::filesystem::path(path);
					std::ifstream f(fsPath);
					if (f.is_open()) {
						json j;
						f >> j;
						if (j.contains("objects")) {
							for (const auto& objData : j["objects"]) {
								std::string name = objData["name"];
								entt::entity e = gameScene->CreateEntity(name);
								
								auto& tc = gameScene->GetRegistry().get<TransformComponent>(e);
								tc.translate = {objData["position"][0].get<float>(), objData["position"][1].get<float>(), objData["position"][2].get<float>()};
								
								// Blenderの回転角はラジアンなのでそのまま使用 (YとZが反転している可能性はスクリプトで対策済み)
								tc.rotate = {objData["rotation"][0].get<float>(), objData["rotation"][1].get<float>(), objData["rotation"][2].get<float>()};
								tc.scale = {objData["scale"][0].get<float>(), objData["scale"][1].get<float>(), objData["scale"][2].get<float>()};
								
								std::string meshFile = objData["mesh"];
								std::string tagStr = objData.value("tag", "Default");
								
								auto& mr = gameScene->GetRegistry().emplace<MeshRendererComponent>(e);
								mr.modelPath = "Resources/Scenes/Map/" + meshFile;
								mr.modelHandle = Engine::Renderer::GetInstance()->LoadObjMesh(mr.modelPath);
								
								auto& tag = gameScene->GetRegistry().get_or_emplace<TagComponent>(e);
								if (tagStr == "ground" || tagStr == "wall" || tagStr == "mountain" || tagStr == "structure" || tagStr == "bridge") {
									tag.tag = TagType::Wall; // 障害物・床として扱う
									// 物理シミュが必要ならコライダーも付与
									auto& col = gameScene->GetRegistry().emplace<BoxColliderComponent>(e);
									col.size = tc.scale; // 簡易的にスケールベース
								} else if (tagStr == "path") {
									tag.tag = TagType::Default;
								}
							}
						}
					}
				}
			}
			ImGui::Separator();
			if (ImGui::MenuItem("Save Scene", "Ctrl+S")) {
				SaveScene(gameScene);
			}
			if (ImGui::MenuItem("Save As...", "Ctrl+Shift+S")) {
				std::string path = SaveFileDialog("JSON Files (*.json)|*.json|All Files (*.*)|*.*|", "json");
				if (!path.empty()) SaveScene(gameScene, path);
			}
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Scenes")) {
			if (ImGui::MenuItem("Generate Default Title UI")) {
				Engine::SceneParameters p; p.sceneName = "Title";
				Engine::SceneManager::GetInstance()->RequestChange("Game", p);
			}
			if (ImGui::MenuItem("Generate Default Select UI")) {
				Engine::SceneParameters p; p.sceneName = "Select";
				Engine::SceneManager::GetInstance()->RequestChange("Game", p);
			}
			if (ImGui::MenuItem("Generate Default Result UI")) {
				Engine::SceneParameters p; p.sceneName = "Result";
				Engine::SceneManager::GetInstance()->RequestChange("Game", p);
			}
			ImGui::Separator();

			std::string sceneDir = "Resources/Scenes";
			std::string absSceneDir = GetUnifiedProjectPath(sceneDir);
			if (fs::exists(Engine::PathUtils::FromUTF8(absSceneDir))) {
				for (const auto& entry : fs::directory_iterator(Engine::PathUtils::FromUTF8(absSceneDir))) {
					if (entry.path().extension() == ".json") {
						std::string fileName = entry.path().filename().string();
						// データ用JSONをシーンリストから除外する
						if (fileName == "skills.json" || fileName == "level_data.json" || fileName == "settings.json") {
							continue;
						}
						std::string fullPath = sceneDir + "/" + fileName;
						bool selected = (currentScenePath == fullPath);
						if (ImGui::MenuItem(fileName.c_str(), nullptr, selected)) {
							LoadScene(gameScene, fullPath);
						}
					}
				}
			} else {
				ImGui::TextDisabled("No Scenes found in %s", sceneDir.c_str());
			}
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Edit")) {
			if (ImGui::MenuItem("Undo", "Ctrl+Z")) Undo();
			if (ImGui::MenuItem("Redo", "Ctrl+Y")) Redo();
			ImGui::Separator();
			if (ImGui::MenuItem("Copy", "Ctrl+C")) ExecuteCopySelected(gameScene);
			if (ImGui::MenuItem("Paste", "Ctrl+V", false, !s_clipboardJson.empty())) ExecutePasteSelected(gameScene);
			if (ImGui::MenuItem("Duplicate", "Ctrl+D")) ExecuteDuplicateSelected(gameScene);
			ImGui::Separator();
			if (ImGui::MenuItem("Delete Selected", "Delete")) ExecuteDeleteSelected(gameScene);
			if (ImGui::MenuItem("Select All", "Ctrl+A")) {
				auto& selSet = gameScene->GetSelectedEntities();
				selSet.clear();
				auto view = gameScene->GetRegistry().view<NameComponent>();
				for (auto e : view) { selSet.insert(e); }
				if (!selSet.empty()) gameScene->SetSelectedEntity(*selSet.rbegin());
			}
			if (ImGui::MenuItem("Deselect All", "Escape")) {
				gameScene->GetSelectedEntities().clear();
				gameScene->SetSelectedEntity(entt::null);
			}
			ImGui::Separator();
			if (ImGui::MenuItem("Translate", "W", currentGizmoMode == GizmoMode::Translate)) currentGizmoMode = GizmoMode::Translate;
			if (ImGui::MenuItem("Rotate", "E", currentGizmoMode == GizmoMode::Rotate)) currentGizmoMode = GizmoMode::Rotate;
			if (ImGui::MenuItem("Scale", "R", currentGizmoMode == GizmoMode::Scale)) currentGizmoMode = GizmoMode::Scale;
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Tools")) {
			static bool spawnerOpen = false;
			if (ImGui::MenuItem("Enemy Spawner", nullptr, &spawnerOpen)) {
				s_spawnerEditor.SetSpawnerMode(spawnerOpen);
			}
			ImGui::EndMenu();
		}
		
		// Tool Specific UI (Snaps etc)
		ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
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

	// Viewport
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
	ImGui::Begin("Viewport", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
	
	if (ImGui::BeginTabBar("ViewTabBar")) {
		if (ImGui::BeginTabItem("Scene")) {
			if (currentViewMode != ViewMode::Scene) {
				currentViewMode = ViewMode::Scene;
			}
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Game")) {
			if (currentViewMode != ViewMode::Game) {
				currentViewMode = ViewMode::Game;
				// trigger camera snapshot handled in GameScene.cpp
			}
			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}
	
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
	
	// 描画実行
	ImGui::Image((ImTextureID)renderer->GetGameFinalSRV().ptr, ImVec2(renderW, renderH));
	
	// ★修正: 実際に画像が描画された正確なスクリーン座標を取得
	gameImageMin = ImGui::GetItemRectMin();
	gameImageMax = ImGui::GetItemRectMax();
	// ★追加: ビューポートのホバー状態を更新（Unityライクなカメラ操作用）
	s_viewportHovered = ImGui::IsItemHovered();
	
	// ★追加: UISystemなどの座標変換用にコンテキストへ設定
	auto& gctx = gameScene->GetContext();
	gctx.viewportOffset = { gameImageMin.x, gameImageMin.y };
	gctx.viewportSize = { renderW, renderH };
	
	// エディター上のマウス座標を内部解像度(1920x1080)に正確に変換して上書き
	// ★修正: ピクセル中心(+0.5f)を基準に精密なマッピングを行う
	ImVec2 mPos_img = ImGui::GetMousePos();
	gctx.overrideMouseX = (mPos_img.x - gameImageMin.x) * (float)Engine::WindowDX::kW / renderW;
	gctx.overrideMouseY = (mPos_img.y - gameImageMin.y) * (float)Engine::WindowDX::kH / renderH;
	gctx.useOverrideMouse = true;

	// Tool Editors Update & Draw (Overlays) - 画像の上にオーバーレイとして描画
	s_spawnerEditor.UpdateAndDraw(gameScene, renderer, gameImageMin, gameImageMax, renderW, renderH);
	
	// Project to Scene Drop
	if (ImGui::BeginDragDropTarget()) {
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("PROJECT_ASSET")) {
			std::string sPath = (const char*)payload->Data;
			std::replace(sPath.begin(), sPath.end(), '\\', '/'); // 正規化
			// Calculate Drop Position via Raycast
			float pickSx = ImGui::GetMousePos().x - gameImageMin.x;
			float pickSy = ImGui::GetMousePos().y - gameImageMin.y;
			DirectX::XMVECTOR rayOrig, rayDir;
			ScreenToWorldRay(pickSx, pickSy, (float)renderW, (float)renderH, gameScene->GetCamera().View(), gameScene->GetCamera().Proj(), rayOrig, rayDir);
			float dirY = DirectX::XMVectorGetY(rayDir);
			Engine::Vector3 dropPos(0, 0, 0);
			if (std::abs(dirY) > 0.001f) {
				float t = -DirectX::XMVectorGetY(rayOrig) / dirY;
				if (t > 0.0f) {
					DirectX::XMVECTOR hit = DirectX::XMVectorAdd(rayOrig, DirectX::XMVectorScale(rayDir, t));
					dropPos = { DirectX::XMVectorGetX(hit), DirectX::XMVectorGetY(hit), DirectX::XMVectorGetZ(hit) };
				}
			}

			if (sPath.find(".obj") != std::string::npos || sPath.find(".fbx") != std::string::npos) {
				auto e = gameScene->CreateEntity(fs::path(sPath).stem().string());
				auto& mr = gameScene->GetRegistry().emplace<MeshRendererComponent>(e);
				mr.modelPath = sPath;
				mr.modelHandle = renderer->LoadObjMesh(sPath);
				mr.texturePath = "Resources/Textures/white1x1.png";
				mr.textureHandle = renderer->LoadTexture2D(mr.texturePath);
				
				auto& tc = gameScene->GetRegistry().get<TransformComponent>(e);
				tc.translate = { dropPos.x, dropPos.y, dropPos.z };

				gameScene->SetSelectedEntity(e);
			} else if (sPath.find(".prefab") != std::string::npos) {
				auto entities = LoadPrefab(gameScene, sPath);
				if (!entities.empty()) {
					for (auto e : entities) {
						bool isRoot = true;
						if (auto* hc = gameScene->GetRegistry().try_get<HierarchyComponent>(e)) {
							if (hc->parentId != entt::null) {
								isRoot = false;
							}
						}
						if (isRoot) {
							if (auto* tc = gameScene->GetRegistry().try_get<TransformComponent>(e)) {
								// Move only root entities to preserve relative child positions
								tc->translate.x += dropPos.x;
								tc->translate.y += dropPos.y;
								tc->translate.z += dropPos.z;
							}
						}
					}
					gameScene->SetSelectedEntity(entities.back());
				}
			}
		}
		ImGui::EndDragDropTarget();
	}
	// Picking and Gizmo Logic
	bool gizmoHovered = false;
	bool isPlaying = gameScene->GetIsPlaying();
	auto selectedEnt = gameScene->GetSelectedEntity();

	if (!isPlaying && selectedEnt != entt::null && gameScene->GetRegistry().valid(selectedEnt)) {
		auto& reg = gameScene->GetRegistry();
		if (auto* tc = reg.try_get<TransformComponent>(selectedEnt)) {
			auto* mc = reg.try_get<MotionComponent>(selectedEnt);
		
		DirectX::XMFLOAT3 currentPos;
		if (mc && mc->selectedKeyframe >= 0 && mc->clips.count(mc->activeClip)) {
			auto& kfs = mc->clips.at(mc->activeClip).keyframes;
			if (mc->selectedKeyframe < (int)kfs.size()) {
				currentPos = kfs[mc->selectedKeyframe].translate;
			} else {
				currentPos = tc->translate;
			}
		} else {
			currentPos = tc->translate;
		}

		DirectX::XMVECTOR objPos3D = DirectX::XMLoadFloat3(&currentPos);
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
				gizmoStartTranslate = currentPos;
				gizmoStartRotate = {0,0,0}; // Not needed for KFs yet
				gizmoStartScale = {1,1,1};
			}

			if (gizmoDragging) {
				if (ImGui::IsMouseReleased(0)) {
					// Undo support
					DirectX::XMFLOAT3 endT = tc->translate;
					DirectX::XMFLOAT3 endR = tc->rotate;
					DirectX::XMFLOAT3 endS = tc->scale;
					DirectX::XMFLOAT3 startT = gizmoStartTranslate;
					DirectX::XMFLOAT3 startR = gizmoStartRotate;
					DirectX::XMFLOAT3 startS = gizmoStartScale;
					entt::entity e = selectedEnt;

					PushUndo({
						"Transform",
						[=, &reg = gameScene->GetRegistry()]() {
							if (reg.valid(e) && reg.all_of<TransformComponent>(e)) {
								auto& t = reg.get<TransformComponent>(e);
								t.translate = startT; t.rotate = startR; t.scale = startS;
							}
						},
						[=, &reg = gameScene->GetRegistry()]() {
							if (reg.valid(e) && reg.all_of<TransformComponent>(e)) {
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

					DirectX::XMFLOAT3* targetPos = &reg.get<TransformComponent>(selectedEnt).translate;
					if (mc && mc->selectedKeyframe >= 0 && mc->clips.count(mc->activeClip)) {
						auto& kfs = mc->clips.at(mc->activeClip).keyframes;
						if (mc->selectedKeyframe < (int)kfs.size()) {
							targetPos = &kfs[mc->selectedKeyframe].translate;
						}
					}
					auto* targetRot = &reg.get<TransformComponent>(selectedEnt).rotate;
					auto* targetScale = &reg.get<TransformComponent>(selectedEnt).scale;

					if (currentGizmoMode == GizmoMode::Translate) {
						if (gizmoDragAxis == 0) targetPos->x += projMovement * sensitivity;
						if (gizmoDragAxis == 1) targetPos->y -= projMovement * sensitivity; // Screen Y is inverted
						if (gizmoDragAxis == 2) targetPos->z += projMovement * sensitivity;
					} else if (currentGizmoMode == GizmoMode::Rotate && !mc) {
						if (gizmoDragAxis == 0) targetRot->x += projMovement * sensitivity;
						if (gizmoDragAxis == 1) targetRot->y -= projMovement * sensitivity;
						if (gizmoDragAxis == 2) targetRot->z += projMovement * sensitivity;
					} else if (currentGizmoMode == GizmoMode::Scale && !mc) {
						if (gizmoDragAxis == 0) targetScale->x += projMovement * sensitivity;
						if (gizmoDragAxis == 1) targetScale->y -= projMovement * sensitivity;
						if (gizmoDragAxis == 2) targetScale->z += projMovement * sensitivity;
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
			}

			// Spline Keyframe Picking
			if (mc && !gizmoHovered) {
				if (ImGui::IsMouseClicked(0)) {
					mousePos = ImGui::GetMousePos();
					float sx = mousePos.x, sy = mousePos.y;
					
					view = gameScene->GetCamera().View();
					proj = gameScene->GetCamera().Proj();

					if (mc->clips.count(mc->activeClip)) {
						auto& kfs = mc->clips.at(mc->activeClip).keyframes;
						for (int i = 0; i < (int)kfs.size(); ++i) {
							DirectX::XMVECTOR p3D = DirectX::XMLoadFloat3(&kfs[i].translate);
							projP = DirectX::XMVector3Project(p3D, 0, 0, renderW, renderH, 0.0f, 1.0f, proj, view, DirectX::XMMatrixIdentity());
							px = DirectX::XMVectorGetX(projP) + gameImageMin.x;
							py = DirectX::XMVectorGetY(projP) + gameImageMin.y;
							pz = DirectX::XMVectorGetZ(projP);

							if (pz > 0.0f && pz < 1.0f) {
								float dx = sx - px, dy = sy - py;
								if (std::sqrt(dx*dx + dy*dy) < 15.0f) {
									mc->selectedKeyframe = i;
									gizmoHovered = true; // Prevent object picking
									break;
								}
								}
							}
						}
					}
				}
			}
		} // end if (tc)

		// ★追加: UI Gizmo Logic
		if (auto* rt = reg.try_get<RectTransformComponent>(selectedEnt)) {
			ImDrawList* uiDrawList = ImGui::GetWindowDrawList();
			ImVec2 mPos = ImGui::GetMousePos();
				UISystem::WorldRect wr = UISystem::CalculateWorldRect(selectedEnt, reg, (float)Engine::WindowDX::kW, (float)Engine::WindowDX::kH);
				
				// 内部解像度 (1920x1080) から実際の表示ピクセルへの変換スケーラー
				float scaleX = renderW / (float)Engine::WindowDX::kW;
				float scaleY = renderH / (float)Engine::WindowDX::kH;

				ImVec2 pMin(gameImageMin.x + wr.x * scaleX, gameImageMin.y + wr.y * scaleY);
				ImVec2 pMax(pMin.x + wr.w * scaleX, pMin.y + wr.h * scaleY);
				
				// UI本体の枠 (オレンジ色)
				uiDrawList->AddRect(pMin, pMax, IM_COL32(255, 140, 0, 255), 0.0f, 0, 2.0f);
				
				// UIButton があれば判定エリアも表示 (水色)
				if (auto* btn = reg.try_get<UIButtonComponent>(selectedEnt)) {
					float hw = wr.w * btn->hitboxScale.x;
					float hh = wr.h * btn->hitboxScale.y;
					float cx = wr.x + wr.w * 0.5f + btn->hitboxOffset.x;
					float cy = wr.y + wr.h * 0.5f + btn->hitboxOffset.y;
					float hx = cx - hw * 0.5f;
					float hy = cy - hh * 0.5f;

					ImVec2 hpMin(gameImageMin.x + hx * scaleX, gameImageMin.y + hy * scaleY);
					ImVec2 hpMax(hpMin.x + hw * scaleX, hpMin.y + hh * scaleY);
					uiDrawList->AddRect(hpMin, hpMax, IM_COL32(0, 255, 255, 255), 0.0f, 0, 1.0f);

					// 判定エリア移動ハンドル (中央の丸)
					ImVec2 hCenter((hpMin.x + hpMax.x) * 0.5f, (hpMin.y + hpMax.y) * 0.5f);
					uiDrawList->AddCircleFilled(hCenter, 6.0f, IM_COL32(0, 255, 255, 255));
					
					mPos = ImGui::GetMousePos();
					float dx_h = mPos.x - hCenter.x;
					float dy_h = mPos.y - hCenter.y;
					float dToHCenter = std::sqrt(dx_h * dx_h + dy_h * dy_h);

					if (dToHCenter < 10.0f) {
						gizmoHovered = true; // ホバー時もフラグを立てて背面クリックを防止
						if (!uiDragging && ImGui::IsMouseClicked(0)) {
							uiDragging = true;
							uiDragHandle = 10; // 10 = HitboxOffset
							uiDragStartHitOffset = btn->hitboxOffset;
						}
					}
					
					// 判定エリア拡縮ハンドル (右下の丸)
					ImVec2 hScaleHandle = hpMax;
					uiDrawList->AddCircleFilled(hScaleHandle, 6.0f, IM_COL32(0, 255, 255, 255));
					float dx_s = mPos.x - hScaleHandle.x;
					float dy_s = mPos.y - hScaleHandle.y;
					float dToHScale = std::sqrt(dx_s * dx_s + dy_s * dy_s);
					if (dToHScale < 10.0f) {
						gizmoHovered = true;
						if (!uiDragging && ImGui::IsMouseClicked(0)) {
							uiDragging = true;
							uiDragHandle = 11; // 11 = HitboxScale
							uiDragStartHitScale = btn->hitboxScale;
						}
					}
				}

				// UI本体移動ハンドル (左上)
				uiDrawList->AddCircleFilled(pMin, 8.0f, IM_COL32(255, 140, 0, 255));
				
				mPos = ImGui::GetMousePos();
				float dx_p = mPos.x - pMin.x;
				float dy_p = mPos.y - pMin.y;
				float dToPMin = std::sqrt(dx_p * dx_p + dy_p * dy_p);
				if (dToPMin < 12.0f) {
					gizmoHovered = true;
					if (!uiDragging && ImGui::IsMouseClicked(0)) {
						uiDragging = true;
						uiDragHandle = 0; // 0 = Rect Pos
						uiDragStartPos = rt->pos;
					}
				}

				// UI本体拡縮ハンドル (右下)
				uiDrawList->AddCircleFilled(pMax, 8.0f, IM_COL32(255, 140, 0, 255));
				float dx_m = mPos.x - pMax.x;
				float dy_m = mPos.y - pMax.y;
				float dToPMax = std::sqrt(dx_m * dx_m + dy_m * dy_m);
				if (dToPMax < 12.0f) {
					gizmoHovered = true;
					if (!uiDragging && ImGui::IsMouseClicked(0)) {
						uiDragging = true;
						uiDragHandle = 1; // 1 = Rect Size
						uiDragStartSize = rt->size;
					}
				}

				if (uiDragging) {
					gizmoHovered = true;
					ImVec2 mDelta = ImGui::GetIO().MouseDelta;
					float dx = mDelta.x / scaleX;
					float dy = mDelta.y / scaleY;

					if (uiDragHandle == 0) { // UI Pos
						rt->pos.x += dx;
						rt->pos.y += dy;
					} else if (uiDragHandle == 1) { // UI Size
						float oldW = rt->size.x;
						float oldH = rt->size.y;
						rt->size.x = (std::max)(1.0f, oldW + dx);
						rt->size.y = (std::max)(1.0f, oldH + dy);

						// ピボットによるズレを補正 (pMinを固定)
						float addedW = rt->size.x - oldW;
						float addedH = rt->size.y - oldH;
						rt->pos.x += rt->pivot.x * addedW;
						rt->pos.y += rt->pivot.y * addedH;

						// UITextが含まれていればフォントサイズも同期してスケーリングする
						if (auto* text = reg.try_get<UITextComponent>(selectedEnt)) {
							if (oldH > 1.0f) {
								text->fontSize = (std::max)(1.0f, text->fontSize * (rt->size.y / oldH));
							}
						}
					} else if (uiDragHandle == 10) { // Hitbox Offset
						if (auto* btn = reg.try_get<UIButtonComponent>(selectedEnt)) {
							btn->hitboxOffset.x += dx;
							btn->hitboxOffset.y += dy;
						}
					} else if (uiDragHandle == 11) { // Hitbox Scale
						if (auto* btn = reg.try_get<UIButtonComponent>(selectedEnt)) {
							btn->hitboxScale.x = (std::max)(0.01f, btn->hitboxScale.x + dx / wr.w);
							btn->hitboxScale.y = (std::max)(0.01f, btn->hitboxScale.y + dy / wr.h);
						}
					}

					if (ImGui::IsMouseReleased(0)) {
						uiDragging = false;
						uiDragHandle = -1;
					}
				}
		}
	}

	// ★ 矩形選択用の静的変数
	static bool s_rectSelecting = false;
	static ImVec2 s_rectStart = {0, 0};

	// Scene Picking - Also disabled during PLAY
	if (!isPlaying && ImGui::IsItemHovered() && !gizmoHovered) {
		ImVec2 mousePos = ImGui::GetMousePos();
		float sx = mousePos.x - gameImageMin.x;
		float sy = mousePos.y - gameImageMin.y;

		// --- 左クリック開始: 矩形選択開始 or 単体ピッキング ---
		if (ImGui::IsMouseClicked(0) && sx >= 0 && sx <= renderW && sy >= 0 && sy <= renderH) {
			s_rectSelecting = true;
			s_rectStart = mousePos;
		}

		// --- 左ボタン離した時: 矩形の大きさで判定 ---
		if (ImGui::IsMouseReleased(0) && s_rectSelecting) {
			s_rectSelecting = false;
			ImVec2 rectEnd = mousePos;
			float dragW = std::fabs(rectEnd.x - s_rectStart.x);
			float dragH = std::fabs(rectEnd.y - s_rectStart.y);

			bool isShift = ImGui::GetIO().KeyShift;

			if (dragW < 5.0f && dragH < 5.0f) {
				// --- 小さいドラッグ = クリックピッキング ---
				float pickSx = s_rectStart.x - gameImageMin.x;
				float pickSy = s_rectStart.y - gameImageMin.y;
				float internalMouseX = gctx.overrideMouseX;
				float internalMouseY = gctx.overrideMouseY;

				entt::entity hitE = entt::null;

				// 1. UI Picking Pass
				int maxLayer = -10000;
				gameScene->GetRegistry().view<RectTransformComponent>().each([&](entt::entity e, const RectTransformComponent&) {
					if (auto* esc = gameScene->GetRegistry().try_get<EditorStateComponent>(e)) { if (esc->locked) return; }
					UISystem::WorldRect wr = UISystem::CalculateWorldRect(e, gameScene->GetRegistry(), (float)Engine::WindowDX::kW, (float)Engine::WindowDX::kH);
					if (internalMouseX >= wr.x && internalMouseX <= wr.x + wr.w &&
						internalMouseY >= wr.y && internalMouseY <= wr.y + wr.h) {
						int layer = 0;
						if (auto* img = gameScene->GetRegistry().try_get<UIImageComponent>(e)) layer = img->layer;
						if (hitE == entt::null || layer >= maxLayer) { maxLayer = layer; hitE = e; }
					}
				});

				// 2. 3D Picking Pass
				if (hitE == entt::null) {
					DirectX::XMVECTOR rayOrig, rayDir;
					ScreenToWorldRay(pickSx, pickSy, (float)renderW, (float)renderH, gameScene->GetCamera().View(), gameScene->GetCamera().Proj(), rayOrig, rayDir);
					float minD = FLT_MAX;
					gameScene->GetRegistry().view<MeshRendererComponent, TransformComponent>().each([&](entt::entity e, const MeshRendererComponent& mr, [[maybe_unused]] const TransformComponent& tc) {
						if (mr.modelHandle == 0) return;
						auto* m = renderer->GetModel(mr.modelHandle);
						if (!m) return;
						if (auto* esc = gameScene->GetRegistry().try_get<EditorStateComponent>(e)) { if (esc->locked) return; }
						float d; Engine::Vector3 p;
						if (m->RayCast(rayOrig, rayDir, gameScene->GetWorldMatrix(static_cast<int>(e)), d, p)) {
							if (d < minD) { minD = d; hitE = e; }
						}
					});
				}

				auto& selSet = gameScene->GetSelectedEntities();
				if (hitE != entt::null) {
					if (isShift) {
						if (selSet.count(hitE)) { selSet.erase(hitE); }
						else { selSet.insert(hitE); }
						gameScene->SetSelectedEntity(selSet.empty() ? entt::null : *selSet.rbegin());
					} else {
						gameScene->SetSelectedEntity(hitE);
						selSet = {hitE};
					}
				} else {
					if (!isShift) {
						gameScene->SetSelectedEntity(entt::null);
						selSet.clear();
					}
				}
			} else {
				// --- 大きなドラッグ = 矩形選択 ---
				float rx0 = (std::min)(s_rectStart.x, rectEnd.x) - gameImageMin.x;
				float ry0 = (std::min)(s_rectStart.y, rectEnd.y) - gameImageMin.y;
				float rx1 = (std::max)(s_rectStart.x, rectEnd.x) - gameImageMin.x;
				float ry1 = (std::max)(s_rectStart.y, rectEnd.y) - gameImageMin.y;

				auto& selSet = gameScene->GetSelectedEntities();
				if (!isShift) selSet.clear();

				// 各エンティティのワールド位置をスクリーン座標に変換して矩形内か判定
				DirectX::XMMATRIX viewMat = gameScene->GetCamera().View();
				DirectX::XMMATRIX projMat = gameScene->GetCamera().Proj();
				DirectX::XMMATRIX vpMat = DirectX::XMMatrixMultiply(viewMat, projMat);

				gameScene->GetRegistry().view<TransformComponent, NameComponent>().each([&](entt::entity e, const TransformComponent& tc, [[maybe_unused]] const NameComponent&) {
					if (auto* esc = gameScene->GetRegistry().try_get<EditorStateComponent>(e)) { if (esc->locked) return; }
					DirectX::XMVECTOR worldPos = DirectX::XMVectorSet(tc.translate.x, tc.translate.y, tc.translate.z, 1.0f);
					DirectX::XMVECTOR clipPos = DirectX::XMVector4Transform(worldPos, vpMat);
					float w = DirectX::XMVectorGetW(clipPos);
					if (w <= 0.001f) return; // カメラ後ろはスキップ
					float ndcX = DirectX::XMVectorGetX(clipPos) / w;
					float ndcY = DirectX::XMVectorGetY(clipPos) / w;
					float screenX = (ndcX * 0.5f + 0.5f) * renderW;
					float screenY = (-ndcY * 0.5f + 0.5f) * renderH;

					if (screenX >= rx0 && screenX <= rx1 && screenY >= ry0 && screenY <= ry1) {
						selSet.insert(e);
					}
				});

				if (!selSet.empty()) {
					gameScene->SetSelectedEntity(*selSet.rbegin());
				} else if (!isShift) {
					gameScene->SetSelectedEntity(entt::null);
				}
			}
		}
	}

	// ★ 矩形選択中の描画（半透明の青い矩形）
	if (s_rectSelecting && ImGui::IsMouseDown(0)) {
		ImVec2 curMouse = ImGui::GetMousePos();
		ImDrawList* dl = ImGui::GetForegroundDrawList();
		ImVec2 mn = ImVec2((std::min)(s_rectStart.x, curMouse.x), (std::min)(s_rectStart.y, curMouse.y));
		ImVec2 mx = ImVec2((std::max)(s_rectStart.x, curMouse.x), (std::max)(s_rectStart.y, curMouse.y));
		dl->AddRectFilled(mn, mx, IM_COL32(80, 140, 255, 40));
		dl->AddRect(mn, mx, IM_COL32(80, 140, 255, 200), 0.0f, 0, 1.5f);
	}

	// -- Overlay removed (Clean Viewport) --
	ImGui::End();
	ImGui::PopStyleVar();

	ImGui::End(); // EditorMain
}

void EditorUI::ShowHierarchy(GameScene* scene) {
	ImGui::Begin("Hierarchy");
	static entt::entity s_hierarchyAnchor = entt::null; // 範囲選択のアンカー
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

			auto& selSet = scene->GetSelectedEntities();
			bool selected = selSet.count(entity) > 0 || (scene->GetSelectedEntity() == entity);
			std::string name = view.get<NameComponent>(entity).name;
			if (ImGui::Selectable((name + "##" + std::to_string((uint32_t)entity)).c_str(), selected)) {
				if (ImGui::GetIO().KeyShift && s_hierarchyAnchor != entt::null) {
					// Shift+Click: アンカーからここまでの範囲を全て選択
					bool inRange = false;
					bool found = false;
					for (auto e2 : view) {
						if (e2 == s_hierarchyAnchor || e2 == entity) {
							if (!inRange) { inRange = true; }
							else { found = true; } // 2つ目に到達
						}
						if (inRange) { selSet.insert(e2); }
						if (found) break;
					}
					scene->SetSelectedEntity(entity);
				} else if (ImGui::GetIO().KeyAlt) {
					// Alt+Click: 1つずつトグル（追加/除去）
					if (selSet.count(entity)) {
						selSet.erase(entity);
						if (scene->GetSelectedEntity() == entity) {
							scene->SetSelectedEntity(selSet.empty() ? entt::null : *selSet.begin());
						}
					} else {
						selSet.insert(entity);
						scene->SetSelectedEntity(entity);
					}
					s_hierarchyAnchor = entity;
				} else {
					// 通常クリック: 単一選択
					scene->SetSelectedEntity(entity);
					selSet = {entity};
					s_hierarchyAnchor = entity;
				}
			}

			// Right-click menu for a specific entity
			if (ImGui::BeginPopupContextItem()) {
				if (ImGui::MenuItem("Delete")) {
					uint32_t id = static_cast<uint32_t>(entity);
					std::string snapshot = SerializeEntity(registry, entity);
					PushUndo({"Delete Entity",
						[=, &reg = scene->GetRegistry()](){ 
							json j = json::parse("{\"objects\": [" + snapshot + "]}");
							RestoreSceneFromJson(scene, j, true);
						},
						[=](){ scene->DestroyObject(id); }
					});
					scene->DestroyObject(id);
					scene->GetSelectedEntities().erase(entity);
					if (scene->GetSelectedEntity() == entity) scene->SetSelectedEntity(entt::null);
				}
				if (scene->GetSelectedEntities().size() > 1) {
					if (ImGui::MenuItem("Delete Selected", "Delete")) ExecuteDeleteSelected(scene);
				}
				ImGui::Separator();
				if (ImGui::MenuItem("Copy", "Ctrl+C")) ExecuteCopySelected(scene);
				if (ImGui::MenuItem("Paste", "Ctrl+V", false, !s_clipboardJson.empty())) ExecutePasteSelected(scene);
				if (ImGui::MenuItem("Duplicate", "Ctrl+D")) ExecuteDuplicateSelected(scene);
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
				mr.modelPath = "Resources/Models/cube/cube.obj";
				mr.modelHandle = Engine::Renderer::GetInstance()->LoadObjMesh(mr.modelPath);
				mr.texturePath = "Resources/Textures/white1x1.png";
				mr.textureHandle = Engine::Renderer::GetInstance()->LoadTexture2D(mr.texturePath);
				scene->SetSelectedEntity(e);
			}
			if (ImGui::MenuItem("Create Sphere")) {
				auto e = scene->CreateEntity("Sphere");
				auto& mr = registry.emplace<MeshRendererComponent>(e);
				mr.modelPath = "Resources/Models/player_ball/ball.obj";
				mr.modelHandle = Engine::Renderer::GetInstance()->LoadObjMesh(mr.modelPath);
				mr.texturePath = "Resources/Textures/white1x1.png";
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
				mr.modelPath = "Resources/Models/cube/cube.obj";
				mr.modelHandle = Engine::Renderer::GetInstance()->LoadObjMesh(mr.modelPath);
				mr.texturePath = "Resources/Textures/white1x1.png";
				mr.textureHandle = Engine::Renderer::GetInstance()->LoadTexture2D(mr.texturePath);
				scene->SetSelectedEntity(e);
			}
			ImGui::EndPopup();
		}
	}
	ImGui::End();
}

std::vector<std::string> EditorUI::GetAssetsInDir(const std::string& root, const std::vector<std::string>& extensions) {
	std::vector<std::string> assets;
	std::string absRoot = GetUnifiedProjectPath(root);
	if (!fs::exists(Engine::PathUtils::FromUTF8(absRoot))) return assets;

	for (const auto& entry : fs::recursive_directory_iterator(Engine::PathUtils::FromUTF8(absRoot))) {
		if (entry.is_regular_file()) {
			std::string ext = entry.path().extension().string();
			std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return (char)std::tolower(c); });
			
			bool match = false;
			for (const auto& targetExt : extensions) {
				if (ext == targetExt) { match = true; break; }
			}
			
			if (match) {
				std::string path = Engine::PathUtils::ToUTF8(entry.path().wstring());
				std::replace(path.begin(), path.end(), '\\', '/');
				
				// Make relative to Resources/ for consistent storage
				size_t pos = path.find("/Resources/");
				if (pos != std::string::npos) {
					path = path.substr(pos + 1); // Skip leading slash if any
				} else {
					pos = path.find("Resources/");
					if (pos != std::string::npos) path = path.substr(pos);
				}
				assets.push_back(path);
			}
		}
	}
	return assets;
}

bool EditorUI::AssetField(const char* label, std::string& path, const std::vector<std::string>& extensions) {
	bool modified = false;
	ImGui::PushID(label);
	
	char buf[256];
	strcpy_s(buf, path.c_str());
	
	float buttonSize = ImGui::GetFrameHeight();
	ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - buttonSize - ImGui::GetStyle().ItemSpacing.x);
	
	if (ImGui::InputText(label, buf, sizeof(buf))) {
		path = buf;
		modified = true;
	}

	// Drag & Drop
	if (ImGui::BeginDragDropTarget()) {
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("PROJECT_ASSET")) {
			std::string droppedPath = (const char*)payload->Data;
			std::replace(droppedPath.begin(), droppedPath.end(), '\\', '/');
			
			std::string ext = fs::path(Engine::PathUtils::FromUTF8(droppedPath)).extension().string();
			std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return (char)std::tolower(c); });
			
			bool valid = false;
			for (const auto& e : extensions) { if (ext == e) { valid = true; break; } }
			
			if (valid) {
				path = droppedPath;
				modified = true;
			}
		}
		ImGui::EndDragDropTarget();
	}

	ImGui::SameLine();
	if (ImGui::Button("...")) {
		ImGui::OpenPopup("AssetDropdown");
	}

	if (ImGui::BeginPopup("AssetDropdown")) {
		static std::vector<std::string> assetList;
		if (ImGui::IsWindowAppearing()) {
			assetList = GetAssetsInDir("Resources", extensions);
		}

		if (ImGui::Selectable("(None)", path.empty())) {
			path = "";
			modified = true;
		}

		for (const auto& a : assetList) {
			if (ImGui::Selectable(a.c_str(), path == a)) {
				path = a;
				modified = true;
			}
		}
		ImGui::EndPopup();
	}

	ImGui::PopID();
	return modified;
}

void EditorUI::ShowInspector(GameScene* scene) {
	// Removed ImGui::Begin("Inspector") to support tab embedding
	auto selected = scene->GetSelectedEntity();
	if (scene && selected != entt::null && scene->GetRegistry().valid(selected)) {
		ImGui::PushID((int)selected);
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
			std::string p = "Resources/Prefabs/" + name + ".prefab";
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
					
					if (AssetField("Model Path", cp->modelPath, {".obj", ".fbx", ".gltf"})) {
						cp->modelHandle = Engine::Renderer::GetInstance()->LoadObjMesh(cp->modelPath);
					}

					if (AssetField("Texture Path", cp->texturePath, {".png", ".jpg"})) {
						cp->textureHandle = Engine::Renderer::GetInstance()->LoadTexture2D(cp->texturePath);
					}

					// ★追加: マルチマテリアル用サブテクスチャ
					for (size_t i = 0; i < cp->extraTexturePaths.size(); ++i) {
						ImGui::PushID(static_cast<int>(i) + 1000);
						std::string label = "Sub Texture " + std::to_string(i + 1);
						if (AssetField(label.c_str(), cp->extraTexturePaths[i], {".png", ".jpg"})) {
							cp->extraTextureHandles[i] = Engine::Renderer::GetInstance()->LoadTexture2D(cp->extraTexturePaths[i]);
						}
						ImGui::SameLine();
						if (ImGui::Button("X")) {
							cp->extraTexturePaths.erase(cp->extraTexturePaths.begin() + i);
							cp->extraTextureHandles.erase(cp->extraTextureHandles.begin() + i);
							ImGui::PopID();
							break;
						}
						ImGui::PopID();
					}
					if (ImGui::Button("Add Sub Texture")) {
						cp->extraTexturePaths.push_back("");
						cp->extraTextureHandles.push_back(0);
					}

					// Shader Dropdown
					const auto& shaders = Engine::Renderer::GetInstance()->GetShaderNames();
					std::string currentShader = cp->shaderName.empty() ? "Default" : cp->shaderName;
					if (ImGui::BeginCombo("Shader", currentShader.c_str())) {
						for (size_t i = 0; i < shaders.size(); ++i) {
							const auto& sName = shaders[i];
							ImGui::PushID((int)i);
							bool isSelected = (currentShader == sName);
							if (ImGui::Selectable(sName.c_str(), isSelected)) {
								cp->shaderName = sName;
							}
							if (isSelected) ImGui::SetItemDefaultFocus();
							ImGui::PopID();
						}
						ImGui::EndCombo();
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
					if (AssetField("Mesh Path", gmc->meshPath, {".obj", ".fbx"})) {
						gmc->meshHandle = Engine::Renderer::GetInstance()->LoadObjMesh(gmc->meshPath);
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
					
					const char* currentTagName = TagToString(tag->tag);
					if (ImGui::BeginCombo("Tag##TagCombo", currentTagName)) {
						for (int i = 0; i <= (int)TagType::Core; ++i) {
							TagType t = (TagType)i;
							bool isSelected = (tag->tag == t);
							if (ImGui::Selectable(TagToString(t), isSelected)) {
								tag->tag = t;
								scene->SyncTag(entity); // キャッシュ更新
							}
							if (isSelected) ImGui::SetItemDefaultFocus();
						}
						ImGui::EndCombo();
					}
					if (ImGui::Button("Remove##Tag")) registry.remove<TagComponent>(entity);
				}
			}
			if (auto* as = registry.try_get<AudioSourceComponent>(entity)) {
				if (ImGui::CollapsingHeader("AudioSource", ImGuiTreeNodeFlags_DefaultOpen)) {
					ImGui::Checkbox("Enabled##AS", &as->enabled);
					if (AssetField("Sound File", as->soundPath, {".wav", ".mp3"})) {
						as->soundHandle = Engine::Audio::GetInstance()->Load(as->soundPath);
					}
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
			if (auto* mc = registry.try_get<MotionComponent>(entity)) {
				if (ImGui::CollapsingHeader("Motion Editor", ImGuiTreeNodeFlags_DefaultOpen)) {
					ImGui::Checkbox("Enabled##MC", &mc->enabled);
					
					// Clip Selection
					if (ImGui::BeginCombo("Active Clip", mc->activeClip.c_str())) {
						for (auto& [name, clip] : mc->clips) {
							bool isSelected = (mc->activeClip == name);
							if (ImGui::Selectable(name.c_str(), isSelected)) {
								mc->activeClip = name;
								mc->currentTime = 0.0f;
								mc->selectedKeyframe = -1;
							}
						}
						ImGui::EndCombo();
					}

					ImGui::SameLine();
					if (ImGui::Button("+##NewClip")) {
						ImGui::OpenPopup("NewClipPopup");
					}

					if (ImGui::BeginPopup("NewClipPopup")) {
						static char newClipBuf[64] = "NewClip";
						ImGui::InputText("Name", newClipBuf, sizeof(newClipBuf));
						if (ImGui::Button("Add")) {
							MotionComponent::MotionClip clip;
							clip.name = newClipBuf;
							clip.keyframes.push_back({0.00f, {0,0,0}, {0,0,0}, {1,1,1}});
							clip.keyframes.push_back({1.00f, {1,0,0}, {0,0,0}, {1,1,1}});
							mc->clips[newClipBuf] = clip;
							mc->activeClip = newClipBuf;
							ImGui::CloseCurrentPopup();
						}
						ImGui::EndPopup();
					}

					if (ImGui::Button("Delete Clip") && mc->clips.size() > 1) {
						mc->clips.erase(mc->activeClip);
						mc->activeClip = mc->clips.begin()->first;
					}

					ImGui::Separator();
					if (mc->clips.count(mc->activeClip)) {
						auto& clip = mc->clips[mc->activeClip];
						ImGui::Text("Clip: %s", clip.name.c_str());
						ImGui::Checkbox("Playing", &mc->isPlaying);
						ImGui::DragFloat("Current Time", &mc->currentTime, 0.01f, 0, clip.totalDuration);
						ImGui::DragFloat("Duration", &clip.totalDuration, 0.1f, 0.1f, 100.0f);
						ImGui::Checkbox("Loop", &clip.loop);
						
						if (ImGui::Button("Add Keyframe")) {
							MotionComponent::Keyframe k;
							k.time = clip.totalDuration;
							if (!clip.keyframes.empty()) {
								k.translate = clip.keyframes.back().translate;
								k.rotate = clip.keyframes.back().rotate;
								k.scale = clip.keyframes.back().scale;
							} else {
								k.translate = {0,0,0};
								k.rotate = {0,0,0};
								k.scale = {1,1,1};
							}
							clip.keyframes.push_back(k);
						}
						
						ImGui::Separator();
						ImGui::Text("Keyframes:");
						for (int i = 0; i < (int)clip.keyframes.size(); ++i) {
							char label[32]; sprintf_s(label, "KF %d", i);
							if (ImGui::Selectable(label, mc->selectedKeyframe == i)) {
								mc->selectedKeyframe = i;
							}
							if (mc->selectedKeyframe == i) {
								ImGui::Indent();
								ImGui::DragFloat("Time", &clip.keyframes[i].time, 0.01f, 0, clip.totalDuration);
								ImGui::DragFloat3("Pos", &clip.keyframes[i].translate.x, 0.1f);
								ImGui::DragFloat3("Rot", &clip.keyframes[i].rotate.x, 0.01f);
								ImGui::DragFloat3("Scale", &clip.keyframes[i].scale.x, 0.01f);
								if (ImGui::Button("Remove KF")) {
									clip.keyframes.erase(clip.keyframes.begin() + i);
									mc->selectedKeyframe = -1;
								}
								ImGui::Unindent();
							}
						}
					}

					if (ImGui::Button("Remove Component##MC")) registry.remove<MotionComponent>(entity);
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
									if (entry.instance) {
										std::string logMsg = "[EditorUI] Inspector: Created instance for " + entry.scriptPath + ", Restoring params: " + entry.parameterData + "\n";
										OutputDebugStringA(logMsg.c_str());
										
										// パラメータが空でなければデシリアライズ
									if (!entry.parameterData.empty()) {
										entry.instance->DeserializeParameters(entry.parameterData);
									}
								} else if (entry.instance) {
									// パラメータが空の場合は初期状態をセット
									entry.parameterData = entry.instance->SerializeParameters();
									std::string logMsg = "[EditorUI] Inspector: Creating instance for " + entry.scriptPath + ", Param was empty, setting default: " + entry.parameterData + "\n";
									OutputDebugStringA(logMsg.c_str());
								}
							}

							if (entry.instance) {
								ImGui::PushID("ScriptEditor");
								
								std::string oldParams = entry.parameterData;
								entry.instance->OnEditorUI();
								std::string newParams = entry.instance->SerializeParameters();

								// 比較を少し賢くする
								// プレイ中以外で、かつ実際に中身が意味のある変更をされた場合のみ更新
								if (!scene->GetIsPlaying()) {
									bool shouldUpdate = false;
									if (oldParams != newParams) {
										// "{}" から具体的な値への変化、または値同士の変化
										if (oldParams == "{}" || oldParams.empty()) {
											// 初期状態からの設定
											shouldUpdate = true;
										} else if (newParams != "{}" && !newParams.empty()) {
											// 値が変更された
											shouldUpdate = true;
										}
									}

									if (shouldUpdate) {
										entry.parameterData = newParams;
										std::string logMsg = "[EditorUI] Parameter Changed: " + oldParams + " -> " + newParams + "\n";
										OutputDebugStringA(logMsg.c_str());
									}
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
					AssetField("Asset", pe->assetPath, {".json"});

					// ★追加: シェーダータイプドロップダウン
					struct ShaderOption { const char* label; const char* shaderName; bool isAdditive; };
					static const ShaderOption shaderOptions[] = {
						{ "(Default) Particle",              "",                        false },
						{ "Particle (Alpha Blend)",          "Particle",               false },
						{ "Particle (Additive)",             "ParticleAdditive",       true  },
						{ "Procedural Smoke",                "ProceduralSmoke",        false },
						{ "Procedural Smoke (Additive)",     "ProceduralSmokeAdditive",true  },
					};
					static const int shaderOptionCount = IM_ARRAYSIZE(shaderOptions);

					int currentIdx = 0;
					for (int si = 0; si < shaderOptionCount; ++si) {
						if (pe->emitter.params.shaderName == shaderOptions[si].shaderName) {
							currentIdx = si;
							break;
						}
					}

					if (ImGui::Combo("Shader Type##PE", &currentIdx, [](void* data, int idx, const char** out) -> bool {
						*out = ((const ShaderOption*)data)[idx].label;
						return true;
					}, (void*)shaderOptions, shaderOptionCount)) {
						pe->emitter.params.shaderName = shaderOptions[currentIdx].shaderName;
						pe->emitter.params.isAdditive = shaderOptions[currentIdx].isAdditive;

						// ★追加: ProceduralSmoke系を選択した場合、煙用プリセットを適用
						std::string sn = shaderOptions[currentIdx].shaderName;
						if (sn == "ProceduralSmoke" || sn == "ProceduralSmokeAdditive") {
							pe->emitter.params.startVelocity = {0, 1.5f, 0};
							pe->emitter.params.velocityVariance = {0.3f, 0.3f, 0.3f};
							pe->emitter.params.acceleration = {0, 0.5f, 0}; // 上昇気流
							pe->emitter.params.damping = 1.0f;
							pe->emitter.params.lifeTime = 3.0f;
							pe->emitter.params.lifeTimeVariance = 0.5f;
							pe->emitter.params.startSize = {1.5f, 1.5f, 1.5f};
							pe->emitter.params.endSize = {4.0f, 4.0f, 4.0f};
							pe->emitter.params.startColor = {0.85f, 0.9f, 0.95f, 0.7f};  // 水蒸気らしい薄青白いグレー
							pe->emitter.params.endColor = {0.6f, 0.65f, 0.7f, 0.0f}; // 滑らかに空気色にフェード
							pe->emitter.params.emitRate = 8.0f;
						}
					}

					if (ImGui::Button("Save Asset##PE")) {
						if (!pe->assetPath.empty()) {
							if (pe->emitter.SaveToJson(pe->assetPath)) {
								Log("Saved Particle Asset: " + pe->assetPath);
							} else {
								LogError("Failed to save Particle Asset: " + pe->assetPath);
							}
						} else {
							LogError("Particle Asset Path is empty.");
						}
					}
					ImGui::SameLine();
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
					if (AssetField("Texture", img->texturePath, {".png", ".jpg"})) {
						img->textureHandle = Engine::Renderer::GetInstance()->LoadTexture2D(img->texturePath, false);
					}
					ImGui::ColorEdit4("Color", &img->color.x);
					ImGui::DragInt("Layer", &img->layer, 1, -100, 100);
					ImGui::Checkbox("9-Slice", &img->is9Slice);
					if (img->is9Slice) {
						ImGui::DragFloat("Border Top", &img->borderTop, 1.0f, 0, 500);
						ImGui::DragFloat("Border Bottom", &img->borderBottom, 1.0f, 0, 500);
						ImGui::DragFloat("Border Left", &img->borderLeft, 1.0f, 0, 500);
						ImGui::DragFloat("Border Right", &img->borderRight, 1.0f, 0, 500);
					}
					if (ImGui::Button("Remove##IMG")) registry.remove<UIImageComponent>(entity);
				}
			}
			if (auto* txt = registry.try_get<UITextComponent>(entity)) {
				if (ImGui::CollapsingHeader("UIText", ImGuiTreeNodeFlags_DefaultOpen)) {
					ImGui::Checkbox("Enabled##TXT", &txt->enabled);
					char tbuf[2048] = {0}; 
					strcpy_s(tbuf, sizeof(tbuf), txt->text.c_str());
					if (ImGui::InputTextMultiline("Text", tbuf, sizeof(tbuf), ImVec2(0, 60), ImGuiInputTextFlags_AllowTabInput)) {
						txt->text = tbuf;
					}
					ImGui::TextDisabled("Help: Use <color=#RRGGBB>Text</color> for colored inline text.");
					ImGui::DragFloat("Font Size", &txt->fontSize, 1.0f, 1, 100);
					ImGui::ColorEdit4("Color##TXT", &txt->color.x);

					// ★追加: アウトライン設定
					ImGui::Checkbox("Outline", &txt->outlineEnabled);
					if (txt->outlineEnabled) {
						ImGui::Indent();
						ImGui::ColorEdit4("Outline Color", &txt->outlineColor.x);
						ImGui::DragFloat("Thickness", &txt->outlineThickness, 0.1f, 0.1f, 10.0f);
						ImGui::Unindent();
					}

					// フォント選択ドロップダウン
					std::vector<std::string> fonts = { "Resources\\Fonts\\Kiwi_Maru\\KiwiMaru-Regular.ttf" };
					std::string fontDir = GetUnifiedProjectPath("Resources/Fonts");
					if (std::filesystem::exists(Engine::PathUtils::FromUTF8(fontDir))) {
						for (auto& p : std::filesystem::directory_iterator(Engine::PathUtils::FromUTF8(fontDir))) {
							std::string ext = p.path().extension().string();
							if (ext == ".ttf" || ext == ".ttc" || ext == ".otf") {
								fonts.push_back(GetUnifiedProjectPath("Resources/Fonts/" + p.path().filename().string()));
							}
						}
					}

					if (ImGui::BeginCombo("Font Path", txt->fontPath.c_str())) {
						for (const auto& f : fonts) {
							bool isSelected = (txt->fontPath == f);
							if (ImGui::Selectable(f.c_str(), isSelected)) txt->fontPath = f;
							if (isSelected) ImGui::SetItemDefaultFocus();
						}
						ImGui::EndCombo();
					}
					AssetField("Custom Font", txt->fontPath, { ".ttf", ".ttc", ".otf" });

					if (!registry.all_of<RectTransformComponent>(entity)) {
						ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.4f, 0.0f, 1.0f));
						if (ImGui::Button("Add RectTransform (Show UI Gizmo)")) {
							registry.emplace<RectTransformComponent>(entity);
						}
						ImGui::PopStyleColor();
					}

					if (ImGui::Button("Remove##TXT")) registry.remove<UITextComponent>(entity);
				}
			}
			if (auto* btn = registry.try_get<UIButtonComponent>(entity)) {
				if (ImGui::CollapsingHeader("UIButton", ImGuiTreeNodeFlags_DefaultOpen)) {
					ImGui::Checkbox("Enabled##BTN", &btn->enabled);
					ImGui::Text("Hovered: %s", btn->isHovered ? "Yes" : "No");
					
					// ★追加: 判定エリアの個別調整
					ImGui::DragFloat2("Hitbox Offset", &btn->hitboxOffset.x, 1.0f);
					ImGui::DragFloat2("Hitbox Scale", &btn->hitboxScale.x, 0.01f, 0.0f, 10.0f);
					
					if (ImGui::Button("Remove##BTN")) registry.remove<UIButtonComponent>(entity);
				}
			}
			if (auto* riv = registry.try_get<RiverComponent>(entity)) {
				if (ImGui::CollapsingHeader("River", ImGuiTreeNodeFlags_DefaultOpen)) {
					ImGui::Checkbox("Enabled##RIV", &riv->enabled);
					ImGui::DragFloat("Width", &riv->width, 0.1f);
					ImGui::DragFloat("Speed", &riv->flowSpeed, 0.1f);
					AssetField("Texture", riv->texturePath, {".png", ".jpg"});
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
					
					const char* currentTagName = TagToString(hb->tag);
					if (ImGui::BeginCombo("Tag##HBTagCombo", currentTagName)) {
						for (int i = 0; i <= (int)TagType::EnemyBullet; ++i) {
							TagType t = (TagType)i;
							bool isSelected = (hb->tag == t);
							if (ImGui::Selectable(TagToString(t), isSelected)) hb->tag = t;
							if (isSelected) ImGui::SetItemDefaultFocus();
						}
						ImGui::EndCombo();
					}

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

					const char* currentTagName = TagToString(hb->tag);
					if (ImGui::BeginCombo("Tag##HurtBTagCombo", currentTagName)) {
						for (int i = 0; i <= (int)TagType::EnemyBullet; ++i) {
							TagType t = (TagType)i;
							bool isSelected = (hb->tag == t);
							if (ImGui::Selectable(TagToString(t), isSelected)) hb->tag = t;
							if (isSelected) ImGui::SetItemDefaultFocus();
						}
						ImGui::EndCombo();
					}

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
		ImGui::PopID();
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
					s_thumbnails[fullPath] = Engine::Renderer::GetInstance()->LoadTexture2D(fullPath, false);
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
	for (auto it = consoleLog.rbegin(); it != consoleLog.rend(); ++it) {
		ImGui::TextUnformatted(it->message.c_str());
	}
	ImGui::End();
}

void EditorUI::DrawSelectionGizmo([[maybe_unused]] Engine::Renderer* renderer, [[maybe_unused]] GameScene* scene) {} // WIP
void EditorUI::ShowAnimationWindow([[maybe_unused]] Engine::Renderer* renderer, [[maybe_unused]] GameScene* scene) { 
	// Removed ImGui::Begin("Animation")
	ImGui::Text("Animation Controls (WIP)");
}
void EditorUI::ShowPlayModeMonitor([[maybe_unused]] GameScene* scene) { 
	// ========== パフォーマンスプロファイラー ==========
	static constexpr int kHistorySize = 120; // 2秒分 (60fps)
	static float fpsHistory[kHistorySize] = {};
	static float frameTimeHistory[kHistorySize] = {};
	static float drawCallHistory[kHistorySize] = {};
	static float instanceHistory[kHistorySize] = {};
	static float memoryHistory[kHistorySize] = {};
	static float uploadHistory[kHistorySize] = {};
	static int historyOffset = 0;

	float fps = ImGui::GetIO().Framerate;
	float frameTimeMs = 1000.0f / (fps > 0.001f ? fps : 0.001f);

	// 履歴更新 (毎フレーム)
	fpsHistory[historyOffset] = fps;
	frameTimeHistory[historyOffset] = frameTimeMs;

	// Renderer統計
	auto* renderer = Engine::Renderer::GetInstance();
	const auto& stats = renderer ? renderer->GetFrameStats() : Engine::Renderer::FrameStats{};

	drawCallHistory[historyOffset] = (float)(stats.drawCalls + stats.instancedBatches);
	instanceHistory[historyOffset] = (float)stats.totalInstances;
	uploadHistory[historyOffset] = stats.uploadBufferTotal > 0 ? (float)stats.uploadBufferUsed / (float)stats.uploadBufferTotal * 100.0f : 0.0f;

	// メモリ使用量 (Windows API)
	float memoryMB = 0.0f;
	{
		PROCESS_MEMORY_COUNTERS_EX pmc{};
		pmc.cb = sizeof(pmc);
		if (GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc))) {
			memoryMB = (float)(pmc.WorkingSetSize / (1024.0 * 1024.0));
		}
	}
	memoryHistory[historyOffset] = memoryMB;

	historyOffset = (historyOffset + 1) % kHistorySize;

	// --- FPS セクション ---
	ImVec4 fpsColor = fps >= 58.0f ? ImVec4(0.2f, 1.0f, 0.4f, 1.0f) :
	                  fps >= 30.0f ? ImVec4(1.0f, 0.9f, 0.2f, 1.0f) :
	                                 ImVec4(1.0f, 0.3f, 0.2f, 1.0f);
	ImGui::TextColored(fpsColor, "FPS: %.1f", fps);
	ImGui::SameLine();
	ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "(%.2f ms)", frameTimeMs);

	// FPSグラフ
	{
		float reordered[kHistorySize];
		for (int i = 0; i < kHistorySize; ++i)
			reordered[i] = fpsHistory[(historyOffset + i) % kHistorySize];
		ImGui::PlotLines("##fps_graph", reordered, kHistorySize, 0, "FPS", 0.0f, 120.0f, ImVec2(-1, 35));
	}

	ImGui::Separator();

	// --- フレームタイム ---
	if (ImGui::CollapsingHeader("Frame Time", ImGuiTreeNodeFlags_DefaultOpen)) {
		float reordered[kHistorySize];
		for (int i = 0; i < kHistorySize; ++i)
			reordered[i] = frameTimeHistory[(historyOffset + i) % kHistorySize];
		ImGui::PlotLines("##frametime", reordered, kHistorySize, 0, "ms/frame", 0.0f, 50.0f, ImVec2(-1, 35));
		
		// 平均・最大フレームタイム
		float maxFT = 0.0f, avgFT = 0.0f;
		for (int i = 0; i < kHistorySize; ++i) { avgFT += reordered[i]; if (reordered[i] > maxFT) maxFT = reordered[i]; }
		avgFT /= kHistorySize;
		ImGui::Text("Avg: %.2f ms  Max: %.2f ms", avgFT, maxFT);
	}

	// --- ドローコール ---
	if (ImGui::CollapsingHeader("Draw Calls", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Text("Individual: %u", stats.drawCalls);
		ImGui::Text("Instanced Batches: %u", stats.instancedBatches);
		ImGui::Text("Total Instances: %u", stats.totalInstances);
		ImGui::Text("Sprites: %u", stats.spriteDrawCalls);
		ImGui::Text("Line Verts: %u", stats.lineVertices);

		float reordered[kHistorySize];
		for (int i = 0; i < kHistorySize; ++i)
			reordered[i] = drawCallHistory[(historyOffset + i) % kHistorySize];
		ImGui::PlotHistogram("##drawcalls", reordered, kHistorySize, 0, "Draw Calls", 0.0f, 0.0f, ImVec2(-1, 35));
	}

	// --- メモリ使用量 ---
	if (ImGui::CollapsingHeader("Memory", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Text("Working Set: %.1f MB", memoryMB);
		
		// VRAM使用量 (DXGI)
		if (renderer && renderer->GetDevice()) {
			Microsoft::WRL::ComPtr<IDXGIDevice3> dxgiDevice;
			Microsoft::WRL::ComPtr<IDXGIAdapter> dxgiAdapter;
			if (SUCCEEDED(renderer->GetDevice()->QueryInterface(IID_PPV_ARGS(&dxgiDevice))) &&
			    SUCCEEDED(dxgiDevice->GetAdapter(&dxgiAdapter))) {
				DXGI_ADAPTER_DESC adapterDesc{};
				dxgiAdapter->GetDesc(&adapterDesc);
				
				Microsoft::WRL::ComPtr<IDXGIAdapter3> adapter3;
				if (SUCCEEDED(dxgiAdapter.As(&adapter3))) {
					DXGI_QUERY_VIDEO_MEMORY_INFO vramInfo{};
					if (SUCCEEDED(adapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &vramInfo))) {
						float usedMB = (float)(vramInfo.CurrentUsage / (1024.0 * 1024.0));
						float budgetMB = (float)(vramInfo.Budget / (1024.0 * 1024.0));
						ImGui::Text("VRAM: %.0f / %.0f MB", usedMB, budgetMB);
						float ratio = budgetMB > 0 ? usedMB / budgetMB : 0.0f;
						ImVec4 barCol = ratio < 0.7f ? ImVec4(0.2f, 0.8f, 0.4f, 1.0f) :
						               ratio < 0.9f ? ImVec4(0.9f, 0.8f, 0.2f, 1.0f) :
						                              ImVec4(1.0f, 0.3f, 0.2f, 1.0f);
						ImGui::PushStyleColor(ImGuiCol_PlotHistogram, barCol);
						ImGui::ProgressBar(ratio, ImVec2(-1, 14), "");
						ImGui::PopStyleColor();
					}
				}
			}
		}
		
		float reordered[kHistorySize];
		for (int i = 0; i < kHistorySize; ++i)
			reordered[i] = memoryHistory[(historyOffset + i) % kHistorySize];
		ImGui::PlotLines("##memory", reordered, kHistorySize, 0, "RAM (MB)", 0.0f, 0.0f, ImVec2(-1, 35));
	}

	// --- GPU リソース ---
	if (ImGui::CollapsingHeader("GPU Resources")) {
		ImGui::Text("Models: %u", stats.loadedModels);
		ImGui::Text("Textures: %u", stats.loadedTextures);
		ImGui::Text("SRV Used: %u", stats.srvUsed);
		
		// Upload Buffer使用率
		float uploadUsedMB = stats.uploadBufferUsed / (1024.0f * 1024.0f);
		float uploadTotalMB = stats.uploadBufferTotal / (1024.0f * 1024.0f);
		ImGui::Text("Upload Buffer: %.2f / %.1f MB", uploadUsedMB, uploadTotalMB);
		float uploadRatio = uploadTotalMB > 0 ? uploadUsedMB / uploadTotalMB : 0.0f;
		ImVec4 uploadCol = uploadRatio < 0.5f ? ImVec4(0.2f, 0.8f, 0.4f, 1.0f) :
		                   uploadRatio < 0.8f ? ImVec4(0.9f, 0.8f, 0.2f, 1.0f) :
		                                        ImVec4(1.0f, 0.3f, 0.2f, 1.0f);
		ImGui::PushStyleColor(ImGuiCol_PlotHistogram, uploadCol);
		ImGui::ProgressBar(uploadRatio, ImVec2(-1, 14), "");
		ImGui::PopStyleColor();
		
		float reordered[kHistorySize];
		for (int i = 0; i < kHistorySize; ++i)
			reordered[i] = uploadHistory[(historyOffset + i) % kHistorySize];
		ImGui::PlotLines("##upload", reordered, kHistorySize, 0, "Upload %", 0.0f, 100.0f, ImVec2(-1, 35));
	}

	// --- エンティティ統計 ---
	if (scene && ImGui::CollapsingHeader("Scene Stats")) {
		auto& reg = scene->GetRegistry();
		uint32_t totalEntities = 0;
		uint32_t meshEntities = 0;
		uint32_t scriptEntities = 0;
		uint32_t colliderEntities = 0;
		uint32_t uiEntities = 0;
		
		for (auto e : reg.view<NameComponent>()) { (void)e; totalEntities++; }
		for (auto e : reg.view<MeshRendererComponent>()) { (void)e; meshEntities++; }
		for (auto e : reg.view<ScriptComponent>()) { (void)e; scriptEntities++; }
		for (auto e : reg.view<BoxColliderComponent>()) { (void)e; colliderEntities++; }
		for (auto e : reg.view<RectTransformComponent>()) { (void)e; uiEntities++; }
		
		ImGui::Text("Entities: %u", totalEntities);
		ImGui::Text("  Mesh: %u  Script: %u", meshEntities, scriptEntities);
		ImGui::Text("  Collider: %u  UI: %u", colliderEntities, uiEntities);
	}

	ImGui::Separator();
	
	// --- Player Monitor (既存) ---
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

