// Engine/Renderer.h
#pragma once

#include <cstdint>
#include <memory> // std::shared_ptr
#include <string>
#include <unordered_map>
#include <vector>

#include <d3d12.h>
#include <wrl/client.h>

#include "Camera.h"
#include "Matrix4x4.h"
#include "Transform.h"
#include "WindowDX.h"

// Modelクラスを利用するためインクルード
#include "Model.h"

namespace Engine {

class Renderer final {
public:
	using MeshHandle = uint32_t;
	using TextureHandle = uint32_t;

	struct SpriteDesc {
		float x = 0.0f;
		float y = 0.0f;
		float w = 64.0f;
		float h = 64.0f;
		float rotationRad = 0.0f;
		Vector4 color{1, 1, 1, 1};
		Vector4 uvScaleOffset{1.0f, 1.0f, 0.0f, 0.0f}; // ★追加: UVスケール・オフセット
	};

	// --- ライト構造体 ---
	struct DirectionalLight {
		Vector3 direction{0, -1, 0};
		float _pad0;
		Vector3 color{1, 1, 1};
		float _pad1;
		uint32_t enabled = 0;
		float _pad2[3];
	};

	struct PointLight {
		Vector3 position{0, 0, 0};
		float _pad0;
		Vector3 color{1, 1, 1};
		float range = 10.0f;
		Vector3 atten{1.0f, 0.1f, 0.01f};
		float _pad1;
		uint32_t enabled = 0;
		float _pad2[3];
	};

	struct SpotLight {
		Vector3 position{0, 0, 0};
		float _pad0;
		Vector3 direction{0, -1, 0};
		float range = 20.0f;
		Vector3 color{1, 1, 1};
		float innerCos = 0.98f;
		Vector3 atten{1.0f, 0.1f, 0.01f};
		float outerCos = 0.90f;
		uint32_t enabled = 0;
		float _pad[3];
	};

	struct AreaLight {
		Vector3 position{0, 0, 0};
		float _pad0;
		Vector3 color{1, 1, 1};
		float range = 10.0f;
		Vector3 right{1, 0, 0};
		float halfWidth = 1.0f;
		Vector3 up{0, 1, 0};
		float halfHeight = 1.0f;
		Vector3 direction{0, 0, 1};
		float _pad1;
		Vector3 atten{1.0f, 0.1f, 0.01f};
		float _pad2;
		uint32_t enabled = 0;
		float _pad3[3];
	};

	static constexpr int kMaxDirLights = 1;
	static constexpr int kMaxPointLights = 4;
	static constexpr int kMaxSpotLights = 4;
	static constexpr int kMaxAreaLights = 4;

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4324)
#endif
	struct alignas(256) LightCB {
		Vector3 ambientColor{0.1f, 0.1f, 0.1f};
		float _pad0 = 0.0f;

		DirectionalLight dirLights[kMaxDirLights];
		PointLight pointLights[kMaxPointLights];
		SpotLight spotLights[kMaxSpotLights];
		AreaLight areaLights[kMaxAreaLights];
		
		Matrix4x4 shadowMatrix; // 追加: シャドウマッピング用行列
	};
#ifdef _MSC_VER
#pragma warning(pop)
#endif

	struct PostProcessParams {
		float time = 0.0f;
		float noiseStrength = 0.0f;
		float distortion = 0.0f;
		float chromaShift = 0.0f;
		float vignette = 0.0f;
		float scanline = 0.0f;
		float san = 0.0f;
	};

public:
	Renderer() = default;
	~Renderer();

	Renderer(const Renderer&) = delete;
	Renderer& operator=(const Renderer&) = delete;

	void SetPostEffect(const std::string& name);

public:
	bool Initialize(WindowDX* window);
	void Shutdown();

	static Renderer* GetInstance() { return instance_; }

	void BeginFrame(const float clearColorRGBA[4]);
	void EndFrame();

	void SetPostProcessEnabled(bool on) { ppEnabled_ = on; }
	bool GetPostProcessEnabled() const { return ppEnabled_; }

	void SetPostProcessParams(const PostProcessParams& p) { ppParams_ = p; }
	const PostProcessParams& GetPostProcessParams() const { return ppParams_; }

	D3D12_GPU_DESCRIPTOR_HANDLE GetPostProcessSRV() const { return ppSrvGpu_; }

	// ★追加: Gameシーンの最終出力テクスチャ。エディタUIからここを描画する。
	D3D12_GPU_DESCRIPTOR_HANDLE GetGameFinalSRV() const { return finalSrvGpu_; }

	// ★追加: 描画領域を強制上書きするメソッド
	// これを呼び出すと、次回の BeginFrame()/EndFrame() において指定領域に描画される
	void SetGameViewport(float x, float y, float w, float h);

