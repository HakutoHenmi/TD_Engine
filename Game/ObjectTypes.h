#pragma once
#include "Transform.h"
#include <DirectXMath.h>
#include <cstdint>
#include <vector>
#include <string>
#include "../Engine/ParticleEmitter.h"

namespace Game {

enum class ObjectType : uint32_t {
	Cube = 0,
	Slope = 1,
	Ball = 2,
	LongFloor = 3,
	Model = 999,
};

struct CollisionMeshData {
	std::vector<DirectX::XMVECTOR> vertices;
	std::vector<int> indices;
};

// コンポーネント
enum class ComponentType { MeshRenderer, BoxCollider, Tag, Animator, Rigidbody, ParticleEmitter };
struct Component { ComponentType type; bool enabled = true; };

struct MeshRendererComponent : public Component {
	uint32_t modelHandle = 0;
	uint32_t textureHandle = 0;
	std::string modelPath;
	std::string texturePath;
	DirectX::XMFLOAT4 color = {1, 1, 1, 1};
	// ★追加: マテリアル/ライトマッププロパティ
	DirectX::XMFLOAT2 uvTiling = {1, 1};
	DirectX::XMFLOAT2 uvOffset = {0, 0};
	uint32_t lightmapHandle = 0;
	std::string lightmapPath;
	MeshRendererComponent() { type = ComponentType::MeshRenderer; }
};

struct BoxColliderComponent : public Component {
	DirectX::XMFLOAT3 center = {0, 0, 0};
	DirectX::XMFLOAT3 size = {1, 1, 1};
	bool isTrigger = false;
	BoxColliderComponent() { type = ComponentType::BoxCollider; }
};

// ★追加: アニメーターコンポーネント
struct AnimatorComponent : public Component {
	std::string currentAnimation;
	float time = 0.0f;
	float speed = 1.0f;
	bool isPlaying = false;
	bool loop = true;
	AnimatorComponent() { type = ComponentType::Animator; }
};

struct TagComponent : public Component {
	std::string tag = "Untagged";
	TagComponent() { type = ComponentType::Tag; }
};

struct RigidbodyComponent : public Component {
	DirectX::XMFLOAT3 velocity = {0.0f, 0.0f, 0.0f};
	bool useGravity = true;
	bool isKinematic = false;
	RigidbodyComponent() { type = ComponentType::Rigidbody; }
};

// ★追加: パーティクルエミッターコンポーネント
struct ParticleEmitterComponent : public Component {
	Engine::ParticleEmitter emitter;
	std::string assetPath = ""; // .particle ファイルのパス
	bool isInitialized = false;

	ParticleEmitterComponent() { type = ComponentType::ParticleEmitter; }
};

// ★ エディター用オブジェクト構造体
struct SceneObject {
	std::string name = "Object";
	bool locked = false; // ★ ロック: 選択・移動・削除を防止
	DirectX::XMFLOAT3 translate = {0, 0, 0};
	DirectX::XMFLOAT3 rotate = {0, 0, 0};
	DirectX::XMFLOAT3 scale = {1, 1, 1};
	DirectX::XMFLOAT4 color = {1, 1, 1, 1}; // ★追加: オブジェクトカラー

	uint32_t modelHandle = 0;
	uint32_t textureHandle = 0;
	std::string modelPath;   // ★追加: 保存/復元用パス
	std::string texturePath;  // ★追加: 保存/復元用パス

	// コンポーネント
	std::vector<MeshRendererComponent> meshRenderers;
	std::vector<BoxColliderComponent> boxColliders;
	std::vector<TagComponent> tags;
	std::vector<AnimatorComponent> animators;
	std::vector<RigidbodyComponent> rigidbodies;
	std::vector<ParticleEmitterComponent> particleEmitters; // ★追加: パーティクルエミッター

	Engine::Transform GetTransform() const {
		Engine::Transform t;
		t.translate = {translate.x, translate.y, translate.z};
		t.rotate = {rotate.x, rotate.y, rotate.z};
		t.scale = {scale.x, scale.y, scale.z};
		return t;
	}
	bool HasMeshRenderer() const { return !meshRenderers.empty() || modelHandle != 0; }
};

} // namespace Game