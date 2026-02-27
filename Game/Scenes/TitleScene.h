#pragma once
#include "IScene.h"
#include "Renderer.h"
#include "WindowDX.h"

namespace Game {

class TitleScene : public Engine::IScene {
public:
	void Initialize(Engine::WindowDX* dx) override;
	void Update() override;
	void Draw() override;
	void DrawEditor() override;

private:
	Engine::WindowDX* dx_ = nullptr;
	Engine::Renderer* renderer_ = nullptr;
	Engine::Camera camera_;
};

} // namespace Game
