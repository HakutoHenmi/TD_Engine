#pragma once
#include "ISystem.h"
#include <vector>

namespace Game {

class UISystem : public ISystem {
public:
    void Update(std::vector<SceneObject>& objects, GameContext& ctx) override;
    void Draw(std::vector<SceneObject>& objects, GameContext& ctx) override;
    void Reset(std::vector<SceneObject>& objects) override;

private:
    struct WorldRect { float x, y, w, h; };
    void RenderNodeWithRect(SceneObject& obj, const WorldRect& wr, GameContext& ctx);
    void DrawText(const SceneObject& obj, const UITextComponent& text, float worldX, float worldY, float worldW, float worldH, Engine::Renderer* renderer);
    void ProcessButton(SceneObject& obj, UIButtonComponent& btn, float worldX, float worldY, float worldW, float worldH, GameContext& ctx);
};

} // namespace Game
