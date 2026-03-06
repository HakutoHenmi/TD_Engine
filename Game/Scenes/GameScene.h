#pragma once
#include "IScene.h"
#include "Camera.h"
#include "Renderer.h"
#include "Model.h"
#include "Transform.h"
#include "WindowDX.h"
#include "../ObjectTypes.h"
#include "../Systems/ISystem.h"
#include <vector>
#include <set>
#include <memory>
#include "../../Engine/ParticleEmitter.h"
#include "../../Engine/ParticleEditor.h"

namespace Game {

class GameScene : public Engine::IScene {
public:
    void Initialize(Engine::WindowDX* dx) override;
    void Update() override;
    void Draw() override;
    void DrawEditor() override;

    void DrawEditorGizmos();
    void DrawSelectionHighlight();
    void DrawLightGizmos();

	// ★ 汎用スポーン（スクリプトから呼べる）
	void SpawnObject(const SceneObject& obj);

    const std::vector<SceneObject>& GetObjects() const { return objects_; }
    void SetObjects(const std::vector<SceneObject>& o) { objects_ = o; }
    bool IsPlaying() const { return isPlaying_; }
    Engine::Renderer* GetRenderer() const { return renderer_; } // ★追加
    Engine::Matrix4x4 GetWorldMatrix(int index) const; // ★追加

private:
    Engine::WindowDX* dx_ = nullptr;
    Engine::Renderer* renderer_ = nullptr;
    Engine::Camera camera_;
    std::vector<SceneObject> objects_;
    std::set<int> selectedIndices_;
    int selectedObjectIndex_ = -1;

    bool isPlaying_ = false;
    std::vector<SceneObject> pendingSpawns_;

    // ★ ECS風Systemリスト
    std::vector<std::unique_ptr<ISystem>> systems_;
    GameContext ctx_;

    // パーティクルエディター
    Engine::ParticleEditor particleEditor_;

    friend class EditorUI;
    friend class PipeEditor;
};

} // namespace Game