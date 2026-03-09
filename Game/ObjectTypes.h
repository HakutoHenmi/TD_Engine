#pragma once
#include "Transform.h"
#include <DirectXMath.h>
#include <cstdint>
#include <vector>
#include <string>
#include "../Engine/ParticleEmitter.h"
#include <map>
#include <memory> // ★追加

namespace Game {

class IScript; // ★追加: C++スクリプトの基底クラス前方宣言

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
enum class ComponentType { 
	MeshRenderer, BoxCollider, Tag, Animator, Rigidbody, ParticleEmitter, 
	GpuMeshCollider, PlayerInput, CharacterMovement, CameraTarget,
	DirectionalLight, PointLight, SpotLight,
	AudioSource, AudioListener, Hitbox, Hurtbox, Health, // ★追加: 音響 & 戦闘判定 & ステータス
	Script, // ★追加: スクリプトコンポーネント
	RectTransform, UIImage, UIText, UIButton, // ★追加: UIコンポーネント
	River // ★追加: 川コンポーネント
};
struct Component { 
	ComponentType type = ComponentType::MeshRenderer; 
	bool enabled = true; 
	Component() = default;
	Component(ComponentType t) : type(t), enabled(true) {}
};

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
	std::vector<uint32_t> extraTextureHandles; // ★追加
	std::vector<std::string> extraTexturePaths; // ★追加
	std::string shaderName = "Default"; // ★追加
	MeshRendererComponent() { type = ComponentType::MeshRenderer; }
};

struct BoxColliderComponent : public Component {
	DirectX::XMFLOAT3 center = {0, 0, 0};
	DirectX::XMFLOAT3 size = {1, 1, 1};
	bool isTrigger = false;
	BoxColliderComponent() { type = ComponentType::BoxCollider; }
};

enum class MeshCollisionType {
	Mesh,   // 全ポリゴン
	Convex, // 凸包近似（簡易化）
};

// ★追加: GPUメッシュコライダーコンポーネント
struct GpuMeshColliderComponent : public Component {
	uint32_t meshHandle = 0;
	std::string meshPath = "";
	MeshCollisionType collisionType = MeshCollisionType::Mesh; // 追加
	bool isTrigger = false;
	bool isIntersecting = false; // 衝突結果格納用
	GpuMeshColliderComponent() { type = ComponentType::GpuMeshCollider; }
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

// ★追加: プレイヤー入力 (意思)
struct PlayerInputComponent : public Component {
	DirectX::XMFLOAT2 moveDir = {0, 0};
	bool jumpRequested = false;
	bool attackRequested = false;

