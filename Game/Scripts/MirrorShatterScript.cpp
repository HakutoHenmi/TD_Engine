#include "MirrorShatterScript.h"
#include "Scenes/GameScene.h"
#include "ScriptEngine.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace Game {

static constexpr float kCrackTime = 0.22f;

void MirrorShatterScript::Start(entt::entity entity, GameScene* scene) {
	auto& reg = scene->GetRegistry();
	if (reg.all_of<TransformComponent>(entity))
		centerPos_ = reg.get<TransformComponent>(entity).translate;

	float scale = 1.0f;
	int shardCount = 150;
	if (reg.all_of<VariableComponent>(entity)) {
		auto& vc = reg.get<VariableComponent>(entity);
		bulletDir_ = {vc.GetValue("DirX", 0), vc.GetValue("DirY", 0), vc.GetValue("DirZ", 1)};
		scale = vc.GetValue("Scale", 1.0f);
		shardCount = (int)vc.GetValue("Count", 150.0f);
		duration_ = vc.GetValue("Duration", 2.5f);
		arcRadius_ = vc.GetValue("Radius", 8.5f) * scale;
		noFlash_ = vc.GetValue("NoFlash", 0.0f) > 0.5f;
		noCracks_ = vc.GetValue("NoCracks", 0.0f) > 0.5f;
	}

	arcForward_ = bulletDir_;
	float cx = arcForward_.z, cz = -arcForward_.x;
	float cl = std::sqrt(cx * cx + cz * cz);
	arcRight_ = (cl > 0.001f) ? DirectX::XMFLOAT3{cx / cl, 0, cz / cl} : DirectX::XMFLOAT3{1, 0, 0};
	arcUp_ = {arcForward_.y * arcRight_.z - arcForward_.z * arcRight_.y, arcForward_.z * arcRight_.x - arcForward_.x * arcRight_.z, arcForward_.x * arcRight_.y - arcForward_.y * arcRight_.x};

	GenerateCurvedCracks((int)(18 * scale));
	GenerateShards(scene, (int)(shardCount * scale));
	GenerateGlassPanel(scene, (int)(40 * scale));
	if (!noFlash_) {
		GenerateLightStreaks((int)(25 * scale));
		SpawnSparks((int)(40 * scale));
	}
}

void MirrorShatterScript::GenerateLightStreaks(int count) {
	lightStreaks_.clear();
	for (int i = 0; i < count; ++i) {
		float u = ((rand() % 2000) / 1000.0f - 1.0f);
		float v = ((rand() % 400) / 1000.0f - 0.2f);
		auto n = GetArcNormal(u, v);

		LightStreak s;
		s.startPos = centerPos_;
		float len = arcRadius_ * (0.8f + (rand() % 100) / 100.0f * 0.7f);
		s.endPos = {centerPos_.x + n.x * len, centerPos_.y + n.y * len, centerPos_.z + n.z * len};
		s.delay = kCrackTime + (std::sqrtf(u * u + v * v) * 0.05f);
		s.life = 0.08f + (rand() % 100) / 100.0f * 0.08f;
		s.maxLife = s.life;
		s.width = 0.02f + (rand() % 100) / 100.0f * 0.05f;
		lightStreaks_.push_back(s);
	}
}

DirectX::XMFLOAT3 MirrorShatterScript::GetArcPoint(float u, float v) const {
	float theta = u * DirectX::XM_PI * 0.8f;
	float phi = v * DirectX::XM_PI * 0.18f;
	float lx = std::sin(theta) * std::cos(phi), ly = std::sin(phi), lz = std::cos(theta) * std::cos(phi);
	return {
	    centerPos_.x + (arcRight_.x * lx + arcUp_.x * ly + arcForward_.x * lz) * arcRadius_, centerPos_.y + (arcRight_.y * lx + arcUp_.y * ly + arcForward_.y * lz) * arcRadius_,
	    centerPos_.z + (arcRight_.z * lx + arcUp_.z * ly + arcForward_.z * lz) * arcRadius_};
}

