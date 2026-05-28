#include "TutorialScript.h"
#include "../../Engine/Input.h"
#include "../../Engine/PathUtils.h"
#include "../../Engine/SceneManager.h"
#include "../../Engine/SceneParameters.h"
#include "../../Engine/ThirdParty/nlohmann/json.hpp"
#include "../../Engine/WindowDX.h"
#include "../Systems/UISystem.h"
#include "Editor/EditorUI.h"
#include "InstallationManager.h"
#include "ObjectTypes.h"
#include "PhaseTransition.h"
#include "PlayerScript.h"
#include "ResultManagerScript.h"
#include "Scenes/GameScene.h"
#include "ScriptEngine.h"
#include "WaveManagement.h"
#include <cfloat>
#include <cmath>
#include <fstream>
#include <unordered_map>
#include <vector>

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
    {TutorialScript::TutorialStep::Step1_Greeting,         {"こんにちは！チュートリアルへようこそ！", "このゲームは[RED]『準備フェーズ』[/RED]と[RED]『戦闘フェーズ』[/RED]を\n交互に繰り返すぞ。"}},
    {TutorialScript::TutorialStep::Step2_CoreIntro,        {"まずはあそこにある青い[RED]『コア』[/RED]だ。", "敵はこれを破壊しにくる。コアを守り切るのが君の目的だ！"}                             },
    {TutorialScript::TutorialStep::Step3_SpawnerIntro,     {"次にあちらに見えるのが[RED]『敵のスポナー』[/RED]だ。", "戦闘フェーズになると、ここから敵が出現するぞ。"}                             },
    {TutorialScript::TutorialStep::Step4_PhaseIntro,       {"現在は[RED]『準備フェーズ』[/RED]だ。", "このフェーズで防衛設備を整えよう！"}                                                         },
    {TutorialScript::TutorialStep::Step5_CameraControl,
     {"まずはカメラの操作方法だ。", "[RED]WASDキー[/RED]で移動、[RED]マウス右ドラッグ[/RED]でカメラを回転できるぞ。", "カメラを自由に動かして、マップ全体を確認しよう。"}                          },
    {TutorialScript::TutorialStep::Step6_CannonInstall,
     {"よし！それでは、敵を迎撃するための[RED]『大砲』[/RED]を設置しよう。", "画面下の大砲アイコンをクリックして選択し、\nマップ上に配置するんだ！", "目標：[RED]大砲を3個設置[/RED] (0/3)"}       },
    {TutorialScript::TutorialStep::Step7_DeleteIntro,
     {"大砲が設置できたな！", "間違えて置いた時のために、削除の方法も覚えよう。", "左下の[RED]『削除ボタン』[/RED]をクリックして、\n設置した大砲をどれか1つクリックして削除だ！"}                  },
    {TutorialScript::TutorialStep::Step8_BattleTransition, {"防衛の準備が整ったな。", "いよいよ戦闘開始だ！"}                                                                                      },
    {TutorialScript::TutorialStep::Step9_BuffExplanation,
     {"戦闘フェーズに入った！\nまずはプレイヤーの攻撃操作だ。", "プレイヤーは[RED]左クリック[/RED]で通常攻撃、\n[RED]Eキー[/RED]で強力なスキルが使えるぞ！",
      "まずはやってくる敵を攻撃して、\n敵の[RED]シールドを2体分[/RED]割ってみよう！"}                                                                                                              },
    {TutorialScript::TutorialStep::Step10_BuffPractice,
     {"よくやった！次は防衛で[RED]一番大事なこと[/RED]を教えよう。", "プレイヤーが持つ『青いオーラ』についてだ。",
      "プレイヤーの周囲のオーラの中に大砲を入れると、\nエネルギーが供給され[RED]攻撃力が大幅に上昇[/RED]する！", "実際にWASDで移動し、大砲に近づいてオーラを繋いでみてくれ。"}                     },
    {TutorialScript::TutorialStep::Step11_PlayerAttack,    {"素晴らしい！光のリンクが繋がったな！", "さあ、大砲とプレイヤーの連携で全ての敵を撃退しよう！"}                                        },
    {TutorialScript::TutorialStep::Step12_CombatPlay,      {"大砲にバフを与えるのも忘れるなよ！"}                                                                                                  },
    {TutorialScript::TutorialStep::Step13_SkillTree,
     {"敵を倒してレベルアップしたぞ！", "[RED]Nキー[/RED]を押してスキルツリーを開き、\nオレンジ色に点滅しているスキルを獲得してみよう！",
      "素晴らしい！スキルを獲得できたな！\n[RED]Nキー[/RED]を押してスキルツリーを閉じてくれ。"}                                                                                                    },
    {TutorialScript::TutorialStep::Step14_EndExplanation,  {"これでチュートリアルは終了だ！"}                                                                                                      },
    {TutorialScript::TutorialStep::Step15_FreePlayBattle,  {"ここからは無限に続くフリープレイになるぞ！", "ESCキーのポーズメニューからいつでも終了できる。\n存分に楽しんでくれ！"}                 }
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
	currentEntity_ = entity;
	currentScene_ = scene;
	tutorialStep_ = TutorialStep::Step1_Greeting;
	currentLineIndex_ = 0;
	PhaseSystemScript::ResetPhaseCount();
	phaseState_ = PhaseSystemScript::PreparationPhase;
	nextPhaseState_ = PhaseSystemScript::PreparationPhase;
	isPhaseTransitioning_ = false;
	isFadeFinished_ = false;
	isPlacementMode_ = false;
	hasPlacedCannon_ = false;
	hasOpenedSkillTreeInGuide_ = false;
	preKeyN_ = false;
	stepGuideShown_ = false;
	isSellMode_ = false;

	// サブ状態のリセット
	step7_placedExtraCannon_ = false;
	step7_deletedCannon_ = false;
	step13_pageSwitched_ = false;
	step13_upgraded_ = false;
	step13_initialSP_ = 0;

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
				isPlacementMode_ = true;
			} catch (...) {
			}
		});
	}
}

