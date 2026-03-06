#include "EditorUI.h"
#include "../../externals/imgui/imgui.h"
#include "../../externals/imgui/imgui_internal.h"
#include "../Scenes/GameScene.h"
#include "Audio.h"
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

#include "PipeEditor.h"

namespace Game {
namespace fs = std::filesystem;

// ====== Static State ======
static std::deque<UndoCommand> undoStack;
static std::deque<UndoCommand> redoStack;
static constexpr size_t kMaxUndoDepth = 100;
// 笘・non-static: GameScene.cpp縺九ｉextern縺ｧ蜿ら・
GizmoMode currentGizmoMode = GizmoMode::Translate;
static std::deque<LogEntry> consoleLog;
static constexpr size_t kMaxConsoleLines = 500;
static float globalTime = 0.0f;

// 笘・繝薙Η繝ｼ繝昴・繝域桃菴懃畑縺ｮ迥ｶ諷・(non-static: extern蜿ら・)
bool gizmoDragging = false;
int gizmoDragAxis = -1; // 0=X, 1=Y, 2=Z
static std::map<int, Engine::Transform> dragStartTransforms = {};
static ImVec2 gizmoDragStartMouse = {};
static bool objectDragging = false;               // 笘・閾ｪ逕ｱ繝峨Λ繝・げ荳ｭ繝輔Λ繧ｰ
static std::vector<SceneObject> clipboardObjects; // Ctrl+C 繧ｳ繝斐・逕ｨ

// ★ Gameウィンドウの画像座標 (ピッキング用)
static ImVec2 gameImageMin = {};
static ImVec2 gameImageMax = {};

static PipeEditor s_pipeEditor;
static uint32_t nextObjectId = 1;
static uint32_t GenerateId() { return nextObjectId++; }

// 笘・繧ｳ繝斐・/隍・｣ｽ譎ゅ・繝ｦ繝九・繧ｯ蜷咲函謌・
static std::string GenerateCopyName(const std::string& baseName, const std::vector<SceneObject>& objects) {
	// 譛ｫ蟆ｾ縺ｮ "_謨ｰ蟄・ 繧・" (Copy)" 繧帝勁蜴ｻ縺励※繝吶・繧ｹ蜷阪ｒ蜿門ｾ・
	std::string base = baseName;
	// " (Copy)" 縺ｮ郢ｰ繧願ｿ斐＠繧帝勁蜴ｻ
	while (base.size() > 7 && base.substr(base.size() - 7) == " (Copy)")
		base = base.substr(0, base.size() - 7);
	// "_謨ｰ蟄・ 繧帝勁蜴ｻ
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
	if (base.empty())
		base = "Object";
	// 譌｢蟄倥・繧ｪ繝悶ず繧ｧ繧ｯ繝亥錐縺九ｉ譛螟ｧ逡ｪ蜿ｷ繧呈爾縺・
	int maxNum = 0;
	for (const auto& obj : objects) {
		if (obj.name.size() > base.size() + 1 && obj.name.substr(0, base.size()) == base && obj.name[base.size()] == '_') {
			std::string numPart = obj.name.substr(base.size() + 1);
			bool allDigit = true;
			for (char c : numPart)
				if (!isdigit((unsigned char)c)) {
					allDigit = false;
					break;
				}
			if (allDigit && !numPart.empty()) {
				int n = std::stoi(numPart);
				if (n > maxNum)
					maxNum = n;
			}
		}
	}
	return base + "_" + std::to_string(maxNum + 1);
}

// ====== Undo/Redo ======
void EditorUI::PushUndo(const UndoCommand& cmd) {
	undoStack.push_back(cmd);
	if (undoStack.size() > kMaxUndoDepth)
		undoStack.pop_front();
	redoStack.clear();
}
void EditorUI::Undo() {
	if (undoStack.empty())
		return;
	auto c = undoStack.back();
	undoStack.pop_back();
	c.undo();
	redoStack.push_back(c);
}
void EditorUI::Redo() {
	if (redoStack.empty())
		return;
	auto c = redoStack.back();
	redoStack.pop_back();
	c.redo();
	undoStack.push_back(c);
}

// ====== Console ======
void EditorUI::Log(const std::string& msg) {
	consoleLog.push_back({LogLevel::Info, msg, globalTime});
	if (consoleLog.size() > kMaxConsoleLines)
		consoleLog.pop_front();
}
void EditorUI::LogWarning(const std::string& msg) {
	consoleLog.push_back({LogLevel::Warning, msg, globalTime});
	if (consoleLog.size() > kMaxConsoleLines)
		consoleLog.pop_front();
}
void EditorUI::LogError(const std::string& msg) {
	consoleLog.push_back({LogLevel::Error, msg, globalTime});
	if (consoleLog.size() > kMaxConsoleLines)
		consoleLog.pop_front();
}

// ====== JSON Save ======
static std::string EscapeJson(const std::string& s) {
	std::string o;
	for (char c : s) {
		if (c == '"')
			o += "\\\"";
		else if (c == '\\')
			o += "\\\\";
		else
			o += c;
	}
	return o;
}

static std::string SerializeSceneObject(const SceneObject& o) {
	std::stringstream ss;
	ss << "    {\n";
	ss << "      \"id\": " << o.id << ",\n";
	ss << "      \"parentId\": " << o.parentId << ",\n";
	ss << "      \"name\": \"" << EscapeJson(o.name) << "\",\n";
	ss << "      \"locked\": " << (o.locked ? "true" : "false") << ",\n";
	ss << "      \"modelPath\": \"" << EscapeJson(o.modelPath) << "\",\n";
	ss << "      \"texturePath\": \"" << EscapeJson(o.texturePath) << "\",\n";
	ss << "      \"translate\": [" << o.translate.x << ", " << o.translate.y << ", " << o.translate.z << "],\n";
	ss << "      \"rotate\": [" << o.rotate.x << ", " << o.rotate.y << ", " << o.rotate.z << "],\n";
	ss << "      \"scale\": [" << o.scale.x << ", " << o.scale.y << ", " << o.scale.z << "],\n";
	ss << "      \"color\": [" << o.color.x << ", " << o.color.y << ", " << o.color.z << ", " << o.color.w << "],\n";
	ss << "      \"extraTexturePaths\": [";
	for (size_t i = 0; i < o.extraTexturePaths.size(); ++i) {
		ss << "\"" << EscapeJson(o.extraTexturePaths[i]) << "\"" << (i == o.extraTexturePaths.size() - 1 ? "" : ", ");
	}
	ss << "],\n";
	ss << "      \"shaderName\": \"" << EscapeJson(o.shaderName) << "\",\n";
	ss << "      \"components\": [\n";
	bool first = true;
	for (const auto& mr : o.meshRenderers) {
		if (!first)
			ss << ",\n";
		first = false;
		ss << "        {\"type\": \"MeshRenderer\", \"enabled\": " << (mr.enabled ? "true" : "false") << ", \"modelPath\": \"" << EscapeJson(mr.modelPath) << "\", \"texturePath\": \""
		   << EscapeJson(mr.texturePath) << "\", \"color\": [" << mr.color.x << "," << mr.color.y << "," << mr.color.z << "," << mr.color.w << "], \"uvTiling\": [" << mr.uvTiling.x << ","
		   << mr.uvTiling.y << "], \"uvOffset\": [" << mr.uvOffset.x << "," << mr.uvOffset.y << "], \"extraTexturePaths\": [";
		for (size_t i = 0; i < mr.extraTexturePaths.size(); ++i) {
			ss << "\"" << EscapeJson(mr.extraTexturePaths[i]) << "\"" << (i == mr.extraTexturePaths.size() - 1 ? "" : ", ");
		}
		ss << "], \"lightmapPath\": \"" << EscapeJson(mr.lightmapPath) << "\", \"shaderName\": \"" << EscapeJson(mr.shaderName) << "\"}";
	}
	for (const auto& bc : o.boxColliders) {
		if (!first)
			ss << ",\n";
		first = false;
		ss << "        {\"type\": \"BoxCollider\", \"enabled\": " << (bc.enabled ? "true" : "false") << ", \"center\": [" << bc.center.x << "," << bc.center.y << "," << bc.center.z << "], \"size\": ["
		   << bc.size.x << "," << bc.size.y << "," << bc.size.z << "], \"isTrigger\": " << (bc.isTrigger ? "true" : "false") << "}";
	}
	for (const auto& tg : o.tags) {
		if (!first)
			ss << ",\n";
		first = false;
		ss << "        {\"type\": \"Tag\", \"enabled\": " << (tg.enabled ? "true" : "false") << ", \"tag\": \"" << EscapeJson(tg.tag) << "\"}";
	}
	for (const auto& an : o.animators) {
		if (!first)
			ss << ",\n";
		first = false;
		ss << "        {\"type\": \"Animator\", \"enabled\": " << (an.enabled ? "true" : "false") << ", \"currentAnimation\": \"" << EscapeJson(an.currentAnimation)
		   << "\", \"isPlaying\": " << (an.isPlaying ? "true" : "false") << ", \"loop\": " << (an.loop ? "true" : "false") << ", \"speed\": [" << an.speed << "], \"time\": [" << an.time << "]}";
	}
	for (const auto& rb : o.rigidbodies) {
		if (!first)
			ss << ",\n";
		first = false;
		ss << "        {\"type\": \"Rigidbody\", \"enabled\": " << (rb.enabled ? "true" : "false") << ", \"velocity\": [" << rb.velocity.x << "," << rb.velocity.y << "," << rb.velocity.z
		   << "], \"useGravity\": " << (rb.useGravity ? "true" : "false") << ", \"isKinematic\": " << (rb.isKinematic ? "true" : "false") << "}";
	}
	// 笘・ｿｽ蜉: ParticleEmitter縺ｮ繧ｷ繝ｪ繧｢繝ｩ繧､繧ｺ
	for (const auto& pe : o.particleEmitters) {
		if (!first)
			ss << ",\n";
		first = false;
		const auto& p = pe.emitter.params;
		ss << "        {\"type\": \"ParticleEmitter\", \"enabled\": " << (pe.enabled ? "true" : "false") << ", \"isPlaying\": " << (pe.emitter.isPlaying ? "true" : "false")
		   << ", \"emitRate\": " << p.emitRate << ", \"burstCount\": " << p.burstCount;
		ss << ", \"lifeTime\": " << p.lifeTime << ", \"lifeTimeVariance\": " << p.lifeTimeVariance;
		ss << ", \"startVelocity\": [" << p.startVelocity.x << "," << p.startVelocity.y << "," << p.startVelocity.z << "], \"velocityVariance\": [" << p.velocityVariance.x << ","
		   << p.velocityVariance.y << "," << p.velocityVariance.z << "], \"acceleration\": [" << p.acceleration.x << "," << p.acceleration.y << "," << p.acceleration.z << "]";
		ss << ", \"startSize\": [" << p.startSize.x << "," << p.startSize.y << "," << p.startSize.z << "], \"endSize\": [" << p.endSize.x << "," << p.endSize.y << "," << p.endSize.z << "]";
		ss << ", \"startColor\": [" << p.startColor.x << "," << p.startColor.y << "," << p.startColor.z << "," << p.startColor.w << "], \"endColor\": [" << p.endColor.x << "," << p.endColor.y << ","
		   << p.endColor.z << "," << p.endColor.w << "]";
		ss << ", \"isAdditive\": " << (p.isAdditive ? "true" : "false");
		ss << ", \"assetPath\": \"" << EscapeJson(pe.assetPath) << "\"";
		ss << "}";
	}
	for (const auto& gmc : o.gpuMeshColliders) {
		if (!first)
			ss << ",\n";
		first = false;
		ss << "        {\"type\": \"GpuMeshCollider\", \"enabled\": " << (gmc.enabled ? "true" : "false") << ", \"isTrigger\": " << (gmc.isTrigger ? "true" : "false")
		   << ", \"collisionType\": " << (int)gmc.collisionType << ", \"meshPath\": \"" << EscapeJson(gmc.meshPath) << "\"}";
	}
	// 笘・ｿｽ蜉: PlayerInput
	for (const auto& pi : o.playerInputs) {
		if (!first)
			ss << ",\n";
		first = false;
		ss << "        {\"type\": \"PlayerInput\", \"enabled\": " << (pi.enabled ? "true" : "false") << "}";
	}
	// 笘・ｿｽ蜉: CharacterMovement
	for (const auto& cm : o.characterMovements) {
		if (!first)
			ss << ",\n";
		first = false;
		ss << "        {\"type\": \"CharacterMovement\", \"enabled\": " << (cm.enabled ? "true" : "false") << ", \"speed\": " << cm.speed << ", \"jumpPower\": " << cm.jumpPower
		   << ", \"gravity\": " << cm.gravity << "}";
	}
	// 笘・ｿｽ蜉: CameraTarget
	for (const auto& ct : o.cameraTargets) {
		if (!first)
			ss << ",\n";
		first = false;
		ss << "        {\"type\": \"CameraTarget\", \"enabled\": " << (ct.enabled ? "true" : "false") << ", \"distance\": " << ct.distance << ", \"height\": " << ct.height
		   << ", \"smoothSpeed\": " << ct.smoothSpeed << "}";
	}
	// 笘・ｿｽ蜉: Light Components
	for (const auto& dl : o.directionalLights) {
		if (!first)
			ss << ",\n";
		first = false;
		ss << "        {\"type\": \"DirectionalLight\", \"enabled\": " << (dl.enabled ? "true" : "false") << ", \"color\": [" << dl.color.x << "," << dl.color.y << "," << dl.color.z
		   << "], \"intensity\": " << dl.intensity << "}";
	}
	for (const auto& pl : o.pointLights) {
		if (!first)
			ss << ",\n";
		first = false;
		ss << "        {\"type\": \"PointLight\", \"enabled\": " << (pl.enabled ? "true" : "false") << ", \"color\": [" << pl.color.x << "," << pl.color.y << "," << pl.color.z
		   << "], \"intensity\": " << pl.intensity << ", \"range\": " << pl.range << ", \"atten\": [" << pl.atten.x << "," << pl.atten.y << "," << pl.atten.z << "]}";
	}
	for (const auto& sl : o.spotLights) {
		if (!first)
			ss << ",\n";
		first = false;
		ss << "        {\"type\": \"SpotLight\", \"enabled\": " << (sl.enabled ? "true" : "false") << ", \"color\": [" << sl.color.x << "," << sl.color.y << "," << sl.color.z
		   << "], \"intensity\": " << sl.intensity << ", \"range\": " << sl.range << ", \"innerCos\": " << sl.innerCos << ", \"outerCos\": " << sl.outerCos << ", \"atten\": [" << sl.atten.x << ","
		   << sl.atten.y << "," << sl.atten.z << "]}";
	}
	// 笘・ｿｽ蜉: AudioSource
	for (const auto& as : o.audioSources) {
		if (!first)
			ss << ",\n";
		first = false;
		ss << "        {\"type\": \"AudioSource\", \"enabled\": " << (as.enabled ? "true" : "false") << ", \"soundPath\": \"" << EscapeJson(as.soundPath) << "\", \"volume\": " << as.volume
		   << ", \"loop\": " << (as.loop ? "true" : "false") << ", \"playOnStart\": " << (as.playOnStart ? "true" : "false") << ", \"is3D\": " << (as.is3D ? "true" : "false")
		   << ", \"maxDistance\": " << as.maxDistance << "}";
	}
	// 笘・ｿｽ蜉: AudioListener
	for (const auto& al : o.audioListeners) {
		if (!first)
			ss << ",\n";
		first = false;
		ss << "        {\"type\": \"AudioListener\", \"enabled\": " << (al.enabled ? "true" : "false") << "}";
	}
	// 笘・ｿｽ蜉: Hitbox
	for (const auto& hb : o.hitboxes) {
		if (!first)
			ss << ",\n";
		first = false;
		ss << "        {\"type\": \"Hitbox\", \"enabled\": " << (hb.enabled ? "true" : "false") << ", \"center\": [" << hb.center.x << "," << hb.center.y << "," << hb.center.z << "], \"size\": ["
		   << hb.size.x << "," << hb.size.y << "," << hb.size.z << "], \"damage\": " << hb.damage << ", \"isActive\": " << (hb.isActive ? "true" : "false") << ", \"tag\": \"" << EscapeJson(hb.tag)
		   << "\"}";
	}
	// 笘・ｿｽ蜉: Hurtbox
	for (const auto& hb : o.hurtboxes) {
		if (!first)
			ss << ",\n";
		first = false;
		ss << "        {\"type\": \"Hurtbox\", \"enabled\": " << (hb.enabled ? "true" : "false") << ", \"center\": [" << hb.center.x << "," << hb.center.y << "," << hb.center.z << "], \"size\": ["
		   << hb.size.x << "," << hb.size.y << "," << hb.size.z << "], \"tag\": \"" << EscapeJson(hb.tag) << "\", \"damageMultiplier\": " << hb.damageMultiplier << "}";
	}
	// 笘・ｿｽ蜉: Health
	for (const auto& hc : o.healths) {
		if (!first)
			ss << ",\n";
		first = false;
		ss << "        {\"type\": \"Health\", \"enabled\": " << (hc.enabled ? "true" : "false") << ", \"hp\": " << hc.hp << ", \"maxHp\": " << hc.maxHp << ", \"stamina\": " << hc.stamina
		   << ", \"maxStamina\": " << hc.maxStamina << ", \"invincibleTime\": " << hc.invincibleTime << ", \"isDead\": " << (hc.isDead ? "true" : "false") << "}";
	}
	// 笘・ｿｽ蜉: Script
	for (const auto& sc : o.scripts) {
		if (!first)
			ss << ",\n";
		first = false;
		ss << "        {\"type\": \"Script\", \"enabled\": " << (sc.enabled ? "true" : "false") << ", \"scriptPath\": \"" << EscapeJson(sc.scriptPath) << "\"}";
	}
	// 笘・ｿｽ蜉: UI Components
	for (const auto& rt : o.rectTransforms) {
		if (!first) ss << ",\n"; first = false;
		ss << "        {\"type\": \"RectTransform\", \"enabled\": " << (rt.enabled ? "true" : "false") << ", \"pos\": [" << rt.pos.x << "," << rt.pos.y << "], \"size\": [" << rt.size.x << "," << rt.size.y << "], \"anchor\": [" << rt.anchor.x << "," << rt.anchor.y << "], \"pivot\": [" << rt.pivot.x << "," << rt.pivot.y << "], \"rotation\": " << rt.rotation << "}";
	}
	for (const auto& img : o.images) {
		if (!first) ss << ",\n"; first = false;
		ss << "        {\"type\": \"UIImage\", \"enabled\": " << (img.enabled ? "true" : "false") << ", \"texturePath\": \"" << EscapeJson(img.texturePath) << "\", \"color\": [" << img.color.x << "," << img.color.y << "," << img.color.z << "," << img.color.w << "]}";
	}
	for (const auto& txt : o.texts) {
		if (!first) ss << ",\n"; first = false;
		ss << "        {\"type\": \"UIText\", \"enabled\": " << (txt.enabled ? "true" : "false") << ", \"text\": \"" << EscapeJson(txt.text) << "\", \"fontSize\": " << txt.fontSize << ", \"color\": [" << txt.color.x << "," << txt.color.y << "," << txt.color.z << "," << txt.color.w << "]}";
	}
	for (const auto& btn : o.buttons) {
		if (!first) ss << ",\n"; first = false;
		ss << "        {\"type\": \"UIButton\", \"enabled\": " << (btn.enabled ? "true" : "false") << ", \"normalColor\": [" << btn.normalColor.x << "," << btn.normalColor.y << "," << btn.normalColor.z << "," << btn.normalColor.w << "], \"hoverColor\": [" << btn.hoverColor.x << "," << btn.hoverColor.y << "," << btn.hoverColor.z << "," << btn.hoverColor.w << "], \"pressedColor\": [" << btn.pressedColor.x << "," << btn.pressedColor.y << "," << btn.pressedColor.z << "," << btn.pressedColor.w << "]}";
	}
	ss << "\n      ]\n";
	ss << "    }";
	return ss.str();
}

void EditorUI::SaveScene(GameScene* scene, const std::string& path) {
	if (!scene)
		return;
	std::ofstream f(path);
	if (!f.is_open()) {
		LogError("Save failed: " + path);
		return;
	}
	f << "{\n  \"settings\": {\n";
	auto* r = Engine::Renderer::GetInstance();
	auto pp = r->GetPostProcessParams();
	f << "    \"postProcessEnabled\": " << (r->GetPostProcessEnabled() ? "true" : "false") << ",\n";
	f << "    \"vignette\": " << pp.vignette << ",\n";
	f << "    \"distortion\": " << pp.distortion << ",\n";
	f << "    \"noiseStrength\": " << pp.noiseStrength << ",\n";
	f << "    \"chromaShift\": " << pp.chromaShift << ",\n";
	f << "    \"scanline\": " << pp.scanline << "\n";
	f << "  },\n";
	f << "  \"objects\": [\n";

	// 笘・ｿｽ蜉: Git縺ｧ縺ｮ遶ｶ蜷・繧ｳ繝ｳ繝輔Μ繧ｯ繝・繧帝亟縺舌◆繧√√が繝悶ず繧ｧ繧ｯ繝医ｒ蜷榊燕鬆・↓繧ｽ繝ｼ繝医＠縺ｦ菫晏ｭ倥☆繧・
	std::vector<SceneObject> sortedObjects = scene->objects_;
	std::stable_sort(sortedObjects.begin(), sortedObjects.end(), [](const SceneObject& a, const SceneObject& b) {
		return a.name < b.name;
	});

	for (size_t i = 0; i < sortedObjects.size(); ++i) {
		if (i > 0)
			f << ",\n";
		f << SerializeSceneObject(sortedObjects[i]);
	}
	f << "\n  ]\n}\n";
	f.close();
	Log("Scene saved: " + path + " (" + std::to_string(scene->objects_.size()) + " objects)");
}

// ====== JSON Load ======
static std::string UnescapeJson(const std::string& s) {
	std::string o;
	for (size_t i = 0; i < s.size(); ++i) {
		if (s[i] == '\\' && i + 1 < s.size()) {
			o += s[i + 1];
			i++;
		} else {
			o += s[i];
		}
	}
	return o;
}

static std::string ExtractString(const std::string& block, const std::string& key) {
	auto pos = block.find("\"" + key + "\"");
	if (pos == std::string::npos)
		return "";
	auto q1 = block.find("\"", block.find(":", pos) + 1);
	if (q1 == std::string::npos)
		return "";
	size_t q2 = q1 + 1;
	while (q2 < block.size()) {
		if (block[q2] == '\\')
			q2 += 2;
		else if (block[q2] == '"')
			break;
		else
			q2++;
	}
	if (q2 >= block.size())
		return "";
	return UnescapeJson(block.substr(q1 + 1, q2 - q1 - 1));
}

static size_t FindBlockEnd(const std::string& str, size_t startPos) {
	int depth = 0;
	bool inString = false;
	bool escape = false;

	for (size_t i = startPos; i < str.size(); ++i) {
		char c = str[i];
		if (escape) {
			escape = false;
			continue;
		}
		if (c == '\\') {
			escape = true;
			continue;
		}
		if (c == '"') {
			inString = !inString;
			continue;
		}

		if (!inString) {
			if (c == '{' || c == '[')
				depth++;
			else if (c == '}' || c == ']') {
				depth--;
				if (depth <= 0)
					return i;
			}
		}
	}
	return std::string::npos;
}

static std::vector<std::string> ExtractStringArray(const std::string& block, const std::string& key) {
	std::vector<std::string> res;
	auto pos = block.find("\"" + key + "\"");
	if (pos == std::string::npos) return res;
	auto arrStart = block.find("[", pos);
	if (arrStart == std::string::npos) return res;
	auto arrEnd = FindBlockEnd(block, arrStart);
	if (arrEnd == std::string::npos) return res;

	std::string ab = block.substr(arrStart + 1, arrEnd - arrStart - 1);
	size_t cur = 0;
	while (cur < ab.size()) {
		auto q1 = ab.find("\"", cur);
		if (q1 == std::string::npos) break;
		size_t q2 = q1 + 1;
		while (q2 < ab.size()) {
			if (ab[q2] == '\\') q2 += 2;
			else if (ab[q2] == '"') break;
			else q2++;
		}
		if (q2 >= ab.size()) break;
		res.push_back(UnescapeJson(ab.substr(q1 + 1, q2 - q1 - 1)));
		cur = q2 + 1;
	}
	return res;
}
static std::vector<float> ExtractArray(const std::string& block, const std::string& key) {
	std::vector<float> r;
	auto pos = block.find("\"" + key + "\"");
	if (pos == std::string::npos)
		return r;
	auto b = block.find("[", pos);
	auto e = block.find("]", b);
	if (b == std::string::npos || e == std::string::npos)
		return r;
	std::istringstream ss(block.substr(b + 1, e - b - 1));
	float v;
	while (ss >> v) {
		r.push_back(v);
		char c;
		ss >> c;
	}
	return r;
}

static float ExtractFloat(const std::string& block, const std::string& key, float defaultVal) {
	auto pos = block.find("\"" + key + "\"");
	if (pos == std::string::npos)
		return defaultVal;
	auto col = block.find(":", pos);
	if (col == std::string::npos)
		return defaultVal;
	std::string s = block.substr(col + 1);
	// Skip spaces, commas
	size_t start = 0;
	while (start < s.size() && (std::isspace((unsigned char)s[start]) || s[start] == ':' || s[start] == ','))
		start++;
	return (float)std::atof(s.c_str() + start);
}

static uint32_t ExtractUint(const std::string& block, const std::string& key, uint32_t defaultVal) {
	auto pos = block.find("\"" + key + "\"");
	if (pos == std::string::npos)
		return defaultVal;
	auto col = block.find(":", pos);
	if (col == std::string::npos)
		return defaultVal;
	std::string s = block.substr(col + 1);
	size_t start = 0;
	while (start < s.size() && (std::isspace((unsigned char)s[start]) || (unsigned char)s[start] == ':' || (unsigned char)s[start] == ','))
		start++;
	if (start >= s.size() || !isdigit((unsigned char)s[start])) return defaultVal;
	return (uint32_t)std::stoul(s.substr(start));
}

static bool ExtractBool(const std::string& block, const std::string& key, bool defaultVal) {
	auto pos = block.find("\"" + key + "\"");
	if (pos == std::string::npos)
		return defaultVal;
	if (block.find("true", pos) != std::string::npos && block.find("true", pos) < pos + 30)
		return true;
	if (block.find("false", pos) != std::string::npos && block.find("false", pos) < pos + 30)
		return false;
	return defaultVal;
}

static void ParseComponents(SceneObject& obj, const std::string& block, Engine::Renderer* renderer) {
	auto compStart = block.find("\"components\"");
	if (compStart == std::string::npos)
		return;
	auto arrStart = block.find("[", compStart);
	if (arrStart == std::string::npos)
		return;
	auto arrEnd = FindBlockEnd(block, arrStart);
	if (arrEnd == std::string::npos)
		return;

	size_t pos = arrStart + 1;
	while (pos < arrEnd) {
		pos = block.find("{", pos);
		if (pos == std::string::npos || pos > arrEnd)
			break;
		auto endPos = FindBlockEnd(block, pos);
		if (endPos == std::string::npos || endPos > arrEnd)
			break;
		std::string cblock = block.substr(pos, endPos - pos + 1);
		std::string type = ExtractString(cblock, "type");
		bool enabled = true;
		auto lkPos = cblock.find("\"enabled\"");
		if (lkPos != std::string::npos && cblock.find("false", lkPos) != std::string::npos && cblock.find("false", lkPos) < lkPos + 30)
			enabled = false;

		if (type == "MeshRenderer") {
			MeshRendererComponent mr;
			mr.enabled = enabled;
			mr.modelPath = ExtractString(cblock, "modelPath");
			if (!mr.modelPath.empty())
				mr.modelHandle = renderer->LoadObjMesh(mr.modelPath);
			mr.texturePath = ExtractString(cblock, "texturePath");
			if (!mr.texturePath.empty())
				mr.textureHandle = renderer->LoadTexture2D(mr.texturePath);
			auto co = ExtractArray(cblock, "color");
			if (co.size() >= 4)
				mr.color = {co[0], co[1], co[2], co[3]};
			auto uvt = ExtractArray(cblock, "uvTiling");
			if (uvt.size() >= 2)
				mr.uvTiling = {uvt[0], uvt[1]};
			auto uvo = ExtractArray(cblock, "uvOffset");
			if (uvo.size() >= 2)
				mr.uvOffset = {uvo[0], uvo[1]};
			mr.lightmapPath = ExtractString(cblock, "lightmapPath");
			if (!mr.lightmapPath.empty())
				mr.lightmapHandle = renderer->LoadTexture2D(mr.lightmapPath);
			mr.extraTexturePaths = ExtractStringArray(cblock, "extraTexturePaths");
			for (const auto& p : mr.extraTexturePaths) {
				mr.extraTextureHandles.push_back(renderer->LoadTexture2D(p));
			}
			mr.shaderName = ExtractString(cblock, "shaderName");
			if (mr.shaderName.empty()) mr.shaderName = "Default";
			obj.meshRenderers.push_back(mr);
		} else if (type == "BoxCollider") {
			BoxColliderComponent bc;
			bc.enabled = enabled;
			auto cen = ExtractArray(cblock, "center");
			if (cen.size() >= 3)
				bc.center = {cen[0], cen[1], cen[2]};
			auto sz = ExtractArray(cblock, "size");
			if (sz.size() >= 3)
				bc.size = {sz[0], sz[1], sz[2]};
			bc.isTrigger = ExtractBool(cblock, "isTrigger", false);
			obj.boxColliders.push_back(bc);
		} else if (type == "Tag") {
			TagComponent tg;
			tg.enabled = enabled;
			tg.tag = ExtractString(cblock, "tag");
			obj.tags.push_back(tg);
		} else if (type == "Animator") {
			AnimatorComponent an;
			an.enabled = enabled;
			an.currentAnimation = ExtractString(cblock, "currentAnimation");
			auto iPos = cblock.find("\"isPlaying\"");
			if (iPos != std::string::npos && cblock.find("true", iPos) != std::string::npos && cblock.find("true", iPos) < iPos + 30)
				an.isPlaying = true;
			else
				an.isPlaying = false;
			auto lPos = cblock.find("\"loop\"");
			if (lPos != std::string::npos && cblock.find("true", lPos) != std::string::npos && cblock.find("true", lPos) < lPos + 30)
				an.loop = true;
			else
				an.loop = false;
			auto sp = ExtractArray(cblock, "speed");
			if (sp.size() >= 1)
				an.speed = sp[0];
			auto tm = ExtractArray(cblock, "time");
			if (tm.size() >= 1)
				an.time = tm[0];
			obj.animators.push_back(an);
		} else if (type == "Rigidbody") {
			RigidbodyComponent rb;
			rb.enabled = enabled;
			auto vl = ExtractArray(cblock, "velocity");
			if (vl.size() >= 3)
				rb.velocity = {vl[0], vl[1], vl[2]};
			auto gPos = cblock.find("\"useGravity\"");
			if (gPos != std::string::npos && cblock.find("false", gPos) != std::string::npos && cblock.find("false", gPos) < gPos + 30)
				rb.useGravity = false;
			else
				rb.useGravity = true;
			auto kPos = cblock.find("\"isKinematic\"");
			if (kPos != std::string::npos && cblock.find("true", kPos) != std::string::npos && cblock.find("true", kPos) < kPos + 30)
				rb.isKinematic = true;
			else
				rb.isKinematic = false;
			obj.rigidbodies.push_back(rb);
		} else if (type == "ParticleEmitter") { // 笘・ｿｽ蜉
			ParticleEmitterComponent pe;
			pe.enabled = enabled;
			pe.emitter.Initialize(*Engine::Renderer::GetInstance(), "LoadedEmitter");

			// assetPath 縺後≠繧後・ ParticleEmitter 閾ｪ霄ｫ縺ｫ繝輔ぃ繧､繝ｫ縺九ｉ蠕ｩ蜈・＆縺帙ｋ
			pe.assetPath = ExtractString(cblock, "assetPath");
			if (!pe.assetPath.empty()) {
				pe.emitter.LoadFromJson(pe.assetPath);
			}

			// JSON蜀・↓繧ゆｸ頑嶌縺阪ヱ繝ｩ繝｡繝ｼ繧ｿ繝ｼ縺後≠繧句ｴ蜷医・繝輔か繝ｼ繝ｫ繝舌ャ繧ｯ・亥ｾ捺擂縺ｨ縺ｮ莠呈鋤諤ｧ逕ｨ・・
			auto& p = pe.emitter.params;
			auto boolCheck = [&](const std::string& k, bool def) {
				auto pos = cblock.find("\"" + k + "\"");
				if (pos == std::string::npos)
					return def;
				return cblock.find("true", pos) < pos + 30;
			};
			if (cblock.find("\"isPlaying\"") != std::string::npos)
				pe.emitter.isPlaying = boolCheck("isPlaying", true);
			if (cblock.find("\"emitRate\"") != std::string::npos)
				p.emitRate = ExtractFloat(cblock, "emitRate", 10.0f);
			if (cblock.find("\"burstCount\"") != std::string::npos)
				p.burstCount = (int)ExtractFloat(cblock, "burstCount", 0.0f);
			if (cblock.find("\"lifeTime\"") != std::string::npos)
				p.lifeTime = ExtractFloat(cblock, "lifeTime", 1.0f);
			if (cblock.find("\"lifeTimeVariance\"") != std::string::npos)
				p.lifeTimeVariance = ExtractFloat(cblock, "lifeTimeVariance", 0.2f);
			auto vel = ExtractArray(cblock, "startVelocity");
			if (vel.size() >= 3)
				p.startVelocity = {vel[0], vel[1], vel[2]};
			auto vRand = ExtractArray(cblock, "velocityVariance");
			if (vRand.size() >= 3)
				p.velocityVariance = {vRand[0], vRand[1], vRand[2]};
			auto acc = ExtractArray(cblock, "acceleration");
			if (acc.size() >= 3)
				p.acceleration = {acc[0], acc[1], acc[2]};
			auto ss = ExtractArray(cblock, "startSize");
			if (ss.size() >= 3)
				p.startSize = {ss[0], ss[1], ss[2]};
			auto es = ExtractArray(cblock, "endSize");
			if (es.size() >= 3)
				p.endSize = {es[0], es[1], es[2]};
			auto sc = ExtractArray(cblock, "startColor");
			if (sc.size() >= 4)
				p.startColor = {sc[0], sc[1], sc[2], sc[3]};
			auto ec = ExtractArray(cblock, "endColor");
			if (ec.size() >= 4)
				p.endColor = {ec[0], ec[1], ec[2], ec[3]};
			if (cblock.find("\"isAdditive\"") != std::string::npos)
				p.isAdditive = boolCheck("isAdditive", false);
			obj.particleEmitters.push_back(pe);
		} else if (type == "GpuMeshCollider") { // 笘・ｿｽ蜉
			GpuMeshColliderComponent gmc;
			gmc.enabled = enabled;
			auto tPos = cblock.find("\"isTrigger\"");
			if (tPos != std::string::npos && cblock.find("true", tPos) != std::string::npos && cblock.find("true", tPos) < tPos + 30)
				gmc.isTrigger = true;
			else
				gmc.isTrigger = false;
			gmc.collisionType = (MeshCollisionType)(int)ExtractFloat(cblock, "collisionType", 0.0f);
			gmc.meshPath = ExtractString(cblock, "meshPath");
			if (!gmc.meshPath.empty())
				gmc.meshHandle = renderer->LoadObjMesh(gmc.meshPath);
			obj.gpuMeshColliders.push_back(gmc);
		} else if (type == "PlayerInput") { // 笘・ｿｽ蜉
			PlayerInputComponent pi;
			pi.enabled = enabled;
			obj.playerInputs.push_back(pi);
		} else if (type == "CharacterMovement") { // 笘・ｿｽ蜉
			CharacterMovementComponent cm;
			cm.enabled = enabled;
			if (cblock.find("\"speed\"") != std::string::npos)
				cm.speed = ExtractFloat(cblock, "speed", 5.0f);
			if (cblock.find("\"jumpPower\"") != std::string::npos)
				cm.jumpPower = ExtractFloat(cblock, "jumpPower", 6.0f);
			if (cblock.find("\"gravity\"") != std::string::npos)
				cm.gravity = ExtractFloat(cblock, "gravity", 9.8f);
			obj.characterMovements.push_back(cm);
		} else if (type == "CameraTarget") { // 笘・ｿｽ蜉
			CameraTargetComponent ct;
			ct.enabled = enabled;
			if (cblock.find("\"distance\"") != std::string::npos)
				ct.distance = ExtractFloat(cblock, "distance", 10.0f);
			if (cblock.find("\"height\"") != std::string::npos)
				ct.height = ExtractFloat(cblock, "height", 3.0f);
			if (cblock.find("\"smoothSpeed\"") != std::string::npos)
				ct.smoothSpeed = ExtractFloat(cblock, "smoothSpeed", 5.0f);
			obj.cameraTargets.push_back(ct);
		} else if (type == "DirectionalLight") { // 笘・ｿｽ蜉
			DirectionalLightComponent dl;
			dl.enabled = enabled;
			auto col = ExtractArray(cblock, "color");
			if (col.size() >= 3)
				dl.color = {col[0], col[1], col[2]};
			if (cblock.find("\"intensity\"") != std::string::npos)
				dl.intensity = ExtractFloat(cblock, "intensity", 1.0f);
			obj.directionalLights.push_back(dl);
		} else if (type == "PointLight") { // 笘・ｿｽ蜉
			PointLightComponent pl;
			pl.enabled = enabled;
			auto col = ExtractArray(cblock, "color");
			if (col.size() >= 3)
				pl.color = {col[0], col[1], col[2]};
			if (cblock.find("\"intensity\"") != std::string::npos)
				pl.intensity = ExtractFloat(cblock, "intensity", 1.0f);
			if (cblock.find("\"range\"") != std::string::npos)
				pl.range = ExtractFloat(cblock, "range", 10.0f);
			auto atten = ExtractArray(cblock, "atten");
			if (atten.size() >= 3)
				pl.atten = {atten[0], atten[1], atten[2]};
			obj.pointLights.push_back(pl);
		} else if (type == "SpotLight") { // 笘・ｿｽ蜉
			SpotLightComponent sl;
			sl.enabled = enabled;
			auto col = ExtractArray(cblock, "color");
			if (col.size() >= 3)
				sl.color = {col[0], col[1], col[2]};
			if (cblock.find("\"intensity\"") != std::string::npos)
				sl.intensity = ExtractFloat(cblock, "intensity", 1.0f);
			if (cblock.find("\"range\"") != std::string::npos)
				sl.range = ExtractFloat(cblock, "range", 20.0f);
			if (cblock.find("\"innerCos\"") != std::string::npos)
				sl.innerCos = ExtractFloat(cblock, "innerCos", 0.98f);
			if (cblock.find("\"outerCos\"") != std::string::npos)
				sl.outerCos = ExtractFloat(cblock, "outerCos", 0.90f);
			auto atten = ExtractArray(cblock, "atten");
			if (atten.size() >= 3)
				sl.atten = {atten[0], atten[1], atten[2]};
			obj.spotLights.push_back(sl);
		} else if (type == "AudioSource") { // 笘・ｿｽ蜉
			AudioSourceComponent as;
			as.enabled = enabled;
			as.soundPath = ExtractString(cblock, "soundPath");
			auto boolCheck = [&](const std::string& k, bool def) {
				auto pos = cblock.find("\"" + k + "\"");
				if (pos == std::string::npos)
					return def;
				return cblock.find("true", pos) < pos + 30;
			};
			if (cblock.find("\"volume\"") != std::string::npos)
				as.volume = ExtractFloat(cblock, "volume", 1.0f);
			if (cblock.find("\"loop\"") != std::string::npos)
				as.loop = boolCheck("loop", false);
			if (cblock.find("\"playOnStart\"") != std::string::npos)
				as.playOnStart = boolCheck("playOnStart", false);
			if (cblock.find("\"is3D\"") != std::string::npos)
				as.is3D = boolCheck("is3D", true);
			if (cblock.find("\"maxDistance\"") != std::string::npos)
				as.maxDistance = ExtractFloat(cblock, "maxDistance", 50.0f);
			// 髻ｳ螢ｰ繝輔ぃ繧､繝ｫ繧偵Ο繝ｼ繝・
			if (!as.soundPath.empty()) {
				auto* audio = Engine::Audio::GetInstance();
				if (audio)
					as.soundHandle = audio->Load(as.soundPath);
			}
			obj.audioSources.push_back(as);
		} else if (type == "AudioListener") { // 笘・ｿｽ蜉
			AudioListenerComponent al;
			al.enabled = enabled;
			obj.audioListeners.push_back(al);
		} else if (type == "Hitbox") { // 笘・ｿｽ蜉
			HitboxComponent hb;
			hb.enabled = enabled;
			auto cen = ExtractArray(cblock, "center");
			if (cen.size() >= 3)
				hb.center = {cen[0], cen[1], cen[2]};
			auto sz = ExtractArray(cblock, "size");
			if (sz.size() >= 3)
				hb.size = {sz[0], sz[1], sz[2]};
			if (cblock.find("\"damage\"") != std::string::npos)
				hb.damage = ExtractFloat(cblock, "damage", 10.0f);
			auto aPos = cblock.find("\"isActive\"");
			if (aPos != std::string::npos && cblock.find("true", aPos) != std::string::npos && cblock.find("true", aPos) < aPos + 30)
				hb.isActive = true;
			else
				hb.isActive = false;
			hb.tag = ExtractString(cblock, "tag");
			if (hb.tag.empty())
				hb.tag = "Default";
			obj.hitboxes.push_back(hb);
		} else if (type == "Hurtbox") { // 笘・ｿｽ蜉
			HurtboxComponent hb;
			hb.enabled = enabled;
			auto cen = ExtractArray(cblock, "center");
			if (cen.size() >= 3)
				hb.center = {cen[0], cen[1], cen[2]};
			auto sz = ExtractArray(cblock, "size");
			if (sz.size() >= 3)
				hb.size = {sz[0], sz[1], sz[2]};
			hb.tag = ExtractString(cblock, "tag");
			if (hb.tag.empty())
				hb.tag = "Body";
			if (cblock.find("\"damageMultiplier\"") != std::string::npos)
				hb.damageMultiplier = ExtractFloat(cblock, "damageMultiplier", 1.0f);
			obj.hurtboxes.push_back(hb);
		} else if (type == "Health") { // 笘・ｿｽ蜉
			HealthComponent hc;
			hc.enabled = enabled;
			if (cblock.find("\"hp\"") != std::string::npos)
				hc.hp = ExtractFloat(cblock, "hp", 100.0f);
			if (cblock.find("\"maxHp\"") != std::string::npos)
				hc.maxHp = ExtractFloat(cblock, "maxHp", 100.0f);
			if (cblock.find("\"stamina\"") != std::string::npos)
				hc.stamina = ExtractFloat(cblock, "stamina", 100.0f);
			if (cblock.find("\"maxStamina\"") != std::string::npos)
				hc.maxStamina = ExtractFloat(cblock, "maxStamina", 100.0f);
			if (cblock.find("\"invincibleTime\"") != std::string::npos)
				hc.invincibleTime = ExtractFloat(cblock, "invincibleTime", 0.0f);
			auto aPos = cblock.find("\"isDead\"");
			if (aPos != std::string::npos && cblock.find("true", aPos) != std::string::npos && cblock.find("true", aPos) < aPos + 30)
				hc.isDead = true;
			else
				hc.isDead = false;
			obj.healths.push_back(hc);
		} else if (type == "Script") { // 笘・ｿｽ蜉
			ScriptComponent sc;
			sc.enabled = enabled;
			sc.scriptPath = ExtractString(cblock, "scriptPath");
			obj.scripts.push_back(sc);
		} else if (type == "RectTransform") {
			RectTransformComponent rt;
			rt.enabled = enabled;
			auto p = ExtractArray(cblock, "pos"); if (p.size() >= 2) rt.pos = {p[0], p[1]};
			auto s = ExtractArray(cblock, "size"); if (s.size() >= 2) rt.size = {s[0], s[1]};
			auto a = ExtractArray(cblock, "anchor"); if (a.size() >= 2) rt.anchor = {a[0], a[1]};
			auto pv = ExtractArray(cblock, "pivot"); if (pv.size() >= 2) rt.pivot = {pv[0], pv[1]};
			rt.rotation = ExtractFloat(cblock, "rotation", 0.0f);
			obj.rectTransforms.push_back(rt);
		} else if (type == "UIImage") {
			UIImageComponent img;
			img.enabled = enabled;
			img.texturePath = ExtractString(cblock, "texturePath");
			if (renderer && !img.texturePath.empty()) img.textureHandle = renderer->LoadTexture2D(img.texturePath);
			auto c = ExtractArray(cblock, "color"); if (c.size() >= 4) img.color = {c[0], c[1], c[2], c[3]};
			obj.images.push_back(img);
		} else if (type == "UIText") {
			UITextComponent txt;
			txt.enabled = enabled;
			txt.text = UnescapeJson(ExtractString(cblock, "text"));
			txt.fontSize = ExtractFloat(cblock, "fontSize", 24.0f);
			auto c = ExtractArray(cblock, "color"); if (c.size() >= 4) txt.color = {c[0], c[1], c[2], c[3]};
			obj.texts.push_back(txt);
		} else if (type == "UIButton") {
			UIButtonComponent btn;
			btn.enabled = enabled;
			auto nc = ExtractArray(cblock, "normalColor"); if (nc.size() >= 4) btn.normalColor = {nc[0], nc[1], nc[2], nc[3]};
			auto hc = ExtractArray(cblock, "hoverColor"); if (hc.size() >= 4) btn.hoverColor = {hc[0], hc[1], hc[2], hc[3]};
			auto pc = ExtractArray(cblock, "pressedColor"); if (pc.size() >= 4) btn.pressedColor = {pc[0], pc[1], pc[2], pc[3]};
			obj.buttons.push_back(btn);
		}
		pos = endPos + 1;
	}
}
void EditorUI::LoadScene(GameScene* scene, const std::string& path) {
	if (!scene)
		return;
	std::ifstream f(path);
	if (!f.is_open()) {
		LogError("Load failed: " + path);
		return;
	}
	std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
	f.close();

	auto* renderer = Engine::Renderer::GetInstance();
	// 笘・ｿｽ蜉: Scene Settings 縺ｮ隱ｭ縺ｿ霎ｼ縺ｿ
	auto settingsStart = content.find("\"settings\"");
	if (settingsStart != std::string::npos) {
		auto blockStart = content.find("{", settingsStart);
		if (blockStart != std::string::npos) {
			auto blockEnd = FindBlockEnd(content, blockStart);
			if (blockEnd != std::string::npos) {
				std::string sblock = content.substr(blockStart, blockEnd - blockStart + 1);
				bool en = ExtractBool(sblock, "postProcessEnabled", renderer->GetPostProcessEnabled());
				renderer->SetPostProcessEnabled(en);
				auto pp = renderer->GetPostProcessParams();
				pp.vignette = ExtractFloat(sblock, "vignette", pp.vignette);
				pp.distortion = ExtractFloat(sblock, "distortion", pp.distortion);
				pp.noiseStrength = ExtractFloat(sblock, "noiseStrength", pp.noiseStrength);
				pp.chromaShift = ExtractFloat(sblock, "chromaShift", pp.chromaShift);
				pp.scanline = ExtractFloat(sblock, "scanline", pp.scanline);
				renderer->SetPostProcessParams(pp);
			}
		}
	}

	scene->objects_.clear();
	scene->selectedIndices_.clear();
	scene->selectedObjectIndex_ = -1;
	auto arrStart = content.find("[", content.find("\"objects\""));
	if (arrStart == std::string::npos) {
		LogError("Invalid scene file");
		return;
	}
	auto arrEnd = content.rfind("]");
	if (arrEnd == std::string::npos)
		arrEnd = content.size();
	size_t objStart = arrStart;
	while (objStart < arrEnd) {
		objStart = content.find("{", objStart);
		if (objStart == std::string::npos || objStart > arrEnd)
			break;
		// 譛蛻昴・ "{" 縺ｮ菴咲ｽｮ縺九ｉ謗｢縺吶◆繧√：indBlockEnd縺ｯobjStart縺九ｉ髢句ｧ具ｼ・indBlockEnd蜀・〒"{"繧偵き繧ｦ繝ｳ繝医☆繧具ｼ・
		auto objEnd = FindBlockEnd(content, objStart);
		if (objEnd == std::string::npos || objEnd > arrEnd)
			break;
		std::string block = content.substr(objStart, objEnd - objStart + 1);
		SceneObject obj;
		obj.id = ExtractUint(block, "id", 0);
		obj.parentId = ExtractUint(block, "parentId", 0);
		if (obj.id == 0) obj.id = GenerateId();
		if (obj.id >= nextObjectId) nextObjectId = obj.id + 1;

		obj.name = ExtractString(block, "name");
		obj.modelPath = ExtractString(block, "modelPath");
		obj.texturePath = ExtractString(block, "texturePath");
		obj.extraTexturePaths = ExtractStringArray(block, "extraTexturePaths");
		obj.shaderName = ExtractString(block, "shaderName");
		if (obj.shaderName.empty()) obj.shaderName = "Default";
		{
			auto lkPos = block.find("\"locked\"");
			if (lkPos != std::string::npos && block.find("true", lkPos) != std::string::npos && block.find("true", lkPos) < lkPos + 30)
				obj.locked = true;
		}
		auto tr = ExtractArray(block, "translate");
		if (tr.size() >= 3) {
			obj.translate = {tr[0], tr[1], tr[2]};
		}
		auto ro = ExtractArray(block, "rotate");
		if (ro.size() >= 3) {
			obj.rotate = {ro[0], ro[1], ro[2]};
		}
		auto sc = ExtractArray(block, "scale");
		if (sc.size() >= 3) {
			obj.scale = {sc[0], sc[1], sc[2]};
		}
		auto co = ExtractArray(block, "color");
		if (co.size() >= 4) {
			obj.color = {co[0], co[1], co[2], co[3]};
		} else if (co.size() >= 3) {
			obj.color = {co[0], co[1], co[2], 1};
		}
		if (!obj.modelPath.empty())
			obj.modelHandle = renderer->LoadObjMesh(obj.modelPath);
		if (!obj.texturePath.empty())
			obj.textureHandle = renderer->LoadTexture2D(obj.texturePath);
		ParseComponents(obj, block, renderer);
		scene->objects_.push_back(obj);
		objStart = objEnd;
	}
	Log("Scene loaded: " + path + " (" + std::to_string(scene->objects_.size()) + " objects)");
}

void EditorUI::AddScene(GameScene* scene, const std::string& path) {
	if (!scene)
		return;
	std::ifstream f(path);
	if (!f.is_open()) {
		LogError("AddScene failed: " + path);
		return;
	}
	std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
	f.close();

	auto* renderer = Engine::Renderer::GetInstance();
	auto arrStart = content.find("[", content.find("\"objects\""));
	if (arrStart == std::string::npos) {
		LogError("Invalid scene file (Add)");
		return;
	}
	auto arrEnd = content.rfind("]");
	if (arrEnd == std::string::npos)
		arrEnd = content.size();
	size_t objStart = arrStart;
	while (objStart < arrEnd) {
		objStart = content.find("{", objStart);
		if (objStart == std::string::npos || objStart > arrEnd)
			break;
		auto objEnd = FindBlockEnd(content, objStart);
		if (objEnd == std::string::npos || objEnd > arrEnd)
			break;
		std::string block = content.substr(objStart, objEnd - objStart + 1);
		SceneObject obj;
		obj.name = ExtractString(block, "name");
		// 蜷後§蜷榊燕縺後≠繧句ｴ蜷医・繧ｳ繝斐・蜷阪ｒ逕滓・・井ｻｻ諢擾ｼ壻ｸ頑嶌縺阪・譁ｹ縺後＞縺・ｴ蜷医ｂ縺ゅｋ縺悟ｮ牙・縺ｮ縺溘ａ・・
		obj.name = GenerateCopyName(obj.name, scene->objects_);

		obj.modelPath = ExtractString(block, "modelPath");
		obj.texturePath = ExtractString(block, "texturePath");
		{
			auto lkPos = block.find("\"locked\"");
			if (lkPos != std::string::npos && block.find("true", lkPos) != std::string::npos && block.find("true", lkPos) < lkPos + 30)
				obj.locked = true;
		}
		auto tr = ExtractArray(block, "translate");
		if (tr.size() >= 3) {
			obj.translate = {tr[0], tr[1], tr[2]};
		}
		auto ro = ExtractArray(block, "rotate");
		if (ro.size() >= 3) {
			obj.rotate = {ro[0], ro[1], ro[2]};
		}
		auto sc = ExtractArray(block, "scale");
		if (sc.size() >= 3) {
			obj.scale = {sc[0], sc[1], sc[2]};
		}
		auto co = ExtractArray(block, "color");
		if (co.size() >= 4) {
			obj.color = {co[0], co[1], co[2], co[3]};
		} else if (co.size() >= 3) {
			obj.color = {co[0], co[1], co[2], 1};
		}
		if (!obj.modelPath.empty())
			obj.modelHandle = renderer->LoadObjMesh(obj.modelPath);
		if (!obj.texturePath.empty())
			obj.textureHandle = renderer->LoadTexture2D(obj.texturePath);
		ParseComponents(obj, block, renderer);
		scene->objects_.push_back(obj);
		objStart = objEnd;
	}
	Log("Scene added: " + path + " (Total: " + std::to_string(scene->objects_.size()) + " objects)");
}

void EditorUI::LoadPrefab(GameScene* scene, const std::string& path) {
	if (!scene)
		return;
	std::ifstream f(path);
	if (!f.is_open()) {
		LogError("Prefab load failed: " + path);
		return;
	}
	std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
	f.close();

	size_t objStart = content.find("{");
	if (objStart == std::string::npos)
		return;
	size_t objEnd = FindBlockEnd(content, objStart);
	if (objEnd == std::string::npos)
		return;
	std::string block = content.substr(objStart, objEnd - objStart + 1);
	SceneObject obj;
	obj.name = GenerateCopyName(ExtractString(block, "name"), scene->objects_);
	obj.modelPath = ExtractString(block, "modelPath");
	obj.texturePath = ExtractString(block, "texturePath");
	auto tr = ExtractArray(block, "translate");
	if (tr.size() >= 3) {
		obj.translate = {tr[0], tr[1], tr[2]};
	}
	auto ro = ExtractArray(block, "rotate");
	if (ro.size() >= 3) {
		obj.rotate = {ro[0], ro[1], ro[2]};
	}
	auto sc = ExtractArray(block, "scale");
	if (sc.size() >= 3) {
		obj.scale = {sc[0], sc[1], sc[2]};
	}
	auto co = ExtractArray(block, "color");
	if (co.size() >= 4) {
		obj.color = {co[0], co[1], co[2], co[3]};
	} else if (co.size() >= 3) {
		obj.color = {co[0], co[1], co[2], 1};
	}
	auto* r = Engine::Renderer::GetInstance();
	if (!obj.modelPath.empty())
		obj.modelHandle = r->LoadObjMesh(obj.modelPath);
	if (!obj.texturePath.empty())
		obj.textureHandle = r->LoadTexture2D(obj.texturePath);

	ParseComponents(obj, block, r);

	// 蠕梧婿莠呈鋤諤ｧ・壹さ繝ｳ繝昴・繝阪Φ繝医′辟｡縺上√Δ繝・Ν縺後≠繧後・繝・ヵ繧ｩ繝ｫ繝医ｒ莉倅ｸ・
	if (obj.meshRenderers.empty() && !obj.modelPath.empty()) {
		MeshRendererComponent mr;
		mr.modelHandle = obj.modelHandle;
		mr.textureHandle = obj.textureHandle;
		mr.modelPath = obj.modelPath;
		mr.texturePath = obj.texturePath;
		mr.color = obj.color;
		obj.meshRenderers.push_back(mr);
	}

	scene->objects_.push_back(obj);
	Log("Prefab loaded and instantiated: " + path);
}

// ====== 笘・Ray-AABB 莠､蟾ｮ蛻､螳・======
static bool RayIntersectsAABB(DirectX::XMVECTOR rayOrig, DirectX::XMVECTOR rayDir, const DirectX::XMFLOAT3& bmin, const DirectX::XMFLOAT3& bmax, float& tOut) {
	using namespace DirectX;
	XMFLOAT3 orig;
	XMStoreFloat3(&orig, rayOrig);
	XMFLOAT3 dir;
	XMStoreFloat3(&dir, rayDir);
	float tmin = -FLT_MAX, tmax = FLT_MAX;
	float mn[3] = {bmin.x, bmin.y, bmin.z};
	float mx[3] = {bmax.x, bmax.y, bmax.z};
	float o[3] = {orig.x, orig.y, orig.z};
	float d[3] = {dir.x, dir.y, dir.z};
	for (int i = 0; i < 3; ++i) {
		if (std::fabs(d[i]) < 1e-8f) {
			if (o[i] < mn[i] || o[i] > mx[i])
				return false;
		} else {
			float t1 = (mn[i] - o[i]) / d[i];
			float t2 = (mx[i] - o[i]) / d[i];
			if (t1 > t2) {
				float tmp = t1;
				t1 = t2;
				t2 = tmp;
			}
			if (t1 > tmin)
				tmin = t1;
			if (t2 < tmax)
				tmax = t2;
			if (tmin > tmax)
				return false;
		}
	}
	if (tmax < 0)
		return false;
	tOut = tmin > 0 ? tmin : tmax;
	return true;
}

void EditorUI::ScreenToWorldRay(float screenX, float screenY, float imageW, float imageH, DirectX::XMMATRIX view, DirectX::XMMATRIX proj, DirectX::XMVECTOR& outOrig, DirectX::XMVECTOR& outDir) {
	using namespace DirectX;
	// NDC蠎ｧ讓吶↓螟画鋤 [-1, 1]
	float ndcX = (screenX / imageW) * 2.0f - 1.0f;
	float ndcY = 1.0f - (screenY / imageH) * 2.0f; // Y蜿崎ｻ｢

	XMMATRIX invProj = XMMatrixInverse(nullptr, proj);
	XMMATRIX invView = XMMatrixInverse(nullptr, view);

	// Near plane 縺ｨ Far plane 縺ｮ繝昴う繝ｳ繝・
	XMVECTOR nearPoint = XMVector3TransformCoord(XMVectorSet(ndcX, ndcY, 0.0f, 1.0f), invProj);
	XMVECTOR farPoint = XMVector3TransformCoord(XMVectorSet(ndcX, ndcY, 1.0f, 1.0f), invProj);

	// 繝薙Η繝ｼ遨ｺ髢凪・繝ｯ繝ｼ繝ｫ繝臥ｩｺ髢・
	nearPoint = XMVector3TransformCoord(nearPoint, invView);
	farPoint = XMVector3TransformCoord(farPoint, invView);

	outOrig = nearPoint;
	outDir = XMVector3Normalize(XMVectorSubtract(farPoint, nearPoint));
}

// 笘・繧ｮ繧ｺ繝｢霆ｸ縺ｮRay繝偵ャ繝亥愛螳夲ｼ医Ο繝ｼ繧ｫ繝ｫ遨ｺ髢薙↓螟画鋤縺励※蛻､螳夲ｼ・
static int HitTestGizmoAxis(DirectX::XMVECTOR rayOrig, DirectX::XMVECTOR rayDir, const Engine::Transform& objTransform, float axisLen, GizmoMode mode) {
	Engine::Matrix4x4 mat = objTransform.ToMatrix();
	DirectX::XMMATRIX worldMat = DirectX::XMLoadFloat4x4(reinterpret_cast<DirectX::XMFLOAT4X4*>(&mat));
	DirectX::XMVECTOR det;
	DirectX::XMMATRIX invWorld = DirectX::XMMatrixInverse(&det, worldMat);

	DirectX::XMVECTOR localOrig = DirectX::XMVector3TransformCoord(rayOrig, invWorld);
	DirectX::XMVECTOR localTarget = DirectX::XMVector3TransformCoord(DirectX::XMVectorAdd(rayOrig, rayDir), invWorld);
	DirectX::XMVECTOR localDir = DirectX::XMVectorSubtract(localTarget, localOrig);

	if (mode == GizmoMode::Rotate) {
		float radius = 1.5f;
		float thickness = 0.2f;
		int bestAxis = -1;
		float bestDistSq = FLT_MAX;

		using namespace DirectX;
		XMFLOAT3 orig;
		XMStoreFloat3(&orig, localOrig);
		XMFLOAT3 dir;
		XMStoreFloat3(&dir, localDir);

		for (int a = 0; a < 3; ++a) {
			float N[3] = {0, 0, 0};
			N[a] = 1.0f; // a=0: YZ蟷ｳ髱｢, a=1: ZX蟷ｳ髱｢, a=2: XY蟷ｳ髱｢
			float denom = dir.x * N[0] + dir.y * N[1] + dir.z * N[2];
			if (std::fabs(denom) > 1e-5f) {
				float t = (-orig.x * N[0] - orig.y * N[1] - orig.z * N[2]) / denom;
				if (t > 0) {
					float px = orig.x + t * dir.x;
					float py = orig.y + t * dir.y;
					float pz = orig.z + t * dir.z;
					float dist = std::sqrt(px * px + py * py + pz * pz);
					if (std::fabs(dist - radius) < thickness) {
						if (t < bestDistSq) {
							bestDistSq = t;
							bestAxis = a;
						}
					}
				}
			}
		}
		return bestAxis;
	} else if (mode == GizmoMode::Scale) {
		float boxSize = 0.2f;
		float bestT = FLT_MAX;
		int bestAxis = -1;
		for (int a = 0; a < 3; ++a) {
			DirectX::XMFLOAT3 bmin, bmax;
			if (a == 0) {
				bmin = {axisLen, -boxSize, -boxSize};
				bmax = {axisLen + boxSize * 2, boxSize, boxSize};
			} else if (a == 1) {
				bmin = {-boxSize, axisLen, -boxSize};
				bmax = {boxSize, axisLen + boxSize * 2, boxSize};
			} else {
				bmin = {-boxSize, -boxSize, axisLen};
				bmax = {boxSize, boxSize, axisLen + boxSize * 2};
			}
			float t;
			if (RayIntersectsAABB(localOrig, localDir, bmin, bmax, t)) {
				if (t < bestT) {
					bestT = t;
					bestAxis = a;
				}
			}
		}
		return bestAxis;
	} else { // Translate
		float thickness = 0.2f;
		DirectX::XMFLOAT3 axes[3][2] = {
		    {{0, -thickness, -thickness}, {axisLen, thickness, thickness}},
		    {{-thickness, 0, -thickness}, {thickness, axisLen, thickness}},
		    {{-thickness, -thickness, 0}, {thickness, thickness, axisLen}}};

		float bestT = FLT_MAX;
		int bestAxis = -1;
		for (int a = 0; a < 3; ++a) {
			float t;
			if (RayIntersectsAABB(localOrig, localDir, axes[a][0], axes[a][1], t)) {
				if (t < bestT) {
					bestT = t;
					bestAxis = a;
				}
			}
		}
		return bestAxis;
	}
}

// ====== Main Show ======
void EditorUI::Show(Engine::Renderer* renderer, GameScene* gameScene) {
	globalTime += ImGui::GetIO().DeltaTime;
	ImGuiIO& io = ImGui::GetIO();

	ImGuiWindowFlags wf = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;
	const ImGuiViewport* vp = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(vp->Pos);
	ImGui::SetNextWindowSize(vp->Size);
	ImGui::SetNextWindowViewport(vp->ID);
	wf |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
	wf |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
	ImGui::Begin("DockSpace Demo", nullptr, wf);
	ImGui::PopStyleVar(3);
	if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable)
		ImGui::DockSpace(ImGui::GetID("MyDockSpace"), ImVec2(0, 0), ImGuiDockNodeFlags_None);