DirectX::XMFLOAT3 MirrorShatterScript::GetArcNormal(float u, float v) const {
	float theta = u * DirectX::XM_PI * 0.8f, phi = v * DirectX::XM_PI * 0.18f;
	float lx = std::sin(theta) * std::cos(phi), ly = std::sin(phi), lz = std::cos(theta) * std::cos(phi);
	return {arcRight_.x * lx + arcUp_.x * ly + arcForward_.x * lz, arcRight_.y * lx + arcUp_.y * ly + arcForward_.y * lz, arcRight_.z * lx + arcUp_.z * ly + arcForward_.x * lz};
}

void MirrorShatterScript::GenerateShards(GameScene* scene, int count) {
	auto* renderer = scene->GetRenderer();
	if (!renderer)
		return;
	auto& reg = scene->GetRegistry();
	int cracksCount = (int)crackSegments_.size();
	if (cracksCount == 0)
		return;

	for (int i = 0; i < count; ++i) {
		const auto& seg = crackSegments_[i % cracksCount];
		float t = (rand() % 100) / 100.0f;
		DirectX::XMFLOAT3 basePos = {seg.start.x + (seg.end.x - seg.start.x) * t, seg.start.y + (seg.end.y - seg.start.y) * t, seg.start.z + (seg.end.z - seg.start.z) * t};
		float offset = 0.05f + (rand() % 100) / 100.0f * 0.4f;
		float offAngle = (rand() % 100) / 100.0f * DirectX::XM_2PI;
		DirectX::XMFLOAT3 arcP = {
		    basePos.x + (arcRight_.x * std::cos(offAngle) + arcUp_.x * std::sin(offAngle)) * offset, basePos.y + (arcRight_.y * std::cos(offAngle) + arcUp_.y * std::sin(offAngle)) * offset,
		    basePos.z + (arcRight_.z * std::cos(offAngle) + arcUp_.z * std::sin(offAngle)) * offset};

		auto ent = scene->CreateEntity("MirrorShard_VFX");
		auto& tc = reg.get<TransformComponent>(ent);
		tc.translate = arcP;
		tc.scale = {0, 0, 0};
		float dx = arcP.x - centerPos_.x, dy = arcP.y - centerPos_.y, dz = arcP.z - centerPos_.z;
		float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
		float sizeBase = (0.35f + (dist / arcRadius_) * 1.6f) * (arcRadius_ / 8.5f);
		float aspect = 0.8f + (rand() % 100) / 100.0f * 0.4f;
		float sX = sizeBase * aspect;
		float sY = sizeBase * (1.0f / aspect);
		DirectX::XMFLOAT3 n = {dx, dy, dz};
		float len = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
		if (len > 0) {
			n.x /= len;
			n.y /= len;
			n.z /= len;
		}
		tc.rotate = {((rand() % 3600) / 1800.0f) * DirectX::XM_PI, ((rand() % 3600) / 1800.0f) * DirectX::XM_PI, ((rand() % 3600) / 1800.0f) * DirectX::XM_PI};

		scene->SetTag(ent, TagType::VFX);
		auto& mrc = reg.emplace<MeshRendererComponent>(ent);
		mrc.shaderName = "GlassShatter";
		mrc.modelPath = "Resources/Models/cube/cube.obj";
		mrc.modelHandle = renderer->LoadObjMesh(mrc.modelPath);
		float intensity = 1.2f + (rand() % 100) / 100.0f * 3.5f;
		float hueShift = ((rand() % 100) / 100.0f - 0.5f) * 0.1f;
		int colorType = rand() % 4;
		if (colorType == 0)
			mrc.color = {(0.8f + hueShift) * intensity, 0.7f * intensity, 0.2f * intensity, 1.0f}; // Brass (真鍮)
		else if (colorType == 1)
			mrc.color = {(0.7f + hueShift) * intensity, 0.4f * intensity, 0.1f * intensity, 1.0f}; // Copper (銅)
		else if (colorType == 2)
			mrc.color = {2.5f * intensity, (0.5f + hueShift) * intensity, 0.1f * intensity, 1.0f}; // Red-hot Iron (赤熱)
		else
			mrc.color = {1.5f * intensity, 0.8f * intensity, 0.1f * intensity, 1.0f}; // Amber (琥珀)

		ShardPiece sp;
		sp.entity = ent;
		sp.targetPos = arcP;
		sp.arcNormal = n;
		float spd = (4.0f + (rand() % 100) / 100.0f * 9.0f) * (arcRadius_ / 8.5f);
		float noiseX = ((rand() % 100) / 100.0f - 0.5f) * 4.0f;
		float noiseY = ((rand() % 100) / 100.0f - 0.5f) * 4.0f;
		float noiseZ = ((rand() % 100) / 100.0f - 0.5f) * 4.0f;
		sp.velocity = {n.x * spd + noiseX, n.y * spd + noiseY, n.z * spd + noiseZ};
		sp.rotSpeed = {((rand() % 100) / 100.0f - 0.5f) * 14.0f, ((rand() % 100) / 100.0f - 0.5f) * 14.0f, ((rand() % 100) / 100.0f - 0.5f) * 14.0f};
		sp.sizeX = sX;
		sp.sizeY = sY;
		sp.appearDelay = seg.delay + (t * 0.06f) + 0.02f;
		shards_.push_back(sp);
	}
}

