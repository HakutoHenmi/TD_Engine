#pragma once
#include "ISystem.h"
#include <vector>

namespace Game {

class UISystem : public ISystem {
public:
    struct WorldRect { float x, y, w, h; };

    void Update(std::vector<SceneObject>& objects, GameContext& ctx) override;
    void Draw(std::vector<SceneObject>& objects, GameContext& ctx) override;
    void Reset(std::vector<SceneObject>& objects) override;

    static WorldRect CalculateWorldRect(const SceneObject& obj, const std::vector<SceneObject>& allObjects, float screenW, float screenH);

    // ★追加: 3Dワールド座標からスクリーン座標(0~1)に変換
    static bool WorldToScreen(const DirectX::XMFLOAT3& worldPos, const Engine::Camera& camera, float& screenX, float& screenY);

private:
    void RenderNodeWithRect(SceneObject& obj, const WorldRect& wr, GameContext& ctx);
    void DrawText(const SceneObject& obj, const UITextComponent& text, float worldX, float worldY, float worldW, float worldH, Engine::Renderer* renderer);
    void ProcessButton(SceneObject& obj, UIButtonComponent& btn, float worldX, float worldY, float worldW, float worldH, GameContext& ctx);
};

} // namespace Game
