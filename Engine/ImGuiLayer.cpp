// Engine/ImGuiLayer.cpp
#include "ImGuiLayer.h"
#include "imgui_impl_dx12.h"
#include "imgui_impl_win32.h"
#include "imgui_internal.h"
#include <algorithm>
#include <cassert>
#include <cctype>
#include <cmath> // std::round用
#include <cstring>
#include <filesystem>
#include <string>

#include <DirectXMath.h>
#include <array>

#include "../Game/Actors/World.h"
#include "../Game/ObjectTypes.h"
#include "GameScene.h"
#include "Renderer.h"

#include "../Game/gimmick/GimmickBase.h"
#include "../Game/gimmick/GimmickFactory.h"

namespace Engine {

using namespace DirectX;

static std::string NormalizePath(const std::string& path) {
	std::string p = path;
	std::replace(p.begin(), p.end(), '\\', '/');
	return p;
}

// -----------------------------------------------------------------------------
// ヘルパー: フォルダーアイコンを描画する関数
// -----------------------------------------------------------------------------
static void DrawFolderIcon(const ImVec2& pMin, const ImVec2& pMax) {
	ImDrawList* drawList = ImGui::GetWindowDrawList();

	float w = pMax.x - pMin.x;
	float h = pMax.y - pMin.y;

	ImU32 colBody = IM_COL32(220, 180, 70, 255);
	ImU32 colTab = IM_COL32(200, 160, 60, 255);
	ImU32 colShadow = IM_COL32(0, 0, 0, 40);

	float tabW = w * 0.4f;
	float tabH = h * 0.2f;
	drawList->AddRectFilled(pMin, ImVec2(pMin.x + tabW, pMin.y + tabH + 2), colTab, 2.0f);

	ImVec2 bodyMin = ImVec2(pMin.x, pMin.y + tabH);
	drawList->AddRectFilled(ImVec2(bodyMin.x + 1, bodyMin.y + 1), ImVec2(pMax.x + 1, pMax.y + 1), colShadow, 3.0f);
	drawList->AddRectFilled(bodyMin, pMax, colBody, 3.0f);
}

// -----------------------------------------------------------------------------
// ヘルパー: ビューギズモ（View Cube）の描画と入力処理
// -----------------------------------------------------------------------------
static bool DrawViewGizmo(Game::GameScene* gameScene, const ImVec2& position, float size) {
	ImDrawList* drawList = ImGui::GetWindowDrawList();

	// マウスがギズモエリア（矩形）内にあるかチェック
	bool isGizmoHovered = ImGui::IsMouseHoveringRect(position, ImVec2(position.x + size, position.y + size));

	// カメラのView行列を取得
	const Engine::Camera& cam = gameScene->GetCamera();
	XMMATRIX view = cam.View();

	struct Axis {
		XMVECTOR dir;
		ImU32 color;
		ImU32 colorHover;
		const char* label;
		Game::CameraDirection type;
		float zOrder;
		ImVec2 screenPos;
	};

	std::array<Axis, 6> axes = {
	    {
         {XMVectorSet(1, 0, 0, 0), IM_COL32(255, 50, 50, 255), IM_COL32(255, 100, 100, 255), "X", Game::CameraDirection::Right},
         {XMVectorSet(-1, 0, 0, 0), IM_COL32(200, 50, 50, 128), IM_COL32(255, 100, 100, 200), "", Game::CameraDirection::Left},
         {XMVectorSet(0, 1, 0, 0), IM_COL32(50, 255, 50, 255), IM_COL32(100, 255, 100, 255), "Y", Game::CameraDirection::Top},
         {XMVectorSet(0, -1, 0, 0), IM_COL32(50, 200, 50, 128), IM_COL32(100, 255, 100, 200), "", Game::CameraDirection::Bottom},
         {XMVectorSet(0, 0, 1, 0), IM_COL32(50, 50, 255, 255), IM_COL32(100, 100, 255, 255), "Z", Game::CameraDirection::Back},
         {XMVectorSet(0, 0, -1, 0), IM_COL32(50, 50, 200, 128), IM_COL32(100, 100, 255, 200), "", Game::CameraDirection::Front},
	     }
    };

	ImVec2 center = ImVec2(position.x + size * 0.5f, position.y + size * 0.5f);
	float radius = size * 0.5f;

	for (auto& axis : axes) {
		XMVECTOR transformed = XMVector3TransformNormal(axis.dir, view);
		float x = XMVectorGetX(transformed);
		float y = XMVectorGetY(transformed);
		float z = XMVectorGetZ(transformed);

		axis.screenPos = ImVec2(center.x + x * radius, center.y - y * radius);
		axis.zOrder = z;
	}

	std::sort(axes.begin(), axes.end(), [](const Axis& a, const Axis& b) { return a.zOrder < b.zOrder; });

	for (const auto& axis : axes) {
		if (axis.zOrder < -0.1f)
			continue;
		drawList->AddLine(center, axis.screenPos, IM_COL32(255, 255, 255, 100), 2.0f);
	}

	float circleRadius = size * 0.15f;
	for (const auto& axis : axes) {
		ImVec2 p = axis.screenPos;

		bool hovered = false;
		ImVec2 mousePos = ImGui::GetMousePos();
		float dx = mousePos.x - p.x;
		float dy = mousePos.y - p.y;
		if (dx * dx + dy * dy < circleRadius * circleRadius) {
			hovered = true;
		}

		ImU32 col = hovered ? axis.colorHover : axis.color;
		drawList->AddCircleFilled(p, circleRadius, col);

		if (axis.label[0] != '\0') {
			ImVec2 textSize = ImGui::CalcTextSize(axis.label);
			drawList->AddText(ImVec2(p.x - textSize.x * 0.5f, p.y - textSize.y * 0.5f), IM_COL32(0, 0, 0, 255), axis.label);
		}

		if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
			gameScene->SetCameraDirection(axis.type);
		}
	}

