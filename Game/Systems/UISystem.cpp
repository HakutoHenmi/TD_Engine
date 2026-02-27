#include "UISystem.h"
#include "../ObjectTypes.h"
#include "../../Engine/Renderer.h"
#include "../../Engine/Input.h"
#include "../../Engine/WindowDX.h"
#include <algorithm>

namespace Game {

void UISystem::Update(std::vector<SceneObject>& objects, GameContext& ctx) {
    for (auto& obj : objects) {
        if (obj.rectTransforms.empty()) continue;
        auto& rect = obj.rectTransforms[0];
        if (!rect.enabled) continue;

        if (!obj.buttons.empty()) {
            ProcessButton(obj, obj.buttons[0], rect, ctx);
        }
    }
}

void UISystem::Draw(std::vector<SceneObject>& objects, GameContext& ctx) {
    for (auto& obj : objects) {
        RenderNode(obj, ctx);
    }
}

void UISystem::Reset(std::vector<SceneObject>& /*objects*/) {
    // 必要に応じて初期化処理を記述
}

void UISystem::RenderNode(SceneObject& obj, GameContext& ctx) {
    if (obj.rectTransforms.empty()) return;
    
    auto& rect = obj.rectTransforms[0];
    if (!rect.enabled) return;

    // ボタンの状態に応じた色を決定
    DirectX::XMFLOAT4 buttonColor = { 1, 1, 1, 1 };
    if (!obj.buttons.empty()) {
        auto& btn = obj.buttons[0];
        if (btn.isPressed) buttonColor = btn.pressedColor;
        else if (btn.isHovered) buttonColor = btn.hoverColor;
        else buttonColor = btn.normalColor;
    }

    // 画像の描画（キューイング）
    for (const auto& img : obj.images) {
        if (img.enabled) {
            Engine::Renderer::SpriteDesc sprite;
            sprite.x = rect.pos.x;
            sprite.y = rect.pos.y;
            sprite.w = rect.size.x;
            sprite.h = rect.size.y;
            sprite.rotationRad = DirectX::XMConvertToRadians(rect.rotation);
            
            sprite.color.x = img.color.x * buttonColor.x;
            sprite.color.y = img.color.y * buttonColor.y;
            sprite.color.z = img.color.z * buttonColor.z;
            sprite.color.w = img.color.w * buttonColor.w;

            ctx.renderer->DrawSprite(img.textureHandle, sprite);
        }
    }

    // テキストの描画
    for (const auto& text : obj.texts) {
        if (text.enabled) {
            DrawText(obj, text, rect, ctx.renderer);
        }
    }
}

void UISystem::DrawText(const SceneObject& /*obj*/, const UITextComponent& /*text*/, const RectTransformComponent& /*rect*/, Engine::Renderer* renderer) {
    if (!renderer) return;

    // TODO: Renderer側にテキスト描画機能を追加した後に実装
    // 現時点では、デバッグ用に枠線などを描画することも検討
}

void UISystem::ProcessButton(SceneObject& /*obj*/, UIButtonComponent& btn, const RectTransformComponent& rect, GameContext& ctx) {
    if (!ctx.input) return;

    int mx, my;
    ctx.input->GetMousePos(mx, my);

    // 矩形内判定
    bool hovered = (mx >= rect.pos.x && mx <= rect.pos.x + rect.size.x &&
                    my >= rect.pos.y && my <= rect.pos.y + rect.size.y);

    btn.isHovered = hovered;
    btn.isPressed = hovered && ctx.input->IsMouseDown(0); // 左ボタン

    if (hovered && ctx.input->IsMouseTrigger(0)) {
        // クリック時
        // TODO: 必要に応じてスクリプト側への通知やイベント発行を行う
    }
}

} // namespace Game
