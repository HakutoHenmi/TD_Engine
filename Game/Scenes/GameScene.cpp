#include "GameScene.h"
#include "../Editor/EditorUI.h"
#include "imgui.h"
#include <cmath>

namespace Game {

void GameScene::Initialize(Engine::WindowDX* dx) {
    dx_ = dx;
    renderer_ = Engine::Renderer::GetInstance();
    camera_.Initialize();
    camera_.SetPosition(0, 2, -5);
    camera_.SetRotation(0.2f, 0, 0);
    renderer_->SetAmbientColor({0.4f, 0.4f, 0.45f});
    renderer_->SetDirectionalLight({0.3f, -1.0f, 0.5f}, {1.0f, 0.95f, 0.9f}, true);

    SceneObject plane;
    plane.name = "Plane";
    plane.modelHandle = renderer_->LoadObjMesh("Resources/plane.obj");
    plane.textureHandle = renderer_->LoadTexture2D("Resources/white1x1.png");
    plane.modelPath = "Resources/plane.obj";
    plane.texturePath = "Resources/white1x1.png";
    plane.scale = {5, 1, 5};
    objects_.push_back(plane);

    // ★追加: スタンドアロンエディタの初期化
    particleEditor_.Initialize();
}

void GameScene::Update() {
    // ★追加: アニメーションの更新 (エディタモードでも時間が進むようにする)
    float dt = ImGui::GetIO().DeltaTime;

    for (auto& obj : objects_) {
        for (auto& anim : obj.animators) {
            if (anim.enabled && anim.isPlaying) {
                anim.time += dt * 60.0f * anim.speed; // TicksPerSecondをとりあえず60fpsと仮定
                // モデルのアニメーションデータをチェックしてループ処理
                auto* m = renderer_->GetModel(obj.modelHandle);
                if (m) {
                    const auto& data = m->GetData();
                    for (const auto& a : data.animations) {
                        if (a.name == anim.currentAnimation) {
                            if (anim.time > a.duration) {
                                if (anim.loop) anim.time = std::fmod(anim.time, a.duration);
                                else { anim.time = a.duration; anim.isPlaying = false; }
                            }
                            break;
                        }
                    }
                }
            }
        }
    }

    // ★追加: スタンドアロンエディタの更新
    particleEditor_.Update(dt);

    if (isPlaying_) {
        // --- 物理挙動 (Rigidbody & BoxCollider) ---
        for (auto& obj : objects_) {
            for (auto& rb : obj.rigidbodies) {
                if (!rb.enabled || rb.isKinematic) continue;

                if (rb.useGravity) {
                    rb.velocity.y -= 9.8f * dt; // 重力加速度
                }

                // 速度による移動前の位置
                auto prevPos = obj.translate;
                obj.translate.x += rb.velocity.x * dt;
                obj.translate.y += rb.velocity.y * dt;
                obj.translate.z += rb.velocity.z * dt;

                // 衝突判定 (OBB vs OBB)
                if (obj.boxColliders.empty()) continue;
                auto& bc = obj.boxColliders[0];
                if (!bc.enabled) continue;

                auto getObbAxes = [](const SceneObject& o, const BoxColliderComponent& cb, Engine::Vector3 axes[3], Engine::Vector3& center, Engine::Vector3& extents) {
                    Engine::Matrix4x4 mat = o.GetTransform().ToMatrix();
                    DirectX::XMMATRIX worldMat = DirectX::XMLoadFloat4x4(reinterpret_cast<DirectX::XMFLOAT4X4*>(&mat));
                    // 中心
                    DirectX::XMVECTOR c = DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(cb.center.x, cb.center.y, cb.center.z, 1.0f), worldMat);
                    DirectX::XMStoreFloat3(reinterpret_cast<DirectX::XMFLOAT3*>(&center), c);
                    
                    // 各軸ベクトルと大きさ（スケール込み）の抽出
                    DirectX::XMVECTOR axisX = DirectX::XMVector3Normalize(worldMat.r[0]);
                    DirectX::XMVECTOR axisY = DirectX::XMVector3Normalize(worldMat.r[1]);
                    DirectX::XMVECTOR axisZ = DirectX::XMVector3Normalize(worldMat.r[2]);
                    DirectX::XMStoreFloat3(reinterpret_cast<DirectX::XMFLOAT3*>(&axes[0]), axisX);
                    DirectX::XMStoreFloat3(reinterpret_cast<DirectX::XMFLOAT3*>(&axes[1]), axisY);
                    DirectX::XMStoreFloat3(reinterpret_cast<DirectX::XMFLOAT3*>(&axes[2]), axisZ);
                    
                    // BoxColliderのサイズとオブジェクトスケールを乗算した半径 (extents)
                    // worldMat.rはすでにスケールが含まれているため、Normalize前の長さを使うか、scaleを直接掛ける
                    extents.x = cb.size.x * 0.5f * std::abs(o.scale.x);
                    extents.y = cb.size.y * 0.5f * std::abs(o.scale.y);
                    extents.z = cb.size.z * 0.5f * std::abs(o.scale.z);
                };

                Engine::Vector3 axes1[3], c1, e1;
                getObbAxes(obj, bc, axes1, c1, e1);

                for (const auto& other : objects_) {
                    if (&obj == &other) continue;
                    if (other.boxColliders.empty()) continue;

                    const auto& obc = other.boxColliders[0];
                    if (!obc.enabled || obc.isTrigger) continue;

                    Engine::Vector3 axes2[3], c2, e2;
                    getObbAxes(other, obc, axes2, c2, e2);

                    // SAT (Separating Axis Theorem)
                    DirectX::XMVECTOR diff = DirectX::XMVectorSubtract(
                        DirectX::XMLoadFloat3(reinterpret_cast<const DirectX::XMFLOAT3*>(&c2)),
                        DirectX::XMLoadFloat3(reinterpret_cast<const DirectX::XMFLOAT3*>(&c1))
                    );

                    float minOverlap = FLT_MAX;
                    Engine::Vector3 pushAxis = {0,0,0};
                    bool intersected = true;

                    // 判定に使う15軸 (axes1[3], axes2[3], それらの外積9本)
                    std::vector<DirectX::XMVECTOR> testAxes;
                    for (int i=0; i<3; ++i) testAxes.push_back(DirectX::XMLoadFloat3(reinterpret_cast<const DirectX::XMFLOAT3*>(&axes1[i])));
                    for (int i=0; i<3; ++i) testAxes.push_back(DirectX::XMLoadFloat3(reinterpret_cast<const DirectX::XMFLOAT3*>(&axes2[i])));
                    for (int i=0; i<3; ++i) {
                        for (int j=0; j<3; ++j) {
                            DirectX::XMVECTOR cross = DirectX::XMVector3Cross(
                                DirectX::XMLoadFloat3(reinterpret_cast<const DirectX::XMFLOAT3*>(&axes1[i])),
                                DirectX::XMLoadFloat3(reinterpret_cast<const DirectX::XMFLOAT3*>(&axes2[j]))
                            );
                            if (DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(cross)) > 1e-6f) {
                                testAxes.push_back(DirectX::XMVector3Normalize(cross));
                            }
                        }
                    }

                    for (const auto& axis : testAxes) {
                        float r1 = e1.x * std::abs(DirectX::XMVectorGetX(DirectX::XMVector3Dot(axis, DirectX::XMLoadFloat3(reinterpret_cast<const DirectX::XMFLOAT3*>(&axes1[0]))))) +
                                   e1.y * std::abs(DirectX::XMVectorGetX(DirectX::XMVector3Dot(axis, DirectX::XMLoadFloat3(reinterpret_cast<const DirectX::XMFLOAT3*>(&axes1[1]))))) +
                                   e1.z * std::abs(DirectX::XMVectorGetX(DirectX::XMVector3Dot(axis, DirectX::XMLoadFloat3(reinterpret_cast<const DirectX::XMFLOAT3*>(&axes1[2])))));
                        float r2 = e2.x * std::abs(DirectX::XMVectorGetX(DirectX::XMVector3Dot(axis, DirectX::XMLoadFloat3(reinterpret_cast<const DirectX::XMFLOAT3*>(&axes2[0]))))) +
                                   e2.y * std::abs(DirectX::XMVectorGetX(DirectX::XMVector3Dot(axis, DirectX::XMLoadFloat3(reinterpret_cast<const DirectX::XMFLOAT3*>(&axes2[1]))))) +
                                   e2.z * std::abs(DirectX::XMVectorGetX(DirectX::XMVector3Dot(axis, DirectX::XMLoadFloat3(reinterpret_cast<const DirectX::XMFLOAT3*>(&axes2[2])))));
                        
                        float distance = std::abs(DirectX::XMVectorGetX(DirectX::XMVector3Dot(diff, axis)));
                        float overlap = r1 + r2 - distance;

                        if (overlap <= 0.0f) {
                            intersected = false;
                            break; // 隙間が見つかったので衝突していない
                        }

                        if (overlap < minOverlap) {
                            minOverlap = overlap;
                            DirectX::XMStoreFloat3(reinterpret_cast<DirectX::XMFLOAT3*>(&pushAxis), axis);
                            if (DirectX::XMVectorGetX(DirectX::XMVector3Dot(diff, axis)) > 0) {
                                pushAxis.x *= -1; pushAxis.y *= -1; pushAxis.z *= -1;
                            }
                        }
                    }

                    if (intersected) {
                        // 最小押し出しベクトル(MTV)に沿って位置を補正
                        obj.translate.x += pushAxis.x * minOverlap;
                        obj.translate.y += pushAxis.y * minOverlap;
                        obj.translate.z += pushAxis.z * minOverlap;

                        // 押し出された軸方向の速度をゼロにする（簡易対応）
                        DirectX::XMVECTOR vel = DirectX::XMLoadFloat3(reinterpret_cast<const DirectX::XMFLOAT3*>(&rb.velocity));
                        DirectX::XMVECTOR pA = DirectX::XMLoadFloat3(reinterpret_cast<const DirectX::XMFLOAT3*>(&pushAxis));
                        float dotV = DirectX::XMVectorGetX(DirectX::XMVector3Dot(vel, pA));
                        if(dotV < 0) { // 押し出し方向と逆(めり込む方向)に動いていた場合のみ減衰
                            DirectX::XMVECTOR vN = DirectX::XMVectorScale(pA, dotV);
                            vel = DirectX::XMVectorSubtract(vel, vN);
                            DirectX::XMStoreFloat3(reinterpret_cast<DirectX::XMFLOAT3*>(&rb.velocity), vel);
                        }

                        // AABB/OBBの再計算 (連鎖衝突用)
                        getObbAxes(obj, bc, axes1, c1, e1);
                    }
                }
            }
        }
    }

    // ★追加: パーティクルの更新 (Transformを追従させる)
    for (auto& obj : objects_) {
        for (auto& emitterComp : obj.particleEmitters) {
            if (!emitterComp.enabled) continue;

			if (!emitterComp.isInitialized) {
				emitterComp.emitter.Initialize(*renderer_, obj.name + "_Emitter");
				if (!emitterComp.assetPath.empty()) {
					emitterComp.emitter.LoadFromJson(emitterComp.assetPath);
				}
				emitterComp.isInitialized = true;
			}

            // エミッターの位置をオブジェクトの位置に合わせる
            emitterComp.emitter.params.position = {obj.translate.x, obj.translate.y, obj.translate.z};
            emitterComp.emitter.Update(dt);
        }
    }
}