	static int aspectMode = 0;
	const char* aspectNames[] = {"Free Aspect", "16:9", "4:3"};

	// 笘・ｿｽ蜉: 繧｢繝九Γ繝ｼ繧ｷ繝ｧ繝ｳ繧ｦ繧｣繝ｳ繝峨え縺ｮ蜻ｼ縺ｳ蜃ｺ縺・
	ShowAnimationWindow(renderer, gameScene);

	// 笘・ｿｽ蜉: Play Mode Monitor
	ShowPlayModeMonitor(gameScene);
	ShowPlayModeMonitor(gameScene); // 笘・ｿｽ蜉: Play Mode Monitor

	// ====== Menu Bar ======
	if (ImGui::BeginMenuBar()) {
		if (ImGui::BeginMenu("File")) {
			// 笘・ｿｽ蜉: 迴ｾ蝨ｨ縺ｮ繧ｷ繝ｼ繝ｳ蜷阪ｒ蜈･蜉・陦ｨ遉ｺ縺吶ｋ繝舌ャ繝輔ぃ
			static char currentSceneName[128] = "scene.json";
			ImGui::InputText("Current Scene", currentSceneName, sizeof(currentSceneName));

			if (ImGui::MenuItem("Save Scene", "Ctrl+S")) {
				SaveScene(gameScene, std::string("Resources/") + currentSceneName);
			}

			if (ImGui::BeginMenu("Load Scene...")) {
				// Resources繝輔か繝ｫ繝蜀・・json繝輔ぃ繧､繝ｫ繧貞・謖・
				try {
					for (const auto& entry : std::filesystem::directory_iterator("Resources")) {
						if (entry.path().extension() == ".json") {
							std::string filename = entry.path().filename().string();
							if (ImGui::MenuItem(filename.c_str())) {
								strcpy_s(currentSceneName, filename.c_str());
								LoadScene(gameScene, entry.path().string());
							}
						}
					}
				} catch (...) {
				}
				ImGui::EndMenu();
			}

			if (ImGui::BeginMenu("Add Scene... (Additive)")) {
				try {
					for (const auto& entry : std::filesystem::directory_iterator("Resources")) {
						if (entry.path().extension() == ".json") {
							std::string filename = entry.path().filename().string();
							if (ImGui::MenuItem(filename.c_str())) {
								AddScene(gameScene, entry.path().string());
							}
						}
					}
				} catch (...) {
				}
				ImGui::EndMenu();
			}
			ImGui::Separator();
			if (ImGui::MenuItem("Undo", "Ctrl+Z"))
				Undo();
			if (ImGui::MenuItem("Redo", "Ctrl+Y"))
				Redo();
			ImGui::Separator();
			if (ImGui::MenuItem("Exit")) {
			}
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Edit")) {
			if (ImGui::MenuItem("Copy", "Ctrl+C")) {
				if (gameScene) {
					clipboardObjects.clear();
					for (int i : gameScene->selectedIndices_)
						if (i < (int)gameScene->objects_.size())
							clipboardObjects.push_back(gameScene->objects_[i]);
					Log("Copied " + std::to_string(clipboardObjects.size()) + " object(s)");
				}
			}
			if (ImGui::MenuItem("Paste", "Ctrl+V")) {
				if (gameScene && !clipboardObjects.empty()) {
					for (auto obj : clipboardObjects) {
						obj.name = GenerateCopyName(obj.name, gameScene->objects_);
						obj.locked = false;
						obj.translate.x += 1.0f;
						gameScene->objects_.push_back(obj);
					}
					Log("Pasted " + std::to_string(clipboardObjects.size()) + " object(s)");
				}
			}
			if (ImGui::MenuItem("Duplicate", "Ctrl+D")) {
				if (gameScene) {
					std::vector<SceneObject> dups;
					for (int i : gameScene->selectedIndices_)
						if (i < (int)gameScene->objects_.size()) {
							auto o = gameScene->objects_[i];
							o.name = GenerateCopyName(o.name, gameScene->objects_);
							o.locked = false;
							o.translate.x += 1.0f;
							dups.push_back(o);
						}
					for (auto& d : dups)
						gameScene->objects_.push_back(d);
					Log("Duplicated " + std::to_string(dups.size()) + " object(s)");
				}
			}
			if (ImGui::MenuItem("Delete", "Del")) {
				if (gameScene && !gameScene->selectedIndices_.empty()) {
					std::vector<int> sortedIndices(gameScene->selectedIndices_.begin(), gameScene->selectedIndices_.end());
					std::sort(sortedIndices.rbegin(), sortedIndices.rend());
					
					std::vector<SceneObject> deletedObjects;
					std::vector<int> deletedIndices;
					
					for (int i : sortedIndices) {
						if (i < (int)gameScene->objects_.size() && !gameScene->objects_[i].locked) {
							deletedObjects.push_back(gameScene->objects_[i]);
							deletedIndices.push_back(i);
							gameScene->objects_.erase(gameScene->objects_.begin() + i);
						}
					}
					gameScene->selectedIndices_.clear();
					gameScene->selectedObjectIndex_ = -1;
					
					if (!deletedObjects.empty()) {
						PushUndo({
							"Delete Selection",
							[gameScene, deletedObjects, deletedIndices]() {
								// Restore objects in ascending index order to maintain correct positions
								for (int idx = (int)deletedObjects.size() - 1; idx >= 0; --idx) {
									int insertIdx = deletedIndices[idx];
									gameScene->objects_.insert(gameScene->objects_.begin() + insertIdx, deletedObjects[idx]);
								}
							},
							[gameScene, deletedIndices]() {
								// Re-delete objects in descending index order
								for (int i : deletedIndices) {
									if (i < (int)gameScene->objects_.size()) {
										gameScene->objects_.erase(gameScene->objects_.begin() + i);
									}
								}
								gameScene->selectedIndices_.clear();
								gameScene->selectedObjectIndex_ = -1;
							}
						});
					}
				}
			}
			if (ImGui::MenuItem("Select All", "Ctrl+A")) {
				if (gameScene) {
					for (int i = 0; i < (int)gameScene->objects_.size(); ++i)
						gameScene->selectedIndices_.insert(i);
					if (!gameScene->objects_.empty())
						gameScene->selectedObjectIndex_ = 0;
				}
			}
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Scene")) {
			if (ImGui::MenuItem("Title"))
				Engine::SceneManager::GetInstance()->Change("Title");
			if (ImGui::MenuItem("Game"))
				Engine::SceneManager::GetInstance()->Change("Game");
			ImGui::EndMenu();
		}
		ImGui::Spacing();
		ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
		ImGui::Spacing();
		ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2);
		ImGui::Text("Aspect:");
		ImGui::SameLine();
		ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 2);
		ImGui::PushItemWidth(110);
		ImGui::Combo("##Asp", &aspectMode, aspectNames, IM_ARRAYSIZE(aspectNames));
		ImGui::PopItemWidth();

		ImGui::Spacing();
		ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
		ImGui::Spacing();
		ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2);
		auto gBtn = [](const char* l, GizmoMode m) {
			bool a = (currentGizmoMode == m);
			if (a)
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(.3f, .5f, .9f, 1));
			if (ImGui::SmallButton(l))
				currentGizmoMode = m;
			if (a)
				ImGui::PopStyleColor();
		};
		gBtn("T##M", GizmoMode::Translate);
		ImGui::SameLine();
		gBtn("R##R", GizmoMode::Rotate);
		ImGui::SameLine();
		gBtn("S##S", GizmoMode::Scale);

		ImGui::Spacing();
		ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
		ImGui::Spacing();
		
		s_pipeEditor.DrawUI();

		ImGui::Spacing();
		ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
		ImGui::Spacing();
		if (gameScene) {
			if (gameScene->isPlaying_) {
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(.8f, .2f, .2f, 1));
				if (ImGui::SmallButton("Stop")) {
					gameScene->isPlaying_ = false;
					LoadScene(gameScene, "Resources/.temp_play.json");
					auto* audio = Engine::Audio::GetInstance();
					if (audio)
						audio->StopAll();
					Log("Play mode stopped. Scene restored.");
				}
				ImGui::PopStyleColor();
			} else {
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(.2f, .7f, .3f, 1));
				if (ImGui::SmallButton("Play")) {
					SaveScene(gameScene, "Resources/.temp_play.json");
					gameScene->isPlaying_ = true;
					Log("Play mode started.");
				}
				ImGui::PopStyleColor();
			}
		}
		ImGui::EndMenuBar();
	}

	// ====== Shortcuts ======
	if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z, false))
		Undo();
	if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y, false))
		Redo();
	if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false))
		SaveScene(gameScene, "Resources/scene.json");
	if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C, false) && gameScene) {
		clipboardObjects.clear();
		for (int i : gameScene->selectedIndices_)
			if (i < (int)gameScene->objects_.size())
				clipboardObjects.push_back(gameScene->objects_[i]);
		if (!clipboardObjects.empty())
			Log("Copied " + std::to_string(clipboardObjects.size()));
	}
	if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V, false) && gameScene && !clipboardObjects.empty()) {
		for (auto obj : clipboardObjects) {
			obj.name = GenerateCopyName(obj.name, gameScene->objects_);
			obj.locked = false;
			obj.translate.x += 1;
			gameScene->objects_.push_back(obj);
		}
		Log("Pasted " + std::to_string(clipboardObjects.size()));
	}
	if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D, false) && gameScene) {
		std::vector<SceneObject> dups;
		for (int i : gameScene->selectedIndices_) {
			if (i < (int)gameScene->objects_.size()) {
				auto o = gameScene->objects_[i];
				o.name = GenerateCopyName(o.name, gameScene->objects_);
				o.locked = false;
				o.translate.x += 1;
				dups.push_back(o);
			}
		}
		for (auto& d : dups) {
			gameScene->objects_.push_back(d);
		}
	}
	if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A, false) && gameScene) {
		for (int i = 0; i < (int)gameScene->objects_.size(); ++i) {
			gameScene->selectedIndices_.insert(i);
		}
	}
	if (!io.WantTextInput && !ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
		if (ImGui::IsKeyPressed(ImGuiKey_T, false))
			currentGizmoMode = GizmoMode::Translate;
		if (ImGui::IsKeyPressed(ImGuiKey_R, false))
			currentGizmoMode = GizmoMode::Rotate;
		if (ImGui::IsKeyPressed(ImGuiKey_S, false) && !io.KeyCtrl)
			currentGizmoMode = GizmoMode::Scale;
	}

	ShowHierarchy(gameScene);
	ShowInspector(gameScene);
	ShowProject(renderer, gameScene);
	ShowSceneSettings(renderer);
	ShowConsole();

	// ======== Game 繧ｦ繧｣繝ｳ繝峨え ========
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
	ImGui::Begin("Game");
	ImVec2 cp = ImGui::GetCursorPos(), av = ImGui::GetContentRegionAvail();
	float tW = av.x, tH = av.y;
	if (aspectMode == 1) {
		float r = 16.f / 9.f;
		if (tW / tH > r)
			tW = tH * r;
		else
			tH = tW / r;
	} else if (aspectMode == 2) {
		float r = 4.f / 3.f;
		if (tW / tH > r)
			tW = tH * r;
		else
			tH = tW / r;
	}
	float offX = (av.x - tW) * .5f, offY = (av.y - tH) * .5f;
	ImGui::SetCursorPos(ImVec2(cp.x + offX, cp.y + offY));

	// 笘・逕ｻ蜒上・邨ｶ蟇ｾ繧ｹ繧ｯ繝ｪ繝ｼ繝ｳ蠎ｧ讓吶ｒ險倬鹸 (繝斐ャ繧ｭ繝ｳ繧ｰ逕ｨ)
	ImVec2 windowPos = ImGui::GetWindowPos();
	ImVec2 curScreen = ImGui::GetCursorScreenPos();
	
	// ---- パイプ設置エディタ ----
	s_pipeEditor.UpdateAndDraw(gameScene, renderer, gameImageMin, gameImageMax, tW, tH);

	ImGui::Image((ImTextureID)renderer->GetGameFinalSRV().ptr, ImVec2(tW, tH));
	// 笘・ｿｽ蜉: 繝励Ξ繝上ヶ繧・Δ繝・Ν縺ｮ繝峨Λ繝・げ・・ラ繝ｭ繝・・蜿励￠蜈･繧悟・
	if (ImGui::BeginDragDropTarget()) {
		if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("RESOURCE_PATH")) {
			std::string path((const char*)pl->Data, pl->DataSize - 1);
			if (path.find(".prefab") != std::string::npos) {
				LoadPrefab(gameScene, path);
			} else if (path.find(".obj") != std::string::npos || path.find(".gltf") != std::string::npos || path.find(".fbx") != std::string::npos) {
				SceneObject o;
				o.name = "Model";
				o.modelPath = path;
				o.modelHandle = renderer->LoadObjMesh(path);
				MeshRendererComponent mr;
				mr.modelHandle = o.modelHandle;
				mr.modelPath = o.modelPath;
				o.meshRenderers.push_back(mr);
				gameScene->objects_.push_back(o);
			}
		}
		ImGui::EndDragDropTarget();
	}
	gameImageMin = curScreen;
	gameImageMax = ImVec2(curScreen.x + tW, curScreen.y + tH);

	bool gameHovered = ImGui::IsWindowHovered();

	// ====== 笘・繝薙Η繝ｼ繝昴・繝医け繝ｪ繝・け驕ｸ謚・+ 繧ｮ繧ｺ繝｢繝峨Λ繝・げ ======
	if (gameScene && gameHovered && tW > 0 && tH > 0 && !gameScene->IsPlaying()) {
		ImVec2 mousePos = ImGui::GetMousePos();
		float localX = mousePos.x - gameImageMin.x;
		float localY = mousePos.y - gameImageMin.y;
		bool insideImage = (localX >= 0 && localY >= 0 && localX <= tW && localY <= tH);

		auto viewMat = gameScene->camera_.View();
		auto projMat = gameScene->camera_.Proj();

		if (insideImage) {
			// --- 笘・蟾ｦ繧ｯ繝ｪ繝・け 竊・繧ｮ繧ｺ繝｢霆ｸ 竊・繧ｪ繝悶ず繧ｧ繧ｯ繝磯∈謚・竊・閾ｪ逕ｱ繝峨Λ繝・げ髢句ｧ・---
			if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
				DirectX::XMVECTOR rayOrig, rayDir;
				ScreenToWorldRay(localX, localY, tW, tH, viewMat, projMat, rayOrig, rayDir);

				// パイプモード中は通常の選択・ギズモ操作を行わない
				if (s_pipeEditor.IsPipeMode()) {
					goto EndClickProcessing;
				}
			
			{
				// 1. 繧ｮ繧ｺ繝｢霆ｸ繝偵ャ繝医ユ繧ｹ繝・
				bool hitGizmo = false;
				if (gameScene->selectedObjectIndex_ >= 0 && gameScene->selectedObjectIndex_ < (int)gameScene->objects_.size() && !gameScene->objects_[gameScene->selectedObjectIndex_].locked) {
					auto& selObj = gameScene->objects_[gameScene->selectedObjectIndex_];
					int axis = HitTestGizmoAxis(rayOrig, rayDir, selObj.GetTransform(), 2.0f, currentGizmoMode);
					if (axis >= 0) {
						gizmoDragging = true;
						gizmoDragAxis = axis;
						gizmoDragStartMouse = mousePos;
						dragStartTransforms.clear();
						for (int idx : gameScene->selectedIndices_) {
							if (idx >= 0 && idx < (int)gameScene->objects_.size()) {
								dragStartTransforms[idx] = gameScene->objects_[idx].GetTransform();
							}
						}
						hitGizmo = true;
					}
				}

				// 2. 繧ｪ繝悶ず繧ｧ繧ｯ繝磯∈謚・+ 閾ｪ逕ｱ繝峨Λ繝・げ髢句ｧ・
				if (!hitGizmo) {
					float bestT = FLT_MAX;
					int bestIdx = -1;
					for (int i = 0; i < (int)gameScene->objects_.size(); ++i) {
						const auto& obj = gameScene->objects_[i];
						if (obj.locked)
							continue; // 笘・繝ｭ繝・け貂医∩繧ｪ繝悶ず繧ｧ繧ｯ繝医・驕ｸ謚樔ｸ榊庄

						// 笘・OBB蛻､螳・ Ray繧偵が繝悶ず繧ｧ繧ｯ繝医・繝ｭ繝ｼ繧ｫ繝ｫ遨ｺ髢薙↓螟画鋤
						Engine::Matrix4x4 mat = obj.GetTransform().ToMatrix();
						DirectX::XMMATRIX worldMat = DirectX::XMLoadFloat4x4(reinterpret_cast<DirectX::XMFLOAT4X4*>(&mat));
						DirectX::XMVECTOR det;
						DirectX::XMMATRIX invWorld = DirectX::XMMatrixInverse(&det, worldMat);

						DirectX::XMVECTOR localOrig = DirectX::XMVector3TransformCoord(rayOrig, invWorld);
						DirectX::XMVECTOR localTarget = DirectX::XMVector3TransformCoord(DirectX::XMVectorAdd(rayOrig, rayDir), invWorld);
						DirectX::XMVECTOR localDir = DirectX::XMVectorSubtract(localTarget, localOrig);

						// 譛蟆上し繧､繧ｺ菫晁ｨｼ
						float hx = 1.0f;
						if (std::fabs(obj.scale.x) < 0.6f && std::fabs(obj.scale.x) > 0.001f)
							hx = 0.3f / std::fabs(obj.scale.x);
						float hy = 1.0f;
						if (std::fabs(obj.scale.y) < 0.6f && std::fabs(obj.scale.y) > 0.001f)
							hy = 0.3f / std::fabs(obj.scale.y);
						float hz = 1.0f;
						if (std::fabs(obj.scale.z) < 0.6f && std::fabs(obj.scale.z) > 0.001f)
							hz = 0.3f / std::fabs(obj.scale.z);
						DirectX::XMFLOAT3 bmin = {-hx, -hy, -hz};
						DirectX::XMFLOAT3 bmax = {hx, hy, hz};

						float tLocal;
						if (RayIntersectsAABB(localOrig, localDir, bmin, bmax, tLocal)) {
							// tLocal 縺ｯ worldDir (豁｣隕丞喧貂・ 縺ｮ髟ｷ縺・1)縺ｫ蟇ｾ縺吶ｋ菫よ焚縺ｨ荳閾ｴ
							if (tLocal < bestT) {
								bestT = tLocal;
								bestIdx = i;
							}
						}
					}
					if (bestIdx >= 0) {
						if (io.KeyCtrl) {
							// Ctrl+繧ｯ繝ｪ繝・け: 繝医げ繝ｫ霑ｽ蜉
							if (gameScene->selectedIndices_.count(bestIdx))
								gameScene->selectedIndices_.erase(bestIdx);
							else
								gameScene->selectedIndices_.insert(bestIdx);
						} else if (io.KeyShift) {
							// Shift+繧ｯ繝ｪ繝・け: 霑ｽ蜉驕ｸ謚・
							gameScene->selectedIndices_.insert(bestIdx);
						} else {
							// 騾壼ｸｸ繧ｯ繝ｪ繝・け: 蜊倅ｸ驕ｸ謚・
							gameScene->selectedIndices_ = {bestIdx};
						}
						gameScene->selectedObjectIndex_ = bestIdx;

						// 笘・閾ｪ逕ｱ繝峨Λ繝・げ髢句ｧ・
						objectDragging = true;
						gizmoDragStartMouse = mousePos;
						dragStartTransforms.clear();
						for (int idx : gameScene->selectedIndices_) {
							if (idx >= 0 && idx < (int)gameScene->objects_.size()) {
								dragStartTransforms[idx] = gameScene->objects_[idx].GetTransform();
							}
						}
					} else if (!io.KeyCtrl && !io.KeyShift) {
						gameScene->selectedIndices_.clear();
						gameScene->selectedObjectIndex_ = -1;
					}
				}
			} // 1. 繧ｮ繧ｺ繝｢霆ｸ繝偵ャ繝医ユ繧ｹ繝医せ繧ｳ繝ｼ繝礼ｵゆｺ・

			EndClickProcessing:;
			} // if (ImGui::IsMouseClicked(Left)) の終了

			// --- ★ 右クリック (Ctrl押下時) -> オブジェクト即時削除 ---
			if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) && io.KeyCtrl) {
				DirectX::XMVECTOR rayOrig, rayDir;
				ScreenToWorldRay(localX, localY, tW, tH, viewMat, projMat, rayOrig, rayDir);
				
				float bestT = FLT_MAX;
				int bestIdx = -1;
				// 交差判定ロジック（左クリックと同じ）
				for (int i = 0; i < (int)gameScene->objects_.size(); ++i) {
					const auto& obj = gameScene->objects_[i];
					if (obj.locked) continue;

					Engine::Matrix4x4 mat = obj.GetTransform().ToMatrix();
					DirectX::XMMATRIX worldMat = DirectX::XMLoadFloat4x4(reinterpret_cast<DirectX::XMFLOAT4X4*>(&mat));
					DirectX::XMVECTOR det;
					DirectX::XMMATRIX invWorld = DirectX::XMMatrixInverse(&det, worldMat);

					DirectX::XMVECTOR localOrig = DirectX::XMVector3TransformCoord(rayOrig, invWorld);
					DirectX::XMVECTOR localTarget = DirectX::XMVector3TransformCoord(DirectX::XMVectorAdd(rayOrig, rayDir), invWorld);
					DirectX::XMVECTOR localDir = DirectX::XMVectorSubtract(localTarget, localOrig);

					float hx = 1.0f; if (std::fabs(obj.scale.x) < 0.6f && std::fabs(obj.scale.x) > 0.001f) hx = 0.3f / std::fabs(obj.scale.x);
					float hy = 1.0f; if (std::fabs(obj.scale.y) < 0.6f && std::fabs(obj.scale.y) > 0.001f) hy = 0.3f / std::fabs(obj.scale.y);
					float hz = 1.0f; if (std::fabs(obj.scale.z) < 0.6f && std::fabs(obj.scale.z) > 0.001f) hz = 0.3f / std::fabs(obj.scale.z);
					DirectX::XMFLOAT3 bmin = {-hx, -hy, -hz};
					DirectX::XMFLOAT3 bmax = {hx, hy, hz};

					float tLocal;
					if (RayIntersectsAABB(localOrig, localDir, bmin, bmax, tLocal)) {
						if (tLocal < bestT) {
							bestT = tLocal;
							bestIdx = i;
						}
					}
				}

				if (bestIdx >= 0) {
					SceneObject deletedObj = gameScene->objects_[bestIdx];
					gameScene->objects_.erase(gameScene->objects_.begin() + bestIdx);
					gameScene->selectedIndices_.erase(bestIdx);
					// 削除によりインデックスがずれるため、選択状態をリセット
					gameScene->selectedIndices_.clear();
					gameScene->selectedObjectIndex_ = -1;

					// Undoコマンドの登録
					PushUndo({
						"Delete Object (Ctrl+RightClick)",
						[gameScene, bestIdx, deletedObj]() {
							// 元に戻す: 指定インデックスにオブジェクトを挿入
							gameScene->objects_.insert(gameScene->objects_.begin() + bestIdx, deletedObj);
						},
						[gameScene, bestIdx]() {
							// やり直す: 指定インデックスのオブジェクトを削除
							if (bestIdx >= 0 && bestIdx < (int)gameScene->objects_.size()) {
								gameScene->objects_.erase(gameScene->objects_.begin() + bestIdx);
								gameScene->selectedIndices_.clear();
								gameScene->selectedObjectIndex_ = -1;
							}
						}
					});
					Log("Deleted object: " + deletedObj.name);
				}
			} // if (ImGui::IsMouseClicked(Right)) の終了

			// --- 笘・繧ｮ繧ｺ繝｢霆ｸ繝峨Λ繝・げ荳ｭ ---
			if (gizmoDragging && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
				ImVec2 delta = ImVec2(mousePos.x - gizmoDragStartMouse.x, mousePos.y - gizmoDragStartMouse.y);
				for (int idx : gameScene->selectedIndices_) {
					if (idx >= 0 && idx < (int)gameScene->objects_.size() && dragStartTransforms.count(idx)) {
						auto& obj = gameScene->objects_[idx];
						auto initT = dragStartTransforms[idx];
						if (currentGizmoMode == GizmoMode::Translate) {
							float s = 0.02f;
							float dx = (gizmoDragAxis == 0) ? delta.x * s : 0;
							float dy = (gizmoDragAxis == 1) ? -delta.y * s : 0;
							float dz = (gizmoDragAxis == 2) ? delta.x * s : 0;
							// 繝ｭ繝ｼ繧ｫ繝ｫ霆ｸ縺ｫ豐ｿ縺｣縺ｦ遘ｻ蜍輔☆繧・
							auto rotMat = DirectX::XMMatrixRotationRollPitchYaw(initT.rotate.x, initT.rotate.y, initT.rotate.z);
							DirectX::XMVECTOR moveV = DirectX::XMVector3TransformNormal(DirectX::XMVectorSet(dx, dy, dz, 0), rotMat);
							DirectX::XMFLOAT3 moveF;
							DirectX::XMStoreFloat3(&moveF, moveV);
							obj.translate = DirectX::XMFLOAT3(initT.translate.x + moveF.x, initT.translate.y + moveF.y, initT.translate.z + moveF.z);
						} else if (currentGizmoMode == GizmoMode::Rotate) {
							float s = 0.01f;
							auto nr = initT.rotate;
							if (gizmoDragAxis == 0)
								nr.x += delta.y * s;
							else if (gizmoDragAxis == 1)
								nr.y += delta.x * s;
							else
								nr.z += delta.x * s;
							obj.rotate = DirectX::XMFLOAT3(nr.x, nr.y, nr.z);
						} else {
							float s = 0.01f;
							auto ns = initT.scale;
							if (gizmoDragAxis == 0)
								ns.x += delta.x * s;
							else if (gizmoDragAxis == 1)
								ns.y -= delta.y * s;
							else
								ns.z += delta.x * s;
							if (ns.x < 0.01f)
								ns.x = 0.01f;
							if (ns.y < 0.01f)
								ns.y = 0.01f;
							if (ns.z < 0.01f)
								ns.z = 0.01f;
							obj.scale = DirectX::XMFLOAT3(ns.x, ns.y, ns.z);
						}
					}
				}
			}

			// --- 笘・閾ｪ逕ｱ繝峨Λ繝・げ荳ｭ・医ぐ繧ｺ繝｢縺ｧ縺ｯ縺ｪ縺上が繝悶ず繧ｧ繧ｯ繝育峩謗･繝峨Λ繝・げ・・--
			if (objectDragging && !gizmoDragging && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
				ImVec2 delta = ImVec2(mousePos.x - gizmoDragStartMouse.x, mousePos.y - gizmoDragStartMouse.y);
				if (std::fabs(delta.x) > 2.0f || std::fabs(delta.y) > 2.0f) { // 繝・ャ繝峨だ繝ｼ繝ｳ
					auto camR2 = gameScene->camera_.Rotation();
					auto rotMat = DirectX::XMMatrixRotationRollPitchYaw(camR2.x, camR2.y, camR2.z);
					DirectX::XMFLOAT3 right = {1, 0, 0}, up = {0, 1, 0};
					DirectX::XMVECTOR rightV = DirectX::XMVector3TransformCoord(DirectX::XMLoadFloat3(&right), rotMat);
					DirectX::XMVECTOR upV = DirectX::XMVector3TransformCoord(DirectX::XMLoadFloat3(&up), rotMat);

					float sensitivity = 0.015f;
					DirectX::XMVECTOR moveV = DirectX::XMVectorAdd(DirectX::XMVectorScale(rightV, delta.x * sensitivity), DirectX::XMVectorScale(upV, -delta.y * sensitivity));
					DirectX::XMFLOAT3 moveF;
					DirectX::XMStoreFloat3(&moveF, moveV);

					for (int idx : gameScene->selectedIndices_) {
						if (idx >= 0 && idx < (int)gameScene->objects_.size() && dragStartTransforms.count(idx)) {
							auto initT = dragStartTransforms[idx];
							gameScene->objects_[idx].translate = DirectX::XMFLOAT3(initT.translate.x + moveF.x, initT.translate.y + moveF.y, initT.translate.z + moveF.z);
						}
					}
				}
			}

			// --- 笘・繝峨Λ繝・げ邨ゆｺ・(Undo逋ｻ骭ｲ) ---
			if ((gizmoDragging || objectDragging) && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
				std::vector<int> targetIndices;
				std::vector<Engine::Transform> oldTransforms;
				std::vector<Engine::Transform> newTransforms;

				for (int idx : gameScene->selectedIndices_) {
					if (idx >= 0 && idx < (int)gameScene->objects_.size() && dragStartTransforms.count(idx)) {
						targetIndices.push_back(idx);
						oldTransforms.push_back(dragStartTransforms[idx]);
						newTransforms.push_back(gameScene->objects_[idx].GetTransform());
					}
				}
				if (!targetIndices.empty()) {
					PushUndo(
					    {"Transform",
					     [gameScene, targetIndices, oldTransforms]() {
						     for (size_t i = 0; i < targetIndices.size(); ++i) {
							     int idx = targetIndices[i];
							     if (idx < (int)gameScene->objects_.size()) {
								     gameScene->objects_[idx].translate = DirectX::XMFLOAT3(oldTransforms[i].translate.x, oldTransforms[i].translate.y, oldTransforms[i].translate.z);
								     gameScene->objects_[idx].rotate = DirectX::XMFLOAT3(oldTransforms[i].rotate.x, oldTransforms[i].rotate.y, oldTransforms[i].rotate.z);
								     gameScene->objects_[idx].scale = DirectX::XMFLOAT3(oldTransforms[i].scale.x, oldTransforms[i].scale.y, oldTransforms[i].scale.z);
							     }
						     }
					     },
					     [gameScene, targetIndices, newTransforms]() {
						     for (size_t i = 0; i < targetIndices.size(); ++i) {
							     int idx = targetIndices[i];
							     if (idx < (int)gameScene->objects_.size()) {
								     gameScene->objects_[idx].translate = DirectX::XMFLOAT3(newTransforms[i].translate.x, newTransforms[i].translate.y, newTransforms[i].translate.z);
								     gameScene->objects_[idx].rotate = DirectX::XMFLOAT3(newTransforms[i].rotate.x, newTransforms[i].rotate.y, newTransforms[i].rotate.z);
								     gameScene->objects_[idx].scale = DirectX::XMFLOAT3(newTransforms[i].scale.x, newTransforms[i].scale.y, newTransforms[i].scale.z);
							     }
						     }
					     }});
				}
				gizmoDragging = false;
				gizmoDragAxis = -1;
				objectDragging = false;
				dragStartTransforms.clear();
			}

		// --- 繧ｫ繝｡繝ｩ謫堺ｽ懶ｼ亥承繧ｯ繝ｪ繝・け・・---
		auto camP = gameScene->camera_.Position();
		auto camR = gameScene->camera_.Rotation();
		if (ImGui::IsMouseDragging(ImGuiMouseButton_Right, 1.0f)) {
			ImVec2 d = ImGui::GetMouseDragDelta(ImGuiMouseButton_Right, 1.0f);
			ImGui::ResetMouseDragDelta(ImGuiMouseButton_Right);
			camR.y += d.x * 0.003f;
			camR.x += d.y * 0.003f;
			constexpr float lim = DirectX::XMConvertToRadians(89.0f);
			if (camR.x > lim)
				camR.x = lim;
			if (camR.x < -lim)
				camR.x = -lim;
			gameScene->camera_.SetRotation(camR);
		}
		if (ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
			float sp = io.KeyShift ? 0.45f : 0.15f;
			DirectX::XMFLOAT3 mv = {0, 0, 0};
			if (ImGui::IsKeyDown(ImGuiKey_W))
				mv.z += sp;
			if (ImGui::IsKeyDown(ImGuiKey_S))
				mv.z -= sp;
			if (ImGui::IsKeyDown(ImGuiKey_A))
				mv.x -= sp;
			if (ImGui::IsKeyDown(ImGuiKey_D))
				mv.x += sp;
			if (ImGui::IsKeyDown(ImGuiKey_Q))
				mv.y -= sp;
			if (ImGui::IsKeyDown(ImGuiKey_E))
				mv.y += sp;
			auto r = DirectX::XMMatrixRotationRollPitchYaw(camR.x, camR.y, camR.z);
			DirectX::XMStoreFloat3(&camP, DirectX::XMVectorAdd(DirectX::XMLoadFloat3(&camP), DirectX::XMVector3TransformCoord(DirectX::XMLoadFloat3(&mv), r)));
			gameScene->camera_.SetPosition(camP);
		}
		float wh = io.MouseWheel;
		if (std::fabs(wh) > 0.01f) {
			float zs = io.KeyShift ? 3.f : 1.f;
			auto r = DirectX::XMMatrixRotationRollPitchYaw(camR.x, camR.y, camR.z);
			DirectX::XMFLOAT3 fw = {0, 0, 1};
			DirectX::XMStoreFloat3(&camP, DirectX::XMVectorAdd(DirectX::XMLoadFloat3(&camP), DirectX::XMVectorScale(DirectX::XMVector3TransformCoord(DirectX::XMLoadFloat3(&fw), r), wh * zs)));
				gameScene->camera_.SetPosition(camP);
			}
		}

		// 繝峨Λ繝・げ縺後え繧｣繝ｳ繝峨え螟悶↓陦後▲縺溷ｴ蜷医・繝ｪ繧ｻ繝・ヨ
		if ((gizmoDragging || objectDragging) && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
			gizmoDragging = false;
			gizmoDragAxis = -1;
			objectDragging = false;
		}

		if (gameScene && tH > 0.0f)
			gameScene->camera_.SetProjection(DirectX::XMConvertToRadians(45.0f), tW / tH, 0.1f, 1000.0f);
	}
	ImGui::End();
	ImGui::PopStyleVar();

	// 笘・DrawSelectionGizmo蜑企勁: GameScene::Draw()蜀・〒謠冗判縺吶ｋ繧医≧縺ｫ遘ｻ蜍墓ｸ医∩
	ImGui::End(); // DockSpace
}

