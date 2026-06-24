#include "Ui.hpp"
#include "hex.hpp"
#include "5800FileSystem.h"
#include "AddressWindow.h"
#include "BitmapViewer.h"
#include "CallAnalysis.h"
#include "Chipset/Chipset.hpp"
#include "Chipset/MMU.hpp"
#include "CodeViewer.hpp"
#include "Editors.h"
#include "HwController.h"
#include "Injector.hpp"
#include "LabelFile.h"
#include "LabelViewer.h"
#include "MemBreakPoint.hpp"
#include "Random.hpp"
#include "Theme.h"
#include "VariableWindow.h"
#include "WatchWindow.hpp"
#include "Rop/RopCompilerUI.h"
#include "PluginLogWindow.hpp"
#include "SnapshotWindow.h"
#include "CalculatorWindow.h"
#include "imgui/imgui.h"
#include "imgui/imgui_impl_sdl2.h"
#include "imgui/imgui_impl_sdlrenderer2.h"
#include <Gui.h>
#include <SDL.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>

#ifdef ENABLE_SENTRY
#include <sentry.h>
#endif
#include <sdl_win32_extra.h>
bool show_sentry_feedback = false;
char sentry_user_comments[1024] = "";
char sentry_user_email[128] = "";
char sentry_user_name[128] = "";

char* n_ram_buffer = 0;
casioemu::MMU* me_mmu = 0;
SDL_Window* window = 0;
SDL_Renderer* renderer = 0;

std::vector<Label> g_labels;

CodeViewer* code_viewer = 0;
Injector* injector = 0;
int top_bar_size = 0;
Breakpoints* membp = 0;

std::vector<UIWindow*> windows{};

std::string ui_state_fn = "ui_state.txt";
bool ui_ready = false;
void SaveUIState() {
    if (!ui_ready) return;

    std::string tmp = ui_state_fn + ".tmp";

    std::ofstream f(tmp, std::ios::out | std::ios::trunc);
    if (!f.is_open()) return;

    for (auto* w : windows) {
        if (!w) continue;
        f << w->name << "=" << (w->open ? 1 : 0) << "\n";
    }

    f.close();
    std::filesystem::rename(tmp, ui_state_fn);
}
#ifdef __IOS__
#include "iOSNativeBridge.h"
#endif
static float screenshot_toast_timer = 0.0f;