void GameScene::Draw() {
    renderer_->SetCamera(camera_);
#ifdef _DEBUG
    DrawEditorGizmos();
#endif

    // ★追加: スタンドアロンエディタのプレビュー描画
    particleEditor_.DrawPreview(camera_);

    for (const auto& obj : objects_) {
        bool hasMeshRenderer = false;
        for (const auto& mr : obj.meshRenderers) {
            if (mr.enabled && mr.modelHandle != 0) {
                hasMeshRenderer = true;
                bool hasAnim = false;
                std::vector<Engine::Matrix4x4> bonePalette;
                for (const auto& anim : obj.animators) {
                    if (anim.enabled && !anim.currentAnimation.empty()) {
                        auto* m = renderer_->GetModel(mr.modelHandle);
                        if (m) {
                            const auto& data = m->GetData();
                            const Engine::Animation* currAnim = nullptr;
                            for (const auto& a : data.animations) {
                                if (a.name == anim.currentAnimation) { currAnim = &a; break; }
                            }
                            if (currAnim) {
                                bonePalette.resize(data.bones.size());
                                for (auto& b : bonePalette) b = Engine::Matrix4x4::Identity();
                                m->UpdateSkeleton(data.rootNode, Engine::Matrix4x4::Identity(), *currAnim, anim.time, bonePalette);
                                hasAnim = true;
                                break;
                            }
                        }
                    }
                }

                if (hasAnim) {
                    renderer_->DrawSkinnedMesh(mr.modelHandle, mr.textureHandle, obj.GetTransform(), bonePalette, 
                        {mr.color.x, mr.color.y, mr.color.z, mr.color.w});
                } else {
                    renderer_->DrawMesh(mr.modelHandle, mr.textureHandle, obj.GetTransform(), 
                        {mr.color.x, mr.color.y, mr.color.z, mr.color.w});
                }
            }
        }

        // 後方互換性: MeshRendererを持たない古いオブジェクトの場合はデフォルトで描画
        if (!hasMeshRenderer && obj.modelHandle != 0) {
            bool hasAnim = false;
            std::vector<Engine::Matrix4x4> bonePalette;
            for (const auto& anim : obj.animators) {
                if (anim.enabled && !anim.currentAnimation.empty()) {
                    auto* m = renderer_->GetModel(obj.modelHandle);
                    if (m) {
                        const auto& data = m->GetData();
                        const Engine::Animation* currAnim = nullptr;
                        for (const auto& a : data.animations) {
                            if (a.name == anim.currentAnimation) { currAnim = &a; break; }
                        }
                        if (currAnim) {
                            bonePalette.resize(data.bones.size());
                            for (auto& b : bonePalette) b = Engine::Matrix4x4::Identity();
                            m->UpdateSkeleton(data.rootNode, Engine::Matrix4x4::Identity(), *currAnim, anim.time, bonePalette);
                            hasAnim = true;
                            break;
                        }
                    }
                }
            }

            if (hasAnim) {
                renderer_->DrawSkinnedMesh(obj.modelHandle, obj.textureHandle, obj.GetTransform(), bonePalette, 
                    {obj.color.x, obj.color.y, obj.color.z, obj.color.w});
            } else {
                renderer_->DrawMesh(obj.modelHandle, obj.textureHandle, obj.GetTransform(), 
                    {obj.color.x, obj.color.y, obj.color.z, obj.color.w});
            }
        }
    }
#ifdef _DEBUG
    // ★ 選択ハイライトをDraw()内で描画（ゲームテクスチャに描画されるように）
    DrawSelectionHighlight();
#endif
    // ★追加: パーティクルの描画 (各オブジェクトのコンポーネント)
    for (auto& obj : objects_) {
        for (auto& emitterComp : obj.particleEmitters) {
            if (emitterComp.enabled) {
                emitterComp.emitter.Draw(camera_);
            }
        }
    }
}

