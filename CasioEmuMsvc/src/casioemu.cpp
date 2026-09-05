#include "Config.hpp"
#include "Gui/PopUpDisplay.h"
#include "Gui/ThemeManager.h"
#include "Ui.hpp"
#include "imgui_impl_sdl2.h"
#include "Gui/PopUpDisplay.h"

#include "Emulator.hpp"
#include "Localization.h"
#include "Logger.hpp"
#include "SDL_events.h"
#include "SDL_keyboard.h"
#include "SDL_mouse.h"
#include "SDL_video.h"
#include <SDL.h>
#include <SDL_image.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <mutex>
#include <ostream>
#include <sstream>
#include <string>
#include <thread>
#if _WIN32
#include <Windows.h>
#include <combaseapi.h>
#include <timeapi.h>
#pragma comment(lib, "winmm.lib")
#include "sdl_win32_extra.h"
#endif

#if defined(__ANDROID__) || defined(__APPLE__)
#include <unistd.h>
#endif
#ifdef ENABLE_SENTRY
#include <sentry.h>
#endif
#ifdef __APPLE__
#include <mach-o/dyld.h>
#include <unistd.h>
#endif
#include "StartupUi/StartupUi.h"
#include <Gui.h>
#include <Plugin/PluginMan.h>
#include <ThemeManager.h>
#include "DiscordRPC.h"

#include "TouchMouseTranslator.h"

#ifdef __IOS__
#include "Ext/IOSNativeBridge.h"
#endif
// Included unconditionally (not just #ifdef __IOS__): ShortcutLaunch.h provides
// its own internal no-op fallbacks for every other platform, via
// #ifdef __IOS__/#else inside the header itself, specifically so the checks in
// the loops below don't need their own #ifdef guards.
#include "Ext/ShortcutLaunch.h"

using namespace casioemu;
SDL_Surface* background;
SDL_Texture* bg_txt;
bool low_perf_ext = false;

// Driver chain tried in order after a crash: default (auto) → opengl → software
static const char* kRendererDrivers[] = {"default", "opengl", "software"};
static const int kRendererDriverCount = 3;
static const char* kCrashLockFile = ".crash.switch_renderer";
static const char* kRendererHintFile = ".renderer_hint.cfg";

static std::string ReadRendererHint() {
	std::ifstream f(kRendererHintFile);
	if (!f.is_open())
		return "default";
	std::string s;
	std::getline(f, s);
	for (int i = 0; i < kRendererDriverCount; ++i)
		if (s == kRendererDrivers[i])
			return s;
	return "default";
}

static void WriteRendererHint(const std::string& driver) {
	std::ofstream f(kRendererHintFile, std::ios::trunc);
	if (f.is_open())
		f << driver;
}

static std::string NextRendererDriver(const std::string& current) {
	for (int i = 0; i < kRendererDriverCount - 1; ++i)
		if (current == kRendererDrivers[i])
			return kRendererDrivers[i + 1];
	return kRendererDrivers[kRendererDriverCount - 1];
}

static void TouchCrashLock() {
	std::ofstream f(kCrashLockFile, std::ios::trunc);
}

static void RemoveCrashLock() {
	std::filesystem::remove(kCrashLockFile);
}

static bool IsPointInImGuiWindow(float x, float y) {
	ImGuiContext* ctx = ImGui::GetCurrentContext();
	if (!ctx) {
		return false;
	}
	ImGuiContext& g = *ctx;
	ImVec2 p(x, y);

	ImGuiIO& io = ImGui::GetIO();

	if (io.WantCaptureMouse || ImGui::IsAnyItemActive()) {
		return true;
	}
	if (y < top_bar_size)
		return true;

	for (int i = g.Windows.Size - 1; i >= 0; --i) {
		ImGuiWindow* window = g.Windows[i];

		if (!window) {
			continue;
		}

		if (!window->WasActive || window->Hidden) {
			continue;
		}

		if (window->Flags & ImGuiWindowFlags_NoMouseInputs) {
			continue;
		}

		ImRect rect = window->OuterRectClipped;

		if (rect.Contains(p)) {
			return true;
		}
	}

	return false;
}

