#include "GameScene.h"
#include "../Editor/EditorUI.h"
#include "imgui.h"
#include "Audio.h"
#include <cmath>

namespace Game {

void GameScene::Initialize(Engine::WindowDX* dx) {
    dx_ = dx;
    renderer_ = Engine::Renderer::GetInstance();
    camera_.Initialize();
    camera_.SetPosition(0, 2, -5);
    camera_.SetRotation(0.2f, 0, 0);
    renderer_->SetAmbientColor({0.4f, 0.4f, 0.45f});

    // デフォルトの太陽光オブジェクトを作成
    SceneObject sun;
    sun.name = "Sun";
    sun.translate = {0, 10, 0};
    sun.rotate = {DirectX::XMConvertToRadians(45.0f), DirectX::XMConvertToRadians(30.0f), 0};
    sun.directionalLights.push_back(DirectionalLightComponent());
    objects_.push_back(sun);

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
    float dt = ImGui::GetIO().DeltaTime;
    auto* input = Engine::Input::GetInstance();

    // -------------------------------------------------------------
    // ★ 1. Player Input System (意思決定)
    // -------------------------------------------------------------
    if (isPlaying_) {
        for (auto& obj : objects_) {
            for (auto& pi : obj.playerInputs) {
                if (!pi.enabled) continue;

            // WASD入力から移動ベクトルを作成
            DirectX::XMFLOAT2 moveDir = {0.0f, 0.0f};
            if (input->Down(DIK_W)) moveDir.y += 1.0f;
            if (input->Down(DIK_S)) moveDir.y -= 1.0f;
            if (input->Down(DIK_A)) moveDir.x -= 1.0f;
            if (input->Down(DIK_D)) moveDir.x += 1.0f;

            // 正規化
            float len = std::sqrt(moveDir.x * moveDir.x + moveDir.y * moveDir.y);
            if (len > 0.001f) {
                moveDir.x /= len;
                moveDir.y /= len;
            }
            pi.moveDir = moveDir;

            // ジャンプ入力
            			if (input->Trigger(DIK_SPACE)) pi.jumpRequested = true;
			else pi.jumpRequested = false;

            // 攻撃入力など (現在はマウス左クリックの代わりにJキーなどを使用)
            if (input->Trigger(DIK_J)) pi.attackRequested = true;
            else pi.attackRequested = false;

            // マウス視点操作 (右ドラッグ中のみ旋回)
            pi.cameraYaw = 0.0f;
            pi.cameraPitch = 0.0f;
            
            // DirectInputのMouse Stateを直接持っているわけではないが、通常 VK_RBUTTON 等はGetKeyStateなどで取れる。
            // しかしInputクラスにMouse Buttonの取得メソッドがないため、Windows APIの GetAsyncKeyState を使うか、Inputを拡張する。
            if (GetAsyncKeyState(VK_RBUTTON) & 0x8000) {
                float dx = input->GetMouseDeltaX();
                float dy = input->GetMouseDeltaY();
                pi.cameraYaw = dx * 0.005f;   // 感度調整
                pi.cameraPitch = dy * 0.005f;
            }
        }
    }
    } // end isPlaying_ Input System

    // -------------------------------------------------------------
    // ★ 2. Character Movement System (実際の移動処理)
    // -------------------------------------------------------------
    if (isPlaying_) {
        for (auto& obj : objects_) {
            // PlayerInput がある場合はその意思を受け取る
            DirectX::XMFLOAT2 moveDir = {0, 0};
            bool wantJump = false;
        if (!obj.playerInputs.empty() && obj.playerInputs[0].enabled) {
            moveDir = obj.playerInputs[0].moveDir;
            wantJump = obj.playerInputs[0].jumpRequested;
        }

        for (auto& cm : obj.characterMovements) {
            if (!cm.enabled) continue;

            // カメラのY軸回転を利用して、移動方向をカメラ基準にする
            auto camRot = camera_.Rotation();
            float cy = std::cos(camRot.y);
            float sy = std::sin(camRot.y);

            // X->Right, Y->Forward
            float moveX = moveDir.x * cy + moveDir.y * sy;
            float moveZ = -moveDir.x * sy + moveDir.y * cy;

            // 平面移動
            obj.translate.x += moveX * cm.speed * dt;
            obj.translate.z += moveZ * cm.speed * dt;

            // オブジェクトの向き(Y軸回転)を移動方向に向ける (SmoothDamp等の補間があるとより良い)
            if (std::abs(moveDir.x) > 0.01f || std::abs(moveDir.y) > 0.01f) {
                float targetYaw = std::atan2(moveX, moveZ);
                // 簡易的に即座に振り向く
                obj.rotate.y = targetYaw;
            }

            // 重力とジャンプ
            if (!cm.isGrounded) {
                cm.velocityY += cm.gravity * dt;
            }

            if (cm.isGrounded && wantJump) {
                cm.velocityY = cm.jumpPower;
                cm.isGrounded = false;
            }

            obj.translate.y += cm.velocityY * dt;

            // 簡易的な床判定 (Y=0を床とする。GpuMeshColliderと連携する場合はそちらを使用)
            if (obj.translate.y <= 0.0f) {
                obj.translate.y = 0.0f;
                cm.velocityY = 0.0f;
                cm.isGrounded = true;
            } else {
                cm.isGrounded = false;
            }
        }
    }
    } // end isPlaying_ Movement System

    // -------------------------------------------------------------
    // ★ 3. Camera Follow System (カメラ追従)
    // -------------------------------------------------------------
    if (isPlaying_) {
        for (const auto& obj : objects_) {
            for (const auto& ct : obj.cameraTargets) {
                if (!ct.enabled) continue;

            // オブジェクトの座標
            DirectX::XMFLOAT3 targetPos = obj.translate;

            // 現在のカメラの角度（マウス右ドラッグ等で変更されている前提）
            DirectX::XMFLOAT3 camRot = camera_.Rotation();

            // 追従目標の計算（オブジェクトの後ろ・上に配置）
            // 視点の更新 (PlayerInputなどで設定した回転量を適用)
            // この簡易実装では、オブジェクトの最初の PlayerInput の intent をそのまま使用します。
            if (!obj.playerInputs.empty() && obj.playerInputs[0].enabled) {
                auto rot = camera_.Rotation();
                rot.y += obj.playerInputs[0].cameraYaw;
                rot.x += obj.playerInputs[0].cameraPitch;
                
                // ピッチの制限 (上下を見すぎないように)
                const float PITCH_LIMIT = 1.5f; // 約85度
                if (rot.x > PITCH_LIMIT) rot.x = PITCH_LIMIT;
                if (rot.x < -PITCH_LIMIT) rot.x = -PITCH_LIMIT;
                
                camera_.SetRotation(rot);
            }

            // カメラの現在の回転から、ターゲットに対するオフセットを計算
            auto curRot = camera_.Rotation();
            // Y軸回転 (Yaw) と X軸回転 (Pitch) を考慮したオフセット
            float camSy = std::sin(curRot.y);
            float camCy = std::cos(curRot.y);
            float camSx = std::sin(curRot.x);
            float camCx = std::cos(curRot.x);

            // カメラはターゲットから後方(-cy, -sy) かつ 上方(sx)に配置
            // 球面座標系ベースのオフセット計算
            DirectX::XMFLOAT3 offset = {
                -camSy * camCx * ct.distance,
                ct.height + camSx * ct.distance,
                -camCy * camCx * ct.distance
            };
            // ※ 元の実装は単純に Z に distance でしたが、旋回に対応するため変更しました。
            // Engine::Camera の仕様（Pitch=X, Yaw=Y）に合わせた想定の回転オフセット計算です。
            // 注意: TD_EngineのCamera実装によっては xyz の軸マッピングが異なる可能性があります。

            DirectX::XMFLOAT3 desiredPos = {
                targetPos.x + offset.x,
                targetPos.y + offset.y,
                targetPos.z + offset.z
            };

            // スムーズなカメラ移動 (Lerp)
            DirectX::XMFLOAT3 currentPos = camera_.Position();
            float t = ct.smoothSpeed * dt;
            if (t > 1.0f) t = 1.0f;

            DirectX::XMFLOAT3 newPos = {
                currentPos.x + (desiredPos.x - currentPos.x) * t,
                currentPos.y + (desiredPos.y - currentPos.y) * t,
                currentPos.z + (desiredPos.z - currentPos.z) * t
            };

            camera_.SetPosition(newPos);
            break; // 1つのカメラターゲットのみ追従
        }
    }
    } // end isPlaying_ Camera System

    // ★追加: 前フレームのGPUポリゴン当たり判定結果の読み取りと、今フレームのディスパッチ
    if (renderer_) {
        // 結果の反映
        uint32_t pairIndex = 0;
        for (size_t i = 0; i < objects_.size(); ++i) {
            auto& objA = objects_[i];
            for (auto& mc : objA.gpuMeshColliders) mc.isIntersecting = false;
        }

        for (size_t i = 0; i < objects_.size(); ++i) {
            auto& objA = objects_[i];
            if (objA.gpuMeshColliders.empty() || !objA.gpuMeshColliders[0].enabled) continue;

            for (size_t j = i + 1; j < objects_.size(); ++j) {
                auto& objB = objects_[j];
                if (objB.gpuMeshColliders.empty() || !objB.gpuMeshColliders[0].enabled) continue;

                if (renderer_->GetCollisionResult(pairIndex)) {
                    objA.gpuMeshColliders[0].isIntersecting = true;
                    objB.gpuMeshColliders[0].isIntersecting = true;
                }
                pairIndex++;
            }
        }

        // 今フレームのディスパッチ
        uint32_t numPairs = 0;
        for (size_t i = 0; i < objects_.size(); ++i) {
            if (!objects_[i].gpuMeshColliders.empty() && objects_[i].gpuMeshColliders[0].enabled) {
                for (size_t j = i + 1; j < objects_.size(); ++j) {
                    if (!objects_[j].gpuMeshColliders.empty() && objects_[j].gpuMeshColliders[0].enabled) numPairs++;
                }
            }
        }

        renderer_->BeginCollisionCheck(numPairs);
        pairIndex = 0;
        for (size_t i = 0; i < objects_.size(); ++i) {
            auto& objA = objects_[i];
            if (objA.gpuMeshColliders.empty() || !objA.gpuMeshColliders[0].enabled) continue;

            for (size_t j = i + 1; j < objects_.size(); ++j) {
                auto& objB = objects_[j];
                if (objB.gpuMeshColliders.empty() || !objB.gpuMeshColliders[0].enabled) continue;

                uint32_t meshA = objA.gpuMeshColliders[0].meshHandle;
                uint32_t meshB = objB.gpuMeshColliders[0].meshHandle;
                
                if (meshA == 0) meshA = objA.modelHandle;
                if (meshB == 0) meshB = objB.modelHandle;

                if (meshA != 0 && meshB != 0) {
                    renderer_->DispatchCollision(meshA, objA.GetTransform(), meshB, objB.GetTransform(), pairIndex);
                }
                pairIndex++;
            }
        }
        renderer_->EndCollisionCheck();
    }

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

    // -------------------------------------------------------------
    // ★ 4. Light System (ライト情報の Renderer への送信)
    // -------------------------------------------------------------
    if (renderer_) {
        int plCount = 0;
        int slCount = 0;
        bool hasDirLight = false;

        for (const auto& obj : objects_) {
            for (const auto& dl : obj.directionalLights) {
                if (dl.enabled && !hasDirLight) {
                    // Y軸回転等を考慮したZ軸(前方向)を取得
                    Engine::Matrix4x4 mat = obj.GetTransform().ToMatrix();
                    Engine::Vector3 dir = {mat.m[2][0], mat.m[2][1], mat.m[2][2]};
                    Engine::Vector3 color = {dl.color.x * dl.intensity, dl.color.y * dl.intensity, dl.color.z * dl.intensity};
                    renderer_->SetDirectionalLight(dir, color, true);
                    hasDirLight = true;
                }
            }
            for (const auto& pl : obj.pointLights) {
                if (pl.enabled && plCount < Engine::Renderer::kMaxPointLights) {
                    Engine::Vector3 pos = {obj.translate.x, obj.translate.y, obj.translate.z};
                    Engine::Vector3 color = {pl.color.x * pl.intensity, pl.color.y * pl.intensity, pl.color.z * pl.intensity};
                    Engine::Vector3 atten = {pl.atten.x, pl.atten.y, pl.atten.z};
                    renderer_->SetPointLight(plCount, pos, color, pl.range, atten, true);
                    plCount++;
                }
            }
            for (const auto& sl : obj.spotLights) {
                if (sl.enabled && slCount < Engine::Renderer::kMaxSpotLights) {
                    Engine::Matrix4x4 mat = obj.GetTransform().ToMatrix();
                    Engine::Vector3 dir = {mat.m[2][0], mat.m[2][1], mat.m[2][2]};
                    Engine::Vector3 pos = {obj.translate.x, obj.translate.y, obj.translate.z};
                    Engine::Vector3 color = {sl.color.x * sl.intensity, sl.color.y * sl.intensity, sl.color.z * sl.intensity};
                    Engine::Vector3 atten = {sl.atten.x, sl.atten.y, sl.atten.z};
                    renderer_->SetSpotLight(slCount, pos, dir, color, sl.range, sl.innerCos, sl.outerCos, atten, true);
                    slCount++;
                }
            }
        }

        // 見つからなかったスロットのライトを無効化
        if (!hasDirLight) {
            renderer_->SetDirectionalLight({0, -1, 0}, {0, 0, 0}, false);
        }
        for (int i = plCount; i < Engine::Renderer::kMaxPointLights; ++i) {
            renderer_->SetPointLight(i, {0, 0, 0}, {0, 0, 0}, 0, {1, 0, 0}, false);
        }
        for (int i = slCount; i < Engine::Renderer::kMaxSpotLights; ++i) {
            renderer_->SetSpotLight(i, {0, 0, 0}, {0, -1, 0}, {0, 0, 0}, 0, 0.0f, 0.0f, {1, 0, 0}, false);
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

    // ★追加: AudioSource の Play モード開始時自動再生 & 3D距離減衰
    if (isPlaying_) {
        // AudioListener の位置を取得 (最初に見つかったものを使用、なければカメラ位置)
        DirectX::XMFLOAT3 listenerPos = camera_.Position();
        for (const auto& obj : objects_) {
            for (const auto& al : obj.audioListeners) {
                if (al.enabled) {
                    listenerPos = obj.translate;
                    goto found_listener;
                }
            }
        }
        found_listener:

        auto* audio = Engine::Audio::GetInstance();
        if (audio) {
            for (auto& obj : objects_) {
                for (auto& as : obj.audioSources) {
                    if (!as.enabled) continue;

                    // playOnStart で未再生ならば再生開始
                    if (as.playOnStart && !as.isPlaying && as.soundHandle != 0xFFFFFFFF) {
                        as.voiceHandle = audio->Play(as.soundHandle, as.loop, as.volume);
                        as.isPlaying = true;
                    }

                    // 音量更新 & 3D距離減衰 (再生中のみ)
                    if (as.isPlaying && as.voiceHandle != 0) {
                        float finalVol = as.volume;
                        if (as.is3D) {
                            float dx = obj.translate.x - listenerPos.x;
                            float dy = obj.translate.y - listenerPos.y;
                            float dz = obj.translate.z - listenerPos.z;
                            float dist = std::sqrt(dx*dx + dy*dy + dz*dz);
                            if (as.maxDistance > 0.001f) {
                                float atten = 1.0f - (dist / as.maxDistance);
                                if (atten < 0.0f) atten = 0.0f;
                                // より自然な減衰にするため2乗減衰を適用
                                atten = atten * atten;
                                finalVol *= atten;
                            }
                        }
                        audio->SetVolume(as.voiceHandle, finalVol);
                    }
                }
            }
        }
    }

    // ★追加: Health の更新処理 (無敵時間のカウントダウン等)
    if (isPlaying_) {
        for (auto& obj : objects_) {
            for (auto& hc : obj.healths) {
                if (!hc.enabled || hc.isDead) continue;

                // 無敵時間の減少
                if (hc.invincibleTime > 0.0f) {
                    hc.invincibleTime -= dt;
                    if (hc.invincibleTime < 0.0f) hc.invincibleTime = 0.0f;
                }

                // 簡易的な死亡判定
                if (hc.hp <= 0.0f && !hc.isDead) {
                    hc.isDead = true;
                    // TODO: 死亡時のエフェクト生成やオブジェクト削除フラグ立てなど
                    // EditorUI::Log("Object Died: " + obj.name); // GameSceneからEditorUIの機能は直接呼べないので必要ならリスナー経由で
                }
            }
        }
    }
}

void GameScene::Draw() {
    renderer_->SetCamera(camera_);
#ifdef _DEBUG
    DrawEditorGizmos();
#endif

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
    DrawLightGizmos();
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
        for (auto& eg : edges) renderer_->DrawLine3D(v[eg[0]], v[eg[1]], hlColor, true); // ★ ハイライトはX-Ray

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
            for (auto& eg : edges) renderer_->DrawLine3D(cv[eg[0]], cv[eg[1]], colColor, true); // ★ コライダーもX-Ray
        }

        // ★追加: GPUメッシュコライダーの可視化 (交差時は赤、通常時は青)
        for (const auto& gmc : obj.gpuMeshColliders) {
            if (!gmc.enabled) continue;
            Engine::Vector4 gColor = gmc.isIntersecting ? Engine::Vector4{1.0f, 0.2f, 0.2f, 0.8f} : Engine::Vector4{0.2f, 0.2f, 1.0f, 0.8f};
            // とりあえずAABBだけ描画する (実際のメッシュワイヤーは見づらいため)
            float hs = 1.0f; // 簡易描画用
            Engine::Vector3 cv[8] = {
                {-hs,-hs,-hs},{hs,-hs,-hs},{hs,hs,-hs},{-hs,hs,-hs},
                {-hs,-hs,hs},{hs,-hs,hs},{hs,hs,hs},{-hs,hs,hs},
            };
            for(int i=0; i<8; ++i) {
                DirectX::XMVECTOR p = DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(cv[i].x, cv[i].y, cv[i].z, 1.0f), worldMat);
                DirectX::XMStoreFloat3(reinterpret_cast<DirectX::XMFLOAT3*>(&cv[i]), p);
            }
            for (auto& eg : edges) renderer_->DrawLine3D(cv[eg[0]], cv[eg[1]], gColor, true); 
        }

        // ★追加: Hitbox 可視化 (赤色ワイヤーフレーム)
        for (const auto& hb : obj.hitboxes) {
            if (!hb.enabled) continue;
            float hx = hb.size.x * 0.5f;
            float hy = hb.size.y * 0.5f;
            float hz = hb.size.z * 0.5f;
            Engine::Vector3 cp = {hb.center.x, hb.center.y, hb.center.z};
            Engine::Vector4 hbColor = hb.isActive ? Engine::Vector4{1.0f, 0.2f, 0.2f, 1.0f} : Engine::Vector4{1.0f, 0.2f, 0.2f, 0.3f};
            Engine::Vector3 hv[8] = {
                {cp.x-hx,cp.y-hy,cp.z-hz},{cp.x+hx,cp.y-hy,cp.z-hz},{cp.x+hx,cp.y+hy,cp.z-hz},{cp.x-hx,cp.y+hy,cp.z-hz},
                {cp.x-hx,cp.y-hy,cp.z+hz},{cp.x+hx,cp.y-hy,cp.z+hz},{cp.x+hx,cp.y+hy,cp.z+hz},{cp.x-hx,cp.y+hy,cp.z+hz},
            };
            for(int i=0; i<8; ++i) {
                DirectX::XMVECTOR p = DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(hv[i].x, hv[i].y, hv[i].z, 1.0f), worldMat);
                DirectX::XMStoreFloat3(reinterpret_cast<DirectX::XMFLOAT3*>(&hv[i]), p);
            }
            for (auto& eg : edges) renderer_->DrawLine3D(hv[eg[0]], hv[eg[1]], hbColor, true);
        }

