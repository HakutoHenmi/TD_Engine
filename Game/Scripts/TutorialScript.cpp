#include "ObjectTypes.h"
#include "TutorialScript.h"
#include "../../Engine/Input.h"
#include "../../Engine/PathUtils.h"
#include "../../Engine/SceneManager.h"
#include "../../Engine/SceneParameters.h"
#include "../../Engine/WindowDX.h"
#include "Editor/EditorUI.h"
#include "InstallationButton.h"
#include "PhaseTransition.h"
#include "Scenes/GameScene.h"
#include "ScriptEngine.h"
#include "WaveManagement.h"
#include <cfloat>
#include <cmath>
#include <fstream>
#include <vector>
#include <unordered_map>
#if defined(USE_IMGUI) && !defined(NDEBUG)
#include <imgui.h>
#endif

namespace Game {

namespace {
// 2x2のグリッドに値をスナップさせる（2の倍数に丸める）
float SnapTo2x2Grid(float value) { return std::floor(value / 2.0f) * 2.0f; }

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
    tutorialStep_ = TutorialStep::Preparation1;
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
    PhaseSystemScript::ForcePhaseState(phaseState_);
    if (auto* renderer = Engine::Renderer::GetInstance()) {
        skillTree_.SetUIContext(renderer, (float)Engine::WindowDX::kW, (float)Engine::WindowDX::kH, 0.0f, 0.0f);
        skillTree_.Start(entity, scene);
        skillTree_.LoadFromJson("Resources/Scenes/skills.json");
    }
    if (scene) {
        ShowStepGuide();
    }
}

// チュートリアルの指定されたステップへ移行し、必要な状態やフラグをリセット・設定する
void TutorialScript::EnterStep(TutorialStep step) {
    tutorialStep_ = step;
    stepGuideShown_ = false;
    isPlacementMode_ = false;
    isPipeSet_ = false;
    hasPipeStartPoint_ = false;
    autoProceedTimer_ = 0.0f;

    if (step == TutorialStep::InstallCannonGuide1 || step == TutorialStep::InstallCannonGuide2 || step == TutorialStep::InstallCannonGuide3) {
        hasPlacedTank_ = false;
        hasPlacedPipe_ = false;
        hasPlacedCannon_ = false;
    }

    if (step == TutorialStep::SkillTreeGuide1 || step == TutorialStep::SkillTreeGuide2 || step == TutorialStep::SkillTreeGuide3) {
        hasOpenedSkillTreeInGuide_ = false;
        skillTree_.Close();
    }

    if (step == TutorialStep::Preparation1 || step == TutorialStep::Preparation2 || step == TutorialStep::Preparation3 || 
        step == TutorialStep::InstallCannonGuide1 || step == TutorialStep::InstallCannonGuide2 || step == TutorialStep::InstallCannonGuide3 || 
        step == TutorialStep::InstallTankGuide1 || step == TutorialStep::InstallTankGuide2 || step == TutorialStep::InstallTankGuide3 || 
        step == TutorialStep::InstallPipeGuide1 || step == TutorialStep::InstallPipeGuide2 || step == TutorialStep::InstallPipeGuide3 || 
        step == TutorialStep::SkillTreeGuide1 || step == TutorialStep::SkillTreeGuide2 || step == TutorialStep::SkillTreeGuide3) {
        RequestPhaseChange(PhaseSystemScript::PreparationPhase);
    } else {
        RequestPhaseChange(PhaseSystemScript::BattlePhase);
    }
}

// 現在のチュートリアルステップに応じたガイドテキストをUIに表示する
void TutorialScript::ShowStepGuide() {
    if (stepGuideShown_)
        return;

    switch (tutorialStep_) {
    case TutorialStep::Preparation1:
    case TutorialStep::Preparation2:
    case TutorialStep::Preparation3:
        EditorUI::Log("Tutorial: 準備フェーズです。Spaceで次の説明へ進みます。");
        break;
    case TutorialStep::InstallCannonGuide1:
    case TutorialStep::InstallCannonGuide2:
    case TutorialStep::InstallCannonGuide3:
        EditorUI::Log("Tutorial: 大砲の設置説明。大砲を1つ設置してください。(3キー)");
        break;
    case TutorialStep::InstallTankGuide1:
    case TutorialStep::InstallTankGuide2:
    case TutorialStep::InstallTankGuide3:
        EditorUI::Log("Tutorial: タンクの設置説明。弾丸タンクを1つ設置してください。(1キー)");
        break;
    case TutorialStep::InstallPipeGuide1:
    case TutorialStep::InstallPipeGuide2:
    case TutorialStep::InstallPipeGuide3:
        EditorUI::Log("Tutorial: パイプの設置説明。タンクの緑の位置から大砲へパイプを繋いでください。(2キー)");
        break;
    case TutorialStep::FirstBattle1:
    case TutorialStep::FirstBattle2:
    case TutorialStep::FirstBattle3:
        EditorUI::Log("Tutorial: 戦闘フェーズです。ウェーブ終了後にスキルツリー説明へ進みます。(Pキーでも進行可)");
        break;
    case TutorialStep::SkillTreeGuide1:
    case TutorialStep::SkillTreeGuide2:
    case TutorialStep::SkillTreeGuide3:
        EditorUI::Log("Tutorial: スキルツリー説明。Nで開いて確認後、Spaceで最終戦闘へ進みます。");
        break;
	case TutorialStep::Finish:
        EditorUI::Log("Tutorial: 最終");
        break;
    }

    stepGuideShown_ = true;
}

// スキルツリーのUIの表示トグルや、開いている場合の入力およびマウス状態のアップデート処理を行う
void TutorialScript::UpdateSkillTree(entt::entity entity, GameScene* scene, bool& outKeyN) {
    auto* input = Engine::Input::GetInstance();
    if (!input || !scene)
        return;

    auto* renderer = scene->GetRenderer();
    if (!renderer)
        return;

    outKeyN = input->Trigger(DIK_N) || (GetAsyncKeyState('N') & 0x8001);
    if (outKeyN && !preKeyN_) {
        skillTree_.Toggle();
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
#endif

        skillTree_.SetUIContext(renderer, tW, tH, mx, my);
        skillTree_.Update(entity, scene, 0.0f);
    }
}

// 毎フレーム呼ばれる更新処理。入力の監視、チュートリアルステップの進行チェック、オブジェクトの設置モード制御などを行う
void TutorialScript::Update(entt::entity entity, GameScene* scene, float dt) {
    auto* input = Engine::Input::GetInstance();
    if (!scene || !input)
        return;

    ShowStepGuide();
	ShowGuideText(entity, scene);

    const bool key1 = input->Trigger(DIK_1) || (GetAsyncKeyState('1') & 0x8001);
    const bool key2 = input->Trigger(DIK_2) || (GetAsyncKeyState('2') & 0x8001);
    const bool key3 = input->Trigger(DIK_3) || (GetAsyncKeyState('3') & 0x8001);
    const bool keyP = input->Trigger(DIK_P) || (GetAsyncKeyState('P') & 0x8001);
    static bool prevKeySpace = false;
    const bool currentRawSpace = (GetAsyncKeyState(VK_SPACE) & 0x8000) != 0;
    const bool keySpace = input->Trigger(DIK_SPACE) || (currentRawSpace && !prevKeySpace);
    prevKeySpace = currentRawSpace;

    if ((tutorialStep_ == TutorialStep::FirstBattle1 || tutorialStep_ == TutorialStep::FirstBattle2 || tutorialStep_ == TutorialStep::FirstBattle3) && 
        phaseState_ == PhaseSystemScript::BattlePhase && PhaseSystemScript::GetRequestedPhase() == PhaseSystemScript::PreparationPhase) {
        EnterStep(TutorialStep::SkillTreeGuide1);
    }

    switch (tutorialStep_) {
    case TutorialStep::Preparation1:
        if (keySpace) EnterStep(TutorialStep::Preparation2);
        break;
    case TutorialStep::Preparation2:
        if (keySpace) EnterStep(TutorialStep::Preparation3);
        break;
    case TutorialStep::Preparation3:
        if (keySpace) EnterStep(TutorialStep::InstallCannonGuide1);
        break;

    case TutorialStep::InstallCannonGuide1:
        if (keySpace) EnterStep(TutorialStep::InstallCannonGuide2);
        break;
    case TutorialStep::InstallCannonGuide2:
        if (keySpace) EnterStep(TutorialStep::InstallCannonGuide3);
        break;
    case TutorialStep::InstallCannonGuide3:
        if (phaseState_ != PhaseSystemScript::PreparationPhase || isPhaseTransitioning_)
            break;

        if (key3 || InstallationButton::IsButtonPressed(InstallationButton::Cannon)) {
            selectedObjPath_ = "Resources/Prefabs/Canon.prefab";
            isPlacementMode_ = true;
            isPipeSet_ = false;
            hasPipeStartPoint_ = false;
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

        Installation(scene, selectedObjPath_);
        if (hasPlacedCannon_) {
            EnterStep(TutorialStep::InstallTankGuide1);
        }
        break;

    case TutorialStep::InstallTankGuide1:
        if (keySpace) EnterStep(TutorialStep::InstallTankGuide2);
        break;
    case TutorialStep::InstallTankGuide2:
        if (keySpace) EnterStep(TutorialStep::InstallTankGuide3);
        break;
    case TutorialStep::InstallTankGuide3:
        if (phaseState_ != PhaseSystemScript::PreparationPhase || isPhaseTransitioning_)
            break;

        if (key1 || InstallationButton::IsButtonPressed(InstallationButton::Tank)) {
            selectedObjPath_ = "Resources/Prefabs/BulletTank.prefab";
            isPlacementMode_ = true;
            isPipeSet_ = false;
            hasPipeStartPoint_ = false;
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

        Installation(scene, selectedObjPath_);
        if (hasPlacedTank_) {
            EnterStep(TutorialStep::InstallPipeGuide1);
        }
        break;

    case TutorialStep::InstallPipeGuide1:
        if (keySpace) EnterStep(TutorialStep::InstallPipeGuide2);
        break;
    case TutorialStep::InstallPipeGuide2:
        if (keySpace) EnterStep(TutorialStep::InstallPipeGuide3);
        break;
    case TutorialStep::InstallPipeGuide3:
        if (phaseState_ != PhaseSystemScript::PreparationPhase || isPhaseTransitioning_)
            break;

        if (key2 || InstallationButton::IsButtonPressed(InstallationButton::Pipe)) {
            selectedObjPath_ = "Resources/Prefabs/Pipe.prefab";
            isPipeSet_ = true;
            isPlacementMode_ = true;
            hasPipeStartPoint_ = false;
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

        Installation(scene, selectedObjPath_);
        if (hasPlacedPipe_) {
            if (keySpace) {
                EnterStep(TutorialStep::FirstBattle1);
            }
        }
        break;

    case TutorialStep::FirstBattle1:
        autoProceedTimer_ += dt;
        if (autoProceedTimer_ >= 2.0f) EnterStep(TutorialStep::FirstBattle2);
        break;
    case TutorialStep::FirstBattle2:
        autoProceedTimer_ += dt;
        if (autoProceedTimer_ >= 2.0f) EnterStep(TutorialStep::FirstBattle3);
        break;
    case TutorialStep::FirstBattle3:
        if (keyP) {
            EnterStep(TutorialStep::SkillTreeGuide1);
        }
        break;

    case TutorialStep::SkillTreeGuide1: {
        if (keySpace) EnterStep(TutorialStep::SkillTreeGuide2);
        break;
    }
    case TutorialStep::SkillTreeGuide2: {
        if (keySpace) EnterStep(TutorialStep::SkillTreeGuide3);
        break;
    }
    case TutorialStep::SkillTreeGuide3: {
        if (phaseState_ != PhaseSystemScript::PreparationPhase || isPhaseTransitioning_)
            break;

        bool keyN = false;
        UpdateSkillTree(entity, scene, keyN);
        if (hasOpenedSkillTreeInGuide_ && keySpace) {
            skillTree_.Close();
			EnterStep(TutorialStep::Finish);
        }
        preKeyN_ = keyN;
        break;
    }

    case TutorialStep::Finish:
        skillTree_.Close();
        if (scene) {
			// 次のシーン（リザルト画面）に引き継ぐためのパラメータを作成します
            Engine::SceneParameters res;
            res.isWin = true;
            res.score = 99999;
            res.clearTime = 0.0f;
			// シーンマネージャーを使って、パラメータと共に「Result」シーンへの切り替えを要求します
            Engine::SceneManager::GetInstance()->RequestChange("Result", res);
        }
        break;
    }

    if (tutorialStep_ != TutorialStep::SkillTreeGuide1 && tutorialStep_ != TutorialStep::SkillTreeGuide2 && tutorialStep_ != TutorialStep::SkillTreeGuide3) {
        preKeyN_ = false;
    }

    UpdatePhaseTransition(scene);
}

// フェーズ変更（準備フェーズ・戦闘フェーズなど）のリクエストを出し、フェード演出の準備を行う
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

// フェーズ切り替えのトランジション（フェード等）の更新処理と、切り替え完了後のウェーブ設定や経路探索マップ更新等の処理を行う
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

            if (tutorialStep_ == TutorialStep::FirstBattle1 || tutorialStep_ == TutorialStep::FirstBattle2 || tutorialStep_ == TutorialStep::FirstBattle3) {
                WaveManagement::SetWave(0);
			} else if (tutorialStep_ == TutorialStep::Finish) {
            }
        } else if (phaseState_ == PhaseSystemScript::PreparationPhase) {
            WaveManagement::SetWave(-1);
        }
    }
}

void TutorialScript::ShowGuideText(entt::entity entity, GameScene* scene) {

	switch (tutorialStep_) {
	case TutorialStep::Preparation1:
		if (scene->GetRegistry().all_of<UITextComponent>(entity))
			scene->GetRegistry().get<UITextComponent>(entity).text = "チュートリアルです。\nSPACE:次へ";
		break;
	case TutorialStep::Preparation2:
		if (scene->GetRegistry().all_of<UITextComponent>(entity))
			scene->GetRegistry().get<UITextComponent>(entity).text = "ゲームは準備フェーズと戦闘フェーズで構成されています。\nSPACE:次へ";
		break;
	case TutorialStep::Preparation3:
		if (scene->GetRegistry().all_of<UITextComponent>(entity))
			scene->GetRegistry().get<UITextComponent>(entity).text = "準備ができたら、防衛の準備を始めましょう。\nSPACE:次へ";
		break;

	case TutorialStep::InstallCannonGuide1:
		if (scene->GetRegistry().all_of<UITextComponent>(entity))
			scene->GetRegistry().get<UITextComponent>(entity).text = "設置について！\nSPACE:次へ";
		break;
	case TutorialStep::InstallCannonGuide2:
		if (scene->GetRegistry().all_of<UITextComponent>(entity))
			scene->GetRegistry().get<UITextComponent>(entity).text = "大砲を設置しましょう。\nSPACE:次へ";
		break;
	case TutorialStep::InstallCannonGuide3:
		if (scene->GetRegistry().all_of<UITextComponent>(entity))
			scene->GetRegistry().get<UITextComponent>(entity).text = "アイコンをタップして設置したい場所に置こう\n";
		break;

	case TutorialStep::InstallTankGuide1:
		if (scene->GetRegistry().all_of<UITextComponent>(entity))
			scene->GetRegistry().get<UITextComponent>(entity).text = "大砲には動力源が必要！\nSPACE:次へ";
		break;
	case TutorialStep::InstallTankGuide2:
		if (scene->GetRegistry().all_of<UITextComponent>(entity))
			scene->GetRegistry().get<UITextComponent>(entity).text = "弾丸タンクを設置してエネルギーを供給します。\nSPACE:次へ";
		break;
	case TutorialStep::InstallTankGuide3:
		if (scene->GetRegistry().all_of<UITextComponent>(entity))
			scene->GetRegistry().get<UITextComponent>(entity).text = "動力を設置しよう！\n";
		break;

	case TutorialStep::InstallPipeGuide1:
		if (scene->GetRegistry().all_of<UITextComponent>(entity))
			scene->GetRegistry().get<UITextComponent>(entity).text = "タンクと大砲を繋ぐ必要があります。\nSPACE:次へ";
		break;
	case TutorialStep::InstallPipeGuide2:
		if (scene->GetRegistry().all_of<UITextComponent>(entity))
			scene->GetRegistry().get<UITextComponent>(entity).text = "動力を送るために\nSPACE:次へ";
		break;
	case TutorialStep::InstallPipeGuide3:
		if (scene->GetRegistry().all_of<UITextComponent>(entity))
			scene->GetRegistry().get<UITextComponent>(entity).text = "パイプを設置しよう！\n（タンクの緑の位置から大砲へ）";
		break;

	case TutorialStep::FirstBattle1:
		if (scene->GetRegistry().all_of<UITextComponent>(entity))
			scene->GetRegistry().get<UITextComponent>(entity).text = "いよいよ敵がやってきます。";
		break;
	case TutorialStep::FirstBattle2:
		if (scene->GetRegistry().all_of<UITextComponent>(entity))
			scene->GetRegistry().get<UITextComponent>(entity).text = "後ろの青いタワーを敵から守ろう。";
		break;
	case TutorialStep::FirstBattle3:
		if (scene->GetRegistry().all_of<UITextComponent>(entity))
			scene->GetRegistry().get<UITextComponent>(entity).text = "敵を倒すと経験値とお金が落ちます。";
		break;
    case TutorialStep::SkillTreeGuide1:
		if (scene->GetRegistry().all_of<UITextComponent>(entity))
			scene->GetRegistry().get<UITextComponent>(entity).text = "戦闘で得た経験値を使って強くなろう。\nSPACE:次へ";
		break;
    case TutorialStep::SkillTreeGuide2:
		if (scene->GetRegistry().all_of<UITextComponent>(entity))
			scene->GetRegistry().get<UITextComponent>(entity).text = "スキルツリーを開いて強化を行います。\nSPACE:次へ";
		break;
    case TutorialStep::SkillTreeGuide3:
		if (scene->GetRegistry().all_of<UITextComponent>(entity))
			scene->GetRegistry().get<UITextComponent>(entity).text = "Nキーでスキルツリーを開いてね！\nSPACEを押したらチュートリアル終了。";
		break;
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
            const bool canPlaceStart = !IsPlacementBlocked(scene, snappedHitPoint) && withinFacility;
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

        if (input->IsMouseTrigger(0)) {
            if (canPlaceAll) {
                for (const auto& p : pathPoints) {
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

    // パイプ設置時のみ、既存のタンク・大砲の接続エリア（緑の平面十字）を描画する
    if (objPath.find("Pipe") != std::string::npos) {
        static uint32_t crossPlaneHandle = 0;
        if (crossPlaneHandle == 0) {
            crossPlaneHandle = renderer->LoadObjMesh("Resources/Models/cube/cube.obj");
        }
        auto& registry = scene->GetRegistry();
        registry.view<NameComponent, TransformComponent>().each([&](entt::entity, const NameComponent& nc, const TransformComponent& tc) {
            if (nc.name.find("Canon") != std::string::npos || nc.name.find("Cannon") != std::string::npos || nc.name.find("Tank") != std::string::npos) {
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
void TutorialScript::OnDestroy(entt::entity /*entity*/, GameScene* /*scene*/) {
    skillTree_.Close();
    PhaseSystemScript::ForcePhaseState(PhaseSystemScript::PreparationPhase);
}

REGISTER_SCRIPT(TutorialScript);

} // namespace Game