// ★ 選択ハイライト + ギズモ描画（ゲームRenderTarget上で）
// ★ EditorUI.cppで定義されたgizmo状態変数のextern宣言
extern GizmoMode currentGizmoMode;
extern bool gizmoDragging;
extern int gizmoDragAxis;

void GameScene::DrawSelectionHighlight() {
    if (!renderer_) return;

    for (int idx : selectedIndices_) {
        if (idx < 0 || idx >= (int)objects_.size()) continue;
        auto& obj = objects_[idx];
        Engine::Vector3 pos = {obj.translate.x, obj.translate.y, obj.translate.z};

        // ★ ハイライト: 黄色のバウンディングボックス (OBB対応)
        Engine::Matrix4x4 mat = obj.GetTransform().ToMatrix();
        DirectX::XMMATRIX worldMat = DirectX::XMLoadFloat4x4(reinterpret_cast<DirectX::XMFLOAT4X4*>(&mat));

        Engine::Vector4 hlColor = {1.0f, 0.85f, 0.0f, 1.0f};
        Engine::Vector3 v[8] = {
            {-1.0f,-1.0f,-1.0f},{1.0f,-1.0f,-1.0f},
            {1.0f,1.0f,-1.0f},{-1.0f,1.0f,-1.0f},
            {-1.0f,-1.0f,1.0f},{1.0f,-1.0f,1.0f},
            {1.0f,1.0f,1.0f},{-1.0f,1.0f,1.0f},
        };

        for(int i=0; i<8; ++i) {
            DirectX::XMVECTOR p = DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(v[i].x, v[i].y, v[i].z, 1.0f), worldMat);
            DirectX::XMStoreFloat3(reinterpret_cast<DirectX::XMFLOAT3*>(&v[i]), p);
        }
        int edges[][2] = {{0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7}};
        for (auto& eg : edges) renderer_->DrawLine3D(v[eg[0]], v[eg[1]], hlColor);

        // ★追加: コライダー可視化 (緑色のワイヤーフレーム)
        for (const auto& bc : obj.boxColliders) {
            if (!bc.enabled) continue;
            float hx = bc.size.x * 0.5f;
            float hy = bc.size.y * 0.5f;
            float hz = bc.size.z * 0.5f;
            Engine::Vector3 cp = {bc.center.x, bc.center.y, bc.center.z};
            Engine::Vector4 colColor = {0.2f, 1.0f, 0.2f, 0.8f}; // 緑色
            Engine::Vector3 cv[8] = {
                {cp.x-hx,cp.y-hy,cp.z-hz},{cp.x+hx,cp.y-hy,cp.z-hz},{cp.x+hx,cp.y+hy,cp.z-hz},{cp.x-hx,cp.y+hy,cp.z-hz},
                {cp.x-hx,cp.y-hy,cp.z+hz},{cp.x+hx,cp.y-hy,cp.z+hz},{cp.x+hx,cp.y+hy,cp.z+hz},{cp.x-hx,cp.y+hy,cp.z+hz},
            };
            for(int i=0; i<8; ++i) {
                DirectX::XMVECTOR p = DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(cv[i].x, cv[i].y, cv[i].z, 1.0f), worldMat);
                DirectX::XMStoreFloat3(reinterpret_cast<DirectX::XMFLOAT3*>(&cv[i]), p);
            }
            for (auto& eg : edges) renderer_->DrawLine3D(cv[eg[0]], cv[eg[1]], colColor);
        }

        // ★ ギズモ軸描画 (ローカル座標ベースで回転を適用)
        DirectX::XMMATRIX gizmoMat = DirectX::XMMatrixRotationRollPitchYaw(obj.rotate.x, obj.rotate.y, obj.rotate.z) * DirectX::XMMatrixTranslation(obj.translate.x, obj.translate.y, obj.translate.z);
        auto drawLocalLine = [&](const Engine::Vector3& localP0, const Engine::Vector3& localP1, const Engine::Vector4& col) {
            DirectX::XMVECTOR p0 = DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(localP0.x, localP0.y, localP0.z, 1.0f), gizmoMat);
            DirectX::XMVECTOR p1 = DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(localP1.x, localP1.y, localP1.z, 1.0f), gizmoMat);
            Engine::Vector3 wp0, wp1;
            DirectX::XMStoreFloat3(reinterpret_cast<DirectX::XMFLOAT3*>(&wp0), p0);
            DirectX::XMStoreFloat3(reinterpret_cast<DirectX::XMFLOAT3*>(&wp1), p1);
            renderer_->DrawLine3D(wp0, wp1, col);
        };

        const float al = 2.0f, ar = 0.3f;
        int dAxis = (gizmoDragging && idx == selectedObjectIndex_) ? gizmoDragAxis : -1;
        auto axCol = [](int axis, int drag) -> Engine::Vector4 {
            bool a = (drag == axis);
            switch (axis) {
                case 0: return a ? Engine::Vector4{1,.6f,.6f,1} : Engine::Vector4{1,.2f,.2f,1};
                case 1: return a ? Engine::Vector4{.6f,1,.6f,1} : Engine::Vector4{.2f,1,.2f,1};
                case 2: return a ? Engine::Vector4{.6f,.6f,1,1} : Engine::Vector4{.2f,.2f,1,1};
                default: return {1,1,1,1};
            }
        };
        auto cX = axCol(0, dAxis), cY = axCol(1, dAxis), cZ = axCol(2, dAxis);

        if (currentGizmoMode == GizmoMode::Translate) {
            drawLocalLine({0,0,0}, {al,0,0}, cX);
            drawLocalLine({al,0,0}, {al-ar,ar*.4f,0}, cX);
            drawLocalLine({al,0,0}, {al-ar,-ar*.4f,0}, cX);
            drawLocalLine({0,0,0}, {0,al,0}, cY);
            drawLocalLine({0,al,0}, {ar*.4f,al-ar,0}, cY);
            drawLocalLine({0,al,0}, {-ar*.4f,al-ar,0}, cY);
            drawLocalLine({0,0,0}, {0,0,al}, cZ);
            drawLocalLine({0,0,al}, {0,ar*.4f,al-ar}, cZ);
            drawLocalLine({0,0,al}, {0,-ar*.4f,al-ar}, cZ);
        } else if (currentGizmoMode == GizmoMode::Rotate) {
            const int seg = 32; const float rad = 1.5f;
            for (int i = 0; i < seg; ++i) {
                float a0 = (float)i/seg*DirectX::XM_2PI, a1 = (float)(i+1)/seg*DirectX::XM_2PI;
                drawLocalLine({0, cosf(a0)*rad, sinf(a0)*rad}, {0, cosf(a1)*rad, sinf(a1)*rad}, cX);
                drawLocalLine({cosf(a0)*rad, 0, sinf(a0)*rad}, {cosf(a1)*rad, 0, sinf(a1)*rad}, cY);
                drawLocalLine({cosf(a0)*rad, sinf(a0)*rad, 0}, {cosf(a1)*rad, sinf(a1)*rad, 0}, cZ);
            }
        } else {
            float e = 0.15f;
            drawLocalLine({0,0,0}, {al,0,0}, cX);
            drawLocalLine({al-e,-e,0}, {al+e,e,0}, cX);
            drawLocalLine({al+e,-e,0}, {al-e,e,0}, cX);
            drawLocalLine({0,0,0}, {0,al,0}, cY);
            drawLocalLine({-e,al-e,0}, {e,al+e,0}, cY);
            drawLocalLine({e,al-e,0}, {-e,al+e,0}, cY);
            drawLocalLine({0,0,0}, {0,0,al}, cZ);
            drawLocalLine({0,-e,al-e}, {0,e,al+e}, cZ);
            drawLocalLine({0,e,al-e}, {0,-e,al+e}, cZ);
        }
    }
    renderer_->FlushLines();
}

