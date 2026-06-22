#pragma once
#include "Localization.h"
#include <filesystem>
#include <imgui.h>
#include <imgui_impl_sdlrenderer2.h>
#include <iostream>
#include <string>
#include <vector>

// -----------------------------------------------------------------------------
// 辅助：通用范围定义
// -----------------------------------------------------------------------------
inline const ImWchar* GetCJKRanges() {
	static const ImWchar ranges[] = {
		0x0020,
		0x00FF, // Basic Latin + Latin Supplement
		0x2000,
		0x206F, // General Punctuation
		0x3000,
		0x30FF, // CJK Symbols and Punctuations, Hiragana, Katakana
		0x31F0,
		0x31FF, // Katakana Phonetic Extensions
		0xFF00,
		0xFFEF, // Half-width characters
		0x4e00,
		0x9FAF, // CJK Ideograms
		0,
	};
	return &ranges[0];
}

// -----------------------------------------------------------------------------
// 辅助：在路径列表中寻找第一个存在的字体文件
// -----------------------------------------------------------------------------
inline std::string FindBestFont(const std::vector<std::string>& candidates) {
	for (const auto& path : candidates) {
		if (std::filesystem::exists(path)) {
			return path;
		}
	}
	return "";
}

inline void AddFontsFromDir(std::vector<std::string>& candidates, const std::string& dir) {
	if (!std::filesystem::exists(dir))
		return;

	for (auto& entry : std::filesystem::directory_iterator(dir)) {
		if (!entry.is_regular_file())
			continue;

		auto path = entry.path().string();
		auto ext = entry.path().extension().string();

		if (ext == ".ttf" || ext == ".otf") {
			candidates.push_back(path); // append OK
		}
	}
}

// -----------------------------------------------------------------------------
// 获取等宽字体 (Monospace Font) - 核心修正
// -----------------------------------------------------------------------------
inline std::string GetMonospaceFontPath() {
	std::vector<std::string> candidates;

#ifdef _WIN32
	// Windows 等宽字体回退链
	candidates = {
		// 1. Cascadia (Win10/11 Terminal 默认字体，极其适合代码)
		"C:\\Windows\\Fonts\\CascadiaMono.ttf",
		"C:\\Windows\\Fonts\\CascadiaCode.ttf",
		// 2. Consolas (经典的编程字体)
		"C:\\Windows\\Fonts\\Consola.ttf",
		// 3. Courier New (最后的兜底)
		"C:\\Windows\\Fonts\\cour.ttf"};
#elif defined(__ANDROID__)
	candidates = {
		"/system/fonts/DroidSansMono.ttf",
		"/system/fonts/NotoSansMono-Regular.ttf"};
#else // Linux / Unix
	// Linux 等宽字体非常多，这里列出主流发行版的默认项
	candidates = {
		// Ubuntu / Debian
		"/usr/share/fonts/truetype/ubuntu/UbuntuMono-R.ttf",
		"/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
		"/usr/share/fonts/liberation/LiberationMono-Regular.ttf",
		// Fedora / Arch / Others
		"/usr/share/fonts/noto/NotoMono-Regular.ttf",
		"/usr/share/fonts/TTF/DejaVuSansMono.ttf",
		"/usr/share/fonts/gnu-free/FreeMono.ttf"};
#endif

	return FindBestFont(candidates);
}