static Uint32 GetEventWindowId(const SDL_Event& event) {
	switch (event.type) {
	case SDL_WINDOWEVENT:
		return event.window.windowID;
	case SDL_KEYDOWN:
	case SDL_KEYUP:
		return event.key.windowID;
	case SDL_TEXTEDITING:
	case SDL_TEXTINPUT:
		return event.text.windowID;
	case SDL_MOUSEMOTION:
		return event.motion.windowID;
	case SDL_MOUSEBUTTONDOWN:
	case SDL_MOUSEBUTTONUP:
		return event.button.windowID;
	case SDL_MOUSEWHEEL:
		return event.wheel.windowID;
	default:
		return 0;
	}
}

static void ProcessImGuiEvent(const SDL_Event& event) {
#ifdef __ANDROID__
	if (event.type == SDL_TEXTINPUT) {
		ThemeManager::Instance().RegisterInputGlyphs(event.text.text);
	}
#endif
	ImGui_ImplSDL2_ProcessEvent(&event);
}

static bool IsScreenMirrorWindowEvent(const SDL_Event& event) {
	const Uint32 windowId = GetEventWindowId(event);
	if (windowId == 0) {
		return false;
	}
	SDL_Window* eventWindow = SDL_GetWindowFromID(windowId);
	return eventWindow && SDL_GetWindowData(eventWindow, SCREEN_MIRROR_WINDOW_DATA_KEY);
}

int main(int argc, char* argv[]) {
#ifdef _WIN32
	SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE);
	timeBeginPeriod(1);
	SetConsoleCP(65001); // Set to UTF8
	SetConsoleOutputCP(65001);
	CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
#endif //  _WIN32
#ifdef ENABLE_SENTRY
	sentry_options_t* options = sentry_options_new();
	sentry_options_set_dsn(options, "https://335230bc5e18c7b25464556638c4cfdc@o4510804732018688.ingest.us.sentry.io/4510805048950784");
	// This is also the default-path. For further information and recommendations:
	// https://docs.sentry.io/platforms/native/configuration/options/#database-path
	sentry_options_set_database_path(options, ".sentry");
	sentry_options_set_release(options, "CasioEmuMsvc@" EMULATOR_VERSION);
	sentry_options_set_debug(options, 1);
	sentry_init(options);
#endif
#ifdef __ANDROID__
	chdir(SDL_AndroidGetExternalStoragePath());
#elif defined(MACOS)
	{
		const char* home = getenv("HOME");
		if (home) {
			std::string path = std::string(home) + "/CasioEmuMsvc";
			std::filesystem::create_directories(path);
			chdir(path.c_str());
    
            std::filesystem::path src =
                "/Applications/CasioEmuMsvc.app/Contents/Resources";
    
            try {
                for (auto& entry : std::filesystem::directory_iterator(src)) {
                    if (entry.path().filename() == "AppIcons.icns")
                        continue;
            
                    auto dst = std::filesystem::path(path) / entry.path().filename();
            
                    std::filesystem::copy(
                        entry.path(),
                        dst,
                        std::filesystem::copy_options::recursive |
                        std::filesystem::copy_options::overwrite_existing
                    );
                }
            }
            catch (const std::exception& e) {
                fprintf(stderr, "Resource copy failed: %s\n", e.what());
            }
        }
    }
#elif defined(__IOS__)
	// NOTE: the actual resource copy + chdir for iOS happens further below,
	// right after SDL_Init(), where SDL_GetBasePath() can reliably resolve
	// the app bundle's Resources directory. SDL does not chdir into the
	// bundle on its own (see SDL's own docs: the working directory is left
	// as whatever the launching process used), so relying on relative paths
	// like "models" here — before SDL_Init() establishes anything — silently
	// resolves against the wrong directory and leaves the app without any
	// models, locales, or fonts after the chdir() below runs.
#elif defined(__APPLE__)
	char path[1024];
	uint32_t size = sizeof(path);
	if (_NSGetExecutablePath(path, &size) == 0) {
		char* last_slash = strrchr(path, '/');
		if (last_slash) {
			*last_slash = '\0';
			chdir(path);
		}
	}