// チュートリアルの指定されたステップに移行し、必要な初期状態のセットアップやフェーズ切り替えを行う
void TutorialScript::EnterStep(TutorialStep step) {
	tutorialStep_ = step;
	stepGuideShown_ = false;
	isPlacementMode_ = false;
	autoProceedTimer_ = 0.0f;
	currentLineIndex_ = 0;

	if (step == TutorialStep::Step5_CameraControl) {
		step5_moved_ = false;
		step5_rotated_ = false;
	}
	if (step == TutorialStep::Step6_CannonInstall) {
		hasPlacedCannon_ = false;
		step6_cannonCount_ = 0;
	}
	if (step == TutorialStep::Step7_DeleteIntro) {
		step7_placedExtraCannon_ = false;
		step7_deletedCannon_ = false;
		hasPlacedCannon_ = false;
		isSellMode_ = false;
	}
	if (step == TutorialStep::Step13_SkillTree) {
		step13_pageSwitched_ = false;
		step13_upgraded_ = false;
		step13_initialSP_ = skillTree_.GetSkillPoints();
		SetVar(currentEntity_, currentScene_, "IsLevelUpPhase", 1.0f);
		if (currentScene_) {
			currentScene_->GetEventSystem().Emit("GainExp", 10000.0f); // 確実にレベルアップさせる
		}
		skillTree_.Close(nullptr);
	} else if (step == TutorialStep::Step14_EndExplanation) {
		SetVar(currentEntity_, currentScene_, "IsLevelUpPhase", 0.0f);
	}

	if (step == TutorialStep::Step9_BuffExplanation) {
		WaveManagement::SetWave(0); // プレイヤー操作説明と同時に敵をスポーンさせる
		ResetBrokenShieldCount();
	}

	if (step == TutorialStep::Step10_BuffPractice) {
		enemyTimeStopped_ = true; // オーラ説明時に敵の時間を止める
	}

	if (step == TutorialStep::Step11_PlayerAttack) {
		enemyTimeStopped_ = false; // オーラを繋いだら敵の時間を再開する
	}

	if (step == TutorialStep::Step9_BuffExplanation || step == TutorialStep::Step10_BuffPractice || step == TutorialStep::Step11_PlayerAttack || step == TutorialStep::Step12_CombatPlay ||
	    step == TutorialStep::Step15_FreePlayBattle) {
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

	outKeyN = input->Trigger(DIK_N) || (GetAsyncKeyState('N') & 0x8001) || input->IsControllerButtonTrigger(XINPUT_GAMEPAD_BACK);
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
	if (!scene || entity == entt::null || !scene->GetRegistry().valid(entity))
		return;
	auto* renderer = scene->GetRenderer();
	if (!renderer)
		return;
	auto* tc = scene->GetRegistry().try_get<TransformComponent>(entity);
	if (!tc)
		return;

	DirectX::XMFLOAT3 pos = tc->translate;
	constexpr int kSegments = 36;
	float time = (float)GetTickCount64() / 1000.0f;
	float alpha = 0.5f + 0.5f * std::sin(time * 5.0f);
	Engine::Vector4 c = {color.x, color.y, color.z, color.w * alpha};

	for (int j = 0; j < 3; ++j) {
		float r = radius + j * 0.5f;
		for (int i = 0; i < kSegments; ++i) {
			float theta1 = (i * 2.0f * 3.1415926f) / kSegments;
			float theta2 = ((i + 1) * 2.0f * 3.1415926f) / kSegments;
			Engine::Vector3 p1 = {pos.x + std::cos(theta1) * r, pos.y + 0.1f, pos.z + std::sin(theta1) * r};
			Engine::Vector3 p2 = {pos.x + std::cos(theta2) * r, pos.y + 0.1f, pos.z + std::sin(theta2) * r};
			renderer->DrawLine3D(p1, p2, c, true);
		}
	}
}

// チュートリアルのステップに応じて、コアやスポナーなどの注目させたいオブジェクトにハイライトを描画する
void TutorialScript::DrawHighlights(GameScene* scene) {
	if (!scene)
		return;
	auto* renderer = scene->GetRenderer();
	if (!renderer)
		return;

	float time = (float)GetTickCount64() / 1000.0f;

	// --- 視線誘導用：画面全体の暗転 ---
	bool needsDarken = false;
	entt::entity highlightEntity = entt::null;

	if (tutorialStep_ == TutorialStep::Step6_CannonInstall) {
		if (isPlacementMode_) {
			step6_clickedCannonButton_ = true;
		}
		if (!isPlacementMode_ && !step6_clickedCannonButton_) {
			needsDarken = true;
			highlightEntity = scene->FindObjectByName("CannonButton");
		}
	} else if (tutorialStep_ == TutorialStep::Step7_DeleteIntro) {
		if (!step7_deletedCannon_) {
			if (!isSellMode_) {
				needsDarken = true;
				highlightEntity = scene->FindObjectByName("DeleteButton");
			}
		}
	} else if (tutorialStep_ == TutorialStep::Step13_SkillTree) {
		if (!skillTree_.IsOpen()) {
			needsDarken = true;
		}
		// HPゲージはImGuiのForegroundで描画されるため、暗転を描画するだけでハイライトされる
	}

	if (needsDarken) {
		static uint32_t bgTexHandle = 0;
		if (bgTexHandle == 0)
			bgTexHandle = renderer->LoadTexture2D("Resources/Textures/white1x1.png");

		if (tutorialStep_ == TutorialStep::Step13_SkillTree) {
			// レベルUI部分をくり抜いて暗転を描画する
			float sw = (float)Engine::WindowDX::kW;
			float sh = (float)Engine::WindowDX::kH;
			float scale = 0.55f;
			float gaugeW = 1024.0f * scale;
			float gaugeH = 128.0f * scale;
			// PlayerScript.cppの配置に合わせる
			float holeX = sw - gaugeW - 250.0f - 10.0f;
			float holeY = sh - gaugeH - 15.0f - 40.0f;
			float holeW = gaugeW + 20.0f;
			float holeH = gaugeH + 50.0f;

			Engine::Renderer::SpriteDesc s;
			s.color = {0, 0, 0, 0.7f};
			s.layer = 15000;

			// Top
			s.x = 0;
			s.y = 0;
			s.w = sw;
			s.h = holeY;
			renderer->DrawSprite(bgTexHandle, s);
			// Bottom
			s.x = 0;
			s.y = holeY + holeH;
			s.w = sw;
			s.h = sh - (holeY + holeH);
			renderer->DrawSprite(bgTexHandle, s);
			// Left
			s.x = 0;
			s.y = holeY;
			s.w = holeX;
			s.h = holeH;
			renderer->DrawSprite(bgTexHandle, s);
			// Right
			s.x = holeX + holeW;
			s.y = holeY;
			s.w = sw - (holeX + holeW);
			s.h = holeH;
			renderer->DrawSprite(bgTexHandle, s);

			// 視線誘導のために穴の枠を少し光らせる
			Engine::Renderer::SpriteDesc s2;
			s2.color = {1.0f, 1.0f, 0.8f, 0.5f + 0.3f * std::sin(time * 5.0f)};
			s2.layer = 15001;
			float t = 4.0f; // thickness
			// Top edge
			s2.x = holeX - t;
			s2.y = holeY - t;
			s2.w = holeW + t * 2;
			s2.h = t;
			renderer->DrawSpriteAdditive(bgTexHandle, s2);
			// Bottom edge
			s2.x = holeX - t;
			s2.y = holeY + holeH;
			s2.w = holeW + t * 2;
			s2.h = t;
			renderer->DrawSpriteAdditive(bgTexHandle, s2);
			// Left edge
			s2.x = holeX - t;
			s2.y = holeY;
			s2.w = t;
			s2.h = holeH;
			renderer->DrawSpriteAdditive(bgTexHandle, s2);
			// Right edge
			s2.x = holeX + holeW;
			s2.y = holeY;
			s2.w = t;
			s2.h = holeH;
			renderer->DrawSpriteAdditive(bgTexHandle, s2);
		} else {
			Engine::Renderer::SpriteDesc s;
			s.x = 0;
			s.y = 0;
			s.w = (float)Engine::WindowDX::kW;
			s.h = (float)Engine::WindowDX::kH;
			s.color = {0, 0, 0, 0.7f}; // やや濃いめの暗転
			s.layer = 15000;
			renderer->DrawSprite(bgTexHandle, s);
		}
	}

	auto viewImg = scene->GetRegistry().view<UIImageComponent>();
	for (auto e : viewImg) {
		auto& img = viewImg.get<UIImageComponent>(e);
		if (e == highlightEntity) {
			img.layer = 20000; // 暗転より手前に描画

			// ★アイコンが暗く見えないように背面に光るパネルを描画する
			if (scene->GetRegistry().all_of<RectTransformComponent>(e)) {
				auto wr = UISystem::CalculateWorldRect(e, scene->GetRegistry(), (float)Engine::WindowDX::kW, (float)Engine::WindowDX::kH);

				static uint32_t glowTexHandle = 0;
				if (glowTexHandle == 0)
					glowTexHandle = renderer->LoadTexture2D("Resources/Textures/white1x1.png");

				Engine::Renderer::SpriteDesc bg;
				bg.x = wr.x;
				bg.y = wr.y;
				bg.w = wr.w;
				bg.h = wr.h;

				float alpha = 0.3f + 0.2f * std::sin(time * 5.0f); // 穏やかな点滅
				bg.color = {1.0f, 1.0f, 0.8f, alpha};
				bg.layer = 19999; // ボタン(20000)の直前（奥）に描画

				renderer->DrawSpriteAdditive(glowTexHandle, bg);
			}
		} else if (img.layer == 20000 || img.layer == 5000) {
			img.layer = 10; // 元に戻す
		}
	}

	if (tutorialStep_ == TutorialStep::Step2_CoreIntro) {
		auto core = scene->FindObjectByName("Core");
		if (scene->GetRegistry().valid(core)) {
			Draw3DHighlight(scene, core, {0.2f, 0.6f, 1.0f, 1.0f}, 4.0f);
		}
	}

	// スポナーのガイド（Step 3 または 戦闘フェーズ中）
	const bool isBattlePhase = (phaseState_ == PhaseSystemScript::BattlePhase);
	if (tutorialStep_ == TutorialStep::Step3_SpawnerIntro || isBattlePhase) {
		auto& registry = scene->GetRegistry();
		registry.view<NameComponent, TransformComponent>().each([&](entt::entity entity, const NameComponent& nc, const TransformComponent& tc) {
			if (nc.name.find("Spawner") != std::string::npos && (!registry.all_of<HierarchyComponent>(entity) || registry.get<HierarchyComponent>(entity).parentId == entt::null)) {

				// Step 3 の時だけ足元をハイライト
				if (tutorialStep_ == TutorialStep::Step3_SpawnerIntro) {
					Draw3DHighlight(scene, entity, {1.0f, 0.2f, 0.2f, 1.0f}, 3.0f);
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
					while (angleDiff < -PI_VAL)
						angleDiff += 2.0f * PI_VAL;
					while (angleDiff > PI_VAL)
						angleDiff -= 2.0f * PI_VAL;

					float margin = (float)Engine::WindowDX::kW * 0.1f;
					float startX = margin;
					float endX = (float)Engine::WindowDX::kW - margin;
					float barWidth = endX - startX;

					float t = (PI_VAL - angleDiff) / (2.0f * PI_VAL);
					float targetX = startX + t * barWidth;
					float targetY = 50.0f;

					Engine::Renderer::SdfUIDesc desc;
					desc.centerPx = {targetX, targetY};
					desc.sizePx = {70.0f + 10.0f * std::sin(time * 10.0f), 70.0f + 10.0f * std::sin(time * 10.0f)};
					desc.lineWidth = 4.0f;
					desc.color = {1.0f, 0.2f, 0.2f, 1.0f};
					desc.shape = 1;
					desc.fill = 0.0f;
					desc.glow = 1.0f;
					desc.additive = true;
					renderer->DrawSDFUI(desc);
				}
			}
		});
	}

	// ★追加: 大砲設置フェーズでは、大砲が置けるエリアを地面に斜線でハイライト表示する
	if (tutorialStep_ == TutorialStep::Step6_CannonInstall) {
		float minX = 9999.0f, maxX = -9999.0f;
		float minZ = 9999.0f, maxZ = -9999.0f;
		auto& registry = scene->GetRegistry();
		for (auto [entity, nc, tc] : registry.view<NameComponent, TransformComponent>().each()) {
			if (nc.name.find("Spawner") != std::string::npos || nc.name.find("Core") != std::string::npos) {
				if (tc.translate.x < minX) minX = tc.translate.x;
				if (tc.translate.x > maxX) maxX = tc.translate.x;
				if (tc.translate.z < minZ) minZ = tc.translate.z;
				if (tc.translate.z > maxZ) maxZ = tc.translate.z;
			}
		}

		if (minX <= maxX) {
			float validMinX = minX - 12.0f;
			float validMaxX = maxX + 12.0f;
			float validMinZ = minZ - 12.0f;
			float validMaxZ = maxZ + 12.0f;

			// 斜線を描画するステージ全体の範囲
			float stageMinX = validMinX - 16.0f;
			float stageMaxX = validMaxX + 16.0f;
			float stageMinZ = validMinZ - 16.0f;
			float stageMaxZ = validMaxZ + 16.0f;

			// 障害物（既存のタワーなど）のBoundingBoxを取得
			struct AABB { float minX, maxX, minZ, maxZ; };
			std::vector<AABB> obstacles;
			for (auto [e, tc] : registry.view<TransformComponent>().each()) {
				if (!registry.any_of<MeshRendererComponent, BoxColliderComponent, GpuMeshColliderComponent>(e)) continue;
				if (registry.all_of<NameComponent>(e)) {
					const auto& nc = registry.get<NameComponent>(e);
					if (nc.name.find("Terrain") != std::string::npos || nc.name.find("Floor") != std::string::npos || 
						nc.name.find("Ground") != std::string::npos || nc.name.find("Stage") != std::string::npos || 
						nc.name.find("Plane") != std::string::npos) continue;
				}
				// 障害物は大体半径2.0fの範囲を占有するとみなす
				obstacles.push_back({tc.translate.x - 2.0f, tc.translate.x + 2.0f, tc.translate.z - 2.0f, tc.translate.z + 2.0f});
			}

			// 地面に斜線を引く
			float spacing = 1.25f; // もっと密にして見やすくする
			for (float d = -100.0f; d <= 100.0f; d += spacing) {
				for (float z = stageMinZ; z < stageMaxZ; z += 1.0f) {
					float x1 = z + d;
					float x2 = (z + 1.0f) + d;
					
					// 範囲外なら描画しない
					if (x1 < stageMinX || x2 > stageMaxX) continue;

					float mx = (x1 + x2) * 0.5f;
					float mz = z + 0.5f;
					
					// 有効エリア内かどうか
					bool inValidArea = (mx >= validMinX && mx <= validMaxX && mz >= validMinZ && mz <= validMaxZ);
					bool blocked = !inValidArea;
					
					// 障害物との衝突判定
					if (!blocked) {
						for (const auto& obs : obstacles) {
							if (mx >= obs.minX && mx <= obs.maxX && mz >= obs.minZ && mz <= obs.maxZ) {
								blocked = true; 
								break;
							}
						}
					}
					
					// 緑色(設置可能)、赤色(設置不可) - 透明度を上げて濃くする
					Engine::Vector4 color = blocked ? Engine::Vector4{1.0f, 0.0f, 0.0f, 1.0f} : Engine::Vector4{0.0f, 1.0f, 0.0f, 0.8f};
					Engine::Vector3 p1 = {x1, 0.05f, z};
					Engine::Vector3 p2 = {x2, 0.05f, z + 1.0f};
					renderer->DrawLine3D(p1, p2, color, true);

					// バツ印にするため反対向きの斜線も追加
					Engine::Vector3 p3 = {x1, 0.05f, z + 1.0f};
					Engine::Vector3 p4 = {x2, 0.05f, z};
					renderer->DrawLine3D(p3, p4, color, true);
				}
			}
		}
	}
}

// 施設の売却（削除）モードの切り替えおよび、削除対象のハイライト表示・クリックによる削除処理を行う
void TutorialScript::UpdateSellMode(GameScene* scene) {
	if (!scene)
		return;
	auto* input = Engine::Input::GetInstance();
	if (!input)
		return;
	auto* renderer = scene->GetRenderer();
	if (!renderer)
		return;

	const bool keyX = input->Trigger(DIK_X) || (GetAsyncKeyState('X') & 0x8001);
	if (keyX || InstallationManager::IsButtonPressedByName("DeleteButton")) {
		isSellMode_ = !isSellMode_;
		isPlacementMode_ = false;
		EditorUI::Log(isSellMode_ ? "Tutorial: Sell Mode Activated" : "Tutorial: Sell Mode Deactivated");
	}

	if (!isSellMode_)
		return;

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
			if (nc.name.find("Canon") == std::string::npos && nc.name.find("Cannon") == std::string::npos) {
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

			if (!model)
				return;

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
				Engine::Vector3 minP = {tc->translate.x - tc->scale.x, tc->translate.y, tc->translate.z - tc->scale.z};
				Engine::Vector3 maxP = {tc->translate.x + tc->scale.x, tc->translate.y + tc->scale.y * 2.0f, tc->translate.z + tc->scale.z};

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
				step7_deletedCannon_ = true;
				EditorUI::Log("Tutorial: Object deleted successfully");
			}
		}
	}
}

