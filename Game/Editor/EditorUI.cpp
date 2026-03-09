#include "EditorUI.h"
#include "../../externals/imgui/imgui.h"
#include "../../externals/imgui/imgui_internal.h"
#include "../Scenes/GameScene.h"
#include "../Systems/RiverSystem.h" // ★追加
#include "../Systems/UISystem.h"    // ★追加
#include "Audio.h"
#include "PipeEditor.h"
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

namespace Game {
namespace fs = std::filesystem;

// ====== Static State ======
static std::deque<UndoCommand> undoStack;
static std::deque<UndoCommand> redoStack;
static constexpr size_t kMaxUndoDepth = 100;
// ★ non-static: GameScene.cppからexternで参照
GizmoMode currentGizmoMode = GizmoMode::Translate;
static std::deque<LogEntry> consoleLog;
static constexpr size_t kMaxConsoleLines = 500;
static float globalTime = 0.0f;

// ★ ビューポート操作用の状態 (non-static: extern参照)
bool gizmoDragging = false;
int gizmoDragAxis = -1; // 0=X, 1=Y, 2=Z
static std::map<int, Engine::Transform> dragStartTransforms = {};
static ImVec2 gizmoDragStartMouse = {};
static bool objectDragging = false;               // ★ 自由ドラッグ中フラグ
static std::vector<SceneObject> clipboardObjects; // Ctrl+C コピー用
static bool s_riverPlaceMode = false;             // ★追加: 川ポイント配置モード
static int s_riverPlaceCompIdx = 0;               // 川コンポーネントindexコピー用

// ★UIギズモ用ドラッグ状態
static bool uiDragging = false;
static int uiDragHandle = -1; // 0..7: Corners/Edges, 8: Center (Move)
static DirectX::XMFLOAT2 uiDragStartPos = {};
static DirectX::XMFLOAT2 uiDragStartSize = {};
static DirectX::XMFLOAT2 uiDragStartHitOffset = {};
static DirectX::XMFLOAT2 uiDragStartHitScale = {};
static bool uiHoveredAny = false; // 次のフレームのクリック判定用
static int uiHoveredHandle = -1;

// ★ Gameウィンドウの画像座標 (ピッキング用)
static ImVec2 gameImageMin = {};
static ImVec2 gameImageMax = {};

static PipeEditor s_pipeEditor;
static uint32_t nextObjectId = 1;
static uint32_t GenerateId() { return nextObjectId++; }

// ★ コピー/複製時のユニーク名生成
static std::string GenerateCopyName(const std::string& baseName, const std::vector<SceneObject>& objects) {
	// 末尾の "_数字" や " (Copy)" を除去してベース名を取得
	std::string base = baseName;
	// " (Copy)" の繰り返しを除去
	while (base.size() > 7 && base.substr(base.size() - 7) == " (Copy)")
		base = base.substr(0, base.size() - 7);
	// "_数字" を除去
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
	// 既存のオブジェクト名から最大番号を探す
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
	// ★追加: ParticleEmitterのシリアライズ
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
	// ★追加: PlayerInput
	for (const auto& pi : o.playerInputs) {
		if (!first)
			ss << ",\n";
		first = false;
		ss << "        {\"type\": \"PlayerInput\", \"enabled\": " << (pi.enabled ? "true" : "false") << "}";
	}
	// ★追加: CharacterMovement
	for (const auto& cm : o.characterMovements) {
		if (!first)
			ss << ",\n";
		first = false;
		ss << "        {\"type\": \"CharacterMovement\", \"enabled\": " << (cm.enabled ? "true" : "false") << ", \"speed\": " << cm.speed << ", \"jumpPower\": " << cm.jumpPower
		   << ", \"gravity\": " << cm.gravity << "}";
	}
	// ★追加: CameraTarget
	for (const auto& ct : o.cameraTargets) {
		if (!first)
			ss << ",\n";
		first = false;
		ss << "        {\"type\": \"CameraTarget\", \"enabled\": " << (ct.enabled ? "true" : "false") << ", \"distance\": " << ct.distance << ", \"height\": " << ct.height
		   << ", \"smoothSpeed\": " << ct.smoothSpeed << "}";
	}
	// ★追加: Light Components
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
	// ★追加: AudioSource
	for (const auto& as : o.audioSources) {
		if (!first)
			ss << ",\n";
		first = false;
		ss << "        {\"type\": \"AudioSource\", \"enabled\": " << (as.enabled ? "true" : "false") << ", \"soundPath\": \"" << EscapeJson(as.soundPath) << "\", \"volume\": " << as.volume
		   << ", \"loop\": " << (as.loop ? "true" : "false") << ", \"playOnStart\": " << (as.playOnStart ? "true" : "false") << ", \"is3D\": " << (as.is3D ? "true" : "false")
		   << ", \"maxDistance\": " << as.maxDistance << "}";
	}
	// ★追加: AudioListener
	for (const auto& al : o.audioListeners) {
		if (!first)
			ss << ",\n";
		first = false;
		ss << "        {\"type\": \"AudioListener\", \"enabled\": " << (al.enabled ? "true" : "false") << "}";
	}
	// ★追加: Hitbox
	for (const auto& hb : o.hitboxes) {
		if (!first)
			ss << ",\n";
		first = false;
		ss << "        {\"type\": \"Hitbox\", \"enabled\": " << (hb.enabled ? "true" : "false") << ", \"center\": [" << hb.center.x << "," << hb.center.y << "," << hb.center.z << "], \"size\": ["
		   << hb.size.x << "," << hb.size.y << "," << hb.size.z << "], \"damage\": " << hb.damage << ", \"isActive\": " << (hb.isActive ? "true" : "false") << ", \"tag\": \"" << EscapeJson(hb.tag)
		   << "\"}";
	}
	// ★追加: Hurtbox
	for (const auto& hb : o.hurtboxes) {
		if (!first)
			ss << ",\n";
		first = false;
		ss << "        {\"type\": \"Hurtbox\", \"enabled\": " << (hb.enabled ? "true" : "false") << ", \"center\": [" << hb.center.x << "," << hb.center.y << "," << hb.center.z << "], \"size\": ["
		   << hb.size.x << "," << hb.size.y << "," << hb.size.z << "], \"tag\": \"" << EscapeJson(hb.tag) << "\", \"damageMultiplier\": " << hb.damageMultiplier << "}";
	}
	// ★追加: Health
	for (const auto& hc : o.healths) {
		if (!first)
			ss << ",\n";
		first = false;
		ss << "        {\"type\": \"Health\", \"enabled\": " << (hc.enabled ? "true" : "false") << ", \"hp\": " << hc.hp << ", \"maxHp\": " << hc.maxHp << ", \"stamina\": " << hc.stamina
		   << ", \"maxStamina\": " << hc.maxStamina << ", \"invincibleTime\": " << hc.invincibleTime << ", \"isDead\": " << (hc.isDead ? "true" : "false") << "}";
	}
	// ★追加: Script
	for (const auto& sc : o.scripts) {
		if (!first)
			ss << ",\n";
		first = false;
		ss << "        {\"type\": \"Script\", \"enabled\": " << (sc.enabled ? "true" : "false") << ", \"scriptPath\": \"" << EscapeJson(sc.scriptPath) << "\"}";
	}
	// ★追加: UI Components
	for (const auto& rt : o.rectTransforms) {
		if (!first)
			ss << ",\n";
		first = false;
		ss << "        {\"type\": \"RectTransform\", \"enabled\": " << (rt.enabled ? "true" : "false") << ", \"pos\": [" << rt.pos.x << "," << rt.pos.y << "], \"size\": [" << rt.size.x << ","
		   << rt.size.y << "], \"anchor\": [" << rt.anchor.x << "," << rt.anchor.y << "], \"pivot\": [" << rt.pivot.x << "," << rt.pivot.y << "], \"rotation\": " << rt.rotation << "}";
	}
	for (const auto& img : o.images) {
		if (!first)
			ss << ",\n";
		first = false;
		ss << "        {\"type\": \"UIImage\", \"enabled\": " << (img.enabled ? "true" : "false") << ", \"texturePath\": \"" << EscapeJson(img.texturePath) << "\", \"color\": [" << img.color.x << ","
		   << img.color.y << "," << img.color.z << "," << img.color.w << "]}";
	}
	for (const auto& txt : o.texts) {
		if (!first)
			ss << ",\n";
		first = false;
		ss << "        {\"type\": \"UIText\", \"enabled\": " << (txt.enabled ? "true" : "false") << ", \"text\": \"" << EscapeJson(txt.text) << "\", \"fontSize\": " << txt.fontSize << ", \"color\": ["
		   << txt.color.x << "," << txt.color.y << "," << txt.color.z << "," << txt.color.w << "]}";
	}
	for (const auto& btn : o.buttons) {
		if (!first)
			ss << ",\n";
		first = false;
		ss << "        {\"type\": \"UIButton\", \"enabled\": " << (btn.enabled ? "true" : "false") << ", \"normalColor\": [" << btn.normalColor.x << "," << btn.normalColor.y << ","
		   << btn.normalColor.z << "," << btn.normalColor.w << "], \"hoverColor\": [" << btn.hoverColor.x << "," << btn.hoverColor.y << "," << btn.hoverColor.z << "," << btn.hoverColor.w
		   << "], \"pressedColor\": [" << btn.pressedColor.x << "," << btn.pressedColor.y << "," << btn.pressedColor.z << "," << btn.pressedColor.w << "]}";
	}

	for (const auto& rv : o.rivers) {
		if (!first)
			ss << ",\n";
		first = false;
		ss << "        {\"type\": \"River\", \"enabled\": " << (rv.enabled ? "true" : "false") << ", \"width\": " << rv.width << ", \"flowSpeed\": " << rv.flowSpeed << ", \"uvScale\": " << rv.uvScale
		   << ", \"texture\": \"" << rv.texturePath << "\", \"points\": [";
		for (size_t i = 0; i < rv.points.size(); ++i) {
			ss << rv.points[i].x << "," << rv.points[i].y << "," << rv.points[i].z << (i == rv.points.size() - 1 ? "" : ",");
		}
		ss << "]}";
	}
	ss << "\n      ]\n";
	ss << "    }";
	return ss.str();
}

std::string EditorUI::GetUnifiedProjectPath(const std::string& path) {
	std::string absPath = path;
	if (absPath.length() >= 2 && (absPath[1] == ':' || absPath[0] == '/' || absPath[0] == '\\')) {
		return absPath; // 既に絶対パス
	}

	char exePath[MAX_PATH] = {0};
	::GetModuleFileNameA(nullptr, exePath, MAX_PATH);
	std::filesystem::path currentP = std::filesystem::path(exePath).parent_path();
	std::filesystem::path exeDir = currentP; // フォールバック用に保存

	// Engineフォルダを探す
	while (currentP.has_parent_path() && currentP.filename() != "Engine") {
		auto parent = currentP.parent_path();
		if (parent == currentP) { // ルートディレクトリに到達（無限ループ防止）
			break;
		}
		currentP = parent;
	}

	if (currentP.filename() == "Engine") {
		std::filesystem::path projectDir = currentP / "TD_Engine";
		return (projectDir / path).string();
	}

	// フォールバック: 実行ファイルのあるディレクトリを基準とする
	return (exeDir / path).string();
}

void EditorUI::SaveScene(GameScene* scene, const std::string& path) {
	if (!scene)
		return;

	std::string absPath = GetUnifiedProjectPath(path);

	std::ofstream f(absPath);
	if (!f.is_open()) {
		LogError("Save failed: " + absPath);
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

	// ★追加: Gitでの競合(コンフリクト)を防ぐため、オブジェクトを名前順にソートして保存する。
	std::vector<SceneObject> sortedObjects = scene->objects_;
	std::stable_sort(sortedObjects.begin(), sortedObjects.end(), [](const SceneObject& a, const SceneObject& b) { return a.name < b.name; });

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
	if (pos == std::string::npos)
		return res;
	auto arrStart = block.find("[", pos);
	if (arrStart == std::string::npos)
		return res;
	auto arrEnd = FindBlockEnd(block, arrStart);
	if (arrEnd == std::string::npos)
		return res;

	std::string ab = block.substr(arrStart + 1, arrEnd - arrStart - 1);
	size_t cur = 0;
	while (cur < ab.size()) {
		auto q1 = ab.find("\"", cur);
		if (q1 == std::string::npos)
			break;
		size_t q2 = q1 + 1;
		while (q2 < ab.size()) {
			if (ab[q2] == '\\')
				q2 += 2;
			else if (ab[q2] == '"')
				break;
			else
				q2++;
		}
		if (q2 >= ab.size())
			break;
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
	if (start >= s.size() || !isdigit((unsigned char)s[start]))
		return defaultVal;
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
			if (mr.shaderName.empty())
				mr.shaderName = "Default";
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
		} else if (type == "PointLight") { // ★追加
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
		} else if (type == "SpotLight") { // ★追加
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
		} else if (type == "AudioSource") { // ★追加
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
			// 音声ファイルをロード
			if (!as.soundPath.empty()) {
				auto* audio = Engine::Audio::GetInstance();
				if (audio)
					as.soundHandle = audio->Load(as.soundPath);
			}
			obj.audioSources.push_back(as);
		} else if (type == "AudioListener") { // ★追加
			AudioListenerComponent al;
			al.enabled = enabled;
			obj.audioListeners.push_back(al);
		} else if (type == "Hitbox") { // ★追加
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
		} else if (type == "Hurtbox") { // ★追加
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
		} else if (type == "Health") { // ★追加
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
		} else if (type == "Script") { // ★追加
			ScriptComponent sc;
			sc.enabled = enabled;
			sc.scriptPath = ExtractString(cblock, "scriptPath");
			obj.scripts.push_back(sc);
		} else if (type == "RectTransform") {
			RectTransformComponent rt;
			rt.enabled = enabled;
			auto p = ExtractArray(cblock, "pos");
			if (p.size() >= 2)
				rt.pos = {p[0], p[1]};
			auto s = ExtractArray(cblock, "size");
			if (s.size() >= 2)
				rt.size = {s[0], s[1]};
			auto a = ExtractArray(cblock, "anchor");
			if (a.size() >= 2)
				rt.anchor = {a[0], a[1]};
			auto pv = ExtractArray(cblock, "pivot");
			if (pv.size() >= 2)
				rt.pivot = {pv[0], pv[1]};
			rt.rotation = ExtractFloat(cblock, "rotation", 0.0f);
			obj.rectTransforms.push_back(rt);
		} else if (type == "UIImage") {
			UIImageComponent img;
			img.enabled = enabled;
			img.texturePath = ExtractString(cblock, "texturePath");
			if (renderer && !img.texturePath.empty())
				img.textureHandle = renderer->LoadTexture2D(img.texturePath);
			auto c = ExtractArray(cblock, "color");
			if (c.size() >= 4)
				img.color = {c[0], c[1], c[2], c[3]};
			obj.images.push_back(img);
		} else if (type == "UIText") {
			UITextComponent txt;
			txt.enabled = enabled;
			txt.text = UnescapeJson(ExtractString(cblock, "text"));
			txt.fontSize = ExtractFloat(cblock, "fontSize", 24.0f);
			auto c = ExtractArray(cblock, "color");
			if (c.size() >= 4)
				txt.color = {c[0], c[1], c[2], c[3]};
			obj.texts.push_back(txt);
		} else if (type == "UIButton") {
			UIButtonComponent btn;
			btn.enabled = enabled;
			auto nc = ExtractArray(cblock, "normalColor");
			if (nc.size() >= 4)
				btn.normalColor = {nc[0], nc[1], nc[2], nc[3]};
			auto hc = ExtractArray(cblock, "hoverColor");
			if (hc.size() >= 4)
				btn.hoverColor = {hc[0], hc[1], hc[2], hc[3]};
			auto pc = ExtractArray(cblock, "pressedColor");
			if (pc.size() >= 4)
				btn.pressedColor = {pc[0], pc[1], pc[2], pc[3]};
			obj.buttons.push_back(btn);
		} else if (type == "River") {
			RiverComponent rv;
			rv.enabled = enabled;
			if (cblock.find("\"width\"") != std::string::npos)
				rv.width = ExtractFloat(cblock, "width", 2.0f);
			if (cblock.find("\"flowSpeed\"") != std::string::npos)
				rv.flowSpeed = ExtractFloat(cblock, "flowSpeed", 1.0f);
			if (cblock.find("\"uvScale\"") != std::string::npos)
				rv.uvScale = ExtractFloat(cblock, "uvScale", 1.0f);
			rv.texturePath = ExtractString(cblock, "texture");
			if (rv.texturePath.empty())
				rv.texturePath = "Resources/Water/water.png";
			auto pts = ExtractArray(cblock, "points");
			for (size_t i = 0; i + 2 < pts.size(); i += 3) {
				rv.points.push_back({pts[i], pts[i + 1], pts[i + 2]});
			}
			obj.rivers.push_back(rv);
		}
		pos = endPos + 1;
	}
}
void EditorUI::LoadScene(GameScene* scene, const std::string& path) {
	if (!scene)
		return;

	std::string absPath = GetUnifiedProjectPath(path);

	std::ifstream f(absPath);
	if (!f.is_open()) {
		LogError("Load failed: " + absPath);
		return;
	}
	std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
	f.close();

	auto* renderer = Engine::Renderer::GetInstance();
	// ★追加: Scene Settings の読み込み
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
		// 最初の "{" の位置から探すため、FindBlockEndはobjStartから開始（FindBlockEnd内で"{"をカウントする）
		auto objEnd = FindBlockEnd(content, objStart);
		if (objEnd == std::string::npos || objEnd > arrEnd)
			break;
		std::string block = content.substr(objStart, objEnd - objStart + 1);
		SceneObject obj;
		obj.id = ExtractUint(block, "id", 0);
		obj.parentId = ExtractUint(block, "parentId", 0);
		if (obj.id == 0)
			obj.id = GenerateId();
		if (obj.id >= nextObjectId)
			nextObjectId = obj.id + 1;

		obj.name = ExtractString(block, "name");
		obj.modelPath = ExtractString(block, "modelPath");
		obj.texturePath = ExtractString(block, "texturePath");
		obj.extraTexturePaths = ExtractStringArray(block, "extraTexturePaths");
		obj.shaderName = ExtractString(block, "shaderName");
		if (obj.shaderName.empty())
			obj.shaderName = "Default";
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

	// ★追加: ロードされた川オブジェクトのメッシュを生成
	for (auto& obj : scene->objects_) {
		for (auto& rv : obj.rivers) {
			if (rv.enabled && rv.meshHandle == 0) {
				Game::RiverSystem::BuildRiverMesh(rv, renderer, scene->objects_);
			}
		}
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
		// 同じ名前がある場合はコピー名を生成（任意：上書きの方がいい場合もあるが安全のため）
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

	// ★追加: ロードされた川オブジェクトのメッシュを生成
	for (auto& obj : scene->objects_) {
		for (auto& rv : obj.rivers) {
			if (rv.enabled && rv.meshHandle == 0) {
				Game::RiverSystem::BuildRiverMesh(rv, renderer, scene->objects_);
			}
		}
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

	// 後方互換性：コンポーネントが無く、モデルがあればデフォルトを付与
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

// ====== ★ Ray-AABB 交差判定 ======
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
	// NDC座標に変換 [-1, 1]
	float ndcX = (screenX / imageW) * 2.0f - 1.0f;
	float ndcY = 1.0f - (screenY / imageH) * 2.0f; // Y反転

	XMMATRIX invProj = XMMatrixInverse(nullptr, proj);
	XMMATRIX invView = XMMatrixInverse(nullptr, view);

	// Near plane と Far plane のポイント
	XMVECTOR nearPoint = XMVector3TransformCoord(XMVectorSet(ndcX, ndcY, 0.0f, 1.0f), invProj);
	XMVECTOR farPoint = XMVector3TransformCoord(XMVectorSet(ndcX, ndcY, 1.0f, 1.0f), invProj);

	// ビュー空間・ワールド空間
	nearPoint = XMVector3TransformCoord(nearPoint, invView);
	farPoint = XMVector3TransformCoord(farPoint, invView);

	outOrig = nearPoint;
	outDir = XMVector3Normalize(XMVectorSubtract(farPoint, nearPoint));
}

// ★ ギズモ軸のRayヒット判定（ローカル空間に変換して判定）
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
			N[a] = 1.0f; // a=0: YZ平面, a=1: ZX平面, a=2: XY平面
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
		    {{-thickness, -thickness, 0}, {thickness, thickness, axisLen}}
        };

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

	// ★追加: アニメーションウィンドウの呼び出し
	ShowAnimationWindow(renderer, gameScene);

	// ★追加: Play Mode Monitor
	ShowPlayModeMonitor(gameScene);
	ShowPlayModeMonitor(gameScene); // ★追加: Play Mode Monitor

	// ====== Menu Bar ======
	if (ImGui::BeginMenuBar()) {
		if (ImGui::BeginMenu("File")) {
			// ★追加: 現在のシーン名を入力・表示するバッファ
			static char currentSceneName[128] = "scene.json";
			ImGui::InputText("Current Scene", currentSceneName, sizeof(currentSceneName));

			if (ImGui::MenuItem("Save Scene", "Ctrl+S")) {
				SaveScene(gameScene, std::string("Resources/") + currentSceneName);
			}

			if (ImGui::BeginMenu("Load Scene...")) {
				// Resourcesフォルダ内のjsonファイルを列挙
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
						PushUndo(
						    {"Delete Selection",
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
						     }});
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
	if (gameScene && gameHovered && tW > 0 && tH > 0) {
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

				// ★追加: 川配置モード中は地形クリックでポイント追加
				if (s_riverPlaceMode && gameScene->selectedObjectIndex_ >= 0) {
					auto& selObj = gameScene->objects_[gameScene->selectedObjectIndex_];
					if (s_riverPlaceCompIdx < (int)selObj.rivers.size()) {
						float closestDist = FLT_MAX;
						DirectX::XMFLOAT3 hitPt = {0, 0, 0};
						bool hitTerrain = false;
						for (const auto& obj2 : gameScene->objects_) {
							if (!obj2.gpuMeshColliders.empty()) {
								auto* model = Engine::Renderer::GetInstance()->GetModel(obj2.gpuMeshColliders[0].meshHandle);
								if (model) {
									Engine::Vector3 hp;
									float dist;
									if (model->RayCast(rayOrig, rayDir, obj2.GetTransform().ToMatrix(), dist, hp)) {
										if (dist < closestDist) {
											closestDist = dist;
											hitPt = {hp.x, hp.y, hp.z};
											hitTerrain = true;
										}
									}
								}
							}
						}
						if (hitTerrain) {
							selObj.rivers[s_riverPlaceCompIdx].points.push_back(hitPt);
						}
					}
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
						} else if (!io.KeyCtrl && !io.KeyShift && !uiDragging && !uiHoveredAny) {
							// UIハンドルをホバー中、または操作中（uiDragging）は選択解除しない
							gameScene->selectedIndices_.clear();
							gameScene->selectedObjectIndex_ = -1;
						}
					}
				}

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
					if (obj.locked)
						continue;

					Engine::Matrix4x4 mat = obj.GetTransform().ToMatrix();
					DirectX::XMMATRIX worldMat = DirectX::XMLoadFloat4x4(reinterpret_cast<DirectX::XMFLOAT4X4*>(&mat));
					DirectX::XMVECTOR det;
					DirectX::XMMATRIX invWorld = DirectX::XMMatrixInverse(&det, worldMat);

					DirectX::XMVECTOR localOrig = DirectX::XMVector3TransformCoord(rayOrig, invWorld);
					DirectX::XMVECTOR localTarget = DirectX::XMVector3TransformCoord(DirectX::XMVectorAdd(rayOrig, rayDir), invWorld);
					DirectX::XMVECTOR localDir = DirectX::XMVectorSubtract(localTarget, localOrig);

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
					PushUndo(
					    {"Delete Object (Ctrl+RightClick)",
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
					     }});
					Log("Deleted object: " + deletedObj.name);
				}
			} // if (ImGui::IsMouseClicked(Right)) の終了
		} // ★ 追加: if (insideImage) の終了

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

		// UIギズモの描画と操作
		uiHoveredAny = false;
		if (gameScene && gameScene->selectedObjectIndex_ >= 0 && gameScene->selectedObjectIndex_ < (int)gameScene->objects_.size() && !gameScene->IsPlaying()) {
			auto& selObj = gameScene->objects_[gameScene->selectedObjectIndex_];
			if (!selObj.rectTransforms.empty()) {
				// 解像度ズレを解消するため、バックバッファ解像度 (1920x1080) で計算してからスケーリングする
				float kW = (float)Engine::WindowDX::kW;
				float kH = (float)Engine::WindowDX::kH;
				auto wr = UISystem::CalculateWorldRect(selObj, gameScene->objects_, kW, kH);
				float scaleX = tW / kW;
				float scaleY = tH / kH;

				float cx = gameImageMin.x + wr.x * scaleX;
				float cy = gameImageMin.y + wr.y * scaleY;
				float cw = wr.w * scaleX;
				float ch = wr.h * scaleY;

				ImDrawList* dl = ImGui::GetWindowDrawList();
				ImU32 colLine = IM_COL32(50, 255, 50, 255);
				ImU32 colHandle = IM_COL32(200, 255, 200, 255);
				ImU32 colHandleHover = IM_COL32(255, 255, 255, 255);
				ImU32 colHitbox = IM_COL32(255, 150, 50, 255);
				ImU32 colHitHandle = IM_COL32(255, 180, 80, 255);

				// 枠線の描画 (Visual)
				dl->AddRect(ImVec2(cx, cy), ImVec2(cx + cw, cy + ch), colLine, 0.0f, 0, 2.0f);

				// ヒットボックス情報
				float hbxX = cx, hbxY = cy, hbxW = cw, hbxH = ch;
				bool hasButton = !selObj.buttons.empty();
				if (hasButton) {
					auto& btn = selObj.buttons[0];
					hbxW = cw * btn.hitboxScale.x;
					hbxH = ch * btn.hitboxScale.y;
					hbxX = cx + (cw * 0.5f) + btn.hitboxOffset.x * scaleX - hbxW * 0.5f;
					hbxY = cy + (ch * 0.5f) + btn.hitboxOffset.y * scaleY - hbxH * 0.5f;
					// 枠線の描画 (Hitbox)
					dl->AddRect(ImVec2(hbxX, hbxY), ImVec2(hbxX + hbxW, hbxY + hbxH), colHitbox, 0.0f, 0, 1.5f);
				}

				// ハンドルの定義: {x, y, cursor, type(0:Visual, 1:Hitbox)}
				struct HandleDef {
					float x, y;
					ImGuiMouseCursor cursor;
					int type;
				};
				std::vector<HandleDef> handles;
					// Visual Handles (0-8)
				handles.push_back({cx, cy, ImGuiMouseCursor_ResizeNWSE, 0});
				handles.push_back({cx + cw * 0.5f, cy, ImGuiMouseCursor_ResizeNS, 0});
				handles.push_back({cx + cw, cy, ImGuiMouseCursor_ResizeNESW, 0});
				handles.push_back({cx + cw, cy + ch * 0.5f, ImGuiMouseCursor_ResizeEW, 0});
				handles.push_back({cx + cw, cy + ch, ImGuiMouseCursor_ResizeNWSE, 0});
				handles.push_back({cx + cw * 0.5f, cy + ch, ImGuiMouseCursor_ResizeNS, 0});
				handles.push_back({cx, cy + ch, ImGuiMouseCursor_ResizeNESW, 0});
				handles.push_back({cx, cy + ch * 0.5f, ImGuiMouseCursor_ResizeEW, 0});
				handles.push_back({cx + cw * 0.5f, cy + ch * 0.5f, ImGuiMouseCursor_ResizeAll, 0});

				// Hitbox Handles (9-17)
				if (hasButton) {
					handles.push_back({hbxX, hbxY, ImGuiMouseCursor_ResizeNWSE, 1});
					handles.push_back({hbxX + hbxW * 0.5f, hbxY, ImGuiMouseCursor_ResizeNS, 1});
					handles.push_back({hbxX + hbxW, hbxY, ImGuiMouseCursor_ResizeNESW, 1});
					handles.push_back({hbxX + hbxW, hbxY + hbxH * 0.5f, ImGuiMouseCursor_ResizeEW, 1});
					handles.push_back({hbxX + hbxW, hbxY + hbxH, ImGuiMouseCursor_ResizeNWSE, 1});
					handles.push_back({hbxX + hbxW * 0.5f, hbxY + hbxH, ImGuiMouseCursor_ResizeNS, 1});
					handles.push_back({hbxX, hbxY + hbxH, ImGuiMouseCursor_ResizeNESW, 1});
					handles.push_back({hbxX, hbxY + hbxH * 0.5f, ImGuiMouseCursor_ResizeEW, 1});
					handles.push_back({hbxX + hbxW * 0.5f, hbxY + hbxH * 0.5f, ImGuiMouseCursor_ResizeAll, 1});
				}

				float handleSz = 6.0f;
				bool hoveredAny = false;
				int hoveredHandle = -1;
				if (insideImage && !gizmoDragging && !objectDragging && !uiDragging) {
					ImVec2 mpos = ImGui::GetMousePos();
					float hitDetectRad = handleSz * 1.5f;
					float bezelDetectWidth = 4.0f; // 枠線判定の太さ

					for (int i = 0; i < (int)handles.size(); ++i) {
						if (mpos.x >= handles[i].x - hitDetectRad && mpos.x <= handles[i].x + hitDetectRad && mpos.y >= handles[i].y - hitDetectRad && mpos.y <= handles[i].y + hitDetectRad) {
							hoveredHandle = i;
							hoveredAny = true;
							break;
						}
					}

					// ベゼル（枠線）判定
					if (hoveredHandle == -1) {
						// Visual Bezel
						bool onLeft = std::abs(mpos.x - cx) < bezelDetectWidth && mpos.y >= cy && mpos.y <= cy + ch;
						bool onRight = std::abs(mpos.x - (cx + cw)) < bezelDetectWidth && mpos.y >= cy && mpos.y <= cy + ch;
						bool onTop = std::abs(mpos.y - cy) < bezelDetectWidth && mpos.x >= cx && mpos.x <= cx + cw;
						bool onBottom = std::abs(mpos.y - (cy + ch)) < bezelDetectWidth && mpos.x >= cx && mpos.x <= cx + cw;

						if (onLeft && onTop) hoveredHandle = 0;
						else if (onRight && onTop) hoveredHandle = 2;
						else if (onRight && onBottom) hoveredHandle = 4;
						else if (onLeft && onBottom) hoveredHandle = 6;
						else if (onTop) hoveredHandle = 1;
						else if (onRight) hoveredHandle = 3;
						else if (onBottom) hoveredHandle = 5;
						else if (onLeft) hoveredHandle = 7;

						if (hoveredHandle == -1 && hasButton) {
							// Hitbox Bezel
							bool hOnLeft = std::abs(mpos.x - hbxX) < bezelDetectWidth && mpos.y >= hbxY && mpos.y <= hbxY + hbxH;
							bool hOnRight = std::abs(mpos.x - (hbxX + hbxW)) < bezelDetectWidth && mpos.y >= hbxY && mpos.y <= hbxY + hbxH;
							bool hOnTop = std::abs(mpos.y - hbxY) < bezelDetectWidth && mpos.x >= hbxX && mpos.x <= hbxX + hbxW;
							bool hOnBottom = std::abs(mpos.y - (hbxY + hbxH)) < bezelDetectWidth && mpos.x >= hbxX && mpos.x <= hbxX + hbxW;

							if (hOnLeft && hOnTop) hoveredHandle = 9;
							else if (hOnRight && hOnTop) hoveredHandle = 11;
							else if (hOnRight && hOnBottom) hoveredHandle = 13;
							else if (hOnLeft && hOnBottom) hoveredHandle = 15;
							else if (hOnTop) hoveredHandle = 10;
							else if (hOnRight) hoveredHandle = 12;
							else if (hOnBottom) hoveredHandle = 14;
							else if (hOnLeft) hoveredHandle = 16;
						}

						if (hoveredHandle != -1) hoveredAny = true;
					}

					// Center drag
					if (hoveredHandle == -1) {
						if (hasButton && mpos.x >= hbxX && mpos.x <= hbxX + hbxW && mpos.y >= hbxY && mpos.y <= hbxY + hbxH) {
							hoveredHandle = 17; // Hitbox Move
							hoveredAny = true;
						} else if (mpos.x >= cx && mpos.x <= cx + cw && mpos.y >= cy && mpos.y <= cy + ch) {
							hoveredHandle = 8; // Visual Move
							hoveredAny = true;
						}
					}
				}

				uiHoveredAny = hoveredAny;
				uiHoveredHandle = hoveredHandle;

				if (hoveredHandle != -1 && !uiDragging) {
					ImGui::SetMouseCursor(handles[hoveredHandle].cursor);
					if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
						uiDragging = true;
						uiDragHandle = hoveredHandle;
						uiDragStartPos = selObj.rectTransforms[0].pos;
						uiDragStartSize = selObj.rectTransforms[0].size;
						if (hasButton) {
							uiDragStartHitOffset = selObj.buttons[0].hitboxOffset;
							uiDragStartHitScale = selObj.buttons[0].hitboxScale;
						}
						gizmoDragStartMouse = ImGui::GetMousePos();
					}
				}

				if (uiDragging) {
					ImGui::SetMouseCursor(handles[uiDragHandle].cursor);
					ImVec2 mpos = ImGui::GetMousePos();
					float adx = (mpos.x - gizmoDragStartMouse.x) / scaleX; // バックバッファ座標系での移動量
					float ady = (mpos.y - gizmoDragStartMouse.y) / scaleY;

					auto& rt = selObj.rectTransforms[0];
					if (handles[uiDragHandle].type == 0) { // Visual Edit
						if (uiDragHandle == 8) {           // Move
							rt.pos.x = uiDragStartPos.x + adx;
							rt.pos.y = uiDragStartPos.y + ady;
						} else { // Resize
							float nX = uiDragStartPos.x, nY = uiDragStartPos.y, nW = uiDragStartSize.x, nH = uiDragStartSize.y;
							if (uiDragHandle == 0 || uiDragHandle == 6 || uiDragHandle == 7) {
								nX += adx;
								nW -= adx;
							}
							if (uiDragHandle == 2 || uiDragHandle == 3 || uiDragHandle == 4) {
								nW += adx;
							}
							if (uiDragHandle == 0 || uiDragHandle == 1 || uiDragHandle == 2) {
								nY += ady;
								nH -= ady;
							}
							if (uiDragHandle == 4 || uiDragHandle == 5 || uiDragHandle == 6) {
								nH += ady;
							}
							if (nW < 5.0f) {
								if (uiDragHandle == 0 || uiDragHandle == 6 || uiDragHandle == 7)
									nX -= (5.0f - nW);
								nW = 5.0f;
							}
							if (nH < 5.0f) {
								if (uiDragHandle == 0 || uiDragHandle == 1 || uiDragHandle == 2)
									nY -= (5.0f - nH);
								nH = 5.0f;
							}
							rt.pos = {nX, nY};
							rt.size = {nW, nH};
						}
					} else if (hasButton) { // Hitbox Edit (Handle index 9-17)
						auto& btn = selObj.buttons[0];
						int hIdx = uiDragHandle - 9;
						if (hIdx == 8) { // Center Move
							btn.hitboxOffset.x = uiDragStartHitOffset.x + adx;
							btn.hitboxOffset.y = uiDragStartHitOffset.y + ady;
						} else { // Resize
							float curW = uiDragStartSize.x * uiDragStartHitScale.x;
							float curH = uiDragStartSize.y * uiDragStartHitScale.y;
							float curOffX = uiDragStartHitOffset.x;
							float curOffY = uiDragStartHitOffset.y;

							if (hIdx == 0 || hIdx == 6 || hIdx == 7) {
								curOffX += adx * 0.5f;
								curW -= adx;
							}
							if (hIdx == 2 || hIdx == 3 || hIdx == 4) {
								curOffX += adx * 0.5f;
								curW += adx;
							}
							if (hIdx == 0 || hIdx == 1 || hIdx == 2) {
								curOffY += ady * 0.5f;
								curH -= ady;
							}
							if (hIdx == 4 || hIdx == 5 || hIdx == 6) {
								curOffY += ady * 0.5f;
								curH += ady;
							}

							if (curW < 5.0f)
								curW = 5.0f;
							if (curH < 5.0f)
								curH = 5.0f;
							btn.hitboxScale = {curW / uiDragStartSize.x, curH / uiDragStartSize.y};
							btn.hitboxOffset = {curOffX, curOffY};
						}
					}

					if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
						int idx = gameScene->selectedObjectIndex_;
						if (handles[uiDragHandle].type == 0) {
							auto sP = uiDragStartPos, sS = uiDragStartSize;
							auto eP = rt.pos, eS = rt.size;
							PushUndo(
							    {"UI Rect Edit",
							     [gameScene, idx, sP, sS]() {
								     if (idx < (int)gameScene->objects_.size() && !gameScene->objects_[idx].rectTransforms.empty()) {
									     gameScene->objects_[idx].rectTransforms[0].pos = sP;
									     gameScene->objects_[idx].rectTransforms[0].size = sS;
								     }
							     },
							     [gameScene, idx, eP, eS]() {
								     if (idx < (int)gameScene->objects_.size() && !gameScene->objects_[idx].rectTransforms.empty()) {
									     gameScene->objects_[idx].rectTransforms[0].pos = eP;
									     gameScene->objects_[idx].rectTransforms[0].size = eS;
								     }
							     }});
						} else if (hasButton) {
							auto sO = uiDragStartHitOffset, sS = uiDragStartHitScale;
							auto eO = selObj.buttons[0].hitboxOffset, eS = selObj.buttons[0].hitboxScale;
							PushUndo(
							    {"UI Hitbox Edit",
							     [gameScene, idx, sO, sS]() {
								     if (idx < (int)gameScene->objects_.size() && !gameScene->objects_[idx].buttons.empty()) {
									     gameScene->objects_[idx].buttons[0].hitboxOffset = sO;
									     gameScene->objects_[idx].buttons[0].hitboxScale = sS;
								     }
							     },
							     [gameScene, idx, eO, eS]() {
								     if (idx < (int)gameScene->objects_.size() && !gameScene->objects_[idx].buttons.empty()) {
									     gameScene->objects_[idx].buttons[0].hitboxOffset = eO;
									     gameScene->objects_[idx].buttons[0].hitboxScale = eS;
								     }
							     }});
						}
						uiDragging = false;
						uiDragHandle = -1;
					}
				}

				// ハンドル描画
				for (int i = 0; i < (int)handles.size(); ++i) {
					if (i == 8 || i == 17)
						continue; // Center handles are invisible
					ImU32 col = (hoveredHandle == i || uiDragHandle == i) ? colHandleHover : (handles[i].type == 0 ? colHandle : colHitHandle);
					dl->AddRectFilled(ImVec2(handles[i].x - handleSz, handles[i].y - handleSz), ImVec2(handles[i].x + handleSz, handles[i].y + handleSz), col);
				}
			}
		}

		// ドラッグがウィンドウ外に行った場合のリセット
		if ((gizmoDragging || objectDragging || uiDragging) && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
			gizmoDragging = false;
			gizmoDragAxis = -1;
			objectDragging = false;
			uiDragging = false;
		}

		if (gameScene && tH > 0.0f)
			gameScene->camera_.SetProjection(DirectX::XMConvertToRadians(45.0f), tW / tH, 0.1f, 1000.0f);

		// EditorUI内でGameScene::Draw()を呼ぶ際に、
		// 該当する範囲（ゲームビュー）の座標系にマウス位置を変換してUIに渡す
		if (gameScene && tW > 0.0f && tH > 0.0f) {
			float scaleX = (float)Engine::WindowDX::kW / tW;
			float scaleY = (float)Engine::WindowDX::kH / tH;
			gameScene->ctx_.useOverrideMouse = true;
			gameScene->ctx_.overrideMouseX = (mousePos.x - gameImageMin.x) * scaleX;
			gameScene->ctx_.overrideMouseY = (mousePos.y - gameImageMin.y) * scaleY;
		}
	} // ★ 修正: if (gameScene && gameHovered && tW > 0 && tH > 0) の終了 (IsPlaying除外)

	ImGui::End();
	ImGui::PopStyleVar();

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
			if (i < 0 || i >= (int)scene->objects_.size())
				return;
			if (rendered.count(scene->objects_[i].id))
				return;
			rendered.insert(scene->objects_[i].id);

			bool sel = scene->selectedIndices_.count(i) > 0;
			bool locked = scene->objects_[i].locked;

			ImGui::PushID(i);
			if (locked)
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0.4f, 0.4f, 1));
			if (ImGui::SmallButton(locked ? "L##lk" : "U##lk")) {
				scene->objects_[i].locked = !locked;
			}
			if (locked)
				ImGui::PopStyleColor();
			ImGui::SameLine();
			ImGui::PopID();

			std::string lb = (locked ? "[L] " : "") + scene->objects_[i].name + "##" + std::to_string(i);
			ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth;
			if (sel)
				flags |= ImGuiTreeNodeFlags_Selected;

			// 子オブジェクトがあるか確認
			bool hasChildren = false;
			for (const auto& o : scene->objects_)
				if (o.parentId == scene->objects_[i].id) {
					hasChildren = true;
					break;
				}
			if (!hasChildren)
				flags |= ImGuiTreeNodeFlags_Leaf;

			bool opened = ImGui::TreeNodeEx(lb.c_str(), flags);

			if (ImGui::IsItemClicked()) {
				if (io.KeyCtrl) {
					if (sel)
						scene->selectedIndices_.erase(i);
					else
						scene->selectedIndices_.insert(i);
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
	} else {
		ImGui::Text("No Active Scene");
	}
	ImGui::End();
}