	// ★追加: 描画領域を元（フルスクリーン）に戻すメソッド
	void ResetGameViewport();

	// ★追加: 外部（ParticleEditorなど）用のカスタムレンダーターゲット
	struct CustomRenderTarget {
		Microsoft::WRL::ComPtr<ID3D12Resource> texture;
		Microsoft::WRL::ComPtr<ID3D12Resource> depth;
		D3D12_CPU_DESCRIPTOR_HANDLE rtv{};
		D3D12_CPU_DESCRIPTOR_HANDLE dsv{};
		D3D12_GPU_DESCRIPTOR_HANDLE srvGpu{};
		uint32_t width = 0;
		uint32_t height = 0;
	};

	CustomRenderTarget CreateRenderTarget(uint32_t width, uint32_t height);
	void BeginCustomRenderTarget(const CustomRenderTarget& target);
	void EndCustomRenderTarget();

	void SetCamera(const Camera& camera);

	void SetAmbientColor(const Vector3& color);
	void SetDirectionalLight(const Vector3& dir, const Vector3& color, bool enabled = true);
	void SetPointLight(int index, const Vector3& pos, const Vector3& color, float range, const Vector3& atten = {1.0f, 0.1f, 0.01f}, bool enabled = true);
	void SetSpotLight(
	    int index, const Vector3& pos, const Vector3& dir, const Vector3& color, float range, float innerCos, float outerCos, const Vector3& atten = {1.0f, 0.1f, 0.01f}, bool enabled = true);
	void SetAreaLight(
	    int index, const Vector3& pos, const Vector3& color, float range, const Vector3& right, const Vector3& up, float halfW, float halfH, const Vector3& atten = {1.0f, 0.1f, 0.01f},
	    bool enabled = true);

	void SetLightCB(const LightCB& cb) { lightCB_ = cb; }
	LightCB GetLightCB() const { return lightCB_; }

	TextureHandle LoadTexture2D(const std::string& filePath, bool sRGB = true);
	MeshHandle LoadObjMesh(const std::string& objFilePath);

	// ★追加: 動的メッシュの作成と更新
	MeshHandle CreateDynamicMesh(const std::vector<VertexData>& vertices, const std::vector<uint32_t>& indices);
	void UpdateDynamicMesh(MeshHandle handle, const std::vector<VertexData>& vertices);

	// ★追加: テクスチャのSRVハンドルを取得 (ImGui::Imageでサムネイル表示用)
	D3D12_GPU_DESCRIPTOR_HANDLE GetTextureSrvGpu(TextureHandle handle) const {
		if (handle < textures_.size()) return textures_[handle].srvGpu;
		return D3D12_GPU_DESCRIPTOR_HANDLE{0};
	}

	// 通常メッシュ描画
	void DrawMesh(MeshHandle mesh, TextureHandle texture, const Transform& transform, const Vector4& mulColor, const std::string& shaderName = "Default");

	// インスタンス描画の予約
	void DrawMeshInstanced(MeshHandle mesh, TextureHandle texture, const Transform& transform, const Vector4& mulColor, 
						   const std::string& shaderName = "Default", const std::vector<TextureHandle>& extraTex = {});

	// ★追加: パーティクル インスタンス描画
	void DrawParticleInstanced(MeshHandle mesh, TextureHandle texture, const Transform& transform, const Vector4& mulColor, const Vector4& uvScaleOffset, const std::string& shaderName = "Particle");

	// ★追加: パーティクル描画 (UVスケール・オフセット付き)
	void DrawParticle(MeshHandle mesh, TextureHandle texture, const Transform& transform, 
					  const Vector4& mulColor, const Vector4& uvScaleOffset, 
					  const std::string& shaderName = "Particle");

	// ★スキニングメッシュ描画
	void DrawSkinnedMesh(MeshHandle mesh, TextureHandle texture, const Transform& transform, const std::vector<Matrix4x4>& bones, const Vector4& mulColor = {1, 1, 1, 1});

	// スプライト描画
	struct SpriteDrawCall {
		TextureHandle tex;
		SpriteDesc desc;
	};

	struct Sprite9SliceDesc {
		float x, y, w, h;
		float left, right, top, bottom; // border in pixels
		Vector4 color{1,1,1,1};
		float rotationRad = 0;
	};
	void DrawSprite(TextureHandle texH, const SpriteDesc& s);
	void DrawSprite9Slice(TextureHandle texH, const Sprite9SliceDesc& s); // ★追加
	void FlushSprites(); // スプライトの描画実行

	// ★追加: 3Dライン描画（エディタ用ギズモ・グリッドなど）
	void DrawLine3D(const Vector3& p0, const Vector3& p1, const Vector4& color, bool xray = false);
	void FlushLines();