// ====== Hierarchy ======
void EditorUI::ShowHierarchy(GameScene* scene) {
	ImGui::Begin("Hierarchy");
	ImGuiIO& io = ImGui::GetIO();
	if (scene) {
		if (ImGui::BeginPopupContextWindow("HierarchyCtx")) {
			auto addObj = [&](const char* label, const std::string& mp, const std::string& tp) {
				if (ImGui::MenuItem(label)) {
					auto* r = Engine::Renderer::GetInstance();
					SceneObject obj;
					obj.name = label;
					if (!mp.empty()) {
						obj.modelHandle = r->LoadObjMesh(mp);
						obj.modelPath = mp;
					}
					if (!tp.empty()) {
						obj.textureHandle = r->LoadTexture2D(tp);
						obj.texturePath = tp;
					} else {
						obj.textureHandle = r->LoadTexture2D("Resources/white1x1.png");
						obj.texturePath = "Resources/white1x1.png";
					}
					obj.id = GenerateId();
					scene->objects_.push_back(obj);
					int idx = (int)scene->objects_.size() - 1;
					scene->selectedIndices_ = {idx};
					scene->selectedObjectIndex_ = idx;
					PushUndo(
					    {std::string("Create ") + label,
					     [scene, idx]() {
						     if (idx < (int)scene->objects_.size()) {
							     scene->objects_.erase(scene->objects_.begin() + idx);
							     scene->selectedIndices_.clear();
							     scene->selectedObjectIndex_ = -1;
						     }
					     },
					     [scene, obj, idx]() {
						     scene->objects_.insert(scene->objects_.begin() + idx, obj);
						     scene->selectedIndices_ = {idx};
						     scene->selectedObjectIndex_ = idx;
					     }});
					Log(std::string("Created: ") + label);
				}
			};
			addObj("Empty", "", "");
			addObj("Cube", "Resources/cube/cube.obj", "Resources/white1x1.png");
			addObj("Plane", "Resources/plane.obj", "Resources/white1x1.png");
			ImGui::Separator();
			if (!scene->selectedIndices_.empty() && ImGui::MenuItem("Delete Selected")) {
				std::vector<std::pair<int, SceneObject>> del;
				for (auto it = scene->selectedIndices_.rbegin(); it != scene->selectedIndices_.rend(); ++it) {
					int i = *it;
					if (i < (int)scene->objects_.size() && !scene->objects_[i].locked) {
						del.push_back({i, scene->objects_[i]});
						scene->objects_.erase(scene->objects_.begin() + i);
					}
				}
				scene->selectedIndices_.clear();
				scene->selectedObjectIndex_ = -1;
				if (!del.empty())
					PushUndo(
					    {"Delete",
					     [scene, del]() {
						     for (auto it = del.rbegin(); it != del.rend(); ++it)
							     if (it->first <= (int)scene->objects_.size())
								     scene->objects_.insert(scene->objects_.begin() + it->first, it->second);
					     },
					     [scene, del]() {
						     for (auto& p : del)
							     if (p.first < (int)scene->objects_.size())
								     scene->objects_.erase(scene->objects_.begin() + p.first);
						     scene->selectedIndices_.clear();
						     scene->selectedObjectIndex_ = -1;
					     }});
			}
			ImGui::Separator();
			// 笘・荳諡ｬ繝ｭ繝・け/隗｣髯､
			if (ImGui::MenuItem("Lock All")) {
				for (auto& o : scene->objects_)
					o.locked = true;
				Log("All objects locked");
			}
			if (ImGui::MenuItem("Unlock All")) {
				for (auto& o : scene->objects_)
					o.locked = false;
				Log("All objects unlocked");
			}
			if (!scene->selectedIndices_.empty()) {
				if (ImGui::MenuItem("Lock Selected")) {
					for (int i : scene->selectedIndices_)
						if (i < (int)scene->objects_.size())
							scene->objects_[i].locked = true;
				}
				if (ImGui::MenuItem("Unlock Selected")) {
					for (int i : scene->selectedIndices_)
						if (i < (int)scene->objects_.size())
							scene->objects_[i].locked = false;
				}
			}
			ImGui::EndPopup();
		}

		auto renderNode = [&](auto self, int i, std::set<int>& rendered) -> void {
			if (i < 0 || i >= (int)scene->objects_.size()) return;
			if (rendered.count(scene->objects_[i].id)) return;
			rendered.insert(scene->objects_[i].id);

			bool sel = scene->selectedIndices_.count(i) > 0;
			bool locked = scene->objects_[i].locked;

			ImGui::PushID(i);
			if (locked) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0.4f, 0.4f, 1));
			if (ImGui::SmallButton(locked ? "L##lk" : "U##lk")) { scene->objects_[i].locked = !locked; }
			if (locked) ImGui::PopStyleColor();
			ImGui::SameLine();
			ImGui::PopID();

			std::string lb = (locked ? "[L] " : "") + scene->objects_[i].name + "##" + std::to_string(i);
			ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth;
			if (sel) flags |= ImGuiTreeNodeFlags_Selected;
			
			// 子オブジェクトがあるか確認
			bool hasChildren = false;
			for(const auto& o : scene->objects_) if(o.parentId == scene->objects_[i].id) { hasChildren = true; break; }
			if (!hasChildren) flags |= ImGuiTreeNodeFlags_Leaf;

			bool opened = ImGui::TreeNodeEx(lb.c_str(), flags);

			if (ImGui::IsItemClicked()) {
				if (io.KeyCtrl) {
					if (sel) scene->selectedIndices_.erase(i);
					else scene->selectedIndices_.insert(i);
				} else {
					scene->selectedIndices_ = {i};
				}
				scene->selectedObjectIndex_ = i;
			}

			// ドラッグ＆ドロップ (ソース)
			if (ImGui::BeginDragDropSource()) {
				ImGui::SetDragDropPayload("HIERARCHY_NODE", &i, sizeof(int));
				ImGui::Text("Move %s", scene->objects_[i].name.c_str());
				ImGui::EndDragDropSource();
			}
			// ドラッグ＆ドロップ (ターゲット - 親子付け)
			if (ImGui::BeginDragDropTarget()) {
				if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("HIERARCHY_NODE")) {
					int dragIdx = *(const int*)payload->Data;
					if (dragIdx != i) {
						scene->objects_[dragIdx].parentId = scene->objects_[i].id;
					}
				}
				ImGui::EndDragDropTarget();
			}

			if (opened) {
				for (int j = 0; j < (int)scene->objects_.size(); ++j) {
					if (scene->objects_[j].parentId == scene->objects_[i].id) {
						self(self, j, rendered);
					}
				}
				ImGui::TreePop();
			}
		};

		std::set<int> renderedIds;
		// まず親なし（ルート）を表示
		for (int i = 0; i < (int)scene->objects_.size(); ++i) {
			if (scene->objects_[i].parentId == 0) {
				renderNode(renderNode, i, renderedIds);
			}
		}
		// 親が見つからなかった孤立オブジェクトを表示（安全策）
		for (int i = 0; i < (int)scene->objects_.size(); ++i) {
			if (renderedIds.count(scene->objects_[i].id) == 0) {
				renderNode(renderNode, i, renderedIds);
			}
		}
		
		// 背景へのドロップで親解除
		if (ImGui::BeginDragDropTarget()) {
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("HIERARCHY_NODE")) {
				int dragIdx = *(const int*)payload->Data;
				scene->objects_[dragIdx].parentId = 0;
			}
			ImGui::EndDragDropTarget();
		}
		if (!io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Delete, false) && !scene->selectedIndices_.empty()) {
			for (auto it = scene->selectedIndices_.rbegin(); it != scene->selectedIndices_.rend(); ++it)
				if (*it < (int)scene->objects_.size() && !scene->objects_[*it].locked)
					scene->objects_.erase(scene->objects_.begin() + *it);
			scene->selectedIndices_.clear();
			scene->selectedObjectIndex_ = -1;
		}
	} else
		ImGui::Text("No Active Scene");
	ImGui::End();
}

