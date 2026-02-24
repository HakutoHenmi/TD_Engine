#include "EditorUI.h"
#include "../../externals/imgui/imgui.h"
#include "../../externals/imgui/imgui_internal.h"
#include "../Scenes/GameScene.h"
#include "SceneManager.h"
#include "WindowDX.h"
#include <filesystem>
#include <string>
#include <deque>
#include <cmath>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cfloat>
#include <map>
#include <cctype>

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
int gizmoDragAxis = -1;       // 0=X, 1=Y, 2=Z
static std::map<int, Engine::Transform> dragStartTransforms = {};
static ImVec2 gizmoDragStartMouse = {};
static bool objectDragging = false;  // ★ 自由ドラッグ中フラグ
static std::vector<SceneObject> clipboardObjects; // Ctrl+C コピー用

// ★ Gameウィンドウの画像座標 (ピッキング用)
static ImVec2 gameImageMin = {};
static ImVec2 gameImageMax = {};

// ★ コピー/複製時のユニーク名生成
static std::string GenerateCopyName(const std::string& baseName, const std::vector<SceneObject>& objects) {
	// 末尾の "_数字" や " (Copy)" を除去してベース名を取得
	std::string base = baseName;
	// " (Copy)" の繰り返しを除去
	while (base.size() > 7 && base.substr(base.size() - 7) == " (Copy)") base = base.substr(0, base.size() - 7);
	// "_数字" を除去
	{
		auto pos = base.rfind('_');
		if (pos != std::string::npos && pos + 1 < base.size()) {
			bool allDigit = true;
			for (size_t i = pos + 1; i < base.size(); ++i) if (!isdigit((unsigned char)base[i])) { allDigit = false; break; }
			if (allDigit) base = base.substr(0, pos);
		}
	}
	if (base.empty()) base = "Object";
	// 既存のオブジェクト名から最大番号を探す
	int maxNum = 0;
	for (const auto& obj : objects) {
		if (obj.name.size() > base.size() + 1 && obj.name.substr(0, base.size()) == base && obj.name[base.size()] == '_') {
			std::string numPart = obj.name.substr(base.size() + 1);
			bool allDigit = true;
			for (char c : numPart) if (!isdigit((unsigned char)c)) { allDigit = false; break; }
			if (allDigit && !numPart.empty()) { int n = std::stoi(numPart); if (n > maxNum) maxNum = n; }
		}
	}
	return base + "_" + std::to_string(maxNum + 1);
}

// ====== Undo/Redo ======
void EditorUI::PushUndo(const UndoCommand& cmd) { undoStack.push_back(cmd); if (undoStack.size()>kMaxUndoDepth) undoStack.pop_front(); redoStack.clear(); }
void EditorUI::Undo() { if(undoStack.empty()) return; auto c=undoStack.back(); undoStack.pop_back(); c.undo(); redoStack.push_back(c); }
void EditorUI::Redo() { if(redoStack.empty()) return; auto c=redoStack.back(); redoStack.pop_back(); c.redo(); undoStack.push_back(c); }

// ====== Console ======
void EditorUI::Log(const std::string& msg) { consoleLog.push_back({LogLevel::Info, msg, globalTime}); if(consoleLog.size()>kMaxConsoleLines) consoleLog.pop_front(); }
void EditorUI::LogWarning(const std::string& msg) { consoleLog.push_back({LogLevel::Warning, msg, globalTime}); if(consoleLog.size()>kMaxConsoleLines) consoleLog.pop_front(); }
void EditorUI::LogError(const std::string& msg) { consoleLog.push_back({LogLevel::Error, msg, globalTime}); if(consoleLog.size()>kMaxConsoleLines) consoleLog.pop_front(); }

// ====== JSON Save ======
static std::string EscapeJson(const std::string& s) { std::string o; for(char c:s){if(c=='"')o+="\\\""; else if(c=='\\')o+="\\\\"; else o+=c;} return o; }

static std::string SerializeSceneObject(const SceneObject& o) {
	std::stringstream ss;
	ss<<"    {\n";
	ss<<"      \"name\": \""<<EscapeJson(o.name)<<"\",\n";
	ss<<"      \"locked\": "<<(o.locked?"true":"false")<<",\n";
	ss<<"      \"modelPath\": \""<<EscapeJson(o.modelPath)<<"\",\n";
	ss<<"      \"texturePath\": \""<<EscapeJson(o.texturePath)<<"\",\n";
	ss<<"      \"translate\": ["<<o.translate.x<<", "<<o.translate.y<<", "<<o.translate.z<<"],\n";
	ss<<"      \"rotate\": ["<<o.rotate.x<<", "<<o.rotate.y<<", "<<o.rotate.z<<"],\n";
	ss<<"      \"scale\": ["<<o.scale.x<<", "<<o.scale.y<<", "<<o.scale.z<<"],\n";
	ss<<"      \"color\": ["<<o.color.x<<", "<<o.color.y<<", "<<o.color.z<<", "<<o.color.w<<"],\n";
	ss<<"      \"components\": [\n";
	bool first = true;
	for(const auto& mr : o.meshRenderers) {
		if(!first) ss<<",\n"; first = false;
		ss<<"        {\"type\": \"MeshRenderer\", \"enabled\": "<<(mr.enabled?"true":"false")<<", \"modelPath\": \""<<EscapeJson(mr.modelPath)<<"\", \"texturePath\": \""<<EscapeJson(mr.texturePath)<<"\", \"color\": ["<<mr.color.x<<","<<mr.color.y<<","<<mr.color.z<<","<<mr.color.w<<"], \"uvTiling\": ["<<mr.uvTiling.x<<","<<mr.uvTiling.y<<"], \"uvOffset\": ["<<mr.uvOffset.x<<","<<mr.uvOffset.y<<"], \"lightmapPath\": \""<<EscapeJson(mr.lightmapPath)<<"\"}";
	}
	for(const auto& bc : o.boxColliders) {
		if(!first) ss<<",\n"; first = false;
		ss<<"        {\"type\": \"BoxCollider\", \"enabled\": "<<(bc.enabled?"true":"false")<<", \"center\": ["<<bc.center.x<<","<<bc.center.y<<","<<bc.center.z<<"], \"size\": ["<<bc.size.x<<","<<bc.size.y<<","<<bc.size.z<<"]}";
	}
	for(const auto& tg : o.tags) {
		if(!first) ss<<",\n"; first = false;
		ss<<"        {\"type\": \"Tag\", \"enabled\": "<<(tg.enabled?"true":"false")<<", \"tag\": \""<<EscapeJson(tg.tag)<<"\"}";
	}
	for(const auto& an : o.animators) {
		if(!first) ss<<",\n"; first = false;
		ss<<"        {\"type\": \"Animator\", \"enabled\": "<<(an.enabled?"true":"false")<<", \"currentAnimation\": \""<<EscapeJson(an.currentAnimation)<<"\", \"isPlaying\": "<<(an.isPlaying?"true":"false")<<", \"loop\": "<<(an.loop?"true":"false")<<", \"speed\": ["<<an.speed<<"], \"time\": ["<<an.time<<"]}";
	}
	for(const auto& rb : o.rigidbodies) {
		if(!first) ss<<",\n"; first = false;
		ss<<"        {\"type\": \"Rigidbody\", \"enabled\": "<<(rb.enabled?"true":"false")<<", \"velocity\": ["<<rb.velocity.x<<","<<rb.velocity.y<<","<<rb.velocity.z<<"], \"useGravity\": "<<(rb.useGravity?"true":"false")<<", \"isKinematic\": "<<(rb.isKinematic?"true":"false")<<"}";
	}
	// ★追加: ParticleEmitterのシリアライズ
	for(const auto& pe : o.particleEmitters) {
		if(!first) ss<<",\n"; first = false;
		const auto& p = pe.emitter.params;
		ss<<"        {\"type\": \"ParticleEmitter\", \"enabled\": "<<(pe.enabled?"true":"false")<<", \"isPlaying\": "<<(pe.emitter.isPlaying?"true":"false")<<", \"emitRate\": "<<p.emitRate<<", \"burstCount\": "<<p.burstCount;
		ss<<", \"lifeTime\": "<<p.lifeTime<<", \"lifeTimeVariance\": "<<p.lifeTimeVariance;
		ss<<", \"startVelocity\": ["<<p.startVelocity.x<<","<<p.startVelocity.y<<","<<p.startVelocity.z<<"], \"velocityVariance\": ["<<p.velocityVariance.x<<","<<p.velocityVariance.y<<","<<p.velocityVariance.z<<"], \"acceleration\": ["<<p.acceleration.x<<","<<p.acceleration.y<<","<<p.acceleration.z<<"]";
		ss<<", \"startSize\": ["<<p.startSize.x<<","<<p.startSize.y<<","<<p.startSize.z<<"], \"endSize\": ["<<p.endSize.x<<","<<p.endSize.y<<","<<p.endSize.z<<"]";
		ss<<", \"startColor\": ["<<p.startColor.x<<","<<p.startColor.y<<","<<p.startColor.z<<","<<p.startColor.w<<"], \"endColor\": ["<<p.endColor.x<<","<<p.endColor.y<<","<<p.endColor.z<<","<<p.endColor.w<<"]";
		ss<<", \"isAdditive\": "<<(p.isAdditive?"true":"false");
		ss<<", \"assetPath\": \""<<EscapeJson(pe.assetPath)<<"\"";
		ss<<"}";
	}
	ss<<"\n      ]\n";
	ss<<"    }";
	return ss.str();
}

void EditorUI::SaveScene(GameScene* scene, const std::string& path) {
	if(!scene) return;
	std::ofstream f(path); if(!f.is_open()){LogError("Save failed: "+path);return;}
	f<<"{\n  \"objects\": [\n";
	for(size_t i=0;i<scene->objects_.size();++i) {
		if(i>0) f<<",\n";
		f<<SerializeSceneObject(scene->objects_[i]);
	}
	f<<"\n  ]\n}\n"; f.close(); Log("Scene saved: "+path+" ("+std::to_string(scene->objects_.size())+" objects)");
}

// ====== JSON Load ======
static std::string UnescapeJson(const std::string& s) {
	std::string o;
	for (size_t i = 0; i < s.size(); ++i) {
		if (s[i] == '\\' && i + 1 < s.size()) {
			o += s[i+1];
			i++;
		} else {
			o += s[i];
		}
	}
	return o;
}

static std::string ExtractString(const std::string& block, const std::string& key) {
	auto pos=block.find("\""+key+"\""); if(pos==std::string::npos) return "";
	auto q1=block.find("\"", block.find(":",pos)+1); if(q1==std::string::npos) return "";
	size_t q2 = q1 + 1;
	while (q2 < block.size()) {
		if (block[q2] == '\\') q2 += 2;
		else if (block[q2] == '"') break;
		else q2++;
	}
	if(q2>=block.size()) return ""; 
	return UnescapeJson(block.substr(q1+1,q2-q1-1));
}