void MirrorShatterScript::GenerateGlassPanel(GameScene* scene, int count) {
	auto* renderer = scene->GetRenderer();
	if (!renderer)
		return;
	auto& reg = scene->GetRegistry();

	for (int i = 0; i < count; ++i) {
		float angle = ((float)(rand() % 1000) / 1000.0f) * DirectX::XM_2PI;
		float radius = std::sqrt((float)(rand() % 1000) / 1000.0f) * arcRadius_ * 0.6f;
		float localX = std::cos(angle) * radius;
		float localY = std::sin(angle) * radius;
		DirectX::XMFLOAT3 pos = {
		    centerPos_.x + arcRight_.x * localX + arcUp_.x * localY, centerPos_.y + arcRight_.y * localX + arcUp_.y * localY, centerPos_.z + arcRight_.z * localX + arcUp_.z * localY};

		auto ent = scene->CreateEntity("GlassPanel_VFX");
		auto& tc = reg.get<TransformComponent>(ent);
		tc.translate = pos;
		tc.scale = {0, 0, 0};
		tc.rotate = {((rand() % 3600) / 1800.0f) * DirectX::XM_PI, ((rand() % 3600) / 1800.0f) * DirectX::XM_PI, ((rand() % 3600) / 1800.0f) * DirectX::XM_PI};
		float distRatio = radius / (std::max<float>(arcRadius_ * 0.6f, 0.1f));
		float sB = (0.2f + distRatio * 0.7f) * (arcRadius_ / 4.0f);
		float aspect = 0.8f + (rand() % 100) / 100.0f * 0.4f;
		float sX = sB * aspect;
		float sY = sB * (1.0f / aspect);
		scene->SetTag(ent, TagType::VFX);
		auto& mrc = reg.emplace<MeshRendererComponent>(ent);
		mrc.shaderName = "GlassShatter";
		mrc.modelPath = "Resources/Models/cube/cube.obj";
		mrc.modelHandle = renderer->LoadObjMesh(mrc.modelPath);
		float intensity = 1.2f + (rand() % 100) / 100.0f * 2.0f;
		mrc.color = {1.2f * intensity, 0.7f * intensity, 0.1f * intensity, 0.7f}; // Amber/Copper mix

		float outX = pos.x - centerPos_.x, outY = pos.y - centerPos_.y, outZ = pos.z - centerPos_.z;
		float outLen = std::sqrt(outX * outX + outY * outY + outZ * outZ);
		if (outLen > 0.01f) {
			outX /= outLen;
			outY /= outLen;
			outZ /= outLen;
		}
		float fS = (2.0f + (rand() % 100) / 100.0f * 4.0f) * (arcRadius_ / 5.0f);
		float oS = (0.5f + (rand() % 100) / 100.0f * 2.0f) * (arcRadius_ / 5.0f);

		ShardPiece sp;
		sp.entity = ent;
		sp.targetPos = pos;
		sp.arcNormal = arcForward_;
		float randX = ((rand() % 100) / 100.0f - 0.5f) * 2.5f;
		float randY = ((rand() % 100) / 100.0f - 0.5f) * 2.5f;
		float randZ = ((rand() % 100) / 100.0f - 0.5f) * 2.5f;
		sp.velocity = {arcForward_.x * fS + outX * oS + randX, arcForward_.y * fS + outY * oS + 0.5f + randY, arcForward_.z * fS + outZ * oS + randZ};
		sp.rotSpeed = {((rand() % 100) / 100.0f - 0.5f) * 6.0f, ((rand() % 100) / 100.0f - 0.5f) * 6.0f, ((rand() % 100) / 100.0f - 0.5f) * 6.0f};
		sp.sizeX = sX;
		sp.sizeY = sY;
		sp.appearDelay = distRatio * 0.3f;
		sp.isGlassPanel = true;
		shards_.push_back(sp);
	}
}