// -----------------------------------------------------------------------------
// 获取 CJK 字体
// 注意：即使是代码环境，CJK 字体通常也可以使用非严格等宽的黑体（Gothic），
// 因为 ImGui 会处理字符宽度，但最好还是选字形工整的。
// -----------------------------------------------------------------------------
inline std::string GetCJKFontPath() {
	auto preference = "Localization.CJKPreference"_l;
	std::vector<std::string> candidates;

#ifdef _WIN32
	if (preference == "JP") {
		candidates = {
			"C:\\Windows\\Fonts\\msgothic.ttc", // MS Gothic 是日文等宽的首选
			"C:\\Windows\\Fonts\\YuGothM.ttc",
			"C:\\Windows\\Fonts\\meiryo.ttc"};
	}
	else if (preference == "KR") {
		candidates = {
			"C:\\Windows\\Fonts\\malgun.ttf",  // Malgun Gothic (맑은 고딕)
			"C:\\Windows\\Fonts\\gulim.ttc",   // Gulim (굴림)
			"C:\\Windows\\Fonts\\batang.ttc"   // Batang (바탕)
		};
	}
	else {
		candidates = {
			"C:\\Windows\\Fonts\\msyh.ttc",	  // 雅黑
			"C:\\Windows\\Fonts\\simhei.ttf", // 黑体 (较粗，但在某些低分屏上可读性好)
			"C:\\Windows\\Fonts\\simsun.ttc"  // 宋体 (最传统)
		};
	}
#elif defined(__ANDROID__)
	candidates = {
		"/system/fonts/NotoSansCJK-Regular.ttc",
		"/system/fonts/DroidSansFallback.ttf"};
#elif defined(IOS)
	// iOS sandboxes the app away from any system CJK font files, so we ship
	// our own subset Noto Sans fonts under ./fonts_cjk/ (copied into the
	// writable Documents dir at startup, see casioemu.cpp). This is kept
	// SEPARATE from ./fonts/ on purpose: ./fonts/ is auto-scanned and every
	// file in it gets merged into the base font with the full glyph range
	// (see AddFontsFromDir below) — dumping multi-MB CJK/Hangul fonts in
	// there would force-rasterize tens of thousands of glyphs on every
	// launch regardless of language settings, which is expensive enough on
	// mobile to risk the launch watchdog killing the app. Keeping them in a
	// dedicated folder means they're only loaded here, on demand.
	// Korean needs its own font since Hangul isn't covered by the Simplified
	// Chinese subset; Japanese shares the SC font because its common Kanji +
	// Kana glyphs are covered.
	if (preference == "KR") {
		candidates = {"./fonts_cjk/NotoSansKR-Regular.otf"};
	}
	else {
		candidates = {"./fonts_cjk/NotoSansSC-Regular.otf"};
	}
#else // Linux
	// 尝试寻找 CJK 的 Mono 版本 (如果有)，否则使用 Regular
	candidates = {
		"/usr/share/fonts/noto-cjk/NotoSansCJK-Mono.ttc", // 最佳：Noto 的等宽版本
		"/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc",
		"/usr/share/fonts/truetype/wqy/wqy-microhei.ttc",
		"/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc",
		"/usr/share/fonts/google-noto-cjk/NotoSansCJK-Regular.ttc",
		"/usr/share/fonts/droid/DroidSansFallbackFull.ttf"};
#endif

	return FindBestFont(candidates);
}

// -----------------------------------------------------------------------------
// 获取泰文字体 (Thai Font)
// Most system "monospace"/CJK fonts used as the base font do not include Thai
// glyphs, so Thai needs its own dedicated font merged into the atlas.
// -----------------------------------------------------------------------------
inline std::string GetThaiFontPath() {
	std::vector<std::string> candidates;

#ifdef _WIN32
	candidates = {
		"C:\\Windows\\Fonts\\leelawad.ttf", // Leelawadee — Windows' default Thai font
		"C:\\Windows\\Fonts\\tahoma.ttf"};	 // Tahoma has Thai glyphs as a fallback
#elif defined(__ANDROID__)
	candidates = {
		"/system/fonts/NotoSansThai-Regular.ttf",
		"/system/fonts/DroidSansThai.ttf"};
#elif defined(IOS)
	candidates = {"./fonts_cjk/NotoSansThai-Regular.ttf"};
#else // Linux
	candidates = {
		"/usr/share/fonts/truetype/noto/NotoSansThai-Regular.ttf",
		"/usr/share/fonts/thai-scalable/Garuda.ttf",
		"/usr/share/fonts/truetype/tlwg/Garuda.ttf"};
#endif

	return FindBestFont(candidates);
}

inline const ImWchar* GetGlobalRanges() {
    static const ImWchar ranges[] = {
        // Basics (ASCII + symbols)
        0x0001, 0x1fff,

        // Vietnamese + Latin Extended

        // General punctuation
        0x2000, 0x206F,

        // CJK (Chinese / Japanese basic)
        0x3000, 0x30FF,
        0x31F0, 0x31FF,
        0x4E00, 0x9FFF,

        // Emoji (IMPORTANT)
        //0x1F300, 0x1FAFF,

        0
    };
    return ranges;
}

// Theme/font/scaling globals are now managed by ThemeManager.
// See ThemeManager.h for the unified API.