static size_t FindBlockEnd(const std::string& str, size_t startPos) {
	int depth = 0;
	bool inString = false;
	bool escape = false;

	for (size_t i = startPos; i < str.size(); ++i) {
		char c = str[i];
		if (escape) { escape = false; continue; }
		if (c == '\\') { escape = true; continue; }
		if (c == '"') { inString = !inString; continue; }

		if (!inString) {
			if (c == '{' || c == '[') depth++;
			else if (c == '}' || c == ']') {
				depth--;
				if (depth <= 0) return i; 
			}
		}
	}
	return std::string::npos;
}
static std::vector<float> ExtractArray(const std::string& block, const std::string& key) {
	std::vector<float> r; auto pos=block.find("\""+key+"\""); if(pos==std::string::npos) return r;
	auto b=block.find("[",pos); auto e=block.find("]",b); if(b==std::string::npos||e==std::string::npos) return r;
	std::istringstream ss(block.substr(b+1,e-b-1)); float v; while(ss>>v){r.push_back(v); char c; ss>>c;} return r;
}

static void ParseComponents(SceneObject& obj, const std::string& block, Engine::Renderer* renderer) {
	auto compStart = block.find("\"components\"");
	if(compStart == std::string::npos) return;
	auto arrStart = block.find("[", compStart);
	if(arrStart == std::string::npos) return;
	auto arrEnd = FindBlockEnd(block, arrStart);
	if(arrEnd == std::string::npos) return;
	
	size_t pos = arrStart + 1;
	while(pos < arrEnd) {
		pos = block.find("{", pos);
		if(pos == std::string::npos || pos > arrEnd) break;
		auto endPos = FindBlockEnd(block, pos);
		if(endPos == std::string::npos || endPos > arrEnd) break;
		std::string cblock = block.substr(pos, endPos - pos + 1);
		std::string type = ExtractString(cblock, "type");
		bool enabled = true;
		auto lkPos = cblock.find("\"enabled\"");
		if(lkPos!=std::string::npos && cblock.find("false",lkPos)!=std::string::npos && cblock.find("false",lkPos)<lkPos+30) enabled=false;

		if (type == "MeshRenderer") {
			MeshRendererComponent mr; mr.enabled = enabled;
			mr.modelPath = ExtractString(cblock, "modelPath");
			if (!mr.modelPath.empty()) mr.modelHandle = renderer->LoadObjMesh(mr.modelPath);
			mr.texturePath = ExtractString(cblock, "texturePath");
			if (!mr.texturePath.empty()) mr.textureHandle = renderer->LoadTexture2D(mr.texturePath);
			auto co = ExtractArray(cblock, "color"); if(co.size()>=4) mr.color={co[0],co[1],co[2],co[3]};
			auto uvt = ExtractArray(cblock, "uvTiling"); if(uvt.size()>=2) mr.uvTiling={uvt[0],uvt[1]};
			auto uvo = ExtractArray(cblock, "uvOffset"); if(uvo.size()>=2) mr.uvOffset={uvo[0],uvo[1]};
			mr.lightmapPath = ExtractString(cblock, "lightmapPath");
			if (!mr.lightmapPath.empty()) mr.lightmapHandle = renderer->LoadTexture2D(mr.lightmapPath);
			obj.meshRenderers.push_back(mr);
		} else if (type == "BoxCollider") {
			BoxColliderComponent bc; bc.enabled = enabled;
			auto cen = ExtractArray(cblock, "center"); if(cen.size()>=3) bc.center={cen[0],cen[1],cen[2]};
			auto sz = ExtractArray(cblock, "size"); if(sz.size()>=3) bc.size={sz[0],sz[1],sz[2]};
			obj.boxColliders.push_back(bc);
		} else if (type == "Tag") {
			TagComponent tg; tg.enabled = enabled;
			tg.tag = ExtractString(cblock, "tag");
			obj.tags.push_back(tg);
		} else if (type == "Animator") {
			AnimatorComponent an; an.enabled = enabled;
			an.currentAnimation = ExtractString(cblock, "currentAnimation");
			auto iPos = cblock.find("\"isPlaying\""); if(iPos!=std::string::npos && cblock.find("true",iPos)!=std::string::npos && cblock.find("true",iPos)<iPos+30) an.isPlaying=true; else an.isPlaying=false;
			auto lPos = cblock.find("\"loop\""); if(lPos!=std::string::npos && cblock.find("true",lPos)!=std::string::npos && cblock.find("true",lPos)<lPos+30) an.loop=true; else an.loop=false;
			auto sp = ExtractArray(cblock, "speed"); if(sp.size()>=1) an.speed=sp[0];
			auto tm = ExtractArray(cblock, "time"); if(tm.size()>=1) an.time=tm[0];
			obj.animators.push_back(an);
		} else if (type == "Rigidbody") {
			RigidbodyComponent rb; rb.enabled = enabled;
			auto vl = ExtractArray(cblock, "velocity"); if(vl.size()>=3) rb.velocity={vl[0],vl[1],vl[2]};
			auto gPos = cblock.find("\"useGravity\""); if(gPos!=std::string::npos && cblock.find("false",gPos)!=std::string::npos && cblock.find("false",gPos)<gPos+30) rb.useGravity=false; else rb.useGravity=true;
			auto kPos = cblock.find("\"isKinematic\""); if(kPos!=std::string::npos && cblock.find("true",kPos)!=std::string::npos && cblock.find("true",kPos)<kPos+30) rb.isKinematic=true; else rb.isKinematic=false;
			obj.rigidbodies.push_back(rb);
		} else if (type == "ParticleEmitter") { // ★追加
			ParticleEmitterComponent pe; pe.enabled = enabled;
			pe.emitter.Initialize(*Engine::Renderer::GetInstance(), "LoadedEmitter");

			// assetPath があれば ParticleEmitter 自身にファイルから復元させる
			pe.assetPath = ExtractString(cblock, "assetPath");
			if(!pe.assetPath.empty()) {
				pe.emitter.LoadFromJson(pe.assetPath);
			}

			// JSON内にも上書きパラメーターがある場合のフォールバック（従来との互換性用）
			auto& p = pe.emitter.params;
			auto boolCheck = [&](const std::string& k, bool def) { auto pos = cblock.find("\""+k+"\""); if(pos==std::string::npos) return def; return cblock.find("true",pos)<pos+30; };
			auto floatCheck = [&](const std::string& k, float def) { auto a = ExtractArray(cblock, k); return a.empty() ? def : a[0]; };
			if (cblock.find("\"isPlaying\"") != std::string::npos) pe.emitter.isPlaying = boolCheck("isPlaying", true);
			if (cblock.find("\"emitRate\"") != std::string::npos) p.emitRate = floatCheck("emitRate", 10.0f);
			if (cblock.find("\"burstCount\"") != std::string::npos) p.burstCount = (int)floatCheck("burstCount", 0.0f);
			if (cblock.find("\"lifeTime\"") != std::string::npos) p.lifeTime = floatCheck("lifeTime", 1.0f);
			if (cblock.find("\"lifeTimeVariance\"") != std::string::npos) p.lifeTimeVariance = floatCheck("lifeTimeVariance", 0.2f);
			auto vel = ExtractArray(cblock, "startVelocity"); if(vel.size()>=3) p.startVelocity = {vel[0],vel[1],vel[2]};
			auto vRand = ExtractArray(cblock, "velocityVariance"); if(vRand.size()>=3) p.velocityVariance = {vRand[0],vRand[1],vRand[2]};
			auto acc = ExtractArray(cblock, "acceleration"); if(acc.size()>=3) p.acceleration = {acc[0],acc[1],acc[2]};
			auto ss = ExtractArray(cblock, "startSize"); if(ss.size()>=3) p.startSize = {ss[0],ss[1],ss[2]};
			auto es = ExtractArray(cblock, "endSize"); if(es.size()>=3) p.endSize = {es[0],es[1],es[2]};
			auto sc = ExtractArray(cblock, "startColor"); if(sc.size()>=4) p.startColor = {sc[0],sc[1],sc[2],sc[3]};
			auto ec = ExtractArray(cblock, "endColor"); if(ec.size()>=4) p.endColor = {ec[0],ec[1],ec[2],ec[3]};
			if (cblock.find("\"isAdditive\"") != std::string::npos) p.isAdditive = boolCheck("isAdditive", false);
			obj.particleEmitters.push_back(pe);
		}
		pos = endPos + 1;
	}
}
void EditorUI::LoadScene(GameScene* scene, const std::string& path) {
	if(!scene) return; std::ifstream f(path); if(!f.is_open()){LogError("Load failed: "+path);return;}
	std::string content((std::istreambuf_iterator<char>(f)),std::istreambuf_iterator<char>()); f.close();
	scene->objects_.clear(); scene->selectedIndices_.clear(); scene->selectedObjectIndex_=-1;
	auto* renderer=Engine::Renderer::GetInstance();
	auto arrStart=content.find("[",content.find("\"objects\"")); if(arrStart==std::string::npos){LogError("Invalid scene file");return;}
	auto arrEnd=content.rfind("]"); if(arrEnd==std::string::npos) arrEnd=content.size();
	size_t objStart=arrStart;
	while(objStart < arrEnd){
		objStart=content.find("{",objStart); if(objStart==std::string::npos || objStart>arrEnd) break;
		// 最初の "{" の位置から探すため、FindBlockEndはobjStartから開始（FindBlockEnd内で"{"をカウントする）
		auto objEnd=FindBlockEnd(content, objStart); if(objEnd==std::string::npos || objEnd>arrEnd) break;
		std::string block=content.substr(objStart,objEnd-objStart+1);
		SceneObject obj; obj.name=ExtractString(block,"name"); obj.modelPath=ExtractString(block,"modelPath"); obj.texturePath=ExtractString(block,"texturePath");
		{auto lkPos=block.find("\"locked\""); if(lkPos!=std::string::npos && block.find("true",lkPos)!=std::string::npos && block.find("true",lkPos)<lkPos+30) obj.locked=true;}
		auto tr=ExtractArray(block,"translate"); if(tr.size()>=3){obj.translate={tr[0],tr[1],tr[2]};}
		auto ro=ExtractArray(block,"rotate"); if(ro.size()>=3){obj.rotate={ro[0],ro[1],ro[2]};}
		auto sc=ExtractArray(block,"scale"); if(sc.size()>=3){obj.scale={sc[0],sc[1],sc[2]};}
		auto co=ExtractArray(block,"color"); if(co.size()>=4){obj.color={co[0],co[1],co[2],co[3]};} else if(co.size()>=3){obj.color={co[0],co[1],co[2],1};}
		if(!obj.modelPath.empty()) obj.modelHandle=renderer->LoadObjMesh(obj.modelPath);
		if(!obj.texturePath.empty()) obj.textureHandle=renderer->LoadTexture2D(obj.texturePath);
		ParseComponents(obj, block, renderer);
		scene->objects_.push_back(obj); objStart=objEnd;
	}
	Log("Scene loaded: "+path+" ("+std::to_string(scene->objects_.size())+" objects)");
}