void MirrorShatterScript::Update(entt::entity entity, GameScene* scene, float dt) {
	timer_ += dt;
	auto& reg = scene->GetRegistry();
	float oa = std::max<float>(0.0f, (timer_ > duration_ * 0.7f) ? 1.0f - (timer_ - duration_ * 0.7f) / (duration_ * 0.3f) : 1.0f);

	for (auto& sh : shards_) {
		if (!reg.valid(sh.entity))
			continue;
		auto& tc = reg.get<TransformComponent>(sh.entity);

		if (!sh.isAppeared && timer_ >= sh.appearDelay) {
			sh.isAppeared = true;
			if (sh.isGlassPanel)
				tc.scale = {sh.sizeX, sh.sizeY, sh.sizeX * 0.04f};
			else
				tc.scale = {sh.sizeX, sh.sizeY, sh.sizeX * 0.5f};
		}

		if (sh.isAppeared) {
			float scatterStartTime = sh.appearDelay + freezeTime_;
			if (!sh.isScattering && timer_ >= scatterStartTime)
				sh.isScattering = true;

			float currentAlpha = 0.0f;

			if (sh.isScattering) {
				float el = timer_ - scatterStartTime;
				float drag = std::exp(-el * 5.0f);
				float rotDrag = std::exp(-el * 0.8f);

				tc.translate.x += sh.velocity.x * drag * dt;
				tc.translate.y += sh.velocity.y * drag * dt;
				tc.translate.z += sh.velocity.z * drag * dt;

				sh.velocity.y -= 1.0f * dt;
				tc.rotate.x += sh.rotSpeed.x * rotDrag * dt;
				tc.rotate.y += sh.rotSpeed.y * rotDrag * dt;
				tc.rotate.z += sh.rotSpeed.z * rotDrag * dt;

				float lifeRatio = std::max<float>(0.0f, 1.0f - (el / 1.5f));
				float sk = lifeRatio * lifeRatio;
				if (sh.isGlassPanel)
					tc.scale = {sh.sizeX * sk, sh.sizeY * sk, sh.sizeX * sk * 0.15f};
				else
					tc.scale = {sh.sizeX * sk, sh.sizeY * sk, sh.sizeX * sk * 0.6f};

				currentAlpha = lifeRatio * 4.0f * oa;
			} else {
				float vib = std::sin(timer_ * 60.0f) * 0.005f;
				tc.translate = {sh.targetPos.x + sh.arcNormal.x * vib, sh.targetPos.y + sh.arcNormal.y * vib, sh.targetPos.z + sh.arcNormal.z * vib};

				float buildUp = std::min(1.0f, (timer_ - sh.appearDelay) / 0.3f);
				currentAlpha = buildUp * 4.0f * oa;
			}

			if (reg.all_of<MeshRendererComponent>(sh.entity))
				reg.get<MeshRendererComponent>(sh.entity).color.w = currentAlpha;
		}
	}

	// LightStreaks
	for (auto& s : lightStreaks_) {
		if (timer_ > s.delay && s.life > 0)
			s.life -= dt;
	}
	// Sparks
	for (auto& sp : sparks_) {
		if (sp.life > 0) {
			sp.pos.x += sp.velocity.x * dt;
			sp.pos.y += sp.velocity.y * dt;
			sp.pos.z += sp.velocity.z * dt;
			sp.velocity.y -= 15.0f * dt; // 重力：物理的な火花感を出す
			sp.life -= dt;
		}
	}

	if (timer_ >= duration_) {
		for (auto& sh : shards_)
			if (reg.valid(sh.entity))
				scene->DestroyObject(static_cast<uint32_t>(sh.entity));
		shards_.clear();
		scene->DestroyObject(static_cast<uint32_t>(entity));
	}
}