// ====== Inspector ======
void EditorUI::ShowInspector(GameScene* scene) {
	ImGui::Begin("Inspector");
	if (scene && scene->selectedObjectIndex_ >= 0 && scene->selectedObjectIndex_ < (int)scene->objects_.size()) {
		auto& obj = scene->objects_[scene->selectedObjectIndex_];
		char buf[256];
		strcpy_s(buf, obj.name.c_str());
		if (ImGui::InputText("Name", buf, sizeof(buf))) {
			std::string oN = obj.name, nN = buf;
			obj.name = nN;
			int i = scene->selectedObjectIndex_;
			PushUndo(
			    {"Rename",
			     [scene, i, oN]() {
				     if (i < (int)scene->objects_.size())
					     scene->objects_[i].name = oN;
			     },
			     [scene, i, nN]() {
				     if (i < (int)scene->objects_.size())
					     scene->objects_[i].name = nN;
			     }});
		}
		// ★ IDと親設定
		ImGui::Text("ID: %u", obj.id);
		ImGui::SameLine();
		ImGui::SetNextItemWidth(120);
		auto oldParentId = obj.parentId;
		if (ImGui::InputScalar("Parent ID", ImGuiDataType_U32, &obj.parentId)) {
			auto newParentId = obj.parentId;
			int i = scene->selectedObjectIndex_;
			PushUndo({"Change Parent",
				[scene, i, oldParentId]() { if (i < (int)scene->objects_.size()) scene->objects_[i].parentId = oldParentId; },
				[scene, i, newParentId]() { if (i < (int)scene->objects_.size()) scene->objects_[i].parentId = newParentId; }
			});
		}
		ImGui::SameLine();
		ImGui::Checkbox("Lock", &obj.locked);
		// 笘・ｿｽ蜉: Prefab菫晏ｭ倥・繧ｿ繝ｳ
		ImGui::SameLine();
		if (ImGui::Button("Save Prefab")) {
			std::string ppath = "Resources/" + obj.name + ".prefab";
			std::ofstream pf(ppath);
			if (pf.is_open()) {
				// SerializeSceneObject縺ｯ4繧ｹ繝壹・繧ｹ繧､繝ｳ繝・Φ繝医・繧ｳ繝ｳ繝・く繧ｹ繝医〒蝗ｲ繧薙〒縺・ｋ縺溘ａ縲√◎繧後ｒ縺昴・縺ｾ縺ｾ菴ｿ逕ｨ縺吶ｋ
				pf << "{\n  \"prefab\":\n" << SerializeSceneObject(obj) << "\n}\n";
				pf.close();
				Log("Prefab saved: " + ppath);
			} else {
				LogError("Failed to save prefab: " + ppath);
			}
		}
		if (obj.locked) {
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
			ImGui::Text("** LOCKED - Transform editing disabled **");
			ImGui::PopStyleColor();
		}
		// 笘・繝ｭ繝・け荳ｭ縲√∪縺溘・繝励Ξ繧､荳ｭ縺ｯTransform縺ｨ繧ｳ繝ｳ繝昴・繝阪Φ繝育ｷｨ髮・ｒ辟｡蜉ｹ蛹・
		if (obj.locked || scene->IsPlaying())
			ImGui::BeginDisabled();
		ImGui::Separator();
		ImGui::Text("Transform");
		{
			auto old = obj.translate;
			ImGui::DragFloat3("Position", &obj.translate.x, 0.1f);
			if (ImGui::IsItemDeactivatedAfterEdit()) {
				auto nv = obj.translate;
				int i = scene->selectedObjectIndex_;
				PushUndo(
				    {"Move",
				     [scene, i, old]() {
					     if (i < (int)scene->objects_.size())
						     scene->objects_[i].translate = old;
				     },
				     [scene, i, nv]() {
					     if (i < (int)scene->objects_.size())
						     scene->objects_[i].translate = nv;
				     }});
			}
		}
		{
			auto old = obj.rotate;
			ImGui::DragFloat3("Rotation", &obj.rotate.x, 0.01f);
			if (ImGui::IsItemDeactivatedAfterEdit()) {
				auto nv = obj.rotate;
				int i = scene->selectedObjectIndex_;
				PushUndo(
				    {"Rotate",
				     [scene, i, old]() {
					     if (i < (int)scene->objects_.size())
						     scene->objects_[i].rotate = old;
				     },
				     [scene, i, nv]() {
					     if (i < (int)scene->objects_.size())
						     scene->objects_[i].rotate = nv;
				     }});
			}
		}
		{
			auto old = obj.scale;
			ImGui::DragFloat3("Scale", &obj.scale.x, 0.1f);
			if (ImGui::IsItemDeactivatedAfterEdit()) {
				auto nv = obj.scale;
				int i = scene->selectedObjectIndex_;
				PushUndo(
				    {"Scale",
				     [scene, i, old]() {
					     if (i < (int)scene->objects_.size())
						     scene->objects_[i].scale = old;
				     },
				     [scene, i, nv]() {
					     if (i < (int)scene->objects_.size())
						     scene->objects_[i].scale = nv;
				     }});
			}
		}

		ImGui::Separator();
		{
			auto oldColor = obj.color;
			ImGui::ColorEdit4("Color", &obj.color.x);
			if (ImGui::IsItemDeactivatedAfterEdit()) {
				auto newColor = obj.color;
				int i = scene->selectedObjectIndex_;
				PushUndo(
					{"Change Color",
					 [scene, i, oldColor]() {
						 if (i < (int)scene->objects_.size()) scene->objects_[i].color = oldColor;
					 },
					 [scene, i, newColor]() {
						 if (i < (int)scene->objects_.size()) scene->objects_[i].color = newColor;
					 }});
			}
		}
		ImGui::Separator();
		ImGui::Text("Model: %s", obj.modelPath.empty() ? "(none)" : obj.modelPath.c_str());
		ImGui::Text("Texture: %s", obj.texturePath.empty() ? "(none)" : obj.texturePath.c_str());
		if (ImGui::BeginDragDropTarget()) {
			if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("RESOURCE_PATH")) {
				std::string path((const char*)pl->Data, pl->DataSize - 1);
				auto* r = Engine::Renderer::GetInstance();
				if (path.find(".png") != std::string::npos || path.find(".jpg") != std::string::npos) {
					obj.textureHandle = r->LoadTexture2D(path);
					obj.texturePath = path;
					if (!obj.meshRenderers.empty()) {
						obj.meshRenderers[0].textureHandle = obj.textureHandle;
						obj.meshRenderers[0].texturePath = path;
					}
					Log("Texture: " + path);
				} else if (path.find(".obj") != std::string::npos || path.find(".gltf") != std::string::npos) {
					obj.modelHandle = r->LoadObjMesh(path);
					obj.modelPath = path;
					if (!obj.meshRenderers.empty()) {
						obj.meshRenderers[0].modelHandle = obj.modelHandle;
						obj.meshRenderers[0].modelPath = path;
					}
					Log("Model: " + path);
				}
			}
			ImGui::EndDragDropTarget();
		}

		ImGui::Separator();
		if (ImGui::CollapsingHeader("Components", ImGuiTreeNodeFlags_DefaultOpen)) {
			for (size_t ci = 0; ci < obj.meshRenderers.size(); ++ci) {
				auto& mr = obj.meshRenderers[ci];
				ImGui::PushID((int)ci);
				if (ImGui::TreeNode("MeshRenderer")) {
					bool prevEnabled = mr.enabled;
					if (ImGui::Checkbox("Enabled", &mr.enabled)) {
						bool newVal = mr.enabled;
						int i = scene->selectedObjectIndex_;
						PushUndo({"MeshRenderer Enabled", 
							[scene, i, ci, prevEnabled]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].meshRenderers.size()) scene->objects_[i].meshRenderers[ci].enabled = prevEnabled; },
							[scene, i, ci, newVal]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].meshRenderers.size()) scene->objects_[i].meshRenderers[ci].enabled = newVal; }
						});
					}
					
					auto prevColor = mr.color;
					ImGui::ColorEdit4("Color##MR", &mr.color.x);
					if (ImGui::IsItemDeactivatedAfterEdit()) {
						auto newColor = mr.color;
						int i = scene->selectedObjectIndex_;
						PushUndo({"MeshRenderer Color", 
							[scene, i, ci, prevColor]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].meshRenderers.size()) scene->objects_[i].meshRenderers[ci].color = prevColor; },
							[scene, i, ci, newColor]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].meshRenderers.size()) scene->objects_[i].meshRenderers[ci].color = newColor; }
						});
					}

					auto prevTiling = mr.uvTiling;
					ImGui::DragFloat2("UV Tiling", &mr.uvTiling.x, 0.01f);
					if (ImGui::IsItemDeactivatedAfterEdit()) {
						auto newTiling = mr.uvTiling;
						int i = scene->selectedObjectIndex_;
						PushUndo({"MeshRenderer UV Tiling", 
							[scene, i, ci, prevTiling]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].meshRenderers.size()) scene->objects_[i].meshRenderers[ci].uvTiling = prevTiling; },
							[scene, i, ci, newTiling]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].meshRenderers.size()) scene->objects_[i].meshRenderers[ci].uvTiling = newTiling; }
						});
					}

					auto prevOffset = mr.uvOffset;
					ImGui::DragFloat2("UV Offset", &mr.uvOffset.x, 0.01f);
					if (ImGui::IsItemDeactivatedAfterEdit()) {
						auto newOffset = mr.uvOffset;
						int i = scene->selectedObjectIndex_;
						PushUndo({"MeshRenderer UV Offset", 
							[scene, i, ci, prevOffset]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].meshRenderers.size()) scene->objects_[i].meshRenderers[ci].uvOffset = prevOffset; },
							[scene, i, ci, newOffset]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].meshRenderers.size()) scene->objects_[i].meshRenderers[ci].uvOffset = newOffset; }
						});
					}
					
					// 笘・ｿｽ蜉: Shader驕ｸ謚・
					if (ImGui::BeginCombo("Shader", mr.shaderName.c_str())) {
						if (ImGui::Selectable("Default", mr.shaderName == "Default")) mr.shaderName = "Default";
						if (ImGui::Selectable("Toon", mr.shaderName == "Toon")) mr.shaderName = "Toon";
						if (ImGui::Selectable("ToonSkinning", mr.shaderName == "ToonSkinning")) mr.shaderName = "ToonSkinning";
						if (ImGui::Selectable("EnhancedTerrain", mr.shaderName == "EnhancedTerrain")) mr.shaderName = "EnhancedTerrain";
						if (ImGui::Selectable("StylizedGrass", mr.shaderName == "StylizedGrass")) mr.shaderName = "StylizedGrass";
						ImGui::EndCombo();
					}

					if (mr.shaderName == "EnhancedTerrain") {
						ImGui::Separator();
						ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "Terrain Settings");
						const char* layerNames[] = { "Splat Map", "Layer 0 (Grass)", "Layer 1 (Rock)", "Layer 2 (Dirt)", "Layer 3 (Sand)", "Detail Map" };
						if (mr.extraTexturePaths.size() < 5) mr.extraTexturePaths.resize(5);
						if (mr.extraTextureHandles.size() < 5) mr.extraTextureHandles.resize(5);

						for (int i = 0; i < 6; ++i) {
							std::string path = (i == 0) ? mr.texturePath : mr.extraTexturePaths[i - 1];
							ImGui::Text("%s: %s", layerNames[i], path.empty() ? "(none)" : path.c_str());
							if (ImGui::BeginDragDropTarget()) {
								if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("RESOURCE_PATH")) {
									std::string newPath((const char*)pl->Data, pl->DataSize - 1);
									if (newPath.find(".png") != std::string::npos || newPath.find(".jpg") != std::string::npos) {
										uint32_t handle = Engine::Renderer::GetInstance()->LoadTexture2D(newPath);
										if (i == 0) {
											mr.texturePath = newPath;
											mr.textureHandle = handle;
										} else {
											mr.extraTexturePaths[i - 1] = newPath;
											mr.extraTextureHandles[i - 1] = handle;
										}
										EditorUI::Log(std::string(layerNames[i]) + ": " + newPath);
									}
								}
								ImGui::EndDragDropTarget();
							}
						}
						ImGui::Separator();
					}

					if (mr.shaderName == "StylizedGrass") {
						ImGui::Separator();
						ImGui::TextColored(ImVec4(0.4f, 1.0f, 1.0f, 1.0f), "Grass Settings");
						ImGui::Text("Tip: uv.y=0 is Tip, uv.y=1 is Bottom");
						auto* renderer = Engine::Renderer::GetInstance();
						static float windDir[2] = { 1.0f, 0.0f };
						static float windSpeed = 0.5f;
						static float windStrength = 0.2f;
						bool windChanged = false;
						if (ImGui::DragFloat2("Wind Direction", windDir, 0.01f)) windChanged = true;
						if (ImGui::DragFloat("Wind Speed", &windSpeed, 0.01f)) windChanged = true;
						if (ImGui::DragFloat("Wind Strength", &windStrength, 0.01f)) windChanged = true;
						if (windChanged) renderer->SetWindParams(Engine::Vector4{ windDir[0], windDir[1], windSpeed, windStrength });
						ImGui::Separator();
					}

					ImGui::Text("Lightmap: %s", mr.lightmapPath.empty() ? "(none)" : mr.lightmapPath.c_str());
					if (ImGui::BeginDragDropTarget()) {
						if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("RESOURCE_PATH")) {
							std::string path((const char*)pl->Data, pl->DataSize - 1);
							if (path.find(".png") != std::string::npos || path.find(".jpg") != std::string::npos) {
								mr.lightmapHandle = Engine::Renderer::GetInstance()->LoadTexture2D(path);
								mr.lightmapPath = path;
							}
						}
						ImGui::EndDragDropTarget();
					}
					if (ImGui::Button("Remove##MR")) {
						obj.meshRenderers.erase(obj.meshRenderers.begin() + ci);
						ImGui::TreePop();
						ImGui::PopID();
						goto end_comp;
					}
					ImGui::TreePop();
				}
				ImGui::PopID();
			}
			for (size_t ci = 0; ci < obj.boxColliders.size(); ++ci) {
				auto& bc = obj.boxColliders[ci];
				ImGui::PushID(1000 + (int)ci);
				if (ImGui::TreeNode("BoxCollider")) {
					bool prevEnabled = bc.enabled;
					if (ImGui::Checkbox("Enabled", &bc.enabled)) {
						bool newVal = bc.enabled;
						int i = scene->selectedObjectIndex_;
						PushUndo({"BoxCollider Enabled",
							[scene, i, ci, prevEnabled]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].boxColliders.size()) scene->objects_[i].boxColliders[ci].enabled = prevEnabled; },
							[scene, i, ci, newVal]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].boxColliders.size()) scene->objects_[i].boxColliders[ci].enabled = newVal; }
						});
					}

					auto prevCenter = bc.center;
					ImGui::DragFloat3("Center", &bc.center.x, 0.1f);
					if (ImGui::IsItemDeactivatedAfterEdit()) {
						auto newCenter = bc.center;
						int i = scene->selectedObjectIndex_;
						PushUndo({"BoxCollider Center",
							[scene, i, ci, prevCenter]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].boxColliders.size()) scene->objects_[i].boxColliders[ci].center = prevCenter; },
							[scene, i, ci, newCenter]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].boxColliders.size()) scene->objects_[i].boxColliders[ci].center = newCenter; }
						});
					}

					auto prevSize = bc.size;
					ImGui::DragFloat3("Size", &bc.size.x, 0.1f);
					if (ImGui::IsItemDeactivatedAfterEdit()) {
						auto newSize = bc.size;
						int i = scene->selectedObjectIndex_;
						PushUndo({"BoxCollider Size",
							[scene, i, ci, prevSize]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].boxColliders.size()) scene->objects_[i].boxColliders[ci].size = prevSize; },
							[scene, i, ci, newSize]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].boxColliders.size()) scene->objects_[i].boxColliders[ci].size = newSize; }
						});
					}

					bool prevTrigger = bc.isTrigger;
					if (ImGui::Checkbox("Is Trigger", &bc.isTrigger)) {
						bool newVal = bc.isTrigger;
						int i = scene->selectedObjectIndex_;
						PushUndo({"BoxCollider IsTrigger",
							[scene, i, ci, prevTrigger]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].boxColliders.size()) scene->objects_[i].boxColliders[ci].isTrigger = prevTrigger; },
							[scene, i, ci, newVal]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].boxColliders.size()) scene->objects_[i].boxColliders[ci].isTrigger = newVal; }
						});
					}
					if (ImGui::Button("Remove##BC")) {
						obj.boxColliders.erase(obj.boxColliders.begin() + ci);
						ImGui::TreePop();
						ImGui::PopID();
						goto end_comp;
					}
					ImGui::TreePop();
				}
				ImGui::PopID();
			}
			for (size_t ci = 0; ci < obj.tags.size(); ++ci) {
				auto& tg = obj.tags[ci];
				ImGui::PushID(2000 + (int)ci);
				if (ImGui::TreeNode("Tag")) {
					char tb[128];
					strcpy_s(tb, tg.tag.c_str());
					std::string oldTag = tg.tag;
					if (ImGui::InputText("Tag", tb, sizeof(tb))) {
						tg.tag = tb;
						std::string newTag = tg.tag;
						int i = scene->selectedObjectIndex_;
						PushUndo({"Change Tag",
							[scene, i, ci, oldTag]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].tags.size()) scene->objects_[i].tags[ci].tag = oldTag; },
							[scene, i, ci, newTag]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].tags.size()) scene->objects_[i].tags[ci].tag = newTag; }
						});
					}
					if (ImGui::Button("Remove##Tag")) {
						obj.tags.erase(obj.tags.begin() + ci);
						ImGui::TreePop();
						ImGui::PopID();
						goto end_comp;
					}
					ImGui::TreePop();
				}
				ImGui::PopID();
			}
			for (size_t ci = 0; ci < obj.animators.size(); ++ci) {
				auto& an = obj.animators[ci];
				ImGui::PushID(3000 + (int)ci);
				if (ImGui::TreeNode("Animator")) {
					bool prevEnabled = an.enabled;
					if (ImGui::Checkbox("Enabled", &an.enabled)) {
						bool newVal = an.enabled;
						int i = scene->selectedObjectIndex_;
						PushUndo({"Animator Enabled",
							[scene, i, ci, prevEnabled]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].animators.size()) scene->objects_[i].animators[ci].enabled = prevEnabled; },
							[scene, i, ci, newVal]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].animators.size()) scene->objects_[i].animators[ci].enabled = newVal; }
						});
					}
					
					char tb[128];
					strcpy_s(tb, an.currentAnimation.c_str());
					std::string oldAnim = an.currentAnimation;
					if (ImGui::InputText("Animation", tb, sizeof(tb))) {
						an.currentAnimation = tb;
						std::string newAnim = an.currentAnimation;
						int i = scene->selectedObjectIndex_;
						PushUndo({"Change Animation",
							[scene, i, ci, oldAnim]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].animators.size()) scene->objects_[i].animators[ci].currentAnimation = oldAnim; },
							[scene, i, ci, newAnim]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].animators.size()) scene->objects_[i].animators[ci].currentAnimation = newAnim; }
						});
					}

					bool prevPlaying = an.isPlaying;
					if (ImGui::Checkbox("Is Playing", &an.isPlaying)) {
						bool newVal = an.isPlaying;
						int i = scene->selectedObjectIndex_;
						PushUndo({"Animator IsPlaying",
							[scene, i, ci, prevPlaying]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].animators.size()) scene->objects_[i].animators[ci].isPlaying = prevPlaying; },
							[scene, i, ci, newVal]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].animators.size()) scene->objects_[i].animators[ci].isPlaying = newVal; }
						});
					}

					ImGui::SameLine();
					bool prevLoop = an.loop;
					if (ImGui::Checkbox("Loop", &an.loop)) {
						bool newVal = an.loop;
						int i = scene->selectedObjectIndex_;
						PushUndo({"Animator Loop",
							[scene, i, ci, prevLoop]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].animators.size()) scene->objects_[i].animators[ci].loop = prevLoop; },
							[scene, i, ci, newVal]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].animators.size()) scene->objects_[i].animators[ci].loop = newVal; }
						});
					}

					auto prevSpeed = an.speed;
					ImGui::DragFloat("Speed", &an.speed, 0.01f);
					if (ImGui::IsItemDeactivatedAfterEdit()) {
						auto newSpeed = an.speed;
						int i = scene->selectedObjectIndex_;
						PushUndo({"Animator Speed",
							[scene, i, ci, prevSpeed]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].animators.size()) scene->objects_[i].animators[ci].speed = prevSpeed; },
							[scene, i, ci, newSpeed]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].animators.size()) scene->objects_[i].animators[ci].speed = newSpeed; }
						});
					}

					auto prevTime = an.time;
					ImGui::DragFloat("Time", &an.time, 0.01f);
					if (ImGui::IsItemDeactivatedAfterEdit()) {
						auto newTime = an.time;
						int i = scene->selectedObjectIndex_;
						PushUndo({"Animator Time",
							[scene, i, ci, prevTime]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].animators.size()) scene->objects_[i].animators[ci].time = prevTime; },
							[scene, i, ci, newTime]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].animators.size()) scene->objects_[i].animators[ci].time = newTime; }
						});
					}
					if (ImGui::Button("Remove##Anim")) {
						obj.animators.erase(obj.animators.begin() + ci);
						ImGui::TreePop();
						ImGui::PopID();
						goto end_comp;
					}
					ImGui::TreePop();
				}
				ImGui::PopID();
			}
			for (size_t ci = 0; ci < obj.rigidbodies.size(); ++ci) {
				auto& rb = obj.rigidbodies[ci];
				ImGui::PushID(4000 + (int)ci);
				if (ImGui::TreeNode("Rigidbody")) {
					bool prevEnabled = rb.enabled;
					if (ImGui::Checkbox("Enabled", &rb.enabled)) {
						bool newVal = rb.enabled;
						int i = scene->selectedObjectIndex_;
						PushUndo({"Rigidbody Enabled",
							[scene, i, ci, prevEnabled]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].rigidbodies.size()) scene->objects_[i].rigidbodies[ci].enabled = prevEnabled; },
							[scene, i, ci, newVal]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].rigidbodies.size()) scene->objects_[i].rigidbodies[ci].enabled = newVal; }
						});
					}

					auto prevVel = rb.velocity;
					ImGui::DragFloat3("Velocity", &rb.velocity.x, 0.1f);
					if (ImGui::IsItemDeactivatedAfterEdit()) {
						auto newVel = rb.velocity;
						int i = scene->selectedObjectIndex_;
						PushUndo({"Rigidbody Velocity",
							[scene, i, ci, prevVel]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].rigidbodies.size()) scene->objects_[i].rigidbodies[ci].velocity = prevVel; },
							[scene, i, ci, newVel]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].rigidbodies.size()) scene->objects_[i].rigidbodies[ci].velocity = newVel; }
						});
					}

					bool prevGrav = rb.useGravity;
					if (ImGui::Checkbox("Use Gravity", &rb.useGravity)) {
						bool newVal = rb.useGravity;
						int i = scene->selectedObjectIndex_;
						PushUndo({"Rigidbody Use Gravity",
							[scene, i, ci, prevGrav]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].rigidbodies.size()) scene->objects_[i].rigidbodies[ci].useGravity = prevGrav; },
							[scene, i, ci, newVal]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].rigidbodies.size()) scene->objects_[i].rigidbodies[ci].useGravity = newVal; }
						});
					}

					bool prevKinem = rb.isKinematic;
					if (ImGui::Checkbox("Is Kinematic", &rb.isKinematic)) {
						bool newVal = rb.isKinematic;
						int i = scene->selectedObjectIndex_;
						PushUndo({"Rigidbody Is Kinematic",
							[scene, i, ci, prevKinem]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].rigidbodies.size()) scene->objects_[i].rigidbodies[ci].isKinematic = prevKinem; },
							[scene, i, ci, newVal]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].rigidbodies.size()) scene->objects_[i].rigidbodies[ci].isKinematic = newVal; }
						});
					}
					if (ImGui::Button("Remove##RB")) {
						obj.rigidbodies.erase(obj.rigidbodies.begin() + ci);
						ImGui::TreePop();
						ImGui::PopID();
						goto end_comp;
					}
					ImGui::TreePop();
				}
				ImGui::PopID();
			}
			// 笘・ｿｽ蜉: ParticleEmitter 繧ｳ繝ｳ繝昴・繝阪Φ繝・
			for (size_t ci = 0; ci < obj.particleEmitters.size(); ++ci) {
				auto& pe = obj.particleEmitters[ci];
				ImGui::PushID(5000 + (int)ci);
				if (ImGui::TreeNode("Particle Emitter")) {
					ImGui::Checkbox("Enabled##PE", &pe.enabled);

					// 笘・ｿｽ蜉: 繧｢繧ｻ繝・ヨ繝代せ縺ｨD&D
					ImGui::Text("Asset: %s", pe.assetPath.empty() ? "(none)" : pe.assetPath.c_str());
					if (ImGui::BeginDragDropTarget()) {
						if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("RESOURCE_PATH")) {
							std::string path((const char*)pl->Data, pl->DataSize - 1);
							if (path.find(".particle") != std::string::npos) {
								pe.assetPath = path;
								pe.emitter.LoadFromJson(path);
								Log("Loaded particle asset: " + path);
							}
						}
						ImGui::EndDragDropTarget();
					}

					if (pe.enabled) {
						Engine::EmitterParams& p = pe.emitter.params;

						// --- 繝輔ぃ繧､繝ｫ騾｣謳ｺ ---
						ImGui::Separator();
						ImGui::Text("Asset Link");
						char assetBuf[256];
						strcpy_s(assetBuf, pe.assetPath.c_str());
						if (ImGui::InputText("Asset Path##PE", assetBuf, sizeof(assetBuf))) {
							std::string newPath = assetBuf;
							if (newPath != pe.assetPath) {
								pe.assetPath = newPath;
								// 繝代せ縺悟､画峩縺輔ｌ縺溘ｉ閾ｪ蜍慕噪縺ｫ隱ｭ縺ｿ霎ｼ繧
								pe.emitter.LoadFromJson(pe.assetPath);
							}
						}

						if (ImGui::BeginDragDropTarget()) {
							if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("RESOURCE_PATH")) {
								std::string path((const char*)payload->Data, payload->DataSize - 1);
								if (path.find(".particle") != std::string::npos || path.find(".json") != std::string::npos) {
									pe.assetPath = path;
									pe.emitter.LoadFromJson(path);
									Log("Loaded particle asset: " + path);
								}
							}
							ImGui::EndDragDropTarget();
						}

						if (!pe.assetPath.empty()) {
							if (ImGui::Button("Save Settings to File##PE")) {
								if (pe.emitter.SaveToJson(pe.assetPath)) {
									Log("Saved particle settings to: " + pe.assetPath);
								} else {
									LogError("Failed to save particle settings to: " + pe.assetPath);
								}
							}
							ImGui::SameLine();
							if (ImGui::Button("Reload from File##PE")) {
								if (pe.emitter.LoadFromJson(pe.assetPath)) {
									Log("Reloaded particle settings from: " + pe.assetPath);
								} else {
									LogError("Failed to reload particle settings from: " + pe.assetPath);
								}
							}
						} else {
							ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.4f, 1.0f), "Set an asset path to save/load.");
						}
						ImGui::Separator();

						// --- 繝励Ξ繝薙Η繝ｼ/蝓ｺ譛ｬ險ｭ螳・---
						ImGui::Checkbox("Is Playing##PE", &pe.emitter.isPlaying);

						if (ImGui::CollapsingHeader("Emission##PE", ImGuiTreeNodeFlags_DefaultOpen)) {
							ImGui::DragFloat("Emit Rate##PE", &p.emitRate, 1.0f, 0.0f, 1000.0f);
							ImGui::DragInt("Burst Count##PE", &p.burstCount, 1, 0, 1000);
							if (ImGui::Button("Emit Burst (10)##PE")) {
								pe.emitter.EmitBurst(10);
							}
						}

						// 蠖｢迥ｶ
						if (ImGui::CollapsingHeader("Shape##PEHeader", ImGuiTreeNodeFlags_DefaultOpen)) {
							int shapeType = static_cast<int>(p.shape);
							const char* shapeNames[] = {"Point", "Sphere", "Cone"};
							if (ImGui::Combo("Shape##PECombo", &shapeType, shapeNames, IM_ARRAYSIZE(shapeNames)))
								p.shape = static_cast<Engine::EmissionShape>(shapeType);
							if (p.shape != Engine::EmissionShape::Point)
								ImGui::DragFloat("Shape Radius##PE", &p.shapeRadius, 0.01f, 0.0f, 100.0f);
							if (p.shape == Engine::EmissionShape::Cone)
								ImGui::DragFloat("Cone Angle##PE", &p.shapeAngle, 0.01f, 0.0f, 3.1415f);
						}

						if (ImGui::CollapsingHeader("Life Time & Physics##PE", ImGuiTreeNodeFlags_DefaultOpen)) {
							ImGui::DragFloat("Life Time##PE", &p.lifeTime, 0.1f, 0.1f, 30.0f);
							ImGui::DragFloat("Life Variance##PE", &p.lifeTimeVariance, 0.01f, 0.0f, 5.0f);
							ImGui::DragFloat3("Velocity##PE", &p.startVelocity.x, 0.1f);
							ImGui::DragFloat3("Vel Variance##PE", &p.velocityVariance.x, 0.1f);
							ImGui::DragFloat3("Acceleration##PE", &p.acceleration.x, 0.1f);
							ImGui::DragFloat("Damping##PE", &p.damping, 0.01f, 0.0f, 100.0f);
						}

						if (ImGui::CollapsingHeader("Size & Color##PE", ImGuiTreeNodeFlags_DefaultOpen)) {
							ImGui::DragFloat3("Start Size##PE", &p.startSize.x, 0.1f, 0.0f, 100.0f);
							ImGui::DragFloat3("End Size##PE", &p.endSize.x, 0.1f, 0.0f, 100.0f);
							ImGui::ColorEdit4("Start Color##PE", &p.startColor.x);
							ImGui::ColorEdit4("End Color##PE", &p.endColor.x);
						}

						if (ImGui::CollapsingHeader("Rendering & UV##PE", ImGuiTreeNodeFlags_DefaultOpen)) {
							char texBuf[256];
							strcpy_s(texBuf, p.texturePath.c_str());
							if (ImGui::InputText("Texture##PE", texBuf, sizeof(texBuf)))
								p.texturePath = texBuf;

							char shaderBuf[256];
							strcpy_s(shaderBuf, p.shaderName.c_str());
							if (ImGui::InputText("Shader##PE", shaderBuf, sizeof(shaderBuf)))
								p.shaderName = shaderBuf;

							ImGui::Checkbox("Additive Blend##PE", &p.isAdditive);
							ImGui::Checkbox("Use Billboard##PE", &p.useBillboard);

							// UV繧｢繝九Γ繝ｼ繧ｷ繝ｧ繝ｳ
							ImGui::Separator();
							ImGui::Checkbox("Use UV Animation##PE", &p.useUvAnim);
							if (p.useUvAnim) {
								ImGui::DragInt("Cols##PE", &p.uvAnimCols, 0.1f, 1, 64);
								ImGui::DragInt("Rows##PE", &p.uvAnimRows, 0.1f, 1, 64);
								ImGui::DragFloat("FPS##PE", &p.uvAnimFps, 0.1f, 0.1f, 120.0f);
							}
						}
					}
					if (ImGui::Button("Remove##PE")) {
						obj.particleEmitters.erase(obj.particleEmitters.begin() + ci);
						ImGui::TreePop();
						ImGui::PopID();
						goto end_comp;
					}
					ImGui::TreePop();
				}
				ImGui::PopID();
			}
			// 笘・ｿｽ蜉: GpuMeshCollider 繧ｳ繝ｳ繝昴・繝阪Φ繝・
			for (size_t ci = 0; ci < obj.gpuMeshColliders.size(); ++ci) {
				auto& gmc = obj.gpuMeshColliders[ci];
				ImGui::PushID(6000 + (int)ci);
				if (ImGui::TreeNode("GpuMeshCollider")) {
					bool prevEnabled = gmc.enabled;
					if (ImGui::Checkbox("Enabled##GMC", &gmc.enabled)) {
						bool newVal = gmc.enabled;
						int i = scene->selectedObjectIndex_;
						PushUndo({"GpuMeshCollider Enabled",
							[scene, i, ci, prevEnabled]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].gpuMeshColliders.size()) scene->objects_[i].gpuMeshColliders[ci].enabled = prevEnabled; },
							[scene, i, ci, newVal]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].gpuMeshColliders.size()) scene->objects_[i].gpuMeshColliders[ci].enabled = newVal; }
						});
					}

					bool prevTrigger = gmc.isTrigger;
					if (ImGui::Checkbox("Is Trigger##GMC", &gmc.isTrigger)) {
						bool newVal = gmc.isTrigger;
						int i = scene->selectedObjectIndex_;
						PushUndo({"GpuMeshCollider IsTrigger",
							[scene, i, ci, prevTrigger]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].gpuMeshColliders.size()) scene->objects_[i].gpuMeshColliders[ci].isTrigger = prevTrigger; },
							[scene, i, ci, newVal]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].gpuMeshColliders.size()) scene->objects_[i].gpuMeshColliders[ci].isTrigger = newVal; }
						});
					}
					
					const char* collisionTypes[] = { "Mesh", "Convex" };
					int currentType = (int)gmc.collisionType;
					MeshCollisionType prevType = gmc.collisionType;
					if (ImGui::Combo("Collision Type##GMC", &currentType, collisionTypes, IM_ARRAYSIZE(collisionTypes))) {
						gmc.collisionType = (MeshCollisionType)currentType;
						MeshCollisionType newType = gmc.collisionType;
						int i = scene->selectedObjectIndex_;
						PushUndo({"GpuMeshCollider Type",
							[scene, i, ci, prevType]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].gpuMeshColliders.size()) scene->objects_[i].gpuMeshColliders[ci].collisionType = prevType; },
							[scene, i, ci, newType]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].gpuMeshColliders.size()) scene->objects_[i].gpuMeshColliders[ci].collisionType = newType; }
						});
					}

					ImGui::Text("Mesh: %s", gmc.meshPath.empty() ? "(none)" : gmc.meshPath.c_str());
					if (ImGui::BeginDragDropTarget()) {
						if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("RESOURCE_PATH")) {
							std::string path((const char*)pl->Data, pl->DataSize - 1);
							if (path.find(".obj") != std::string::npos || path.find(".gltf") != std::string::npos) {
								gmc.meshHandle = Engine::Renderer::GetInstance()->LoadObjMesh(path);
								gmc.meshPath = path;
								Log("GpuMeshCollider Mesh: " + path);
							}
						}
						ImGui::EndDragDropTarget();
					}
					if (gmc.isIntersecting)
						ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "[Intersection Detected!]");
					else
						ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "No Intersection");
					if (ImGui::Button("Remove##GMC")) {
						obj.gpuMeshColliders.erase(obj.gpuMeshColliders.begin() + ci);
						ImGui::TreePop();
						ImGui::PopID();
						goto end_comp;
					}
					ImGui::TreePop();
				}
				ImGui::PopID();
			}

			// 笘・ｿｽ蜉: PlayerInput 繧ｳ繝ｳ繝昴・繝阪Φ繝・
			for (size_t ci = 0; ci < obj.playerInputs.size(); ++ci) {
				auto& pi = obj.playerInputs[ci];
				ImGui::PushID(7000 + (int)ci);
				if (ImGui::TreeNode("PlayerInput")) {
					bool prevEnabled = pi.enabled;
					if (ImGui::Checkbox("Enabled##PI", &pi.enabled)) {
						bool newVal = pi.enabled;
						int i = scene->selectedObjectIndex_;
						PushUndo({"PlayerInput Enabled",
							[scene, i, ci, prevEnabled]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].playerInputs.size()) scene->objects_[i].playerInputs[ci].enabled = prevEnabled; },
							[scene, i, ci, newVal]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].playerInputs.size()) scene->objects_[i].playerInputs[ci].enabled = newVal; }
						});
					}
					ImGui::Text("MoveDir: (%.2f, %.2f)", pi.moveDir.x, pi.moveDir.y);
					ImGui::Text("JumpRequested: %s", pi.jumpRequested ? "true" : "false");
					ImGui::Text("AttackRequested: %s", pi.attackRequested ? "true" : "false");
					if (ImGui::Button("Remove##PI")) {
						obj.playerInputs.erase(obj.playerInputs.begin() + ci);
						ImGui::TreePop();
						ImGui::PopID();
						goto end_comp;
					}
					ImGui::TreePop();
				}
				ImGui::PopID();
			}

			// 笘・ｿｽ蜉: CharacterMovement 繧ｳ繝ｳ繝昴・繝阪Φ繝・
			for (size_t ci = 0; ci < obj.characterMovements.size(); ++ci) {
				auto& cm = obj.characterMovements[ci];
				ImGui::PushID(8000 + (int)ci);
				if (ImGui::TreeNode("CharacterMovement")) {
					bool prevEnabled = cm.enabled;
					if (ImGui::Checkbox("Enabled##CM", &cm.enabled)) {
						bool newVal = cm.enabled;
						int i = scene->selectedObjectIndex_;
						PushUndo({"CharacterMovement Enabled",
							[scene, i, ci, prevEnabled]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].characterMovements.size()) scene->objects_[i].characterMovements[ci].enabled = prevEnabled; },
							[scene, i, ci, newVal]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].characterMovements.size()) scene->objects_[i].characterMovements[ci].enabled = newVal; }
						});
					}
					
					auto prevSpeed = cm.speed;
					ImGui::DragFloat("Speed##CM", &cm.speed, 0.1f);
					if (ImGui::IsItemDeactivatedAfterEdit()) {
						auto newSpeed = cm.speed;
						int i = scene->selectedObjectIndex_;
						PushUndo({"CharacterMovement Speed",
							[scene, i, ci, prevSpeed]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].characterMovements.size()) scene->objects_[i].characterMovements[ci].speed = prevSpeed; },
							[scene, i, ci, newSpeed]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].characterMovements.size()) scene->objects_[i].characterMovements[ci].speed = newSpeed; }
						});
					}

					auto prevJump = cm.jumpPower;
					ImGui::DragFloat("JumpPower##CM", &cm.jumpPower, 0.1f);
					if (ImGui::IsItemDeactivatedAfterEdit()) {
						auto newJump = cm.jumpPower;
						int i = scene->selectedObjectIndex_;
						PushUndo({"CharacterMovement JumpPower",
							[scene, i, ci, prevJump]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].characterMovements.size()) scene->objects_[i].characterMovements[ci].jumpPower = prevJump; },
							[scene, i, ci, newJump]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].characterMovements.size()) scene->objects_[i].characterMovements[ci].jumpPower = newJump; }
						});
					}

					auto prevGrav = cm.gravity;
					ImGui::DragFloat("Gravity##CM", &cm.gravity, 0.1f);
					if (ImGui::IsItemDeactivatedAfterEdit()) {
						auto newGrav = cm.gravity;
						int i = scene->selectedObjectIndex_;
						PushUndo({"CharacterMovement Gravity",
							[scene, i, ci, prevGrav]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].characterMovements.size()) scene->objects_[i].characterMovements[ci].gravity = prevGrav; },
							[scene, i, ci, newGrav]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].characterMovements.size()) scene->objects_[i].characterMovements[ci].gravity = newGrav; }
						});
					}
					ImGui::Text("VelocityY: %.2f", cm.velocityY);
					ImGui::Text("IsGrounded: %s", cm.isGrounded ? "true" : "false");
					if (ImGui::Button("Remove##CM")) {
						obj.characterMovements.erase(obj.characterMovements.begin() + ci);
						ImGui::TreePop();
						ImGui::PopID();
						goto end_comp;
					}
					ImGui::TreePop();
				}
				ImGui::PopID();
			}

			// 笘・ｿｽ蜉: CameraTarget 繧ｳ繝ｳ繝昴・繝阪Φ繝・
			for (size_t ci = 0; ci < obj.cameraTargets.size(); ++ci) {
				auto& ct = obj.cameraTargets[ci];
				ImGui::PushID(9000 + (int)ci);
				if (ImGui::TreeNode("CameraTarget")) {
					bool prevEnabled = ct.enabled;
					if (ImGui::Checkbox("Enabled##CT", &ct.enabled)) {
						bool newVal = ct.enabled;
						int i = scene->selectedObjectIndex_;
						PushUndo({"CameraTarget Enabled",
							[scene, i, ci, prevEnabled]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].cameraTargets.size()) scene->objects_[i].cameraTargets[ci].enabled = prevEnabled; },
							[scene, i, ci, newVal]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].cameraTargets.size()) scene->objects_[i].cameraTargets[ci].enabled = newVal; }
						});
					}

					auto prevDist = ct.distance;
					ImGui::DragFloat("Distance##CT", &ct.distance, 0.1f);
					if (ImGui::IsItemDeactivatedAfterEdit()) {
						auto newDist = ct.distance;
						int i = scene->selectedObjectIndex_;
						PushUndo({"CameraTarget Distance",
							[scene, i, ci, prevDist]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].cameraTargets.size()) scene->objects_[i].cameraTargets[ci].distance = prevDist; },
							[scene, i, ci, newDist]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].cameraTargets.size()) scene->objects_[i].cameraTargets[ci].distance = newDist; }
						});
					}

					auto prevHeight = ct.height;
					ImGui::DragFloat("Height##CT", &ct.height, 0.1f);
					if (ImGui::IsItemDeactivatedAfterEdit()) {
						auto newHeight = ct.height;
						int i = scene->selectedObjectIndex_;
						PushUndo({"CameraTarget Height",
							[scene, i, ci, prevHeight]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].cameraTargets.size()) scene->objects_[i].cameraTargets[ci].height = prevHeight; },
							[scene, i, ci, newHeight]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].cameraTargets.size()) scene->objects_[i].cameraTargets[ci].height = newHeight; }
						});
					}

					auto prevSmooth = ct.smoothSpeed;
					ImGui::DragFloat("SmoothSpeed##CT", &ct.smoothSpeed, 0.1f);
					if (ImGui::IsItemDeactivatedAfterEdit()) {
						auto newSmooth = ct.smoothSpeed;
						int i = scene->selectedObjectIndex_;
						PushUndo({"CameraTarget SmoothSpeed",
							[scene, i, ci, prevSmooth]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].cameraTargets.size()) scene->objects_[i].cameraTargets[ci].smoothSpeed = prevSmooth; },
							[scene, i, ci, newSmooth]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].cameraTargets.size()) scene->objects_[i].cameraTargets[ci].smoothSpeed = newSmooth; }
						});
					}
					if (ImGui::Button("Remove##CT")) {
						obj.cameraTargets.erase(obj.cameraTargets.begin() + ci);
						ImGui::TreePop();
						ImGui::PopID();
						goto end_comp;
					}
					ImGui::TreePop();
				}
				ImGui::PopID();
			}

			// 笘・ｿｽ蜉: DirectionalLight 繧ｳ繝ｳ繝昴・繝阪Φ繝・
			for (size_t ci = 0; ci < obj.directionalLights.size(); ++ci) {
				auto& dl = obj.directionalLights[ci];
				ImGui::PushID(10000 + (int)ci);
				if (ImGui::TreeNode("DirectionalLight")) {
					bool prevEnabled = dl.enabled;
					if (ImGui::Checkbox("Enabled##DL", &dl.enabled)) {
						bool newVal = dl.enabled;
						int i = scene->selectedObjectIndex_;
						PushUndo({"DirectionalLight Enabled",
							[scene, i, ci, prevEnabled]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].directionalLights.size()) scene->objects_[i].directionalLights[ci].enabled = prevEnabled; },
							[scene, i, ci, newVal]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].directionalLights.size()) scene->objects_[i].directionalLights[ci].enabled = newVal; }
						});
					}

					// ColorEdit3
					auto prevColor = dl.color;
					ImGui::ColorEdit3("Color##DL", &dl.color.x);
					if (ImGui::IsItemDeactivatedAfterEdit()) {
						auto newColor = dl.color;
						int i = scene->selectedObjectIndex_;
						PushUndo({"DirectionalLight Color",
							[scene, i, ci, prevColor]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].directionalLights.size()) scene->objects_[i].directionalLights[ci].color = prevColor; },
							[scene, i, ci, newColor]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].directionalLights.size()) scene->objects_[i].directionalLights[ci].color = newColor; }
						});
					}

					auto prevIntensity = dl.intensity;
					ImGui::DragFloat("Intensity##DL", &dl.intensity, 0.01f, 0.0f, 100.0f);
					if (ImGui::IsItemDeactivatedAfterEdit()) {
						auto newIntensity = dl.intensity;
						int i = scene->selectedObjectIndex_;
						PushUndo({"DirectionalLight Intensity",
							[scene, i, ci, prevIntensity]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].directionalLights.size()) scene->objects_[i].directionalLights[ci].intensity = prevIntensity; },
							[scene, i, ci, newIntensity]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].directionalLights.size()) scene->objects_[i].directionalLights[ci].intensity = newIntensity; }
						});
					}
					if (ImGui::Button("Remove##DL")) {
						obj.directionalLights.erase(obj.directionalLights.begin() + ci);
						ImGui::TreePop();
						ImGui::PopID();
						goto end_comp;
					}
					ImGui::TreePop();
				}
				ImGui::PopID();
			}

			// 笘・ｿｽ蜉: PointLight 繧ｳ繝ｳ繝昴・繝阪Φ繝・
			for (size_t ci = 0; ci < obj.pointLights.size(); ++ci) {
				auto& pl = obj.pointLights[ci];
				ImGui::PushID(11000 + (int)ci);
				if (ImGui::TreeNode("PointLight")) {
					bool prevEnabled = pl.enabled;
					if (ImGui::Checkbox("Enabled##PL", &pl.enabled)) {
						bool newVal = pl.enabled;
						int i = scene->selectedObjectIndex_;
						PushUndo({"PointLight Enabled",
							[scene, i, ci, prevEnabled]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].pointLights.size()) scene->objects_[i].pointLights[ci].enabled = prevEnabled; },
							[scene, i, ci, newVal]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].pointLights.size()) scene->objects_[i].pointLights[ci].enabled = newVal; }
						});
					}

					auto prevColor = pl.color;
					ImGui::ColorEdit3("Color##PL", &pl.color.x);
					if (ImGui::IsItemDeactivatedAfterEdit()) {
						auto newColor = pl.color;
						int i = scene->selectedObjectIndex_;
						PushUndo({"PointLight Color",
							[scene, i, ci, prevColor]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].pointLights.size()) scene->objects_[i].pointLights[ci].color = prevColor; },
							[scene, i, ci, newColor]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].pointLights.size()) scene->objects_[i].pointLights[ci].color = newColor; }
						});
					}

					auto prevIntensity = pl.intensity;
					ImGui::DragFloat("Intensity##PL", &pl.intensity, 0.01f, 0.0f, 100.0f);
					if (ImGui::IsItemDeactivatedAfterEdit()) {
						auto newIntensity = pl.intensity;
						int i = scene->selectedObjectIndex_;
						PushUndo({"PointLight Intensity",
							[scene, i, ci, prevIntensity]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].pointLights.size()) scene->objects_[i].pointLights[ci].intensity = prevIntensity; },
							[scene, i, ci, newIntensity]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].pointLights.size()) scene->objects_[i].pointLights[ci].intensity = newIntensity; }
						});
					}

					auto prevRange = pl.range;
					ImGui::DragFloat("Range##PL", &pl.range, 0.1f, 0.0f, 1000.0f);
					if (ImGui::IsItemDeactivatedAfterEdit()) {
						auto newRange = pl.range;
						int i = scene->selectedObjectIndex_;
						PushUndo({"PointLight Range",
							[scene, i, ci, prevRange]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].pointLights.size()) scene->objects_[i].pointLights[ci].range = prevRange; },
							[scene, i, ci, newRange]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].pointLights.size()) scene->objects_[i].pointLights[ci].range = newRange; }
						});
					}

					auto prevAtten = pl.atten;
					ImGui::DragFloat3("Attenuation##PL", &pl.atten.x, 0.001f, 0.0f, 1.0f);
					if (ImGui::IsItemDeactivatedAfterEdit()) {
						auto newAtten = pl.atten;
						int i = scene->selectedObjectIndex_;
						PushUndo({"PointLight Attenuation",
							[scene, i, ci, prevAtten]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].pointLights.size()) scene->objects_[i].pointLights[ci].atten = prevAtten; },
							[scene, i, ci, newAtten]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].pointLights.size()) scene->objects_[i].pointLights[ci].atten = newAtten; }
						});
					}
					if (ImGui::Button("Remove##PL")) {
						obj.pointLights.erase(obj.pointLights.begin() + ci);
						ImGui::TreePop();
						ImGui::PopID();
						goto end_comp;
					}
					ImGui::TreePop();
				}
				ImGui::PopID();
			}

			// 笘・ｿｽ蜉: SpotLight 繧ｳ繝ｳ繝昴・繝阪Φ繝・
			for (size_t ci = 0; ci < obj.spotLights.size(); ++ci) {
				auto& sl = obj.spotLights[ci];
				ImGui::PushID(12000 + (int)ci);
				if (ImGui::TreeNode("SpotLight")) {
					bool prevEnabled = sl.enabled;
					if (ImGui::Checkbox("Enabled##SL", &sl.enabled)) {
						bool newVal = sl.enabled;
						int i = scene->selectedObjectIndex_;
						PushUndo({"SpotLight Enabled",
							[scene, i, ci, prevEnabled]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].spotLights.size()) scene->objects_[i].spotLights[ci].enabled = prevEnabled; },
							[scene, i, ci, newVal]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].spotLights.size()) scene->objects_[i].spotLights[ci].enabled = newVal; }
						});
					}

					auto prevColor = sl.color;
					ImGui::ColorEdit3("Color##SL", &sl.color.x);
					if (ImGui::IsItemDeactivatedAfterEdit()) {
						auto newColor = sl.color;
						int i = scene->selectedObjectIndex_;
						PushUndo({"SpotLight Color",
							[scene, i, ci, prevColor]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].spotLights.size()) scene->objects_[i].spotLights[ci].color = prevColor; },
							[scene, i, ci, newColor]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].spotLights.size()) scene->objects_[i].spotLights[ci].color = newColor; }
						});
					}

					auto prevIntensity = sl.intensity;
					ImGui::DragFloat("Intensity##SL", &sl.intensity, 0.01f, 0.0f, 100.0f);
					if (ImGui::IsItemDeactivatedAfterEdit()) {
						auto newIntensity = sl.intensity;
						int i = scene->selectedObjectIndex_;
						PushUndo({"SpotLight Intensity",
							[scene, i, ci, prevIntensity]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].spotLights.size()) scene->objects_[i].spotLights[ci].intensity = prevIntensity; },
							[scene, i, ci, newIntensity]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].spotLights.size()) scene->objects_[i].spotLights[ci].intensity = newIntensity; }
						});
					}

					auto prevRange = sl.range;
					ImGui::DragFloat("Range##SL", &sl.range, 0.1f, 0.0f, 1000.0f);
					if (ImGui::IsItemDeactivatedAfterEdit()) {
						auto newRange = sl.range;
						int i = scene->selectedObjectIndex_;
						PushUndo({"SpotLight Range",
							[scene, i, ci, prevRange]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].spotLights.size()) scene->objects_[i].spotLights[ci].range = prevRange; },
							[scene, i, ci, newRange]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].spotLights.size()) scene->objects_[i].spotLights[ci].range = newRange; }
						});
					}

					auto prevInnerCos = sl.innerCos;
					ImGui::DragFloat("Inner Cos##SL", &sl.innerCos, 0.01f, -1.0f, 1.0f);
					if (ImGui::IsItemDeactivatedAfterEdit()) {
						auto newInnerCos = sl.innerCos;
						int i = scene->selectedObjectIndex_;
						PushUndo({"SpotLight InnerCos",
							[scene, i, ci, prevInnerCos]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].spotLights.size()) scene->objects_[i].spotLights[ci].innerCos = prevInnerCos; },
							[scene, i, ci, newInnerCos]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].spotLights.size()) scene->objects_[i].spotLights[ci].innerCos = newInnerCos; }
						});
					}

					auto prevOuterCos = sl.outerCos;
					ImGui::DragFloat("Outer Cos##SL", &sl.outerCos, 0.01f, -1.0f, 1.0f);
					if (ImGui::IsItemDeactivatedAfterEdit()) {
						auto newOuterCos = sl.outerCos;
						int i = scene->selectedObjectIndex_;
						PushUndo({"SpotLight OuterCos",
							[scene, i, ci, prevOuterCos]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].spotLights.size()) scene->objects_[i].spotLights[ci].outerCos = prevOuterCos; },
							[scene, i, ci, newOuterCos]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].spotLights.size()) scene->objects_[i].spotLights[ci].outerCos = newOuterCos; }
						});
					}

					auto prevAtten = sl.atten;
					ImGui::DragFloat3("Attenuation##SL", &sl.atten.x, 0.001f, 0.0f, 1.0f);
					if (ImGui::IsItemDeactivatedAfterEdit()) {
						auto newAtten = sl.atten;
						int i = scene->selectedObjectIndex_;
						PushUndo({"SpotLight Attenuation",
							[scene, i, ci, prevAtten]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].spotLights.size()) scene->objects_[i].spotLights[ci].atten = prevAtten; },
							[scene, i, ci, newAtten]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].spotLights.size()) scene->objects_[i].spotLights[ci].atten = newAtten; }
						});
					}
					if (ImGui::Button("Remove##SL")) {
						obj.spotLights.erase(obj.spotLights.begin() + ci);
						ImGui::TreePop();
						ImGui::PopID();
						goto end_comp;
					}
					ImGui::TreePop();
				}
				ImGui::PopID();
			}

			// 笘・ｿｽ蜉: AudioSource 繧ｳ繝ｳ繝昴・繝阪Φ繝・
			for (size_t ci = 0; ci < obj.audioSources.size(); ++ci) {
				auto& as = obj.audioSources[ci];
				ImGui::PushID(13000 + (int)ci);
				if (ImGui::TreeNode("AudioSource")) {
					bool prevEnabled = as.enabled;
					if (ImGui::Checkbox("Enabled##AS", &as.enabled)) {
						bool newVal = as.enabled;
						int i = scene->selectedObjectIndex_;
						PushUndo({"AudioSource Enabled",
							[scene, i, ci, prevEnabled]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].audioSources.size()) scene->objects_[i].audioSources[ci].enabled = prevEnabled; },
							[scene, i, ci, newVal]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].audioSources.size()) scene->objects_[i].audioSources[ci].enabled = newVal; }
						});
					}
					// 繧ｵ繧ｦ繝ｳ繝峨ヵ繧｡繧､繝ｫ繝代せ
					char pathBuf[256];
					strcpy_s(pathBuf, as.soundPath.c_str());
					std::string oldPath = as.soundPath;
					uint32_t oldHandle = as.soundHandle;
					if (ImGui::InputText("Sound Path##AS", pathBuf, sizeof(pathBuf))) {
						as.soundPath = pathBuf;
						// 繝代せ螟画峩譎ゅ↓蜀阪Ο繝ｼ繝・
						if (!as.soundPath.empty()) {
							auto* audio = Engine::Audio::GetInstance();
							if (audio)
								as.soundHandle = audio->Load(as.soundPath);
						}
						std::string newPath = as.soundPath;
						uint32_t newHandle = as.soundHandle;
						int i = scene->selectedObjectIndex_;
						PushUndo({"AudioSource Path",
							[scene, i, ci, oldPath, oldHandle]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].audioSources.size()) { scene->objects_[i].audioSources[ci].soundPath = oldPath; scene->objects_[i].audioSources[ci].soundHandle = oldHandle; } },
							[scene, i, ci, newPath, newHandle]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].audioSources.size()) { scene->objects_[i].audioSources[ci].soundPath = newPath; scene->objects_[i].audioSources[ci].soundHandle = newHandle; } }
						});
					}
					if (ImGui::BeginDragDropTarget()) {
						if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("RESOURCE_PATH")) {
							std::string path((const char*)pl->Data, pl->DataSize - 1);
							if (path.find(".mp3") != std::string::npos || path.find(".wav") != std::string::npos) {
								std::string oldPathDrag = as.soundPath;
								uint32_t oldHandleDrag = as.soundHandle;

								as.soundPath = path;
								auto* audio = Engine::Audio::GetInstance();
								if (audio)
									as.soundHandle = audio->Load(path);
								EditorUI::Log("AudioSource: " + path);

								std::string newPathDrag = as.soundPath;
								uint32_t newHandleDrag = as.soundHandle;
								int i = scene->selectedObjectIndex_;
								PushUndo({"AudioSource File Drop",
									[scene, i, ci, oldPathDrag, oldHandleDrag]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].audioSources.size()) { scene->objects_[i].audioSources[ci].soundPath = oldPathDrag; scene->objects_[i].audioSources[ci].soundHandle = oldHandleDrag; } },
									[scene, i, ci, newPathDrag, newHandleDrag]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].audioSources.size()) { scene->objects_[i].audioSources[ci].soundPath = newPathDrag; scene->objects_[i].audioSources[ci].soundHandle = newHandleDrag; } }
								});
							}
						}
						ImGui::EndDragDropTarget();
					}

					auto prevVol = as.volume;
					if (ImGui::DragFloat("Volume##AS", &as.volume, 0.01f, 0.0f, 1.0f)) {
						if (as.isPlaying && as.voiceHandle) {
							auto* audio = Engine::Audio::GetInstance();
							if (audio)
								audio->SetVolume(as.voiceHandle, as.volume);
						}
					}
					if (ImGui::IsItemDeactivatedAfterEdit()) {
						auto newVol = as.volume;
						int i = scene->selectedObjectIndex_;
						PushUndo({"AudioSource Volume",
							[scene, i, ci, prevVol]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].audioSources.size()) scene->objects_[i].audioSources[ci].volume = prevVol; },
							[scene, i, ci, newVol]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].audioSources.size()) scene->objects_[i].audioSources[ci].volume = newVol; }
						});
					}

					bool prevLoop = as.loop;
					if (ImGui::Checkbox("Loop##AS", &as.loop)) {
						bool newVal = as.loop;
						int i = scene->selectedObjectIndex_;
						PushUndo({"AudioSource Loop",
							[scene, i, ci, prevLoop]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].audioSources.size()) scene->objects_[i].audioSources[ci].loop = prevLoop; },
							[scene, i, ci, newVal]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].audioSources.size()) scene->objects_[i].audioSources[ci].loop = newVal; }
						});
					}
					bool prevPlayOnStart = as.playOnStart;
					if (ImGui::Checkbox("Play On Start##AS", &as.playOnStart)) {
						bool newVal = as.playOnStart;
						int i = scene->selectedObjectIndex_;
						PushUndo({"AudioSource PlayOnStart",
							[scene, i, ci, prevPlayOnStart]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].audioSources.size()) scene->objects_[i].audioSources[ci].playOnStart = prevPlayOnStart; },
							[scene, i, ci, newVal]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].audioSources.size()) scene->objects_[i].audioSources[ci].playOnStart = newVal; }
						});
					}

					bool prevIs3D = as.is3D;
					if (ImGui::Checkbox("3D Sound##AS", &as.is3D)) {
						bool newVal = as.is3D;
						int i = scene->selectedObjectIndex_;
						PushUndo({"AudioSource 3D Sound",
							[scene, i, ci, prevIs3D]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].audioSources.size()) scene->objects_[i].audioSources[ci].is3D = prevIs3D; },
							[scene, i, ci, newVal]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].audioSources.size()) scene->objects_[i].audioSources[ci].is3D = newVal; }
						});
					}
					if (as.is3D) {
						auto prevMaxDist = as.maxDistance;
						ImGui::DragFloat("Max Distance##AS", &as.maxDistance, 0.5f, 0.0f, 500.0f);
						if (ImGui::IsItemDeactivatedAfterEdit()) {
							auto newMaxDist = as.maxDistance;
							int i = scene->selectedObjectIndex_;
							PushUndo({"AudioSource Max Distance",
								[scene, i, ci, prevMaxDist]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].audioSources.size()) scene->objects_[i].audioSources[ci].maxDistance = prevMaxDist; },
								[scene, i, ci, newMaxDist]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].audioSources.size()) scene->objects_[i].audioSources[ci].maxDistance = newMaxDist; }
							});
						}
					}
					// Play/Stop 繝懊ち繝ｳ
					if (as.isPlaying) {
						if (ImGui::Button("Stop##AS")) {
							auto* audio = Engine::Audio::GetInstance();
							if (audio && as.voiceHandle) {
								audio->Stop(as.voiceHandle);
								as.voiceHandle = 0;
							}
							as.isPlaying = false;
						}
						ImGui::SameLine();
						ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "Playing...");
					} else {
						if (ImGui::Button("Play##AS")) {
							auto* audio = Engine::Audio::GetInstance();
							if (audio && as.soundHandle != 0xFFFFFFFF) {
								as.voiceHandle = audio->Play(as.soundHandle, as.loop, as.volume);
								as.isPlaying = true;
							}
						}
					}
					if (ImGui::Button("Remove##AS")) {
						obj.audioSources.erase(obj.audioSources.begin() + ci);
						ImGui::TreePop();
						ImGui::PopID();
						goto end_comp;
					}
					ImGui::TreePop();
				}
				ImGui::PopID();
			}

			// 笘・ｿｽ蜉: AudioListener 繧ｳ繝ｳ繝昴・繝阪Φ繝・
			for (size_t ci = 0; ci < obj.audioListeners.size(); ++ci) {
				auto& al = obj.audioListeners[ci];
				ImGui::PushID(14000 + (int)ci);
				if (ImGui::TreeNode("AudioListener")) {
					bool prevEnabled = al.enabled;
					if (ImGui::Checkbox("Enabled##AL", &al.enabled)) {
						bool newVal = al.enabled;
						int i = scene->selectedObjectIndex_;
						PushUndo({"AudioListener Enabled",
							[scene, i, ci, prevEnabled]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].audioListeners.size()) scene->objects_[i].audioListeners[ci].enabled = prevEnabled; },
							[scene, i, ci, newVal]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].audioListeners.size()) scene->objects_[i].audioListeners[ci].enabled = newVal; }
						});
					}
					ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "This object receives audio.");
					if (ImGui::Button("Remove##AL")) {
						obj.audioListeners.erase(obj.audioListeners.begin() + ci);
						ImGui::TreePop();
						ImGui::PopID();
						goto end_comp;
					}
					ImGui::TreePop();
				}
				ImGui::PopID();
			}

			// 笘・ｿｽ蜉: RectTransform 繧ｳ繝ｳ繝昴・繝阪Φ繝・
			for (size_t ci = 0; ci < obj.rectTransforms.size(); ++ci) {
				auto& rt = obj.rectTransforms[ci];
				ImGui::PushID(14000 + (int)ci);
				if (ImGui::TreeNode("RectTransform")) {
					bool prevEnabled = rt.enabled;
					if (ImGui::Checkbox("Enabled##RT", &rt.enabled)) {
						bool newVal = rt.enabled;
						int i = scene->selectedObjectIndex_;
						PushUndo({"RectTransform Enabled",
							[scene, i, ci, prevEnabled]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].rectTransforms.size()) scene->objects_[i].rectTransforms[ci].enabled = prevEnabled; },
							[scene, i, ci, newVal]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].rectTransforms.size()) scene->objects_[i].rectTransforms[ci].enabled = newVal; }
						});
					}

					auto prevPos = rt.pos;
					ImGui::DragFloat2("Pos##RT", &rt.pos.x, 1.0f);
					if (ImGui::IsItemDeactivatedAfterEdit()) {
						auto newPos = rt.pos;
						int i = scene->selectedObjectIndex_;
						PushUndo({"RectTransform Pos",
							[scene, i, ci, prevPos]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].rectTransforms.size()) scene->objects_[i].rectTransforms[ci].pos = prevPos; },
							[scene, i, ci, newPos]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].rectTransforms.size()) scene->objects_[i].rectTransforms[ci].pos = newPos; }
						});
					}

					auto prevSize = rt.size;
					ImGui::DragFloat2("Size##RT", &rt.size.x, 1.0f);
					if (ImGui::IsItemDeactivatedAfterEdit()) {
						auto newSize = rt.size;
						int i = scene->selectedObjectIndex_;
						PushUndo({"RectTransform Size",
							[scene, i, ci, prevSize]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].rectTransforms.size()) scene->objects_[i].rectTransforms[ci].size = prevSize; },
							[scene, i, ci, newSize]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].rectTransforms.size()) scene->objects_[i].rectTransforms[ci].size = newSize; }
						});
					}

					auto prevAnchor = rt.anchor;
					ImGui::DragFloat2("Anchor##RT", &rt.anchor.x, 0.01f, 0.0f, 1.0f);
					if (ImGui::IsItemDeactivatedAfterEdit()) {
						auto newAnchor = rt.anchor;
						int i = scene->selectedObjectIndex_;
						PushUndo({"RectTransform Anchor",
							[scene, i, ci, prevAnchor]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].rectTransforms.size()) scene->objects_[i].rectTransforms[ci].anchor = prevAnchor; },
							[scene, i, ci, newAnchor]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].rectTransforms.size()) scene->objects_[i].rectTransforms[ci].anchor = newAnchor; }
						});
					}

					auto prevPivot = rt.pivot;
					ImGui::DragFloat2("Pivot##RT", &rt.pivot.x, 0.01f, 0.0f, 1.0f);
					if (ImGui::IsItemDeactivatedAfterEdit()) {
						auto newPivot = rt.pivot;
						int i = scene->selectedObjectIndex_;
						PushUndo({"RectTransform Pivot",
							[scene, i, ci, prevPivot]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].rectTransforms.size()) scene->objects_[i].rectTransforms[ci].pivot = prevPivot; },
							[scene, i, ci, newPivot]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].rectTransforms.size()) scene->objects_[i].rectTransforms[ci].pivot = newPivot; }
						});
					}

					auto prevRot = rt.rotation;
					ImGui::DragFloat("Rotation##RT", &rt.rotation, 0.5f);
					if (ImGui::IsItemDeactivatedAfterEdit()) {
						auto newRot = rt.rotation;
						int i = scene->selectedObjectIndex_;
						PushUndo({"RectTransform Rotation",
							[scene, i, ci, prevRot]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].rectTransforms.size()) scene->objects_[i].rectTransforms[ci].rotation = prevRot; },
							[scene, i, ci, newRot]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].rectTransforms.size()) scene->objects_[i].rectTransforms[ci].rotation = newRot; }
						});
					}

					if (ImGui::Button("Remove##RT")) {
						obj.rectTransforms.erase(obj.rectTransforms.begin() + ci);
						ImGui::TreePop();
						ImGui::PopID();
						goto end_comp;
					}
					ImGui::TreePop();
				}
				ImGui::PopID();
			}

			// 笘・ｿｽ蜉: UIImage 繧ｳ繝ｳ繝昴・繝阪Φ繝・
			for (size_t ci = 0; ci < obj.images.size(); ++ci) {
				auto& img = obj.images[ci];
				auto* renderer = scene->GetRenderer(); // 笘・ｿｽ蜉
				ImGui::PushID(15000 + (int)ci);
				if (ImGui::TreeNode("UIImage")) {
					bool prevEnabled = img.enabled;
					if (ImGui::Checkbox("Enabled##Img", &img.enabled)) {
						bool newVal = img.enabled;
						int i = scene->selectedObjectIndex_;
						PushUndo({"UIImage Enabled",
							[scene, i, ci, prevEnabled]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].images.size()) scene->objects_[i].images[ci].enabled = prevEnabled; },
							[scene, i, ci, newVal]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].images.size()) scene->objects_[i].images[ci].enabled = newVal; }
						});
					}

					auto prevColor = img.color;
					ImGui::ColorEdit4("Color##Img", &img.color.x);
					if (ImGui::IsItemDeactivatedAfterEdit()) {
						auto newColor = img.color;
						int i = scene->selectedObjectIndex_;
						PushUndo({"UIImage Color",
							[scene, i, ci, prevColor]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].images.size()) scene->objects_[i].images[ci].color = prevColor; },
							[scene, i, ci, newColor]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].images.size()) scene->objects_[i].images[ci].color = newColor; }
						});
					}
					// 繝・け繧ｹ繝√Ε
					char pathBuf[256];
					strcpy_s(pathBuf, img.texturePath.c_str());
					std::string oldPath = img.texturePath;
					uint32_t oldHandle = img.textureHandle;
					if (ImGui::InputText("Texture##Img", pathBuf, sizeof(pathBuf))) {
						img.texturePath = pathBuf;
						if (renderer && !img.texturePath.empty())
							img.textureHandle = renderer->LoadTexture2D(img.texturePath);
						std::string newPath = img.texturePath;
						uint32_t newHandle = img.textureHandle;
						int i = scene->selectedObjectIndex_;
						PushUndo({"UIImage Texture",
							[scene, i, ci, oldPath, oldHandle]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].images.size()) { scene->objects_[i].images[ci].texturePath = oldPath; scene->objects_[i].images[ci].textureHandle = oldHandle; } },
							[scene, i, ci, newPath, newHandle]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].images.size()) { scene->objects_[i].images[ci].texturePath = newPath; scene->objects_[i].images[ci].textureHandle = newHandle; } }
						});
					}
					if (ImGui::BeginDragDropTarget()) {
						if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("RESOURCE_PATH")) {
							std::string path((const char*)pl->Data, pl->DataSize - 1);
							if (path.find(".png") != std::string::npos || path.find(".jpg") != std::string::npos) {
								std::string oldPathDrag = img.texturePath;
								uint32_t oldHandleDrag = img.textureHandle;
								img.texturePath = path;
								img.textureHandle = renderer->LoadTexture2D(path);
								std::string newPathDrag = img.texturePath;
								uint32_t newHandleDrag = img.textureHandle;
								int i = scene->selectedObjectIndex_;
								PushUndo({"UIImage Texture Drop",
									[scene, i, ci, oldPathDrag, oldHandleDrag]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].images.size()) { scene->objects_[i].images[ci].texturePath = oldPathDrag; scene->objects_[i].images[ci].textureHandle = oldHandleDrag; } },
									[scene, i, ci, newPathDrag, newHandleDrag]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].images.size()) { scene->objects_[i].images[ci].texturePath = newPathDrag; scene->objects_[i].images[ci].textureHandle = newHandleDrag; } }
								});
							}
						}
						ImGui::EndDragDropTarget();
					}
					if (img.textureHandle != 0) {
						ImGui::Image((ImTextureID)renderer->GetTextureSrvGpu(img.textureHandle).ptr, ImVec2(64, 64));
					}

					bool prev9 = img.is9Slice;
					if (ImGui::Checkbox("9-Slice##Img", &img.is9Slice)) {
						bool newVal = img.is9Slice;
						int i = scene->selectedObjectIndex_;
						PushUndo({"UIImage 9-Slice",
							[scene, i, ci, prev9]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].images.size()) scene->objects_[i].images[ci].is9Slice = prev9; },
							[scene, i, ci, newVal]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].images.size()) scene->objects_[i].images[ci].is9Slice = newVal; }
						});
					}
					if (img.is9Slice) {
						auto bT = img.borderTop, bB = img.borderBottom, bL = img.borderLeft, bR = img.borderRight;
						ImGui::DragFloat4("Borders (T,B,L,R)##Img", &img.borderTop, 1.0f, 0.0f, 256.0f);
						if (ImGui::IsItemDeactivatedAfterEdit()) {
							auto nT = img.borderTop, nB = img.borderBottom, nL = img.borderLeft, nR = img.borderRight;
							int i = scene->selectedObjectIndex_;
							PushUndo({"UIImage Borders",
								[scene, i, ci, bT, bB, bL, bR]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].images.size()) { auto& img2 = scene->objects_[i].images[ci]; img2.borderTop = bT; img2.borderBottom = bB; img2.borderLeft = bL; img2.borderRight = bR; } },
								[scene, i, ci, nT, nB, nL, nR]() { if (i < (int)scene->objects_.size() && ci < scene->objects_[i].images.size()) { auto& img2 = scene->objects_[i].images[ci]; img2.borderTop = nT; img2.borderBottom = nB; img2.borderLeft = nL; img2.borderRight = nR; } }
							});
						}
					}
					if (ImGui::Button("Remove##Img")) {
						obj.images.erase(obj.images.begin() + ci);
						ImGui::TreePop();
						ImGui::PopID();
						goto end_comp;
					}
					ImGui::TreePop();
				}
				ImGui::PopID();
			}

			// 笘・ｿｽ蜉: UIButton 繧ｳ繝ｳ繝昴・繝阪Φ繝・
			for (size_t ci = 0; ci < obj.buttons.size(); ++ci) {
				auto& btn = obj.buttons[ci];
				ImGui::PushID(16000 + (int)ci);
				if (ImGui::TreeNode("UIButton")) {
					ImGui::Checkbox("Enabled##Btn", &btn.enabled);
					ImGui::ColorEdit4("Normal Color##Btn", &btn.normalColor.x);
					ImGui::ColorEdit4("Hover Color##Btn", &btn.hoverColor.x);
					ImGui::ColorEdit4("Pressed Color##Btn", &btn.pressedColor.x);
					ImGui::Text("State: %s", btn.isPressed ? "Pressed" : (btn.isHovered ? "Hovered" : "Normal"));
					if (ImGui::Button("Remove##Btn")) {
						obj.buttons.erase(obj.buttons.begin() + ci);
						ImGui::TreePop();
						ImGui::PopID();
						goto end_comp;
					}
					ImGui::TreePop();
				}
				ImGui::PopID();
			}

			// 笘・ｿｽ蜉: UIText 繧ｳ繝ｳ繝昴・繝阪Φ繝・
			for (size_t ci = 0; ci < obj.texts.size(); ++ci) {
				auto& txt = obj.texts[ci];
				ImGui::PushID(17000 + (int)ci);
				if (ImGui::TreeNode("UIText")) {
					ImGui::Checkbox("Enabled##Txt", &txt.enabled);
					char txtBuf[1024];
					strcpy_s(txtBuf, txt.text.c_str());
					if (ImGui::InputTextMultiline("Text##Txt", txtBuf, sizeof(txtBuf))) {
						txt.text = txtBuf;
					}
					ImGui::DragFloat("Font Size##Txt", &txt.fontSize, 0.5f, 1.0f, 256.0f);
					ImGui::ColorEdit4("Color##Txt", &txt.color.x);
					if (ImGui::Button("Remove##Txt")) {
						obj.texts.erase(obj.texts.begin() + ci);
						ImGui::TreePop();
						ImGui::PopID();
						goto end_comp;
					}
					ImGui::TreePop();
				}
				ImGui::PopID();
			}

			// 笘・ｿｽ蜉: Hitbox 繧ｳ繝ｳ繝昴・繝阪Φ繝・(謾ｻ謦・愛螳・
			for (size_t ci = 0; ci < obj.hitboxes.size(); ++ci) {
				auto& hb = obj.hitboxes[ci];
				ImGui::PushID(15000 + (int)ci);
				if (ImGui::TreeNode("Hitbox (Attack)")) {
					ImGui::Checkbox("Enabled##HB", &hb.enabled);
					ImGui::Checkbox("Active##HB", &hb.isActive);
					if (hb.isActive)
						ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "[ACTIVE]");
					else
						ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "[Inactive]");
					ImGui::DragFloat3("Center##HB", &hb.center.x, 0.1f);
					ImGui::DragFloat3("Size##HB", &hb.size.x, 0.1f, 0.01f, 100.0f);
					ImGui::DragFloat("Damage##HB", &hb.damage, 0.1f, 0.0f, 9999.0f);
					char tagBuf[128];
					strcpy_s(tagBuf, hb.tag.c_str());
					if (ImGui::InputText("Tag##HB", tagBuf, sizeof(tagBuf)))
						hb.tag = tagBuf;
					if (ImGui::Button("Remove##HB")) {
						obj.hitboxes.erase(obj.hitboxes.begin() + ci);
						ImGui::TreePop();
						ImGui::PopID();
						goto end_comp;
					}
					ImGui::TreePop();
				}
				ImGui::PopID();
			}

			// 笘・ｿｽ蜉: Hurtbox 繧ｳ繝ｳ繝昴・繝阪Φ繝・(鬟溘ｉ縺・愛螳・
			for (size_t ci = 0; ci < obj.hurtboxes.size(); ++ci) {
				auto& hb = obj.hurtboxes[ci];
				ImGui::PushID(16000 + (int)ci);
				if (ImGui::TreeNode("Hurtbox (Damage)")) {
					ImGui::Checkbox("Enabled##HU", &hb.enabled);
					ImGui::DragFloat3("Center##HU", &hb.center.x, 0.1f);
					ImGui::DragFloat3("Size##HU", &hb.size.x, 0.1f, 0.01f, 100.0f);
					char tagBuf[128];
					strcpy_s(tagBuf, hb.tag.c_str());
					if (ImGui::InputText("Tag##HU", tagBuf, sizeof(tagBuf)))
						hb.tag = tagBuf;
					ImGui::DragFloat("Damage Multiplier##HU", &hb.damageMultiplier, 0.01f, 0.0f, 10.0f);
					if (ImGui::Button("Remove##HU")) {
						obj.hurtboxes.erase(obj.hurtboxes.begin() + ci);
						ImGui::TreePop();
						ImGui::PopID();
						goto end_comp;
					}
					ImGui::TreePop();
				}
				ImGui::PopID();
			}

			// 笘・ｿｽ蜉: Health 繧ｳ繝ｳ繝昴・繝阪Φ繝・(繧ｹ繝・・繧ｿ繧ｹ邂｡逅・
			for (size_t ci = 0; ci < obj.healths.size(); ++ci) {
				auto& hc = obj.healths[ci];
				ImGui::PushID(17000 + (int)ci);
				if (ImGui::TreeNode("Health (Status)")) {
					ImGui::Checkbox("Enabled##HC", &hc.enabled);
					ImGui::DragFloat("HP##HC", &hc.hp, 1.0f, 0.0f, hc.maxHp);
					ImGui::DragFloat("Max HP##HC", &hc.maxHp, 1.0f, 1.0f, 9999.0f);
					ImGui::DragFloat("Stamina##HC", &hc.stamina, 1.0f, 0.0f, hc.maxStamina);
					ImGui::DragFloat("Max Stamina##HC", &hc.maxStamina, 1.0f, 1.0f, 9999.0f);
					ImGui::DragFloat("Invincible Time##HC", &hc.invincibleTime, 0.1f, 0.0f, 10.0f);
					ImGui::Checkbox("Is Dead##HC", &hc.isDead);
					if (ImGui::Button("Remove##HC")) {
						obj.healths.erase(obj.healths.begin() + ci);
						ImGui::TreePop();
						ImGui::PopID();
						goto end_comp;
					}
					ImGui::TreePop();
				}
				ImGui::PopID();
			}

			// 笘・ｿｽ蜉: Script 繧ｳ繝ｳ繝昴・繝阪Φ繝・(繝・く繧ｹ繝医せ繧ｯ繝ｪ繝励ヨ)
			for (size_t ci = 0; ci < obj.scripts.size(); ++ci) {
				auto& sc = obj.scripts[ci];
				ImGui::PushID(18000 + (int)ci);
				if (ImGui::TreeNode("Script")) {
					ImGui::Checkbox("Enabled##SC", &sc.enabled);
					char pathBuf[256];
					strcpy_s(pathBuf, sc.scriptPath.c_str());
					if (ImGui::InputText("Class Name##SC", pathBuf, sizeof(pathBuf)))
						sc.scriptPath = pathBuf;
					ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "(e.g., PlayerScript)");

					// ==========================================
					// 笘・ｿｽ蜉: VS Code繧帝幕縺上・繧ｿ繝ｳ
					// ==========================================
					if (ImGui::Button("Open in VS Code")) {
						// code 繧ｳ繝槭Φ繝峨〒繝ｯ繝ｼ繧ｯ繧ｹ繝壹・繧ｹ縺ｨ蟇ｾ雎｡繝輔ぃ繧､繝ｫ繧帝幕縺・
						std::string cmd = "code . " + sc.scriptPath + ".cpp " + sc.scriptPath + ".h";
						system(cmd.c_str());
					}

					if (ImGui::Button("Remove##SC")) {
						obj.scripts.erase(obj.scripts.begin() + ci);
						ImGui::TreePop();
						ImGui::PopID();
						goto end_comp;
					}
					ImGui::TreePop();
				}
				ImGui::PopID();
			}

		end_comp:
			if (ImGui::Button("Add Component"))
				ImGui::OpenPopup("AddComp");
			if (ImGui::BeginPopup("AddComp")) {
				if (ImGui::MenuItem("MeshRenderer")) {
					MeshRendererComponent mr;
					obj.meshRenderers.push_back(mr);
				}
				if (ImGui::MenuItem("BoxCollider")) {
					obj.boxColliders.push_back({});
				}
				if (ImGui::MenuItem("Tag")) {
					obj.tags.push_back({});
				}
				if (ImGui::MenuItem("Animator")) {
					obj.animators.push_back({});
				}
				if (ImGui::MenuItem("Rigidbody")) {
					obj.rigidbodies.push_back({});
				}
				if (ImGui::MenuItem("ParticleEmitter")) { // 笘・ｿｽ蜉
					ParticleEmitterComponent pe;
					pe.emitter.Initialize(*Engine::Renderer::GetInstance(), "NewEmitter");
					obj.particleEmitters.push_back(pe);
				}
				if (ImGui::MenuItem("GpuMeshCollider")) { // 笘・ｿｽ蜉
					GpuMeshColliderComponent gmc;
					gmc.meshHandle = obj.modelHandle; // 繝・ヵ繧ｩ繝ｫ繝医〒繧ｪ繝悶ず繧ｧ繧ｯ繝医・繝｡繝・す繝･繧剃ｽｿ逕ｨ
					gmc.meshPath = obj.modelPath;
					obj.gpuMeshColliders.push_back(gmc);
				}
				if (ImGui::MenuItem("PlayerInput")) {
					obj.playerInputs.push_back({});
				} // 笘・ｿｽ蜉
				if (ImGui::MenuItem("CharacterMovement")) {
					obj.characterMovements.push_back({});
				} // 笘・ｿｽ蜉
				if (ImGui::MenuItem("CameraTarget")) {
					obj.cameraTargets.push_back({});
				} // 笘・ｿｽ蜉
				ImGui::Separator();
				if (ImGui::MenuItem("DirectionalLight")) {
					obj.directionalLights.push_back({});
				} // 笘・ｿｽ蜉
				if (ImGui::MenuItem("PointLight")) {
					obj.pointLights.push_back({});
				} // 笘・ｿｽ蜉
				if (ImGui::MenuItem("SpotLight")) {
					obj.spotLights.push_back({});
				} // 笘・ｿｽ蜉
				ImGui::Separator();
				if (ImGui::MenuItem("AudioSource")) {
					obj.audioSources.push_back({});
				} // 笘・ｿｽ蜉
				if (ImGui::MenuItem("AudioListener")) {
					obj.audioListeners.push_back({});
				} // 笘・ｿｽ蜉
				ImGui::Separator();
				if (ImGui::MenuItem("Hitbox")) {
					obj.hitboxes.push_back({});
				} // 笘・ｿｽ蜉
				if (ImGui::MenuItem("Hurtbox")) {
					obj.hurtboxes.push_back({});
				} // 笘・ｿｽ蜉
				ImGui::Separator();
				if (ImGui::MenuItem("RectTransform")) {
					obj.rectTransforms.push_back({});
				}
				if (ImGui::MenuItem("UIImage")) {
					obj.images.push_back({});
				}
				if (ImGui::MenuItem("UIButton")) {
					obj.buttons.push_back({});
				}
				if (ImGui::MenuItem("UIText")) {
					obj.texts.push_back({});
				}
				ImGui::Separator();
				if (ImGui::MenuItem("Health")) {
					obj.healths.push_back({});
				} // 笘・ｿｽ蜉
				ImGui::Separator();
				if (ImGui::MenuItem("Script")) {
					obj.scripts.push_back({});
				} // 笘・ｿｽ蜉
				ImGui::EndPopup();
			}
		}
		if (obj.locked || scene->IsPlaying())
			ImGui::EndDisabled();
		ImGui::Separator();
		const char* mn[] = {"Translate (T)", "Rotate (R)", "Scale (S)"};
		ImGui::Text("Gizmo: %s", mn[(int)currentGizmoMode]);
		if (scene->selectedIndices_.size() > 1)
			ImGui::Text("(%d selected)", (int)scene->selectedIndices_.size());
	} else
		ImGui::Text("No Object Selected");
	ImGui::End();
} // ShowInspector