// =============================================================================
// Floating collapsible toolbar â fix focus/touch issue on iOS
// =============================================================================
void RenderDebuggerToolbar() {
    bool isCustom = false;
#if defined(IOS)
    isCustom = true;
#endif

#if defined(IOS)
    // =========================================================================
    // iOS: Floating draggable toolbar vá»i nÃºt < > thu gá»n/má» ra
    // =========================================================================
    ImGuiViewport* viewport = ImGui::GetMainViewport();

    static ImVec2 s_toolbar_pos = ImVec2(-1.0f, -1.0f);
    static bool s_toolbar_collapsed = true;
    static bool s_pos_initialized = false;
    // FIX 1: DÃ¹ng delayed focus thay vÃ¬ SetWindowFocus trá»±c tiáº¿p trong button callback
    static bool s_needs_focus = false;

    float tbHeight = ImGui::GetFrameHeight() + 18.0f;

    if (!s_pos_initialized) {
        float safeY = std::max(viewport->WorkPos.y, 55.0f);
        float toolbarWidth = 380.0f;
        s_toolbar_pos = ImVec2(
            viewport->WorkPos.x + (viewport->WorkSize.x - toolbarWidth) * 0.5f,
            safeY + 8.0f
        );
        s_pos_initialized = true;
    }

    float winWidth;
    if (s_toolbar_collapsed) {
        winWidth = tbHeight + 4.0f;
    } else {
        winWidth = viewport->WorkSize.x - 16.0f;
        if (winWidth < 500.0f) winWidth = 500.0f;
    }

    ImGui::SetNextWindowPos(s_toolbar_pos, ImGuiCond_Appearing);
    ImGui::SetNextWindowSize(ImVec2(winWidth, tbHeight));
    ImGui::SetNextWindowBgAlpha(0.88f);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,  ImVec2(8.0f, 8.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize,  ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,    ImVec2(4.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
        ImVec2(ImGui::GetStyle().FramePadding.x,
               ImGui::GetStyle().FramePadding.y + 3.0f));

    // FIX 2: ThÃªm NoNav Äá» trÃ¡nh ImGui nav system cÆ°á»p focus
    ImGuiWindowFlags tbFlags =
        ImGuiWindowFlags_NoTitleBar        |
        ImGuiWindowFlags_NoScrollbar       |
        ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoSavedSettings   |
        ImGuiWindowFlags_NoDocking         |
        ImGuiWindowFlags_NoFocusOnAppearing|
        ImGuiWindowFlags_NoNav;            // <-- FIX: ngÄn nav system can thiá»p focus

    if (s_toolbar_collapsed) {
        tbFlags |= ImGuiWindowFlags_NoMove;
    }

    bool opened = ImGui::Begin("##FloatingToolbar", nullptr, tbFlags);

    // FIX 3: Ãp dá»¥ng delayed focus SAU Begin(), chá» 1 láº§n khi user chá»§ Äá»ng má»
    if (s_needs_focus && !s_toolbar_collapsed) {
        ImGui::SetWindowFocus();
        s_needs_focus = false;
    }

    // Cáº­p nháº­t vá» trÃ­ sau khi kÃ©o
    s_toolbar_pos = ImGui::GetWindowPos();

    if (opened) {
        const char* toggleLabel = s_toolbar_collapsed ? ">" : "<";
        ImGui::PushStyleColor(ImGuiCol_Button,
            ImVec4(0.20f, 0.25f, 0.40f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
            ImVec4(0.32f, 0.40f, 0.62f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,
            ImVec4(0.42f, 0.52f, 0.80f, 1.0f));

        if (ImGui::Button(toggleLabel, ImVec2(tbHeight - 6.0f, tbHeight - 6.0f))) {
            s_toolbar_collapsed = !s_toolbar_collapsed;
            if (!s_toolbar_collapsed) {
                // FIX 4: KhÃ´ng gá»i SetWindowFocus á» ÄÃ¢y â delay sang frame sau
                s_needs_focus = true;
            }
        }

        ImGui::PopStyleColor(3);

        if (!s_toolbar_collapsed) {
            ImGui::SameLine(0, 4);

            if (ImGui::BeginTabBar("ToolbarTabs",
                    ImGuiTabBarFlags_FittingPolicyScroll |
                    ImGuiTabBarFlags_NoTooltip)) {

                // Debugger Windows
                if (ImGui::TabItemButton("Debugger Windows")) {
                    ImGui::OpenPopup("DebuggerMenuPopup");
                }
                ImGui::SetNextWindowPos(ImVec2(
                    ImGui::GetItemRectMin().x,
                    ImGui::GetItemRectMax().y));
                if (ImGui::BeginPopup("DebuggerMenuPopup")) {
                    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 8.0f);
                    for (auto* w : windows) {
                        if (w && ImGui::MenuItem(w->name, nullptr, &w->open)) {
                            SaveUIState();
                        }
                    }
                    ImGui::PopStyleVar();
                    ImGui::EndPopup();
                }

                // Open All / Close All
                if (std::any_of(windows.begin(), windows.end(),
                        [](UIWindow* w){ return w && !w->open; })) {
                    if (ImGui::TabItemButton("Open All")) {
                        for (auto* w : windows) if (w) w->open = true;
                    }
                } else {
                    if (ImGui::TabItemButton("Close All")) {
                        for (auto* w : windows) if (w) w->open = false;
                    }
                }

                // Pause / Resume
                bool isPaused = m_emu->GetPaused();
                if (ImGui::TabItemButton(
                        isPaused ? "[>] Resume" : "[||] Pause")) {
                    m_emu->SetPaused(!isPaused);
                }

                // Screenshot
                if (ImGui::TabItemButton("[C] Screenshot")) {
                    ImGui::OpenPopup("ScreenshotMenuPopup");
                }
                ImGui::SetNextWindowPos(ImVec2(
                    ImGui::GetItemRectMin().x,
                    ImGui::GetItemRectMax().y));
                if (ImGui::BeginPopup("ScreenshotMenuPopup")) {
                    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 8.0f);
                    if (ImGui::MenuItem("Full Calculator")) {
                        m_emu->screenshot_full_ui = true;
                        m_emu->screenshot_requested = true;
                    }
                    if (ImGui::MenuItem("Screen Only")) {
                        m_emu->screenshot_full_ui = false;
                        m_emu->screenshot_requested = true;
                    }
                    ImGui::PopStyleVar();
                    ImGui::EndPopup();
                }

                // Record
                if (m_emu->recording_active.load()) {
                    if (ImGui::TabItemButton("[ ] Stop Rec")) {
                        m_emu->recording_stop_requested = true;
                    }
                } else {
                    if (ImGui::TabItemButton("[O] Record")) {
                        ImGui::OpenPopup("RecordMenuPopup");
                    }
                    ImGui::SetNextWindowPos(ImVec2(
                        ImGui::GetItemRectMin().x,
                        ImGui::GetItemRectMax().y));
                    if (ImGui::BeginPopup("RecordMenuPopup")) {
                        ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 8.0f);
                        if (ImGui::MenuItem("Full Calculator")) {
                            m_emu->recording_full_ui = true;
                            m_emu->recording_requested = true;
                        }
                        if (ImGui::MenuItem("Screen Only")) {
                            m_emu->recording_full_ui = false;
                            m_emu->recording_requested = true;
                        }
                        ImGui::PopStyleVar();
                        ImGui::EndPopup();
                    }
                }

                // Theme toggle
                if (ImGui::TabItemButton(
                        ThemeManager::Instance().Settings().isDarkMode
                            ? "Light Theme" : "Dark Theme")) {
                    if (ThemeManager::Instance().Settings().isDarkMode)
                        ThemeManager::Instance().SetLightMode();
                    else
                        ThemeManager::Instance().SetDarkMode();
                }

                ImGui::EndTabBar();
            }

            // Toast
            if (m_emu->screenshot_taken.exchange(false)) {
                screenshot_toast_timer = 3.0f;
            }
            if (screenshot_toast_timer > 0.0f) {
                ImGui::SameLine(ImGui::GetWindowWidth() - 250.0f);
                ImGui::TextColored(
                    ImVec4(0.2f, 1.0f, 0.2f, 1.0f),
                    "[C] Screenshot Saved!");
                screenshot_toast_timer -= ImGui::GetIO().DeltaTime;
            }

            if (m_emu->recording_active.load()) {
                ImGui::SameLine(ImGui::GetWindowWidth() -
                    (screenshot_toast_timer > 0.0f ? 450.0f : 200.0f));
                ImGui::TextColored(
                    ImVec4(1.0f, 0.2f, 0.2f, 1.0f),
                    "[O] Recording: %u frames",
                    m_emu->recording_frame_count.load());
            }
        }
    }

    ImGui::End();
    ImGui::PopStyleVar(5);

#else
    // =========================================================================
    // Desktop/Android: BeginMainMenuBar
    // =========================================================================
    bool opened = ImGui::BeginMainMenuBar();
    if (opened) {
        if (ImGui::BeginTabBar("ToolbarTabs",
                ImGuiTabBarFlags_FittingPolicyScroll |
                ImGuiTabBarFlags_NoTooltip)) {

            if (ImGui::TabItemButton("Debugger Windows")) {
                ImGui::OpenPopup("DebuggerMenuPopup");
            }
            ImGui::SetNextWindowPos(ImVec2(
                ImGui::GetItemRectMin().x,
                ImGui::GetItemRectMax().y));
            if (ImGui::BeginPopup("DebuggerMenuPopup")) {
                ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 8.0f);
                for (auto* w : windows) {
                    if (w && ImGui::MenuItem(w->name, nullptr, &w->open)) {
                        SaveUIState();
                    }
                }
                ImGui::PopStyleVar();
                ImGui::EndPopup();
            }

            if (std::any_of(windows.begin(), windows.end(),
                    [](UIWindow* w){ return !w->open; })) {
                if (ImGui::TabItemButton("Open All")) {
                    for (auto* w : windows) if (w) w->open = true;
                }
            } else {
                if (ImGui::TabItemButton("Close All")) {
                    for (auto* w : windows) if (w) w->open = false;
                }
            }

            bool isPaused = m_emu->GetPaused();
            if (ImGui::TabItemButton(
                    isPaused ? "[>] Resume" : "[||] Pause")) {
                m_emu->SetPaused(!isPaused);
            }

            if (ImGui::TabItemButton("[C] Screenshot")) {
                ImGui::OpenPopup("ScreenshotMenuPopup");
            }
            ImGui::SetNextWindowPos(ImVec2(
                ImGui::GetItemRectMin().x,
                ImGui::GetItemRectMax().y));
            if (ImGui::BeginPopup("ScreenshotMenuPopup")) {
                ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 8.0f);
                if (ImGui::MenuItem("Full Calculator")) {
                    m_emu->screenshot_full_ui = true;
                    m_emu->screenshot_requested = true;
                }
                if (ImGui::MenuItem("Screen Only")) {
                    m_emu->screenshot_full_ui = false;
                    m_emu->screenshot_requested = true;
                }
                ImGui::PopStyleVar();
                ImGui::EndPopup();
            }

            if (m_emu->recording_active.load()) {
                if (ImGui::TabItemButton("[ ] Stop Rec")) {
                    m_emu->recording_stop_requested = true;
                }
            } else {
                if (ImGui::TabItemButton("[O] Record")) {
                    ImGui::OpenPopup("RecordMenuPopup");
                }
                ImGui::SetNextWindowPos(ImVec2(
                    ImGui::GetItemRectMin().x,
                    ImGui::GetItemRectMax().y));
                if (ImGui::BeginPopup("RecordMenuPopup")) {
                    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 8.0f);
                    if (ImGui::MenuItem("Full Calculator")) {
                        m_emu->recording_full_ui = true;
                        m_emu->recording_requested = true;
                    }
                    if (ImGui::MenuItem("Screen Only")) {
                        m_emu->recording_full_ui = false;
                        m_emu->recording_requested = true;
                    }
                    ImGui::PopStyleVar();
                    ImGui::EndPopup();
                }
            }

            if (ImGui::TabItemButton(
                    ThemeManager::Instance().Settings().isDarkMode
                        ? "Light Theme" : "Dark Theme")) {
                if (ThemeManager::Instance().Settings().isDarkMode)
                    ThemeManager::Instance().SetLightMode();
                else
                    ThemeManager::Instance().SetDarkMode();
            }

            ImGui::EndTabBar();
        }

        if (m_emu->screenshot_taken.exchange(false)) {
            screenshot_toast_timer = 3.0f;
        }
        if (screenshot_toast_timer > 0.0f) {
            ImGui::SameLine(ImGui::GetWindowWidth() - 250.0f);
            ImGui::TextColored(
                ImVec4(0.2f, 1.0f, 0.2f, 1.0f),
                "[C] Screenshot Saved!");
            screenshot_toast_timer -= ImGui::GetIO().DeltaTime;
        }
        if (m_emu->recording_active.load()) {
            ImGui::SameLine(ImGui::GetWindowWidth() -
                (screenshot_toast_timer > 0.0f ? 450.0f : 200.0f));
            ImGui::TextColored(
                ImVec4(1.0f, 0.2f, 0.2f, 1.0f),
                "[O] Recording: %u frames",
                m_emu->recording_frame_count.load());
        }

        ImGui::EndMainMenuBar();
    }
