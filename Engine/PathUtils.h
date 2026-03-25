#pragma once
#include <string>
#include <filesystem>
#include <windows.h>
#include <algorithm>

namespace Engine {

class PathUtils {
public:
    static std::string GetUnifiedPath(const std::string& relPath) {
        if (relPath.empty()) return "";
        std::filesystem::path p(relPath);
        if (p.is_absolute()) return relPath;
        std::string root = GetRootPath();
        std::filesystem::path finalPath = std::filesystem::path(root) / relPath;
        std::string result = finalPath.string();
        std::replace(result.begin(), result.end(), '\\', '/');
        return result;
    }

    static std::wstring GetUnifiedPathW(const std::wstring& relPath) {
        if (relPath.empty()) return L"";
        std::filesystem::path p(relPath);
        if (p.is_absolute()) return relPath;
        std::string root = GetRootPath();
        std::filesystem::path finalPath = std::filesystem::path(root) / relPath;
        std::wstring result = finalPath.wstring();
        std::replace(result.begin(), result.end(), L'\\', L'/');
        return result;
    }

private:
    // プロジェクト指標ファイルの存在チェック (.sln or .vcxproj)
    static bool HasProjectFile(const std::filesystem::path& dir) {
        try {
            // 固定名チェック (高速)
            if (std::filesystem::exists(dir / "DirectXGame_New.sln")) return true;
            if (std::filesystem::exists(dir / "DirectXGameApp.vcxproj")) return true;
            // 任意の .sln ファイル (フォールバック)
            for (auto& entry : std::filesystem::directory_iterator(dir)) {
                if (entry.path().extension() == ".sln") return true;
            }
        } catch (...) {}
        return false;
    }

    static std::string GetRootPath() {
        static std::string rootPath = "";
        if (rootPath.empty()) {
            // Unicode版を使用 (日本語パス「デスクトップ」等の文字化け防止)
            wchar_t buffer[MAX_PATH];
            GetModuleFileNameW(NULL, buffer, MAX_PATH);
            std::filesystem::path exeDir = std::filesystem::path(buffer).parent_path();
            
            // ビルド出力ディレクトリに含まれがちな名前
            auto IsBuildFolder = [](const std::string& name) {
                std::string lower = name;
                std::transform(lower.begin(), lower.end(), lower.begin(), 
                    [](unsigned char c){ return (char)std::tolower(c); });
                return lower == "generated" || lower == "outputs" || lower == "development" ||
                       lower == "bin" || lower == "obj" || lower == "x64" || lower == "debug" || lower == "release";
            };

            // Phase 1: プロジェクトファイル (.sln / .vcxproj / .git) を最優先で探す
            std::filesystem::path current = exeDir;
            bool found = false;
            for (int i = 0; i < 10; ++i) {
                // ビルドフォルダ自体はルートパスの候補にしない（ソースディレクトリを見つけるため）
                if (!IsBuildFolder(current.filename().string())) {
                    if (HasProjectFile(current) || std::filesystem::exists(current / ".git")) {
                        rootPath = current.string();
                        found = true;
                        break;
                    }
                    // 子フォルダ TD_Engine 内のチェック (並列フォルダ対策)
                    if (std::filesystem::exists(current / "TD_Engine") && 
                        (HasProjectFile(current / "TD_Engine") || std::filesystem::exists(current / "TD_Engine" / ".git"))) {
                        rootPath = (current / "TD_Engine").string();
                        found = true;
                        break;
                    }
                }
                if (current.has_parent_path() && current.parent_path() != current) {
                    current = current.parent_path();
                } else {
                    break;
                }
            }
            
            // Phase 2: 配布環境用フォールバック (Resources/shaders/ToonPS.hlsl の存在を確認)
            if (!found) {
                current = exeDir;
                for (int i = 0; i < 10; ++i) {
                    // ここではビルドフォルダ内でも、完全なリソースがあれば許容する
                    if (std::filesystem::exists(current / "Resources" / "shaders" / "ToonPS.hlsl")) {
                        rootPath = current.string();
                        found = true;
                        break;
                    }
                    if (current.has_parent_path() && current.parent_path() != current) {
                        current = current.parent_path();
                    } else {
                        break;
                    }
                }
            }
            
            if (!found) {
                rootPath = exeDir.string();
            }
            
            // デバッグ出力
            OutputDebugStringA(("[PathUtils] Root path resolved to: " + rootPath + "\n").c_str());
        }
        return rootPath;
    }
};

} // namespace Engine