void MirrorShatterScript::DrawUI(entt::entity, GameScene*) {}

void MirrorShatterScript::GenerateCurvedCracks(int numBranches) {
	crackSegments_.clear();
	for (int b = 0; b < numBranches; ++b) {
		float u = 0, v = 0;
		float a = (float)b / numBranches * DirectX::XM_2PI + ((rand() % 100) / 100.0f - 0.5f) * 0.4f;
		float speed = 0.15f + (rand() % 100) / 100.0f * 0.08f;
		float du = std::cos(a) * speed, dv = std::sin(a) * speed;

		int segCount = 8 + rand() % 5;
		for (int s = 0; s < segCount; ++s) {
			auto p0 = GetArcPoint(u, v);
			du += ((rand() % 100) / 100.0f - 0.5f) * 0.05f;
			dv += ((rand() % 100) / 100.0f - 0.5f) * 0.05f;
			u += du;
			v += dv;
			float delay = (float)s * 0.05f;
			float width = 0.04f * (1.0f - (float)s / segCount * 0.6f);
			crackSegments_.push_back({p0, GetArcPoint(u, v), delay, width});

			if (s > 1 && s < segCount - 1 && (rand() % 100) < 30) {
				float ba = a + ((rand() % 100) / 100.0f - 0.5f) * 1.5f;
				float bu = u, bv = v;
				float bdu = std::cos(ba) * speed * 0.7f, bdv = std::sin(ba) * speed * 0.7f;
				int bSegCount = 3 + rand() % 3;
				for (int bs = 0; bs < bSegCount; ++bs) {
					auto bp0 = GetArcPoint(bu, bv);
					bdu += ((rand() % 100) / 100.0f - 0.5f) * 0.06f;
					bdv += ((rand() % 100) / 100.0f - 0.5f) * 0.06f;
					bu += bdu;
					bv += bdv;
					crackSegments_.push_back({bp0, GetArcPoint(bu, bv), delay + (float)bs * 0.04f, width * 0.6f});
				}
			}
		}
	}

	int ringCount = 3 + (int)(arcRadius_ / 3.0f);
	for (int r = 1; r <= ringCount; ++r) {
		float ringRadius = (float)r / (ringCount + 1);
		float ringDelay = ringRadius * 0.4f;
		int arcSegments = 12 + r * 4;
		for (int a = 0; a < arcSegments; ++a) {
			float t0 = (float)a / arcSegments;
			float t1 = (float)(a + 1) / arcSegments;
			float angle0 = t0 * DirectX::XM_2PI;
			float angle1 = t1 * DirectX::XM_2PI;
			float ru0 = std::cos(angle0) * ringRadius * 0.9f;
			float rv0 = std::sin(angle0) * ringRadius * 0.9f;
			float ru1 = std::cos(angle1) * ringRadius * 0.9f;
			float rv1 = std::sin(angle1) * ringRadius * 0.9f;
			ru0 += ((rand() % 100) / 100.0f - 0.02f);
			rv0 += ((rand() % 100) / 100.0f - 0.02f);
			crackSegments_.push_back({GetArcPoint(ru0, rv0), GetArcPoint(ru1, rv1), ringDelay, 0.02f});
		}
	}
}

