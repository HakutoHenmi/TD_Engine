#include "Particle.h"

#include <cmath>
#include <cstdlib> // rand用

namespace Engine {

void ParticleSystem::Initialize(Renderer& renderer, WindowDX& dx, size_t maxCount, const std::string& meshPath, const std::string& texturePath, bool sRGB, bool useBillboard) {
	(void)dx; // WindowDX はRendererが内部で使うので、Particle側では未使用でもOK

	renderer_ = &renderer;
	particles_.clear();
	particles_.resize(maxCount);
	useBillboard_ = useBillboard;

	// 新Renderer: メッシュとテクスチャをハンドルで持つ
	mesh_ = renderer_->LoadObjMesh(meshPath);
	tex_ = renderer_->LoadTexture2D(texturePath, sRGB);

	// ※ mesh_ / tex_ が 0 の場合、Draw() が何も描かずにreturnするのでクラッシュしません
}

// ★変更: angVelを受け取ってセットする
void ParticleSystem::Emit(const Vector3& pos, const Vector3& vel, const Vector3& scale, const Vector4& color, float life, const Vector3& angVel) {
	for (auto& p : particles_) {
		if (!p.active) {
			p.active = true;
			p.pos = pos;
			p.vel = vel;
			p.scale = scale;
			p.color = color;
			p.life = life;
			p.age = 0.0f;

			// 回転初期化
			p.angVel = angVel;
			// 初期角度をランダムに (0 ~ 2pi)
			float r1 = (float)(rand() % 628) / 100.0f;
			float r2 = (float)(rand() % 628) / 100.0f;
			float r3 = (float)(rand() % 628) / 100.0f;
			p.rotation = {r1, r2, r3};

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

		p.pos += p.vel * dt;

		// ★追加: 回転更新
		p.rotation += p.angVel * dt;

		// フェードアウト
		const float t = (p.life > 0.0001f) ? (p.age / p.life) : 1.0f;
		p.color.w = 1.0f - t;
	}
}

void ParticleSystem::Clear() {
	for (auto& p : particles_) {
		p.active = false;
	}
}

void ParticleSystem::Draw(const Camera& cam) {
	if (!renderer_)
		return;
	if (mesh_ == 0 || tex_ == 0)
		return;

	// Camera::Position() は XMFLOAT3 を返す仕様（あなたのEngine互換）
	const auto cp = cam.Position();
	const Vector3 camPos{cp.x, cp.y, cp.z};

	for (auto& p : particles_) {
		if (!p.active)
			continue;

		Transform tf;
		tf.translate = p.pos;
		tf.scale = p.scale;

		if (useBillboard_) {
			// Billboard：パーティクル→カメラ方向
			Vector3 d = camPos - p.pos;
			const float len = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
			if (len > 1e-6f) {
				d.x /= len;
				d.y /= len;
				d.z /= len;
			} else {
				d = {0, 0, 1};
			}

			// +Z をカメラ方向へ向ける（Yaw/Pitch）
			const float yaw = std::atan2(d.x, d.z);
			const float pitch = std::atan2(-d.y, std::sqrt(d.x * d.x + d.z * d.z));
			const float roll = 0.0f;
			tf.rotate = {pitch, yaw, roll};
		} else {
			// ★追加: 自由回転（紙吹雪など）
			tf.rotate = p.rotation;
		}

		// 新Renderer: DrawMesh(mesh, tex, transform, color)
		renderer_->DrawMesh(mesh_, tex_, tf, p.color);
	}
}

void ParticleSystem::Draw(const Camera& cam, std::string shaderName) {
	if (!renderer_)
		return;
	if (mesh_ == 0 || tex_ == 0)
		return;

	// Camera::Position() は XMFLOAT3 を返す仕様（あなたのEngine互換）
	const auto cp = cam.Position();
	const Vector3 camPos{cp.x, cp.y, cp.z};

	for (auto& p : particles_) {
		if (!p.active)
			continue;

		Transform tf;
		tf.translate = p.pos;
		tf.scale = p.scale;

		if (useBillboard_) {
			// Billboard：パーティクル→カメラ方向
			Vector3 d = camPos - p.pos;
			const float len = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
			if (len > 1e-6f) {
				d.x /= len;
				d.y /= len;
				d.z /= len;
			} else {
				d = {0, 0, 1};
			}

			// +Z をカメラ方向へ向ける（Yaw/Pitch）
			const float yaw = std::atan2(d.x, d.z);
			const float pitch = std::atan2(-d.y, std::sqrt(d.x * d.x + d.z * d.z));
			const float roll = 0.0f;
			tf.rotate = {pitch, yaw, roll};
		} else {
			// ★追加: 自由回転（紙吹雪など）
			tf.rotate = p.rotation;
		}

		// 新Renderer: DrawMesh(mesh, tex, transform, color)
		renderer_->DrawMesh(mesh_, tex_, tf, p.color,shaderName);
	}
}

} // namespace Engine