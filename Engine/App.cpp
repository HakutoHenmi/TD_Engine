#include "App.h"
#include <Windows.h>

// ★GameSceneを知るために必要
#include "GameScene.h"

namespace Engine {

bool App::Initialize(HINSTANCE hInst, int cmdShow) {
	sceneManager_.SetDX(&dx_);
	if (!dx_.Initialize(hInst, cmdShow, hwnd_))
		return false;

	if (!renderer_.Initialize(&dx_))
		return false;

	input_.Initialize(hInst, hwnd_);
	camera_.Initialize();
	audio_.Initialize();

	// ImGui初期化
	const uint32_t kImGuiFontSrvIndex = 0;
	ID3D12DescriptorHeap* srvHeap = dx_.SRV();
	D3D12_CPU_DESCRIPTOR_HANDLE fontCpuHandle = dx_.SRV_CPU(kImGuiFontSrvIndex);
	D3D12_GPU_DESCRIPTOR_HANDLE fontGpuHandle = dx_.SRV_GPU(kImGuiFontSrvIndex);

	if (!imgui_.Initialize(hwnd_, dx_, srvHeap, fontCpuHandle, fontGpuHandle)) {
		return false;
	}

	if (registrar_) {
		registrar_(sceneManager_, dx_);
	}

	if (!initialSceneKey_.empty()) {
		sceneManager_.Change(initialSceneKey_);
	} else if (sceneManager_.Has("FPS")) {
		sceneManager_.Change("FPS");
	} else {
		const std::string first = sceneManager_.FirstRegisteredName();
		if (!first.empty())
			sceneManager_.Change(first);
	}

	return true;
}

void App::Run() {
	MSG msg{};
	bool running = true;

	while (running) {
		while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
			if (msg.message == WM_QUIT) {
				running = false;
				break;
			}
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		if (!running)
			break;

		input_.Update();

		dx_.BeginFrame();
		const float clearColor[] = {0.1f, 0.25f, 0.5f, 1.0f};
		renderer_.BeginFrame(clearColor);

		imgui_.NewFrame(dx_);

		sceneManager_.Update();
		sceneManager_.Draw();

		renderer_.EndFrame();

		// 3. エディター表示
		Engine::IScene* currentScene = sceneManager_.Current();
		auto* gameScene = dynamic_cast<Game::GameScene*>(currentScene);

		if (gameScene) {
			// ★修正: gameSceneポインタそのものを渡す (ShowEditorUIの引数変更に対応)
			imgui_.ShowEditorUI(renderer_.GetPostProcessSRV(), gameScene);
		} else {
			// GameScene以外では表示しない、または空のUIなどを検討
		}

		imgui_.Render(dx_);
		dx_.EndFrame();
	}
}

void App::Shutdown() {
	imgui_.Shutdown();
	audio_.Shutdown();
	input_.Shutdown();
	dx_.WaitIdle();
	dx_.Shutdown();
}

void App::BeginFrame_() {}
void App::EndFrame_() {}

} // namespace Engine