inline void RebuildFont(float scale = 0.0f) {
	auto& io = ImGui::GetIO();
	ImFontConfig config = ImFontConfig();

	// 对于代码显示，PixelSnapH 非常重要，能防止字母边缘模糊
	config.PixelSnapH = true;

	io.Fonts->Clear();

#ifdef __ANDROID__
	constexpr float defaultscale = 3.0f;
#else
	constexpr float defaultscale = 1.0f;
#endif
	if (scale == 0) {
		scale = defaultscale;
	}

	// 3. External Users Fonts
	std::vector<std::string> externalFonts;
	AddFontsFromDir(externalFonts, "./fonts/");
	if (!externalFonts.empty()) {
		for (const auto& font : externalFonts) {
			io.Fonts->AddFontFromFileTTF(font.c_str(), 15 * scale, &config, GetGlobalRanges());
			printf("[Ui][Info] Loaded Users Fonts: %s\n", font.c_str());
		}
	}
	

	// 1. 加载等宽基础字体 (Monospace Base)
	// 这是改动最大的地方，确保英文和代码符号绝对等宽
	std::string mono_font_path = GetMonospaceFontPath();
	bool base_loaded = false;

	if (!mono_font_path.empty()) {
		io.Fonts->AddFontFromFileTTF(mono_font_path.c_str(), 15 * scale, &config, io.Fonts->GetGlyphRangesDefault());
		base_loaded = true;
		printf("[Ui][Info] Loaded Monospace Font: %s\n", mono_font_path.c_str());

		// Merge additional glyph ranges based on localization settings
		config.MergeMode = true;
		if ("Localization.LoadVietnamese"_l == "1" || "Localization.LoadVietnamese"_l == "true") {
			io.Fonts->AddFontFromFileTTF(mono_font_path.c_str(), 15 * scale, &config, io.Fonts->GetGlyphRangesVietnamese());
		}
		if ("Localization.LoadCyrillic"_l == "1" || "Localization.LoadCyrillic"_l == "true") {
			io.Fonts->AddFontFromFileTTF(mono_font_path.c_str(), 15 * scale, &config, io.Fonts->GetGlyphRangesCyrillic());
		}
		if ("Localization.LoadGreek"_l == "1" || "Localization.LoadGreek"_l == "true") {
			io.Fonts->AddFontFromFileTTF(mono_font_path.c_str(), 15 * scale, &config, io.Fonts->GetGlyphRangesGreek());
		}
		config.MergeMode = false;
	}
	else {
		printf("[Ui][Warn] No monospace font found! Falling back to ImGui Default (ProggyClean).\n");
		// ImGui 自带的默认字体 (ProggyClean) 也是等宽的，是一个安全的最后防线
		io.Fonts->AddFontDefault(&config);
		base_loaded = true;
	}

	// Thai glyphs are not reliably present in the base/monospace font, so
	// always merge a dedicated Thai font when requested instead of reusing
	// mono_font_path (which previously silently produced tofu boxes).
	if ("Localization.LoadThai"_l == "1" || "Localization.LoadThai"_l == "true") {
		std::string thai_font_path = GetThaiFontPath();
		if (!thai_font_path.empty()) {
			config.MergeMode = true;
			io.Fonts->AddFontFromFileTTF(thai_font_path.c_str(), 15 * scale, &config, io.Fonts->GetGlyphRangesThai());
			config.MergeMode = false;
			printf("[Ui][Info] Loaded Thai Font: %s\n", thai_font_path.c_str());
		}
		else {
			printf("[Ui][Warn] Thai requested but no Thai font found.\n");
		}
	}

	// 2. 合并 CJK 字体
	auto enable_cjk = "Localization.EnableCJK"_l;
	if (enable_cjk == "1" || enable_cjk == "true") {
		std::string cjk_font_path = GetCJKFontPath();

		if (!cjk_font_path.empty()) {
			config.MergeMode = true;

			// 稍微放大一点 CJK 字体，通常中文比同号的英文显得小
			// 保持 16 vs 15 或者 15 vs 15 都可以，看你视觉偏好
			if ("Localization.CJKPreference"_l == "KR") {
				io.Fonts->AddFontFromFileTTF(cjk_font_path.c_str(), 16 * scale, &config, io.Fonts->GetGlyphRangesKorean());
			}
			else {
				io.Fonts->AddFontFromFileTTF(cjk_font_path.c_str(), 16 * scale, &config, GetCJKRanges());
			}
		}
	}

	io.Fonts->Build();
}