#endif
#ifndef __IOS__
	g_local.Load();
	ThemeManager::Instance().LoadSettings();
#endif

	DiscordRPC::Init();
  DiscordRPC::UpdatePresence("");

#if !defined(__ANDROID__) && !defined(__IOS__)
	std::string rendererDriver = ReadRendererHint();
	bool previouslyCrashed = std::filesystem::exists(kCrashLockFile);
	if (previouslyCrashed) {
		rendererDriver = NextRendererDriver(rendererDriver);
		WriteRendererHint(rendererDriver);
		printf("[Startup][Warn] Previous session crashed. Switching renderer to: %s\n", rendererDriver.c_str());

		char msg[256];
		snprintf(msg, sizeof(msg),
			"The previous session crashed.\n"
			"Automatically switching to the '%s' renderer backend.\n"
			"If crashes persist, try updating your GPU drivers.\n"
			"If you think this is a error, delete .renderer_hint.cfg to reset to default.",
			rendererDriver.c_str());
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING, "CasioEmuMsvc - Renderer Fallback", msg, nullptr);

		RemoveCrashLock();
	}
	if (rendererDriver != "default") {
		// SDL_RENDER_DRIVER is checked by SDL when creating a renderer
		SDL_SetHint(SDL_HINT_RENDER_DRIVER, rendererDriver.c_str());
		printf("[Startup][Info] Renderer hint set to: %s\n", rendererDriver.c_str());
	}
#endif

	std::map<std::string, std::string> argv_map;
	for (int ix = 1; ix != argc; ++ix) {
		std::string key, value;
		char* eq_pos = strchr(argv[ix], '=');
		if (eq_pos) {
			key = std::string(argv[ix], eq_pos);
			value = eq_pos + 1;
		}
		else {
			key = "model";
			value = argv[ix];
		}

		if (argv_map.find(key) == argv_map.end())
			argv_map[key] = value;
		else
			logger::Info("[argv][Info] #%i: key '%s' already set\n", ix, key.c_str());
	}
	bool headless = argv_map.find("headless") != argv_map.end();
	std::shared_ptr<casioemu::ModelResourceStore> startup_resources;
	int sdlFlags = SDL_INIT_VIDEO | SDL_INIT_TIMER;
	if (SDL_Init(sdlFlags) != 0)
		PANIC("SDL_Init failed: %s\n", SDL_GetError());
#ifdef __IOS__
	InitShortcutWakeEvent();
#endif

#ifdef __IOS__
	{
		// SDL_GetBasePath() is the reliable way to find the app bundle's
		// Resources directory on iOS/macOS — it does NOT depend on the
		// process's working directory at launch (which SDL leaves
		// untouched, see SDL's own docs). Calling this only works after
		// SDL_Init(), hence why this whole block lives here instead of at
		// the top of main().
		char* basePathRaw = SDL_GetBasePath();
		std::string basePath = basePathRaw ? basePathRaw : "";
		if (basePathRaw) SDL_free(basePathRaw);

		const char* home = getenv("HOME");
		if (home && !basePath.empty()) {
			std::string path = std::string(home) + "/Documents/CasioEmuMsvc";
			std::filesystem::create_directories(path);
			std::error_code ec;
			std::filesystem::copy(basePath + "models", path + "/models", std::filesystem::copy_options::recursive | std::filesystem::copy_options::skip_existing, ec);
			std::filesystem::copy(basePath + "locales", path + "/locales", std::filesystem::copy_options::recursive | std::filesystem::copy_options::skip_existing, ec);
			std::filesystem::copy(basePath + "fonts", path + "/fonts", std::filesystem::copy_options::recursive | std::filesystem::copy_options::skip_existing, ec);
			std::filesystem::copy(basePath + "fonts_cjk", path + "/fonts_cjk", std::filesystem::copy_options::recursive | std::filesystem::copy_options::skip_existing, ec);
			std::filesystem::copy(basePath + "License.md", path + "/License.md", std::filesystem::copy_options::skip_existing, ec);
			chdir(path.c_str());
		}
		else if (!basePath.empty()) {
			// No writable HOME dir available — fall back to running
			// straight out of the (read-only) bundle directory so the app
			// can still find its resources, even though it won't be able
			// to persist settings/recordings/etc.
			chdir(basePath.c_str());
		}
	}
	g_local.Load();
	ThemeManager::Instance().LoadSettings();