void EditorUI::LoadPrefab(GameScene* scene, const std::string& path) {
	if(!scene) return; std::ifstream f(path); if(!f.is_open()){LogError("Prefab load failed: "+path);return;}
	std::string content((std::istreambuf_iterator<char>(f)),std::istreambuf_iterator<char>()); f.close();
	
	size_t objStart = content.find("{");
	if (objStart == std::string::npos) return;
	size_t objEnd = FindBlockEnd(content, objStart);
	if (objEnd == std::string::npos) return;
	std::string block = content.substr(objStart, objEnd - objStart + 1);
	SceneObject obj; obj.name = GenerateCopyName(ExtractString(block,"name"), scene->objects_);
	obj.modelPath = ExtractString(block,"modelPath"); obj.texturePath = ExtractString(block,"texturePath");
	auto tr=ExtractArray(block,"translate"); if(tr.size()>=3){obj.translate={tr[0],tr[1],tr[2]};}
	auto ro=ExtractArray(block,"rotate"); if(ro.size()>=3){obj.rotate={ro[0],ro[1],ro[2]};}
	auto sc=ExtractArray(block,"scale"); if(sc.size()>=3){obj.scale={sc[0],sc[1],sc[2]};}
	auto co=ExtractArray(block,"color"); if(co.size()>=4){obj.color={co[0],co[1],co[2],co[3]};} else if(co.size()>=3){obj.color={co[0],co[1],co[2],1};}
	auto* r = Engine::Renderer::GetInstance();
	if(!obj.modelPath.empty()) obj.modelHandle = r->LoadObjMesh(obj.modelPath);
	if(!obj.texturePath.empty()) obj.textureHandle = r->LoadTexture2D(obj.texturePath);
	
	ParseComponents(obj, block, r);

	// 後方互換性：コンポーネントが無く、モデルがあればデフォルトを付与
	if (obj.meshRenderers.empty() && !obj.modelPath.empty()) {
		MeshRendererComponent mr; mr.modelHandle = obj.modelHandle; mr.textureHandle = obj.textureHandle;
		mr.modelPath = obj.modelPath; mr.texturePath = obj.texturePath; mr.color = obj.color;
		obj.meshRenderers.push_back(mr);
	}
	
	scene->objects_.push_back(obj);
	Log("Prefab loaded and instantiated: "+path);
}

// ====== ★ Ray-AABB 交差判定 ======
static bool RayIntersectsAABB(DirectX::XMVECTOR rayOrig, DirectX::XMVECTOR rayDir,
	const DirectX::XMFLOAT3& bmin, const DirectX::XMFLOAT3& bmax, float& tOut)
{
	using namespace DirectX;
	XMFLOAT3 orig; XMStoreFloat3(&orig, rayOrig);
	XMFLOAT3 dir;  XMStoreFloat3(&dir, rayDir);
	float tmin = -FLT_MAX, tmax = FLT_MAX;
	float mn[3] = {bmin.x, bmin.y, bmin.z};
	float mx[3] = {bmax.x, bmax.y, bmax.z};
	float o[3]  = {orig.x, orig.y, orig.z};
	float d[3]  = {dir.x, dir.y, dir.z};
	for (int i = 0; i < 3; ++i) {
		if (std::fabs(d[i]) < 1e-8f) { if (o[i] < mn[i] || o[i] > mx[i]) return false; }
		else {
			float t1 = (mn[i] - o[i]) / d[i]; float t2 = (mx[i] - o[i]) / d[i];
			if (t1 > t2) { float tmp = t1; t1 = t2; t2 = tmp; }
			if (t1 > tmin) tmin = t1; if (t2 < tmax) tmax = t2;
			if (tmin > tmax) return false;
		}
	}
	if (tmax < 0) return false;
	tOut = tmin > 0 ? tmin : tmax;
	return true;
}

// ★ スクリーン座標からワールドRayを構築
static void ScreenToWorldRay(float screenX, float screenY, float imageW, float imageH,
	DirectX::XMMATRIX view, DirectX::XMMATRIX proj,
	DirectX::XMVECTOR& outOrig, DirectX::XMVECTOR& outDir)
{
	using namespace DirectX;
	// NDC座標に変換 [-1, 1]
	float ndcX = (screenX / imageW) * 2.0f - 1.0f;
	float ndcY = 1.0f - (screenY / imageH) * 2.0f; // Y反転
	
	XMMATRIX invProj = XMMatrixInverse(nullptr, proj);
	XMMATRIX invView = XMMatrixInverse(nullptr, view);

	// Near plane と Far plane のポイント
	XMVECTOR nearPoint = XMVector3TransformCoord(XMVectorSet(ndcX, ndcY, 0.0f, 1.0f), invProj);
	XMVECTOR farPoint  = XMVector3TransformCoord(XMVectorSet(ndcX, ndcY, 1.0f, 1.0f), invProj);

	// ビュー空間→ワールド空間
	nearPoint = XMVector3TransformCoord(nearPoint, invView);
	farPoint  = XMVector3TransformCoord(farPoint, invView);

	outOrig = nearPoint;
	outDir  = XMVector3Normalize(XMVectorSubtract(farPoint, nearPoint));
}