	return isGizmoHovered;
}

// -----------------------------------------------------------------------------
// ディレクトリツリーを再帰的に描画する関数
// -----------------------------------------------------------------------------
static void DrawTreeRecursive(const std::filesystem::path& path, std::string& currentSelection) {
	namespace fs = std::filesystem;

	std::string label = path.filename().string();
	if (label.empty())
		label = path.string();

	ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth;

	bool isSelected = fs::equivalent(path, currentSelection);
	if (isSelected) {
		flags |= ImGuiTreeNodeFlags_Selected;
	}

	bool hasSubDir = false;
	try {
		for (const auto& entry : fs::directory_iterator(path)) {
			if (entry.is_directory()) {
				hasSubDir = true;
				break;
			}
		}
	} catch (...) {
	}

	if (!hasSubDir) {
		flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
	}

	bool isOpen = ImGui::TreeNodeEx(path.string().c_str(), flags, "");

	if (ImGui::IsItemClicked()) {
		currentSelection = NormalizePath(path.string());
	}

	ImGui::SameLine();

	float iconSize = ImGui::GetTextLineHeight();
	ImVec2 p = ImGui::GetCursorScreenPos();

	DrawFolderIcon(p, ImVec2(p.x + iconSize * 1.2f, p.y + iconSize));

	ImGui::SetCursorScreenPos(ImVec2(p.x + iconSize * 1.4f, p.y));
	ImGui::TextUnformatted(label.c_str());

	if (isOpen) {
		if (hasSubDir) {
			try {
				for (const auto& entry : fs::directory_iterator(path)) {
					if (entry.is_directory()) {
						DrawTreeRecursive(entry.path(), currentSelection);
					}
				}
			} catch (...) {
			}
		}
		if (hasSubDir) {
			ImGui::TreePop();
		}
	}
}

// -----------------------------------------------------------------------------
// ImGuiLayer 実装
// -----------------------------------------------------------------------------

bool ImGuiLayer::Initialize(
    HWND hwnd, WindowDX& dx, ID3D12DescriptorHeap* srvHeap, D3D12_CPU_DESCRIPTOR_HANDLE fontCpuHandle, D3D12_GPU_DESCRIPTOR_HANDLE fontGpuHandle, float jpFontSize, const char* jpFontPath) {

	(void)dx;
	(void)jpFontSize;
	(void)jpFontPath;

	if (fontGpuHandle.ptr == 0) {
		assert(false && "ImGui Font GPU Handle is INVALID.");
		return false;
	}

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
	ImGui::StyleColorsDark();

	ImGuiStyle& style = ImGui::GetStyle();
	style.WindowRounding = 0.0f;
	style.FrameRounding = 3.0f;
	style.GrabRounding = 3.0f;
	style.Colors[ImGuiCol_WindowBg] = ImVec4(0.15f, 0.15f, 0.15f, 1.0f);
	style.Colors[ImGuiCol_Header] = ImVec4(0.2f, 0.2f, 0.2f, 1.0f);
	style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.26f, 0.59f, 0.98f, 0.8f);
	style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.26f, 0.59f, 0.98f, 1.0f);
	style.Colors[ImGuiCol_Button] = ImVec4(0.25f, 0.25f, 0.25f, 1.0f);

	srvHeap_ = srvHeap;
	fontDefault_ = io.Fonts->AddFontDefault();

	if (!ImGui_ImplWin32_Init(hwnd))
		return false;
	if (!ImGui_ImplDX12_Init(dx.Dev(), WindowDX::kBackBufferCount, DXGI_FORMAT_R8G8B8A8_UNORM, srvHeap_, fontCpuHandle, fontGpuHandle))
		return false;

	{
		unsigned char* pixels;
		int width, height;
		io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
	}

	if (!ImGui_ImplDX12_CreateDeviceObjects())
		return false;

	return true;
}