#endif

	int imgFlags = IMG_INIT_PNG;
	if (IMG_Init(imgFlags) != imgFlags)
		PANIC("IMG_Init failed: %s\n", IMG_GetError());
	if (headless && argv_map["model"].empty()) {
		PANIC("No model path supplied.\n");
	}
	while (true) {
#ifdef __IOS__
		if (argv_map["model"].empty()) {
			// A Quick Action may have requested a specific model before
			// this process's C++ main() even started running (see
			// CasioEmuAppDelegate.mm's
			// application:didFinishLaunchingWithOptions:, which iOS calls
			// earlier, as part of the normal app-launch sequence). Prefer
			// that over showing the menu, exactly like a command-line model
			// argument would.
			argv_map["model"] = TakePendingShortcutModel();
		}
#endif
		if (argv_map["model"].empty()) {
			auto selection = sui_loop();
			argv_map["model"] = std::move(selection.model_path);
			startup_resources = std::move(selection.resources);
			if (argv_map["model"].empty()) {
				DiscordRPC::Shutdown();
				return -1;
			}
		}
	}

	// After startupui has done its job:
	// startupui doesn't need that.
#if defined(__ANDROID__) || defined(__IOS__)
	SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
	SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
#endif

	bool no_dbg = !argv_map["no_dbg"].empty();
	low_perf_ext = !argv_map["low_perf_ext"].empty();
	Emulator emulator(argv_map, false, std::move(startup_resources));
	m_emu = &emulator;

	// static std::atomic<bool> running(true);
	
	DiscordRPC::UpdatePresence(emulator.ModelDefinition.model_name);

	bool guiCreated = false;

#if defined(__ANDROID__) || defined(__IOS__)
	TouchMouseTranslator touchTranslator(
		SDL_GetWindowID(emulator.window),

		[&](const SDL_Event& translatedEvent, TouchTarget target) {
			if (target == TouchTarget::ImGui) {
				ProcessImGuiEvent(translatedEvent);
				return;
			}

			emulator.UIEvent(translatedEvent);
		},

		[&](float x, float y) -> bool {
#ifdef SINGLE_WINDOW
			if (no_dbg) {
				return false;
			}
			return IsPointInImGuiWindow(x, y);
#else
			if (no_dbg || !guiCreated) {
				return false;
			}
			return IsPointInImGuiWindow(x, y);
#endif
		});
#endif
	auto frame_event = SDL_RegisterEvents(1);
	bool busy = false;
	bool running = true;
	std::thread t3([&]() {
		SDL_Event se{};
		se.type = frame_event;
		se.user.windowID = SDL_GetWindowID(emulator.window);
		while (running) {
			if (!busy)
				SDL_PushEvent(&se);
#if defined(__ANDROID__) || defined(__IOS__)
			// [Perf fix — 2026-09-03 13:44 GMT+7] Was SDL_Delay(40) — hard-capped every device at ~25fps
			// regardless of what the screen can actually do. Now this
			// thread just offers a new frame far faster than any real
			// display refreshes; SDL_RENDERER_PRESENTVSYNC (set in
			// Emulator.cpp) is what actually blocks SDL_RenderPresent()
			// until the next vblank, so the real ceiling becomes each
			// device's own panel rate — 60Hz on older iPhones, up to
			// 120Hz on ProMotion — instead of one fixed number forced
			// on every device alike.
			SDL_Delay(4);
#else
			if (ThemeManager::Instance().Settings().lowPerformanceMode || low_perf_ext)
				SDL_Delay(24);
			else
				SDL_Delay(1);
#endif
		}
	});
	// t3.detach(); removed to allow joining