void MirrorShatterScript::SpawnSparks(int count) {
	sparks_.clear();
	for (int i = 0; i < count; ++i) {
		SparkParticle sp;
		float u = ((rand() % 200) / 100.0f - 1.0f) * 0.8f, v = ((rand() % 200) / 100.0f - 1.0f) * 0.8f;
		sp.pos = GetArcPoint(u, v);
		auto n = GetArcNormal(u, v);
		float spd = 7.0f + (rand() % 100) / 100.0f * 15.0f;
		sp.velocity = {n.x * spd, n.y * spd, n.z * spd};
		sp.life = 0.2f + (rand() % 100) / 100.0f * 0.5f;
		sp.maxLife = sp.life;
		sparks_.push_back(sp);
	}
}

void MirrorShatterScript::Draw(entt::entity /*entity*/, GameScene* scene) {
	auto* renderer = scene->GetRenderer();
	if (!renderer || noCracks_)
		return;

	float fadeStart = freezeTime_ + 0.5f;
	float fadeDuration = 1.0f;
	float alpha = 1.0f;
	if (timer_ > fadeStart) {
		alpha = std::max<float>(0.0f, 1.0f - (timer_ - fadeStart) / fadeDuration);
	}
	if (alpha <= 0.0f)
		return;

	for (const auto& seg : crackSegments_) {
		if (timer_ < seg.delay)
			continue;
		float segAge = timer_ - seg.delay;
		float appear = std::min<float>(1.0f, segAge / 0.3f);
		Engine::Vector3 start = {seg.start.x, seg.start.y, seg.start.z};
		Engine::Vector3 end = {seg.end.x, seg.end.y, seg.end.z};
		if (appear < 1.0f) {
			end.x = start.x + (end.x - start.x) * appear;
			end.y = start.y + (end.y - start.y) * appear;
			end.z = start.z + (end.z - start.z) * appear;
		}
		float intensity = seg.width / 0.04f;
		float r = (0.05f + intensity * 0.3f) * alpha;
		float g = (0.8f + intensity * 1.5f) * alpha;
		float b = (1.5f + intensity * 3.0f) * alpha;
		Engine::Vector4 color = {r, g, b, alpha * appear};
		renderer->DrawLine3D(start, end, color, false);
	}

	// Draw LightStreaks
	for (const auto& s : lightStreaks_) {
		if (timer_ < s.delay || s.life <= 0)
			continue;
		float lifeRatio = s.life / s.maxLife;
		// スチームパンク風の琥珀色の光
		Engine::Vector4 color = {2.5f * lifeRatio, 1.2f * lifeRatio, 0.2f * lifeRatio, lifeRatio};
		renderer->DrawLine3D({s.startPos.x, s.startPos.y, s.startPos.z}, {s.endPos.x, s.endPos.y, s.endPos.z}, color, false);
	}

	// Draw Sparks (着弾時の火花は赤熱した鉄粉のように)
	for (const auto& sp : sparks_) {
		if (sp.life <= 0)
			continue;
		float lifeRatio = sp.life / sp.maxLife;
		Engine::Vector4 color = {4.0f * lifeRatio, 0.8f * lifeRatio, 0.1f * lifeRatio, lifeRatio};
		Engine::Vector3 p = {sp.pos.x, sp.pos.y, sp.pos.z};
		renderer->DrawLine3D(p, {p.x + sp.velocity.x * 0.015f, p.y + sp.velocity.y * 0.015f, p.z + sp.velocity.z * 0.015f}, color, false);
	}
}

REGISTER_SCRIPT(MirrorShatterScript);

} // namespace Game