void ImGuiLayer::NewFrame(WindowDX& dx) {
	ImGui_ImplDX12_NewFrame();
	ImGui_ImplWin32_NewFrame();

	ImGuiIO& io = ImGui::GetIO();

	RECT rect;
	GetClientRect(dx.GetHwnd(), &rect);
	float clientW = (float)(rect.right - rect.left);
	float clientH = (float)(rect.bottom - rect.top);

	if (clientW > 0 && clientH > 0) {
		float scaleX = (float)WindowDX::kW / clientW;
		float scaleY = (float)WindowDX::kH / clientH;
		io.DisplayFramebufferScale = ImVec2(scaleX, scaleY);
	}

	ImGui::NewFrame();
}

void ImGuiLayer::ShowEditorUI(D3D12_GPU_DESCRIPTOR_HANDLE sceneTextureHandle, Game::GameScene* gameScene) {

	(void)gameScene;

#ifdef _DEBUG

	Game::World* world = gameScene->GetWorld();
	std::vector<GameObject>& objects = world->GetObjects();
	GameObject* activeObj = gameScene->GetActiveObject();

	ImGuiIO& io = ImGui::GetIO();
	bool isCtrl = io.KeyCtrl;

	// ★追加: 複数オブジェクト配置用の構造
	struct PlacingItem {
		GameObject* obj;
		Engine::Vector3 offset; // 中心からのオフセット
	};
	static bool s_IsPlacingMode = false;
	static std::vector<PlacingItem> s_PlacingList; // 複製されたオブジェクトリスト
	static float s_PlacingBaseY = 0.0f;            // ★追加: 複製時の基準高さ

	// ---------------------------------------------------------
	// ショートカット: Ctrl+C (選択中のオブジェクトを一括複製 & 配置モードへ)
	// ---------------------------------------------------------
	if (isCtrl && ImGui::IsKeyPressed(ImGuiKey_C)) {
		s_PlacingList.clear();
		std::vector<GameObject*> selectedObjs;
		Engine::Vector3 centerPos = {0, 0, 0};

		// 1. 選択中のオブジェクトを収集
		for (auto& obj : world->GetObjects()) {
			if (gameScene->IsSelected(&obj)) {
				selectedObjs.push_back(&obj);
				centerPos = centerPos + obj.transform.translate;
			}
		}

		if (!selectedObjs.empty()) {
			// 2. 中心点(重心)を計算
			float invSize = 1.0f / (float)selectedObjs.size();
			centerPos = centerPos * invSize;

			// ★修正: 配置時の高さを記憶する
			s_PlacingBaseY = centerPos.y;

			// 3. 全て複製し、リストに追加
			for (auto* src : selectedObjs) {
				GameObject* newObj = world->DuplicateObject(src);
				if (newObj) {
					PlacingItem item;
					item.obj = newObj;
					// 中心からの相対座標を記録
					item.offset = newObj->transform.translate - centerPos;
					s_PlacingList.push_back(item);
				}
			}

			// 4. 新しいオブジェクトを選択状態にする(既存選択はクリア)
			gameScene->ClearSelection();
			for (auto& item : s_PlacingList) {
				gameScene->SelectObject(item.obj, true);
			}

			// 5. 配置モード有効化
			s_IsPlacingMode = true;
		}
	}

	// --- DockSpace ---
	ImGuiViewport* viewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(viewport->Pos);
	ImGui::SetNextWindowSize(viewport->Size);
	ImGui::SetNextWindowViewport(viewport->ID);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	ImGui::Begin(
	    "DockSpace Base", nullptr,
	    ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
	        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus);
	ImGui::PopStyleVar(3);
	ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
	ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);

	static bool firstRun = true;
	if (firstRun) {
		firstRun = false;
		ImGui::DockBuilderRemoveNode(dockspace_id);
		ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
		ImGui::DockBuilderSetNodeSize(dockspace_id, viewport->Size);

		ImGuiID dock_main_id = dockspace_id;
		ImGuiID dock_right_id = ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Right, 0.25f, nullptr, &dock_main_id);
		ImGuiID dock_left_id = ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Left, 0.2f, nullptr, &dock_main_id);
		ImGuiID dock_bottom_id = ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Down, 0.3f, nullptr, &dock_main_id);

		ImGui::DockBuilderDockWindow("Hierarchy", dock_left_id);
		ImGui::DockBuilderDockWindow("Assets", dock_bottom_id);
		ImGui::DockBuilderDockWindow("Inspector", dock_right_id);
		ImGui::DockBuilderDockWindow("Game Scene", dock_main_id);
		ImGui::DockBuilderFinish(dockspace_id);
	}

	if (ImGui::BeginMenuBar()) {
		if (ImGui::BeginMenu("File")) {
			static char stageNameBuf[128] = "level_data.csv";
			ImGui::InputText("Stage Name", stageNameBuf, IM_ARRAYSIZE(stageNameBuf));

			if (ImGui::MenuItem("Save Level")) {
				std::string path = "Resources/" + std::string(stageNameBuf);
				if (path.find(".csv") == std::string::npos)
					path += ".csv";
				world->Save(path);
			}
			if (ImGui::MenuItem("Load Level")) {
				std::string path = "Resources/" + std::string(stageNameBuf);
				if (path.find(".csv") == std::string::npos)
					path += ".csv";
				world->Load(path);
			}
			ImGui::EndMenu();
		}
		ImGui::EndMenuBar();
	}
	ImGui::End();

	// ---------------------------------------------------------
	//  Project Window (Assets)
	// ---------------------------------------------------------
	ImGui::Begin("Assets");
	{
		namespace fs = std::filesystem;
		if (!fs::exists(currentDir_) || !fs::is_directory(currentDir_))
			currentDir_ = "Resources";

		WindowDX::s_DropDirectory = currentDir_;

		ImGui::Columns(2, "ProjectLayout", true);

		static bool initWidth = true;
		if (initWidth) {
			ImGui::SetColumnWidth(0, 200.0f);
			initWidth = false;
		}

		if (ImGui::GetColumnIndex() == 0) {
			ImGui::BeginChild("TreePane", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
			DrawTreeRecursive("Resources", currentDir_);
			ImGui::EndChild();
		}

		ImGui::NextColumn();

		if (ImGui::GetColumnIndex() == 1) {
			ImGui::BeginChild("FilePane", ImVec2(0, 0), false);

			ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "%s", currentDir_.c_str());
			ImGui::Separator();

			float padding = 10.0f;
			float thumbnailSize = 64.0f;
			float cellSize = thumbnailSize + padding;
			float panelWidth = ImGui::GetContentRegionAvail().x;
			int columnCount = (int)(panelWidth / cellSize);
			if (columnCount < 1)
				columnCount = 1;

			int currentColumn = 0;

			for (const auto& entry : fs::directory_iterator(currentDir_)) {
				const auto& path = entry.path();
				std::string filename = path.filename().string();
				std::string pathStr = NormalizePath(path.string());

				ImGui::PushID(filename.c_str());
				ImGui::BeginGroup();

				bool isDir = entry.is_directory();
				std::string ext = path.extension().string();
				for (auto& c : ext)
					c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

				bool isObj = (ext == ".obj");
				bool isImg = (ext == ".png" || ext == ".jpg" || ext == ".bmp");

				ImVec2 cursorStart = ImGui::GetCursorScreenPos();
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, 1, 1, 0.1f));
				ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1, 1, 1, 0.2f));

				if (ImGui::Button("##Item", ImVec2(thumbnailSize, thumbnailSize + 20))) {
					if (isDir) {
						currentDir_ = pathStr;
					}
				}
				ImGui::PopStyleColor(3);

				if (isObj && ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
					std::string relPath = fs::relative(path, "Resources").string();
					relPath = NormalizePath(relPath);
					char nameBuf[256];
					strcpy_s(nameBuf, relPath.c_str());
					ImGui::SetDragDropPayload("DND_FILE_OBJ", nameBuf, sizeof(nameBuf));
					ImGui::Text("Placing %s", filename.c_str());
					ImGui::EndDragDropSource();
				}

				ImDrawList* drawList = ImGui::GetWindowDrawList();
				float iconPad = 10.0f;
				ImVec2 iMin = ImVec2(cursorStart.x + iconPad, cursorStart.y + iconPad);
				ImVec2 iMax = ImVec2(cursorStart.x + thumbnailSize - iconPad, cursorStart.y + thumbnailSize - iconPad);

				if (isDir) {
					DrawFolderIcon(iMin, iMax);
				} else if (isObj) {
					ImU32 boxCol = IM_COL32(80, 140, 240, 255);
					ImU32 topCol = IM_COL32(120, 180, 255, 255);
					float w = iMax.x - iMin.x;
					float cx = iMin.x + w * 0.5f;
					float cy = iMin.y + w * 0.6f;
					float r = w * 0.4f;
					ImVec2 p[6];
					for (int k = 0; k < 6; k++) {
						float ang = 3.14159f / 6.0f + k * 3.14159f / 3.0f;
						p[k] = ImVec2(cx + cos(ang) * r, cy + sin(ang) * r * 0.9f);
					}
					drawList->AddConvexPolyFilled(p, 6, boxCol);
					ImVec2 top[4] = {ImVec2(cx, cy), p[4], p[5], p[0]};
					drawList->AddConvexPolyFilled(top, 4, topCol);
					drawList->AddText(ImVec2(cx - 10, cy - 5), IM_COL32(255, 255, 255, 200), "3D");
				} else if (isImg) {
					ImU32 col = IM_COL32(180, 100, 220, 255);
					drawList->AddRectFilled(iMin, iMax, col, 3.0f);
					drawList->AddText(ImVec2(iMin.x + 5, iMin.y + 15), IM_COL32(255, 255, 255, 200), "IMG");
				} else {
					ImU32 col = IM_COL32(180, 180, 180, 255);
					float fold = 10.0f;
					ImVec2 pts[] = {iMin, ImVec2(iMax.x - fold, iMin.y), ImVec2(iMax.x, iMin.y + fold), iMax, ImVec2(iMin.x, iMax.y)};
					drawList->AddConvexPolyFilled(pts, 5, col);
					ImVec2 tri[] = {ImVec2(iMax.x - fold, iMin.y), ImVec2(iMax.x - fold, iMin.y + fold), ImVec2(iMax.x, iMin.y + fold)};
					drawList->AddConvexPolyFilled(tri, 3, IM_COL32(140, 140, 140, 255));
				}

				ImGui::SetCursorScreenPos(ImVec2(cursorStart.x, cursorStart.y + thumbnailSize));
				std::string showName = filename;
				if (showName.length() > 9)
					showName = showName.substr(0, 7) + "..";

				float txtW = ImGui::CalcTextSize(showName.c_str()).x;
				float txtX = cursorStart.x + (thumbnailSize - txtW) * 0.5f;
				if (txtX < cursorStart.x)
					txtX = cursorStart.x;

				ImGui::SetCursorScreenPos(ImVec2(txtX, cursorStart.y + thumbnailSize));
				ImGui::Text("%s", showName.c_str());

				ImGui::EndGroup();
				ImGui::PopID();

				currentColumn++;
				if (currentColumn < columnCount) {
					ImGui::SameLine();
				} else {
					currentColumn = 0;
				}
			}

			ImGui::EndChild();
		}

		ImGui::Columns(1);
	}
	ImGui::End();