	// ★追加: 現在のドローコールを直ちにフラッシュ（描画発行）し、キューをクリアする
	void FlushDrawCalls();

	// ★追加: コンピュートシェーダーで衝突判定を実行
	void BeginCollisionCheck(uint32_t maxPairs = 1024);
	void DispatchCollision(MeshHandle meshA, const Transform& trA, MeshHandle meshB, const Transform& trB, uint32_t resultIndex);
	void EndCollisionCheck();
	bool GetCollisionResult(uint32_t resultIndex) const;

	bool CreateShaderPipeline(const std::string& shaderName, const std::wstring& vsPath, const std::wstring& psPath);
	const std::vector<std::string>& GetShaderNames() const { return shaderNames_; }

	// Modelへのポインタを取得
	Model* GetModel(MeshHandle handle);
	// 追加：透明/加算 シェーダー登録
	bool CreateShaderPipelineTransparent(const std::string& shaderName, const std::wstring& vsPath, const std::wstring& psPath, bool additive);
	// 追加：透明/加算 用 PSO作成
	bool CreatePSO_Transparent(const std::string& name, ID3DBlob* vsBlob, ID3DBlob* psBlob, bool additive);

	bool CreatePSOAlpha(const std::string& name, ID3DBlob* vsBlob, ID3DBlob* psBlob);

	// ★追加: 風とプレイヤーの位置設定
	void SetWindParams(const Vector4& p) { cbFrame_.windParams = p; }
	void SetPlayerPos(const Vector3& p) { cbFrame_.playerPos = p; }

private:
	struct Mesh {
		Microsoft::WRL::ComPtr<ID3D12Resource> vb;
		Microsoft::WRL::ComPtr<ID3D12Resource> ib;
		D3D12_VERTEX_BUFFER_VIEW vbView{};
		D3D12_INDEX_BUFFER_VIEW ibView{};
		uint32_t indexCount = 0;
	};

	struct DrawCall {
		MeshHandle mesh;
		TextureHandle tex;
		std::vector<TextureHandle> extraTex; // ★追加: 地形用の追加テクスチャ
		Transform tr;
		Vector4 color;
		std::string shaderName;
		bool isSkinned = false;
		std::vector<Matrix4x4> bones;
		bool isParticle = false;
		Vector4 uvScaleOffset;
	};

	struct InstanceData {
		Matrix4x4 world;
		Vector4 color;
		Vector4 uvScaleOffset; // ★追加: UVスケール・オフセット用
	};

	struct InstancedDrawCall {
		MeshHandle mesh;
		TextureHandle tex;
		std::vector<TextureHandle> extraTex; // ★追加
		std::string shaderName;
		std::vector<InstanceData> instances;
	};

private:
	struct Texture {
		Microsoft::WRL::ComPtr<ID3D12Resource> res;
		D3D12_CPU_DESCRIPTOR_HANDLE srvCpu{};
		D3D12_GPU_DESCRIPTOR_HANDLE srvGpu{};
	};

	struct UploadRing {
		Microsoft::WRL::ComPtr<ID3D12Resource> buffer;
		uint8_t* mapped = nullptr;
		uint32_t sizeBytes = 0;
		uint32_t offset = 0;

		void Reset() { offset = 0; }
		uint32_t Allocate(uint32_t bytes, uint32_t alignment);
	};

private:
	uint32_t AllocateSrvIndex(uint32_t count = 1);
	uint32_t AllocateDynamicSrvIndex(uint32_t count = 1);
	Microsoft::WRL::ComPtr<ID3DBlob> CompileShader(const char* src, const char* entry, const char* target);
	Microsoft::WRL::ComPtr<ID3DBlob> CompileShaderFromFile(const wchar_t* filePath, const char* entry, const char* target);

	bool InitPipelines();
	bool InitPostProcess_();
	bool CreatePSO(const std::string& name, ID3DBlob* vsBlob, ID3DBlob* psBlob);
	bool CreatePSO(const std::string& name, ID3DBlob* vsBlob, ID3DBlob* psBlob, const D3D12_INPUT_ELEMENT_DESC* layout, UINT numElements);

	void WaitGPU();

private:
	static Renderer* instance_;

	WindowDX* window_ = nullptr;

	ID3D12Device* dev_ = nullptr;
	ID3D12GraphicsCommandList* list_ = nullptr;
	ID3D12CommandQueue* queue_ = nullptr;

	ID3D12DescriptorHeap* srvHeap_ = nullptr;
	uint32_t srvInc_ = 0;

	uint32_t srvCursor_ = 10;
	uint32_t srvDynamicCursor_ = 0;
	static constexpr uint32_t kSrvStaticMax = 1000;
	static constexpr uint32_t kSrvHeapTotal = 2048;
	