#ifdef DBG
	if (!no_dbg) {
		test_gui(&guiCreated, emulator.window, emulator.renderer);
		background = IMG_Load("background.jpg");
		bg_txt = 0;
		if (background) {
			bg_txt = SDL_CreateTextureFromSurface(renderer, background);
			ThemeManager::Instance().ExtractAndApplyAutoTint(bg_txt, renderer);
		}
	}
#endif
#ifdef _WIN32
	EnableDarkTitleBar(GetSDLWindowHandle(emulator.window));
#endif
	SDL_ShowWindow(emulator.window);
	SDL_RaiseWindow(emulator.window);

#if defined(_WIN32) || defined(__ANDROID__)
	LoadPlugins();
#endif
	while (emulator.Running()) {
		SDL_Event event{};
		busy = false;
		DiscordRPC::Update();
		// [Perf fix — 2026-09-03 13:44 GMT+7] Was SDL_PollEvent() + continue — spins this thread at 100% of
		// a CPU core with no delay whenever there's nothing to do, since
		// PollEvent returns immediately either way. WaitEventTimeout
		// blocks (letting the OS actually idle the core) until either a
		// real input event or the t3 thread's injected frame_event shows
		// up, or the 8ms timeout elapses — same return semantics as
		// PollEvent (nonzero = got an event), just without the spin.
		// Matters most on older iPhones: less sustained heat means less
		// thermal throttling over a long session.
		if (!SDL_WaitEventTimeout(&event, 8))
			continue;
		busy = true;
#ifdef __IOS__
		{
			// A Home Screen shortcut was tapped while this, a *different*,
			// model is already running -- either a Quick Action
			// (TakePendingShortcutModel(), set by CasioEmuAppDelegate.mm,
			// possibly from a different thread) or, for backward
			// compatibility, a tap on a shortcut created by an older build
			// of this app (the casioemu:// URL scheme, delivered as this
			// specific SDL_DROPFILE event and resolved via
			// ResolveShortcutLaunchEvent()). Either way: stash the
			// requested model and shut down the current session the same
			// way a normal quit does; main()'s outer loop calls
			// TakePendingShortcutModel() right after this loop ends and, if
			// it returns something, loops back with the new model instead
			// of exiting. See Ext/ShortcutLaunch.h.
			std::string pendingModel = TakePendingShortcutModel();
			if (pendingModel.empty()) {
				auto resolved = ResolveShortcutLaunchEvent(event);
				if (!resolved.empty())
					pendingModel = resolved.string();
			}
			if (!pendingModel.empty()) {
				SetPendingShortcutModel(pendingModel);
				emulator.Shutdown();
			}
		}
#endif
		if (event.type == frame_event) {
			SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
			SDL_RenderClear(renderer);
			if (bg_txt) {
				int w, h;
				SDL_GetWindowSize(window, &w, &h);
				int bg_w, bg_h;
				SDL_QueryTexture(bg_txt, NULL, NULL, &bg_w, &bg_h);

				float window_aspect = (float)w / h;
				float bg_aspect = (float)bg_w / bg_h;

				SDL_Rect dst_rect;
				if (window_aspect > bg_aspect) {
					dst_rect.w = w;
					dst_rect.h = (int)(w / bg_aspect);
					dst_rect.x = 0;
					dst_rect.y = (h - dst_rect.h) / 2;
				}
				else {
					dst_rect.h = h;
					dst_rect.w = (int)(h * bg_aspect);
					dst_rect.x = (w - dst_rect.w) / 2;
					dst_rect.y = 0;
				}

				SDL_RenderCopy(renderer, bg_txt, NULL, &dst_rect);
			}
			SDL_SetRenderDrawColor(renderer, 0, 0, 0, 20);
			SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
			SDL_RenderFillRect(renderer, 0);
#ifdef SINGLE_WINDOW
			emulator.Frame();
			gui_loop();

#if defined(__ANDROID__) || defined(__IOS__)
			touchTranslator.RenderDebug(renderer);
#endif

			SDL_RenderPresent(emulator.renderer);
#else
			emulator.Frame();
			if (!no_dbg) {
				gui_loop();
			}
			SDL_RenderPresent(emulator.renderer);
#endif
			if (!no_dbg) {
				ThemeManager::Instance().ProcessFontRebuild();
				if (ThemeManager::Instance().IsBgReloadRequested()) {
					SDL_DestroyTexture(bg_txt);
					SDL_FreeSurface(background);
					background = IMG_Load("background.jpg");
					if (background) {
						bg_txt = SDL_CreateTextureFromSurface(renderer, background);
						ThemeManager::Instance().ExtractAndApplyAutoTint(bg_txt, renderer);
					}
					ThemeManager::Instance().ClearBgReloadRequest();
				}
			}
			while (SDL_PollEvent(&event)) {
				if (event.type != frame_event)
					goto hld;
			}
			continue;
		}

	hld:
		if (g_mirror && g_mirror->handleEvent(event)) {
			continue;
		}
		int wid, hei;
		SDL_GetWindowSize(window, &wid, &hei);
		const Uint32 eventWindowId = GetEventWindowId(event);
		if (IsScreenMirrorWindowEvent(event)) {
			if (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP) {
				event.key.windowID = SDL_GetWindowID(emulator.window);
				emulator.UIEvent(event);
			}
			continue;
		}
		switch (event.type) {
		case SDL_WINDOWEVENT:
			switch (event.window.event) {
			case SDL_WINDOWEVENT_CLOSE: {
				if (SDL_Window* closedWindow = SDL_GetWindowFromID(eventWindowId)) {
					if (SDL_GetWindowData(closedWindow, SCREEN_MIRROR_WINDOW_DATA_KEY)) {
						break;
					}
				}
				extern SDL_Window* window; // This is the debugger window
				if (event.window.windowID == SDL_GetWindowID(emulator.window)) {
#if !defined(__ANDROID__) && !defined(__IOS__)
					if (!no_dbg) {
						emulator.calculator_as_tab.store(true);
						SDL_HideWindow(emulator.window);
					} else {
						emulator.Shutdown();
						std::exit(0);
					}
#else
					emulator.Shutdown();
					std::exit(0);
#endif
				} else if (window && event.window.windowID == SDL_GetWindowID(window)) {
					std::exit(0);
				}
				break;
			}
			case SDL_WINDOWEVENT_RESIZED:
				break;
			}
			break;
#if defined(__ANDROID__) || defined(__IOS__)
		case SDL_FINGERDOWN:
		case SDL_FINGERUP:
		case SDL_FINGERMOTION:
			touchTranslator.HandleEvent(event, wid, hei);
			break;
#else
		case SDL_MOUSEBUTTONDOWN:
		case SDL_MOUSEBUTTONUP:
		case SDL_MOUSEMOTION:
#endif
#ifdef __IOS__
		case SDL_DROPFILE:
			// Already fully handled above (before this switch), together
			// with Quick Action taps -- see the comment there.
			break;
#endif
		case SDL_KEYDOWN:
		case SDL_KEYUP:
		case SDL_TEXTINPUT:
		case SDL_MOUSEWHEEL:
#ifdef SINGLE_WINDOW
			ProcessImGuiEvent(event);
			if (ImGui::GetIO().WantCaptureMouse) {
				break;
			}
#else
			if (!no_dbg)
				if (guiCreated && eventWindowId != 0 && window && eventWindowId == SDL_GetWindowID(window)) {
					ProcessImGuiEvent(event);
					break;
				}
#endif
			[[fallthrough]];
		default:
			emulator.UIEvent(event);
			break;
		}
	}
	running = false;
	if (t3.joinable()) {
		t3.join();
	}
	if (bg_txt) {
		SDL_DestroyTexture(bg_txt);
	}