#endif

	// --- Game Scene ---
#ifdef _DEBUG
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
	ImGui::Begin("Game Scene");
#else
	ImGuiViewport* viewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(viewport->Pos);
	ImGui::SetNextWindowSize(viewport->Size);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	ImGui::Begin(
	    "Game Scene", nullptr,
	    ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNav);
#endif

	{
		ImVec2 winPos = ImGui::GetWindowPos();
		ImVec2 winSize = ImGui::GetContentRegionAvail();

		if (sceneTextureHandle.ptr != 0)
			ImGui::Image((ImTextureID)sceneTextureHandle.ptr, winSize);
		else
			ImGui::Text("No Scene Texture");

#ifdef _DEBUG
		bool isGizmoHovered = false;
		if (gameScene->IsEditorMode()) {
			// ビューギズモの描画
			float gizmoSize = 80.0f;
			float padding = 10.0f;
			ImVec2 gizmoPos;
			gizmoPos.x = winPos.x + winSize.x - gizmoSize - padding;
			gizmoPos.y = winPos.y + padding + 20.0f;

			isGizmoHovered = DrawViewGizmo(gameScene, gizmoPos, gizmoSize);
		}

		// ★追加: 3Dシーン上でのマウスインタラクション（配置・削除）
		if (gameScene->IsEditorMode()) {
			bool isSceneHovered = ImGui::IsItemHovered() && !isGizmoHovered;

			ImVec2 mousePos = ImGui::GetMousePos();
			ImVec2 winRectMin = ImGui::GetItemRectMin();
			ImVec2 winRectSize = ImGui::GetItemRectSize();
			float u = (mousePos.x - winRectMin.x) / winRectSize.x;
			float v = (mousePos.y - winRectMin.y) / winRectSize.y;

			// --- 1. 配置モードの処理 (複製オブジェクトをマウスに追従) ---
			if (s_IsPlacingMode && !s_PlacingList.empty()) {
				if (isSceneHovered) {
					const Engine::Camera& cam = gameScene->GetCamera();
					XMMATRIX proj = cam.Proj();
					XMMATRIX view = cam.View();
					XMMATRIX invViewProj = XMMatrixInverse(nullptr, view * proj);

					float ndcX = u * 2.0f - 1.0f;
					float ndcY = 1.0f - v * 2.0f;
					XMVECTOR nearPoint = XMVector3TransformCoord(XMVectorSet(ndcX, ndcY, 0.0f, 1.0f), invViewProj);
					XMVECTOR farPoint = XMVector3TransformCoord(XMVectorSet(ndcX, ndcY, 1.0f, 1.0f), invViewProj);
					XMVECTOR dirVec = XMVector3Normalize(farPoint - nearPoint);

					Engine::Vector3 origin = {XMVectorGetX(nearPoint), XMVectorGetY(nearPoint), XMVectorGetZ(nearPoint)};
					Engine::Vector3 direction = {XMVectorGetX(dirVec), XMVectorGetY(dirVec), XMVectorGetZ(dirVec)};

					Engine::Vector3 targetPos = origin;

					// ★修正: レイキャスト(CastRay)ではなく、Y平面との交差判定を行う
					// これにより、自分自身や他のオブジェクトにレイが当たって高さが変わる(ガクガクする)のを防ぎ
					// 常にコピー元の高さを維持(Plane Intersection)します。
					if (std::abs(direction.y) > 0.0001f) {
						// 平面方程式: y = s_PlacingBaseY
						// origin.y + t * direction.y = s_PlacingBaseY
						float t = (s_PlacingBaseY - origin.y) / direction.y;
						if (t > 0.0f) {
							targetPos = origin + direction * t;
						} else {
							// カメラの後ろにある場合などのフォールバック
							targetPos = origin + direction * 15.0f;
							targetPos.y = s_PlacingBaseY;
						}
					} else {
						// 水平に近い視線の場合
						targetPos = origin + direction * 15.0f;
						targetPos.y = s_PlacingBaseY;
					}

					// ★修正: グリッドスナップ (X/Zのみ適用、Yは高さを維持)
					float gridSize = 1.0f;
					targetPos.x = std::round(targetPos.x / gridSize) * gridSize;
					targetPos.z = std::round(targetPos.z / gridSize) * gridSize;
					targetPos.y = s_PlacingBaseY; // Yは強制的に元の高さに合わせる

					// オフセットを適用して全オブジェクトを移動
					for (auto& item : s_PlacingList) {
						item.obj->transform.translate = targetPos + item.offset;
					}

					// 左クリックで配置確定
					if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
						s_IsPlacingMode = false;
						s_PlacingList.clear();
					}
				}
				// 右クリックでキャンセル（削除）
				if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
					for (auto& item : s_PlacingList) {
						world->DeleteObject(item.obj);
					}
					s_PlacingList.clear();
					gameScene->ClearSelection();
					s_IsPlacingMode = false;
				}
			}
			// --- 2. 配置モードでなければ通常の操作 ---
			else {
				// ★追加: 3D画面での Ctrl + 右クリック削除
				if (isSceneHovered && isCtrl && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
					const Engine::Camera& cam = gameScene->GetCamera();
					XMMATRIX proj = cam.Proj();
					XMMATRIX view = cam.View();
					XMMATRIX invViewProj = XMMatrixInverse(nullptr, view * proj);

					float ndcX = u * 2.0f - 1.0f;
					float ndcY = 1.0f - v * 2.0f;
					XMVECTOR nearPoint = XMVector3TransformCoord(XMVectorSet(ndcX, ndcY, 0.0f, 1.0f), invViewProj);
					XMVECTOR farPoint = XMVector3TransformCoord(XMVectorSet(ndcX, ndcY, 1.0f, 1.0f), invViewProj);
					XMVECTOR dirVec = XMVector3Normalize(farPoint - nearPoint);

					Engine::Vector3 origin = {XMVectorGetX(nearPoint), XMVectorGetY(nearPoint), XMVectorGetZ(nearPoint)};
					Engine::Vector3 direction = {XMVectorGetX(dirVec), XMVectorGetY(dirVec), XMVectorGetZ(dirVec)};

					float dist;
					GameObject* hitObj = world->CastRay(origin, direction, dist, true);
					if (hitObj && !hitObj->isLocked) {
						world->DeleteObject(hitObj);
						gameScene->ClearSelection(); // 選択情報の不整合を防ぐためクリア
					}
				}

				// 既存の選択・操作処理
				gameScene->EditorUpdate(isSceneHovered, u * 2.0f - 1.0f, 1.0f - v * 2.0f);
			}
		}

		if (ImGui::BeginDragDropTarget()) {
			ImVec2 mousePos = ImGui::GetMousePos();
			ImVec2 winRectMin = ImGui::GetItemRectMin();
			ImVec2 winRectSize = ImGui::GetItemRectSize();
			float u = (mousePos.x - winRectMin.x) / winRectSize.x;
			float v = (mousePos.y - winRectMin.y) / winRectSize.y;

			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DND_BLOCK_TYPE")) {
				Game::ObjectType type = *(const Game::ObjectType*)payload->Data;
				gameScene->SpawnBlockAtNDC(type, u * 2.0f - 1.0f, 1.0f - v * 2.0f);
			}
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DND_FILE_OBJ")) {
				const char* filename = (const char*)payload->Data;
				gameScene->SpawnModelAtNDC(filename, u * 2.0f - 1.0f, 1.0f - v * 2.0f);
			}
			ImGui::EndDragDropTarget();
		}