	// ★追加: RTV割り当て用
	uint32_t rtvCursor_ = WindowDX::kBackBufferCount; // スワップチェーンの分をスキップ
	uint32_t dsvCursor_ = 1; // メイン深度バッファの分をスキップ

	static constexpr uint32_t kFrameCount = 2;
	UploadRing upload_[kFrameCount]{};

	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSig3D_;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSigTerrain_; // ★追加: 地形用

	// ★追加: コンピュート用
	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSigCompute_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> psoCollision_;
	Microsoft::WRL::ComPtr<ID3D12Resource> collisionResultBuffer_;
	Microsoft::WRL::ComPtr<ID3D12Resource> collisionReadbackBuffer_;
	uint32_t* collisionReadbackMapped_ = nullptr;
	uint32_t collisionMaxPairs_ = 0;

	std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID3D12PipelineState>> pipelines_;
	std::vector<std::string> shaderNames_;

	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSig2D_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> pso2D_;

	// ★追加: 3Dライン描画用
	Microsoft::WRL::ComPtr<ID3D12PipelineState> psoLine3D_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> psoLine3DXRay_;
	struct LineVertex {
		float x, y, z;    // position
		float r, g, b, a; // color
	};
	static constexpr uint32_t kMaxLineVertices = 65536;
	std::vector<LineVertex> lineVertices_;
	std::vector<LineVertex> lineVerticesXRay_;

	D3D12_VIEWPORT viewport_{};
	D3D12_RECT scissor_{};

	bool ppEnabled_ = true;
	PostProcessParams ppParams_{};

	Microsoft::WRL::ComPtr<ID3D12Resource> ppSceneColor_;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> ppRtvHeap_;
	D3D12_CPU_DESCRIPTOR_HANDLE ppRtv_{};
	D3D12_GPU_DESCRIPTOR_HANDLE ppSrvGpu_{};
	D3D12_RESOURCE_STATES ppSceneState_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

	// ★追加: カスタムレンダーターゲットのトラッキング
	const CustomRenderTarget* currentCustomTarget_ = nullptr;

	// ★追加: 最終描画先(エディタで表示、または画面にコピーする用)
	Microsoft::WRL::ComPtr<ID3D12Resource> finalSceneColor_;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> finalRtvHeap_;
	D3D12_CPU_DESCRIPTOR_HANDLE finalRtv_{};
	D3D12_GPU_DESCRIPTOR_HANDLE finalSrvGpu_{};
	D3D12_RESOURCE_STATES finalSceneState_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSigPP_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> psoPP_;

	// ★追加: 最終テクスチャをバックバッファにそのままコピーして映すパイプライン
	Microsoft::WRL::ComPtr<ID3D12PipelineState> psoCopy_;

	// ★追加: シャドウマップ用リソース
	Microsoft::WRL::ComPtr<ID3D12Resource> shadowMap_;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> shadowDsvHeap_;
	D3D12_CPU_DESCRIPTOR_HANDLE shadowDsv_{};
	D3D12_GPU_DESCRIPTOR_HANDLE shadowSrv_{};
	Microsoft::WRL::ComPtr<ID3D12PipelineState> shadowPso_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> shadowSkinPso_;

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4324)
#endif
	struct alignas(256) CBFrame {
		Matrix4x4 view;
		Matrix4x4 proj;
		Matrix4x4 viewProj;
		Vector3 cameraPos;
		float time = 0.0f;
		Vector4 windParams = {1.0f, 0.0f, 0.5f, 0.2f}; // x,y:方向, z:速度, w:強さ
		Vector3 playerPos = {0.0f, 0.0f, 0.0f};
		float pad;
	} cbFrame_{};
#ifdef _MSC_VER
#pragma warning(pop)
#endif

	LightCB lightCB_{};

	D3D12_GPU_VIRTUAL_ADDRESS cbFrameAddr_ = 0;
	D3D12_GPU_VIRTUAL_ADDRESS cbLightAddr_ = 0;

	bool framePPEnabled_ = false;
	bool backBufferBarrierState_ = false;

	std::vector<DrawCall> drawCalls_; // ★追加: ドローコールバッファ
	std::vector<InstancedDrawCall> instancedDrawCalls_; // ★追加: インスタンスドローコール
	std::vector<InstancedDrawCall> instancedParticleDrawCalls_; // ★追加: パーティクル用
	std::vector<SpriteDrawCall> spriteDrawCalls_; // ★追加: スプライト用

	// ★変更: Mesh構造体ではなくModelクラスへのスマートポインタで管理
	std::vector<std::shared_ptr<Model>> models_;
	std::vector<Texture> textures_;

	std::unordered_map<std::string, TextureHandle> textureCache_;
	std::unordered_map<std::string, MeshHandle> meshCache_;
};

} // namespace Engine