#include "BaseScript.h"
#include "ObjectTypes.h"
#include "Scenes/GameScene.h"
#include "ScriptEngine.h"
#include "../../externals/imgui/imgui.h" // ★追加
#include <cmath>

namespace Game {

static bool HasTag(entt::registry& registry, entt::entity entity, const char* tagName) {
	if (!registry.valid(entity) || !registry.all_of<TagComponent>(entity)) return false;
	return registry.get<TagComponent>(entity).tag == tagName;
}

void BaseScript::Start(entt::entity /*entity*/, GameScene* /*scene*/) {
	attackTimer_ = 0.0f; // クールダウン初期化
}

void BaseScript::Update(entt::entity entity, GameScene* scene, float dt) {
	if (!scene || !scene->GetRegistry().valid(entity) || !scene->GetRegistry().all_of<TransformComponent>(entity)) return;
	auto& registry = scene->GetRegistry();
	auto& baseTc = registry.get<TransformComponent>(entity);

	// タワーを回転させる
	baseTc.rotate.y += rotationSpeed_ * dt;

	// 発射クールダウン
	if (attackTimer_ > 0.0f) {
		attackTimer_ -= dt;
	}

	// 一番近いEnemyを探す（範囲内）
	entt::entity target = entt::null;
	float bestDistance = attackRange_;

	auto enemyView = registry.view<TagComponent, TransformComponent>();
	for (auto other : enemyView) {
		if (enemyView.get<TagComponent>(other).tag != "Enemy") continue;

		auto& otherTc = enemyView.get<TransformComponent>(other);
		float dx = otherTc.translate.x - baseTc.translate.x;
		float dz = otherTc.translate.z - baseTc.translate.z;
		float distance = std::sqrt(dx * dx + dz * dz);

		if (distance < bestDistance) {
			bestDistance = distance;
			target = other;
		}
	}

	// ターゲットがいなければ何もしない
	if (target == entt::null) return;

	// クールダウン終わってたら撃つ
	if (attackTimer_ > 0.0f) return;

	// 弾を生成して撃つ (enTT)
	auto& targetTc = registry.get<TransformComponent>(target);

	entt::entity bullet = registry.create();
	auto& bTag = registry.emplace<TagComponent>(bullet);
	bTag.tag = "Bullet";

	auto& bTc = registry.emplace<TransformComponent>(bullet);
	bTc.translate = baseTc.translate;
	bTc.translate.y += 2.0f;

	float toX = targetTc.translate.x - baseTc.translate.x;
	float toZ = targetTc.translate.z - baseTc.translate.z;

	if (std::fabs(toX) < 0.0001f && std::fabs(toZ) < 0.0001f) return;

	float desiredYaw = std::atan2(toX, toZ);
	bTc.translate.x += std::sin(desiredYaw) * 1.5f;
	bTc.translate.z += std::cos(desiredYaw) * 1.5f;
	bTc.rotate = baseTc.rotate;
	bTc.rotate.y = desiredYaw;
	bTc.scale = {0.2f, 0.2f, 0.2f};

	auto* renderer = scene->GetRenderer();
	if (renderer) {
		auto& bMr = registry.emplace<MeshRendererComponent>(bullet);
		bMr.modelHandle = renderer->LoadObjMesh("Resources/cube/cube.obj");
		bMr.textureHandle = renderer->LoadTexture2D("Resources/white1x1.png");
	}

	auto& hb = registry.emplace<HitboxComponent>(bullet);
	hb.isActive = true;
	hb.damage = damage_;
	hb.tag = "Bullet";
	hb.size = {0.2f, 0.2f, 0.2f};

	auto& hc = registry.emplace<HealthComponent>(bullet);
	hc.hp = 1.0f;
	hc.maxHp = 1.0f;

	auto& sc = registry.emplace<ScriptComponent>(bullet);
	sc.scripts.push_back({ "BulletScript", "", nullptr });

	attackTimer_ = attackInterval_;
}

void BaseScript::OnDestroy(entt::entity /*entity*/, GameScene* /*scene*/) {}

void BaseScript::OnEditorUI() {
	ImGui::DragFloat("Rotation Speed", &rotationSpeed_, 0.1f);
	ImGui::DragFloat("Attack Interval", &attackInterval_, 0.1f, 0.1f, 10.0f);
	ImGui::DragFloat("Damage", &damage_, 1.0f, 0.0f, 1000.0f);
	ImGui::DragFloat("Attack Range", &attackRange_, 1.0f, 1.0f, 100.0f);
}

std::string BaseScript::SerializeParameters() {
	std::stringstream ss;
	ss << "rotationSpeed=" << rotationSpeed_ << ";";
	ss << "attackInterval=" << attackInterval_ << ";";
	ss << "damage=" << damage_ << ";";
	ss << "attackRange=" << attackRange_ << ";";
	return ss.str();
}

void BaseScript::DeserializeParameters(const std::string& data) {
	std::stringstream ss(data);
	std::string item;
	while (std::getline(ss, item, ';')) {
		size_t pos = item.find('=');
		if (pos == std::string::npos) continue;
		std::string key = item.substr(0, pos);
		std::string val = item.substr(pos + 1);
		if (key == "rotationSpeed") rotationSpeed_ = std::stof(val);
		else if (key == "attackInterval") attackInterval_ = std::stof(val);
		else if (key == "damage") damage_ = std::stof(val);
		else if (key == "attackRange") attackRange_ = std::stof(val);
	}
}

REGISTER_SCRIPT(BaseScript);

} // namespace Game