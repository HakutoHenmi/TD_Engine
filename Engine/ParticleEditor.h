#pragma once
#include "ParticleEmitter.h"
#include <string>

namespace Engine {

class ParticleEditor {
public:
	void Initialize();
	void Update(float dt);
	void DrawUI();
	void DrawPreview(const Camera& cam);

	// ★追加: 編集用・プレビュー用のエミッター（対象が外部から与えられない場合に使用）
	ParticleEmitter previewEmitter_;

	// 編集対象のエミッター（コンポーネント用、nullptrでもよい）
	ParticleEmitter* targetEmitter = nullptr;

private:
	char filePathBuf_[256] = "Resources/particle.json";
};

} // namespace Engine