        // ★追加: Hurtbox 可視化 (緑色ワイヤーフレーム)
        for (const auto& hb : obj.hurtboxes) {
            if (!hb.enabled) continue;
            float hx = hb.size.x * 0.5f;
            float hy = hb.size.y * 0.5f;
            float hz = hb.size.z * 0.5f;
            Engine::Vector3 cp = {hb.center.x, hb.center.y, hb.center.z};
            Engine::Vector4 hbColor = {0.2f, 1.0f, 0.5f, 0.6f};
            Engine::Vector3 hv[8] = {
                {cp.x-hx,cp.y-hy,cp.z-hz},{cp.x+hx,cp.y-hy,cp.z-hz},{cp.x+hx,cp.y+hy,cp.z-hz},{cp.x-hx,cp.y+hy,cp.z-hz},
                {cp.x-hx,cp.y-hy,cp.z+hz},{cp.x+hx,cp.y-hy,cp.z+hz},{cp.x+hx,cp.y+hy,cp.z+hz},{cp.x-hx,cp.y+hy,cp.z+hz},
            };
            for(int i=0; i<8; ++i) {
                DirectX::XMVECTOR p = DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(hv[i].x, hv[i].y, hv[i].z, 1.0f), worldMat);
                DirectX::XMStoreFloat3(reinterpret_cast<DirectX::XMFLOAT3*>(&hv[i]), p);
            }
            for (auto& eg : edges) renderer_->DrawLine3D(hv[eg[0]], hv[eg[1]], hbColor, true);
        }

        // ★ ギズモ軸描画 (ローカル座標ベースで回転を適用)
        DirectX::XMMATRIX gizmoMat = DirectX::XMMatrixRotationRollPitchYaw(obj.rotate.x, obj.rotate.y, obj.rotate.z) * DirectX::XMMatrixTranslation(obj.translate.x, obj.translate.y, obj.translate.z);
        auto drawLocalLine = [&](const Engine::Vector3& localP0, const Engine::Vector3& localP1, const Engine::Vector4& col) {
            DirectX::XMVECTOR p0 = DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(localP0.x, localP0.y, localP0.z, 1.0f), gizmoMat);
            DirectX::XMVECTOR p1 = DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(localP1.x, localP1.y, localP1.z, 1.0f), gizmoMat);
            Engine::Vector3 wp0, wp1;
            DirectX::XMStoreFloat3(reinterpret_cast<DirectX::XMFLOAT3*>(&wp0), p0);
            DirectX::XMStoreFloat3(reinterpret_cast<DirectX::XMFLOAT3*>(&wp1), p1);
            renderer_->DrawLine3D(wp0, wp1, col, true); // ★ ギズモ軸はX-Ray
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
}