// ====== Project ======
void EditorUI::ShowProject(Engine::Renderer* renderer, GameScene* scene) {
	(void)scene;
	ImGui::Begin("Project");

	// 笘・髱咏噪螟画焚: 繝輔か繝ｫ繝繝翫ン繧ｲ繝ｼ繧ｷ繝ｧ繝ｳ繝ｻ繧ｭ繝｣繝・す繝･繝ｻ髻ｳ螢ｰ蜀咲函
	static std::string currentDir = "Resources";
	static std::map<std::string, Engine::Renderer::TextureHandle> thumbnailCache;
	static float iconSize = 80.0f;
	static uint32_t playingSoundHandle = 0xFFFFFFFF;
	static size_t playingVoiceHandle = 0;
	static std::string playingAudioPath;

	// 笘・繝輔ぃ繧､繝ｫ謫堺ｽ懃畑縺ｮ迥ｶ諷句､画焚
	static bool renaming = false;
	static std::string renamingPath; // 蜷榊燕螟画峩蟇ｾ雎｡縺ｮ繝輔Ν繝代せ
	static char renameBuffer[256] = {};
	static bool showDeleteConfirm = false;
	static std::string deletingPath; // 蜑企勁蟇ｾ雎｡縺ｮ繝輔Ν繝代せ
	static std::string deletingName; // 蜑企勁蟇ｾ雎｡縺ｮ陦ｨ遉ｺ蜷・
	static bool creatingFolder = false;
	static char newFolderNameBuf[256] = {};

	// 笘・霑ｽ蜉: 繧ｹ繧ｯ繝ｪ繝励ヨ菴懈・逕ｨ縺ｮ迥ｶ諷句､画焚
	static bool creatingScript = false;
	static char newScriptNameBuf[256] = "NewScript";

	if (!fs::exists(currentDir))
		currentDir = "Resources";

	// --- 繝代Φ縺上★繝ｪ繧ｹ繝・---
	{
		std::string accumulated;
		std::string remaining = currentDir;
		// "Resources" 繧偵Ν繝ｼ繝医→縺励※蛻・牡陦ｨ遉ｺ
		std::istringstream iss(remaining);
		std::string token;
		bool first = true;
		while (std::getline(iss, token, '\\')) {
			// '/' 縺ｧ繧ょ・蜑ｲ
			std::istringstream iss2(token);
			std::string t2;
			while (std::getline(iss2, t2, '/')) {
				if (t2.empty())
					continue;
				if (!first) {
					accumulated += "/";
					ImGui::SameLine(0, 2);
					ImGui::TextUnformatted(">");
					ImGui::SameLine(0, 2);
				}
				accumulated += t2;
				if (ImGui::SmallButton((t2 + "##bc" + accumulated).c_str())) {
					currentDir = accumulated;
				}
				first = false;
			}
		}
	}

	// 笘・繝・・繝ｫ繝舌・: + 繝懊ち繝ｳ (譁ｰ隕上ヵ繧ｩ繝ｫ繝菴懈・)
	ImGui::SameLine(0, 8);
	if (ImGui::SmallButton("+##createFolder")) {
		creatingFolder = true;
		memset(newFolderNameBuf, 0, sizeof(newFolderNameBuf));
		strncpy_s(newFolderNameBuf, "NewFolder", sizeof(newFolderNameBuf) - 1);
	}
	if (ImGui::IsItemHovered()) {
		ImGui::BeginTooltip();
		ImGui::Text("Create Folder");
		ImGui::EndTooltip();
	}

	ImGui::SameLine(ImGui::GetWindowWidth() - 160);
	ImGui::PushItemWidth(100);
	ImGui::SliderFloat("##iconSz", &iconSize, 48.0f, 128.0f, "%.0f");
	ImGui::PopItemWidth();
	ImGui::SameLine();
	ImGui::Text("Size");

	ImGui::Separator();

	// --- 笘・譁ｰ隕上ヵ繧ｩ繝ｫ繝菴懈・繝繧､繧｢繝ｭ繧ｰ ---
	if (creatingFolder) {
		ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.15f, 0.15f, 0.2f, 1.0f));
		ImGui::BeginChild("##createFolderPanel", ImVec2(0, 32), true);
		ImGui::Text("Folder Name:");
		ImGui::SameLine();
		ImGui::PushItemWidth(200);
		bool enterPressed = ImGui::InputText("##newFolderName", newFolderNameBuf, sizeof(newFolderNameBuf), ImGuiInputTextFlags_EnterReturnsTrue);
		ImGui::PopItemWidth();
		ImGui::SameLine();
		if (ImGui::SmallButton("OK") || enterPressed) {
			std::string newPath = currentDir + "/" + std::string(newFolderNameBuf);
			if (strlen(newFolderNameBuf) > 0 && !fs::exists(newPath)) {
				fs::create_directories(newPath);
				Log("Folder created: " + newPath);
			} else if (fs::exists(newPath)) {
				LogWarning("Folder already exists: " + newPath);
			}
			creatingFolder = false;
		}
		ImGui::SameLine();
		if (ImGui::SmallButton("Cancel")) {
			creatingFolder = false;
		}
		ImGui::EndChild();
		ImGui::PopStyleColor();
	}

	// --- 笘・C++繧ｹ繧ｯ繝ｪ繝励ヨ菴懈・繝繧､繧｢繝ｭ繧ｰ ---
	if (creatingScript) {
		ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.20f, 0.12f, 0.25f, 1.0f));
		ImGui::BeginChild("##createScriptPanel", ImVec2(0, 32), true);
		ImGui::Text("Script Name:");
		ImGui::SameLine();
		ImGui::PushItemWidth(200);
		bool enterPressedS = ImGui::InputText("##newScriptName", newScriptNameBuf, sizeof(newScriptNameBuf), ImGuiInputTextFlags_EnterReturnsTrue);
		ImGui::PopItemWidth();
		ImGui::SameLine();
		if (ImGui::SmallButton("Create##scr") || enterPressedS) {
			std::string className(newScriptNameBuf);
			if (!className.empty()) {
				// Game/ 繝輔か繝ｫ繝縺ｫ繝倥ャ繝繝ｼ縺ｨ繧ｽ繝ｼ繧ｹ繧堤函謌・
				// 笘・ｿｮ豁｣: 螳溯｡後ョ繧｣繝ｬ繧ｯ繝医Μ(x64/Debug遲・縺ｫGame繝輔か繝ｫ繝縺後↑縺・ｴ蜷医∬ｦｪ繝・ぅ繝ｬ繧ｯ繝医Μ繧呈爾邏｢縺励※豁｣縺励＞繝代せ繧堤音螳壹☆繧・
				std::string gameDir = "Game";
				if (!fs::exists(gameDir)) {
					if (fs::exists("../../Game")) gameDir = "../../Game";
					else if (fs::exists("../Game")) gameDir = "../Game";
				}
				if (!fs::exists(gameDir)) fs::create_directories(gameDir);

				std::string hPath = gameDir + "/" + className + ".h";
				std::string cppPath = gameDir + "/" + className + ".cpp";

				if (!fs::exists(hPath) && !fs::exists(cppPath)) {
						// 繝倥ャ繝繝ｼ繝輔ぃ繧､繝ｫ
						{
							std::ofstream f(hPath);
							if (f.is_open()) {
							f << "#pragma once\n";
							f << "#include \"IScript.h\"\n\n";
							f << "namespace Game {\n\n";
							f << "class " << className << " : public IScript {\n";
							f << "public:\n";
							f << "\t// 蛻晄悄蛹門・逅・ｼ医す繝ｼ繝ｳ髢句ｧ区凾縺ｫ1蝗槫他縺ｰ繧後ｋ・噂n";
							f << "\tvoid Start(SceneObject& obj, GameScene* scene) override;\n\n";
							f << "\t// 豈弱ヵ繝ｬ繝ｼ繝蜃ｦ逅・n";
							f << "\tvoid Update(SceneObject& obj, GameScene* scene, float dt) override;\n\n";
							f << "\t// 繧ｪ繝悶ず繧ｧ繧ｯ繝育ｴ譽・凾縺ｮ蜃ｦ逅・n";
							f << "\tvoid OnDestroy(SceneObject& obj, GameScene* scene) override;\n";
							f << "};\n\n";
							f << "} // namespace Game\n";
							f.close();
							} else {
								LogError("Failed to write header: " + hPath);
							}
						}
						// 繧ｽ繝ｼ繧ｹ繝輔ぃ繧､繝ｫ
						{
							std::ofstream f(cppPath);
							if (f.is_open()) {
							f << "#include \"" << className << ".h\"\n";
							f << "#include \"ObjectTypes.h\"\n";
							f << "#include \"Scenes/GameScene.h\"\n";
							f << "#include \"ScriptEngine.h\"\n\n";
							f << "namespace Game {\n\n";
							f << "void " << className << "::Start(SceneObject& /*obj*/, GameScene* /*scene*/) {\n";
							f << "\t// 縺薙％縺ｫ蛻晄悄險ｭ螳壹ｒ險倩ｿｰ\n";
							f << "}\n\n";
							f << "void " << className << "::Update(SceneObject& obj, GameScene* scene, float dt) {\n";
							f << "\t// 縺薙％縺ｫ豈弱ヵ繝ｬ繝ｼ繝縺ｮ謖吝虚繧定ｨ倩ｿｰ\n";
							f << "}\n\n";
							f << "void " << className << "::OnDestroy(SceneObject& /*obj*/, GameScene* /*scene*/) {\n";
							f << "\t// 邨ゆｺ・凾縺ｮ繧ｯ繝ｪ繝ｼ繝ｳ繧｢繝・・縺ｪ縺ｩ繧定ｨ倩ｿｰ\n";
							f << "}\n\n";
							f << "// 笘・繧ｹ繧ｯ繝ｪ繝励ヨ閾ｪ蜍慕匳骭ｲ\n";
							f << "REGISTER_SCRIPT(" << className << ");\n\n";
							f << "} // namespace Game\n";
							f.close();
							} else {
								LogError("Failed to write source: " + cppPath);
							}
						}
					Log("Script created: " + hPath + " / " + cppPath);
					// VS Code縺ｧ髢九￥
					std::string cmd = "code . " + hPath + " " + cppPath;
					system(cmd.c_str());
				} else {
					LogWarning("Script already exists: " + className);
				}
			}
			creatingScript = false;
		}
		ImGui::SameLine();
		if (ImGui::SmallButton("Cancel##scr")) {
			creatingScript = false;
		}
		ImGui::EndChild();
		ImGui::PopStyleColor();
	}

	// --- 笘・蜷榊燕螟画峩繧､繝ｳ繝ｩ繧､繝ｳUI ---
	if (renaming) {
		ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.15f, 0.2f, 0.15f, 1.0f));
		ImGui::BeginChild("##renamePanel", ImVec2(0, 32), true);
		ImGui::Text("Rename:");
		ImGui::SameLine();
		ImGui::PushItemWidth(250);
		bool enterPressed = ImGui::InputText("##renameInput", renameBuffer, sizeof(renameBuffer), ImGuiInputTextFlags_EnterReturnsTrue);
		ImGui::PopItemWidth();
		ImGui::SameLine();
		if (ImGui::SmallButton("OK##ren") || enterPressed) {
			std::string newName(renameBuffer);
			if (!newName.empty() && newName != fs::path(renamingPath).filename().string()) {
				std::string newPath = (fs::path(renamingPath).parent_path() / newName).string();
				if (!fs::exists(newPath)) {
					std::error_code ec;
					fs::rename(renamingPath, newPath, ec);
					if (!ec) {
						Log("Renamed: " + renamingPath + " -> " + newPath);
						// 繧ｵ繝繝阪う繝ｫ繧ｭ繝｣繝・す繝･縺ｮ譖ｴ譁ｰ
						auto it = thumbnailCache.find(renamingPath);
						if (it != thumbnailCache.end()) {
							auto handle = it->second;
							thumbnailCache.erase(it);
							thumbnailCache[newPath] = handle;
						}
					} else {
						LogError("Rename failed: " + ec.message());
					}
				} else {
					LogWarning("A file with that name already exists: " + newPath);
				}
			}
			renaming = false;
		}
		ImGui::SameLine();
		if (ImGui::SmallButton("Cancel##ren")) {
			renaming = false;
		}
		ImGui::EndChild();
		ImGui::PopStyleColor();
	}

	// --- 笘・蜑企勁遒ｺ隱阪ム繧､繧｢繝ｭ繧ｰ ---
	if (showDeleteConfirm) {
		ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.25f, 0.1f, 0.1f, 1.0f));
		ImGui::BeginChild("##deleteConfirm", ImVec2(0, 36), true);
		ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Delete \"%s\"?", deletingName.c_str());
		ImGui::SameLine();
		if (ImGui::SmallButton("Yes##del")) {
			std::error_code ec;
			if (fs::is_directory(deletingPath)) {
				fs::remove_all(deletingPath, ec);
			} else {
				fs::remove(deletingPath, ec);
			}
			if (!ec) {
				Log("Deleted: " + deletingPath);
				// 繧ｵ繝繝阪う繝ｫ繧ｭ繝｣繝・す繝･縺ｮ繧ｯ繝ｪ繧｢
				thumbnailCache.erase(deletingPath);
			} else {
				LogError("Delete failed: " + ec.message());
			}
			showDeleteConfirm = false;
		}
		ImGui::SameLine();
		if (ImGui::SmallButton("No##del")) {
			showDeleteConfirm = false;
		}
		ImGui::EndChild();
		ImGui::PopStyleColor();
	}

	// --- 繝輔ぃ繧､繝ｫ荳隕ｧ繧貞庶髮・---
	struct ProjectEntry {
		std::string path; // 繝輔Ν繝代せ
		std::string name; // 繝輔ぃ繧､繝ｫ蜷阪・縺ｿ
		bool isDir = false;
		std::string ext; // 蟆乗枚蟄玲僑蠑ｵ蟄・
	};
	std::vector<ProjectEntry> entries;

	if (fs::exists(currentDir) && fs::is_directory(currentDir)) {
		for (const auto& e : fs::directory_iterator(currentDir)) {
			ProjectEntry pe;
			pe.path = e.path().string();
			pe.name = e.path().filename().string();
			pe.isDir = e.is_directory();
			pe.ext = "";
			if (!pe.isDir) {
				pe.ext = e.path().extension().string();
				// 蟆乗枚蟄怜喧
				for (auto& c : pe.ext)
					c = (char)std::tolower((unsigned char)c);
			}
			entries.push_back(pe);
		}
	}

	// 繧ｽ繝ｼ繝・ 繝輔か繝ｫ繝蜈医√ヵ繧｡繧､繝ｫ蠕・
	std::sort(entries.begin(), entries.end(), [](const ProjectEntry& a, const ProjectEntry& b) {
		if (a.isDir != b.isDir)
			return a.isDir > b.isDir;
		return a.name < b.name;
	});

	// --- 縲・.縲堺ｸ贋ｽ阪ヵ繧ｩ繝ｫ繝繝懊ち繝ｳ ---
	if (currentDir != "Resources") {
		auto parent = fs::path(currentDir).parent_path().string();
		if (parent.empty())
			parent = "Resources";
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.25f, 0.3f, 1.0f));
		if (ImGui::Button(".. (Up)", ImVec2(iconSize, 30))) {
			currentDir = parent;
		}
		ImGui::PopStyleColor();
		ImGui::SameLine();
	}

	// --- 繧｢繧､繧ｳ繝ｳ繧ｰ繝ｪ繝・ラ ---
	float panelWidth = ImGui::GetContentRegionAvail().x;
	float cellWidth = iconSize + 12.0f;
	int columns = (int)(panelWidth / cellWidth);
	if (columns < 1)
		columns = 1;
	int col = (currentDir != "Resources") ? 1 : 0; // 縲・.縲阪・繧ｿ繝ｳ縺ｮ蛻・

	for (size_t ei = 0; ei < entries.size(); ++ei) {
		auto& pe = entries[ei];
		ImGui::PushID((int)ei);

		bool isTexture = (pe.ext == ".png" || pe.ext == ".jpg" || pe.ext == ".jpeg" || pe.ext == ".bmp");
		bool isModel = (pe.ext == ".obj" || pe.ext == ".gltf" || pe.ext == ".fbx");
		bool isAudio = (pe.ext == ".mp3" || pe.ext == ".wav" || pe.ext == ".ogg");
		bool isPrefab = (pe.ext == ".prefab");
		bool isScript = (pe.ext == ".cpp" || pe.ext == ".h"); // 笘・ｿｽ蜉

		// 繧ｰ繝ｪ繝・ラ繝ｬ繧､繧｢繧ｦ繝・
		if (col > 0 && col < columns)
			ImGui::SameLine();
		else if (col >= columns)
			col = 0;

		ImGui::BeginGroup();

		if (pe.isDir) {
			// 笘・繝輔か繝ｫ繝: 鮟・牡縺｣縺ｽ縺・・繧ｿ繝ｳ
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.35f, 0.30f, 0.15f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.50f, 0.45f, 0.20f, 1.0f));
			if (ImGui::Button("##dir", ImVec2(iconSize, iconSize))) {
				currentDir = pe.path;
			}
			ImGui::PopStyleColor(2);

			// 笘・繝輔か繝ｫ繝縺ｸ縺ｮ繝峨Λ繝・げ・・ラ繝ｭ繝・・蜿励￠蜈･繧鯉ｼ医ヵ繧｡繧､繝ｫ遘ｻ蜍包ｼ・
			if (ImGui::BeginDragDropTarget()) {
				if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("RESOURCE_PATH")) {
					std::string srcPath((const char*)pl->Data, pl->DataSize - 1);
					std::string fileName = fs::path(srcPath).filename().string();
					std::string destPath = pe.path + "/" + fileName;
					if (srcPath != destPath && !fs::exists(destPath)) {
						std::error_code ec;
						fs::rename(srcPath, destPath, ec);
						if (!ec) {
							Log("Moved: " + srcPath + " -> " + destPath);
							// 繧ｵ繝繝阪う繝ｫ繧ｭ繝｣繝・す繝･縺ｮ遘ｻ蜍・
							auto it = thumbnailCache.find(srcPath);
							if (it != thumbnailCache.end()) {
								auto handle = it->second;
								thumbnailCache.erase(it);
								thumbnailCache[destPath] = handle;
							}
						} else {
							LogError("Move failed: " + ec.message());
						}
					}
				}
				// 笘・繝輔か繝ｫ繝縺ｮ繝峨Λ繝・げ・・ラ繝ｭ繝・・遘ｻ蜍輔↓繧ょｯｾ蠢・
				if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("RESOURCE_DIR")) {
					std::string srcPath((const char*)pl->Data, pl->DataSize - 1);
					std::string dirName = fs::path(srcPath).filename().string();
					std::string destPath = pe.path + "/" + dirName;
					if (srcPath != destPath && srcPath != pe.path && !fs::exists(destPath)) {
						std::error_code ec;
						fs::rename(srcPath, destPath, ec);
						if (!ec) {
							Log("Moved folder: " + srcPath + " -> " + destPath);
						} else {
							LogError("Move folder failed: " + ec.message());
						}
					}
				}
				ImGui::EndDragDropTarget();
			}

			// 繝輔か繝ｫ繝繧｢繧､繧ｳ繝ｳ縺ｮ繝・く繧ｹ繝・
			ImVec2 bmin = ImGui::GetItemRectMin();
			ImVec2 bmax = ImGui::GetItemRectMax();
			float cx = (bmin.x + bmax.x) * 0.5f;
			float cy = (bmin.y + bmax.y) * 0.5f;
			ImGui::GetWindowDrawList()->AddText(ImVec2(cx - 8, cy - 10), IM_COL32(255, 220, 80, 255), "D");

			// 笘・繝輔か繝ｫ繝縺ｮ繝峨Λ繝・げ・・ラ繝ｭ繝・・繧ｽ繝ｼ繧ｹ
			if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
				ImGui::SetDragDropPayload("RESOURCE_DIR", pe.path.c_str(), pe.path.size() + 1);
				ImGui::Text("[Dir] %s", pe.name.c_str());
				ImGui::EndDragDropSource();
			}
		} else if (isTexture) {
			// 笘・繝・け繧ｹ繝√Ε: 繧ｵ繝繝阪う繝ｫ繝励Ξ繝薙Η繝ｼ
			Engine::Renderer::TextureHandle th = 0;
			auto it = thumbnailCache.find(pe.path);
			if (it != thumbnailCache.end()) {
				th = it->second;
			} else {
				// std::replace for path separators
				std::string loadPath = pe.path;
				th = renderer->LoadTexture2D(loadPath);
				thumbnailCache[pe.path] = th;
			}
			auto srv = renderer->GetTextureSrvGpu(th);
			if (srv.ptr != 0) {
				ImGui::Image((ImTextureID)srv.ptr, ImVec2(iconSize, iconSize));
			} else {
				ImGui::Button("TEX", ImVec2(iconSize, iconSize));
			}
		} else if (isModel) {
			// 笘・繝｢繝・Ν: 繧｢繧､繧ｳ繝ｳ
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.30f, 0.40f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.40f, 0.55f, 1.0f));
			ImGui::Button("##mdl", ImVec2(iconSize, iconSize));
			ImGui::PopStyleColor(2);
			ImVec2 bmin = ImGui::GetItemRectMin();
			ImVec2 bmax = ImGui::GetItemRectMax();
			float cx = (bmin.x + bmax.x) * 0.5f;
			float cy = (bmin.y + bmax.y) * 0.5f;
			ImGui::GetWindowDrawList()->AddText(ImVec2(cx - 12, cy - 10), IM_COL32(100, 200, 255, 255), "3D");
		} else if (isAudio) {
			// 笘・髻ｳ螢ｰ: 蜀咲函/蛛懈ｭ｢繝懊ち繝ｳ莉倥″繧｢繧､繧ｳ繝ｳ
			bool isPlaying = (playingAudioPath == pe.path && playingVoiceHandle != 0);
			ImVec4 btnColor = isPlaying ? ImVec4(0.5f, 0.2f, 0.2f, 1.0f) : ImVec4(0.20f, 0.35f, 0.20f, 1.0f);
			ImVec4 btnHover = isPlaying ? ImVec4(0.7f, 0.3f, 0.3f, 1.0f) : ImVec4(0.30f, 0.50f, 0.30f, 1.0f);
			ImGui::PushStyleColor(ImGuiCol_Button, btnColor);
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, btnHover);
			if (ImGui::Button(isPlaying ? "##stop" : "##play", ImVec2(iconSize, iconSize))) {
				auto* audio = Engine::Audio::GetInstance();
				if (audio) {
					if (isPlaying) {
						audio->Stop(playingVoiceHandle);
						playingVoiceHandle = 0;
						playingAudioPath.clear();
					} else {
						// 蜑阪・蜀咲函繧貞●豁｢
						if (playingVoiceHandle != 0)
							audio->Stop(playingVoiceHandle);
						uint32_t sh = audio->Load(pe.path);
						if (sh != 0xFFFFFFFF) {
							playingVoiceHandle = audio->Play(sh, false, 0.5f);
							playingSoundHandle = sh;
							playingAudioPath = pe.path;
						}
					}
				}
			}
			ImGui::PopStyleColor(2);
			ImVec2 bmin = ImGui::GetItemRectMin();
			ImVec2 bmax = ImGui::GetItemRectMax();
			float cx = (bmin.x + bmax.x) * 0.5f;
			float cy = (bmin.y + bmax.y) * 0.5f;
			const char* icon = isPlaying ? "||" : ">";
			ImGui::GetWindowDrawList()->AddText(ImVec2(cx - 6, cy - 10), IM_COL32(180, 255, 180, 255), icon);
		} else if (isPrefab) {
			// 笘・Prefab: 髱堤ｷ代い繧､繧ｳ繝ｳ
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.40f, 0.40f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.30f, 0.50f, 0.50f, 1.0f));
			ImGui::Button("##prefab", ImVec2(iconSize, iconSize));
			ImGui::PopStyleColor(2);
			ImVec2 bmin = ImGui::GetItemRectMin();
			ImVec2 bmax = ImGui::GetItemRectMax();
			float cx = (bmin.x + bmax.x) * 0.5f;
			float cy = (bmin.y + bmax.y) * 0.5f;
			ImGui::GetWindowDrawList()->AddText(ImVec2(cx - 16, cy - 10), IM_COL32(150, 255, 200, 255), "PFB");
		} else if (isScript) {
			// 笘・C++繧ｹ繧ｯ繝ｪ繝励ヨ: 邏ｫ繧｢繧､繧ｳ繝ｳ
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.15f, 0.35f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.35f, 0.25f, 0.45f, 1.0f));
			ImGui::Button("##script", ImVec2(iconSize, iconSize));
			ImGui::PopStyleColor(2);
			ImVec2 bmin = ImGui::GetItemRectMin();
			ImVec2 bmax = ImGui::GetItemRectMax();
			float cx = (bmin.x + bmax.x) * 0.5f;
			float cy = (bmin.y + bmax.y) * 0.5f;
			ImGui::GetWindowDrawList()->AddText(ImVec2(cx - 12, cy - 10), IM_COL32(200, 150, 255, 255), "C++");
		} else {
			// 笘・縺昴・莉悶ヵ繧｡繧､繝ｫ: 繧ｰ繝ｬ繝ｼ繧｢繧､繧ｳ繝ｳ
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.25f, 0.25f, 1.0f));
			ImGui::Button("##file", ImVec2(iconSize, iconSize));
			ImGui::PopStyleColor();
			ImVec2 bmin = ImGui::GetItemRectMin();
			ImVec2 bmax = ImGui::GetItemRectMax();
			float cx = (bmin.x + bmax.x) * 0.5f;
			float cy = (bmin.y + bmax.y) * 0.5f;
			ImGui::GetWindowDrawList()->AddText(ImVec2(cx - 6, cy - 10), IM_COL32(180, 180, 180, 255), "F");
		}

		// 笘・ｿｽ蜉: 繧ｹ繧ｯ繝ｪ繝励ヨ繧・ユ繧ｭ繧ｹ繝医ヵ繧｡繧､繝ｫ繧偵ム繝悶Ν繧ｯ繝ｪ繝・け縺ｧVS Code縺ｧ髢九￥
		if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
			if (isScript || pe.ext == ".json" || pe.ext == ".txt") {
				std::string cmd = "code \"" + pe.path + "\"";
				system(cmd.c_str());
			}
		}

		// 笘・繝峨Λ繝・げ・・ラ繝ｭ繝・・繧ｽ繝ｼ繧ｹ (繝輔ぃ繧､繝ｫ縺ｮ縺ｿ 窶・繝輔か繝ｫ繝縺ｯ荳翫〒蛻･騾泌・逅・
		if (!pe.isDir && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
			ImGui::SetDragDropPayload("RESOURCE_PATH", pe.path.c_str(), pe.path.size() + 1);
			ImGui::Text("%s", pe.name.c_str());
			ImGui::EndDragDropSource();
		}

		// 笘・蜿ｳ繧ｯ繝ｪ繝・け繧ｳ繝ｳ繝・く繧ｹ繝医Γ繝九Η繝ｼ (繧｢繧､繝・Β荳・
		if (ImGui::BeginPopupContextItem("##itemCtx")) {
			if (ImGui::MenuItem("Rename")) {
				renaming = true;
				renamingPath = pe.path;
				memset(renameBuffer, 0, sizeof(renameBuffer));
				strncpy_s(renameBuffer, pe.name.c_str(), sizeof(renameBuffer) - 1);
			}
			if (ImGui::MenuItem("Delete")) {
				showDeleteConfirm = true;
				deletingPath = pe.path;
				deletingName = pe.name;
			}
			ImGui::Separator();
			if (ImGui::MenuItem("Show in Explorer")) {
				std::string cmd = "explorer /select,\"" + pe.path + "\"";
				std::replace(cmd.begin(), cmd.end(), '/', '\\');
				system(cmd.c_str());
			}
			ImGui::EndPopup();
		}

		// 繝輔ぃ繧､繝ｫ蜷・(蛻・ｊ隧ｰ繧√※陦ｨ遉ｺ)
		float textWidth = iconSize;
		std::string displayName = pe.name;
		if (ImGui::CalcTextSize(displayName.c_str()).x > textWidth) {
			while (displayName.size() > 3 && ImGui::CalcTextSize((displayName + "..").c_str()).x > textWidth) {
				displayName.pop_back();
			}
			displayName += "..";
		}
		ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + textWidth);
		ImGui::TextUnformatted(displayName.c_str());
		ImGui::PopTextWrapPos();

		// 繝・・繝ｫ繝√ャ繝・(繝輔Ν繝代せ)
		if (ImGui::IsItemHovered()) {
			ImGui::BeginTooltip();
			ImGui::Text("%s", pe.path.c_str());
			ImGui::EndTooltip();
		}

		ImGui::EndGroup();
		col++;

		ImGui::PopID();
	}

	// 笘・閭梧勹縺ｮ蜿ｳ繧ｯ繝ｪ繝・け繝｡繝九Η繝ｼ・井ｽ輔ｂ縺ｪ縺・ｴ謇縺ｧ蜿ｳ繧ｯ繝ｪ繝・け・・
	if (ImGui::BeginPopupContextWindow("##bgCtx", ImGuiPopupFlags_NoOpenOverItems | ImGuiPopupFlags_MouseButtonRight)) {
		if (ImGui::MenuItem("Create Folder")) {
			creatingFolder = true;
			memset(newFolderNameBuf, 0, sizeof(newFolderNameBuf));
			strncpy_s(newFolderNameBuf, "NewFolder", sizeof(newFolderNameBuf) - 1);
		}
		if (ImGui::BeginMenu("Create File")) {
			// 笘・ｿｽ蜉: C++繧ｹ繧ｯ繝ｪ繝励ヨ菴懈・繝懊ち繝ｳ
			if (ImGui::MenuItem("C++ Script")) {
				creatingScript = true;
				memset(newScriptNameBuf, 0, sizeof(newScriptNameBuf));
				strncpy_s(newScriptNameBuf, "NewScript", sizeof(newScriptNameBuf) - 1);
			}
			if (ImGui::MenuItem(".prefab")) {
				std::string path = currentDir + "/New.prefab";
				int num = 1;
				while (fs::exists(path)) {
					path = currentDir + "/New_" + std::to_string(num++) + ".prefab";
				}
				std::ofstream f(path);
				f << "{\n  \"name\": \"NewObject\",\n  \"translate\": [0, 0, 0],\n  \"rotate\": [0, 0, 0],\n  \"scale\": [1, 1, 1],\n  \"color\": [1, 1, 1, 1],\n  \"components\": []\n}\n";
				f.close();
				Log("Created: " + path);
			}
			if (ImGui::MenuItem(".json (empty)")) {
				std::string path = currentDir + "/NewFile.json";
				int num = 1;
				while (fs::exists(path)) {
					path = currentDir + "/NewFile_" + std::to_string(num++) + ".json";
				}
				std::ofstream f(path);
				f << "{\n}\n";
				f.close();
				Log("Created: " + path);
			}
			if (ImGui::MenuItem(".txt (empty)")) {
				std::string path = currentDir + "/NewFile.txt";
				int num = 1;
				while (fs::exists(path)) {
					path = currentDir + "/NewFile_" + std::to_string(num++) + ".txt";
				}
				std::ofstream f(path);
				f.close();
				Log("Created: " + path);
			}
			ImGui::EndMenu();
		}
		ImGui::Separator();
		if (ImGui::MenuItem("Open in Explorer")) {
			std::string cmd = "explorer \"" + currentDir + "\"";
			std::replace(cmd.begin(), cmd.end(), '/', '\\');
			system(cmd.c_str());
		}
		if (ImGui::MenuItem("Refresh")) {
			// 繧ｵ繝繝阪う繝ｫ繧ｭ繝｣繝・す繝･繧ｯ繝ｪ繧｢・亥・隱ｭ縺ｿ霎ｼ縺ｿ・・
			thumbnailCache.clear();
			Log("Refreshed project view.");
		}
		ImGui::EndPopup();
	}

	ImGui::End();
}

