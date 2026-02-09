#include "ResultScene.h"
#include <DirectXMath.h>

namespace Game {

using namespace DirectX;

void ResultScene::Initialize(Engine::WindowDX* dx) {
	dx_ = dx;

	// Result単体で renderer 初期化 
	// renderer_.Initialize(dx_);

	// ★GameSceneと同じ共有レンダラを使う
	renderer_ = Engine::Renderer::GetInstance();

	camera_.Initialize();
	const float aspect = (float)Engine::WindowDX::kW / (float)Engine::WindowDX::kH;
	camera_.SetProjection(XMConvertToRadians(60.0f), aspect, 0.1f, 500.0f);
	camera_.SetPosition(0.0f, 5.0f, -10.0f);
	camera_.SetRotation(0.2f, 0.0f, 0.0f);

	uvTex_ = renderer_->LoadTexture2D("Resources/uvChecker.png", true);

	for (int i = 0; i < 256; ++i) {
		prevKey_[i] = GetAsyncKeyState(i);
	}

	end_ = false;
	next_.clear();
}

void ResultScene::Update() {
	if (WasPressed_(VK_RETURN)) {
		end_ = true;
		next_ = "Title";
		return;
	}

	if (WasPressed_(VK_ESCAPE)) {
		PostQuitMessage(0);
		return;
	}
}

void ResultScene::Draw() {
	const float clear[4] = {0.10f, 0.10f, 0.15f, 1.0f};

	renderer_->SetCamera(camera_);
	// ここで描画だけする（フレーム開始/終了は上位がやる設計）
	if (uvTex_ != 0) {
		Engine::Renderer::SpriteDesc s{};
		s.x = 20.0f;
		s.y = 20.0f;
		s.w = 180.0f;
		s.h = 180.0f;
		s.rotationRad = 0.0f;
		s.color = Engine::Vector4{1, 1, 1, 1};
		renderer_->DrawSprite(uvTex_, s);
	}
	
}

} // namespace Game