// ★ ギズモ軸のRayヒット判定（ローカル空間に変換して判定）
static int HitTestGizmoAxis(DirectX::XMVECTOR rayOrig, DirectX::XMVECTOR rayDir,
	const Engine::Transform& objTransform, float axisLen, GizmoMode mode)
{
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
		XMFLOAT3 orig; XMStoreFloat3(&orig, localOrig);
		XMFLOAT3 dir;  XMStoreFloat3(&dir, localDir);

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
					float dist = std::sqrt(px*px + py*py + pz*pz);
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
	} else {
		float thickness = 0.15f;
		DirectX::XMFLOAT3 axes[3][2] = {
			{{-thickness, -thickness, -thickness}, {axisLen, thickness, thickness}},
			{{-thickness, -thickness, -thickness}, {thickness, axisLen, thickness}},
			{{-thickness, -thickness, -thickness}, {thickness, thickness, axisLen}},
		};
		float bestT = FLT_MAX;
		int bestAxis = -1;
		for (int a = 0; a < 3; ++a) {
			float t;
			if (RayIntersectsAABB(localOrig, localDir, axes[a][0], axes[a][1], t)) {
				if (t < bestT) { bestT = t; bestAxis = a; }
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
	ImGui::SetNextWindowPos(vp->Pos); ImGui::SetNextWindowSize(vp->Size);
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


	// ====== Menu Bar ======
	if (ImGui::BeginMenuBar()) {
		if (ImGui::BeginMenu("File")) {
			if(ImGui::MenuItem("Save Scene","Ctrl+S")) SaveScene(gameScene,"Resources/scene.json");
			if(ImGui::MenuItem("Load Scene")) LoadScene(gameScene,"Resources/scene.json");
			ImGui::Separator();
			if(ImGui::MenuItem("Undo","Ctrl+Z")) Undo(); if(ImGui::MenuItem("Redo","Ctrl+Y")) Redo();
			ImGui::Separator(); if(ImGui::MenuItem("Exit")){} ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Edit")) {
			if(ImGui::MenuItem("Copy","Ctrl+C")) { if(gameScene) { clipboardObjects.clear(); for(int i:gameScene->selectedIndices_) if(i<(int)gameScene->objects_.size()) clipboardObjects.push_back(gameScene->objects_[i]); Log("Copied "+std::to_string(clipboardObjects.size())+" object(s)"); }}
			if(ImGui::MenuItem("Paste","Ctrl+V")) { if(gameScene && !clipboardObjects.empty()) { for(auto obj:clipboardObjects){obj.name=GenerateCopyName(obj.name,gameScene->objects_); obj.locked=false; obj.translate.x+=1.0f; gameScene->objects_.push_back(obj);} Log("Pasted "+std::to_string(clipboardObjects.size())+" object(s)"); }}
			if(ImGui::MenuItem("Duplicate","Ctrl+D")) { if(gameScene) { std::vector<SceneObject> dups; for(int i:gameScene->selectedIndices_) if(i<(int)gameScene->objects_.size()){auto o=gameScene->objects_[i]; o.name=GenerateCopyName(o.name,gameScene->objects_); o.locked=false; o.translate.x+=1.0f; dups.push_back(o);} for(auto&d:dups) gameScene->objects_.push_back(d); Log("Duplicated "+std::to_string(dups.size())+" object(s)"); }}
			if(ImGui::MenuItem("Delete","Del")) { if(gameScene && !gameScene->selectedIndices_.empty()) { for(auto it=gameScene->selectedIndices_.rbegin();it!=gameScene->selectedIndices_.rend();++it) if(*it<(int)gameScene->objects_.size() && !gameScene->objects_[*it].locked) gameScene->objects_.erase(gameScene->objects_.begin()+*it); gameScene->selectedIndices_.clear(); gameScene->selectedObjectIndex_=-1; }}
			if(ImGui::MenuItem("Select All","Ctrl+A")) { if(gameScene) { for(int i=0;i<(int)gameScene->objects_.size();++i) gameScene->selectedIndices_.insert(i); if(!gameScene->objects_.empty()) gameScene->selectedObjectIndex_=0; }}
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Scene")) {
			if(ImGui::MenuItem("Title")) Engine::SceneManager::GetInstance()->Change("Title");
			if(ImGui::MenuItem("Game")) Engine::SceneManager::GetInstance()->Change("Game");
			ImGui::EndMenu();
		}
		ImGui::Spacing(); ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical); ImGui::Spacing();
		ImGui::SetCursorPosY(ImGui::GetCursorPosY()+2); ImGui::Text("Aspect:"); ImGui::SameLine();
		ImGui::SetCursorPosY(ImGui::GetCursorPosY()-2); ImGui::PushItemWidth(110); ImGui::Combo("##Asp",&aspectMode,aspectNames,IM_ARRAYSIZE(aspectNames)); ImGui::PopItemWidth();

		ImGui::Spacing(); ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical); ImGui::Spacing();
		ImGui::SetCursorPosY(ImGui::GetCursorPosY()+2);
		auto gBtn=[](const char* l,GizmoMode m){bool a=(currentGizmoMode==m); if(a) ImGui::PushStyleColor(ImGuiCol_Button,ImVec4(.3f,.5f,.9f,1)); if(ImGui::SmallButton(l)) currentGizmoMode=m; if(a) ImGui::PopStyleColor();};
		gBtn("T##M",GizmoMode::Translate); ImGui::SameLine(); gBtn("R##R",GizmoMode::Rotate); ImGui::SameLine(); gBtn("S##S",GizmoMode::Scale);

		ImGui::Spacing(); ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical); ImGui::Spacing();
		if (gameScene) {
			if (gameScene->isPlaying_) {
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(.8f, .2f, .2f, 1));
				if (ImGui::SmallButton("Stop")) {
					gameScene->isPlaying_ = false;
					LoadScene(gameScene, "Resources/.temp_play.json");
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
	if(io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z,false)) Undo();
	if(io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y,false)) Redo();
	if(io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S,false)) SaveScene(gameScene,"Resources/scene.json");
	if(io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C,false) && gameScene) { clipboardObjects.clear(); for(int i:gameScene->selectedIndices_) if(i<(int)gameScene->objects_.size()) clipboardObjects.push_back(gameScene->objects_[i]); if(!clipboardObjects.empty()) Log("Copied "+std::to_string(clipboardObjects.size())); }
	if(io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V,false) && gameScene && !clipboardObjects.empty()) { for(auto obj:clipboardObjects){obj.name=GenerateCopyName(obj.name,gameScene->objects_); obj.locked=false; obj.translate.x+=1; gameScene->objects_.push_back(obj);} Log("Pasted "+std::to_string(clipboardObjects.size())); }
	if(io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D,false) && gameScene) { std::vector<SceneObject> dups; for(int i:gameScene->selectedIndices_) if(i<(int)gameScene->objects_.size()){auto o=gameScene->objects_[i]; o.name=GenerateCopyName(o.name,gameScene->objects_); o.locked=false; o.translate.x+=1; dups.push_back(o);} for(auto&d:dups) gameScene->objects_.push_back(d); }
	if(io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A,false) && gameScene) { for(int i=0;i<(int)gameScene->objects_.size();++i) gameScene->selectedIndices_.insert(i); }
	if(!io.WantTextInput && !ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
		if(ImGui::IsKeyPressed(ImGuiKey_T,false)) currentGizmoMode=GizmoMode::Translate;
		if(ImGui::IsKeyPressed(ImGuiKey_R,false)) currentGizmoMode=GizmoMode::Rotate;
		if(ImGui::IsKeyPressed(ImGuiKey_S,false)&&!io.KeyCtrl) currentGizmoMode=GizmoMode::Scale;
	}

	ShowHierarchy(gameScene);
	ShowInspector(gameScene);
	ShowProject(renderer, gameScene);
	ShowSceneSettings(renderer);
	ShowConsole();

	// ======== Game ウィンドウ ========
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
	ImGui::Begin("Game");
	ImVec2 cp = ImGui::GetCursorPos(), av = ImGui::GetContentRegionAvail();
	float tW = av.x, tH = av.y;
	if(aspectMode==1){float r=16.f/9.f; if(tW/tH>r) tW=tH*r; else tH=tW/r;}
	else if(aspectMode==2){float r=4.f/3.f; if(tW/tH>r) tW=tH*r; else tH=tW/r;}
	float offX=(av.x-tW)*.5f, offY=(av.y-tH)*.5f;
	ImGui::SetCursorPos(ImVec2(cp.x+offX, cp.y+offY));

	// ★ 画像の絶対スクリーン座標を記録 (ピッキング用)
	ImVec2 windowPos = ImGui::GetWindowPos();
	ImVec2 curScreen = ImGui::GetCursorScreenPos();
	ImGui::Image((ImTextureID)renderer->GetGameFinalSRV().ptr, ImVec2(tW, tH));
	// ★追加: プレハブやモデルのドラッグ＆ドロップ受け入れ先
	if(ImGui::BeginDragDropTarget()){
		if(const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("RESOURCE_PATH")){
			std::string path((const char*)pl->Data, pl->DataSize - 1);
			if (path.find(".prefab") != std::string::npos) {
				LoadPrefab(gameScene, path);
			} else if (path.find(".obj") != std::string::npos || path.find(".gltf") != std::string::npos || path.find(".fbx") != std::string::npos) {
				SceneObject o; o.name = "Model"; o.modelPath = path; o.modelHandle = renderer->LoadObjMesh(path);
				MeshRendererComponent mr; mr.modelHandle = o.modelHandle; mr.modelPath = o.modelPath; o.meshRenderers.push_back(mr);
				gameScene->objects_.push_back(o);
			}
		}
		ImGui::EndDragDropTarget();
	}
	gameImageMin = curScreen;
	gameImageMax = ImVec2(curScreen.x + tW, curScreen.y + tH);

	bool gameHovered = ImGui::IsWindowHovered();

	// ====== ★ ビューポートクリック選択 + ギズモドラッグ ======
	if (gameScene && gameHovered && tW > 0 && tH > 0) {
		ImVec2 mousePos = ImGui::GetMousePos();
		float localX = mousePos.x - gameImageMin.x;
		float localY = mousePos.y - gameImageMin.y;
		bool insideImage = (localX >= 0 && localY >= 0 && localX <= tW && localY <= tH);

		auto viewMat = gameScene->camera_.View();
		auto projMat = gameScene->camera_.Proj();

		if (insideImage) {
			// --- ★ 左クリック → ギズモ軸 → オブジェクト選択 → 自由ドラッグ開始 ---
			if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
				DirectX::XMVECTOR rayOrig, rayDir;
				ScreenToWorldRay(localX, localY, tW, tH, viewMat, projMat, rayOrig, rayDir);

				// 1. ギズモ軸ヒットテスト
				bool hitGizmo = false;
				if (gameScene->selectedObjectIndex_ >= 0 && gameScene->selectedObjectIndex_ < (int)gameScene->objects_.size()
					&& !gameScene->objects_[gameScene->selectedObjectIndex_].locked) {
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

				// 2. オブジェクト選択 + 自由ドラッグ開始
				if (!hitGizmo) {
					float bestT = FLT_MAX;
					int bestIdx = -1;
					for (int i = 0; i < (int)gameScene->objects_.size(); ++i) {
						const auto& obj = gameScene->objects_[i];
						if (obj.locked) continue; // ★ ロック済みオブジェクトは選択不可
						
						// ★ OBB判定: Rayをオブジェクトのローカル空間に変換
						Engine::Matrix4x4 mat = obj.GetTransform().ToMatrix();
						DirectX::XMMATRIX worldMat = DirectX::XMLoadFloat4x4(reinterpret_cast<DirectX::XMFLOAT4X4*>(&mat));
						DirectX::XMVECTOR det;
						DirectX::XMMATRIX invWorld = DirectX::XMMatrixInverse(&det, worldMat);

						DirectX::XMVECTOR localOrig = DirectX::XMVector3TransformCoord(rayOrig, invWorld);
						DirectX::XMVECTOR localTarget = DirectX::XMVector3TransformCoord(DirectX::XMVectorAdd(rayOrig, rayDir), invWorld);
						DirectX::XMVECTOR localDir = DirectX::XMVectorSubtract(localTarget, localOrig);

						// 最小サイズ保証
						float hx = 1.0f; if (std::fabs(obj.scale.x) < 0.6f && std::fabs(obj.scale.x) > 0.001f) hx = 0.3f / std::fabs(obj.scale.x);
						float hy = 1.0f; if (std::fabs(obj.scale.y) < 0.6f && std::fabs(obj.scale.y) > 0.001f) hy = 0.3f / std::fabs(obj.scale.y);
						float hz = 1.0f; if (std::fabs(obj.scale.z) < 0.6f && std::fabs(obj.scale.z) > 0.001f) hz = 0.3f / std::fabs(obj.scale.z);
						DirectX::XMFLOAT3 bmin = {-hx, -hy, -hz};
						DirectX::XMFLOAT3 bmax = { hx,  hy,  hz};

						float tLocal;
						if (RayIntersectsAABB(localOrig, localDir, bmin, bmax, tLocal)) {
							// tLocal は worldDir (正規化済) の長さ(1)に対する係数と一致
							if (tLocal < bestT) { bestT = tLocal; bestIdx = i; }
						}
					}
					if (bestIdx >= 0) {
						if (io.KeyCtrl) {
							// Ctrl+クリック: トグル追加
							if (gameScene->selectedIndices_.count(bestIdx)) gameScene->selectedIndices_.erase(bestIdx);
							else gameScene->selectedIndices_.insert(bestIdx);
						} else if (io.KeyShift) {
							// Shift+クリック: 追加選択
							gameScene->selectedIndices_.insert(bestIdx);
						} else {
							// 通常クリック: 単一選択
							gameScene->selectedIndices_ = {bestIdx};
						}
						gameScene->selectedObjectIndex_ = bestIdx;

						// ★ 自由ドラッグ開始
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
			}

			// --- ★ ギズモ軸ドラッグ中 ---
			if (gizmoDragging && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
				ImVec2 delta = ImVec2(mousePos.x - gizmoDragStartMouse.x, mousePos.y - gizmoDragStartMouse.y);
				for (int idx : gameScene->selectedIndices_) {
					if (idx >= 0 && idx < (int)gameScene->objects_.size() && dragStartTransforms.count(idx)) {
						auto& obj = gameScene->objects_[idx];
						auto initT = dragStartTransforms[idx];
						if (currentGizmoMode == GizmoMode::Translate) {
							float s = 0.02f;
							float dx = (gizmoDragAxis == 0) ? delta.x*s : 0;
							float dy = (gizmoDragAxis == 1) ? -delta.y*s : 0;
							float dz = (gizmoDragAxis == 2) ? delta.x*s : 0;
							// ローカル軸に沿って移動する
							auto rotMat = DirectX::XMMatrixRotationRollPitchYaw(initT.rotate.x, initT.rotate.y, initT.rotate.z);
							DirectX::XMVECTOR moveV = DirectX::XMVector3TransformNormal(DirectX::XMVectorSet(dx, dy, dz, 0), rotMat);
							DirectX::XMFLOAT3 moveF; DirectX::XMStoreFloat3(&moveF, moveV);
							obj.translate = DirectX::XMFLOAT3(initT.translate.x + moveF.x, initT.translate.y + moveF.y, initT.translate.z + moveF.z);
						} else if (currentGizmoMode == GizmoMode::Rotate) {
							float s = 0.01f;
							auto nr = initT.rotate;
							if(gizmoDragAxis==0) nr.x += delta.y*s; 
							else if(gizmoDragAxis==1) nr.y += delta.x*s; 
							else nr.z += delta.x*s;
							obj.rotate = DirectX::XMFLOAT3(nr.x, nr.y, nr.z);
						} else {
							float s = 0.01f; 
							auto ns = initT.scale;
							if(gizmoDragAxis==0) ns.x+=delta.x*s; else if(gizmoDragAxis==1) ns.y-=delta.y*s; else ns.z+=delta.x*s;
							if(ns.x<0.01f)ns.x=0.01f; if(ns.y<0.01f)ns.y=0.01f; if(ns.z<0.01f)ns.z=0.01f;
							obj.scale = DirectX::XMFLOAT3(ns.x, ns.y, ns.z);
						}
					}
				}
			}

			// --- ★ 自由ドラッグ中（ギズモではなくオブジェクト直接ドラッグ）---
			if (objectDragging && !gizmoDragging && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
				ImVec2 delta = ImVec2(mousePos.x - gizmoDragStartMouse.x, mousePos.y - gizmoDragStartMouse.y);
				if (std::fabs(delta.x) > 2.0f || std::fabs(delta.y) > 2.0f) { // デッドゾーン
					auto camR2 = gameScene->camera_.Rotation();
					auto rotMat = DirectX::XMMatrixRotationRollPitchYaw(camR2.x, camR2.y, camR2.z);
					DirectX::XMFLOAT3 right = {1,0,0}, up = {0,1,0};
					DirectX::XMVECTOR rightV = DirectX::XMVector3TransformCoord(DirectX::XMLoadFloat3(&right), rotMat);
					DirectX::XMVECTOR upV = DirectX::XMVector3TransformCoord(DirectX::XMLoadFloat3(&up), rotMat);

					float sensitivity = 0.015f;
					DirectX::XMVECTOR moveV = DirectX::XMVectorAdd(
						DirectX::XMVectorScale(rightV, delta.x * sensitivity),
						DirectX::XMVectorScale(upV, -delta.y * sensitivity)
					);
					DirectX::XMFLOAT3 moveF;
					DirectX::XMStoreFloat3(&moveF, moveV);

					for (int idx : gameScene->selectedIndices_) {
						if (idx >= 0 && idx < (int)gameScene->objects_.size() && dragStartTransforms.count(idx)) {
							auto initT = dragStartTransforms[idx];
							gameScene->objects_[idx].translate = DirectX::XMFLOAT3(
								initT.translate.x + moveF.x,
								initT.translate.y + moveF.y,
								initT.translate.z + moveF.z
							);
						}
					}
				}
			}

			// --- ★ ドラッグ終了 (Undo登録) ---
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
					PushUndo({"Transform",
						[gameScene, targetIndices, oldTransforms](){
							for(size_t i=0; i<targetIndices.size(); ++i){
								int idx = targetIndices[i];
								if(idx < (int)gameScene->objects_.size()){
									gameScene->objects_[idx].translate = DirectX::XMFLOAT3(oldTransforms[i].translate.x, oldTransforms[i].translate.y, oldTransforms[i].translate.z);
									gameScene->objects_[idx].rotate = DirectX::XMFLOAT3(oldTransforms[i].rotate.x, oldTransforms[i].rotate.y, oldTransforms[i].rotate.z);
									gameScene->objects_[idx].scale = DirectX::XMFLOAT3(oldTransforms[i].scale.x, oldTransforms[i].scale.y, oldTransforms[i].scale.z);
								}
							}
						},
						[gameScene, targetIndices, newTransforms](){
							for(size_t i=0; i<targetIndices.size(); ++i){
								int idx = targetIndices[i];
								if(idx < (int)gameScene->objects_.size()){
									gameScene->objects_[idx].translate = DirectX::XMFLOAT3(newTransforms[i].translate.x, newTransforms[i].translate.y, newTransforms[i].translate.z);
									gameScene->objects_[idx].rotate = DirectX::XMFLOAT3(newTransforms[i].rotate.x, newTransforms[i].rotate.y, newTransforms[i].rotate.z);
									gameScene->objects_[idx].scale = DirectX::XMFLOAT3(newTransforms[i].scale.x, newTransforms[i].scale.y, newTransforms[i].scale.z);
								}
							}
						}
					});
				}
				gizmoDragging = false; gizmoDragAxis = -1;
				objectDragging = false;
				dragStartTransforms.clear();
			}
		}

		// --- カメラ操作（右クリック） ---
		auto camP = gameScene->camera_.Position();
		auto camR = gameScene->camera_.Rotation();
		if (ImGui::IsMouseDragging(ImGuiMouseButton_Right, 1.0f)) {
			ImVec2 d = ImGui::GetMouseDragDelta(ImGuiMouseButton_Right, 1.0f);
			ImGui::ResetMouseDragDelta(ImGuiMouseButton_Right);
			camR.y += d.x * 0.003f; camR.x += d.y * 0.003f;
			float lim = DirectX::XMConvertToRadians(89.0f);
			if (camR.x > lim) camR.x = lim; if (camR.x < -lim) camR.x = -lim;
			gameScene->camera_.SetRotation(camR);
		}
		if (ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
			float sp = io.KeyShift ? 0.45f : 0.15f;
			DirectX::XMFLOAT3 mv = {0,0,0};
			if(ImGui::IsKeyDown(ImGuiKey_W))mv.z+=sp; if(ImGui::IsKeyDown(ImGuiKey_S))mv.z-=sp;
			if(ImGui::IsKeyDown(ImGuiKey_A))mv.x-=sp; if(ImGui::IsKeyDown(ImGuiKey_D))mv.x+=sp;
			if(ImGui::IsKeyDown(ImGuiKey_Q))mv.y-=sp; if(ImGui::IsKeyDown(ImGuiKey_E))mv.y+=sp;
			auto r=DirectX::XMMatrixRotationRollPitchYaw(camR.x,camR.y,camR.z);
			DirectX::XMStoreFloat3(&camP, DirectX::XMVectorAdd(DirectX::XMLoadFloat3(&camP), DirectX::XMVector3TransformCoord(DirectX::XMLoadFloat3(&mv),r)));
			gameScene->camera_.SetPosition(camP);
		}
		float wh = io.MouseWheel;
		if (std::fabs(wh) > 0.01f) {
			float zs=io.KeyShift?3.f:1.f; auto r=DirectX::XMMatrixRotationRollPitchYaw(camR.x,camR.y,camR.z);
			DirectX::XMFLOAT3 fw={0,0,1};
			DirectX::XMStoreFloat3(&camP, DirectX::XMVectorAdd(DirectX::XMLoadFloat3(&camP), DirectX::XMVectorScale(DirectX::XMVector3TransformCoord(DirectX::XMLoadFloat3(&fw),r),wh*zs)));
			gameScene->camera_.SetPosition(camP);
		}
	}
	// ドラッグがウィンドウ外に行った場合のリセット
	if ((gizmoDragging || objectDragging) && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) { gizmoDragging = false; gizmoDragAxis = -1; objectDragging = false; }

	if (gameScene && tH > 0.0f) gameScene->camera_.SetProjection(DirectX::XMConvertToRadians(45.0f), tW/tH, 0.1f, 1000.0f);
	ImGui::End(); ImGui::PopStyleVar();

	// ★ DrawSelectionGizmo削除: GameScene::Draw()内で描画するように移動済み
	ImGui::End(); // DockSpace
}