void GameScene::DrawEditorGizmos() {
    if (!renderer_) return;
    const float gridSize = 100.0f, step = 1.0f; // Unity style large grid
    for (float i = -gridSize; i <= gridSize; i += step) {
        if (std::fabs(i) < 0.01f) continue; // Skip axes
        
        // Major lines every 10 units
        bool isMajor = std::fmod(std::fabs(i), 10.0f) < 0.01f;
        float alpha = isMajor ? 0.35f : 0.15f;
        Engine::Vector4 gc = {0.6f, 0.6f, 0.6f, alpha}; // Gray lines
        
        renderer_->DrawLine3D({-gridSize, 0.0f, i}, {gridSize, 0.0f, i}, gc, false);
        renderer_->DrawLine3D({i, 0.0f, -gridSize}, {i, 0.0f, gridSize}, gc, false);
    }
    
    // Unity Style Main Axes
    renderer_->DrawLine3D({-gridSize, 0.0f, 0.0f}, {gridSize, 0.0f, 0.0f}, {0.8f, 0.2f, 0.2f, 0.7f}, false); // X-Axis (Red)
    renderer_->DrawLine3D({0.0f, 0.0f, -gridSize}, {0.0f, 0.0f, gridSize}, {0.2f, 0.2f, 0.8f, 0.7f}, false); // Z-Axis (Blue)
    
    // 原点の中心ギズモ (X-Ray)
    renderer_->DrawLine3D({0, 0, 0}, {1.5f, 0, 0}, {1.f, 0.2f, 0.2f, 1.f}, true);
    renderer_->DrawLine3D({0, 0, 0}, {0, 1.5f, 0}, {0.2f, 1.f, 0.2f, 1.f}, true);
    renderer_->DrawLine3D({0, 0, 0}, {0, 0, 1.5f}, {0.2f, 0.2f, 1.f, 1.f}, true);
}