void EditorUI::ShowSceneSettings(Engine::Renderer* renderer) {
	ImGui::Begin("Scene Settings");
	if (ImGui::CollapsingHeader("Post Processing", ImGuiTreeNodeFlags_DefaultOpen)) {
		auto pp = renderer->GetPostProcessParams();
		bool ch = false;
		bool en = renderer->GetPostProcessEnabled();
		if (ImGui::Checkbox("Enable", &en))
			renderer->SetPostProcessEnabled(en);
		if (en) {
			ch |= ImGui::DragFloat("Vignette", &pp.vignette, 0.01f, 0, 5);
			ch |= ImGui::DragFloat("Distortion", &pp.distortion, 0.001f, 0, 1);
			ch |= ImGui::DragFloat("Noise", &pp.noiseStrength, 0.01f, 0, 1);
			ch |= ImGui::DragFloat("Chromatic", &pp.chromaShift, 0.001f, 0, 0.1f);
			ch |= ImGui::DragFloat("Scanline", &pp.scanline, 0.01f, 0, 1);
		}
		if (ch)
			renderer->SetPostProcessParams(pp);
	}
	ImGui::End();
}

void EditorUI::ShowConsole() {
	ImGui::Begin("Console");
	if (ImGui::SmallButton("Clear"))
		consoleLog.clear();
	ImGui::SameLine();
	ImGui::Text("(%d)", (int)consoleLog.size());
	ImGui::Separator();
	ImGui::BeginChild("CS", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
	for (const auto& e : consoleLog) {
		ImVec4 c;
		const char* p;
		switch (e.level) {
		case LogLevel::Info:
			c = {.8f, .8f, .8f, 1};
			p = "[INFO] ";
			break;
		case LogLevel::Warning:
			c = {1, .9f, .3f, 1};
			p = "[WARN] ";
			break;
		default:
			c = {1, .3f, .3f, 1};
			p = "[ERR]  ";
			break;
		}
		ImGui::PushStyleColor(ImGuiCol_Text, c);
		ImGui::TextUnformatted((std::string(p) + e.message).c_str());
		ImGui::PopStyleColor();
	}
	if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 10)
		ImGui::SetScrollHereY(1);
	ImGui::EndChild();
	ImGui::End();
}

