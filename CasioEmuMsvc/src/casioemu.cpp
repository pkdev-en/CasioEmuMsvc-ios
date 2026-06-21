#include "Config.hpp"
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

#include "StartupUi/StartupUi.h"
#include <Gui.h>
#include <Plugin/PluginMan.h>
#include <ThemeManager.h>
#include "DiscordRPC.h"

#include "TouchMouseTranslator.h"

#ifdef IOS
#include "iOSNativeBridge.h"
#endif

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

		if ((window->Flags & ImGuiWindowFlags_NoMouseInputs) || (window->Flags & ImGuiWindowFlags_NoTitleBar)) {
			continue;
		}

		ImRect rect = window->OuterRectClipped;

		if (rect.Contains(p)) {
			return true;
		}
	}

	return false;
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
#elif defined(IOS)
	const char* home = getenv("HOME");
	if (home) {
		std::string path = std::string(home) + "/Documents/CasioEmuMsvc";
		std::filesystem::create_directories(path);
		std::error_code ec;
		std::filesystem::copy("models", path + "/models", std::filesystem::copy_options::recursive | std::filesystem::copy_options::skip_existing, ec);
		std::filesystem::copy("locales", path + "/locales", std::filesystem::copy_options::recursive | std::filesystem::copy_options::skip_existing, ec);
		std::filesystem::copy("fonts", path + "/fonts", std::filesystem::copy_options::recursive | std::filesystem::copy_options::skip_existing, ec);
		std::filesystem::copy("License.md", path + "/License.md", std::filesystem::copy_options::skip_existing, ec);
		chdir(path.c_str());
	}
#endif
	g_local.Load();
	ThemeManager::Instance().LoadSettings();

	DiscordRPC::Init();
  DiscordRPC::UpdatePresence("");

#if !defined(__ANDROID__) && !defined(IOS)
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
	int sdlFlags = SDL_INIT_VIDEO | SDL_INIT_TIMER;
	if (SDL_Init(sdlFlags) != 0)
		PANIC("SDL_Init failed: %s\n", SDL_GetError());

	int imgFlags = IMG_INIT_PNG;
	if (IMG_Init(imgFlags) != imgFlags)
		PANIC("IMG_Init failed: %s\n", IMG_GetError());
	if (headless && argv_map["model"].empty()) {
		PANIC("No model path supplied.\n");
	}
	
	while (true) {
		if (argv_map["model"].empty()) {
		auto s = sui_loop();
		argv_map["model"] = std::move(s);
		if (argv_map["model"].empty()) {
      DiscordRPC::Shutdown();
			return -1;
	  }
	}

	// After startupui has done its job:
	// startupui doesn't need that.
#if defined(__ANDROID__) || defined(IOS)
	SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
	SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
#endif

	bool no_dbg = !argv_map["no_dbg"].empty();
	low_perf_ext = !argv_map["low_perf_ext"].empty();
	Emulator emulator(argv_map);
	m_emu = &emulator;

	// static std::atomic<bool> running(true);
	
	DiscordRPC::UpdatePresence(emulator.ModelDefinition.model_name);

	bool guiCreated = false;

#if defined(__ANDROID__) || defined(IOS)
	TouchMouseTranslator touchTranslator(
		SDL_GetWindowID(emulator.window),

		[&](const SDL_Event& translatedEvent, TouchTarget target) {
			if (target == TouchTarget::ImGui) {
				ImGui_ImplSDL2_ProcessEvent(&translatedEvent);
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
#if defined(__ANDROID__) || defined(IOS)
			SDL_Delay(40);
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
		if (!SDL_PollEvent(&event))
			continue;
		busy = true;
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

#if defined(__ANDROID__) || defined(IOS)
			touchTranslator.RenderDebug(renderer);
#endif

			SDL_RenderPresent(emulator.renderer);
#else
			emulator.Frame();
			if (!no_dbg)
				gui_loop();
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
		switch (event.type) {
		case SDL_WINDOWEVENT:
			switch (event.window.event) {
			case SDL_WINDOWEVENT_CLOSE: {
				extern SDL_Window* window; // This is the debugger window
				if (event.window.windowID == SDL_GetWindowID(emulator.window)) {
#if !defined(__ANDROID__) && !defined(IOS)
					if (!no_dbg) {
						emulator.calculator_as_tab.store(true);
						SDL_HideWindow(emulator.window);
					} else {
						emulator.Shutdown();
					}
#else
					emulator.Shutdown();
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
#if defined(__ANDROID__) || defined(IOS)
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
		case SDL_KEYDOWN:
		case SDL_KEYUP:
		case SDL_TEXTINPUT:
		case SDL_MOUSEWHEEL:
#ifdef SINGLE_WINDOW
			ImGui_ImplSDL2_ProcessEvent(&event);
			if (ImGui::GetIO().WantCaptureMouse) {
				break;
			}
#else
			if (!no_dbg)
				if ((SDL_GetKeyboardFocus() != emulator.window) && guiCreated) {
					ImGui_ImplSDL2_ProcessEvent(&event);
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
	break;
	} // end while(true)
	
#ifdef ENABLE_SENTRY
	sentry_close();
#endif
  DiscordRPC::Shutdown();
	return 0;
};
#ifdef IOS
#include <chrono>
#include <thread>
#include <atomic>

static std::atomic<bool> is_in_background(false);
static std::thread* background_timer_thread = nullptr;
static std::atomic<bool> exit_timer_thread(false);

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
