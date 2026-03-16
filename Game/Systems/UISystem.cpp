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

void UISystem::Update(std::vector<SceneObject>& /*objects*/, GameContext& /*ctx*/) {
    // ボタンの更新や入力判定はワールド座標が確定するDrawフェーズ (RenderNodeWithRect) で実行するため、ここでは何もしない
}

UISystem::WorldRect UISystem::CalculateWorldRect(const SceneObject& obj, const std::vector<SceneObject>& allObjects, float screenW, float screenH) {
    if (obj.rectTransforms.empty()) return {0, 0, 0, 0};

    // 親を辿ってパスを構築
    std::vector<const SceneObject*> path;
    const SceneObject* current = &obj;
    while (current) {
        path.push_back(current);
        const SceneObject* parent = nullptr;
        if (current->parentId != 0) {
            for (const auto& o : allObjects) {
                if (o.id == current->parentId) {
                    parent = &o;
                    break;
                }
            }
        }
        current = parent;
    }
    std::reverse(path.begin(), path.end());

    WorldRect currentRect = { 0, 0, screenW, screenH };

    for (const SceneObject* pObj : path) {
        if (pObj->rectTransforms.empty()) continue;
        auto& rect = pObj->rectTransforms[0];
        
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

void UISystem::Draw(std::vector<SceneObject>& objects, GameContext& ctx) {
    std::unordered_map<uint32_t, WorldRect> cache;

    // --- 既存のUI（Canvasベース）の描画 ---
    auto renderRecursive = [&](auto self, uint32_t parentId, WorldRect parentRect) -> void {
        for (auto& obj : objects) {
            if (obj.rectTransforms.empty()) continue;
            if (obj.parentId == parentId) {
                auto& rect = obj.rectTransforms[0];
                float worldW = rect.size.x;
                float worldH = rect.size.y;
                float anchorX = parentRect.x + parentRect.w * rect.anchor.x;
                float anchorY = parentRect.y + parentRect.h * rect.anchor.y;
                float worldX = anchorX - worldW * rect.pivot.x + rect.pos.x;
                float worldY = anchorY - worldH * rect.pivot.y + rect.pos.y;
                
                WorldRect selfRect = { worldX, worldY, worldW, worldH };
                cache[obj.id] = selfRect;

                RenderNodeWithRect(obj, selfRect, ctx);
                self(self, obj.id, selfRect);
            }
        }
    };

    WorldRect screen = { 0, 0, (float)Engine::WindowDX::kW, (float)Engine::WindowDX::kH };
    renderRecursive(renderRecursive, 0, screen);

    // --- ★追加: ワールド空間UI（HPバー、ダメージ数字）の描画 ---
    if (ctx.isPlaying && ctx.camera) {
        ImDrawList* drawList = ImGui::GetBackgroundDrawList();
        if (!drawList) return;

        for (auto& obj : objects) {
            if (obj.isPendingDestroy) continue;

            // WorldSpaceUIコンポーネントがあるかチェック
            const WorldSpaceUIComponent* uiComp = obj.worldSpaceUIs.empty() ? nullptr : &obj.worldSpaceUIs[0];

            // 1. HPバーの描画 (Healthコンポーネントを持ち、かつWorldSpaceUIコンポーネントで表示が許可されている場合)
            if (!obj.healths.empty() && obj.healths[0].enabled && !obj.healths[0].isDead) {
                auto& hc = obj.healths[0];
                bool shouldShow = (!uiComp || uiComp->showHealthBar);

                // HPが満タンでない、かつコンポーネント設定で許可されている場合に表示
                if (shouldShow && hc.hp < hc.maxHp) {
                    float sx, sy;
                    DirectX::XMFLOAT3 pos = obj.translate;
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
                        pos.y += obj.scale.y * 1.2f + 0.5f;
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

            // 2. 汎用変数を使ったダメージ数字演出
            bool showDmg = (!uiComp || uiComp->showDamageNumbers);
            if (showDmg) {
                float dmgTimer = obj.GetVariable("damage_timer", 0.0f);
                if (dmgTimer > 0.0f) {
                    std::string dmgStr = obj.GetString("damage_text", "");
                    if (!dmgStr.empty()) {
                        float sx, sy;
                        DirectX::XMFLOAT3 pos = obj.translate;
                        // タイマーに応じて上に浮かび上がらせる
                        float t = 1.0f - dmgTimer; // 1秒演出想定
                        
                        if (uiComp) {
                            pos.x += uiComp->offset.x;
                            pos.y += uiComp->offset.y + t * 2.0f;
                            pos.z += uiComp->offset.z;
                        } else {
                            pos.y += obj.scale.y + t * 2.0f;
                        }

                        if (WorldToScreen(pos, *ctx.camera, sx, sy)) {
                            ImU32 col = IM_COL32(255, 255, 50, (int)(dmgTimer * 255));
                            drawList->AddText(ImGui::GetFont(), 24.0f, ImVec2(sx, sy), col, dmgStr.c_str());
                        }
                        // タイマー更新
                        obj.SetVariable("damage_timer", dmgTimer - ctx.dt);
                    }
                }
            }
        }
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

void UISystem::Reset(std::vector<SceneObject>& /*objects*/) {
    // 必要に応じて初期化処理を記述
}

void UISystem::RenderNodeWithRect(SceneObject& obj, const WorldRect& wr, GameContext& ctx) {
    // ボタンの更新
    if (!obj.buttons.empty()) {
        ProcessButton(obj, obj.buttons[0], wr.x, wr.y, wr.w, wr.h, ctx);
    }

    // ボタンの状態に応じた色を決定
    DirectX::XMFLOAT4 buttonColor = { 1, 1, 1, 1 };
    if (!obj.buttons.empty()) {
        auto& btn = obj.buttons[0];
        if (btn.isPressed) buttonColor = btn.pressedColor;
        else if (btn.isHovered) buttonColor = btn.hoverColor;
        else buttonColor = btn.normalColor;
    }

    // 画像の描画
    for (const auto& img : obj.images) {
        if (img.enabled) {
            DirectX::XMFLOAT4 finalColor = { img.color.x * buttonColor.x, img.color.y * buttonColor.y, img.color.z * buttonColor.z, img.color.w * buttonColor.w };
            if (img.is9Slice) {
                Engine::Renderer::Sprite9SliceDesc s;
                s.x = wr.x; s.y = wr.y; s.w = wr.w; s.h = wr.h;
                s.left = img.borderLeft; s.right = img.borderRight; s.top = img.borderTop; s.bottom = img.borderBottom;
                s.color = { finalColor.x, finalColor.y, finalColor.z, finalColor.w };
                s.rotationRad = DirectX::XMConvertToRadians(obj.rectTransforms[0].rotation);
                ctx.renderer->DrawSprite9Slice(img.textureHandle, s);
            } else {
                Engine::Renderer::SpriteDesc s;
                s.x = wr.x; s.y = wr.y; s.w = wr.w; s.h = wr.h;
                s.color = { finalColor.x, finalColor.y, finalColor.z, finalColor.w };
                s.rotationRad = DirectX::XMConvertToRadians(obj.rectTransforms[0].rotation);
                ctx.renderer->DrawSprite(img.textureHandle, s);
            }
        }
    }

    // テキストの描画
    for (const auto& text : obj.texts) {
        if (text.enabled) {
            DrawText(obj, text, wr.x, wr.y, wr.w, wr.h, ctx.renderer);
        }
    }
}

void UISystem::DrawText(const SceneObject& /*obj*/, const UITextComponent& text, float worldX, float worldY, float worldW, float worldH, Engine::Renderer* /*renderer*/) {
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

void UISystem::ProcessButton(SceneObject& obj, UIButtonComponent& btn, float worldX, float worldY, float worldW, float worldH, GameContext& ctx) {
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
        if (!obj.scripts.empty() && obj.scripts[0].enabled && obj.scripts[0].instance) {
            obj.scripts[0].instance->OnClick(obj, ctx.scene, btn.onClickCallback);
        }
    }
}

} // namespace Game