#endif
	}
	ImGui::End();

#ifdef _DEBUG
	ImGui::PopStyleVar();
#else
	ImGui::PopStyleVar(3); // Padding, Rounding, BorderSize
#endif

	// --- Hierarchy ---
#ifdef _DEBUG
	ImGui::Begin("Hierarchy");
	{
		int deleteIndex = -1;
		for (int i = 0; i < (int)objects.size(); ++i) {
			int flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_Leaf;

			if (gameScene->IsSelected(&objects[i]))
				flags |= ImGuiTreeNodeFlags_Selected;

			if (objects[i].isLocked) {
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
			}

			bool open = ImGui::TreeNodeEx((void*)(intptr_t)i, flags, objects[i].name.c_str());

			if (objects[i].isLocked) {
				ImGui::PopStyleColor();
			}

			// ヒエラルキー上での Ctrl + 右クリック削除
			if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && ImGui::GetIO().KeyCtrl) {
				if (!objects[i].isLocked) {
					deleteIndex = i;
				}
			}

			if (ImGui::IsItemClicked()) {
				bool isCtrlKey = ImGui::GetIO().KeyCtrl;
				gameScene->SelectObject(&objects[i], isCtrlKey);
			}
			if (ImGui::BeginPopupContextItem()) {
				if (!objects[i].isLocked) {
					if (ImGui::MenuItem("Delete"))
						deleteIndex = i;
				} else {
					ImGui::TextDisabled("Delete (Locked)");
				}
				ImGui::EndPopup();
			}
			if (open)
				ImGui::TreePop();
		}
		if (deleteIndex != -1) {
			Engine::GameObject* ptr = &objects[deleteIndex];
			if (gameScene->IsSelected(ptr)) {
				gameScene->RemoveFromSelection(ptr);
			}

			// 削除されたオブジェクトが配置リストにあれば除去
			for (auto it = s_PlacingList.begin(); it != s_PlacingList.end();) {
				if (it->obj == ptr) {
					it = s_PlacingList.erase(it);
				} else {
					++it;
				}
			}
			if (s_PlacingList.empty()) {
				s_IsPlacingMode = false;
			}

			world->DeleteObject(ptr);
		}
		if (ImGui::IsMouseDown(0) && ImGui::IsWindowHovered()) {
			gameScene->ClearSelection();
		}
	}
	ImGui::End();

	// --- Inspector ---
	ImGui::Begin("Inspector");
	{
		if (activeObj) {
			GameObject& obj = *activeObj;

			ImGui::Checkbox("Lock Object", &obj.isLocked);
			ImGui::SameLine();
			ImGui::Checkbox("Mesh Collision", &obj.useMeshCollision);

			ImGui::Separator();

			char buf[256];
			strcpy_s(buf, obj.name.c_str());
			if (ImGui::InputText("Name", buf, sizeof(buf)))
				obj.name = buf;
			ImGui::Separator();

			if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
				if (obj.isLocked) {
					ImGui::BeginDisabled();
				}

				ImGui::DragFloat3("Position", &obj.transform.translate.x, 0.1f);
				ImGui::DragFloat3("Rotation", &obj.transform.rotate.x, 0.01f);
				ImGui::DragFloat3("Scale", &obj.transform.scale.x, 0.1f);

				if (obj.isLocked) {
					ImGui::EndDisabled();
				}
			}

			if (ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen)) {
				ImGui::ColorEdit4("Color", &obj.color.x);

				std::string currentTex = obj.textureName.empty() ? "(Default)" : obj.textureName;
				if (ImGui::BeginCombo("Texture", currentTex.c_str())) {
					if (ImGui::Selectable("(Default)", obj.textureName.empty())) {
						obj.textureName = "";
						obj.textureHandle = Renderer::GetInstance()->LoadTexture2D("Resources/uvChecker.png");
					}
					namespace fs = std::filesystem;
					if (fs::exists("Resources")) {
						for (const auto& entry : fs::directory_iterator("Resources")) {
							if (!entry.is_regular_file())
								continue;
							std::string fname = entry.path().filename().string();
							std::string ext = entry.path().extension().string();
							for (auto& c : ext)
								c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

							if (ext == ".png" || ext == ".jpg" || ext == ".bmp" || ext == ".tga") {
								bool isSelected = (obj.textureName == fname);
								if (ImGui::Selectable(fname.c_str(), isSelected)) {
									obj.textureName = fname;
									std::string path = "Resources/" + fname;
									uint32_t h = Renderer::GetInstance()->LoadTexture2D(path);
									if (h != 0)
										obj.textureHandle = h;
								}
								if (isSelected)
									ImGui::SetItemDefaultFocus();
							}
						}
					}
					ImGui::EndCombo();
				}

				Engine::Renderer* renderer = Engine::Renderer::GetInstance();
				const auto& shaderNames = renderer->GetShaderNames();

				if (ImGui::BeginCombo("Shader", obj.shaderName.c_str())) {
					for (const auto& name : shaderNames) {
						bool isSelected = (obj.shaderName == name);
						if (ImGui::Selectable(name.c_str(), isSelected)) {
							obj.shaderName = name;
						}
						if (isSelected) {
							ImGui::SetItemDefaultFocus();
						}
					}
					ImGui::EndCombo();
				}
			}

			if (ImGui::CollapsingHeader("Gimmick Component", ImGuiTreeNodeFlags_DefaultOpen)) {
				if (obj.gimmick != nullptr) {
					ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "Attached: %s", obj.gimmickName.c_str());

					ImGui::Indent();
					obj.gimmick->OnInspectorGUI();
					ImGui::Unindent();

					ImGui::Separator();
					if (ImGui::Button("Remove Component", ImVec2(-1, 0))) {
						delete obj.gimmick;
						obj.gimmick = nullptr;
						obj.gimmickName = "";
					}
				} else {
					static int selectedGimmickIndex = 0;
					const auto& names = Game::GimmickFactory::Instance().GetGimmickNames();

					if (!names.empty()) {
						if (selectedGimmickIndex >= (int)names.size())
							selectedGimmickIndex = 0;

						std::string comboLabel = names[selectedGimmickIndex];
						if (ImGui::BeginCombo("Type", comboLabel.c_str())) {
							for (int n = 0; n < (int)names.size(); n++) {
								bool is_selected = (selectedGimmickIndex == n);
								if (ImGui::Selectable(names[n].c_str(), is_selected)) {
									selectedGimmickIndex = n;
								}
								if (is_selected) {
									ImGui::SetItemDefaultFocus();
								}
							}
							ImGui::EndCombo();
						}

						if (ImGui::Button("Add Component", ImVec2(-1, 0))) {
							std::string typeName = names[selectedGimmickIndex];
							Game::GimmickBase* newGimmick = Game::GimmickFactory::Instance().Create(typeName);
							if (newGimmick) {
								newGimmick->Start(&obj);
								obj.gimmick = newGimmick;
								obj.gimmickName = typeName;
							}
						}
					} else {
						ImGui::TextDisabled("No Gimmicks Registered");
					}
				}
			}

		} else {
			ImGui::Text("No object selected.");
		}
	}
	ImGui::End();
#endif
}

void ImGuiLayer::Render(WindowDX& dx) {
	ImGui::Render();
	if (srvHeap_) {
		ID3D12DescriptorHeap* heaps[] = {srvHeap_};
		dx.List()->SetDescriptorHeaps(1, heaps);
	}
	ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), dx.List());
}

void ImGuiLayer::Shutdown() {
	ImGui_ImplDX12_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();
	srvHeap_ = nullptr;
}

} // namespace Engine