void GameScene::DrawEditorGizmos() {
    if (!renderer_) return;
    const float gridSize = 50.0f, step = 1.0f;
    for (float i = -gridSize; i <= gridSize; i += step) {
        float alpha = 0.15f;
        if (std::fabs(i) < 0.01f) continue;
        if (std::fmod(std::fabs(i), 10.0f) < 0.01f) alpha = 0.35f;
        else if (std::fmod(std::fabs(i), 5.0f) < 0.01f) alpha = 0.25f;
        Engine::Vector4 gc = {0.5f, 0.5f, 0.5f, alpha};
        renderer_->DrawLine3D({-gridSize, 0.0f, i}, {gridSize, 0.0f, i}, gc);
        renderer_->DrawLine3D({i, 0.0f, -gridSize}, {i, 0.0f, gridSize}, gc);
    }
    renderer_->DrawLine3D({-gridSize, 0.0f, 0.0f}, {gridSize, 0.0f, 0.0f}, {0.8f, 0.2f, 0.2f, 0.6f});
    renderer_->DrawLine3D({0.0f, 0.0f, -gridSize}, {0.0f, 0.0f, gridSize}, {0.2f, 0.2f, 0.8f, 0.6f});
    renderer_->DrawLine3D({0, 0, 0}, {1.5f, 0, 0}, {1.f, 0.2f, 0.2f, 1.f});
    renderer_->DrawLine3D({0, 0, 0}, {0, 1.5f, 0}, {0.2f, 1.f, 0.2f, 1.f});
    renderer_->DrawLine3D({0, 0, 0}, {0, 0, 1.5f}, {0.2f, 0.2f, 1.f, 1.f});
    renderer_->FlushLines();
}

void GameScene::DrawEditor() {
#ifdef _DEBUG
    EditorUI::Show(renderer_, this);
    
    // ★追加: スタンドアロンパーティクルエディタのUI
    particleEditor_.DrawUI();
#endif
}

} // namespace Game