	// ★追加: マウス操作によるカメラの旋回量（intent）
	float cameraYaw = 0.0f;
	float cameraPitch = 0.0f;
	PlayerInputComponent() { type = ComponentType::PlayerInput; }
};

// ★追加: キャラクター移動 (能力)
struct CharacterMovementComponent : public Component {
	float speed = 5.0f;
	float jumpPower = 6.0f;
	float gravity = 9.8f;
	float velocityY = 0.0f;
	bool isGrounded = false;
	CharacterMovementComponent() { type = ComponentType::CharacterMovement; }
};

// ★追加: カメラ追従対象 (属性)
struct CameraTargetComponent : public Component {
	float distance = 10.0f;
	float height = 3.0f;
	float smoothSpeed = 5.0f;
	CameraTargetComponent() { type = ComponentType::CameraTarget; }
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

// ★追加: ライトコンポーネント (Directional, Point, Spot)
struct DirectionalLightComponent : public Component {
	DirectX::XMFLOAT3 color = {1.0f, 1.0f, 1.0f};
	float intensity = 1.0f;
	DirectionalLightComponent() { type = ComponentType::DirectionalLight; }
};

struct PointLightComponent : public Component {
	DirectX::XMFLOAT3 color = {1.0f, 1.0f, 1.0f};
	float intensity = 1.0f;
	float range = 10.0f;
	DirectX::XMFLOAT3 atten = {1.0f, 0.1f, 0.01f}; // 減衰(一定, 線形, 二次)
	PointLightComponent() { type = ComponentType::PointLight; }
};

struct SpotLightComponent : public Component {
	DirectX::XMFLOAT3 color = {1.0f, 1.0f, 1.0f};
	float intensity = 1.0f;
	float range = 20.0f;
	float innerCos = 0.98f; // cos(内側角度)
	float outerCos = 0.90f; // cos(外側角度)
	DirectX::XMFLOAT3 atten = {1.0f, 0.1f, 0.01f};
	SpotLightComponent() { type = ComponentType::SpotLight; }
};

// ★追加: AudioSource コンポーネント (音の発信源)
struct AudioSourceComponent : public Component {
	std::string soundPath = "";       // 音声ファイルパス
	uint32_t soundHandle = 0xFFFFFFFF; // Audio::Load()で取得したハンドル
	size_t voiceHandle = 0;            // 再生中のボイスハンドル
	float volume = 1.0f;               // 音量 (0.0〜1.0)
	bool loop = false;                 // ループ再生
	bool playOnStart = false;          // Play時に自動再生
	bool is3D = true;                  // 3Dサウンド（距離減衰あり）
	float maxDistance = 50.0f;         // 減衰最大距離
	bool isPlaying = false;            // 再生中フラグ
	AudioSourceComponent() { type = ComponentType::AudioSource; }
};

// ★追加: AudioListener コンポーネント (音の聞き取り位置、通常はカメラにアタッチ)
struct AudioListenerComponent : public Component {
	AudioListenerComponent() { type = ComponentType::AudioListener; }
};

// ★追加: Hitbox コンポーネント (攻撃判定)
struct HitboxComponent : public Component {
	DirectX::XMFLOAT3 center = {0, 0, 0}; // ローカルオフセット
	DirectX::XMFLOAT3 size = {1, 1, 1};    // 判定サイズ
	float damage = 10.0f;                  // ダメージ量
	bool isActive = false;                 // 有効フラグ（攻撃アニメ中のみtrue等）
	std::string tag = "Default";           // 識別タグ ("Sword", "Projectile"等)
	HitboxComponent() { type = ComponentType::Hitbox; }
};

// ★追加: Hurtbox コンポーネント (食らい判定)
struct HurtboxComponent : public Component {
	DirectX::XMFLOAT3 center = {0, 0, 0}; // ローカルオフセット
	DirectX::XMFLOAT3 size = {1, 1, 1};    // 判定サイズ
	std::string tag = "Body";              // 識別タグ ("Body", "Head"等)
	float damageMultiplier = 1.0f;         // ダメージ倍率 (頭部=2.0等)
	HurtboxComponent() { type = ComponentType::Hurtbox; }
};

// ★追加: Health コンポーネント (ステータス管理)
struct HealthComponent : public Component {
	float hp = 100.0f;               // 現在の体力
	float maxHp = 100.0f;            // 最大体力
	float stamina = 100.0f;          // スタミナ
	float maxStamina = 100.0f;       // 最大スタミナ
	float invincibleTime = 0.0f;     // 残り無敵時間（ゼロ以上なら無敵）
	bool isDead = false;             // 死亡フラグ
	HealthComponent() { type = ComponentType::Health; }
};

// ★追加: UIコンポーネント
struct RectTransformComponent : public Component {
	DirectX::XMFLOAT2 pos = {0, 0};   // スクリーン座標
	DirectX::XMFLOAT2 size = {100, 100};
	DirectX::XMFLOAT2 anchor = {0.5f, 0.5f}; // 0.0〜1.0
	DirectX::XMFLOAT2 pivot = {0.5f, 0.5f};
	float rotation = 0.0f;
	RectTransformComponent() { type = ComponentType::RectTransform; }
};

struct UIImageComponent : public Component {
	uint32_t textureHandle = 0;
	std::string texturePath = "";
	DirectX::XMFLOAT4 color = {1, 1, 1, 1};
	bool is9Slice = false;
	float borderTop = 10.0f;
	float borderBottom = 10.0f;
	float borderLeft = 10.0f;
	float borderRight = 10.0f;
	UIImageComponent() { type = ComponentType::UIImage; }
};

struct UITextComponent : public Component {
	std::string text = "New Text";
	float fontSize = 24.0f;
	DirectX::XMFLOAT4 color = {1, 1, 1, 1};
	UITextComponent() { type = ComponentType::UIText; }
};

struct UIButtonComponent : public Component {
	bool isHovered = false;
	bool isPressed = false;
	DirectX::XMFLOAT4 normalColor = {1, 1, 1, 1};
	DirectX::XMFLOAT4 hoverColor = {0.8f, 0.8f, 0.8f, 1.0f};
	DirectX::XMFLOAT4 pressedColor = {0.6f, 0.6f, 0.6f, 1.0f};
	std::string onClickCallback = ""; // スクリプト側のメソッド名など
	
