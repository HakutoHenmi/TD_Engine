#include "SceneManager.h"
#include <Windows.h> // OutputDebugStringA
#include "Renderer.h"

namespace Engine {


SceneManager* SceneManager::instance_ = nullptr;

SceneManager* SceneManager::GetInstance() { return instance_; }

void SceneManager::Register(const std::string& name, Factory factory) { factories_[name] = std::move(factory); }

bool SceneManager::Has(const std::string& name) const { return factories_.find(name) != factories_.end(); }

std::string SceneManager::FirstRegisteredName() const {
	if (factories_.empty())
		return {};
	// unordered_map は順序不定だが「何も無いよりマシ」な自動起動用
	return factories_.begin()->first;
}

std::vector<std::string> SceneManager::RegisteredNames() const {
	std::vector<std::string> out;
	out.reserve(factories_.size());
	for (auto& kv : factories_)
		out.push_back(kv.first);
	return out;
}

bool SceneManager::Change(const std::string& name, const SceneParameters& params) {
	auto it = factories_.find(name);
	if (it == factories_.end()) {
		OutputDebugStringA("[SceneManager] Change failed: not registered\n");
		return false;
	}

	current_ = it->second();
	currentName_ = name;

	// ★追加: sceneNameが空の場合、シーンキー名を自動設定
	SceneParameters adjustedParams = params;
	if (adjustedParams.sceneName.empty()) {
		adjustedParams.sceneName = name;
	}

	if (current_) {
		OutputDebugStringA("[SceneManager] Initialize scene\n");
		current_->Initialize(dx_, adjustedParams);
	}

	pendingNext_.clear();
	pendingParams_ = {};
	return true;
}

void SceneManager::RequestChange(const std::string& name, const SceneParameters& params) {
	pendingNext_ = name;
	pendingParams_ = params;
}

void SceneManager::Update() {
	if (!isShutterLoaded_) {
		shutterTex_ = Engine::Renderer::GetInstance()->LoadTexture2D("Resources/Textures/syatta.png");
		isShutterLoaded_ = true;
	}

	// 遷移開始のトリガー
	if (transitionState_ == TransitionState::None) {
		if (!pendingNext_.empty()) {
			transitionState_ = TransitionState::Closing;
			transitionTimer_ = 0.0f;
		} else if (current_ && current_->IsEnd()) {
			const std::string next = current_->Next();
			if (!next.empty()) {
				pendingNext_ = next;
				transitionState_ = TransitionState::Closing;
				transitionTimer_ = 0.0f;
			}
		}
	}

	// 遷移の更新
	if (transitionState_ == TransitionState::Closing) {
		transitionTimer_ += 1.0f / 60.0f; // 固定フレームレートと仮定
		
		float progress = transitionTimer_ / transitionDuration_;
		if (progress > 1.0f) progress = 1.0f;
		
		float screenW = (float)Engine::WindowDX::kW;
		float screenH = (float)Engine::WindowDX::kH;
		float easeProgress = progress * progress * (3.0f - 2.0f * progress);
		float currentY = -screenH + (screenH * easeProgress) + screenH; // シャッターの下端のY座標

		// 落ちている最中、端から火花を大量に出す
		if (progress < 1.0f && currentY > 0.0f && currentY < screenH) {
			for (int i = 0; i < 6; ++i) { // 2から6へ増加
				ShutterParticle p;
				p.x = (rand() % 2 == 0) ? static_cast<float>(rand() % 30) : static_cast<float>(screenW - (rand() % 30)); // 左右の端
				p.y = currentY - static_cast<float>(rand() % 20); // シャッターの少し上
				p.vx = (p.x < screenW / 2) ? (3.0f + static_cast<float>(rand() % 10)) : -(3.0f + static_cast<float>(rand() % 10)); // 内側へ勢いよく飛ぶ
				p.vy = -5.0f - static_cast<float>(rand() % 15); // より高く跳ねる
				p.maxLife = 0.2f + static_cast<float>(rand() % 20) / 30.0f;
				p.life = p.maxLife;
				p.size = 15.0f + static_cast<float>(rand() % 20); // サイズも少し大きく
				p.angle = 0.0f;
				p.color = {1.0f, 0.6f, 0.1f, 1.0f}; // オレンジ色の火花
				p.isSmoke = false;
				shutterParticles_.push_back(p);
			}
		}

		if (transitionTimer_ >= transitionDuration_) {
			// 暗転しきったタイミングでシーン切替
			Change(pendingNext_, pendingParams_);
			transitionState_ = TransitionState::Opening;
			transitionTimer_ = 0.0f;

			// 落ち切った瞬間に画面下から煙と火花を生成
			for (int i = 0; i < 15; ++i) { // 描画負荷(重さ)を軽減するため煙の数を減らす
				ShutterParticle p;
				p.x = static_cast<float>(rand() % static_cast<int>(screenW));
				p.y = screenH - static_cast<float>(rand() % 40); 
				p.vx = -12.0f + static_cast<float>(rand() % 240) / 10.0f; 
				p.vy = -3.0f - static_cast<float>(rand() % 50) / 10.0f; 
				p.maxLife = 0.8f + static_cast<float>(rand() % 12) / 10.0f; 
				p.life = p.maxLife;
				p.size = 80.0f + static_cast<float>(rand() % 80); // サイズも少し小さくして塗りつぶし面積(負荷)を減らす
				p.angle = static_cast<float>(rand() % 314) / 100.0f; 
				p.color = {0.5f, 0.5f, 0.5f, 1.0f}; 
				p.isSmoke = true;
				shutterParticles_.push_back(p);
			}
			
			// 衝突時の大火花
			for (int i = 0; i < 60; ++i) { // 火花の数を2倍に増量
				ShutterParticle p;
				p.x = static_cast<float>(rand() % static_cast<int>(screenW));
				p.y = screenH - 10.0f - static_cast<float>(rand() % 30); // 少し上からも出るように
				p.vx = -15.0f + static_cast<float>(rand() % 300) / 10.0f; 
				p.vy = -12.0f - static_cast<float>(rand() % 40); // 非常に高く跳ね上げる
				p.maxLife = 0.3f + static_cast<float>(rand() % 30) / 30.0f; // 寿命も少し長く
				p.life = p.maxLife;
				p.size = 20.0f + static_cast<float>(rand() % 30); // サイズも少し大きく
				p.angle = 0.0f;
				p.color = {1.0f, 0.8f, 0.3f, 1.0f}; 
				p.angle = std::atan2(p.vy, p.vx); // モーションブラーのため進行方向へ回転
				p.isSmoke = false;
				shutterParticles_.push_back(p);
			}
		}
	} else if (transitionState_ == TransitionState::Opening) {
		transitionTimer_ += 1.0f / 60.0f;
		if (transitionTimer_ >= transitionDuration_) {
			transitionState_ = TransitionState::None;
		}
	}

	// パーティクルの更新
	for (auto it = shutterParticles_.begin(); it != shutterParticles_.end(); ) {
		it->x += it->vx;
		it->y += it->vy;
		if (it->isSmoke) {
			it->vx *= 0.94f; // 空気抵抗を強める
			it->vy *= 0.94f;
			it->size += 1.5f; // 広がる速度
			// 寿命によるフェードアウト: 最初は濃さを保ち、後半でゆっくり消えるようにする
			float lifeRatio = it->life / it->maxLife;
			it->color.w = lifeRatio * 1.5f; // 最大1.5として、寿命の1/3まではアルファ1.0を維持する
			if (it->color.w > 1.0f) it->color.w = 1.0f;
			it->angle += 0.015f; // 時間経過でノイズをスクロールさせて煙が巻いているように見せる
		} else {
			it->vy += 0.5f; // 火花は重力で落ちる
			it->size *= 0.92f; // 火花は小さくなる
			it->color.w = (it->life / it->maxLife);
			it->angle = std::atan2(it->vy, it->vx); // 進行方向へ向ける
		}
		
		it->life -= 1.0f / 60.0f;
		if (it->life <= 0.0f) {
			it = shutterParticles_.erase(it);
		} else {
			++it;
		}
	}

	if (current_) {
		current_->Update();
	}
}

void SceneManager::Draw() {
	if (current_) {
		current_->Draw();
	}
}

void SceneManager::DrawOverlay() {
	// フェード（シャッター）とパーティクルの描画
	if (transitionState_ != TransitionState::None && isShutterLoaded_) {
		auto* renderer = Engine::Renderer::GetInstance();
		float screenW = (float)Engine::WindowDX::kW;
		float screenH = (float)Engine::WindowDX::kH;

		float progress = transitionTimer_ / transitionDuration_;
		if (progress > 1.0f) progress = 1.0f;

		// スチームパンク風のシャッターが上からガシャンと降りてくる
		float yOffset = 0.0f;
		float easeProgress = progress * progress * (3.0f - 2.0f * progress);

		if (transitionState_ == TransitionState::Closing) {
			yOffset = -screenH + (screenH * easeProgress);
		} else {
			yOffset = -(screenH * easeProgress);
		}

		Engine::Renderer::SpriteDesc desc;
		desc.x = 0;
		desc.y = yOffset;
		desc.w = screenW;
		desc.h = screenH;
		desc.layer = 1000; // 最前面
		renderer->DrawSprite(shutterTex_, desc);
	}

	// パーティクルの描画
	if (!shutterParticles_.empty()) {
		auto* renderer = Engine::Renderer::GetInstance();
		for (const auto& p : shutterParticles_) {
			Engine::Renderer::SdfUIDesc sdf;
			sdf.centerPx = {p.x, p.y};
			sdf.sizePx = {p.size, p.size};
			sdf.color = p.color;
			sdf.fill = 1.0f; // 塗りつぶし
			sdf.rotateRad = p.angle; // ランダムシード/時間としてシェーダに渡す
			
			if (p.isSmoke) {
				sdf.shape = 3; // プロシージャル煙
			} else {
				// ★火花はSDFプロシージャル + 加算合成で表現する
				sdf.shape = 4; // ソフトな火花
				sdf.additive = true; // 加算合成パイプラインを使用
				// Shape 4はuSizePx.xを使うため、少し大きめに設定
				sdf.sizePx = {p.size * 1.5f, p.size * 1.5f}; 
				// ピボットではなくSDFの中心がp.x, p.yになる
			}
			renderer->DrawSDFUI(sdf);
		}
	}
}

void SceneManager::Clear() {
	current_.reset();
	currentName_.clear();
	pendingNext_.clear();
	factories_.clear();
}

} // namespace Engine
