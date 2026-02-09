#pragma once
#include <string>
#include <vector>

#include "Camera.h"
#include "Matrix4x4.h"
#include "Renderer.h"
#include "Transform.h"
#include "WindowDX.h"

namespace Engine {

struct Particle {
	Vector3 pos{};
	Vector3 vel{};
	Vector3 scale{1, 1, 1};
	Vector4 color{1, 1, 1, 1};
	float life = 1.0f;
	float age = 0.0f;
	bool active = false;

	// ★追加: 回転制御用
	Vector3 rotation{}; // 現在の回転角 (ラジアン)
	Vector3 angVel{};   // 回転速度
};

class ParticleSystem {
public:
	// texturePath は任意。存在する png を指定してください（例: "Resources/uvChecker.png"）
	// ★変更: useBillboard 引数を追加（デフォルトtrue）
	void Initialize(
	    Renderer& renderer, WindowDX& dx, size_t maxCount = 1000, const std::string& meshPath = "Resources/plane.obj", const std::string& texturePath = "Resources/uvChecker.png", bool sRGB = true,
	    bool useBillboard = true);

	void Update(float dt);
	void Draw(const Camera& cam);
	void Draw(const Camera& cam, std::string shaderName);
	

	void Clear();

	// ★変更: 回転速度(angVel)引数を追加（デフォルト0）
	void Emit(const Vector3& pos, const Vector3& vel, const Vector3& scale, const Vector4& color, float life, const Vector3& angVel = {0, 0, 0});

private:
	Renderer* renderer_ = nullptr;

	Renderer::MeshHandle mesh_ = 0;
	Renderer::TextureHandle tex_ = 0;

	std::vector<Particle> particles_;

	// ★追加: ビルボードを使用するかどうか
	bool useBillboard_ = true;
};

} // namespace Engine