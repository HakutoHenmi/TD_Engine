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
	};
#ifdef _MSC_VER
#pragma warning(pop)
#endif

	struct PostProcessParams {
		float time = 0.0f;
		float noiseStrength = 0.06f;
		float distortion = 0.0025f;
		float chromaShift = 0.0015f;
		float vignette = 0.75f;
		float scanline = 0.20f;
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

	// 通常メッシュ描画
	void DrawMesh(MeshHandle mesh, TextureHandle texture, const Transform& transform, const Vector4& mulColor, const std::string& shaderName = "Default");

	// ★スキニングメッシュ描画
	void DrawSkinnedMesh(MeshHandle mesh, TextureHandle texture, const Transform& transform, const std::vector<Matrix4x4>& bones, const Vector4& mulColor = {1, 1, 1, 1});

	void DrawSprite(TextureHandle texture, const SpriteDesc& sprite);

	bool CreateShaderPipeline(const std::string& shaderName, const std::wstring& vsPath, const std::wstring& psPath);
	const std::vector<std::string>& GetShaderNames() const { return shaderNames_; }

	// Modelへのポインタを取得
	Model* GetModel(MeshHandle handle);
	// 追加：透明/加算 シェーダー登録
	bool CreateShaderPipelineTransparent(const std::string& shaderName, const std::wstring& vsPath, const std::wstring& psPath, bool additive);
	// 追加：透明/加算 用 PSO作成
	bool CreatePSO_Transparent(const std::string& name, ID3DBlob* vsBlob, ID3DBlob* psBlob, bool additive);

	bool CreatePSOAlpha(const std::string& name, ID3DBlob* vsBlob, ID3DBlob* psBlob);

private:
	struct Mesh {
		Microsoft::WRL::ComPtr<ID3D12Resource> vb;
		Microsoft::WRL::ComPtr<ID3D12Resource> ib;
		D3D12_VERTEX_BUFFER_VIEW vbView{};
		D3D12_INDEX_BUFFER_VIEW ibView{};
		uint32_t indexCount = 0;
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
	uint32_t AllocateSrvIndex();
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

	static constexpr uint32_t kFrameCount = 2;
	UploadRing upload_[kFrameCount]{};

	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSig3D_;

	std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID3D12PipelineState>> pipelines_;
	std::vector<std::string> shaderNames_;

	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSig2D_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> pso2D_;

	D3D12_VIEWPORT viewport_{};
	D3D12_RECT scissor_{};

	bool ppEnabled_ = true;
	PostProcessParams ppParams_{};

	Microsoft::WRL::ComPtr<ID3D12Resource> ppSceneColor_;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> ppRtvHeap_;
	D3D12_CPU_DESCRIPTOR_HANDLE ppRtv_{};
	D3D12_GPU_DESCRIPTOR_HANDLE ppSrvGpu_{};
	D3D12_RESOURCE_STATES ppSceneState_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSigPP_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> psoPP_;

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
	} cbFrame_{};
#ifdef _MSC_VER
#pragma warning(pop)
#endif

	LightCB lightCB_{};

	D3D12_GPU_VIRTUAL_ADDRESS cbFrameAddr_ = 0;
	D3D12_GPU_VIRTUAL_ADDRESS cbLightAddr_ = 0;

	bool framePPEnabled_ = false;
	bool backBufferBarrierState_ = false;

	// ★変更: Mesh構造体ではなくModelクラスへのスマートポインタで管理
	std::vector<std::shared_ptr<Model>> models_;
	std::vector<Texture> textures_;

	std::unordered_map<std::string, TextureHandle> textureCache_;
	std::unordered_map<std::string, MeshHandle> meshCache_;
};

} // namespace Engine