// ====== Hierarchy ======
void EditorUI::ShowHierarchy(GameScene* scene) {
	ImGui::Begin("Hierarchy");
	ImGuiIO& io = ImGui::GetIO();
	if (scene) {
		if (ImGui::BeginPopupContextWindow("HierarchyCtx")) {
			auto addObj=[&](const char* label,const std::string& mp,const std::string& tp){
				if(ImGui::MenuItem(label)){auto* r=Engine::Renderer::GetInstance(); SceneObject obj; obj.name=label;
					if(!mp.empty()){obj.modelHandle=r->LoadObjMesh(mp); obj.modelPath=mp;}
					if(!tp.empty()){obj.textureHandle=r->LoadTexture2D(tp); obj.texturePath=tp;}
					else{obj.textureHandle=r->LoadTexture2D("Resources/white1x1.png"); obj.texturePath="Resources/white1x1.png";}
					scene->objects_.push_back(obj); int idx=(int)scene->objects_.size()-1;
					scene->selectedIndices_={idx}; scene->selectedObjectIndex_=idx;
					PushUndo({std::string("Create ")+label,
						[scene,idx](){if(idx<(int)scene->objects_.size()){scene->objects_.erase(scene->objects_.begin()+idx);scene->selectedIndices_.clear();scene->selectedObjectIndex_=-1;}},
						[scene,obj,idx](){scene->objects_.insert(scene->objects_.begin()+idx,obj);scene->selectedIndices_={idx};scene->selectedObjectIndex_=idx;}
					}); Log(std::string("Created: ")+label);}
			};
			addObj("Empty","",""); addObj("Cube","Resources/cube/cube.obj","Resources/white1x1.png"); addObj("Plane","Resources/plane.obj","Resources/white1x1.png");
			ImGui::Separator();
			if(!scene->selectedIndices_.empty() && ImGui::MenuItem("Delete Selected")){
				std::vector<std::pair<int,SceneObject>> del;
				for(auto it=scene->selectedIndices_.rbegin();it!=scene->selectedIndices_.rend();++it){int i=*it; if(i<(int)scene->objects_.size() && !scene->objects_[i].locked){del.push_back({i,scene->objects_[i]});scene->objects_.erase(scene->objects_.begin()+i);}}
				scene->selectedIndices_.clear();scene->selectedObjectIndex_=-1;
				if(!del.empty()) PushUndo({"Delete",[scene,del](){for(auto it=del.rbegin();it!=del.rend();++it)if(it->first<=(int)scene->objects_.size())scene->objects_.insert(scene->objects_.begin()+it->first,it->second);},
					[scene,del](){for(auto&p:del)if(p.first<(int)scene->objects_.size())scene->objects_.erase(scene->objects_.begin()+p.first);scene->selectedIndices_.clear();scene->selectedObjectIndex_=-1;}});
			}
			ImGui::Separator();
			// ★ 一括ロック/解除
			if(ImGui::MenuItem("Lock All")) { for(auto& o : scene->objects_) o.locked = true; Log("All objects locked"); }
			if(ImGui::MenuItem("Unlock All")) { for(auto& o : scene->objects_) o.locked = false; Log("All objects unlocked"); }
			if(!scene->selectedIndices_.empty()) {
				if(ImGui::MenuItem("Lock Selected")) { for(int i:scene->selectedIndices_) if(i<(int)scene->objects_.size()) scene->objects_[i].locked=true; }
				if(ImGui::MenuItem("Unlock Selected")) { for(int i:scene->selectedIndices_) if(i<(int)scene->objects_.size()) scene->objects_[i].locked=false; }
			}
			ImGui::EndPopup();
		}
		for(int i=0;i<(int)scene->objects_.size();++i){
			bool sel=scene->selectedIndices_.count(i)>0;
			bool locked=scene->objects_[i].locked;
			// ★ ロックトグルボタン
			ImGui::PushID(i);
			if(locked) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
			if(ImGui::SmallButton(locked ? "L##lk" : "U##lk")) { scene->objects_[i].locked = !locked; }
			if(locked) ImGui::PopStyleColor();
			ImGui::SameLine();
			ImGui::PopID();
			std::string lb=scene->objects_[i].name; if(lb.empty()) lb="Object "+std::to_string(i);
			if(locked) lb = "[L] " + lb;
			lb+="##"+std::to_string(i);
			// ★ ロック済みオブジェクトは選択不可
			if(locked) {
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
				ImGui::Selectable(lb.c_str(), sel, ImGuiSelectableFlags_Disabled);
				ImGui::PopStyleColor();
			} else {
				if(ImGui::Selectable(lb.c_str(), sel)){
					if(io.KeyCtrl){if(sel)scene->selectedIndices_.erase(i); else scene->selectedIndices_.insert(i);}
					else if(io.KeyShift&&scene->selectedObjectIndex_>=0){int lo=(std::min)(scene->selectedObjectIndex_,i),hi=(std::max)(scene->selectedObjectIndex_,i); for(int j=lo;j<=hi;++j) if(!scene->objects_[j].locked) scene->selectedIndices_.insert(j);}
					else{scene->selectedIndices_={i};}
					scene->selectedObjectIndex_=i;
				}
			}
		}
		if(!io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Delete,false)&&!scene->selectedIndices_.empty()){
			for(auto it=scene->selectedIndices_.rbegin();it!=scene->selectedIndices_.rend();++it)if(*it<(int)scene->objects_.size() && !scene->objects_[*it].locked)scene->objects_.erase(scene->objects_.begin()+*it);
			scene->selectedIndices_.clear();scene->selectedObjectIndex_=-1;
		}
	} else ImGui::Text("No Active Scene");
	ImGui::End();
}