// ====== Inspector ======
void EditorUI::ShowInspector(GameScene* scene) {
	ImGui::Begin("Inspector");
	if (scene && scene->selectedObjectIndex_ >= 0 && scene->selectedObjectIndex_ < (int)scene->objects_.size()) {
		auto& obj = scene->objects_[scene->selectedObjectIndex_];

		// Name
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

		// ID & Parent & Lock & Save
		ImGui::Text("ID: %u", obj.id);
		ImGui::SameLine();
		ImGui::SetNextItemWidth(120);
		auto oldParentId = obj.parentId;
		if (ImGui::InputScalar("Parent ID", ImGuiDataType_U32, &obj.parentId)) {
			auto newParentId = obj.parentId;
			int i = scene->selectedObjectIndex_;
			PushUndo(
			    {"Change Parent",
			     [scene, i, oldParentId]() {
				     if (i < (int)scene->objects_.size())
					     scene->objects_[i].parentId = oldParentId;
			     },
			     [scene, i, newParentId]() {
				     if (i < (int)scene->objects_.size())
					     scene->objects_[i].parentId = newParentId;
			     }});
		}
		ImGui::SameLine();
		ImGui::Checkbox("Lock", &obj.locked);
		ImGui::SameLine();
		if (ImGui::Button("Save Prefab")) {
			std::string ppath = "Resources/" + obj.name + ".prefab";
			std::ofstream pf(ppath);
			if (pf.is_open()) {
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

		if (obj.locked || scene->IsPlaying())
			ImGui::BeginDisabled();

		ImGui::Separator();
		ImGui::Text("Transform");
		{
			auto old = obj.translate;
			if (ImGui::DragFloat3("Position", &obj.translate.x, 0.1f)) {
			}
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
			if (ImGui::DragFloat3("Rotation", &obj.rotate.x, 0.01f)) {
			}
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
			if (ImGui::DragFloat3("Scale", &obj.scale.x, 0.01f)) {
			}
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

		if (obj.locked || scene->IsPlaying())
			ImGui::EndDisabled();

		ImGui::Separator();
		if (ImGui::CollapsingHeader("Components", ImGuiTreeNodeFlags_DefaultOpen)) {
			// MeshRenderer
			for (size_t ci = 0; ci < obj.meshRenderers.size(); ++ci) {
				auto& mr = obj.meshRenderers[ci];
				ImGui::PushID((int)ci + 100);
				if (ImGui::TreeNode("MeshRenderer")) {
					bool prevEnabled = mr.enabled;
					if (ImGui::Checkbox("Enabled", &mr.enabled)) {
						bool newVal = mr.enabled;
						int i = scene->selectedObjectIndex_;
						PushUndo(
						    {"MeshRenderer Enabled",
						     [scene, i, ci, prevEnabled]() {
							     if (i < (int)scene->objects_.size() && ci < scene->objects_[i].meshRenderers.size())
								     scene->objects_[i].meshRenderers[ci].enabled = prevEnabled;
						     },
						     [scene, i, ci, newVal]() {
							     if (i < (int)scene->objects_.size() && ci < scene->objects_[i].meshRenderers.size())
								     scene->objects_[i].meshRenderers[ci].enabled = newVal;
						     }});
					}
					auto prevColor = mr.color;
					ImGui::ColorEdit4("Color", &mr.color.x);
					if (ImGui::IsItemDeactivatedAfterEdit()) {
						auto newColor = mr.color;
						int i = scene->selectedObjectIndex_;
						PushUndo(
						    {"MeshRenderer Color",
						     [scene, i, ci, prevColor]() {
							     if (i < (int)scene->objects_.size() && ci < scene->objects_[i].meshRenderers.size())
								     scene->objects_[i].meshRenderers[ci].color = prevColor;
						     },
						     [scene, i, ci, newColor]() {
							     if (i < (int)scene->objects_.size() && ci < scene->objects_[i].meshRenderers.size())
								     scene->objects_[i].meshRenderers[ci].color = newColor;
						     }});
					}
					if (ImGui::Button("Remove")) {
						obj.meshRenderers.erase(obj.meshRenderers.begin() + ci);
						ImGui::TreePop();
						ImGui::PopID();
						goto end_comp;
					}
					ImGui::TreePop();
				}
				ImGui::PopID();
			}
			// BoxCollider
			for (size_t ci = 0; ci < obj.boxColliders.size(); ++ci) {
				auto& bc = obj.boxColliders[ci];
				ImGui::PushID(1000 + (int)ci);
				if (ImGui::TreeNode("BoxCollider")) {
					ImGui::Checkbox("Enabled", &bc.enabled);
					ImGui::DragFloat3("Center", &bc.center.x, 0.1f);
					ImGui::DragFloat3("Size", &bc.size.x, 0.1f);
					ImGui::Checkbox("Is Trigger", &bc.isTrigger);
					if (ImGui::Button("Remove")) {
						obj.boxColliders.erase(obj.boxColliders.begin() + ci);
						ImGui::TreePop();
						ImGui::PopID();
						goto end_comp;
					}
					ImGui::TreePop();
				}
				ImGui::PopID();
			}
			// Rigidbody
			for (size_t ci = 0; ci < obj.rigidbodies.size(); ++ci) {
				auto& rb = obj.rigidbodies[ci];
				ImGui::PushID(4000 + (int)ci);
				if (ImGui::TreeNode("Rigidbody")) {
					ImGui::Checkbox("Enabled", &rb.enabled);
					ImGui::DragFloat3("Velocity", &rb.velocity.x, 0.1f);
					ImGui::Checkbox("Use Gravity", &rb.useGravity);
					ImGui::Checkbox("Is Kinematic", &rb.isKinematic);
					if (ImGui::Button("Remove")) {
						obj.rigidbodies.erase(obj.rigidbodies.begin() + ci);
						ImGui::TreePop();
						ImGui::PopID();
						goto end_comp;
					}
					ImGui::TreePop();
				}
				ImGui::PopID();
			}
			// AudioSource
			for (size_t ci = 0; ci < obj.audioSources.size(); ++ci) {
				auto& as = obj.audioSources[ci];
				ImGui::PushID(13000 + (int)ci);
				if (ImGui::TreeNode("AudioSource")) {
					ImGui::Checkbox("Enabled", &as.enabled);
					ImGui::Text("Clip: %s", as.soundPath.empty() ? "(none)" : as.soundPath.c_str());
					ImGui::Checkbox("Loop", &as.loop);
					ImGui::Checkbox("Play on Start", &as.playOnStart);
					if (ImGui::Button("Remove")) {
						obj.audioSources.erase(obj.audioSources.begin() + ci);
						ImGui::TreePop();
						ImGui::PopID();
						goto end_comp;
					}
					ImGui::TreePop();
				}
				ImGui::PopID();
			}
			// RectTransform
			for (size_t ci = 0; ci < obj.rectTransforms.size(); ++ci) {
				auto& rt = obj.rectTransforms[ci];
				ImGui::PushID(15000 + (int)ci);
				if (ImGui::TreeNode("RectTransform")) {
					ImGui::Checkbox("Enabled", &rt.enabled);
					ImGui::DragFloat2("Pos", &rt.pos.x, 1.0f);
					ImGui::DragFloat2("Size", &rt.size.x, 1.0f);
					ImGui::DragFloat2("Anchor", &rt.anchor.x, 0.01f, 0.0f, 1.0f);
					if (ImGui::Button("Remove")) {
						obj.rectTransforms.erase(obj.rectTransforms.begin() + ci);
						ImGui::TreePop();
						ImGui::PopID();
						goto end_comp;
					}
					ImGui::TreePop();
				}
				ImGui::PopID();
			}
			// UIImage
			for (size_t ci = 0; ci < obj.images.size(); ++ci) {
				auto& img = obj.images[ci];
				ImGui::PushID(16000 + (int)ci);
				if (ImGui::TreeNode("UIImage")) {
					ImGui::Checkbox("Enabled", &img.enabled);
					ImGui::ColorEdit4("Color", &img.color.x);

					// Path表示とドラッグ＆ドロップ対応
					ImGui::Text("Path: %s", img.texturePath.empty() ? "(none)" : img.texturePath.c_str());
					if (ImGui::BeginDragDropTarget()) {
						if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("RESOURCE_PATH")) {
							std::string path((const char*)pl->Data, pl->DataSize - 1);
							// 画像拡張子の簡易チェック
							if (path.find(".png") != std::string::npos || path.find(".jpg") != std::string::npos || path.find(".jpeg") != std::string::npos) {
								std::string oldPath = img.texturePath;
								img.texturePath = path;
								img.textureHandle = Engine::Renderer::GetInstance()->LoadTexture2D(path);

								// Undo登録
								int idx = scene->selectedObjectIndex_;
								PushUndo(
								    {"Change UIImage Texture",
								     [scene, idx, ci, oldPath]() {
									     if (idx < (int)scene->objects_.size() && ci < scene->objects_[idx].images.size()) {
										     scene->objects_[idx].images[ci].texturePath = oldPath;
										     if (!oldPath.empty())
											     scene->objects_[idx].images[ci].textureHandle = Engine::Renderer::GetInstance()->LoadTexture2D(oldPath);
									     }
								     },
								     [scene, idx, ci, path]() {
									     if (idx < (int)scene->objects_.size() && ci < scene->objects_[idx].images.size()) {
										     scene->objects_[idx].images[ci].texturePath = path;
										     scene->objects_[idx].images[ci].textureHandle = Engine::Renderer::GetInstance()->LoadTexture2D(path);
									     }
								     }});
							}
						}
						ImGui::EndDragDropTarget();
					}

					if (ImGui::Button("Remove")) {
						obj.images.erase(obj.images.begin() + ci);
						ImGui::TreePop();
						ImGui::PopID();
						goto end_comp;
					}
					ImGui::TreePop();
				}
				ImGui::PopID();
			}
			// UIButton
			for (size_t ci = 0; ci < obj.buttons.size(); ++ci) {
				auto& btn = obj.buttons[ci];
				ImGui::PushID(17000 + (int)ci);
				if (ImGui::TreeNode("UIButton")) {
					ImGui::Checkbox("Enabled", &btn.enabled);
					ImGui::DragFloat2("HitOffset", &btn.hitboxOffset.x, 1.0f);
					ImGui::DragFloat2("HitScale", &btn.hitboxScale.x, 0.01f);
					if (ImGui::Button("Remove")) {
						obj.buttons.erase(obj.buttons.begin() + ci);
						ImGui::TreePop();
						ImGui::PopID();
						goto end_comp;
					}
					ImGui::TreePop();
				}
				ImGui::PopID();
			}

			// River
			for (size_t ci = 0; ci < obj.rivers.size(); ++ci) {
				auto& rv = obj.rivers[ci];
				ImGui::PushID(18000 + (int)ci);
				if (ImGui::TreeNode("River")) {
					ImGui::Checkbox("Enabled", &rv.enabled);
					ImGui::DragFloat("Width", &rv.width, 0.1f);
					ImGui::DragFloat("Flow Speed", &rv.flowSpeed, 0.1f);
					ImGui::DragFloat("UV Scale", &rv.uvScale, 0.1f);

					if (ImGui::Button("Add Point")) {
						if (rv.points.empty()) {
							rv.points.push_back({obj.translate.x, obj.translate.y, obj.translate.z});
						} else {
							rv.points.push_back(rv.points.back());
						}
					}
					ImGui::SameLine();
					if (ImGui::Button("Capture Position")) {
						rv.points.push_back({obj.translate.x, obj.translate.y, obj.translate.z});
					}
					if (ImGui::Button("Build Mesh")) {
						Game::RiverSystem::BuildRiverMesh(rv, Engine::Renderer::GetInstance(), scene->objects_);
					}
					// ★追加: 地形クリックでポイント配置モード
					bool isThisRiverPlacing = s_riverPlaceMode && s_riverPlaceCompIdx == (int)ci;
					if (ImGui::Checkbox("Place Points Mode##River", &isThisRiverPlacing)) {
						s_riverPlaceMode = isThisRiverPlacing;
						s_riverPlaceCompIdx = (int)ci;
					}
					if (isThisRiverPlacing) {
						ImGui::TextColored({0, 1, 0, 1}, "Click on terrain to place points");
					}
					if (ImGui::Button("Remove##River")) {
						obj.rivers.erase(obj.rivers.begin() + ci);
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
				ImGui::PushID(19000 + (int)ci);
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
				ImGui::PushID(20000 + (int)ci);
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
				ImGui::PushID(21000 + (int)ci);
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
				ImGui::PushID(22000 + (int)ci);
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

			for (size_t ci = 0; ci < obj.scripts.size(); ++ci) {
				auto& sc = obj.scripts[ci];
				ImGui::PushID(23000 + (int)ci);
				if (ImGui::TreeNode("Script")) {
					ImGui::Checkbox("Enabled##SC", &sc.enabled);
					char pathBuf[256];
					strcpy_s(pathBuf, sc.scriptPath.c_str());
					if (ImGui::InputText("Class Name##SC", pathBuf, sizeof(pathBuf)))
						sc.scriptPath = pathBuf;
					ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "(e.g., PlayerScript)");

					if (ImGui::Button("Open in VS Code")) {
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
			ImGui::Separator();
			if (ImGui::Button("Add Component"))
				ImGui::OpenPopup("AddComp");
			if (ImGui::BeginPopup("AddComp")) {
				if (ImGui::MenuItem("MeshRenderer"))
					obj.meshRenderers.push_back({});
				if (ImGui::MenuItem("BoxCollider"))
					obj.boxColliders.push_back({});
				if (ImGui::MenuItem("Tag"))
					obj.tags.push_back({});
				if (ImGui::MenuItem("Animator"))
					obj.animators.push_back({});
				if (ImGui::MenuItem("Rigidbody"))
					obj.rigidbodies.push_back({});
				if (ImGui::MenuItem("ParticleEmitter")) {
					ParticleEmitterComponent pe;
					pe.emitter.Initialize(*Engine::Renderer::GetInstance(), "NewEmitter");
					obj.particleEmitters.push_back(pe);
				}
				if (ImGui::MenuItem("GpuMeshCollider")) {
					GpuMeshColliderComponent gmc;
					gmc.meshHandle = obj.modelHandle;
					gmc.meshPath = obj.modelPath;
					obj.gpuMeshColliders.push_back(gmc);
				}
				if (ImGui::MenuItem("PlayerInput"))
					obj.playerInputs.push_back({});
				if (ImGui::MenuItem("CharacterMovement"))
					obj.characterMovements.push_back({});
				if (ImGui::MenuItem("CameraTarget"))
					obj.cameraTargets.push_back({});
				ImGui::Separator();
				if (ImGui::MenuItem("DirectionalLight"))
					obj.directionalLights.push_back({});
				if (ImGui::MenuItem("PointLight"))
					obj.pointLights.push_back({});
				if (ImGui::MenuItem("SpotLight"))
					obj.spotLights.push_back({});
				ImGui::Separator();
				if (ImGui::MenuItem("AudioSource"))
					obj.audioSources.push_back({});
				if (ImGui::MenuItem("AudioListener"))
					obj.audioListeners.push_back({});
				ImGui::Separator();
				if (ImGui::MenuItem("Hitbox"))
					obj.hitboxes.push_back({});
				if (ImGui::MenuItem("Hurtbox"))
					obj.hurtboxes.push_back({});
				ImGui::Separator();
				if (ImGui::MenuItem("RectTransform"))
					obj.rectTransforms.push_back({});
				if (ImGui::MenuItem("UIImage"))
					obj.images.push_back({});
				if (ImGui::MenuItem("UIButton"))
					obj.buttons.push_back({});
				if (ImGui::MenuItem("River")) {
					RiverComponent rv;
					rv.points.push_back({obj.translate.x, obj.translate.y, obj.translate.z});
					rv.points.push_back({obj.translate.x, obj.translate.y, obj.translate.z + 50.0f});
					obj.rivers.push_back(rv);
				}
				if (ImGui::MenuItem("UIText"))
					obj.texts.push_back({});
				ImGui::Separator();
				if (ImGui::MenuItem("Health"))
					obj.healths.push_back({});
				if (ImGui::MenuItem("Script"))
					obj.scripts.push_back({});
				ImGui::EndPopup();
			}
		}



		ImGui::Separator();
		const char* mn[] = {"Translate (T)", "Rotate (R)", "Scale (S)"};
		ImGui::Text("Gizmo: %s", mn[(int)currentGizmoMode]);
		if (scene->selectedIndices_.size() > 1)
			ImGui::Text("(%d selected)", (int)scene->selectedIndices_.size());
	} else {
		ImGui::Text("No Object Selected");
	}
	ImGui::End();
}

// ====== Project ======
void EditorUI::ShowProject(Engine::Renderer* renderer, GameScene* scene) {
	(void)scene;
	ImGui::Begin("Project");

	// 静的な変数: フォルダナビゲーション・キャッシュ・音声再生
	static std::string currentDir = "Resources";
	static std::map<std::string, Engine::Renderer::TextureHandle> thumbnailCache;
	static float iconSize = 80.0f;
	static uint32_t playingSoundHandle = 0xFFFFFFFF;
	static size_t playingVoiceHandle = 0;
	static std::string playingAudioPath;

	// ファイル操作用の状態変数
	static bool renaming = false;
	static std::string renamingPath; // 名前変更対象のフルパス
	static char renameBuffer[256] = {};
	static bool showDeleteConfirm = false;
	static std::string deletingPath; // 削除対象のフルパス
	static std::string deletingName; // 削除対象の表示名
	static bool creatingFolder = false;
	static char newFolderNameBuf[256] = {};

	// 追加: スクリプト作成用の状態変数
	static bool creatingScript = false;
	static char newScriptNameBuf[256] = "NewScript";

	if (!fs::exists(currentDir))
		currentDir = "Resources";

	// --- パンくずリスト ---
	{
		std::string accumulated;
		std::string remaining = currentDir;
		// "Resources" をルートとして分割表示
		std::istringstream iss(remaining);
		std::string token;
		bool first = true;
		while (std::getline(iss, token, '\\')) {
			// '/' でも分割
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

	// ツールバー: + ボタン (新規フォルダ作成)
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

	// --- 新規フォルダ作成ダイアログ ---
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

	// --- C++スクリプト作成ダイアログ ---
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
				// Game/ フォルダにヘッダーとソースを生成
				// 修正: 実行ディレクトリ(x64/Debug等)にGameフォルダがない場合、親ディレクトリを探索して正しいパスを特定する
				std::string gameDir = "Game";
				if (!fs::exists(gameDir)) {
					if (fs::exists("../../Game"))
						gameDir = "../../Game";
					else if (fs::exists("../Game"))
						gameDir = "../Game";
				}
				if (!fs::exists(gameDir))
					fs::create_directories(gameDir);

				std::string hPath = gameDir + "/" + className + ".h";
				std::string cppPath = gameDir + "/" + className + ".cpp";

				if (!fs::exists(hPath) && !fs::exists(cppPath)) {
					// ヘッダーファイル
					{
						std::ofstream f(hPath);
						if (f.is_open()) {
							f << "#pragma once\n";
							f << "#include \"IScript.h\"\n\n";
							f << "namespace Game {\n\n";
							f << "class " << className << " : public IScript {\n";
							f << "public:\n";
							f << "\t// 初期化処理（シーン開始時に1回呼ばれる）\n";
							f << "\tvoid Start(SceneObject& obj, GameScene* scene) override;\n\n";
							f << "\t// 毎フレーム処理\n";
							f << "\tvoid Update(SceneObject& obj, GameScene* scene, float dt) override;\n\n";
							f << "\t// オブジェクト破棄時の処理\n";
							f << "\tvoid OnDestroy(SceneObject& obj, GameScene* scene) override;\n";
							f << "};\n\n";
							f << "} // namespace Game\n";
							f.close();
						} else {
							LogError("Failed to write header: " + hPath);
						}
					}
					// ソースファイル
					{
						std::ofstream f(cppPath);
						if (f.is_open()) {
							f << "#include \"" << className << ".h\"\n";
							f << "#include \"ObjectTypes.h\"\n";
							f << "#include \"Scenes/GameScene.h\"\n";
							f << "#include \"ScriptEngine.h\"\n\n";
							f << "namespace Game {\n\n";
							f << "void " << className << "::Start(SceneObject& /*obj*/, GameScene* /*scene*/) {\n";
							f << "\t// ここに初期設定を記述\n";
							f << "}\n\n";
							f << "void " << className << "::Update(SceneObject& obj, GameScene* scene, float dt) {\n";
							f << "\t// ここに毎フレームの挙動を記述\n";
							f << "}\n\n";
							f << "void " << className << "::OnDestroy(SceneObject& /*obj*/, GameScene* /*scene*/) {\n";
							f << "\t// 終了時のクリーンアップなどを記述\n";
							f << "}\n\n";
							f << "// スクリプト自動登録\n";
							f << "REGISTER_SCRIPT(" << className << ");\n\n";
							f << "} // namespace Game\n";
							f.close();
						} else {
							LogError("Failed to write source: " + cppPath);
						}
					}
					Log("Script created: " + hPath + " / " + cppPath);
					// VS Codeで開く
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

	// --- 名前変更インラインUI ---
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
						// サムネイルキャッシュの更新
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

	// --- 削除確認ダイアログ ---
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
				// サムネイルキャッシュのクリア
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

	// --- ファイル一覧を取得 ---
	struct ProjectEntry {
		std::string path; // フルパス
		std::string name; // ファイル名のみ
		bool isDir = false;
		std::string ext; // 小文字拡張子
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
				// 小文字化
				for (auto& c : pe.ext)
					c = (char)std::tolower((unsigned char)c);
			}
			entries.push_back(pe);
		}
	}

	// ソート: フォルダ先、ファイル後
	std::sort(entries.begin(), entries.end(), [](const ProjectEntry& a, const ProjectEntry& b) {
		if (a.isDir != b.isDir)
			return a.isDir > b.isDir;
		return a.name < b.name;
	});

	// --- 「..」上位フォルダボタン ---
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

	// --- アイコングリッド ---
	float panelWidth = ImGui::GetContentRegionAvail().x;
	float cellWidth = iconSize + 12.0f;
	int columns = (int)(panelWidth / cellWidth);
	if (columns < 1)
		columns = 1;
	int col = (currentDir != "Resources") ? 1 : 0; // 「..」ボタンの分

	for (size_t ei = 0; ei < entries.size(); ++ei) {
		auto& pe = entries[ei];
		ImGui::PushID((int)ei);

		bool isTexture = (pe.ext == ".png" || pe.ext == ".jpg" || pe.ext == ".jpeg" || pe.ext == ".bmp");
		bool isModel = (pe.ext == ".obj" || pe.ext == ".gltf" || pe.ext == ".fbx");
		bool isAudio = (pe.ext == ".mp3" || pe.ext == ".wav" || pe.ext == ".ogg");
		bool isPrefab = (pe.ext == ".prefab");
		bool isScript = (pe.ext == ".cpp" || pe.ext == ".h"); // 追加

		// グリッドレイアウト
		if (col > 0 && col < columns)
			ImGui::SameLine();
		else if (col >= columns)
			col = 0;

		ImGui::BeginGroup();

		if (pe.isDir) {
			// フォルダ: 黄色っぽいボタン
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.35f, 0.30f, 0.15f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.50f, 0.45f, 0.20f, 1.0f));
			if (ImGui::Button("##dir", ImVec2(iconSize, iconSize))) {
				currentDir = pe.path;
			}
			ImGui::PopStyleColor(2);

			// フォルダへのドラッグ＆ドロップ受け入れ（ファイル移動）
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
							// サムネイルキャッシュの移動
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
				// フォルダのドラッグ＆ドロップ移動にも対応
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

			// フォルダアイコンのテキスト
			ImVec2 bmin = ImGui::GetItemRectMin();
			ImVec2 bmax = ImGui::GetItemRectMax();
			float cx = (bmin.x + bmax.x) * 0.5f;
			float cy = (bmin.y + bmax.y) * 0.5f;
			ImGui::GetWindowDrawList()->AddText(ImVec2(cx - 8, cy - 10), IM_COL32(255, 220, 80, 255), "D");

			// フォルダのドラッグ＆ドロップソース
			if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
				ImGui::SetDragDropPayload("RESOURCE_DIR", pe.path.c_str(), pe.path.size() + 1);
				ImGui::Text("[Dir] %s", pe.name.c_str());
				ImGui::EndDragDropSource();
			}
		} else if (isTexture) {
			// テクスチャ: サムネイルプレビュー
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
			// モデル: アイコン
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
			// 音声: 再生/停止ボタン付きアイコン
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
						// 前の再生を停止
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
			// Prefab: 青緑アイコン
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
			// C++スクリプト: 紫アイコン
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
			// その他ファイル: グレーアイコン
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.25f, 0.25f, 1.0f));
			ImGui::Button("##file", ImVec2(iconSize, iconSize));
			ImGui::PopStyleColor();
			ImVec2 bmin = ImGui::GetItemRectMin();
			ImVec2 bmax = ImGui::GetItemRectMax();
			float cx = (bmin.x + bmax.x) * 0.5f;
			float cy = (bmin.y + bmax.y) * 0.5f;
			ImGui::GetWindowDrawList()->AddText(ImVec2(cx - 6, cy - 10), IM_COL32(180, 180, 180, 255), "F");
		}

		// 追加: スクリプトやテキストファイルをダブルクリックでVS Codeで開く
		if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
			if (isScript || pe.ext == ".json" || pe.ext == ".txt") {
				std::string cmd = "code \"" + pe.path + "\"";
				system(cmd.c_str());
			}
		}

		// ドラッグ＆ドロップソース (ファイルのみ ・ フォルダは上で別途処理)
		if (!pe.isDir && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
			ImGui::SetDragDropPayload("RESOURCE_PATH", pe.path.c_str(), pe.path.size() + 1);
			ImGui::Text("%s", pe.name.c_str());
			ImGui::EndDragDropSource();
		}

		// 右クリックコンテキストメニュー (アイテム上)
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

		// ファイル名 (切り詰めて表示)
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

		// ツールチップ (フルパス)
		if (ImGui::IsItemHovered()) {
			ImGui::BeginTooltip();
			ImGui::Text("%s", pe.path.c_str());
			ImGui::EndTooltip();
		}

		ImGui::EndGroup();
		col++;

		ImGui::PopID();
	}

	// 背景の右クリックメニュー（何もない場所で右クリック）
	if (ImGui::BeginPopupContextWindow("##bgCtx", ImGuiPopupFlags_NoOpenOverItems | ImGuiPopupFlags_MouseButtonRight)) {
		if (ImGui::MenuItem("Create Folder")) {
			creatingFolder = true;
			memset(newFolderNameBuf, 0, sizeof(newFolderNameBuf));
			strncpy_s(newFolderNameBuf, "NewFolder", sizeof(newFolderNameBuf) - 1);
		}
		if (ImGui::BeginMenu("Create File")) {
			// 追加: C++スクリプト作成ボタン
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
			// サムネイルキャッシュクリア（再読み込み）
			thumbnailCache.clear();
			Log("Refreshed project view.");
		}
		ImGui::EndPopup();
	}

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

	// FPS
	float fps = ImGui::GetIO().Framerate;
	ImVec4 col = {0.0f, 1.0f, 0.0f, 1.0f};
	if (fps < 55.0f)
		col = {1.0f, 1.0f, 0.0f, 1.0f};
	if (fps < 30.0f)
		col = {1.0f, 0.0f, 0.0f, 1.0f};
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

// ====== Scene Settings ======
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

// ====== Console ======
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

} // namespace Game
