#pragma once

#include <Windows.h>
#include <string>

#include "Camera.h"
#include "IScene.h"
#include "Renderer.h"
#include "WindowDX.h"

namespace Game {

class ResultScene final : public Engine::IScene {
public:
	void Initialize(Engine::WindowDX* dx) override;
	void Update() override;
	void Draw() override;

	bool IsEnd() const override { return end_; }
	std::string Next() const override { return next_; }

private:
	bool WasPressed_(int vk) {
		SHORT now = GetAsyncKeyState(vk);
		bool pressed = ((now & 0x8000) != 0) && ((prevKey_[vk] & 0x8000) == 0);
		prevKey_[vk] = now;
		return pressed;
	}

private:
	Engine::WindowDX* dx_ = nullptr;

	// ここを “値” から “共有インスタンス” に
	Engine::Renderer* renderer_ = nullptr;

	Engine::Camera camera_{};

	Engine::Renderer::TextureHandle uvTex_ = 0;

	SHORT prevKey_[256]{};

	bool end_ = false;
	std::string next_{};
};

} // namespace Game