#ifdef __IOS__
	{
		std::string nextModel = TakePendingShortcutModel();
		if (!nextModel.empty()) {
			// A Home Screen shortcut asked for a different model while this
			// one was running (see the unified check near the top of the
			// loop above) -- loop back to the top of while(true) with the
			// new model already set, instead of falling through to
			// sui_loop() or exiting.
			argv_map["model"] = std::move(nextModel);
			continue;
		}
	}
#endif
	break;
	} // end while(true)
	
#ifdef ENABLE_SENTRY
	sentry_close();
#endif
  DiscordRPC::Shutdown();
	return 0;
};
#ifdef __IOS__
#include <chrono>
#include <thread>
#include <atomic>

static std::atomic<bool> is_in_background(false);
static std::thread* background_timer_thread = nullptr;
static std::atomic<bool> exit_timer_thread(false);

// ---- iOS File/Folder Dialog Callbacks ----
// These C functions are called by IOSNativeBridge.mm (Objective-C side)
// and bridge into the SystemDialogs C++ callback system used by the engine.
#include "Ext/SysDialog.h"
#include <vector>

extern "C" void onFileSelected(const char* path, const unsigned char* data, int dataLength) {
    if (SystemDialogs::fileOpenCallback) {
        // Write the received data to a temp file, then pass the path to the callback
        std::filesystem::path tempDir = std::filesystem::temp_directory_path() / "casioemu_ios_tmp";
        std::filesystem::create_directories(tempDir);
        std::filesystem::path fileName = std::filesystem::path(path).filename();
        std::filesystem::path tempPath = tempDir / fileName;
        {
            std::ofstream f(tempPath, std::ios::binary);
            if (f) f.write(reinterpret_cast<const char*>(data), dataLength);
        }
        auto cb = SystemDialogs::fileOpenCallback;
        SystemDialogs::fileOpenCallback = nullptr;
        cb(tempPath);
    }
}

