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
    // ★追加: ライト用ギズモの描画
    void DrawLightGizmos();

    // The original public `isPlaying_` is removed as per the instruction's implied move to private.

private:
    Engine::WindowDX* dx_ = nullptr;
    Engine::Renderer* renderer_ = nullptr;
    Engine::Camera camera_;
    std::vector<SceneObject> objects_;
    std::set<int> selectedIndices_;
    int selectedObjectIndex_ = -1;

    const std::vector<SceneObject>& GetObjects() const { return objects_; }
    void SetObjects(const std::vector<SceneObject>& o) { objects_ = o; }

    bool isPlaying_ = false; // ★追加: エディタからのPlayモード管理

    // ★追加: 個別のパーティクルエディター
    Engine::ParticleEditor particleEditor_;

    friend class EditorUI;
};

} // namespace Game