	// ★追加: 判定エリアの個別調整用
	DirectX::XMFLOAT2 hitboxOffset = {0.0f, 0.0f};
	DirectX::XMFLOAT2 hitboxScale = {1.0f, 1.0f};

	UIButtonComponent() { type = ComponentType::UIButton; }
};

// ★変更: Script コンポーネント (ロジックの外部化)
struct ScriptComponent : public Component {
	std::string scriptPath = ""; // スクリプトのクラス名 (例: "PlayerScript")
	std::shared_ptr<IScript> instance = nullptr; // C++スクリプトのインスタンス
	ScriptComponent() { type = ComponentType::Script; }
};

// ★追加: River コンポーネント
struct RiverComponent : public Component {
	std::vector<DirectX::XMFLOAT3> points; // スプライン制御点 (ローカル座標)
	float width = 2.0f;                    // 川の基本幅
	float flowSpeed = 1.0f;                // 流れの速さ
	float uvScale = 1.0f;
	uint32_t meshHandle = 0;               // 動的生成メッシュハンドル
	std::string texturePath = "Resources/Water/water.png";
	RiverComponent() { type = ComponentType::River; }
};

// ★ エディター用オブジェクト構造体
struct SceneObject {
	uint32_t id = 0;           // ★ 個別識別子
	uint32_t parentId = 0;     // ★ 親オブジェクトのID（0は親なし）
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
	std::vector<std::string> extraTexturePaths; // ★追加
	std::string shaderName = "Default"; // ★追加

	// コンポーネント
	std::vector<MeshRendererComponent> meshRenderers;
	std::vector<BoxColliderComponent> boxColliders;
	std::vector<TagComponent> tags;
	std::vector<AnimatorComponent> animators;
	std::vector<RigidbodyComponent> rigidbodies;
	std::vector<ParticleEmitterComponent> particleEmitters; // ★追加: パーティクルエミッター
	std::vector<GpuMeshColliderComponent> gpuMeshColliders; // ★追加: GPUメッシュコライダー
	std::vector<PlayerInputComponent> playerInputs;
	std::vector<CharacterMovementComponent> characterMovements;
	std::vector<CameraTargetComponent> cameraTargets;

	// ★追加: ライトコンポーネント
	std::vector<DirectionalLightComponent> directionalLights;
	std::vector<PointLightComponent> pointLights;
	std::vector<SpotLightComponent> spotLights;

	// ★追加: 音響コンポーネント
	std::vector<AudioSourceComponent> audioSources;
	std::vector<AudioListenerComponent> audioListeners;

	// ★追加: 戦闘判定コンポーネント
	std::vector<HitboxComponent> hitboxes;
	std::vector<HurtboxComponent> hurtboxes;

	// ★追加: ステータス管理コンポーネント
	std::vector<HealthComponent> healths;

	// ★追加: スクリプトコンポーネント
	std::vector<ScriptComponent> scripts;

	// ★追加: UIコンポーネント
	std::vector<RectTransformComponent> rectTransforms;
	std::vector<UIImageComponent> images;
	std::vector<UITextComponent> texts;
	std::vector<UIButtonComponent> buttons;

	// ★追加: 川コンポーネント
	std::vector<RiverComponent> rivers;

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