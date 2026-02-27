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
    void RenderNode(SceneObject& obj, GameContext& ctx);
    void DrawText(const SceneObject& obj, const UITextComponent& text, const RectTransformComponent& rect, Engine::Renderer* renderer);
    void ProcessButton(SceneObject& obj, UIButtonComponent& btn, const RectTransformComponent& rect, GameContext& ctx);
};

} // namespace Game
