#include "UISystem.h"
#include "../ObjectTypes.h"
#include "../../Engine/Renderer.h"
#include "../../Engine/Input.h"
#include "../Scripts/IScript.h" // ★追加
#include "../../Engine/WindowDX.h"
#include "../../externals/imgui/imgui.h"
#include <unordered_map>
#include <set>
#include <algorithm>

namespace Game {

void UISystem::Update(entt::registry& /*registry*/, GameContext& /*ctx*/) {
    // ボタンの更新や入力判定はワールド座標が確定するDrawフェーズ (RenderNodeWithRect) で実行するため、ここでは何もしない
}

UISystem::WorldRect UISystem::CalculateWorldRect(entt::entity entity, entt::registry& registry, float screenW, float screenH) {
    if (!registry.all_of<RectTransformComponent>(entity)) return {0, 0, 0, 0};

    // 親を辿ってパスを構築
    std::vector<entt::entity> path;
    entt::entity current = entity;
    while (registry.valid(current)) {
        path.push_back(current);
        entt::entity parent = entt::null;
        
        if (registry.all_of<HierarchyComponent>(current)) {
            entt::entity parentId = registry.get<HierarchyComponent>(current).parentId;
            if (parentId != entt::null) {
                parent = parentId;
            }
        }
        current = parent;
    }
    std::reverse(path.begin(), path.end());

    WorldRect currentRect = { 0, 0, screenW, screenH };

    for (entt::entity pObj : path) {
        if (!registry.all_of<RectTransformComponent>(pObj)) continue;
        auto& rect = registry.get<RectTransformComponent>(pObj);
        
        float worldW = rect.size.x;
        float worldH = rect.size.y;
        float anchorX = currentRect.x + currentRect.w * rect.anchor.x;
        float anchorY = currentRect.y + currentRect.h * rect.anchor.y;
        float worldX = anchorX - worldW * rect.pivot.x + rect.pos.x;
        float worldY = anchorY - worldH * rect.pivot.y + rect.pos.y;
        
        currentRect = { worldX, worldY, worldW, worldH };
    }
    return currentRect;
}

void UISystem::Draw(entt::registry& registry, GameContext& ctx) {
    std::unordered_map<uint32_t, WorldRect> cache;

    // --- 既存のUI（Canvasベース）の描画 ---
    auto renderRecursive = [&](auto self, entt::entity parentId, WorldRect parentRect) -> void {
        auto view = registry.view<RectTransformComponent>();
        for (auto e : view) {
            entt::entity currentParentId = entt::null;
            if (registry.all_of<HierarchyComponent>(e)) {
                currentParentId = registry.get<HierarchyComponent>(e).parentId;
            }

            if (currentParentId == parentId) {
                auto& rect = view.get<RectTransformComponent>(e);
                float worldW = rect.size.x;
                float worldH = rect.size.y;
                float anchorX = parentRect.x + parentRect.w * rect.anchor.x;
                float anchorY = parentRect.y + parentRect.h * rect.anchor.y;
                float worldX = anchorX - worldW * rect.pivot.x + rect.pos.x;
                float worldY = anchorY - worldH * rect.pivot.y + rect.pos.y;
                
                WorldRect selfRect = { worldX, worldY, worldW, worldH };
                uint32_t eId = static_cast<uint32_t>(e);
                cache[eId] = selfRect;

                RenderNodeWithRect(e, registry, selfRect, ctx);
                self(self, e, selfRect);
            }
        }
    };

    WorldRect screen = { 0, 0, (float)Engine::WindowDX::kW, (float)Engine::WindowDX::kH };
    renderRecursive(renderRecursive, entt::null, screen);

    // --- ★追加: ワールド空間UI（HPバー、ダメージ数字）の描画 ---
    if (ctx.isPlaying && ctx.camera) {
        ImDrawList* drawList = ImGui::GetBackgroundDrawList();
        if (!drawList) return;

        auto viewHealth = registry.view<HealthComponent, TransformComponent>();
        for (auto e : viewHealth) {
            auto& hc = viewHealth.get<HealthComponent>(e);
            auto& tc = viewHealth.get<TransformComponent>(e);
            
            const WorldSpaceUIComponent* uiComp = nullptr;
            if (registry.all_of<WorldSpaceUIComponent>(e)) {
                uiComp = &registry.get<WorldSpaceUIComponent>(e);
            }

            // 1. HPバーの描画
            if (hc.enabled && !hc.isDead) {
                bool shouldShow = (!uiComp || uiComp->showHealthBar);

                // HPが満タンでない、かつコンポーネント設定で許可されている場合に表示
                if (shouldShow && hc.hp < hc.maxHp) {
                    float sx, sy;
                    DirectX::XMFLOAT3 pos = tc.translate;
                    float barW = 60.0f;
                    float barH = 6.0f;

                    if (uiComp) {
                        pos.x += uiComp->offset.x;
                        pos.y += uiComp->offset.y;
                        pos.z += uiComp->offset.z;
                        barW = uiComp->barWidth;
                        barH = uiComp->barHeight;
                    } else {
                        // コンポーネントがない場合のデフォルト位置
                        pos.y += tc.scale.y * 1.2f + 0.5f;
                    }

                    if (WorldToScreen(pos, *ctx.camera, sx, sy)) {
                        float curW = barW * (hc.hp / hc.maxHp);
                        
                        ImVec2 pMin(sx - barW * 0.5f, sy - barH * 0.5f);
                        ImVec2 pMax(sx + barW * 0.5f, sy + barH * 0.5f);
                        
                        // 背景（赤）
                        drawList->AddRectFilled(pMin, pMax, IM_COL32(200, 50, 50, 200));
                        // 前景（緑）
                        drawList->AddRectFilled(pMin, ImVec2(pMin.x + curW, pMax.y), IM_COL32(50, 200, 50, 255));
                        // 枠
                        drawList->AddRect(pMin, pMax, IM_COL32(0, 0, 0, 255));
                    }
                }
            }
        }

        // ダメージ数値などは EditorState や一時データではなく、変数コンポーネントがあれば処理（一旦保留）
    }
}

bool UISystem::WorldToScreen(const DirectX::XMFLOAT3& worldPos, const Engine::Camera& camera, float& screenX, float& screenY) {
    DirectX::XMVECTOR p = DirectX::XMLoadFloat3(&worldPos);
    DirectX::XMMATRIX viewProj = camera.View() * camera.Proj();
    
    DirectX::XMVECTOR clipPos = DirectX::XMVector3TransformCoord(p, viewProj);
    
    DirectX::XMFLOAT3 clip;
    DirectX::XMStoreFloat3(&clip, clipPos);

    // 画面外（カメラの後ろなど）の判定
    if (clip.z < 0.0f || clip.z > 1.0f) return false;
    if (clip.x < -1.1f || clip.x > 1.1f || clip.y < -1.1f || clip.y > 1.1f) return false;

    // NDC (-1~1) -> Screen (0~Pixels)
    screenX = (clip.x + 1.0f) * 0.5f * (float)Engine::WindowDX::kW;
    screenY = (1.0f - clip.y) * 0.5f * (float)Engine::WindowDX::kH;
    
    return true;
}

void UISystem::Reset(entt::registry& /*registry*/) {
    // 必要に応じて初期化処理を記述
}

void UISystem::RenderNodeWithRect(entt::entity entity, entt::registry& registry, const WorldRect& wr, GameContext& ctx) {
    // ボタンの更新
    if (registry.all_of<UIButtonComponent>(entity)) {
        auto& btn = registry.get<UIButtonComponent>(entity);
        ProcessButton(entity, registry, btn, wr.x, wr.y, wr.w, wr.h, ctx);
    }

    // ボタンの状態に応じた色を決定
    DirectX::XMFLOAT4 buttonColor = { 1, 1, 1, 1 };
    if (registry.all_of<UIButtonComponent>(entity)) {
        auto& btn = registry.get<UIButtonComponent>(entity);
        if (btn.isPressed) buttonColor = btn.pressedColor;
        else if (btn.isHovered) buttonColor = btn.hoverColor;
        else buttonColor = btn.normalColor;
    }

    // 画像の描画
    if (registry.all_of<UIImageComponent>(entity)) {
        auto& img = registry.get<UIImageComponent>(entity);
        if (img.enabled) {
            DirectX::XMFLOAT4 finalColor = { img.color.x * buttonColor.x, img.color.y * buttonColor.y, img.color.z * buttonColor.z, img.color.w * buttonColor.w };
            if (img.is9Slice) {
                Engine::Renderer::Sprite9SliceDesc s;
                s.x = wr.x; s.y = wr.y; s.w = wr.w; s.h = wr.h;
                s.left = img.borderLeft; s.right = img.borderRight; s.top = img.borderTop; s.bottom = img.borderBottom;
                s.color = { finalColor.x, finalColor.y, finalColor.z, finalColor.w };
                s.rotationRad = DirectX::XMConvertToRadians(registry.get<RectTransformComponent>(entity).rotation);
                ctx.renderer->DrawSprite9Slice(img.textureHandle, s);
            } else {
                Engine::Renderer::SpriteDesc s;
                s.x = wr.x; s.y = wr.y; s.w = wr.w; s.h = wr.h;
                s.color = { finalColor.x, finalColor.y, finalColor.z, finalColor.w };
                s.rotationRad = DirectX::XMConvertToRadians(registry.get<RectTransformComponent>(entity).rotation);
                ctx.renderer->DrawSprite(img.textureHandle, s);
            }
        }
    }

    // テキストの描画
    if (registry.all_of<UITextComponent>(entity)) {
        auto& text = registry.get<UITextComponent>(entity);
        if (text.enabled) {
            DrawTextW(entity, registry, text, wr.x, wr.y, wr.w, wr.h, ctx.renderer);
        }
    }
}

void UISystem::DrawTextW(entt::entity /*entity*/, entt::registry& /*registry*/, const UITextComponent& text, float worldX, float worldY, float worldW, float worldH, Engine::Renderer* /*renderer*/) {
    ImDrawList* drawList = ImGui::GetBackgroundDrawList();
    if (!drawList) return;

    ImVec2 pos(worldX, worldY);
    // 中央揃えなどの簡易実装
    ImVec2 textSize = ImGui::CalcTextSize(text.text.c_str());
    pos.x += (worldW - textSize.x) * 0.5f;
    pos.y += (worldH - textSize.y) * 0.5f;

    ImU32 color = ImGui::GetColorU32(ImVec4(text.color.x, text.color.y, text.color.z, text.color.w));
    drawList->AddText(ImGui::GetFont(), text.fontSize, pos, color, text.text.c_str());
}

void UISystem::ProcessButton(entt::entity entity, entt::registry& registry, UIButtonComponent& btn, float worldX, float worldY, float worldW, float worldH, GameContext& ctx) {
    if (!ctx.input) return;

    float mx, my;
    if (ctx.useOverrideMouse) {
        mx = ctx.overrideMouseX;
        my = ctx.overrideMouseY;
    } else {
        float fmx, fmy;
        ctx.input->GetMousePos(fmx, fmy);
        mx = fmx;
        my = fmy;
    }

    // hitboxパラメータを適用した実際の判定矩形を計算
    float hw = worldW * btn.hitboxScale.x;
    float hh = worldH * btn.hitboxScale.y;
    // ビジュアルの中央を基準にスケールとオフセットを適用
    float cx = worldX + worldW * 0.5f + btn.hitboxOffset.x;
    float cy = worldY + worldH * 0.5f + btn.hitboxOffset.y;
    float hx = cx - hw * 0.5f;
    float hy = cy - hh * 0.5f;

    // 矩形内判定
    bool hovered = (mx >= hx && mx <= hx + hw &&
                    my >= hy && my <= hy + hh);

    btn.isHovered = hovered;
    btn.isPressed = hovered && ctx.input->IsMouseDown(0); // 左ボタン

    if (hovered && ctx.input->IsMouseTrigger(0)) {
        // クリック時: スクリプト側へ通知
        if (registry.all_of<ScriptComponent>(entity)) {
            auto& sc = registry.get<ScriptComponent>(entity);
            if (sc.enabled) {
                for (auto& entry : sc.scripts) {
                    if (entry.instance) {
                        // To DO: on click needs to accept entt::entity instead of SceneObject
                        // entry.instance->OnClick(entity, ctx.scene, btn.onClickCallback);
                    }
                }
            }
        }
    }
}

} // namespace Game