// 毎フレームの更新処理。チュートリアルのステップ進行判定やキー入力の処理を一括して行う
void TutorialScript::Update(entt::entity entity, GameScene* scene, float dt) {
	currentEntity_ = entity;
	currentScene_ = scene;
	auto* input = Engine::Input::GetInstance();
	if (!scene || !input)
		return;

	ShowStepGuide();
	ShowGuideText(entity, scene);
	DrawHighlights(scene);

	const bool key3 = input->Trigger(DIK_3) || (GetAsyncKeyState('3') & 0x8001);

	static bool prevKeySpace = false;
	const bool currentRawSpace = (GetAsyncKeyState(VK_SPACE) & 0x8000) != 0;
	const bool keySpace = input->Trigger(DIK_SPACE) || (currentRawSpace && !prevKeySpace) || input->IsControllerButtonTrigger(XINPUT_GAMEPAD_A);
	prevKeySpace = currentRawSpace;

	bool placementSelectionChangedThisFrame = false;
	const bool clickedInstallationButtonThisFrame = input->IsMouseTrigger(0) && IsPointerOverInstallationButton(scene);

	// Keep Core, Player, and Towers fully healed during Steps 1 to 14 to prevent game over
	if (static_cast<int>(tutorialStep_) <= static_cast<int>(TutorialStep::Step14_EndExplanation)) {
		auto viewHC = scene->GetRegistry().view<HealthComponent>();
		for (auto e : viewHC) {
			bool isEnemy = false;
			if (auto* tag = scene->GetRegistry().try_get<TagComponent>(e)) {
				if (tag->tag == TagType::Enemy) {
					isEnemy = true;
				}
			}
			if (!isEnemy) {
				auto& hc = viewHC.get<HealthComponent>(e);
				hc.hp = hc.maxHp;
				hc.isDead = false;
			}
		}
	}

	// Text progression by SPACE key
	auto textIt = kTutorialTexts.find(tutorialStep_);
	if (textIt != kTutorialTexts.end()) {
		const auto& lines = textIt->second;
		if (keySpace) {
			// ★ SPACEキーをチュートリアルのテキスト進行で消費したフレームは、ジャンプをキャンセルする
			if (scene) {
				auto view = scene->GetRegistry().view<PlayerInputComponent>();
				for (auto e : view) {
					view.get<PlayerInputComponent>(e).jumpRequested = false;
				}
			}
			if (tutorialStep_ == TutorialStep::Step11_PlayerAttack) {
				// Step11はSPACEキーに反応しない（自動的に3秒後に進む）
			} else if (tutorialStep_ == TutorialStep::Step13_SkillTree) {
				// Step13 は「敵を倒してレベルアップしたぞ！」から「Nキーを押して～」へのみSPACEで進める
				if (currentLineIndex_ == 0) {
					currentLineIndex_ = 1;
				}
			} else {
				if (currentLineIndex_ < static_cast<int>(lines.size()) - 1) {
					currentLineIndex_++;
				} else {
					bool isActionStep =
					    (tutorialStep_ == TutorialStep::Step6_CannonInstall || tutorialStep_ == TutorialStep::Step7_DeleteIntro || tutorialStep_ == TutorialStep::Step9_BuffExplanation ||
					     tutorialStep_ == TutorialStep::Step10_BuffPractice || tutorialStep_ == TutorialStep::Step12_CombatPlay || tutorialStep_ == TutorialStep::Step13_SkillTree ||
					     tutorialStep_ == TutorialStep::Step15_FreePlayBattle);
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
	case TutorialStep::Step6_CannonInstall:
		if (phaseState_ != PhaseSystemScript::PreparationPhase || isPhaseTransitioning_)
			break;

		if (key3 || InstallationManager::IsButtonPressed("Resources/Prefabs/Canon.prefab")) {
			selectedObjPath_ = "Resources/Prefabs/Canon.prefab";
			isPlacementMode_ = true;
			placementSelectionChangedThisFrame = true;
		}

		if (input->IsMouseTrigger(1) && isPlacementMode_) {
			isPlacementMode_ = false;
		}

		if (!placementSelectionChangedThisFrame && !clickedInstallationButtonThisFrame) {
			Installation(scene, selectedObjPath_);
		}

		if ((isPlacementMode_ || step6_cannonCount_ > 0) && currentLineIndex_ < static_cast<int>(kTutorialTexts.at(tutorialStep_).size()) - 1) {
			currentLineIndex_ = static_cast<int>(kTutorialTexts.at(tutorialStep_).size()) - 1; // 設置モードに入った、または設置したらテキストスキップ
		}

		if (step6_cannonCount_ >= 3) {
			EnterStep(TutorialStep::Step7_DeleteIntro);
		}
		break;

	case TutorialStep::Step7_DeleteIntro:
		if (phaseState_ != PhaseSystemScript::PreparationPhase || isPhaseTransitioning_)
			break;

		if ((isSellMode_ || InstallationManager::IsButtonPressedByName("DeleteButton")) && currentLineIndex_ < static_cast<int>(kTutorialTexts.at(tutorialStep_).size()) - 1) {
			currentLineIndex_ = static_cast<int>(kTutorialTexts.at(tutorialStep_).size()) - 1; // 削除モードに入ったらテキストスキップ
		}

		if (currentLineIndex_ == 2) {
			if (!step7_deletedCannon_) {
				UpdateSellMode(scene);
			} else {
				isSellMode_ = false;
				EnterStep(TutorialStep::Step8_BattleTransition);
			}
		}
		break;

	case TutorialStep::Step8_BattleTransition:
		// SPACEキーで進行するため、ここでは何もしない
		break;

	case TutorialStep::Step9_BuffExplanation:
		if (brokenShieldCount_ > 0 && currentLineIndex_ < static_cast<int>(kTutorialTexts.at(tutorialStep_).size()) - 1) {
			currentLineIndex_ = static_cast<int>(kTutorialTexts.at(tutorialStep_).size()) - 1; // 攻撃を開始したらスキップ
		}
		if (currentLineIndex_ == static_cast<int>(kTutorialTexts.at(tutorialStep_).size()) - 1) {
			if (brokenShieldCount_ >= 2) {
				EnterStep(TutorialStep::Step10_BuffPractice);
			}
		}
		break;

	case TutorialStep::Step10_BuffPractice: {
		if (currentLineIndex_ == static_cast<int>(kTutorialTexts.at(tutorialStep_).size()) - 1) {
			bool anyBuffed = false;
			auto viewBuff = scene->GetRegistry().view<BuffComponent>();
			for (auto [e, buff] : viewBuff.each()) {
				if (buff.isBuffed) {
					anyBuffed = true;
					break;
				}
			}
			if (anyBuffed) {
				EnterStep(TutorialStep::Step11_PlayerAttack);
			}
		}
		break;
	}

	case TutorialStep::Step11_PlayerAttack: {
		step11Timer_ += dt;
		if (currentLineIndex_ == 0 && step11Timer_ > 3.0f) {
			currentLineIndex_ = 1;
		}
		if (currentLineIndex_ == 1 && step11Timer_ > 6.0f) {
			EnterStep(TutorialStep::Step12_CombatPlay);
			step11Timer_ = 0.0f;
		}
		break;
	}

	case TutorialStep::Step12_CombatPlay: {
		bool allDead = false;
		if (WaveManagement::GetManagerEntity() != entt::null) {
			if (auto* sc = scene->GetRegistry().try_get<ScriptComponent>(WaveManagement::GetManagerEntity())) {
				for (auto& entry : sc->scripts) {
					if (entry.scriptPath == "WaveManagement" && entry.instance) {
						auto* wm = static_cast<WaveManagement*>(entry.instance.get());
						if (wm->GetTotalRemainingEnemies(scene) <= 0) {
							allDead = true;
						}
					}
				}
			}
		}

		// 敵が全滅してWave終了、またはフェーズが切り替わった場合にクリアとする
		if (PhaseSystemScript::IsPhase() == PhaseSystemScript::PreparationPhase || WaveManagement::IsWaveEnded() || allDead) {
			EnterStep(TutorialStep::Step13_SkillTree);
		}
		break;
	}

	case TutorialStep::Step13_SkillTree: {
		if (phaseState_ != PhaseSystemScript::PreparationPhase || isPhaseTransitioning_)
			break;

		bool keyNTrigger = false;
		UpdateSkillTree(entity, scene, keyNTrigger);

		if (!skillTree_.IsOpen()) {
			SetVar(entity, scene, "IsSkillTreeOpen", 0.0f);
			// SkillTree が閉じられた場合、スキルが強化されていればStep14に進む
			if (step13_upgraded_) {
				EnterStep(TutorialStep::Step14_EndExplanation);
			}
		} else {
			SetVar(entity, scene, "IsSkillTreeOpen", 1.0f);
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
				if ((mx >= 100.0f && mx <= 180.0f && my >= 520.0f && my <= 570.0f) || (mx >= 1000.0f && mx <= 1080.0f && my >= 520.0f && my <= 570.0f)) {
					step13_pageSwitched_ = true;
				}
			}

			if (skillTree_.IsSkillUnlocked(1)) {
				// スキルを強化した
				step13_upgraded_ = true;
				currentLineIndex_ = 2;
			} else {
				currentLineIndex_ = 1;
			}
		}
		preKeyN_ = keyNTrigger;
		break;
	}

	case TutorialStep::Step14_EndExplanation:
		if (phaseState_ == PhaseSystemScript::PreparationPhase && !isPhaseTransitioning_) {
			EnterStep(TutorialStep::Step15_FreePlayBattle);
		}
		break;

	case TutorialStep::Step15_FreePlayBattle:
		if (phaseState_ == PhaseSystemScript::PreparationPhase && !isPhaseTransitioning_) {
			if (key3 || InstallationManager::IsButtonPressed("Resources/Prefabs/Canon.prefab")) {
				selectedObjPath_ = "Resources/Prefabs/Canon.prefab";
				isPlacementMode_ = true;
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
				EnterStep(TutorialStep::Step15_FreePlayBattle);
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
	UpdateCameraFocus(scene, dt);
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
	PhaseSystemScript::PhaseState currentPhase = PhaseSystemScript::IsPhase();

	if (!isPhaseTransitioning_) {
		// PhaseSystemScript側でフェーズが変わっていた場合（Spawner経由など）、同期する
		if (phaseState_ != currentPhase && currentPhase != PhaseSystemScript::Transition) {
			phaseState_ = currentPhase;
		}
		return;
	}

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

			if (tutorialStep_ == TutorialStep::Step15_FreePlayBattle) {
				WaveManagement::SetWave(0);
			}
		} else if (phaseState_ == PhaseSystemScript::PreparationPhase) {
			WaveManagement::SetWave(-1);
		}
	}
}

void TutorialScript::UpdateCameraFocus(GameScene* scene, float dt) {
	if (!scene)
		return;

	entt::entity targetEntity = entt::null;
	bool needsOverride = false;
	DirectX::XMFLOAT3 customEyeOffset = {0.0f, 20.0f, -25.0f}; // Default offset

	if (tutorialStep_ == TutorialStep::Step2_CoreIntro) {
		targetEntity = scene->FindObjectByName("Core");
		needsOverride = true;
		customEyeOffset = {8.0f, 6.0f, -8.0f}; // コアに大きくズーム
	} else if (tutorialStep_ == TutorialStep::Step3_SpawnerIntro) {
		auto& reg = scene->GetRegistry();
		for (auto [e, nc] : reg.view<NameComponent>().each()) {
			if (nc.name.find("Spawner") != std::string::npos) {
				targetEntity = e;
				break;
			}
		}
		needsOverride = true;
		customEyeOffset = {-5.0f, 6.0f, -10.0f}; // スポナーに大きくズーム
	}

	auto& reg = scene->GetRegistry();

	if (needsOverride && targetEntity != entt::null) {
		if (!isCameraOverriding_) {
			isCameraOverriding_ = true;
			cameraTargetFound_ = true;
			cameraTransitionTime_ = 0.0f;

			auto camPos = scene->GetCamera().Position();
			startEye_ = {camPos.x, camPos.y, camPos.z};

			// Calculate start target from camera rotation
			Engine::Vector3 rot = scene->GetCamera().GetRotation();
			float yaw = rot.y;
			float pitch = rot.x;
			float dirX = std::sin(yaw) * std::cos(pitch);
			float dirY = -std::sin(pitch);
			float dirZ = std::cos(yaw) * std::cos(pitch);
			startTarget_ = {startEye_.x + dirX * 10.0f, startEye_.y + dirY * 10.0f, startEye_.z + dirZ * 10.0f};

			// 一時的に既存のカメラターゲットを無効化（PreparationCameraの干渉を防ぐためコンポーネントごと削除）
			cameraTargetEntities_.clear();
			for (auto [e, ct] : reg.view<CameraTargetComponent>().each()) {
				cameraTargetEntities_.push_back(e);
			}
			for (auto e : cameraTargetEntities_) {
				reg.remove<CameraTargetComponent>(e);
			}
		}
	} else {
		if (isCameraOverriding_) {
			isCameraOverriding_ = false;
			cameraTargetFound_ = false;

			// コンポーネントを元のエンティティに復元
			for (auto e : cameraTargetEntities_) {
				if (reg.valid(e)) {
					(void)reg.get_or_emplace<CameraTargetComponent>(e);
				}
			}
			cameraTargetEntities_.clear();
		}
	}

	if (isCameraOverriding_ && cameraTargetFound_) {
		auto* tc = reg.try_get<TransformComponent>(targetEntity);
		if (tc) {
			cameraOverrideTargetPos_ = {tc->translate.x, tc->translate.y, tc->translate.z};
			cameraOverrideEyePos_ = {tc->translate.x + customEyeOffset.x, tc->translate.y + customEyeOffset.y, tc->translate.z + customEyeOffset.z};

			cameraTransitionTime_ += dt;
			float t = std::min(cameraTransitionTime_ / cameraTransitionMax_, 1.0f);

			// Smoothstep
			t = t * t * (3.0f - 2.0f * t);

			DirectX::XMFLOAT3 currentEye = {
			    startEye_.x + (cameraOverrideEyePos_.x - startEye_.x) * t, startEye_.y + (cameraOverrideEyePos_.y - startEye_.y) * t, startEye_.z + (cameraOverrideEyePos_.z - startEye_.z) * t};
			DirectX::XMFLOAT3 currentTarget = {
			    startTarget_.x + (cameraOverrideTargetPos_.x - startTarget_.x) * t, startTarget_.y + (cameraOverrideTargetPos_.y - startTarget_.y) * t,
			    startTarget_.z + (cameraOverrideTargetPos_.z - startTarget_.z) * t};

			auto& camera = scene->GetCamera();
			camera.SetPosition(currentEye.x, currentEye.y, currentEye.z);
			camera.LookAt(currentTarget.x, currentTarget.y, currentTarget.z, 0.0f, 1.0f, 0.0f);
		}
	}
}

// 現在のチュートリアルステップに基づくテキストをUIテキストコンポーネントに適用する
void TutorialScript::ShowGuideText(entt::entity entity, GameScene* scene) {
	if (!scene)
		return;
	if (scene->IsPaused())
		return; // ポーズ中はチュートリアル表示を消す

	auto& registry = scene->GetRegistry();
	if (!registry.all_of<UITextComponent>(entity))
		return;

	auto& uiText = registry.get<UITextComponent>(entity);
	uiText.enabled = false; // 独自で描画するためUISystemには描かせない

	auto it = kTutorialTexts.find(tutorialStep_);
	if (it != kTutorialTexts.end()) {
		const auto& lines = it->second;
		if (currentLineIndex_ >= 0 && currentLineIndex_ < static_cast<int>(lines.size())) {
			// Xbox用テキスト置換ヘルパー
			auto replaceGamepadText = [&](std::string text) {
				if (Engine::Input::GetInstance() && Engine::Input::GetInstance()->GetActiveDeviceType() == Engine::InputDeviceType::Controller) {
					size_t pos;
					std::string tgt;
					tgt = "SPACEキー"; while ((pos = text.find(tgt)) != std::string::npos) text.replace(pos, tgt.length(), "Aボタン");
					tgt = "[SPACE]キー"; while ((pos = text.find(tgt)) != std::string::npos) text.replace(pos, tgt.length(), "[A]ボタン");
					tgt = "WASDキー"; while ((pos = text.find(tgt)) != std::string::npos) text.replace(pos, tgt.length(), "左スティック");
					tgt = "WASD"; while ((pos = text.find(tgt)) != std::string::npos) text.replace(pos, tgt.length(), "左スティック");
					tgt = "マウス右ドラッグ"; while ((pos = text.find(tgt)) != std::string::npos) text.replace(pos, tgt.length(), "右スティック");
					tgt = "左クリック"; while ((pos = text.find(tgt)) != std::string::npos) text.replace(pos, tgt.length(), "RTボタン");
					tgt = "Eキー"; while ((pos = text.find(tgt)) != std::string::npos) text.replace(pos, tgt.length(), "RBボタン");
					tgt = "Nキー"; while ((pos = text.find(tgt)) != std::string::npos) text.replace(pos, tgt.length(), "BACKボタン");
					tgt = "ESCキー"; while ((pos = text.find(tgt)) != std::string::npos) text.replace(pos, tgt.length(), "STARTボタン");
				}
				return text;
			};

			std::string currentText = replaceGamepadText(lines[currentLineIndex_]);

			if (tutorialStep_ == TutorialStep::Step6_CannonInstall && currentLineIndex_ == 2) {
				currentText = replaceGamepadText("目標：[RED]大砲を3個設置[/RED] (" + std::to_string(step6_cannonCount_) + "/3)");
			}

			uiText.color = {1.0f, 1.0f, 1.0f, 1.0f}; // 白
			uiText.outlineEnabled = true;
			uiText.outlineColor = {0.0f, 0.0f, 0.0f, 1.0f}; // 黒のアウトライン
			uiText.outlineThickness = 2.0f;
			uiText.fontSize = 60.0f;
			uiText.fontPath = "Resources\\Fonts\\Kiwi_Maru\\KiwiMaru-Regular.ttf";

			auto* renderer = Engine::Renderer::GetInstance();
			if (renderer) {
				float fontScale = uiText.fontSize / 64.0f;
				float lineHeight = renderer->GetTextLineHeight(fontScale, uiText.fontPath);

				// タグを除いたテキストで幅を計算する関数
				auto measureRawTextWidth = [&](const std::string& text) {
					std::string noTag = text;
					size_t pos;
					while ((pos = noTag.find("[RED]")) != std::string::npos)
						noTag.replace(pos, 5, "");
					while ((pos = noTag.find("[/RED]")) != std::string::npos)
						noTag.replace(pos, 6, "");
					return renderer->MeasureTextWidth(noTag, fontScale, uiText.fontPath);
				};

				// 行ごとに分割
				std::vector<std::string> splitted;
				size_t start = 0;
				size_t nlPos;
				while ((nlPos = currentText.find('\n', start)) != std::string::npos) {
					splitted.push_back(currentText.substr(start, nlPos - start));
					start = nlPos + 1;
				}
				splitted.push_back(currentText.substr(start));

				float exactW = 0.0f;
				for (const auto& line : splitted) {
					float w = measureRawTextWidth(line);
					if (w > exactW)
						exactW = w;
				}
				float exactH = lineHeight * splitted.size();

				std::string navText = "";
				bool isActionStep =
				    (tutorialStep_ == TutorialStep::Step5_CameraControl || tutorialStep_ == TutorialStep::Step6_CannonInstall || tutorialStep_ == TutorialStep::Step7_DeleteIntro ||
				     tutorialStep_ == TutorialStep::Step9_BuffExplanation || tutorialStep_ == TutorialStep::Step10_BuffPractice || tutorialStep_ == TutorialStep::Step12_CombatPlay ||
				     tutorialStep_ == TutorialStep::Step13_SkillTree || tutorialStep_ == TutorialStep::Step15_FreePlayBattle);

				bool isLastLine = (currentLineIndex_ >= static_cast<int>(lines.size()) - 1);

				if (tutorialStep_ == TutorialStep::Step11_PlayerAttack) {
					navText = "( 自動で進みます )";
				} else if (tutorialStep_ == TutorialStep::Step5_CameraControl && isLastLine) {
					navText = ""; // 自動・アクション進行
				} else if (tutorialStep_ == TutorialStep::Step7_DeleteIntro && currentLineIndex_ == 2) {
					navText = "[SPACE]キー で進む ▼";
				} else if (tutorialStep_ == TutorialStep::Step13_SkillTree && currentLineIndex_ > 0) {
					navText = ""; // アクション待ち
				} else if (isActionStep && isLastLine) {
					navText = ""; // アクション待ち
				} else {
					navText = "[SPACE]キー で進む ▼";
				}
				
				navText = replaceGamepadText(navText);

				float navScale = fontScale * 0.8f;
				float navW = 0.0f;
				float navH = 0.0f;
				if (!navText.empty()) {
					navW = renderer->MeasureTextWidth(navText, navScale, uiText.fontPath);
					navH = renderer->GetTextLineHeight(navScale, uiText.fontPath);
				}

				float maxW = std::max(exactW, navW);
				float centerX = Engine::WindowDX::kW * 0.5f;

				// 動的Y位置計算 (被りを防ぐためステップに応じてYを変える)
				float targetY = Engine::WindowDX::kH * 0.1f; // デフォルト上部
				if (tutorialStep_ == TutorialStep::Step2_CoreIntro || tutorialStep_ == TutorialStep::Step3_SpawnerIntro) {
					targetY = Engine::WindowDX::kH * 0.75f; // コア等を見せる時は下部
				}
				if (tutorialStep_ == TutorialStep::Step6_CannonInstall || tutorialStep_ == TutorialStep::Step7_DeleteIntro) {
					targetY = Engine::WindowDX::kH * 0.15f; // UIを見る時は少し上
				}
				if (tutorialStep_ == TutorialStep::Step13_SkillTree && skillTree_.IsOpen()) {
					targetY = Engine::WindowDX::kH * 0.78f; // スキルツリーと被らないように下へ
				}

				static uint32_t bgTexHandle = 0;
				if (bgTexHandle == 0) {
					bgTexHandle = renderer->LoadTexture2D("Resources/Textures/white1x1.png");
				}
				float margin = 40.0f;
				float navSpaceY = 20.0f;
				float totalH = exactH + (navText.empty() ? 0 : navSpaceY + navH);

				float bgX = centerX - maxW * 0.5f - margin;
				float bgY = targetY - margin;

				Engine::Renderer::SpriteDesc bgDesc;
				bgDesc.x = bgX;
				bgDesc.y = bgY;
				bgDesc.w = maxW + margin * 2.0f;
				bgDesc.h = totalH + margin * 2.0f;
				bgDesc.layer = 10000;
				bgDesc.color = {0.0f, 0.0f, 0.0f, 0.6f}; // 半透明黒背景
				renderer->DrawSprite(bgTexHandle, bgDesc);

				// 本文描画ルーチン
				float currentY = targetY;
				for (const auto& line : splitted) {
					float lineW = measureRawTextWidth(line);
					float currentX = centerX - lineW * 0.5f;

					std::string textToProcess = line;
					bool isRed = false;

					while (!textToProcess.empty()) {
						size_t redStart = textToProcess.find("[RED]");
						size_t redEnd = textToProcess.find("[/RED]");
						size_t nextTag = std::min(redStart, redEnd);

						std::string part = textToProcess.substr(0, nextTag);
						if (!part.empty()) {
							Engine::Vector4 color = isRed ? Engine::Vector4{1.0f, 0.3f, 0.3f, 1.0f} : Engine::Vector4{uiText.color.x, uiText.color.y, uiText.color.z, uiText.color.w};
							float ot = uiText.outlineThickness;
							Engine::Vector4 oColor = {uiText.outlineColor.x, uiText.outlineColor.y, uiText.outlineColor.z, uiText.outlineColor.w};

							// アウトライン描画
							renderer->DrawString(part, currentX - ot, currentY, fontScale, oColor, uiText.fontPath);
							renderer->DrawString(part, currentX + ot, currentY, fontScale, oColor, uiText.fontPath);
							renderer->DrawString(part, currentX, currentY - ot, fontScale, oColor, uiText.fontPath);
							renderer->DrawString(part, currentX, currentY + ot, fontScale, oColor, uiText.fontPath);

							renderer->DrawString(part, currentX, currentY, fontScale, color, uiText.fontPath);
							currentX += renderer->MeasureTextWidth(part, fontScale, uiText.fontPath);
						}

						if (nextTag == std::string::npos)
							break;
						if (nextTag == redStart) {
							isRed = true;
							textToProcess = textToProcess.substr(redStart + 5);
						} else {
							isRed = false;
							textToProcess = textToProcess.substr(redEnd + 6);
						}
					}
					currentY += lineHeight;
				}

				if (!navText.empty()) {
					static float blinkTimer = 0.0f;
					blinkTimer += 0.016f;
					float alpha = (std::sin(blinkTimer * 5.0f) + 1.0f) * 0.5f;
					float minAlpha = 0.3f;
					float finalAlpha = minAlpha + (1.0f - minAlpha) * alpha;

					Engine::Vector4 navColor = {1.0f, 1.0f, 1.0f, finalAlpha};
					Engine::Vector4 outlineColor = {0.0f, 0.0f, 0.0f, finalAlpha};

					float navX = centerX + maxW * 0.5f - navW;
					float navY = targetY + exactH + navSpaceY;

					float shadowOffset = 2.0f;
					renderer->DrawString(navText, navX + shadowOffset, navY + shadowOffset, navScale, outlineColor, uiText.fontPath);
					renderer->DrawString(navText, navX, navY, navScale, navColor, uiText.fontPath);
				}
			}
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

	const bool canPlace = !IsPlacementBlocked(scene, snappedHitPoint) && !IsPointerOverInstallationButton(scene);
	DrawPlacementPreview(scene, snappedHitPoint, objPath, canPlace);

	if (input->IsMouseTrigger(0) && canPlace) {
		SpawnPlacedObject(scene, snappedHitPoint, objPath);
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

	// シーン遷移中はグリッドやプレビューを一切描画しない
	auto* sm = Engine::SceneManager::GetInstance();
	if (sm && sm->GetTransitionState() != Engine::SceneManager::TransitionState::None) {
		return;
	}

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
			if (nc.name.find("Canon") != std::string::npos || nc.name.find("Cannon") != std::string::npos || nc.name.find("Tank") != std::string::npos ||
			    nc.name.find("Missile") != std::string::npos || nc.name.find("Poison") != std::string::npos) {
				Engine::Transform planeTr;
				planeTr.scale = {1.0f, 0.05f, 1.0f};
				Engine::Vector4 colorPlane = {0.0f, 1.0f, 0.0f, 0.4f};

				// X+ direction
				planeTr.translate = {tc.translate.x + 2.0f, tc.translate.y + 0.05f, tc.translate.z};
				renderer->DrawMesh(crossPlaneHandle, previewTextureHandle_, planeTr, colorPlane, "Toon");
				// X- direction
				planeTr.translate = {tc.translate.x - 2.0f, tc.translate.y + 0.05f, tc.translate.z};
				renderer->DrawMesh(crossPlaneHandle, previewTextureHandle_, planeTr, colorPlane, "Toon");
				// Z+ direction
				planeTr.translate = {tc.translate.x, tc.translate.y + 0.05f, tc.translate.z + 2.0f};
				renderer->DrawMesh(crossPlaneHandle, previewTextureHandle_, planeTr, colorPlane, "Toon");
				// Z- direction
				planeTr.translate = {tc.translate.x, tc.translate.y + 0.05f, tc.translate.z - 2.0f};
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
			Engine::Vector3 p1 = {hitPoint.x + std::cos(theta1) * attackRange, hitPoint.y + 0.05f, hitPoint.z + std::sin(theta1) * attackRange};
			Engine::Vector3 p2 = {hitPoint.x + std::cos(theta2) * attackRange, hitPoint.y + 0.05f, hitPoint.z + std::sin(theta2) * attackRange};
			renderer->DrawLine3D(p1, p2, {0.0f, 0.8f, 0.0f, 1.0f}, true);
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

	// ★ チュートリアル特有の制限：コアとスポナーを囲む矩形領域内にしか置けないようにする
	bool isNearValidArea = false;
	float minX = 9999.0f, maxX = -9999.0f;
	float minZ = 9999.0f, maxZ = -9999.0f;
	auto viewTransform = registry.view<NameComponent, TransformComponent>();
	for (auto [entity, nc, tc] : viewTransform.each()) {
		if (nc.name.find("Spawner") != std::string::npos || nc.name.find("Core") != std::string::npos) {
			if (tc.translate.x < minX) minX = tc.translate.x;
			if (tc.translate.x > maxX) maxX = tc.translate.x;
			if (tc.translate.z < minZ) minZ = tc.translate.z;
			if (tc.translate.z > maxZ) maxZ = tc.translate.z;
		}
	}

	if (minX <= maxX) {
		if (hitPoint.x >= minX - 12.0f && hitPoint.x <= maxX + 12.0f &&
		    hitPoint.z >= minZ - 12.0f && hitPoint.z <= maxZ + 12.0f) {
			isNearValidArea = true;
		}
	}

	if (!isNearValidArea) {
		return true; // エリア外の場合はブロックする
	}

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

		if (objPath.find("Canon") != std::string::npos) {
			hasPlacedCannon_ = true;
			if (tutorialStep_ == TutorialStep::Step6_CannonInstall) {
				step6_cannonCount_++;
			}
		}

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

	if (objPath.find("Canon") != std::string::npos) {
		hasPlacedCannon_ = true;
		if (tutorialStep_ == TutorialStep::Step6_CannonInstall) {
			step6_cannonCount_++;
		}
	}
}

// スクリプト破棄時の処理。スキルツリーを閉じ、フェーズを初期状態にリセットする
void TutorialScript::OnDestroy(entt::entity /*entity*/, GameScene* scene) {
	if (instance_ == this)
		instance_ = nullptr;
	skillTree_.Close(scene);
}

REGISTER_SCRIPT(TutorialScript);

} // namespace Game