extern "C" void onFileSaved(const char* path) {
    if (SystemDialogs::fileSaveCallback) {
        auto cb = SystemDialogs::fileSaveCallback;
        SystemDialogs::fileSaveCallback = nullptr;
        cb(std::filesystem::path(path));
    }
}

extern "C" void onFolderSelected(const char* path) {
    if (SystemDialogs::folderOpenCallback) {
        auto cb = SystemDialogs::folderOpenCallback;
        SystemDialogs::folderOpenCallback = nullptr;
        cb(std::filesystem::path(path));
    }
}

extern "C" void onFolderSaved(const char* path) {
    if (SystemDialogs::folderSaveCallback) {
        auto cb = SystemDialogs::folderSaveCallback;
        SystemDialogs::folderSaveCallback = nullptr;
        cb(std::filesystem::path(path));
    }
}

extern "C" void onImportFailed() {
    // Cancel: clear all pending callbacks gracefully
    SystemDialogs::fileOpenCallback = nullptr;
    SystemDialogs::folderOpenCallback = nullptr;
    SystemDialogs::fileSaveCallback = nullptr;
    SystemDialogs::folderSaveCallback = nullptr;
}

extern "C" void onExportFailed() {
    SystemDialogs::fileSaveCallback = nullptr;
    SystemDialogs::folderSaveCallback = nullptr;
}
// ---- End iOS Callbacks ----

extern "C" void onAppCreate() {
}

extern "C" void onAppResume() {
    is_in_background.store(false);
}

extern "C" void onAppPause() {
}

extern "C" void onAppBackground() {
    is_in_background.store(true);
    if (background_timer_thread) {
        exit_timer_thread.store(true);
        background_timer_thread->join();
        delete background_timer_thread;
        background_timer_thread = nullptr;
    }
    
    exit_timer_thread.store(false);
    background_timer_thread = new std::thread([]() {
        for (int i = 0; i < 300; ++i) { // 5 minutes
            if (exit_timer_thread.load()) return;
            std::this_thread::sleep_for(std::chrono::seconds(1));
            if (!is_in_background.load()) return;
        }
        
        if (is_in_background.load() && !exit_timer_thread.load()) {
            exit(0);
        }
    });
}

extern "C" void onAppForeground() {
    is_in_background.store(false);
    if (background_timer_thread) {
        exit_timer_thread.store(true);
        background_timer_thread->join();
        delete background_timer_thread;
        background_timer_thread = nullptr;
    }
}

extern "C" void onAppTerminate() {
}
#endif