void GameScene::DrawEditor() {
#ifdef _DEBUG
    EditorUI::Show(renderer_, this);
    
    // ★追加: スタンドアロンパーティクルエディタのUI
    particleEditor_.DrawUI();
#endif
}

void GameScene::DrawLightGizmos() {
    if (!renderer_) return;

    for (size_t i = 0; i < objects_.size(); ++i) {
        auto& obj = objects_[i];
        Engine::Vector3 pos = {obj.translate.x, obj.translate.y, obj.translate.z};
        Engine::Matrix4x4 mat = obj.GetTransform().ToMatrix();
        // Z axis is forward
        Engine::Vector3 fwd = {mat.m[2][0], mat.m[2][1], mat.m[2][2]};

        bool isSelected = (selectedIndices_.find((int)i) != selectedIndices_.end());
        float alpha = isSelected ? 1.0f : 0.4f;

        for (const auto& dl : obj.directionalLights) {
            if (!dl.enabled) continue;
            Engine::Vector4 col = {1.0f, 0.9f, 0.2f, alpha}; // Yellow
            // 太陽光の方向を示すライン (長さ5)
            renderer_->DrawLine3D(pos, {pos.x + fwd.x * 5.0f, pos.y + fwd.y * 5.0f, pos.z + fwd.z * 5.0f}, col, true);
            // 太陽アイコンの代わりのボックス
            float s = 0.5f;
            renderer_->DrawLine3D({pos.x-s, pos.y, pos.z}, {pos.x+s, pos.y, pos.z}, col, true);
            renderer_->DrawLine3D({pos.x, pos.y-s, pos.z}, {pos.x, pos.y+s, pos.z}, col, true);
        }

        for (const auto& pl : obj.pointLights) {
            if (!pl.enabled) continue;
            Engine::Vector4 col = {0.2f, 0.9f, 0.2f, alpha}; // Green
            float s = 0.5f;
            renderer_->DrawLine3D({pos.x-s, pos.y, pos.z}, {pos.x+s, pos.y, pos.z}, col, true);
            renderer_->DrawLine3D({pos.x, pos.y-s, pos.z}, {pos.x, pos.y+s, pos.z}, col, true);
            renderer_->DrawLine3D({pos.x, pos.y, pos.z-s}, {pos.x, pos.y, pos.z+s}, col, true);
        }

        for (const auto& sl : obj.spotLights) {
            if (!sl.enabled) continue;
            Engine::Vector4 col = {0.2f, 0.8f, 1.0f, alpha}; // Blue
            // 方向を示すライン
            renderer_->DrawLine3D(pos, {pos.x + fwd.x * 5.0f, pos.y + fwd.y * 5.0f, pos.z + fwd.z * 5.0f}, col, true);
            // アイコン代わりのクロス
            float s = 0.5f;
            renderer_->DrawLine3D({pos.x-s, pos.y, pos.z}, {pos.x+s, pos.y, pos.z}, col, true);
            renderer_->DrawLine3D({pos.x, pos.y-s, pos.z}, {pos.x, pos.y+s, pos.z}, col, true);
        }
    }
}

} // namespace Game