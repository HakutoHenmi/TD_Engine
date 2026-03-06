#include "PipeEditor.h"
#include "EditorUI.h"
#include "../../externals/imgui/imgui.h"
#include <cmath>

namespace Game {

// ====== DefaultPipeBehavior ======
void DefaultPipeBehavior::OnGeneratePipe(SceneObject& outPipe, const Engine::Vector3& start, const Engine::Vector3& end, float length, Engine::Renderer* renderer) {
    outPipe.name = "PipeSegment";
    
    Engine::Vector3 diff = end - start;
    Engine::Vector3 dir = Engine::Normalize(diff);
    
    // シリンダーは高さ3.0なので、長さを合わせるには 3.0 で割る。また、球体にめり込ませるために少し短くする。
    float cyLen = (length - 0.2f) / 3.0f;
    if (cyLen < 0.01f) cyLen = 0.01f;

    // 中間地点に設置
    Engine::Vector3 center = start + diff * 0.5f;
    outPipe.translate = {center.x, center.y, center.z};
    outPipe.scale = {0.35f, cyLen, 0.35f};

    auto euler = Engine::LookRotation(dir);
    outPipe.rotate = {euler.x - 3.14159265f * 0.5f, euler.y, euler.z}; // シリンダーを倒すためにPitchから90度減算

    outPipe.modelPath = "Resources/Cylinder/cylinder.obj";
    if (renderer) outPipe.modelHandle = renderer->LoadObjMesh(outPipe.modelPath);
    
    MeshRendererComponent mr;
    mr.modelHandle = outPipe.modelHandle;
    mr.modelPath = outPipe.modelPath;
    mr.shaderName = "Toon"; 
    outPipe.meshRenderers.push_back(mr);
}

void DefaultPipeBehavior::OnGenerateJoint(SceneObject& outJoint, const Engine::Vector3& position, Engine::Renderer* renderer) {
    outJoint.name = "PipeJoint";
    outJoint.translate = { position.x, position.y, position.z };
    outJoint.scale = { 1.0f, 1.0f, 1.0f };
    outJoint.modelPath = "Resources/player_ball/ball.obj";
    
    if (renderer) outJoint.modelHandle = renderer->LoadObjMesh(outJoint.modelPath);
    
    MeshRendererComponent mr;
    mr.modelHandle = outJoint.modelHandle;
    mr.modelPath = outJoint.modelPath;
    mr.shaderName = "Toon"; 
    outJoint.meshRenderers.push_back(mr);
}

void DefaultPipeBehavior::OnPlacementComplete(GameScene* /*scene*/, const Engine::Vector3& start, const Engine::Vector3& end) {
    EditorUI::Log("Pipe placed from (" + std::to_string(start.x) + "," + std::to_string(start.z) + ") to (" + std::to_string(end.x) + "," + std::to_string(end.z) + ")");
}


// ====== PipeEditor ======
PipeEditor::PipeEditor() {
    behavior_ = std::make_shared<DefaultPipeBehavior>();
}

uint32_t PipeEditor::GenerateId(GameScene* scene) const {
    uint32_t maxId = 0;
    for (const auto& obj : scene->GetObjects()) {
        if (obj.id > maxId) maxId = obj.id;
    }
    return maxId + 1;
}

void PipeEditor::ClearPreview(GameScene* scene) {
    auto objects = scene->GetObjects();
    for (int i = (int)objects.size() - 1; i >= 0; --i) {
        if ((int)objects[i].id == previewPipeId_ || (int)objects[i].id == previewJointId_) {
            objects.erase(objects.begin() + i);
        }
    }
    scene->SetObjects(objects);
    previewPipeId_ = -1;
    previewJointId_ = -1;
}

void PipeEditor::DrawUI() {
    if (ImGui::Button(pipeMode_ ? "Pipe Mode [ON]" : "Pipe Mode [OFF]")) {
        SetPipeMode(!pipeMode_);
    }
    
    if (pipeMode_) {
        ImGui::SameLine();
        ImGui::Checkbox("Snap Angle", &useAngleSnap_);
        if (useAngleSnap_) {
            ImGui::SameLine();
            ImGui::PushItemWidth(80);
            ImGui::SliderFloat("Step##Angle", &snapAngleStep_, 5.0f, 90.0f, "%.0f deg");
            ImGui::PopItemWidth();
        }
        ImGui::SameLine();
        ImGui::Checkbox("Node Snap", &useNodeSnap_);
        if (useNodeSnap_) {
            ImGui::SameLine();
            ImGui::PushItemWidth(80);
            ImGui::SliderFloat("Dist##NodeSnap", &nodeSnapThreshold_, 0.1f, 5.0f, "%.1f");
            ImGui::PopItemWidth();
        }
    }
}

void PipeEditor::UpdateAndDraw(GameScene* scene, Engine::Renderer* renderer, const ImVec2& gameImageMin, const ImVec2& /*gameImageMax*/, float tW, float tH) {
    if (!scene || scene->IsPlaying() || !pipeMode_ || !behavior_) {
        if (previewJointId_ != -1 || previewPipeId_ != -1) ClearPreview(scene);
        return;
    }

    ImVec2 mousePos = ImGui::GetMousePos();
    float localX = mousePos.x - gameImageMin.x;
    float localY = mousePos.y - gameImageMin.y;
    bool insideImage = (localX >= 0 && localY >= 0 && localX <= tW && localY <= tH);

    if (insideImage) {
        auto viewMat = scene->camera_.View();
        auto projMat = scene->camera_.Proj();
        DirectX::XMVECTOR rayOrig, rayDir;
        EditorUI::ScreenToWorldRay(localX, localY, tW, tH, viewMat, projMat, rayOrig, rayDir);

        float bestDist = FLT_MAX;
        Engine::Vector3 hitPoint = {0, 0, 0};
        bool hitTerrain = false;

        auto objects = scene->GetObjects();
        for (auto& obj : objects) {
            bool isTerrain = (obj.name.find("Terrain") != std::string::npos) || (obj.name.find("Floor") != std::string::npos);
            if (!isTerrain) continue;

            Engine::Model* model = nullptr;
            if (!obj.gpuMeshColliders.empty()) {
                model = renderer->GetModel(obj.gpuMeshColliders[0].meshHandle);
            }
            if (!model && obj.modelHandle != 0) {
                model = renderer->GetModel(obj.modelHandle);
            }
            if (!model && !obj.meshRenderers.empty() && obj.meshRenderers[0].modelHandle != 0) {
                model = renderer->GetModel(obj.meshRenderers[0].modelHandle);
            }
            if (model) {
                float d; Engine::Vector3 hp;
                if (model->RayCast(rayOrig, rayDir, obj.GetTransform().ToMatrix(), d, hp)) {
                    if (d < bestDist) {
                        bestDist = d;
                        hitPoint = hp;
                        hitTerrain = true;
                    }
                }
            }
        }

        if (hasPipeStart_ && !hitTerrain) {
            float height = pipeStartNode_.y + 0.5f;
            float dirY = DirectX::XMVectorGetY(rayDir);
            if (std::abs(dirY) > 1e-6f) {
                float t = (height - DirectX::XMVectorGetY(rayOrig)) / dirY;
                if (t > 0 && t < bestDist) {
                    bestDist = t;
                    DirectX::XMVECTOR pVec = DirectX::XMVectorAdd(rayOrig, DirectX::XMVectorScale(rayDir, t));
                    hitPoint = {DirectX::XMVectorGetX(pVec), height, DirectX::XMVectorGetZ(pVec)};
                    hitTerrain = true;
                }
            }
        }

        if (hitTerrain) {
            Engine::Vector3 endNode = hitPoint;
            Engine::Vector3 startPos = pipeStartNode_;
            if (hasPipeStart_) startPos.y += 0.5f;

            // 地面に当たった場合は少し浮かせる
            if (!hasPipeStart_ || std::abs(endNode.y - startPos.y) > 0.01f) endNode.y += 0.5f;

            // スナップ処理
            if (hasPipeStart_ && useAngleSnap_) {
                Engine::Vector3 diffSnap = endNode - startPos;
                float lengthXZ = std::sqrt(diffSnap.x*diffSnap.x + diffSnap.z*diffSnap.z);
                if (lengthXZ > 0.001f) {
                    float angle = std::atan2(diffSnap.z, diffSnap.x);
                    float stepRad = snapAngleStep_ * 3.14159265f / 180.0f;
                    float snappedAngle = std::round(angle / stepRad) * stepRad;
                    endNode.x = startPos.x + std::cos(snappedAngle) * lengthXZ;
                    endNode.z = startPos.z + std::sin(snappedAngle) * lengthXZ;
                }
            }

            if (useNodeSnap_) {
                float bestXDist = nodeSnapThreshold_;
                float bestZDist = nodeSnapThreshold_;
                float snapX = endNode.x;
                float snapZ = endNode.z;
                for (const auto& obj : objects) {
                    if (obj.name == "PipeJoint" || obj.name == "_PreviewJoint") {
                        float distX = std::abs(obj.translate.x - endNode.x);
                        float distZ = std::abs(obj.translate.z - endNode.z);
                        if (distX < bestXDist) { bestXDist = distX; snapX = obj.translate.x; }
                        if (distZ < bestZDist) { bestZDist = distZ; snapZ = obj.translate.z; }
                    }
                }
                endNode.x = snapX;
                endNode.z = snapZ;
            }

            endNode = behavior_->ApplyCustomSnapping(scene, endNode);

            // プレビューと配置
            if (hasPipeStart_) {
                float dx = endNode.x - startPos.x;
                float dy = endNode.y - startPos.y;
                float dz = endNode.z - startPos.z;
                float length = std::sqrt(dx*dx + dy*dy + dz*dz);

                ClearPreview(scene);
                auto currentObjects = scene->GetObjects();

                SceneObject previewJoint;
                behavior_->OnGenerateJoint(previewJoint, endNode, renderer);
                previewJoint.name = "_PreviewJoint";
                previewJoint.id = GenerateId(scene);
                previewJointId_ = previewJoint.id;
                for (auto& mr : previewJoint.meshRenderers) {
                    mr.color = {0.8f, 0.8f, 1.0f, 0.6f};
                    mr.shaderName = "SolidColor";
                }
                currentObjects.push_back(previewJoint);

                SceneObject previewPipe;
                behavior_->OnGeneratePipe(previewPipe, startPos, endNode, length, renderer);
                previewPipe.name = "_PreviewPipe";
                previewPipe.id = GenerateId(scene) + 1;
                previewPipeId_ = previewPipe.id;
                for (auto& mr : previewPipe.meshRenderers) {
                    mr.color = {0.8f, 1.0f, 0.8f, 0.6f};
                    mr.shaderName = "SolidColor";
                }
                currentObjects.push_back(previewPipe);
                scene->SetObjects(currentObjects);

                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
                    ClearPreview(scene);
                    auto finalObjects = scene->GetObjects();
                    
                    SceneObject finalJoint;
                    behavior_->OnGenerateJoint(finalJoint, endNode, renderer);
                    finalJoint.id = GenerateId(scene);
                    finalObjects.push_back(finalJoint);

                    SceneObject finalPipe;
                    behavior_->OnGeneratePipe(finalPipe, startPos, endNode, length, renderer);
                    finalPipe.id = GenerateId(scene) + 1;
                    finalObjects.push_back(finalPipe);
                    
                    scene->SetObjects(finalObjects);
                    behavior_->OnPlacementComplete(scene, startPos, endNode);
                    pipeStartNode_ = {endNode.x, endNode.y - 0.5f, endNode.z};
                }
            } else {
                ClearPreview(scene);
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
                    pipeStartNode_ = hitPoint;
                    hasPipeStart_ = true;

                    auto firstObjects = scene->GetObjects();
                    SceneObject joint;
                    behavior_->OnGenerateJoint(joint, {hitPoint.x, hitPoint.y + 0.5f, hitPoint.z}, renderer);
                    joint.id = GenerateId(scene);
                    firstObjects.push_back(joint);
                    scene->SetObjects(firstObjects);
                    EditorUI::Log("Pipe start placed.");
                }
            }
        }
    }

    if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        ClearPreview(scene);
        hasPipeStart_ = false;
        pipeStartNode_ = {0, 0, 0};
    }
}

} // namespace Game
