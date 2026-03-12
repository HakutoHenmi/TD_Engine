#include "EnemySpawnerEditor.h"
#include "EditorUI.h"
#include "../Scripts/EnemySpawnerScript.h"
#include "../Scripts/ScriptEngine.h"
#include "../../externals/imgui/imgui.h"
#include <cfloat>

namespace Game {

void EnemySpawnerEditor::DrawUI() {
    ImGui::SameLine();
    if (ImGui::Button(spawnerMode_ ? "Spawner [ON]" : "Spawner [OFF]")) {
        spawnerMode_ = !spawnerMode_;
        if (spawnerMode_) {
            EditorUI::Log("Enemy Spawner placement mode ON - click on terrain to place spawners");
        } else {
            EditorUI::Log("Enemy Spawner placement mode OFF");
        }
    }
}

void EnemySpawnerEditor::UpdateAndDraw(GameScene* scene, Engine::Renderer* renderer,
                                        const ImVec2& gameImageMin, const ImVec2& /*gameImageMax*/,
                                        float tW, float tH) {
    if (!scene || scene->IsPlaying()) return;

    // ========== 全スポナープレビュー描画 (ゲーム停止中は常に表示) ==========
    for (auto& obj : scene->objects_) {
        for (auto& sc : obj.scripts) {
            if (!sc.instance && !sc.scriptPath.empty() && !scene->IsPlaying()) {
                sc.instance = ScriptEngine::GetInstance()->CreateScript(sc.scriptPath);
                if (sc.instance) {
                    sc.instance->Start(obj, scene);
                }
            }
            if (sc.instance) {
                auto* spawner = dynamic_cast<EnemySpawnerScript*>(sc.instance.get());
                if (spawner) {
                    spawner->DrawSpawnPreview(obj.translate);
                }
            }
        }
    }

    // ========== 配置モードでなければここで終了 ==========
    if (!spawnerMode_) return;

    ImVec2 mousePos = ImGui::GetMousePos();
    float localX = mousePos.x - gameImageMin.x;
    float localY = mousePos.y - gameImageMin.y;
    bool insideImage = (localX >= 0 && localY >= 0 && localX <= tW && localY <= tH);

    if (!insideImage) return;

    // マウスカーソルをクロスヘアに変更 (視覚的フィードバック)
    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
        auto viewMat = scene->camera_.View();
        auto projMat = scene->camera_.Proj();
        DirectX::XMVECTOR rayOrig, rayDir;
        EditorUI::ScreenToWorldRay(localX, localY, tW, tH, viewMat, projMat, rayOrig, rayDir);

        // メッシュコライダーを持つ全オブジェクトに対してレイキャスト
        float bestDist = FLT_MAX;
        Engine::Vector3 hitPoint = {0, 0, 0};
        bool hitTerrain = false;

        for (const auto& obj : scene->objects_) {
            // gpuMeshColliders があるオブジェクトすべてを対象にする
            if (!obj.gpuMeshColliders.empty()) {
                auto* model = renderer->GetModel(obj.gpuMeshColliders[0].meshHandle);
                if (model) {
                    Engine::Vector3 hp;
                    float dist;
                    if (model->RayCast(rayOrig, rayDir, obj.GetTransform().ToMatrix(), dist, hp)) {
                        if (dist < bestDist) {
                            bestDist = dist;
                            hitPoint = hp;
                            hitTerrain = true;
                        }
                    }
                }
            }
            // meshRenderers のモデルも対象にする (地面メッシュ)
            if (!obj.meshRenderers.empty()) {
                for (const auto& mr : obj.meshRenderers) {
                    if (mr.modelHandle != 0) {
                        auto* model = renderer->GetModel(mr.modelHandle);
                        if (model) {
                            Engine::Vector3 hp;
                            float dist;
                            if (model->RayCast(rayOrig, rayDir, obj.GetTransform().ToMatrix(), dist, hp)) {
                                if (dist < bestDist) {
                                    bestDist = dist;
                                    hitPoint = hp;
                                    hitTerrain = true;
                                }
                            }
                        }
                    }
                }
            }
        }

        if (hitTerrain) {
            // ヒットした位置にスポナーオブジェクトを生成
            SceneObject spawnerObj;
            spawnerObj.name = "EnemySpawner";
            spawnerObj.translate = {hitPoint.x, hitPoint.y, hitPoint.z};
            spawnerObj.scale = {1.0f, 1.0f, 1.0f};

            // ID を生成
            uint32_t maxId = 0;
            for (const auto& o : scene->objects_) {
                if (o.id > maxId) maxId = o.id;
            }
            spawnerObj.id = maxId + 1;

            // EnemySpawnerScript をアタッチ
            ScriptComponent sc;
            sc.scriptPath = "EnemySpawnerScript";
            sc.instance = ScriptEngine::GetInstance()->CreateScript("EnemySpawnerScript");
            spawnerObj.scripts.push_back(sc);

            scene->objects_.push_back(spawnerObj);

            // 新しく配置したスポナーを選択状態にする
            int newIdx = static_cast<int>(scene->objects_.size()) - 1;
            scene->selectedIndices_ = {newIdx};
            scene->selectedObjectIndex_ = newIdx;

            EditorUI::Log("Spawner placed at (" +
                          std::to_string(hitPoint.x) + ", " +
                          std::to_string(hitPoint.y) + ", " +
                          std::to_string(hitPoint.z) + ")");
        }
    }
}

} // namespace Game
