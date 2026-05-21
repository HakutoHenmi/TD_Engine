#include "Particle.h"

#include <cmath>
#include <cstdlib> // rand用

namespace Engine {

// ★追加: グローバルパーティクルバジェットの静的メンバ初期化
std::atomic<uint32_t> ParticleBudget::activeCount_{0};

void ParticleSystem::Initialize(Renderer& renderer, size_t maxCount, const std::string& meshPath, const std::string& texturePath, bool sRGB, bool useBillboard) {

	renderer_ = &renderer;
	particles_.clear();
	particles_.resize(maxCount);
	useBillboard_ = useBillboard;

	// 新Renderer: メッシュとテクスチャをハンドルで持つ
	mesh_ = renderer_->LoadObjMesh(meshPath);
	tex_ = renderer_->LoadTexture2D(texturePath, sRGB);

	// ※ mesh_ / tex_ が 0 の場合、Draw() が何も描かずにreturnするのでクラッシュしません
}

// ★変更: angVelとdampingを受け取ってセットする
// ★追加: アクティブパーティクル数を取得
uint32_t ParticleSystem::GetActiveCount() const {
	uint32_t count = 0;
	for (const auto& p : particles_) {
		if (p.active) ++count;
	}
	return count;
}

// ★変更: angVelとdampingを受け取ってセットする + バジェットチェック
void ParticleSystem::Emit(const Vector3& pos, const Vector3& vel, const Vector3& acceleration, 
			  const Vector3& startScale, const Vector3& endScale,
			  const Vector4& startColor, const Vector4& endColor, 
			  float life, const Vector3& angVel, float damping) {
	// ★追加: グローバルバジェットチェック — 上限超過時はスキップ
	if (!ParticleBudget::CanEmit()) return;

	for (auto& p : particles_) {
		if (!p.active) {
			p.active = true;
			p.pos = pos;
			p.vel = vel;
			p.acceleration = acceleration;
			p.startScale = startScale;
			p.endScale = endScale;
			p.scale = startScale;
			p.startColor = startColor;
			p.endColor = endColor;
			p.color = startColor;
			p.life = life;
			p.age = 0.0f;
			p.damping = damping;

			// 回転初期化
			p.angVel = angVel;
			// 初期角度をランダムに (0 ~ 2pi)
			float r1 = (float)(rand() % 628) / 100.0f;
			float r2 = (float)(rand() % 628) / 100.0f;
			float r3 = (float)(rand() % 628) / 100.0f;
			p.rotation = {r1, r2, r3};

			// ★追加: バジェットに登録
			ParticleBudget::Register(1);

			break;
		}
	}
}

void ParticleSystem::Update(float dt) {
	for (auto& p : particles_) {
		if (!p.active)
			continue;

		p.age += dt;
		if (p.age >= p.life) {
			p.active = false;
			continue;
		}

		// ★追加: 摩擦(減衰)の適用
		if (p.damping > 0.0f) {
			float dampFactor = (std::max)(0.0f, 1.0f - p.damping * dt); // Windowsマクロ回避のためカッコ追加
			p.vel.x *= dampFactor;
			p.vel.y *= dampFactor;
			p.vel.z *= dampFactor;
		}

		p.vel += p.acceleration * dt;
		p.pos += p.vel * dt;

		// ★追加: 回転更新
		p.rotation += p.angVel * dt;

		// 補間
		const float t = (p.life > 0.0001f) ? (p.age / p.life) : 1.0f;
		
		p.scale.x = p.startScale.x + (p.endScale.x - p.startScale.x) * t;
		p.scale.y = p.startScale.y + (p.endScale.y - p.startScale.y) * t;
		p.scale.z = p.startScale.z + (p.endScale.z - p.startScale.z) * t;
		
		p.color.x = p.startColor.x + (p.endColor.x - p.startColor.x) * t;
		p.color.y = p.startColor.y + (p.endColor.y - p.startColor.y) * t;
		p.color.z = p.startColor.z + (p.endColor.z - p.startColor.z) * t;
		p.color.w = p.startColor.w + (p.endColor.w - p.startColor.w) * t;
	}
}

void ParticleSystem::Clear() {
	for (auto& p : particles_) {
		p.active = false;
	}
}



void ParticleSystem::Draw(const Camera& cam, const std::string& shaderName, bool useUvAnim, 
						  int uvCols, int uvRows, float uvFps) {
	if (!renderer_)
		return;
	if (mesh_ == 0 || tex_ == 0)
		return;

	// Camera::Position() は XMFLOAT3 を返す仕様（あなたのEngine互換）
	const auto cp = cam.Position();
	const Vector3 camPos{cp.x, cp.y, cp.z};

	// ★追加: 距離ベースLODカリング
	float dx = emitterPosition_.x - camPos.x;
	float dy = emitterPosition_.y - camPos.y;
	float dz = emitterPosition_.z - camPos.z;
	float distSq = dx * dx + dy * dy + dz * dz;

	// 完全カリング: kLodFarDist 以上は描画しない
	if (distSq > kLodFarDist * kLodFarDist) return;

	// LODスケール計算: 距離に応じてスキップ率を決定
	int skipRate = 1; // 1 = 全部描画
	if (distSq > kLodNearDist * kLodNearDist) {
		float dist = std::sqrt(distSq);
		float lodT = (dist - kLodNearDist) / (kLodFarDist - kLodNearDist);
		// lodT: 0.0(近い)〜1.0(遠い)
		// skipRate: 1(全部)→2(半分)→4(1/4)と段階的に
		if (lodT > 0.66f) skipRate = 4;
		else if (lodT > 0.33f) skipRate = 3;
		else skipRate = 2;
	}

	int drawIdx = 0;
	for (auto& p : particles_) {
		if (!p.active)
			continue;

		// ★追加: LODスキップ（遠方では間引き描画）
		if (skipRate > 1 && (drawIdx++ % skipRate) != 0)
			continue;

		Transform tf;
		tf.translate = p.pos;
		tf.scale = p.scale;

		if (useBillboard_) {
			// ★修正: ジンバルロック（フリップ現象）によるがくがくを防ぐため、
			// カメラのオイラー角を使った Screen-Aligned Billboard に変更
			Engine::Vector3 camRot = cam.GetRotation();
			// カメラに対して常に正対させる（+Zをカメラ方向に向ける）
			float pitch = -camRot.x;
			float yaw = camRot.y + 3.14159265f;
			float roll = p.rotation.z; // パーティクルのZ回転（ロール）を適用できるようにする

			tf.rotate = {pitch, yaw, roll};
		} else {
			// ★追加: 自由回転（紙吹雪など）
			tf.rotate = p.rotation;
		}

		Vector4 uvScaleOffset = {1.0f, 1.0f, 0.0f, 0.0f};
		if (useUvAnim && uvCols > 0 && uvRows > 0) {
			uvScaleOffset.x = 1.0f / uvCols; // Scale U
			uvScaleOffset.y = 1.0f / uvRows; // Scale V

			int totalFrames = uvCols * uvRows;
			int currentFrame = static_cast<int>(p.age * uvFps) % totalFrames;

			int colIdx = currentFrame % uvCols;
			int rowIdx = currentFrame / uvCols;

			uvScaleOffset.z = colIdx * uvScaleOffset.x; // Offset U
			uvScaleOffset.w = rowIdx * uvScaleOffset.y; // Offset V
		}

		// ★GPUインスタンシングを使用して描画予約
		renderer_->DrawParticleInstanced(mesh_, tex_, tf, p.color, uvScaleOffset, shaderName);
	}
}

} // namespace Engine