// ====== 笘・驕ｸ謚槭ぐ繧ｺ繝｢ + 繝上う繝ｩ繧､繝域緒逕ｻ ======
void EditorUI::DrawSelectionGizmo(Engine::Renderer* renderer, GameScene* scene) {
	if (!scene)
		return;
	for (int idx : scene->selectedIndices_) {
		if (idx < 0 || idx >= (int)scene->objects_.size())
			continue;
		auto& obj = scene->objects_[idx];
		Engine::Vector3 pos = {obj.translate.x, obj.translate.y, obj.translate.z};
		const float al = 2.0f, ar = 0.3f;

		// 笘・繧ｮ繧ｺ繝｢縺ｮ濶ｲ (繝峨Λ繝・げ荳ｭ縺ｮ霆ｸ縺ｯ譏弱ｋ縺上√◎繧御ｻ･螟悶・騾壼ｸｸ濶ｲ)
		auto axisColor = [](int axis, int dragAxis) -> Engine::Vector4 {
			bool active = (dragAxis == axis);
			switch (axis) {
			case 0:
				return active ? Engine::Vector4{1.0f, 0.5f, 0.5f, 1.0f} : Engine::Vector4{1.0f, 0.2f, 0.2f, 1.0f}; // X=襍､
			case 1:
				return active ? Engine::Vector4{0.5f, 1.0f, 0.5f, 1.0f} : Engine::Vector4{0.2f, 1.0f, 0.2f, 1.0f}; // Y=邱・
			case 2:
				return active ? Engine::Vector4{0.5f, 0.5f, 1.0f, 1.0f} : Engine::Vector4{0.2f, 0.2f, 1.0f, 1.0f}; // Z=髱・
			default:
				return {1, 1, 1, 1};
			}
		};

		int dAxis = (gizmoDragging && idx == scene->selectedObjectIndex_) ? gizmoDragAxis : -1;
		auto cX = axisColor(0, dAxis), cY = axisColor(1, dAxis), cZ = axisColor(2, dAxis);

		if (currentGizmoMode == GizmoMode::Translate) {
			// X霆ｸ 竊・
			renderer->DrawLine3D(pos, {pos.x + al, pos.y, pos.z}, cX);
			renderer->DrawLine3D({pos.x + al, pos.y, pos.z}, {pos.x + al - ar, pos.y + ar * .4f, pos.z}, cX);
			renderer->DrawLine3D({pos.x + al, pos.y, pos.z}, {pos.x + al - ar, pos.y - ar * .4f, pos.z}, cX);
			// Y霆ｸ 竊・
			renderer->DrawLine3D(pos, {pos.x, pos.y + al, pos.z}, cY);
			renderer->DrawLine3D({pos.x, pos.y + al, pos.z}, {pos.x + ar * .4f, pos.y + al - ar, pos.z}, cY);
			renderer->DrawLine3D({pos.x, pos.y + al, pos.z}, {pos.x - ar * .4f, pos.y + al - ar, pos.z}, cY);
			// Z霆ｸ
			renderer->DrawLine3D(pos, {pos.x, pos.y, pos.z + al}, cZ);
			renderer->DrawLine3D({pos.x, pos.y, pos.z + al}, {pos.x, pos.y + ar * .4f, pos.z + al - ar}, cZ);
			renderer->DrawLine3D({pos.x, pos.y, pos.z + al}, {pos.x, pos.y - ar * .4f, pos.z + al - ar}, cZ);
		} else if (currentGizmoMode == GizmoMode::Rotate) {
			const int seg = 32;
			const float rad = 1.5f;
			for (int i = 0; i < seg; ++i) {
				float a0 = (float)i / seg * DirectX::XM_2PI, a1 = (float)(i + 1) / seg * DirectX::XM_2PI;
				renderer->DrawLine3D({pos.x, pos.y + cosf(a0) * rad, pos.z + sinf(a0) * rad}, {pos.x, pos.y + cosf(a1) * rad, pos.z + sinf(a1) * rad}, cX);
				renderer->DrawLine3D({pos.x + cosf(a0) * rad, pos.y, pos.z + sinf(a0) * rad}, {pos.x + cosf(a1) * rad, pos.y, pos.z + sinf(a1) * rad}, cY);
				renderer->DrawLine3D({pos.x + cosf(a0) * rad, pos.y + sinf(a0) * rad, pos.z}, {pos.x + cosf(a1) * rad, pos.y + sinf(a1) * rad, pos.z}, cZ);
			}
		} else {
			float e = 0.15f;
			renderer->DrawLine3D(pos, {pos.x + al, pos.y, pos.z}, cX);
			renderer->DrawLine3D({pos.x + al - e, pos.y - e, pos.z}, {pos.x + al + e, pos.y + e, pos.z}, cX);
			renderer->DrawLine3D({pos.x + al + e, pos.y - e, pos.z}, {pos.x + al - e, pos.y + e, pos.z}, cX);
			renderer->DrawLine3D(pos, {pos.x, pos.y + al, pos.z}, cY);
			renderer->DrawLine3D({pos.x - e, pos.y + al - e, pos.z}, {pos.x + e, pos.y + al + e, pos.z}, cY);
			renderer->DrawLine3D({pos.x + e, pos.y + al - e, pos.z}, {pos.x - e, pos.y + al + e, pos.z}, cY);
			renderer->DrawLine3D(pos, {pos.x, pos.y, pos.z + al}, cZ);
			renderer->DrawLine3D({pos.x, pos.y - e, pos.z + al - e}, {pos.x, pos.y + e, pos.z + al + e}, cZ);
			renderer->DrawLine3D({pos.x, pos.y + e, pos.z + al - e}, {pos.x, pos.y - e, pos.z + al + e}, cZ);
		}

		// 笘・驕ｸ謚槭ワ繧､繝ｩ繧､繝・ 繝舌え繝ｳ繝・ぅ繝ｳ繧ｰ繝懊ャ繧ｯ繧ｹ (鮟・牡縺ｮ繝ｯ繧､繝､繝ｼ繝輔Ξ繝ｼ繝)
		float sx = obj.scale.x * 0.5f, sy = obj.scale.y * 0.5f, sz = obj.scale.z * 0.5f;
		Engine::Vector4 hlColor = {1.0f, 0.85f, 0.0f, 0.9f}; // 譏弱ｋ縺・ｻ・牡
		Engine::Vector3 v[8] = {
		    {pos.x - sx, pos.y - sy, pos.z - sz},
            {pos.x + sx, pos.y - sy, pos.z - sz},
            {pos.x + sx, pos.y + sy, pos.z - sz},
            {pos.x - sx, pos.y + sy, pos.z - sz},
		    {pos.x - sx, pos.y - sy, pos.z + sz},
            {pos.x + sx, pos.y - sy, pos.z + sz},
            {pos.x + sx, pos.y + sy, pos.z + sz},
            {pos.x - sx, pos.y + sy, pos.z + sz},
		};
		int edges[][2] = {
		    {0, 1},
            {1, 2},
            {2, 3},
            {3, 0},
            {4, 5},
            {5, 6},
            {6, 7},
            {7, 4},
            {0, 4},
            {1, 5},
            {2, 6},
            {3, 7}
        };
		for (auto& eg : edges)
			renderer->DrawLine3D(v[eg[0]], v[eg[1]], hlColor);

		// 笘・ｿｽ蜉: 繧ｳ繝ｩ繧､繝繝ｼ蜿ｯ隕門喧 (邱題牡縺ｮ繝ｯ繧､繝､繝ｼ繝輔Ξ繝ｼ繝)
		for (const auto& bc : obj.boxColliders) {
			if (!bc.enabled)
				continue;
			float csx = bc.size.x * 0.5f * obj.scale.x;
			float csy = bc.size.y * 0.5f * obj.scale.y;
			float csz = bc.size.z * 0.5f * obj.scale.z;
			Engine::Vector3 cp = {pos.x + bc.center.x * obj.scale.x, pos.y + bc.center.y * obj.scale.y, pos.z + bc.center.z * obj.scale.z};
			Engine::Vector4 colColor = {0.2f, 1.0f, 0.2f, 0.8f}; // 邱題牡
			Engine::Vector3 cv[8] = {
			    {cp.x - csx, cp.y - csy, cp.z - csz},
                {cp.x + csx, cp.y - csy, cp.z - csz},
                {cp.x + csx, cp.y + csy, cp.z - csz},
                {cp.x - csx, cp.y + csy, cp.z - csz},
			    {cp.x - csx, cp.y - csy, cp.z + csz},
                {cp.x + csx, cp.y - csy, cp.z + csz},
                {cp.x + csx, cp.y + csy, cp.z + csz},
                {cp.x - csx, cp.y + csy, cp.z + csz},
			};
			for (auto& eg : edges)
				renderer->DrawLine3D(cv[eg[0]], cv[eg[1]], colColor);
		}
	}
}
// ====== 笘・Animation Window ======
void EditorUI::ShowAnimationWindow(Engine::Renderer* renderer, GameScene* scene) {
	(void)renderer;
	ImGui::Begin("Animation");
	if (scene && scene->selectedObjectIndex_ >= 0 && scene->selectedObjectIndex_ < (int)scene->objects_.size()) {
		auto& obj = scene->objects_[scene->selectedObjectIndex_];
		if (!obj.animators.empty()) {
			auto& anim = obj.animators[0]; // 譛蛻昴・Animator繧定｡ｨ遉ｺ
			ImGui::Text("Selected: %s (Animator)", obj.name.c_str());
			ImGui::Separator();

			// 繧｢繝九Γ繝ｼ繧ｷ繝ｧ繝ｳ繝ｪ繧ｹ繝茨ｼ医Δ繝・Ν縺梧戟縺｣縺ｦ縺・ｋ繧｢繝九Γ繝ｼ繧ｷ繝ｧ繝ｳ繧貞叙蠕暦ｼ・
			auto* r = Engine::Renderer::GetInstance();
			auto* m = r->GetModel(obj.modelHandle);
			if (m) {
				const auto& data = m->GetData();
				if (!data.animations.empty()) {
					if (ImGui::BeginCombo("Clips", anim.currentAnimation.c_str())) {
						for (const auto& a : data.animations) {
							bool selected = (anim.currentAnimation == a.name);
							if (ImGui::Selectable(a.name.c_str(), selected)) {
								anim.currentAnimation = a.name;
								anim.time = 0.0f;
							}
							if (selected)
								ImGui::SetItemDefaultFocus();
						}
						ImGui::EndCombo();
					}

					// 迴ｾ蝨ｨ縺ｮ繧｢繝九Γ繝ｼ繧ｷ繝ｧ繝ｳ繧呈爾縺・
					const Engine::Animation* currentAnimPtr = nullptr;
					for (const auto& a : data.animations) {
						if (a.name == anim.currentAnimation) {
							currentAnimPtr = &a;
							break;
						}
					}

					if (currentAnimPtr) {
						ImGui::Text("Duration: %.2f ticks (%.1f fps)", currentAnimPtr->duration, currentAnimPtr->ticksPerSecond);
						// 繧ｷ繝ｼ繧ｯ繝舌・ (繧ｿ繧､繝繝ｩ繧､繝ｳ)
						ImGui::SliderFloat("Time", &anim.time, 0.0f, currentAnimPtr->duration, "%.2f");

						// 蜀咲函繧ｳ繝ｳ繝医Ο繝ｼ繝ｫ
						if (ImGui::Button(anim.isPlaying ? "Stop" : "Play")) {
							anim.isPlaying = !anim.isPlaying;
						}
						ImGui::SameLine();
						ImGui::Checkbox("Loop", &anim.loop);
						ImGui::SameLine();
						ImGui::DragFloat("Speed", &anim.speed, 0.01f, 0.0f, 10.0f);
					}
				} else {
					ImGui::Text("No animations found in this model.");
				}
			} else {
				ImGui::Text("No valid model attached.");
			}
		} else {
			ImGui::Text("Selected object has no Animator Component.");
			if (ImGui::Button("Add Animator")) {
				obj.animators.push_back({});
			}
		}
	} else {
		ImGui::Text("No Object Selected.");
	}
	ImGui::End();
}

