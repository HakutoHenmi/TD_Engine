#pragma once
#include "IScene.h"
#include "Camera.h"
#include "Renderer.h"
#include "Model.h"
#include "Transform.h"
#include "WindowDX.h"
#include "../ObjectTypes.h"
#include <vector>
#include <set>
#include "../../Engine/ParticleEmitter.h"
#include "../../Engine/ParticleEditor.h" // ★追加

namespace Game {

class GameScene : public Engine::IScene {
public:
    void Initialize(Engine::WindowDX* dx) override;
    void Update() override;
    void Draw() override;
    void DrawEditor() override;

    void DrawEditorGizmos();
    // ★追加: 選択オブジェクトのハイライトとギズモを描画
    void DrawSelectionHighlight();

    bool isPlaying_ = false;

private:
    Engine::WindowDX* dx_ = nullptr;
    Engine::Renderer* renderer_ = nullptr;
    Engine::Camera camera_;
    std::vector<SceneObject> objects_;
    std::set<int> selectedIndices_;
    int selectedObjectIndex_ = -1;

    // ★追加: 個別のパーティクルエディター
    Engine::ParticleEditor particleEditor_;

    friend class EditorUI;
};

} // namespace Game