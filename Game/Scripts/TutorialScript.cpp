#include "ObjectTypes.h"
#include "TutorialScript.h"
#include "../Systems/UISystem.h"
#include "../../Engine/Input.h"
#include "../../Engine/PathUtils.h"
#include "../../Engine/SceneManager.h"
#include "../../Engine/SceneParameters.h"
#include "../../Engine/WindowDX.h"
#include "Editor/EditorUI.h"
#include "InstallationManager.h"
#include "PhaseTransition.h"
#include "Scenes/GameScene.h"
#include "ScriptEngine.h"
#include "WaveManagement.h"
#include "ResultManagerScript.h"
#include <cfloat>
#include <cmath>
#include <fstream>
#include <vector>
#include <unordered_map>
#include "../../Engine/ThirdParty/nlohmann/json.hpp"

using json = nlohmann::json;
#if defined(USE_IMGUI) && !defined(NDEBUG)
#include <imgui.h>
#endif

namespace Game {

TutorialScript* TutorialScript::instance_ = nullptr;

namespace {
// チュートリアルの各ステップにおけるテキストガイドの内容
// 簡単に文言や行数を変更できます。SPACEキーで次の行に進みます。
const std::unordered_map<TutorialScript::TutorialStep, std::vector<std::string>> kTutorialTexts = {
    { TutorialScript::TutorialStep::Step1_Greeting, {
        "こんにちは！\nこのゲームのチュートリアルへようこそ。",
        "防衛戦の基本を説明します。[SPACE]キーで読み進めてください。",
    }},
    { TutorialScript::TutorialStep::Step2_CoreIntro, {
        "まずはこちら、\n青い『コア』です。敵はこれを破壊しにきます。",
        "コアを守り切ることがあなたの目的です。[SPACE]キーで進みます。"
    }},
    { TutorialScript::TutorialStep::Step3_SpawnerIntro, {
        "次に、あちらに見えるのが\n『敵のスポナー』です。",
        "戦闘フェーズになると、\nここから敵が出現します。[SPACE]キーで進みます。"
    }},
    { TutorialScript::TutorialStep::Step4_PhaseIntro, {
        "このゲームは、\n『準備フェーズ』と『戦闘フェーズ』を\n交互に繰り返します。",
        "準備フェーズで設備を整え、\n戦闘フェーズで敵を迎撃しましょう。[SPACE]キーで進みます。"
    }},
    { TutorialScript::TutorialStep::Step5_CameraControl, {
        "カメラの操作方法について説明します。",
        "WASDで移動、マウス右ドラッグでカメラを回転できます。",
        "操作を確認したら、[SPACE]キーで次へ進みましょう。"
    }},
    { TutorialScript::TutorialStep::Step6_FacilityIntro, {
        "それでは、防衛施設の設置について学びましょう。",
        "画面下のアイコンをクリックするか、数字キーで施設を選択できます。[SPACE]キーで進みます。"
    }},
    { TutorialScript::TutorialStep::Step7_CannonInstall, {
        "まずは敵を迎撃するための『大砲』を設置します。",
        "3キーを押すか、大砲のアイコンをクリックして設置したい場所に配置してください。"
    }},
    { TutorialScript::TutorialStep::Step8_TankInstall, {
        "次に、大砲を動かすためのエネルギー源『タンク』を設置します。",
        "1キーを押すか、タンクのアイコンをクリックして大砲の近くに配置してください。"
    }},
    { TutorialScript::TutorialStep::Step9_PipeInstall, {
        "タンクから大砲にエネルギーを届けるため、『パイプ』を繋ぎましょう。",
        "2キーを押すか、パイプのアイコンをクリックして、タンクの緑色の接続点から大砲に向けてパイプを伸ばして設置してください。"
    }},
    { TutorialScript::TutorialStep::Step10_DeleteIntro, {
        "練習として、もう一つ余分にタンク（1キー）を適当に設置してみましょう。",
        "設置できました！それではXキーを押すか、左下の『削除ボタン』をクリックして、設置したタンクをクリックして削除してください。",
        "削除できました！[SPACE]キーで次へ進みます。"
    }},
    { TutorialScript::TutorialStep::Step11_BattleTransition, {
        "防衛の準備が整いました。いよいよ戦闘フェーズへ移行します。",
        "[SPACE]キーを押して、戦闘を開始しましょう！"
    }},
    { TutorialScript::TutorialStep::Step12_PlayerAttack, {//nextTimerの時間を参照して、数行に分けて表示する
        "戦闘フェーズが始まりました！プレイヤー自身も攻撃が可能です。",
        "マウス左クリックで通常射撃を行い、Eキーで強力なスキル攻撃を放ちます。",
        "敵を倒してみましょう！[SPACE]キーで進みます。"
    }},
    { TutorialScript::TutorialStep::Step13_CombatPlay, {
        "さあ、やってくる敵を大砲とプレイヤーの攻撃で全て撃退しましょう！"
    }},
    { TutorialScript::TutorialStep::Step14_SkillTree, {
        "敵を倒してレベルアップしました！能力を強化しましょう。\nNキーを押してスキルツリーを開いてください。", // Nを押すと次の行に進む
        "スキルツリーが開きました。矢印キーか画面端のボタンでページを切り替えて、好きなスキルを1つ強化してください。",//強化すると進む
        "強化が完了しました！",//Nで閉じると進む
        "これでチュートリアルは終了ですが、ゲームはここからが本番です！"
    }},
    { TutorialScript::TutorialStep::Step15_EndExplanation, {
        "これでチュートリアルの説明は全て終了です！",
        "ここからは自由にゲームを進めることができます。[SPACE]キーで始めましょう！", 
        "では自由に設置などしましょう"
    }},
    { TutorialScript::TutorialStep::Step16_FreePlayBattle, {
        "準備フェーズです。自由に設備を配置したり強化したりできます。[SPACE]キーを押すと戦闘フェーズが始まります。"
    }}
};

// 2x2のグリッドに値をスナップさせる（2の倍数に丸める）
float SnapTo2x2Grid(float value) { return std::floor(value / 2.0f) * 2.0f; }

// マウスカーソルが施設設置用のボタン（UI）の上にあるかどうかを判定する
bool IsPointerOverInstallationButton(GameScene* scene) {
    if (!scene)
        return false;

    auto& registry = scene->GetRegistry();
    auto view = registry.view<UIButtonComponent>();
    for (auto entity : view) {
        const auto& btn = view.get<UIButtonComponent>(entity);
        if (!btn.enabled || !btn.isHovered)
            continue;

        if (registry.all_of<RectTransformComponent>(entity) && !registry.get<RectTransformComponent>(entity).enabled)
            continue;

        if (InstallationManager::IsManagedButton(entity)) {
            return true;
        }
    }

    return false;
}

// 指定された始点から終点までのパイプの配置経路を2x2グリッドに沿って計算し、ポイントのリストを返す
std::vector<Engine::Vector3> BuildPipePathPoints(const Engine::Vector3& start, const Engine::Vector3& end) {
    std::vector<Engine::Vector3> points;

    const int x0 = static_cast<int>(SnapTo2x2Grid(start.x));
    const int z0 = static_cast<int>(SnapTo2x2Grid(start.z));
    const int x1 = static_cast<int>(SnapTo2x2Grid(end.x));
    const int z1 = static_cast<int>(SnapTo2x2Grid(end.z));
    constexpr int kStep = 2;

    const float y = end.y;
    points.push_back({static_cast<float>(x0), y, static_cast<float>(z0)});

    int x = x0;
    int z = z0;
    const int stepX = (x1 > x0) ? kStep : -kStep;
    const int stepZ = (z1 > z0) ? kStep : -kStep;
    const int totalX = std::abs((x1 - x0) / kStep);
    const int totalZ = std::abs((z1 - z0) / kStep);

    int movedX = 0;
    int movedZ = 0;
    while (movedX < totalX || movedZ < totalZ) {
        const bool canMoveX = movedX < totalX;
        const bool canMoveZ = movedZ < totalZ;

        bool moveX = false;
        if (canMoveX && canMoveZ) {
            const int nextXScore = (movedX + 1) * totalZ;
            const int nextZScore = (movedZ + 1) * totalX;
            moveX = nextXScore <= nextZScore;
        } else {
            moveX = canMoveX;
        }

        if (moveX) {
            x += stepX;
            ++movedX;
        } else {
            z += stepZ;
            ++movedZ;
        }

        points.push_back({static_cast<float>(x), y, static_cast<float>(z)});
    }

    return points;
}

// 指定された座標と範囲内にある施設（大砲やタンク）を検索し、エンティティを返す
entt::entity GetFacilityInRange(GameScene* scene, float x, float z, float range = 2.5f) {
    auto& registry = scene->GetRegistry();
    entt::entity found = entt::null;
    registry.view<NameComponent, TransformComponent>().each([&](entt::entity entity, const NameComponent& nc, const TransformComponent& tc) {
        if (nc.name.find("Canon") != std::string::npos || nc.name.find("Cannon") != std::string::npos || nc.name.find("Tank") != std::string::npos) {
            float dx = tc.translate.x - x;
            float dz = tc.translate.z - z;
            if (std::sqrt(dx * dx + dz * dz) <= range) {
                found = entity;
            }
        }
    });
    return found;
}
} // namespace

// チュートリアルスクリプトの初期化処理。フェーズや変数の初期化、スキルツリーの読み込みなどを行う
void TutorialScript::Start(entt::entity entity, GameScene* scene) {
    instance_ = this;
    tutorialStep_ = TutorialStep::Step1_Greeting;
    currentLineIndex_ = 0;
    phaseState_ = PhaseSystemScript::PreparationPhase;
    nextPhaseState_ = PhaseSystemScript::PreparationPhase;
    isPhaseTransitioning_ = false;
    isFadeFinished_ = false;
    isPlacementMode_ = false;
    isPipeSet_ = false;
    hasPipeStartPoint_ = false;
    hasPlacedTank_ = false;
    hasPlacedPipe_ = false;
    hasPlacedCannon_ = false;
    hasOpenedSkillTreeInGuide_ = false;
    preKeyN_ = false;
    stepGuideShown_ = false;
    isSellMode_ = false;

    // サブ状態のリセット
    step10_placedExtraTank_ = false;
    step10_deletedTank_ = false;
    step14_pageSwitched_ = false;
    step14_upgraded_ = false;
    step14_initialSP_ = 0;

    PhaseSystemScript::ForcePhaseState(phaseState_);
    if (auto* renderer = Engine::Renderer::GetInstance()) {
        skillTree_.SetUIContext(renderer, (float)Engine::WindowDX::kW, (float)Engine::WindowDX::kH, 0.0f, 0.0f);
        skillTree_.Start(entity, scene);
        skillTree_.LoadFromJson("Resources/Scenes/skills.json");
    }
    if (scene) {
        ShowStepGuide();

        // 設置開始イベントの購読
        SubscribeString(scene, "StartInstallation", [this](const std::string& dataStr) {
            try {
                json data = json::parse(dataStr);
                selectedObjPath_ = data.value("prefab", "");
                isPipeSet_ = data.value("isPipe", false);
                isPlacementMode_ = true;
                hasPipeStartPoint_ = false;
            } catch (...) {}
        });
    }
}

// チュートリアルの指定されたステップに移行し、必要な初期状態のセットアップやフェーズ切り替えを行う
void TutorialScript::EnterStep(TutorialStep step) {
    tutorialStep_ = step;
    stepGuideShown_ = false;
    isPlacementMode_ = false;
    isPipeSet_ = false;
    hasPipeStartPoint_ = false;
    autoProceedTimer_ = 0.0f;
    currentLineIndex_ = 0;

    if (step == TutorialStep::Step7_CannonInstall) {
        hasPlacedCannon_ = false;
    }
    if (step == TutorialStep::Step8_TankInstall) {
        hasPlacedTank_ = false;
    }
    if (step == TutorialStep::Step9_PipeInstall) {
        hasPlacedPipe_ = false;
    }
    if (step == TutorialStep::Step10_DeleteIntro) {
        step10_placedExtraTank_ = false;
        step10_deletedTank_ = false;
        hasPlacedTank_ = false;
        isSellMode_ = false;
    }
    if (step == TutorialStep::Step14_SkillTree) {
        step14_pageSwitched_ = false;
        step14_upgraded_ = false;
        step14_initialSP_ = skillTree_.GetSkillPoints();
        skillTree_.Close(nullptr);
    }

    if (step == TutorialStep::Step12_PlayerAttack || step == TutorialStep::Step13_CombatPlay || step == TutorialStep::Step16_FreePlayBattle) {
        RequestPhaseChange(PhaseSystemScript::BattlePhase);
    } else {
        RequestPhaseChange(PhaseSystemScript::PreparationPhase);
    }
}

// 現在のステップをログ出力などの形でガイド表示する（ステップごとに1回のみ）
void TutorialScript::ShowStepGuide() {
    if (stepGuideShown_)
        return;

    EditorUI::Log("Tutorial Step: " + std::to_string(static_cast<int>(tutorialStep_)));
    stepGuideShown_ = true;
}

// スキルツリーのUIの表示切り替えや中身の更新処理を行う
void TutorialScript::UpdateSkillTree(entt::entity entity, GameScene* scene, bool& outKeyN) {
    auto* input = Engine::Input::GetInstance();
    if (!input || !scene)
        return;

    auto* renderer = scene->GetRenderer();
    if (!renderer)
        return;

    outKeyN = input->Trigger(DIK_N) || (GetAsyncKeyState('N') & 0x8001);
    if (outKeyN && !preKeyN_) {
        skillTree_.Toggle(scene);
    }

    if (skillTree_.IsOpen()) {
        hasOpenedSkillTreeInGuide_ = true;
        float mx = 0, my = 0;
        float tW = (float)Engine::WindowDX::kW;
        float tH = (float)Engine::WindowDX::kH;

#if defined(USE_IMGUI) && !defined(NDEBUG)
        ImVec2 mousePos = ImGui::GetMousePos();
        ImVec2 gameMin = EditorUI::GetGameImageMin();
        ImVec2 gameMax = EditorUI::GetGameImageMax();
        float viewW = gameMax.x - gameMin.x;
        float viewH = gameMax.y - gameMin.y;
        if (viewW > 0 && viewH > 0) {
            mx = (mousePos.x - gameMin.x) * (tW / viewW);
            my = (mousePos.y - gameMin.y) * (tH / viewH);
        }
#else
        input->GetMousePos(mx, my);
#endif

        skillTree_.SetUIContext(renderer, tW, tH, mx, my);
        skillTree_.Update(entity, scene, 0.0f);
    }
}

// 指定されたエンティティを中心とした床面に3Dのハイライトの円を描画する
void TutorialScript::Draw3DHighlight(GameScene* scene, entt::entity entity, const Engine::Vector4& color, float radius) {
    if (!scene || entity == entt::null || !scene->GetRegistry().valid(entity)) return;
    auto* renderer = scene->GetRenderer();
    if (!renderer) return;
    auto* tc = scene->GetRegistry().try_get<TransformComponent>(entity);
    if (!tc) return;

    DirectX::XMFLOAT3 pos = tc->translate;
    constexpr int kSegments = 36;
    float time = (float)GetTickCount64() / 1000.0f;
    float alpha = 0.5f + 0.5f * std::sin(time * 5.0f);
    Engine::Vector4 c = { color.x, color.y, color.z, color.w * alpha };

    for (int j = 0; j < 3; ++j) {
        float r = radius + j * 0.5f;
        for (int i = 0; i < kSegments; ++i) {
            float theta1 = (i * 2.0f * 3.1415926f) / kSegments;
            float theta2 = ((i + 1) * 2.0f * 3.1415926f) / kSegments;
            Engine::Vector3 p1 = { pos.x + std::cos(theta1) * r, pos.y + 0.1f, pos.z + std::sin(theta1) * r };
            Engine::Vector3 p2 = { pos.x + std::cos(theta2) * r, pos.y + 0.1f, pos.z + std::sin(theta2) * r };
            renderer->DrawLine3D(p1, p2, c, true);
        }
    }
}

// チュートリアルのステップに応じて、コアやスポナーなどの注目させたいオブジェクトにハイライトを描画する
void TutorialScript::DrawHighlights(GameScene* scene) {
    if (!scene) return;
    auto* renderer = scene->GetRenderer();
    if (!renderer) return;

    float time = (float)GetTickCount64() / 1000.0f;

    if (tutorialStep_ == TutorialStep::Step2_CoreIntro) {
        auto core = scene->FindObjectByName("Core");
        if (scene->GetRegistry().valid(core)) {
            Draw3DHighlight(scene, core, { 0.2f, 0.6f, 1.0f, 1.0f }, 4.0f);
        }
    }

    // スポナーのガイド（Step 3 または 戦闘フェーズ中）
    const bool isBattlePhase = (phaseState_ == PhaseSystemScript::BattlePhase);
    if (tutorialStep_ == TutorialStep::Step3_SpawnerIntro || isBattlePhase) {
        auto& registry = scene->GetRegistry();
        registry.view<NameComponent, TransformComponent>().each([&](entt::entity entity, const NameComponent& nc, const TransformComponent& tc) {
            if (nc.name.find("Spawner") != std::string::npos && 
                (!registry.all_of<HierarchyComponent>(entity) || registry.get<HierarchyComponent>(entity).parentId == entt::null)) {

                // Step 3 の時だけ足元をハイライト
                if (tutorialStep_ == TutorialStep::Step3_SpawnerIntro) {
                    Draw3DHighlight(scene, entity, { 1.0f, 0.2f, 0.2f, 1.0f }, 3.0f);
                }

                auto& camera = scene->GetCamera();
                DirectX::XMMATRIX view = camera.View();
                Engine::Vector3 camPos = camera.GetPosition();
                
                float sdx = tc.translate.x - camPos.x;
                float sdz = tc.translate.z - camPos.z;
                float distance = std::sqrt(sdx * sdx + sdz * sdz);

                if (distance >= 30.0f) {
                    DirectX::XMMATRIX invView = DirectX::XMMatrixInverse(nullptr, view);
                    DirectX::XMVECTOR camForwardVec = invView.r[2];
                    DirectX::XMFLOAT3 camForward;
                    DirectX::XMStoreFloat3(&camForward, camForwardVec);

                    float camAngle = std::atan2(camForward.z, camForward.x);
                    float spawnerAngle = std::atan2(sdz, sdx);

                    float angleDiff = spawnerAngle - camAngle;
                    const float PI_VAL = 3.1415926535f;
                    while (angleDiff < -PI_VAL) angleDiff += 2.0f * PI_VAL;
                    while (angleDiff > PI_VAL) angleDiff -= 2.0f * PI_VAL;

                    float margin = (float)Engine::WindowDX::kW * 0.1f;
                    float startX = margin;
                    float endX = (float)Engine::WindowDX::kW - margin;
                    float barWidth = endX - startX;

                    float t = (PI_VAL - angleDiff) / (2.0f * PI_VAL);
                    float targetX = startX + t * barWidth;
                    float targetY = 50.0f;

                    Engine::Renderer::SdfUIDesc desc;
                    desc.centerPx = { targetX, targetY };
                    desc.sizePx = { 70.0f + 10.0f * std::sin(time * 10.0f), 70.0f + 10.0f * std::sin(time * 10.0f) };
                    desc.lineWidth = 4.0f;
                    desc.color = { 1.0f, 0.2f, 0.2f, 1.0f };
                    desc.shape = 1;
                    desc.fill = 0.0f;
                    desc.glow = 1.0f;
                    desc.additive = true;
                    renderer->DrawSDFUI(desc);
                }
            }
        });
    }
}

// 施設の売却（削除）モードの切り替えおよび、削除対象のハイライト表示・クリックによる削除処理を行う
void TutorialScript::UpdateSellMode(GameScene* scene) {
    if (!scene) return;
    auto* input = Engine::Input::GetInstance();
    if (!input) return;
    auto* renderer = scene->GetRenderer();
    if (!renderer) return;

    const bool keyX = input->Trigger(DIK_X) || (GetAsyncKeyState('X') & 0x8001);
    if (keyX || InstallationManager::IsButtonPressedByName("DeleteButton")) {
        isSellMode_ = !isSellMode_;
        isPlacementMode_ = false;
        isPipeSet_ = false;
        hasPipeStartPoint_ = false;
        EditorUI::Log(isSellMode_ ? "Tutorial: Sell Mode Activated" : "Tutorial: Sell Mode Deactivated");
    }

    if (!isSellMode_) return;

    Engine::Vector3 hitPoint{};
    if (TryGetTerrainHitPoint(scene, hitPoint)) {
        float localX = 0, localY = 0;
        float tW = 0, tH = 0;
#if defined(USE_IMGUI) && !defined(NDEBUG)
        ImVec2 mousePos = ImGui::GetMousePos();
        ImVec2 gameMin = EditorUI::GetGameImageMin();
        ImVec2 gameMax = EditorUI::GetGameImageMax();
        tW = gameMax.x - gameMin.x;
        tH = gameMax.y - gameMin.y;
        localX = mousePos.x - gameMin.x;
        localY = mousePos.y - gameMin.y;
#else
        input->GetMousePos(localX, localY);
        tW = (float)Engine::WindowDX::kW;
        tH = (float)Engine::WindowDX::kH;
#endif

        auto& camera = scene->GetCamera();
        DirectX::XMMATRIX view = camera.View();
        DirectX::XMMATRIX proj = camera.Proj();

        DirectX::XMVECTOR rayOrig, rayDir;
        EditorUI::ScreenToWorldRay(localX, localY, tW, tH, view, proj, rayOrig, rayDir);

        float bestDist = FLT_MAX;
        entt::entity hoverEntity = entt::null;

        auto& registry = scene->GetRegistry();
        registry.view<NameComponent, TransformComponent>().each([&](entt::entity entity, const NameComponent& nc, const TransformComponent& tc) {
            if (nc.name.find("Terrain") != std::string::npos ||
                nc.name.find("Floor") != std::string::npos ||
                nc.name.find("Ground") != std::string::npos ||
                nc.name.find("Stage") != std::string::npos ||
                nc.name.find("Plane") != std::string::npos ||
                nc.name.find("Core") != std::string::npos ||
                nc.name.find("Player") != std::string::npos ||
                nc.name.find("PhysicsSystem") != std::string::npos) {
                return;
            }

            Engine::Model* model = nullptr;
            if (registry.all_of<GpuMeshColliderComponent>(entity)) {
                auto& gmc = registry.get<GpuMeshColliderComponent>(entity);
                if (gmc.meshHandle != 0) {
                    model = renderer->GetModel(gmc.meshHandle);
                }
            }
            if (!model && registry.all_of<MeshRendererComponent>(entity)) {
                auto& mr = registry.get<MeshRendererComponent>(entity);
                if (mr.modelHandle != 0) {
                    model = renderer->GetModel(mr.modelHandle);
                }
            }

            if (!model) return;

            float d;
            Engine::Vector3 hp;
            if (model->RayCast(rayOrig, rayDir, tc.ToMatrix(), d, hp) && d < bestDist) {
                bestDist = d;
                hoverEntity = entity;
            }
        });

        if (hoverEntity != entt::null) {
            auto* tc = registry.try_get<TransformComponent>(hoverEntity);
            if (tc) {
                Engine::Vector3 minP = { tc->translate.x - tc->scale.x, tc->translate.y, tc->translate.z - tc->scale.z };
                Engine::Vector3 maxP = { tc->translate.x + tc->scale.x, tc->translate.y + tc->scale.y * 2.0f, tc->translate.z + tc->scale.z };
                
                renderer->DrawLine3D({minP.x, minP.y, minP.z}, {maxP.x, minP.y, minP.z}, {1, 0, 0, 1}, true);
                renderer->DrawLine3D({maxP.x, minP.y, minP.z}, {maxP.x, minP.y, maxP.z}, {1, 0, 0, 1}, true);
                renderer->DrawLine3D({maxP.x, minP.y, maxP.z}, {minP.x, minP.y, maxP.z}, {1, 0, 0, 1}, true);
                renderer->DrawLine3D({minP.x, minP.y, maxP.z}, {minP.x, minP.y, minP.z}, {1, 0, 0, 1}, true);

                renderer->DrawLine3D({minP.x, maxP.y, minP.z}, {maxP.x, maxP.y, minP.z}, {1, 0, 0, 1}, true);
                renderer->DrawLine3D({maxP.x, maxP.y, minP.z}, {maxP.x, maxP.y, maxP.z}, {1, 0, 0, 1}, true);
                renderer->DrawLine3D({maxP.x, maxP.y, maxP.z}, {minP.x, maxP.y, maxP.z}, {1, 0, 0, 1}, true);
                renderer->DrawLine3D({minP.x, maxP.y, maxP.z}, {minP.x, maxP.y, minP.z}, {1, 0, 0, 1}, true);

                renderer->DrawLine3D({minP.x, minP.y, minP.z}, {minP.x, maxP.y, minP.z}, {1, 0, 0, 1}, true);
                renderer->DrawLine3D({maxP.x, minP.y, minP.z}, {maxP.x, maxP.y, minP.z}, {1, 0, 0, 1}, true);
                renderer->DrawLine3D({maxP.x, minP.y, maxP.z}, {maxP.x, maxP.y, maxP.z}, {1, 0, 0, 1}, true);
                renderer->DrawLine3D({minP.x, minP.y, maxP.z}, {minP.x, maxP.y, maxP.z}, {1, 0, 0, 1}, true);
            }

            if (input->IsMouseTrigger(0)) {
                scene->DestroyObject(static_cast<uint32_t>(hoverEntity));
                isSellMode_ = false;
                step10_deletedTank_ = true;
                EditorUI::Log("Tutorial: Object deleted successfully");
            }
        }
    }
}

// 毎フレームの更新処理。チュートリアルのステップ進行判定やキー入力の処理を一括して行う
void TutorialScript::Update(entt::entity entity, GameScene* scene, float dt) {
    auto* input = Engine::Input::GetInstance();
    if (!scene || !input)
        return;

    ShowStepGuide();
    ShowGuideText(entity, scene);
    DrawHighlights(scene);

    const bool key1 = input->Trigger(DIK_1) || (GetAsyncKeyState('1') & 0x8001);
    const bool key2 = input->Trigger(DIK_2) || (GetAsyncKeyState('2') & 0x8001);
    const bool key3 = input->Trigger(DIK_3) || (GetAsyncKeyState('3') & 0x8001);

    static bool prevKeySpace = false;
    const bool currentRawSpace = (GetAsyncKeyState(VK_SPACE) & 0x8000) != 0;
    const bool keySpace = input->Trigger(DIK_SPACE) || (currentRawSpace && !prevKeySpace);
    prevKeySpace = currentRawSpace;

    bool placementSelectionChangedThisFrame = false;
    const bool clickedInstallationButtonThisFrame = input->IsMouseTrigger(0) && IsPointerOverInstallationButton(scene);

    // Keep Core fully healed during Steps 1 to 15 to prevent game over
    if (static_cast<int>(tutorialStep_) <= static_cast<int>(TutorialStep::Step15_EndExplanation)) {
        auto core = scene->FindObjectByName("Core");
        if (scene->GetRegistry().valid(core)) {
            if (auto* hc = scene->GetRegistry().try_get<HealthComponent>(core)) {
                hc->hp = hc->maxHp;
                hc->isDead = false;
            }
        }
    }

     // Text progression by SPACE key
    auto textIt = kTutorialTexts.find(tutorialStep_);
    if (textIt != kTutorialTexts.end()) {
        const auto& lines = textIt->second;
        if (keySpace) {
            if (tutorialStep_ == TutorialStep::Step10_DeleteIntro) {
                if (step10_placedExtraTank_ && step10_deletedTank_ && currentLineIndex_ == 2) {
                    EnterStep(TutorialStep::Step11_BattleTransition);
                }
            }
            else if (tutorialStep_ == TutorialStep::Step12_PlayerAttack) {
                // Step12はSPACEキーに反応しない（自動的に3秒後に進む）
            }
            else if (tutorialStep_ == TutorialStep::Step14_SkillTree) {
                if (step14_upgraded_ && currentLineIndex_ == 3) {
                    EnterStep(TutorialStep::Step15_EndExplanation);
                }
            }
            else {
                if (currentLineIndex_ < static_cast<int>(lines.size()) - 1) {
                    currentLineIndex_++;
                } else {
                    bool isActionStep = (tutorialStep_ == TutorialStep::Step7_CannonInstall ||
                                         tutorialStep_ == TutorialStep::Step8_TankInstall ||
                                         tutorialStep_ == TutorialStep::Step9_PipeInstall ||
                                         tutorialStep_ == TutorialStep::Step10_DeleteIntro ||
                                         tutorialStep_ == TutorialStep::Step13_CombatPlay ||
                                         tutorialStep_ == TutorialStep::Step14_SkillTree ||
                                         tutorialStep_ == TutorialStep::Step16_FreePlayBattle);
                    if (!isActionStep) {
                        int nextStepInt = static_cast<int>(tutorialStep_) + 1;
                        if (nextStepInt < static_cast<int>(TutorialStep::Count)) {
                            EnterStep(static_cast<TutorialStep>(nextStepInt));
                        }
                    }
                }
            }
        }
    }

    switch (tutorialStep_) {
    case TutorialStep::Step7_CannonInstall:
        if (phaseState_ != PhaseSystemScript::PreparationPhase || isPhaseTransitioning_)
            break;

        if (key3 || InstallationManager::IsButtonPressed("Resources/Prefabs/Canon.prefab")) {
            selectedObjPath_ = "Resources/Prefabs/Canon.prefab";
            isPlacementMode_ = true;
            isPipeSet_ = false;
            hasPipeStartPoint_ = false;
            placementSelectionChangedThisFrame = true;
        }

        if (input->IsMouseTrigger(1) && isPlacementMode_) {
            isPlacementMode_ = false;
        }

        if (!placementSelectionChangedThisFrame && !clickedInstallationButtonThisFrame) {
            Installation(scene, selectedObjPath_);
        }

        if (hasPlacedCannon_) {
            EnterStep(TutorialStep::Step8_TankInstall);
        }
        break;

    case TutorialStep::Step8_TankInstall:
        if (phaseState_ != PhaseSystemScript::PreparationPhase || isPhaseTransitioning_)
            break;

        if (key1 || InstallationManager::IsButtonPressed("Resources/Prefabs/BulletTank.prefab")) {
            selectedObjPath_ = "Resources/Prefabs/BulletTank.prefab";
            isPlacementMode_ = true;
            isPipeSet_ = false;
            hasPipeStartPoint_ = false;
            placementSelectionChangedThisFrame = true;
        }

        if (input->IsMouseTrigger(1) && isPlacementMode_) {
            isPlacementMode_ = false;
        }

        if (!placementSelectionChangedThisFrame && !clickedInstallationButtonThisFrame) {
            Installation(scene, selectedObjPath_);
        }

        if (hasPlacedTank_) {
            EnterStep(TutorialStep::Step9_PipeInstall);
        }
        break;

    case TutorialStep::Step9_PipeInstall:
        if (phaseState_ != PhaseSystemScript::PreparationPhase || isPhaseTransitioning_)
            break;

        if (key2 || InstallationManager::IsButtonPressed("Resources/Prefabs/Pipe.prefab")) {
            selectedObjPath_ = "Resources/Prefabs/Pipe.prefab";
            isPipeSet_ = true;
            isPlacementMode_ = true;
            hasPipeStartPoint_ = false;
            placementSelectionChangedThisFrame = true;
        }

        if (input->IsMouseTrigger(1) && isPlacementMode_) {
            if (isPipeSet_ && hasPipeStartPoint_) {
                hasPipeStartPoint_ = false;
            } else {
                isPlacementMode_ = false;
                isPipeSet_ = false;
                hasPipeStartPoint_ = false;
            }
        }

        if (!placementSelectionChangedThisFrame && !clickedInstallationButtonThisFrame) {
            Installation(scene, selectedObjPath_);
        }

        if (hasPlacedPipe_) {
            EnterStep(TutorialStep::Step10_DeleteIntro);
        }
        break;

    case TutorialStep::Step10_DeleteIntro:
        if (phaseState_ != PhaseSystemScript::PreparationPhase || isPhaseTransitioning_)
            break;

        if (!step10_placedExtraTank_) {
            currentLineIndex_ = 0;
            if (key1 || InstallationManager::IsButtonPressed("Resources/Prefabs/BulletTank.prefab")) {
                selectedObjPath_ = "Resources/Prefabs/BulletTank.prefab";
                isPlacementMode_ = true;
                isPipeSet_ = false;
                hasPipeStartPoint_ = false;
                placementSelectionChangedThisFrame = true;
            }
            if (!placementSelectionChangedThisFrame && !clickedInstallationButtonThisFrame) {
                Installation(scene, selectedObjPath_);
            }
            if (hasPlacedTank_) {
                step10_placedExtraTank_ = true;
                hasPlacedTank_ = false;
                isPlacementMode_ = false;
            }
        } else if (!step10_deletedTank_) {
            currentLineIndex_ = 1;
            UpdateSellMode(scene);
        } else {
            currentLineIndex_ = 2;
            isSellMode_ = false;
        }
        break;

    case TutorialStep::Step11_BattleTransition:
        if (keySpace && currentLineIndex_ == static_cast<int>(kTutorialTexts.at(tutorialStep_).size()) - 1) {
            EnterStep(TutorialStep::Step12_PlayerAttack);
        }
        break;

    case TutorialStep::Step12_PlayerAttack: {
        // 3秒ごとに説明文の行を進める
        autoProceedTimer_ += dt;
        if (autoProceedTimer_ >= nextTimer_) {
            autoProceedTimer_ = 0.0f;
            const auto& lines = kTutorialTexts.at(tutorialStep_);
            if (currentLineIndex_ < static_cast<int>(lines.size()) - 1) {
                // 次の行へ進む
                currentLineIndex_++;
            } else {
                // すべての行が終わったのでStep13へ進む
                EnterStep(TutorialStep::Step13_CombatPlay);
            }
        }
        break;
    }

    case TutorialStep::Step13_CombatPlay:
        if (phaseState_ == PhaseSystemScript::BattlePhase) {
            // Check if all enemies are defeated
            auto waveManagerEntity = WaveManagement::GetManagerEntity();
            if (scene->GetRegistry().valid(waveManagerEntity)) {
                if (auto* sc = scene->GetRegistry().try_get<ScriptComponent>(waveManagerEntity)) {
                    for (auto& entry : sc->scripts) {
                        if (entry.scriptPath == "WaveManagement" && entry.instance) {
                            auto* wm = static_cast<WaveManagement*>(entry.instance.get());
                            int remainingEnemies = wm->GetTotalRemainingEnemies(scene);
                            if (remainingEnemies <= 0) {
                                // All enemies defeated, transition to preparation phase
                                EnterStep(TutorialStep::Step14_SkillTree);
                            }
                            break;
                        }
                    }
                }
            }
        }
        break;

    case TutorialStep::Step14_SkillTree: {
        if (phaseState_ != PhaseSystemScript::PreparationPhase || isPhaseTransitioning_)
            break;

        bool keyNTrigger = false;
        UpdateSkillTree(entity, scene, keyNTrigger);

        if (!skillTree_.IsOpen()) {
            // SkillTree が閉じられた場合、スキルが強化されていれば行4に進む
            if (step14_upgraded_) {
                currentLineIndex_ = 3;
            } else {
                currentLineIndex_ = 0;
            }
        } else {
            // SkillTree が開いている
            if (input->IsMouseTrigger(0)) {
                float mx = 0, my = 0;
                input->GetMousePos(mx, my);
#if defined(USE_IMGUI) && !defined(NDEBUG)
                ImVec2 mousePos = ImGui::GetMousePos();
                ImVec2 gameMin = EditorUI::GetGameImageMin();
                ImVec2 gameMax = EditorUI::GetGameImageMax();
                float viewW = gameMax.x - gameMin.x;
                float viewH = gameMax.y - gameMin.y;
                if (viewW > 0 && viewH > 0) {
                    mx = (mousePos.x - gameMin.x) * ((float)Engine::WindowDX::kW / viewW);
                    my = (mousePos.y - gameMin.y) * ((float)Engine::WindowDX::kH / viewH);
                }
#endif
                if ((mx >= 100.0f && mx <= 180.0f && my >= 520.0f && my <= 570.0f) ||
                    (mx >= 1000.0f && mx <= 1080.0f && my >= 520.0f && my <= 570.0f)) {
                    step14_pageSwitched_ = true;
                }
            }

            if (skillTree_.GetSkillPoints() < step14_initialSP_) {
                // スキルを強化した
                step14_upgraded_ = true;
                currentLineIndex_ = 2;
            } else if (keyNTrigger) {
                // Nを押した
                currentLineIndex_ = 1;
            } else {
                currentLineIndex_ = 0;
            }
        }
        preKeyN_ = keyNTrigger;
        break;
    }

    case TutorialStep::Step16_FreePlayBattle:
        if (phaseState_ == PhaseSystemScript::PreparationPhase && !isPhaseTransitioning_) {
            if (key1 || InstallationManager::IsButtonPressed("Resources/Prefabs/BulletTank.prefab")) {
                selectedObjPath_ = "Resources/Prefabs/BulletTank.prefab";
                isPlacementMode_ = true;
                isPipeSet_ = false;
                hasPipeStartPoint_ = false;
                placementSelectionChangedThisFrame = true;
            }
            else if (key2 || InstallationManager::IsButtonPressed("Resources/Prefabs/Pipe.prefab")) {
                selectedObjPath_ = "Resources/Prefabs/Pipe.prefab";
                isPipeSet_ = true;
                isPlacementMode_ = true;
                hasPipeStartPoint_ = false;
                placementSelectionChangedThisFrame = true;
            }
            else if (key3 || InstallationManager::IsButtonPressed("Resources/Prefabs/Canon.prefab")) {
                selectedObjPath_ = "Resources/Prefabs/Canon.prefab";
                isPlacementMode_ = true;
                isPipeSet_ = false;
                hasPipeStartPoint_ = false;
                placementSelectionChangedThisFrame = true;
            }

            if (input->IsMouseTrigger(1) && isPlacementMode_) {
                isPlacementMode_ = false;
            }

            if (!placementSelectionChangedThisFrame && !clickedInstallationButtonThisFrame) {
                Installation(scene, selectedObjPath_);
            }

            if (keySpace) {
                currentLineIndex_ = 0;
                EnterStep(TutorialStep::Step16_FreePlayBattle);
            }
        } else if (phaseState_ == PhaseSystemScript::BattlePhase) {
            if (WaveManagement::IsWaveEnded()) {
                WaveManagement::ResetState();
                WaveManagement::SetWave(0);

                auto waveManagerEntity = WaveManagement::GetManagerEntity();
                if (scene->GetRegistry().valid(waveManagerEntity)) {
                    if (auto* sc = scene->GetRegistry().try_get<ScriptComponent>(waveManagerEntity)) {
                        for (auto& entry : sc->scripts) {
                            if (entry.scriptPath == "WaveManagement" && entry.instance) {
                                auto* wm = static_cast<WaveManagement*>(entry.instance.get());
                                wm->SpawnSpanner(0, scene);
                            }
                        }
                    }
                }
                EditorUI::Log("Tutorial: Infinite loop respawning Wave 0 enemies!");
            }
        }
        break;
    default:
        break;
    }

    UpdatePhaseTransition(scene);
}

// 準備フェーズと戦闘フェーズの間で状態を切り替えるよう要求する（フェード遷移を開始する）
void TutorialScript::RequestPhaseChange(PhaseSystemScript::PhaseState nextPhase) {
    if (phaseState_ == PhaseSystemScript::Transition || isPhaseTransitioning_)
        return;
    if (phaseState_ == nextPhase)
        return;

    nextPhaseState_ = nextPhase;
    phaseState_ = PhaseSystemScript::Transition;
    isPhaseTransitioning_ = true;
    isFadeFinished_ = false;
    PhaseSystemScript::ForcePhaseState(PhaseSystemScript::Transition);

    if (PhaseTransition::IsAvailable()) {
        PhaseTransition::RequestFade();
    }
}

// フェーズ遷移中の処理を更新し、フェード完了タイミングで実際のフェーズ状態切り替えを適用する
void TutorialScript::UpdatePhaseTransition(GameScene* scene) {
    if (!isPhaseTransitioning_)
        return;

    if (PhaseTransition::IsAvailable()) {
        isFadeFinished_ = PhaseTransition::ConsumeSwitchPoint();
    } else {
        isFadeFinished_ = true;
    }

    if (isFadeFinished_) {
        phaseState_ = nextPhaseState_;
        isPhaseTransitioning_ = false;
        isFadeFinished_ = false;
        PhaseSystemScript::ForcePhaseState(phaseState_);

        if (phaseState_ == PhaseSystemScript::BattlePhase && scene) {
            auto& nav = scene->GetNavigationManager();
            nav.UpdateCostMap(scene);

            auto core = scene->FindObjectByName("Core");
            if (scene->GetRegistry().valid(core)) {
                auto& tc = scene->GetRegistry().get<TransformComponent>(core);
                nav.GenerateFlowField(tc.translate.x, tc.translate.z);
            }

            if (tutorialStep_ == TutorialStep::Step13_CombatPlay || tutorialStep_ == TutorialStep::Step16_FreePlayBattle) {
                WaveManagement::SetWave(0);
            }
        } else if (phaseState_ == PhaseSystemScript::PreparationPhase) {
            WaveManagement::SetWave(-1);
        }
    }
}

// 現在のチュートリアルステップに基づくテキストをUIテキストコンポーネントに適用する
void TutorialScript::ShowGuideText(entt::entity entity, GameScene* scene) {
    if (!scene) return;
    auto& registry = scene->GetRegistry();
    if (!registry.all_of<UITextComponent>(entity)) return;

    auto& uiText = registry.get<UITextComponent>(entity);

    auto it = kTutorialTexts.find(tutorialStep_);
    if (it != kTutorialTexts.end()) {
        const auto& lines = it->second;
        if (currentLineIndex_ >= 0 && currentLineIndex_ < static_cast<int>(lines.size())) {
            uiText.text = lines[currentLineIndex_];
        }
    }
}

// 施設やパイプを設置する処理。座標のグリッドスナップや設置条件の判定、プレビュー描画を行う
void TutorialScript::Installation(GameScene* scene, const std::string& objPath) {
    if (!isPlacementMode_)
        return;

    auto* input = Engine::Input::GetInstance();
    if (!input)
        return;

    Engine::Vector3 hitPoint{};
    if (!TryGetTerrainHitPoint(scene, hitPoint))
        return;

    Engine::Vector3 snappedHitPoint = hitPoint;
    // Align all placements in the tutorial to the 2x2 grid to prevent diagonal misalignment between tanks/cannons and pipes
    snappedHitPoint.x = SnapTo2x2Grid(snappedHitPoint.x);
    snappedHitPoint.z = SnapTo2x2Grid(snappedHitPoint.z);

    if (isPipeSet_) {
        snappedHitPoint.x = SnapTo2x2Grid(snappedHitPoint.x);
        snappedHitPoint.z = SnapTo2x2Grid(snappedHitPoint.z);

        if (!hasPipeStartPoint_) {
            const bool withinFacility = (GetFacilityInRange(scene, snappedHitPoint.x, snappedHitPoint.z) != entt::null);
            
            // ★重複排除＆利便性向上: スタート地点の座標は、すでにパイプがあるマスであっても選択できるようにする
            // ただし、大砲やタンクなどの他の施設がある場合はブロックする
            bool isBlockedByOtherThanPipe = false;
            constexpr float kBlockHalfExtent = 2.0f;
            auto& registry = scene->GetRegistry();
            for (auto entity : registry.view<TransformComponent>()) {
                if (!registry.any_of<MeshRendererComponent, BoxColliderComponent, GpuMeshColliderComponent>(entity)) continue;
                if (registry.all_of<TagComponent>(entity) && registry.get<TagComponent>(entity).tag == TagType::Pipe) {
                    continue; // パイプはスタート地点の重ね合わせを許可
                }
                if (registry.all_of<NameComponent>(entity)) {
                    const auto& nc = registry.get<NameComponent>(entity);
                    if ((nc.name.find("Terrain") != std::string::npos) || (nc.name.find("Floor") != std::string::npos) || (nc.name.find("Ground") != std::string::npos) ||
                        (nc.name.find("Stage") != std::string::npos) || (nc.name.find("Plane") != std::string::npos)) {
                        continue; // 地形もスルー
                    }
                }
                if (registry.all_of<TagComponent>(entity) && registry.get<TagComponent>(entity).tag == TagType::Wall) {
                    continue; // 壁もスルー
                }

                const auto& tc = registry.get<TransformComponent>(entity);
                const float dx = tc.translate.x - snappedHitPoint.x;
                const float dz = tc.translate.z - snappedHitPoint.z;
                if (std::abs(dx) < kBlockHalfExtent && std::abs(dz) < kBlockHalfExtent) {
                    isBlockedByOtherThanPipe = true;
                    break;
                }
            }

            const bool canPlaceStart = !isBlockedByOtherThanPipe && withinFacility;
            DrawPlacementPreview(scene, snappedHitPoint, objPath, canPlaceStart);

            if (input->IsMouseTrigger(0) && canPlaceStart) {
                pipeStartX_ = snappedHitPoint.x;
                pipeStartY_ = snappedHitPoint.y;
                pipeStartZ_ = snappedHitPoint.z;
                hasPipeStartPoint_ = true;
            }
            return;
        }

        Engine::Vector3 startPoint{pipeStartX_, pipeStartY_, pipeStartZ_};
        auto pathPoints = BuildPipePathPoints(startPoint, snappedHitPoint);
        bool canPlaceAll = !pathPoints.empty();

        entt::entity startFacility = GetFacilityInRange(scene, pipeStartX_, pipeStartZ_);
        entt::entity endFacility = GetFacilityInRange(scene, snappedHitPoint.x, snappedHitPoint.z);
        bool validFacilityDrop = (endFacility != entt::null && endFacility != startFacility);

        if (!validFacilityDrop) {
            canPlaceAll = false;
        }

        for (const auto& p : pathPoints) {
            const bool canPlacePoint = !IsPlacementBlocked(scene, p);
            DrawPlacementPreview(scene, p, objPath, canPlacePoint && validFacilityDrop);
            if (!canPlacePoint) {
                canPlaceAll = false;
            }
        }

        // パイプ間の接続ラインを描画
        auto* pipeRenderer = scene->GetRenderer();
        if (pipeRenderer && pathPoints.size() >= 2) {
            for (size_t i = 0; i + 1 < pathPoints.size(); ++i) {
                Engine::Vector3 p1 = {pathPoints[i].x, pathPoints[i].y + 0.5f, pathPoints[i].z};
				Engine::Vector3 p2 = {pathPoints[i+1].x, pathPoints[i+1].y + 0.5f, pathPoints[i+1].z};
                Engine::Vector4 lineColor = canPlaceAll ? Engine::Vector4{0.6f, 1.0f, 0.6f, 1.0f} : Engine::Vector4{1.0f, 0.3f, 0.3f, 1.0f};
                pipeRenderer->DrawLine3D(p1, p2, lineColor, true);
            }
        }

        if (input->IsMouseTrigger(0)) {
            if (canPlaceAll) {
                // ★重複配置の排除最適化★
                // 実際に新しく配置するポイント（すでに同座標にパイプがある場所は除外）をフィルタリング
                std::vector<Engine::Vector3> actualPlacePoints;
                actualPlacePoints.reserve(pathPoints.size());

                auto& registry = scene->GetRegistry();
                for (const auto& p : pathPoints) {
                    bool alreadyExists = false;
                    constexpr float kBlockHalfExtent = 1.0f; // 完全重なりを検知するための狭い範囲
                    for (auto entity : registry.view<TransformComponent>()) {
                        if (!registry.any_of<MeshRendererComponent>(entity)) continue;
                        if (registry.all_of<TagComponent>(entity) && registry.get<TagComponent>(entity).tag == TagType::Pipe) {
                            const auto& tc = registry.get<TransformComponent>(entity);
                            const float dx = tc.translate.x - p.x;
                            const float dz = tc.translate.z - p.z;
							if (std::abs(dx) < kBlockHalfExtent && std::abs(dz) < kBlockHalfExtent) {
                                alreadyExists = true;
                                break;
                            }
                        }
                    }
                    if (!alreadyExists) {
                        actualPlacePoints.push_back(p);
                    }
                }

                for (const auto& p : actualPlacePoints) {
                    SpawnPlacedObject(scene, p, objPath);
                }
                isPlacementMode_ = false;
                isPipeSet_ = false;
            }
            hasPipeStartPoint_ = false;
        }
        return;
    }

    const bool canPlace = !IsPlacementBlocked(scene, snappedHitPoint);
    DrawPlacementPreview(scene, snappedHitPoint, objPath, canPlace);

    if (input->IsMouseTrigger(0) && canPlace) {
        SpawnPlacedObject(scene, snappedHitPoint, objPath);
        isPlacementMode_ = false;
    }
}

// マウスクリック位置からレイキャストを行い、地形（Terrain、Floor等）との交点を取得する
bool TutorialScript::TryGetTerrainHitPoint(GameScene* scene, Engine::Vector3& outHitPoint) const {
    float localX = 0, localY = 0;
    float tW = 0, tH = 0;

#if defined(USE_IMGUI) && !defined(NDEBUG)
    ImVec2 mousePos = ImGui::GetMousePos();
    ImVec2 gameMin = EditorUI::GetGameImageMin();
    ImVec2 gameMax = EditorUI::GetGameImageMax();
    tW = gameMax.x - gameMin.x;
    tH = gameMax.y - gameMin.y;
    if (tW <= 0.0f || tH <= 0.0f)
        return false;

    localX = mousePos.x - gameMin.x;
    localY = mousePos.y - gameMin.y;
    bool insideImage = (localX >= 0.0f && localY >= 0.0f && localX <= tW && localY <= tH);
    if (!insideImage)
        return false;
#else
    auto* input = Engine::Input::GetInstance();
    if (!input)
        return false;
    input->GetMousePos(localX, localY);
    tW = (float)Engine::WindowDX::kW;
    tH = (float)Engine::WindowDX::kH;
#endif

    auto& camera = scene->GetCamera();
    DirectX::XMMATRIX view = camera.View();
    DirectX::XMMATRIX proj = camera.Proj();

    DirectX::XMVECTOR rayOrig, rayDir;
    EditorUI::ScreenToWorldRay(localX, localY, tW, tH, view, proj, rayOrig, rayDir);

    auto* renderer = scene->GetRenderer();
    if (!renderer)
        return false;

    float bestDist = FLT_MAX;
    bool hitTerrain = false;

    auto& registry = scene->GetRegistry();
    registry.view<NameComponent, TransformComponent>().each([&](entt::entity entity, const NameComponent& nc, const TransformComponent& tc) {
        bool isTerrain = (nc.name.find("Terrain") != std::string::npos) || (nc.name.find("Floor") != std::string::npos) || (nc.name.find("Ground") != std::string::npos) ||
                         (nc.name.find("Stage") != std::string::npos) || (nc.name.find("Plane") != std::string::npos);
        if (!isTerrain)
            return;

        Engine::Model* model = nullptr;
        if (registry.all_of<GpuMeshColliderComponent>(entity)) {
            auto& gmc = registry.get<GpuMeshColliderComponent>(entity);
            if (gmc.meshHandle != 0) {
                model = renderer->GetModel(gmc.meshHandle);
            }
        }

        if (!model && registry.all_of<MeshRendererComponent>(entity)) {
            auto& mr = registry.get<MeshRendererComponent>(entity);
            if (mr.modelHandle != 0) {
                model = renderer->GetModel(mr.modelHandle);
            }
        }

        if (!model)
            return;

        float d;
        Engine::Vector3 hp;
        if (model->RayCast(rayOrig, rayDir, tc.ToMatrix(), d, hp) && d < bestDist) {
            bestDist = d;
            outHitPoint = hp;
            hitTerrain = true;
        }
    });

    if (!hitTerrain) {
        DirectX::XMFLOAT3 orig, dir;
        DirectX::XMStoreFloat3(&orig, rayOrig);
        DirectX::XMStoreFloat3(&dir, rayDir);

        if (std::abs(dir.y) > 0.0001f) {
            float t = -orig.y / dir.y;
            if (t > 0) {
                outHitPoint = {orig.x + dir.x * t, 0.0f, orig.z + dir.z * t};
                hitTerrain = true;
            }
        }
    }

    return hitTerrain;
}

// 設置プレビューを描画する。パイプの接続可能範囲や大砲の攻撃範囲の可視化も行う
void TutorialScript::DrawPlacementPreview(GameScene* scene, const Engine::Vector3& hitPoint, const std::string& objPath, bool canPlace) {
    auto* renderer = scene->GetRenderer();
    if (!renderer)
        return;

    std::string previewModelPath = objPath;
    std::string previewTexturePath = "Resources/Textures/white1x1.png";
    if (IsPrefabPath(objPath)) {
        ExtractPrefabRenderPaths(objPath, previewModelPath, previewTexturePath);
    }

    if (previewModelHandle_ == 0 || previewObjPath_ != previewModelPath) {
        previewModelHandle_ = renderer->LoadObjMesh(previewModelPath);
        previewObjPath_ = previewModelPath;
        previewTextureHandle_ = 0;
    }
    if (previewTextureHandle_ == 0) {
        previewTextureHandle_ = renderer->LoadTexture2D(previewTexturePath);
    }

    Engine::Transform tr;
    tr.translate = {hitPoint.x, hitPoint.y + 0.5f, hitPoint.z};
    tr.scale = {1.0f, 1.0f, 1.0f};
    const Engine::Vector4 previewColor = canPlace ? Engine::Vector4{0.6f, 1.0f, 0.6f, 0.6f} : Engine::Vector4{1.0f, 0.3f, 0.3f, 0.6f};
    renderer->DrawMesh(previewModelHandle_, previewTextureHandle_, tr, previewColor, "Toon");

    // パイプ設置時のみ、既存のタンク・大砲・ミサイル・ポイズンの接続エリア（緑の平面十字）を描画する
    if (objPath.find("Pipe") != std::string::npos) {
        static uint32_t crossPlaneHandle = 0;
        if (crossPlaneHandle == 0) {
            crossPlaneHandle = renderer->LoadObjMesh("Resources/Models/cube/cube.obj");
        }
        auto& registry = scene->GetRegistry();
        registry.view<NameComponent, TransformComponent>().each([&](entt::entity, const NameComponent& nc, const TransformComponent& tc) {
            if (nc.name.find("Canon") != std::string::npos || nc.name.find("Cannon") != std::string::npos || nc.name.find("Tank") != std::string::npos || nc.name.find("Missile") != std::string::npos || nc.name.find("Poison") != std::string::npos) {
                Engine::Transform planeTr;
                planeTr.scale = { 1.0f, 0.05f, 1.0f };
                Engine::Vector4 colorPlane = { 0.0f, 1.0f, 0.0f, 0.4f };

                // X+ direction
                planeTr.translate = { tc.translate.x + 2.0f, tc.translate.y + 0.05f, tc.translate.z };
                renderer->DrawMesh(crossPlaneHandle, previewTextureHandle_, planeTr, colorPlane, "Toon");
                // X- direction
                planeTr.translate = { tc.translate.x - 2.0f, tc.translate.y + 0.05f, tc.translate.z };
                renderer->DrawMesh(crossPlaneHandle, previewTextureHandle_, planeTr, colorPlane, "Toon");
                // Z+ direction
                planeTr.translate = { tc.translate.x, tc.translate.y + 0.05f, tc.translate.z + 2.0f };
                renderer->DrawMesh(crossPlaneHandle, previewTextureHandle_, planeTr, colorPlane, "Toon");
                // Z- direction
                planeTr.translate = { tc.translate.x, tc.translate.y + 0.05f, tc.translate.z - 2.0f };
                renderer->DrawMesh(crossPlaneHandle, previewTextureHandle_, planeTr, colorPlane, "Toon");
            }
        });
    }

    // 大砲の場合は攻撃範囲も描画する
    if (objPath.find("Canon") != std::string::npos) {
        float attackRange = 50.0f;
        for (int i = 0; i < 72; ++i) {
            float theta1 = (i * 2.0f * 3.1415926f) / 72.0f;
            float theta2 = ((i + 1) * 2.0f * 3.1415926f) / 72.0f;
            Engine::Vector3 p1 = { hitPoint.x + std::cos(theta1) * attackRange, hitPoint.y + 0.05f, hitPoint.z + std::sin(theta1) * attackRange };
            Engine::Vector3 p2 = { hitPoint.x + std::cos(theta2) * attackRange, hitPoint.y + 0.05f, hitPoint.z + std::sin(theta2) * attackRange };
            renderer->DrawLine3D(p1, p2, { 0.0f, 0.8f, 0.0f, 1.0f }, true);
        }
    }
}

// 指定されたパスがプレハブファイル（.prefab）かどうかを判定する
bool TutorialScript::IsPrefabPath(const std::string& path) const {
    if (path.size() < 7)
        return false;
    return path.compare(path.size() - 7, 7, ".prefab") == 0;
}

// プレハブファイルからモデルとテクスチャのパスを抽出する
bool TutorialScript::ExtractPrefabRenderPaths(const std::string& prefabPath, std::string& outModelPath, std::string& outTexturePath) const {
    static std::unordered_map<std::string, std::pair<std::string, std::string>> cache;
    if (cache.find(prefabPath) != cache.end()) {
        outModelPath = cache[prefabPath].first;
        outTexturePath = cache[prefabPath].second;
        return true;
    }

    std::string absPath = EditorUI::GetUnifiedProjectPath(prefabPath);
    std::ifstream f(Engine::PathUtils::FromUTF8(absPath));
    if (!f.is_open()) {
        f.open(Engine::PathUtils::FromUTF8(prefabPath));
        if (!f.is_open())
            return false;
    }

    std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    f.close();

    auto extractValue = [&](const char* key, std::string& outValue) {
        size_t keyPos = content.find(key);
        if (keyPos == std::string::npos)
            return;
        size_t colonPos = content.find(':', keyPos);
        if (colonPos == std::string::npos)
            return;
        size_t firstQuote = content.find('"', colonPos);
        if (firstQuote == std::string::npos)
            return;
        size_t secondQuote = content.find('"', firstQuote + 1);
        if (secondQuote == std::string::npos)
            return;
        outValue = content.substr(firstQuote + 1, secondQuote - firstQuote - 1);
    };

    extractValue("\"modelPath\"", outModelPath);
    extractValue("\"texturePath\"", outTexturePath);

    if (!outModelPath.empty()) {
        cache[prefabPath] = {outModelPath, outTexturePath};
        return true;
    }
    return false;
}

// 指定した位置に既に他のオブジェクトが存在し、設置がブロックされるかどうかを判定する
bool TutorialScript::IsPlacementBlocked(GameScene* scene, const Engine::Vector3& hitPoint) const {
    constexpr float kBlockHalfExtent = 2.0f;

    auto& registry = scene->GetRegistry();
    auto view = registry.view<TransformComponent>();
    for (auto entity : view) {
        if (!registry.any_of<MeshRendererComponent, BoxColliderComponent, GpuMeshColliderComponent>(entity)) {
            continue;
        }

        if (registry.all_of<NameComponent>(entity)) {
            const auto& nc = registry.get<NameComponent>(entity);
            const bool isTerrain = (nc.name.find("Terrain") != std::string::npos) || (nc.name.find("Floor") != std::string::npos) || (nc.name.find("Ground") != std::string::npos) ||
                                   (nc.name.find("Stage") != std::string::npos) || (nc.name.find("Plane") != std::string::npos);
            if (isTerrain)
                continue;
        }

        const auto& tc = view.get<TransformComponent>(entity);
        const float dx = tc.translate.x - hitPoint.x;
        const float dz = tc.translate.z - hitPoint.z;
        if (std::abs(dx) < kBlockHalfExtent && std::abs(dz) < kBlockHalfExtent) {
            return true;
        }
    }

    return false;
}

// オブジェクト（プレハブまたはモデル）を実際にワールドに生成し、初期位置を設定する
void TutorialScript::SpawnPlacedObject(GameScene* scene, const Engine::Vector3& hitPoint, const std::string& objPath) {
    auto* renderer = scene->GetRenderer();
    if (!renderer)
        return;

    auto& registry = scene->GetRegistry();

    if (IsPrefabPath(objPath)) {
        std::vector<entt::entity> createdEntities = EditorUI::LoadPrefab(scene, objPath);
        if (createdEntities.empty()) {
            return;
        }

        for (auto entity : createdEntities) {
            if (registry.all_of<TransformComponent>(entity)) {
                auto& tc = registry.get<TransformComponent>(entity);
                if (!registry.all_of<HierarchyComponent>(entity) || registry.get<HierarchyComponent>(entity).parentId == entt::null) {
                    tc.translate = {hitPoint.x, hitPoint.y + 0.5f, hitPoint.z};
                }
            }
        }

        if (objPath.find("BulletTank") != std::string::npos) hasPlacedTank_ = true;
        if (objPath.find("Pipe") != std::string::npos) hasPlacedPipe_ = true;
        if (objPath.find("Canon") != std::string::npos) hasPlacedCannon_ = true;

        return;
    }

    if (previewModelHandle_ == 0 || previewObjPath_ != objPath) {
        previewModelHandle_ = renderer->LoadObjMesh(objPath);
        previewObjPath_ = objPath;
    }
    if (previewTextureHandle_ == 0) {
        previewTextureHandle_ = renderer->LoadTexture2D("Resources/Textures/white1x1.png");
    }

    entt::entity newEntity = scene->CreateEntity((objPath.find("cylinder") != std::string::npos || objPath.find("Cylinder") != std::string::npos) ? "PlacedCylinder" : "PlacedCube");

    auto& tc = registry.get<TransformComponent>(newEntity);
    tc.translate = {hitPoint.x, hitPoint.y + 0.5f, hitPoint.z};
    tc.scale = {1.0f, 1.0f, 1.0f};

    auto& mr = registry.emplace<MeshRendererComponent>(newEntity);
    mr.modelHandle = previewModelHandle_;
    mr.textureHandle = previewTextureHandle_;
    mr.modelPath = objPath;
    mr.texturePath = "Resources/Textures/white1x1.png";
    mr.shaderName = "Toon";

    if (objPath.find("BulletTank") != std::string::npos) hasPlacedTank_ = true;
    if (objPath.find("Pipe") != std::string::npos) hasPlacedPipe_ = true;
    if (objPath.find("Canon") != std::string::npos) hasPlacedCannon_ = true;
}

// スクリプト破棄時の処理。スキルツリーを閉じ、フェーズを初期状態にリセットする
void TutorialScript::OnDestroy(entt::entity /*entity*/, GameScene* scene) {
    if (instance_ == this) instance_ = nullptr;
    skillTree_.Close(scene);
    PhaseSystemScript::ForcePhaseState(PhaseSystemScript::PreparationPhase);
}

REGISTER_SCRIPT(TutorialScript);

} // namespace Game