// ====== Inspector ======
void EditorUI::ShowInspector(GameScene* scene) {
	ImGui::Begin("Inspector");
	if(scene&&scene->selectedObjectIndex_>=0&&scene->selectedObjectIndex_<(int)scene->objects_.size()){
		auto& obj=scene->objects_[scene->selectedObjectIndex_];
		char buf[256]; strcpy_s(buf,obj.name.c_str());
		if(ImGui::InputText("Name",buf,sizeof(buf))){std::string oN=obj.name,nN=buf;obj.name=nN;int i=scene->selectedObjectIndex_;
			PushUndo({"Rename",[scene,i,oN](){if(i<(int)scene->objects_.size())scene->objects_[i].name=oN;},[scene,i,nN](){if(i<(int)scene->objects_.size())scene->objects_[i].name=nN;}});}
		// ★ ロックチェックボックス
		ImGui::SameLine();
		ImGui::Checkbox("Lock", &obj.locked);
		// ★追加: Prefab保存ボタン
		ImGui::SameLine();
		if(ImGui::Button("Save Prefab")){
			std::string ppath = "Resources/" + obj.name + ".prefab";
			std::ofstream pf(ppath);
			if(pf.is_open()){
				// SerializeSceneObjectは4スペースインデントのコンテキストで囲んでいるため、それをそのまま使用する
				pf << "{\n  \"prefab\":\n" << SerializeSceneObject(obj) << "\n}\n";
				pf.close();
				Log("Prefab saved: " + ppath);
			} else {
				LogError("Failed to save prefab: " + ppath);
			}
		}
		if(obj.locked) {
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
			ImGui::Text("** LOCKED - Transform editing disabled **");
			ImGui::PopStyleColor();
		}
		// ★ ロック中はTransformとコンポーネント編集を無効化
		if(obj.locked) ImGui::BeginDisabled();
		ImGui::Separator(); ImGui::Text("Transform");
		{auto old=obj.translate;ImGui::DragFloat3("Position",&obj.translate.x,0.1f);
			if(ImGui::IsItemDeactivatedAfterEdit()){auto nv=obj.translate;int i=scene->selectedObjectIndex_;PushUndo({"Move",[scene,i,old](){if(i<(int)scene->objects_.size())scene->objects_[i].translate=old;},[scene,i,nv](){if(i<(int)scene->objects_.size())scene->objects_[i].translate=nv;}});}}
		{auto old=obj.rotate;ImGui::DragFloat3("Rotation",&obj.rotate.x,0.01f);
			if(ImGui::IsItemDeactivatedAfterEdit()){auto nv=obj.rotate;int i=scene->selectedObjectIndex_;PushUndo({"Rotate",[scene,i,old](){if(i<(int)scene->objects_.size())scene->objects_[i].rotate=old;},[scene,i,nv](){if(i<(int)scene->objects_.size())scene->objects_[i].rotate=nv;}});}}
		{auto old=obj.scale;ImGui::DragFloat3("Scale",&obj.scale.x,0.1f);
			if(ImGui::IsItemDeactivatedAfterEdit()){auto nv=obj.scale;int i=scene->selectedObjectIndex_;PushUndo({"Scale",[scene,i,old](){if(i<(int)scene->objects_.size())scene->objects_[i].scale=old;},[scene,i,nv](){if(i<(int)scene->objects_.size())scene->objects_[i].scale=nv;}});}}

		ImGui::Separator(); ImGui::ColorEdit4("Color",&obj.color.x);
		ImGui::Separator(); ImGui::Text("Model: %s",obj.modelPath.empty()?"(none)":obj.modelPath.c_str());
		ImGui::Text("Texture: %s",obj.texturePath.empty()?"(none)":obj.texturePath.c_str());
		if(ImGui::BeginDragDropTarget()){if(const ImGuiPayload*pl=ImGui::AcceptDragDropPayload("RESOURCE_PATH")){
			std::string path((const char*)pl->Data,pl->DataSize-1); auto*r=Engine::Renderer::GetInstance();
			if(path.find(".png")!=std::string::npos||path.find(".jpg")!=std::string::npos){obj.textureHandle=r->LoadTexture2D(path);obj.texturePath=path;Log("Texture: "+path);}
			else if(path.find(".obj")!=std::string::npos||path.find(".gltf")!=std::string::npos){obj.modelHandle=r->LoadObjMesh(path);obj.modelPath=path;Log("Model: "+path);}
		}ImGui::EndDragDropTarget();}

		ImGui::Separator();
		if(ImGui::CollapsingHeader("Components",ImGuiTreeNodeFlags_DefaultOpen)){
			for(size_t ci=0;ci<obj.meshRenderers.size();++ci){auto&mr=obj.meshRenderers[ci];ImGui::PushID((int)ci);
				if(ImGui::TreeNode("MeshRenderer")){ImGui::Checkbox("Enabled",&mr.enabled);ImGui::ColorEdit4("Color##MR",&mr.color.x);
					ImGui::DragFloat2("UV Tiling",&mr.uvTiling.x,0.01f);
					ImGui::DragFloat2("UV Offset",&mr.uvOffset.x,0.01f);
					ImGui::Text("Lightmap: %s",mr.lightmapPath.empty()?"(none)":mr.lightmapPath.c_str());
					if(ImGui::BeginDragDropTarget()){if(const ImGuiPayload*pl=ImGui::AcceptDragDropPayload("RESOURCE_PATH")){
						std::string path((const char*)pl->Data,pl->DataSize-1);
						if(path.find(".png")!=std::string::npos||path.find(".jpg")!=std::string::npos){mr.lightmapHandle=Engine::Renderer::GetInstance()->LoadTexture2D(path);mr.lightmapPath=path;}
					}ImGui::EndDragDropTarget();}
					if(ImGui::Button("Remove##MR")){obj.meshRenderers.erase(obj.meshRenderers.begin()+ci);ImGui::TreePop();ImGui::PopID();goto end_comp;}ImGui::TreePop();}ImGui::PopID();}
			for(size_t ci=0;ci<obj.boxColliders.size();++ci){auto&bc=obj.boxColliders[ci];ImGui::PushID(1000+(int)ci);
				if(ImGui::TreeNode("BoxCollider")){ImGui::Checkbox("Enabled",&bc.enabled);ImGui::DragFloat3("Center",&bc.center.x,0.1f);ImGui::DragFloat3("Size",&bc.size.x,0.1f);
					if(ImGui::Button("Remove##BC")){obj.boxColliders.erase(obj.boxColliders.begin()+ci);ImGui::TreePop();ImGui::PopID();goto end_comp;}ImGui::TreePop();}ImGui::PopID();}
			for(size_t ci=0;ci<obj.tags.size();++ci){auto&tg=obj.tags[ci];ImGui::PushID(2000+(int)ci);
				if(ImGui::TreeNode("Tag")){char tb[128];strcpy_s(tb,tg.tag.c_str());if(ImGui::InputText("Tag",tb,sizeof(tb)))tg.tag=tb;
					if(ImGui::Button("Remove##Tag")){obj.tags.erase(obj.tags.begin()+ci);ImGui::TreePop();ImGui::PopID();goto end_comp;}ImGui::TreePop();}ImGui::PopID();}
			for(size_t ci=0;ci<obj.animators.size();++ci){auto&an=obj.animators[ci];ImGui::PushID(3000+(int)ci);
				if(ImGui::TreeNode("Animator")){ImGui::Checkbox("Enabled",&an.enabled);
					char tb[128];strcpy_s(tb,an.currentAnimation.c_str());if(ImGui::InputText("Animation",tb,sizeof(tb)))an.currentAnimation=tb;
					ImGui::Checkbox("Is Playing",&an.isPlaying); ImGui::SameLine(); ImGui::Checkbox("Loop",&an.loop);
					ImGui::DragFloat("Speed",&an.speed,0.01f); ImGui::DragFloat("Time",&an.time,0.01f);
					if(ImGui::Button("Remove##Anim")){obj.animators.erase(obj.animators.begin()+ci);ImGui::TreePop();ImGui::PopID();goto end_comp;}ImGui::TreePop();}ImGui::PopID();}
			for(size_t ci=0;ci<obj.rigidbodies.size();++ci){auto&rb=obj.rigidbodies[ci];ImGui::PushID(4000+(int)ci);
				if(ImGui::TreeNode("Rigidbody")){ImGui::Checkbox("Enabled",&rb.enabled);
					ImGui::DragFloat3("Velocity",&rb.velocity.x,0.1f);
					ImGui::Checkbox("Use Gravity",&rb.useGravity); ImGui::Checkbox("Is Kinematic",&rb.isKinematic);
					if(ImGui::Button("Remove##RB")){obj.rigidbodies.erase(obj.rigidbodies.begin()+ci);ImGui::TreePop();ImGui::PopID();goto end_comp;}ImGui::TreePop();}ImGui::PopID();}
			// ★追加: ParticleEmitter コンポーネント
			for(size_t ci=0;ci<obj.particleEmitters.size();++ci){auto&pe=obj.particleEmitters[ci];ImGui::PushID(5000+(int)ci);
				if(ImGui::TreeNode("Particle Emitter")){
					ImGui::Checkbox("Enabled##PE",&pe.enabled);
					
					// ★追加: アセットパスとD&D
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

						// --- ファイル連携 ---
						ImGui::Separator();
						ImGui::Text("Asset Link");
						char assetBuf[256];
						strcpy_s(assetBuf, pe.assetPath.c_str());
						if (ImGui::InputText("Asset Path##PE", assetBuf, sizeof(assetBuf))) {
							std::string newPath = assetBuf;
							if (newPath != pe.assetPath) {
								pe.assetPath = newPath;
								// パスが変更されたら自動的に読み込む
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

						// --- プレビュー/基本設定 ---
						ImGui::Checkbox("Is Playing##PE", &pe.emitter.isPlaying);
						
						if (ImGui::CollapsingHeader("Emission##PE", ImGuiTreeNodeFlags_DefaultOpen)) {
							ImGui::DragFloat("Emit Rate##PE", &p.emitRate, 1.0f, 0.0f, 1000.0f);
							ImGui::DragInt("Burst Count##PE", &p.burstCount, 1, 0, 1000);
							if (ImGui::Button("Emit Burst (10)##PE")) {
								pe.emitter.EmitBurst(10);
							}
						}

						// 形状
						if (ImGui::CollapsingHeader("Shape##PEHeader", ImGuiTreeNodeFlags_DefaultOpen)) {
							int shapeType = static_cast<int>(p.shape);
							const char* shapeNames[] = { "Point", "Sphere", "Cone" };
							if (ImGui::Combo("Shape##PECombo", &shapeType, shapeNames, IM_ARRAYSIZE(shapeNames))) p.shape = static_cast<Engine::EmissionShape>(shapeType);
							if (p.shape != Engine::EmissionShape::Point) ImGui::DragFloat("Shape Radius##PE", &p.shapeRadius, 0.01f, 0.0f, 100.0f);
							if (p.shape == Engine::EmissionShape::Cone) ImGui::DragFloat("Cone Angle##PE", &p.shapeAngle, 0.01f, 0.0f, 3.1415f);
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
							if (ImGui::InputText("Texture##PE", texBuf, sizeof(texBuf))) p.texturePath = texBuf;

							char shaderBuf[256];
							strcpy_s(shaderBuf, p.shaderName.c_str());
							if (ImGui::InputText("Shader##PE", shaderBuf, sizeof(shaderBuf))) p.shaderName = shaderBuf;

							ImGui::Checkbox("Additive Blend##PE", &p.isAdditive);
							ImGui::Checkbox("Use Billboard##PE", &p.useBillboard);

							// UVアニメーション
							ImGui::Separator();
							ImGui::Checkbox("Use UV Animation##PE", &p.useUvAnim);
							if (p.useUvAnim) {
								ImGui::DragInt("Cols##PE", &p.uvAnimCols, 0.1f, 1, 64);
								ImGui::DragInt("Rows##PE", &p.uvAnimRows, 0.1f, 1, 64);
								ImGui::DragFloat("FPS##PE", &p.uvAnimFps, 0.1f, 0.1f, 120.0f);
							}
						}
					}
					if(ImGui::Button("Remove##PE")){obj.particleEmitters.erase(obj.particleEmitters.begin()+ci);ImGui::TreePop();ImGui::PopID();goto end_comp;}
					ImGui::TreePop();
				}ImGui::PopID();}
			end_comp:
			if(ImGui::Button("Add Component"))ImGui::OpenPopup("AddComp");
			if(ImGui::BeginPopup("AddComp")){
				if(ImGui::MenuItem("MeshRenderer")){MeshRendererComponent mr;obj.meshRenderers.push_back(mr);} 
				if(ImGui::MenuItem("BoxCollider")){obj.boxColliders.push_back({});} 
				if(ImGui::MenuItem("Tag")){obj.tags.push_back({});} 
				if(ImGui::MenuItem("Animator")){obj.animators.push_back({});} 
				if(ImGui::MenuItem("Rigidbody")){obj.rigidbodies.push_back({});} 
				if(ImGui::MenuItem("ParticleEmitter")){ // ★追加
					ParticleEmitterComponent pe;
					pe.emitter.Initialize(*Engine::Renderer::GetInstance(), "NewEmitter");
					obj.particleEmitters.push_back(pe);
				}
				ImGui::EndPopup();
			}
		}
		if(obj.locked) ImGui::EndDisabled();
		ImGui::Separator();
		const char* mn[]={"Translate (T)","Rotate (R)","Scale (S)"};
		ImGui::Text("Gizmo: %s",mn[(int)currentGizmoMode]);
		if(scene->selectedIndices_.size()>1)ImGui::Text("(%d selected)",(int)scene->selectedIndices_.size());
	} else ImGui::Text("No Object Selected");
	ImGui::End();
}

// ====== Project ======
void EditorUI::ShowProject(Engine::Renderer* renderer, GameScene* scene) {
	(void)scene;
	ImGui::Begin("Project");

	// ★ 静的変数: フォルダナビゲーション・キャッシュ・音声再生
	static std::string currentDir = "Resources";
	static std::map<std::string, Engine::Renderer::TextureHandle> thumbnailCache;
	static float iconSize = 80.0f;
	static uint32_t playingSoundHandle = 0xFFFFFFFF;
	static size_t playingVoiceHandle = 0;
	static std::string playingAudioPath;

	if (!fs::exists(currentDir)) currentDir = "Resources";

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
				if (t2.empty()) continue;
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

	ImGui::SameLine(ImGui::GetWindowWidth() - 160);
	ImGui::PushItemWidth(100);
	ImGui::SliderFloat("##iconSz", &iconSize, 48.0f, 128.0f, "%.0f");
	ImGui::PopItemWidth();
	ImGui::SameLine(); ImGui::Text("Size");

	ImGui::Separator();

	// --- ファイル一覧を収集 ---
	struct ProjectEntry {
		std::string path;     // フルパス
		std::string name;     // ファイル名のみ
		bool isDir;
		std::string ext;      // 小文字拡張子
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
				for (auto& c : pe.ext) c = (char)std::tolower((unsigned char)c);
			}
			entries.push_back(pe);
		}
	}

	// ソート: フォルダ先、ファイル後
	std::sort(entries.begin(), entries.end(), [](const ProjectEntry& a, const ProjectEntry& b) {
		if (a.isDir != b.isDir) return a.isDir > b.isDir;
		return a.name < b.name;
	});

	// --- 「..」上位フォルダボタン ---
	if (currentDir != "Resources") {
		auto parent = fs::path(currentDir).parent_path().string();
		if (parent.empty()) parent = "Resources";
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
	if (columns < 1) columns = 1;
	int col = (currentDir != "Resources") ? 1 : 0; // 「..」ボタンの分

	for (size_t ei = 0; ei < entries.size(); ++ei) {
		auto& pe = entries[ei];
		ImGui::PushID((int)ei);

		bool isTexture = (pe.ext == ".png" || pe.ext == ".jpg" || pe.ext == ".jpeg" || pe.ext == ".bmp");
		bool isModel = (pe.ext == ".obj" || pe.ext == ".gltf" || pe.ext == ".fbx");
		bool isAudio = (pe.ext == ".mp3" || pe.ext == ".wav" || pe.ext == ".ogg");
		bool isPrefab = (pe.ext == ".prefab");

		// グリッドレイアウト
		if (col > 0 && col < columns) ImGui::SameLine();
		else if (col >= columns) col = 0;

		ImGui::BeginGroup();

		if (pe.isDir) {
			// ★ フォルダ: 黄色っぽいボタン
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.35f, 0.30f, 0.15f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.50f, 0.45f, 0.20f, 1.0f));
			if (ImGui::Button("##dir", ImVec2(iconSize, iconSize))) {
				currentDir = pe.path;
			}
			ImGui::PopStyleColor(2);
			// フォルダアイコンのテキスト
			ImVec2 bmin = ImGui::GetItemRectMin();
			ImVec2 bmax = ImGui::GetItemRectMax();
			float cx = (bmin.x + bmax.x) * 0.5f;
			float cy = (bmin.y + bmax.y) * 0.5f;
			ImGui::GetWindowDrawList()->AddText(ImVec2(cx - 8, cy - 10), IM_COL32(255, 220, 80, 255), "D");
		} else if (isTexture) {
			// ★ テクスチャ: サムネイルプレビュー
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
			// ★ モデル: アイコン
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
			// ★ 音声: 再生/停止ボタン付きアイコン
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
						if (playingVoiceHandle != 0) audio->Stop(playingVoiceHandle);
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
			// ★ Prefab: 青緑アイコン
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.40f, 0.40f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.30f, 0.50f, 0.50f, 1.0f));
			ImGui::Button("##prefab", ImVec2(iconSize, iconSize));
			ImGui::PopStyleColor(2);
			ImVec2 bmin = ImGui::GetItemRectMin();
			ImVec2 bmax = ImGui::GetItemRectMax();
			float cx = (bmin.x + bmax.x) * 0.5f;
			float cy = (bmin.y + bmax.y) * 0.5f;
			ImGui::GetWindowDrawList()->AddText(ImVec2(cx - 16, cy - 10), IM_COL32(150, 255, 200, 255), "PFB");
		} else {
			// ★ その他ファイル: グレーアイコン
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.25f, 0.25f, 1.0f));
			ImGui::Button("##file", ImVec2(iconSize, iconSize));
			ImGui::PopStyleColor();
			ImVec2 bmin = ImGui::GetItemRectMin();
			ImVec2 bmax = ImGui::GetItemRectMax();
			float cx = (bmin.x + bmax.x) * 0.5f;
			float cy = (bmin.y + bmax.y) * 0.5f;
			ImGui::GetWindowDrawList()->AddText(ImVec2(cx - 6, cy - 10), IM_COL32(180, 180, 180, 255), "F");
		}

		// ★ ドラッグ＆ドロップソース (ファイルのみ)
		if (!pe.isDir && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
			ImGui::SetDragDropPayload("RESOURCE_PATH", pe.path.c_str(), pe.path.size() + 1);
			ImGui::Text("%s", pe.name.c_str());
			ImGui::EndDragDropSource();
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

	ImGui::End();
}