#endif
}

// =============================================================================
// CÃC HÃM KHÃC GIá»® NGUYÃN
// =============================================================================

void LoadUIState() {
    std::ifstream f(ui_state_fn);
    if (!f.is_open()) return;

    std::unordered_map<std::string, bool> state;

    std::string line;
    while (std::getline(f, line)) {
        auto pos = line.find('=');
        if (pos == std::string::npos) continue;

        std::string name = line.substr(0, pos);
        bool open = line.substr(pos + 1) == "1";

        state[name] = open;
    }

    for (auto* w : windows) {
        if (!w) continue;

        if (state.find(w->name) != state.end())
            w->open = state[w->name];
    }
} 

void RenderStatusBar() {
	ImGuiViewport* viewport = ImGui::GetMainViewport();
	float barHeight = ImGui::GetFrameHeight() + 4.0f;
	
	ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x, viewport->Pos.y + viewport->Size.y - barHeight));
	ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, barHeight));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 2.0f));
	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.08f, 0.08f, 0.12f, 1.0f));
	
	if (ImGui::Begin("##StatusBar", nullptr, 
		ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | 
		ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
		ImGuiWindowFlags_NoDocking)) {
		
		if (m_emu->GetPaused()) {
			ImGui::TextColored(UIHelpers::kColorWarning, "[||] %s", "StatusBar.Paused"_lc);
		} else {
			ImGui::TextColored(UIHelpers::kColorSuccess, "[>] %s", "StatusBar.Running"_lc);
		}
		
		ImGui::SameLine(0.0f, 20.0f);
		ImGui::TextDisabled("|");
		ImGui::SameLine(0.0f, 20.0f);
		
		ImGui::Text("PC: %05X", pc_cache);
		
		ImGui::SameLine(0.0f, 20.0f);
		ImGui::TextDisabled("|");
		ImGui::SameLine(0.0f, 20.0f);
		
		int bpCount = code_viewer ? (int)code_viewer->GetBreakpointCount() : 0;
		ImGui::Text("BP: %d", bpCount);
	}
	ImGui::End();
	ImGui::PopStyleColor();
	ImGui::PopStyleVar();
}