// ====== Play Mode Monitor ======
void EditorUI::ShowPlayModeMonitor(GameScene* scene) {
	if (!scene || !scene->IsPlaying())
		return;

	ImGui::Begin("Play Mode Monitor");

	// 笘・FPS繧ｫ繧ｦ繝ｳ繧ｿ繝ｼ
	float fps = ImGui::GetIO().Framerate;
	ImVec4 col = {0.0f, 1.0f, 0.0f, 1.0f};
	if (fps < 55.0f) col = {1.0f, 1.0f, 0.0f, 1.0f};
	if (fps < 30.0f) col = {1.0f, 0.0f, 0.0f, 1.0f};
	ImGui::TextColored(col, "FPS: %.1f", fps);
	ImGui::Separator();

	static std::map<size_t, std::vector<float>> hpHistories;

	if (ImGui::BeginTable("MonitorTable", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
		ImGui::TableSetupColumn("Name");
		ImGui::TableSetupColumn("Pos (X,Y,Z)");
		ImGui::TableSetupColumn("HP Status");
		ImGui::TableSetupColumn("HP Graph (Recent 100 frames)");
		ImGui::TableHeadersRow();

		const auto& objs = scene->GetObjects();
		for (size_t i = 0; i < objs.size(); ++i) {
			const auto& obj = objs[i];
			ImGui::TableNextRow();

			// Name
			ImGui::TableSetColumnIndex(0);
			ImGui::Text("%s", obj.name.c_str());

			// Position
			ImGui::TableSetColumnIndex(1);
			ImGui::Text("%.2f, %.2f, %.2f", obj.translate.x, obj.translate.y, obj.translate.z);

			// HP
			ImGui::TableSetColumnIndex(2);
			float currentHp = 0.0f;
			float maxHp = 0.0f;
			if (!obj.healths.empty()) {
				currentHp = obj.healths[0].hp;
				maxHp = obj.healths[0].maxHp;
				ImGui::Text("%.1f / %.1f", currentHp, maxHp);
			} else {
				ImGui::Text("-");
			}

			// Graph
			ImGui::TableSetColumnIndex(3);
			if (!obj.healths.empty()) {
				auto& history = hpHistories[i];
				history.push_back(currentHp);
				if (history.size() > 100)
					history.erase(history.begin());

				ImGui::PlotLines("##hplot", history.data(), (int)history.size(), 0, nullptr, 0.0f, maxHp, ImVec2(0, 40));
			} else {
				ImGui::Text("No Health Component");
			}
		}
		ImGui::EndTable();
	}
	ImGui::End();
}

} // namespace Game