void EditorUI::ShowSceneSettings(Engine::Renderer* renderer) {
	ImGui::Begin("Scene Settings");
	if(ImGui::CollapsingHeader("Post Processing",ImGuiTreeNodeFlags_DefaultOpen)){
		auto pp=renderer->GetPostProcessParams();bool ch=false;bool en=renderer->GetPostProcessEnabled();
		if(ImGui::Checkbox("Enable",&en))renderer->SetPostProcessEnabled(en);
		if(en){ch|=ImGui::DragFloat("Vignette",&pp.vignette,0.01f,0,5);ch|=ImGui::DragFloat("Distortion",&pp.distortion,0.001f,0,1);
			ch|=ImGui::DragFloat("Noise",&pp.noiseStrength,0.01f,0,1);ch|=ImGui::DragFloat("Chromatic",&pp.chromaShift,0.001f,0,0.1f);ch|=ImGui::DragFloat("Scanline",&pp.scanline,0.01f,0,1);}
		if(ch)renderer->SetPostProcessParams(pp);}
	ImGui::End();
}

void EditorUI::ShowConsole() {
	ImGui::Begin("Console");
	if(ImGui::SmallButton("Clear"))consoleLog.clear(); ImGui::SameLine();ImGui::Text("(%d)",(int)consoleLog.size());ImGui::Separator();
	ImGui::BeginChild("CS",ImVec2(0,0),false,ImGuiWindowFlags_HorizontalScrollbar);
	for(const auto&e:consoleLog){ImVec4 c;const char*p;
		switch(e.level){case LogLevel::Info:c={.8f,.8f,.8f,1};p="[INFO] ";break;case LogLevel::Warning:c={1,.9f,.3f,1};p="[WARN] ";break;default:c={1,.3f,.3f,1};p="[ERR]  ";break;}
		ImGui::PushStyleColor(ImGuiCol_Text,c);ImGui::TextUnformatted((std::string(p)+e.message).c_str());ImGui::PopStyleColor();}
	if(ImGui::GetScrollY()>=ImGui::GetScrollMaxY()-10)ImGui::SetScrollHereY(1);
	ImGui::EndChild();ImGui::End();
}