void gui_loop() {
    if (!m_emu->Running())
        return;

    ImGuiIO& io = ImGui::GetIO();

#if defined(__ANDROID__) || defined(MACOS) || defined(IOS)
    ThemeManager::Instance().UpdateUIScale();
#endif

    ImGui_ImplSDLRenderer2_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
    
    #if !defined(__ANDROID__) && !defined(IOS)
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    
    ImVec2 dockSize = viewport->WorkSize;
    float barHeight = ImGui::GetFrameHeight() + 4.0f;
    dockSize.y -= barHeight;
    ImGui::SetNextWindowSize(dockSize);
    
    ImGui::SetNextWindowViewport(viewport->ID);

    ImGuiWindowFlags host_flags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | 
        ImGuiWindowFlags_NoResize   | ImGuiWindowFlags_NoMove | 
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
        ImGuiWindowFlags_NoBackground;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

    ImGui::Begin("MainDockHost", nullptr, host_flags);
    ImGui::PopStyleVar(3);

    ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
    ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f),
        ImGuiDockNodeFlags_PassthruCentralNode);
    
    ImGui::End();
    #endif

    RenderDebuggerToolbar();
    for (auto win : windows) {
        if (!win) continue;
        win->Render();
    }

    top_bar_size = ImGui::GetCursorPosY();
