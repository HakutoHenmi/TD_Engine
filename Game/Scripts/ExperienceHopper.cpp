#include "ExperienceHopper.h"
#include "ScriptEngine.h"
#include "../imgui/imgui.h"
namespace Game {
// タグ走査
static bool HasTag(const SceneObject& obj, const char* tagName) {
	for (int i = 0; i < (int)obj.tags.size(); ++i) {
		if (obj.tags[i].tag == tagName) {
			return true;
		}
	}
	return false;
}

static int CountExperienceOrbs(GameScene* scene) {
	int orbCount = 0;

	for (const SceneObject& other : scene->GetObjects()) {
		if (HasTag(other, "ExperienceOrb")) {
			orbCount += 1;
		}
	}

	return orbCount;
}
// 球体接続判定
static bool IsConnectedSphere(const SceneObject& a, const SceneObject& b, float connectRange) {
	float dx = b.translate.x - a.translate.x;
	float dy = b.translate.y - a.translate.y;
	float dz = b.translate.z - a.translate.z;

	float dist = std::sqrt(dx * dx + dy * dy + dz * dz);

	if (dist > connectRange) {
		return false;
	}

	return true;
}

// 訪問済みチェック
static bool IsAlreadyVisited(const std::vector<const SceneObject*>& visitedObjects, const SceneObject& obj) {
	for (int i = 0; i < (int)visitedObjects.size(); ++i) {
		if (visitedObjects[i] == &obj) {
			return true;
		}
	}
	return false;
}

// Pipe をたどって ExperienceMiner に届くか
static bool IsPipeConnectedToExperienceMinerRecursive(GameScene* scene, const SceneObject& currentPipe, std::vector<const SceneObject*>& visitedObjects, float connectRange) {

	visitedObjects.push_back(&currentPipe);

	for (const SceneObject& other : scene->GetObjects()) {

		if (&other == &currentPipe) {
			continue;
		}

		if (!IsConnectedSphere(currentPipe, other, connectRange)) {
			continue;
		}

		if (HasTag(other, "ExperienceMiner")) {
			return true;
		}

		if (HasTag(other, "Pipe")) {

			if (IsAlreadyVisited(visitedObjects, other)) {
				continue;
			}

			bool connected = IsPipeConnectedToExperienceMinerRecursive(scene, other, visitedObjects, connectRange);

			if (connected) {
				return true;
			}
		}
	}

	return false;
}

// ExperienceHopper から Pipe を通して ExperienceMiner につながるか
static bool IsExperienceHopperConnectedToExperienceMiner(GameScene* scene, const SceneObject& hopperObj) {
	const float connectRange = 2.5f;

	for (const SceneObject& other : scene->GetObjects()) {

		if (!HasTag(other, "Pipe")) {
			continue;
		}

		if (!IsConnectedSphere(hopperObj, other, connectRange)) {
			continue;
		}

		std::vector<const SceneObject*> visitedObjects;

		bool connected = IsPipeConnectedToExperienceMinerRecursive(scene, other, visitedObjects, connectRange);

		if (connected) {
			return true;
		}
	}

	return false;
}

void Game::ExperienceHopper::Start(SceneObject& obj, GameScene* scene) {
	(void)obj;
	(void)scene;
}

void ExperienceHopper::Update(SceneObject& obj, GameScene* scene, float dt) {
	obj.rotate.y += 1.0f * dt;

	if (spawnTimer_ > 0.0f) {
		spawnTimer_ -= dt;
	}

	bool connected = IsExperienceHopperConnectedToExperienceMiner(scene, obj);
	int orbCount = CountExperienceOrbs(scene);

	ImGui::Begin("ExperienceHopper Debug");
	ImGui::Text("Connected: %s", connected ? "true" : "false");
	ImGui::Text("OrbCount: %d", orbCount);
	ImGui::Text("SpawnTimer: %.2f", spawnTimer_);
	ImGui::End();

	if (!connected) {
		return;
	}

	if (orbCount >= 100) {
		return;
	}

	if (spawnTimer_ > 0.0f) {
		return;
	}

	SceneObject orb;
	orb.name = "ExperienceOrb";
	orb.translate = obj.translate;
	orb.translate.y += 0.5f;
	orb.scale = {0.2f, 0.2f, 0.2f};
	orb.rotate = {0.0f, 0.0f, 0.0f};

	TagComponent tag;
	tag.tag = "ExperienceOrb";
	orb.tags.push_back(tag);

	HealthComponent health;
	health.hp = 1.0f;
	health.maxHp = 1.0f;
	orb.healths.push_back(health);

	HitboxComponent hitbox;
	hitbox.isActive = true;
	hitbox.damage = 0.0f;
	hitbox.tag = "ExperienceOrb";
	hitbox.size = {1.0f, 1.0f, 1.0f};
	orb.hitboxes.push_back(hitbox);

	auto* renderer = scene->GetRenderer();
	if (renderer) {
		orb.modelHandle = renderer->LoadObjMesh("Resources/cube/cube.obj");
		orb.textureHandle = renderer->LoadTexture2D("Resources/white1x1.png");

		MeshRendererComponent meshRenderer;
		meshRenderer.modelHandle = orb.modelHandle;
		meshRenderer.textureHandle = orb.textureHandle;
		orb.meshRenderers.push_back(meshRenderer);
	}

	ScriptComponent script;
	script.scriptPath = "ExperienceOrbScript";
	orb.scripts.push_back(script);

	scene->SpawnObject(orb);

	spawnTimer_ = 1.0f;
}

void Game::ExperienceHopper::OnDestroy(SceneObject& obj, GameScene* scene) {
	(void)obj;
	(void)scene;
}

REGISTER_SCRIPT(ExperienceHopper);
} // namespace Game