// ====== ★ 選択ギズモ + ハイライト描画 ======
void EditorUI::DrawSelectionGizmo(Engine::Renderer* renderer, GameScene* scene) {
	if (!scene) return;
	for (int idx : scene->selectedIndices_) {
		if (idx < 0 || idx >= (int)scene->objects_.size()) continue;
		auto& obj = scene->objects_[idx];
		Engine::Vector3 pos = {obj.translate.x, obj.translate.y, obj.translate.z};
		const float al = 2.0f, ar = 0.3f;

		// ★ ギズモの色 (ドラッグ中の軸は明るく、それ以外は通常色)
		auto axisColor = [](int axis, int dragAxis) -> Engine::Vector4 {
			bool active = (dragAxis == axis);
			switch (axis) {
				case 0: return active ? Engine::Vector4{1.0f, 0.5f, 0.5f, 1.0f} : Engine::Vector4{1.0f, 0.2f, 0.2f, 1.0f}; // X=赤
				case 1: return active ? Engine::Vector4{0.5f, 1.0f, 0.5f, 1.0f} : Engine::Vector4{0.2f, 1.0f, 0.2f, 1.0f}; // Y=緑
				case 2: return active ? Engine::Vector4{0.5f, 0.5f, 1.0f, 1.0f} : Engine::Vector4{0.2f, 0.2f, 1.0f, 1.0f}; // Z=青
				default: return {1,1,1,1};
			}
		};

		int dAxis = (gizmoDragging && idx == scene->selectedObjectIndex_) ? gizmoDragAxis : -1;
		auto cX = axisColor(0, dAxis), cY = axisColor(1, dAxis), cZ = axisColor(2, dAxis);

		if (currentGizmoMode == GizmoMode::Translate) {
			// X軸 →
			renderer->DrawLine3D(pos, {pos.x+al, pos.y, pos.z}, cX);
			renderer->DrawLine3D({pos.x+al, pos.y, pos.z}, {pos.x+al-ar, pos.y+ar*.4f, pos.z}, cX);
			renderer->DrawLine3D({pos.x+al, pos.y, pos.z}, {pos.x+al-ar, pos.y-ar*.4f, pos.z}, cX);
			// Y軸 ↑
			renderer->DrawLine3D(pos, {pos.x, pos.y+al, pos.z}, cY);
			renderer->DrawLine3D({pos.x, pos.y+al, pos.z}, {pos.x+ar*.4f, pos.y+al-ar, pos.z}, cY);
			renderer->DrawLine3D({pos.x, pos.y+al, pos.z}, {pos.x-ar*.4f, pos.y+al-ar, pos.z}, cY);
			// Z軸
			renderer->DrawLine3D(pos, {pos.x, pos.y, pos.z+al}, cZ);
			renderer->DrawLine3D({pos.x, pos.y, pos.z+al}, {pos.x, pos.y+ar*.4f, pos.z+al-ar}, cZ);
			renderer->DrawLine3D({pos.x, pos.y, pos.z+al}, {pos.x, pos.y-ar*.4f, pos.z+al-ar}, cZ);
		} else if (currentGizmoMode == GizmoMode::Rotate) {
			const int seg = 32; const float rad = 1.5f;
			for (int i = 0; i < seg; ++i) {
				float a0 = (float)i / seg * DirectX::XM_2PI, a1 = (float)(i + 1) / seg * DirectX::XM_2PI;
				renderer->DrawLine3D({pos.x, pos.y + cosf(a0)*rad, pos.z + sinf(a0)*rad}, {pos.x, pos.y + cosf(a1)*rad, pos.z + sinf(a1)*rad}, cX);
				renderer->DrawLine3D({pos.x + cosf(a0)*rad, pos.y, pos.z + sinf(a0)*rad}, {pos.x + cosf(a1)*rad, pos.y, pos.z + sinf(a1)*rad}, cY);
				renderer->DrawLine3D({pos.x + cosf(a0)*rad, pos.y + sinf(a0)*rad, pos.z}, {pos.x + cosf(a1)*rad, pos.y + sinf(a1)*rad, pos.z}, cZ);
			}
		} else {
			float e = 0.15f;
			renderer->DrawLine3D(pos, {pos.x+al, pos.y, pos.z}, cX);
			renderer->DrawLine3D({pos.x+al-e, pos.y-e, pos.z}, {pos.x+al+e, pos.y+e, pos.z}, cX);
			renderer->DrawLine3D({pos.x+al+e, pos.y-e, pos.z}, {pos.x+al-e, pos.y+e, pos.z}, cX);
			renderer->DrawLine3D(pos, {pos.x, pos.y+al, pos.z}, cY);
			renderer->DrawLine3D({pos.x-e, pos.y+al-e, pos.z}, {pos.x+e, pos.y+al+e, pos.z}, cY);
			renderer->DrawLine3D({pos.x+e, pos.y+al-e, pos.z}, {pos.x-e, pos.y+al+e, pos.z}, cY);
			renderer->DrawLine3D(pos, {pos.x, pos.y, pos.z+al}, cZ);
			renderer->DrawLine3D({pos.x, pos.y-e, pos.z+al-e}, {pos.x, pos.y+e, pos.z+al+e}, cZ);
			renderer->DrawLine3D({pos.x, pos.y+e, pos.z+al-e}, {pos.x, pos.y-e, pos.z+al+e}, cZ);
		}

		// ★ 選択ハイライト: バウンディングボックス (黄色のワイヤーフレーム)
		float sx = obj.scale.x * 0.5f, sy = obj.scale.y * 0.5f, sz = obj.scale.z * 0.5f;
		Engine::Vector4 hlColor = {1.0f, 0.85f, 0.0f, 0.9f}; // 明るい黄色
		Engine::Vector3 v[8] = {
			{pos.x-sx,pos.y-sy,pos.z-sz},{pos.x+sx,pos.y-sy,pos.z-sz},{pos.x+sx,pos.y+sy,pos.z-sz},{pos.x-sx,pos.y+sy,pos.z-sz},
			{pos.x-sx,pos.y-sy,pos.z+sz},{pos.x+sx,pos.y-sy,pos.z+sz},{pos.x+sx,pos.y+sy,pos.z+sz},{pos.x-sx,pos.y+sy,pos.z+sz},
		};
		int edges[][2] = {{0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7}};
		for (auto& eg : edges) renderer->DrawLine3D(v[eg[0]], v[eg[1]], hlColor);

		// ★追加: コライダー可視化 (緑色のワイヤーフレーム)
		for (const auto& bc : obj.boxColliders) {
			if (!bc.enabled) continue;
			float csx = bc.size.x * 0.5f * obj.scale.x;
			float csy = bc.size.y * 0.5f * obj.scale.y;
			float csz = bc.size.z * 0.5f * obj.scale.z;
			Engine::Vector3 cp = {
				pos.x + bc.center.x * obj.scale.x,
				pos.y + bc.center.y * obj.scale.y,
				pos.z + bc.center.z * obj.scale.z
			};
			Engine::Vector4 colColor = {0.2f, 1.0f, 0.2f, 0.8f}; // 緑色
			Engine::Vector3 cv[8] = {
				{cp.x-csx,cp.y-csy,cp.z-csz},{cp.x+csx,cp.y-csy,cp.z-csz},{cp.x+csx,cp.y+csy,cp.z-csz},{cp.x-csx,cp.y+csy,cp.z-csz},
				{cp.x-csx,cp.y-csy,cp.z+csz},{cp.x+csx,cp.y-csy,cp.z+csz},{cp.x+csx,cp.y+csy,cp.z+csz},{cp.x-csx,cp.y+csy,cp.z+csz},
			};
			for (auto& eg : edges) renderer->DrawLine3D(cv[eg[0]], cv[eg[1]], colColor);
		}
	}
}
// ====== ★ Animation Window ======
void EditorUI::ShowAnimationWindow(Engine::Renderer* renderer, GameScene* scene) {
	(void)renderer;
	ImGui::Begin("Animation");
	if (scene && scene->selectedObjectIndex_ >= 0 && scene->selectedObjectIndex_ < (int)scene->objects_.size()) {
		auto& obj = scene->objects_[scene->selectedObjectIndex_];
		if (!obj.animators.empty()) {
			auto& anim = obj.animators[0]; // 最初のAnimatorを表示
			ImGui::Text("Selected: %s (Animator)", obj.name.c_str());
			ImGui::Separator();
			
			// アニメーションリスト（モデルが持っているアニメーションを取得）
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
							if (selected) ImGui::SetItemDefaultFocus();
						}
						ImGui::EndCombo();
					}
					
					// 現在のアニメーションを探す
					const Engine::Animation* currentAnimPtr = nullptr;
					for (const auto& a : data.animations) {
						if (a.name == anim.currentAnimation) {
							currentAnimPtr = &a;
							break;
						}
					}
					
					if (currentAnimPtr) {
						ImGui::Text("Duration: %.2f ticks (%.1f fps)", currentAnimPtr->duration, currentAnimPtr->ticksPerSecond);
						// シークバー (タイムライン)
						ImGui::SliderFloat("Time", &anim.time, 0.0f, currentAnimPtr->duration, "%.2f");
						
						// 再生コントロール
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

} // namespace Game