#if !defined(__ANDROID__) && !defined(IOS)
	RenderStatusBar();
#endif

    ImGui::Render();
    ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData());
    
    #ifndef SINGLE_WINDOW
    SDL_RenderPresent(renderer);
    #endif
}

CodeViewer* test_gui(bool* guiCreated, SDL_Window* wnd, SDL_Renderer* rnd) {
    SDL_SetHint(SDL_HINT_IME_SHOW_UI, "1");
    
    if (window || renderer) {
        gui_cleanup();
        window = nullptr;
        renderer = nullptr;
    }

#ifdef SINGLE_WINDOW
    window = wnd;
    renderer = rnd;
#else
#if defined(__ANDROID__) || defined(IOS)
    window = SDL_CreateWindow("CasioEmuMsvc Debugger",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        (int)ThemeManager::Instance().windowWidth,
        (int)ThemeManager::Instance().windowHeight,
        SDL_WINDOW_RESIZABLE);
#else
	int winX = ThemeManager::Instance().Settings().windowX;
	int winY = ThemeManager::Instance().Settings().windowY;
	int winW = ThemeManager::Instance().Settings().windowW;
	int winH = ThemeManager::Instance().Settings().windowH;

	SDL_Rect bounds;
	if (SDL_GetDisplayUsableBounds(0, &bounds) == 0) {
		if (winW > bounds.w) winW = bounds.w;
		if (winH > bounds.h) winH = bounds.h;

		if (winX != SDL_WINDOWPOS_CENTERED) {
			if (winX < bounds.x) winX = bounds.x;
			if (winX + winW > bounds.x + bounds.w) winX = bounds.x + bounds.w - winW;
		}
		if (winY != SDL_WINDOWPOS_CENTERED) {
			if (winY < bounds.y) winY = bounds.y;
			if (winY + winH > bounds.y + bounds.h) winY = bounds.y + bounds.h - winH;
		}
	}

	window = SDL_CreateWindow("CasioEmuMsvc Debugger",
		winX, winY, winW, winH,
		SDL_WINDOW_RESIZABLE);
#endif
#ifdef _WIN32
    EnableDarkTitleBar(GetSDLWindowHandle(window));
#endif
    renderer = SDL_CreateRenderer(window, -1,
        SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_ACCELERATED);
#endif

    if (!renderer) {
        SDL_Log("Error creating SDL_Renderer!");
        return nullptr;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();

#if defined(__ANDROID__) || defined(IOS)
    ThemeManager::Instance().LoadSettings();
    ThemeManager::Instance().UpdateUIScale();
#endif

    RebuildFont();

    io.IniFilename = "imgui.ini";
    io.WantCaptureKeyboard = true;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer2_Init(renderer);
    if (guiCreated)
        *guiCreated = true;
    for (int i = 0; i < 5000 && !me_mmu; i++)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    
    if (!me_mmu) {
        SDL_Log("MMU not ready!");
        return nullptr;
    }
    auto label_file = m_emu->GetModelFilePath("labels.txt");
    if (std::filesystem::exists(label_file))
        g_labels = parseFile(label_file);
    else
        std::cout << "[Warning] labels.txt doesn't exist.\n";

    if (m_emu->hardware_id == casioemu::HW_FX_5800P) {
        windows.push_back(CreateFx5800FileSystem());
    }

    for (auto item : std::initializer_list<UIWindow*>{
             new CalculatorWindow(),
             new VariableWindow(),
             new HwController(),
             new LabelViewer(),
             new WatchWindow(),
             CreateCallAnalysisWindow(),
             code_viewer = new CodeViewer(),
             injector = new Injector(),
             membp = new Breakpoints(),
             CreateAddressWindow(),
             CreateRopCompilerWindow(),
             new PluginLogWindow(),
             CreateSnapshotWindow(),
             MakeThemeWindow(),
             CreateBitmapViewer(), })
        windows.push_back(item);
    for (auto item : GetEditors())
        windows.push_back(item);
    if (!std::filesystem::exists(ui_state_fn)) {
        for (auto* w : windows) {
            if (w) {
                w->open = true;
                w->bring_to_front_requested = false;
            }
        }
    }
    LoadUIState();
    ui_ready = true;
    
    return nullptr;
}

namespace UIHelpers {

	void JumpToMemory(uint32_t addr) {
		for (auto* win : windows) {
			if (win->name && strcmp(win->name, "Ram") == 0) {
				if (win->GotoMemoryAddress(addr)) return;
			}
		}
		for (auto* win : windows) {
			if (win->name && strcmp(win->name, "PRam") == 0) {
				if (win->GotoMemoryAddress(addr)) return;
			}
		}
		for (auto* win : windows) {
			if (win->GotoMemoryAddress(addr)) return;
		}
	}

	void ClickableAddress(uint32_t addr, JumpTarget defaultTarget) {
		ImGui::PushStyleColor(ImGuiCol_Text, kColorInfo);
		char addrLabel[16];
		snprintf(addrLabel, sizeof(addrLabel), "%05X", addr);
		ImGui::TextUnformatted(addrLabel);
		ImGui::PopStyleColor();

		if (ImGui::IsItemHovered()) {
			ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
			ImGui::BeginTooltip();
			if (defaultTarget == JumpTarget::Code) {
				ImGui::Text("ClickableAddress.CodeJumpTooltip"_lc, addr);
				ImGui::TextDisabled("%s", "ClickableAddress.RightClickHint"_lc);
			} else if (defaultTarget == JumpTarget::Memory) {
				ImGui::Text("ClickableAddress.MemJumpTooltip"_lc, addr);
				ImGui::TextDisabled("%s", "ClickableAddress.RightClickHint"_lc);
			} else {
				ImGui::Text("ClickableAddress.BothTooltip"_lc, addr);
			}
			ImGui::EndTooltip();
		}

		if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
			if (defaultTarget == JumpTarget::Code || defaultTarget == JumpTarget::Both) {
				if (code_viewer) {
					code_viewer->JumpTo(addr);
					code_viewer->BringToFront();
				}
			} else {
				JumpToMemory(addr);
			}
		}

		char popupId[32];
		snprintf(popupId, sizeof(popupId), "##ca_popup_%05X", addr);
		if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
			ImGui::OpenPopup(popupId);
		}
		if (ImGui::BeginPopup(popupId)) {
			ImGui::TextDisabled("0x%05X", addr);
			ImGui::Separator();
			if (ImGui::MenuItem("ClickableAddress.CodeJump"_lc)) {
				if (code_viewer) {
					code_viewer->JumpTo(addr);
					code_viewer->BringToFront();
				}
			}
			if (ImGui::MenuItem("ClickableAddress.MemJump"_lc)) {
				JumpToMemory(addr);
			}
			ImGui::EndPopup();
		}
	}
}


void gui_cleanup() {
#ifndef __ANDROID__
#ifndef SINGLE_WINDOW
	if (window) {
		int x, y, w, h;
		SDL_GetWindowPosition(window, &x, &y);
		SDL_GetWindowSize(window, &w, &h);

		ThemeManager::Instance().Settings().windowX = x;
		ThemeManager::Instance().Settings().windowY = y;
		ThemeManager::Instance().Settings().windowW = w;
		ThemeManager::Instance().Settings().windowH = h;
		ThemeManager::Instance().SaveSettings();
	}
#endif
#endif

	ImGui_ImplSDLRenderer2_Shutdown();
	ImGui_ImplSDL2_Shutdown();
	ImGui::DestroyContext();
	SaveUIState();
	windows.clear();

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
}
