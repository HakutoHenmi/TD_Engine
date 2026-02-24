#include "ParticleEditor.h"
#include "../externals/imgui/imgui.h"

namespace Engine {

void ParticleEditor::Initialize() {
	previewEmitter_.Initialize(*Renderer::GetInstance(), "PreviewEmitter");
	targetEmitter = &previewEmitter_;
}

void ParticleEditor::Update(float dt) {
	if (targetEmitter == &previewEmitter_) {
		previewEmitter_.Update(dt);
	}
}

void ParticleEditor::DrawPreview(const Camera& cam) {
	if (targetEmitter == &previewEmitter_) {
		previewEmitter_.Draw(cam);
	}
}

void ParticleEditor::DrawUI() {
	if (!targetEmitter) return;

	ImGui::Begin("Particle Editor");

	if (ImGui::CollapsingHeader("File", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::InputText("File Path", filePathBuf_, sizeof(filePathBuf_));
		if (ImGui::Button("Save JSON")) {
			targetEmitter->SaveToJson(filePathBuf_);
		}
		ImGui::SameLine();
		if (ImGui::Button("Load JSON")) {
			targetEmitter->LoadFromJson(filePathBuf_);
		}
	}

	if (ImGui::CollapsingHeader("Playback", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Checkbox("Is Playing", &targetEmitter->isPlaying);
		if (ImGui::Button("Emit Burst (10)")) {
			targetEmitter->EmitBurst(10);
		}
	}

	if (ImGui::CollapsingHeader("Emission & Shape", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::DragFloat("Emit Rate (Hz)", &targetEmitter->params.emitRate, 0.1f, 0.0f, 1000.0f);
		ImGui::DragFloat("Life Time", &targetEmitter->params.lifeTime, 0.01f, 0.1f, 10.0f);
		ImGui::DragFloat("Life Time Variance", &targetEmitter->params.lifeTimeVariance, 0.01f, 0.0f, 5.0f);

		// ★追加: 形状
		int shapeType = static_cast<int>(targetEmitter->params.shape);
		const char* shapeNames[] = { "Point", "Sphere", "Cone" };
		if (ImGui::Combo("Emission Shape", &shapeType, shapeNames, IM_ARRAYSIZE(shapeNames))) {
			targetEmitter->params.shape = static_cast<EmissionShape>(shapeType);
		}
		if (targetEmitter->params.shape != EmissionShape::Point) {
			ImGui::DragFloat("Shape Radius", &targetEmitter->params.shapeRadius, 0.01f, 0.0f, 100.0f);
		}
		if (targetEmitter->params.shape == EmissionShape::Cone) {
			ImGui::DragFloat("Cone Angle (Rad)", &targetEmitter->params.shapeAngle, 0.01f, 0.0f, 3.1415f);
		}
	}

	if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::DragFloat3("Position", &targetEmitter->params.position.x, 0.1f);
	}

	if (ImGui::CollapsingHeader("Velocity & Acceleration", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::DragFloat3("Start Velocity", &targetEmitter->params.startVelocity.x, 0.1f);
		ImGui::DragFloat3("Velocity Variance", &targetEmitter->params.velocityVariance.x, 0.1f);
		ImGui::DragFloat3("Acceleration", &targetEmitter->params.acceleration.x, 0.1f);
		ImGui::DragFloat("Damping", &targetEmitter->params.damping, 0.01f, 0.0f, 100.0f); // ★追加: 摩擦
	}

	if (ImGui::CollapsingHeader("Size", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::DragFloat3("Start Size", &targetEmitter->params.startSize.x, 0.01f, 0.0f, 10.0f);
		ImGui::DragFloat3("Start Size Variance", &targetEmitter->params.startSizeVariance.x, 0.01f, 0.0f, 10.0f);
		ImGui::DragFloat3("End Size", &targetEmitter->params.endSize.x, 0.01f, 0.0f, 10.0f);
		ImGui::DragFloat3("End Size Variance", &targetEmitter->params.endSizeVariance.x, 0.01f, 0.0f, 10.0f);
	}

	if (ImGui::CollapsingHeader("Color", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::ColorEdit4("Start Color", &targetEmitter->params.startColor.x);
		ImGui::ColorEdit4("End Color", &targetEmitter->params.endColor.x);
	}

	if (ImGui::CollapsingHeader("Rotation", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::DragFloat3("Angular Velocity", &targetEmitter->params.angularVelocity.x, 0.01f);
		ImGui::DragFloat3("Angular Vel Variance", &targetEmitter->params.angularVelocityVariance.x, 0.01f);
	}

	if (ImGui::CollapsingHeader("Rendering", ImGuiTreeNodeFlags_DefaultOpen)) {
		char texBuf[256];
		strcpy_s(texBuf, targetEmitter->params.texturePath.c_str());
		if (ImGui::InputText("Texture Path", texBuf, sizeof(texBuf))) {
			targetEmitter->params.texturePath = texBuf;
		}

        char shaderBuf[256];
        strcpy_s(shaderBuf, targetEmitter->params.shaderName.c_str());
        if (ImGui::InputText("Shader Name", shaderBuf, sizeof(shaderBuf))) {
            targetEmitter->params.shaderName = shaderBuf;
        }

		ImGui::Checkbox("Use Billboard", &targetEmitter->params.useBillboard);
		ImGui::Checkbox("Additive Blending", &targetEmitter->params.isAdditive);

		// ★追加: UVアニメーション
		ImGui::Separator();
		ImGui::Checkbox("Use UV Animation", &targetEmitter->params.useUvAnim);
		if (targetEmitter->params.useUvAnim) {
			ImGui::DragInt("Columns", &targetEmitter->params.uvAnimCols, 0.1f, 1, 64);
			ImGui::DragInt("Rows", &targetEmitter->params.uvAnimRows, 0.1f, 1, 64);
			ImGui::DragFloat("Animation FPS", &targetEmitter->params.uvAnimFps, 0.1f, 0.1f, 120.0f);
		}
	}

	ImGui::End();
}

} // namespace Engine
