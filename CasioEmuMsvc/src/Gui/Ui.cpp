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
#include "ModelInfo.h"
#include "Random.hpp"
#include "RendererBackend.h"
#include "Theme.h"
#include "VariableWindow.h"
#include "WatchWindow.hpp"
#ifndef CASIOEMU_CORE_WEB
#include "QrCodeWindow.h"
#endif
#ifndef TEST_BUILD
#include "Rop/RopCompilerUI.h"
#include "PluginLogWindow.hpp"
#include "SnapshotWindow.h"
#include "CalculatorWindow.h"
#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"
#ifdef CASIOEMU_CORE_WEB
#include "WebDebuggerGui.h"
#else
#include "imgui/imgui_impl_sdl2.h"
#include "imgui/imgui_impl_sdlrenderer2.h"
#endif
#include <Gui.h>
#include <SDL.h>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>
#include <sstream>
#ifdef ENABLE_SENTRY
#include <sentry.h>
#endif
#include <sdl_win32_extra.h>

// ======================== ERROR LOG ========================
std::vector<std::string> g_error_logs;
const size_t MAX_ERROR_LOGS = 1000;

void LogError(const std::string& msg) {
    if (g_error_logs.size() >= MAX_ERROR_LOGS)
        g_error_logs.erase(g_error_logs.begin());
    g_error_logs.push_back(msg);
}
// ===========================================================

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
SnapshotWindow* snapshot_window = 0;

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
#include "IOSNativeBridge.h"
#endif

static float screenshot_toast_timer = 0.0f;

#ifdef CASIOEMU_CORE_WEB
SDL_Surface* background = nullptr;
SDL_Texture* bg_txt = nullptr;
#endif

static float GetStatusBarHeight() {
	return ImGui::GetFrameHeight() + 4.0f;
}

void RenderStatusBar() {
	ImGuiViewport* viewport = ImGui::GetMainViewport();
	float barHeight = GetStatusBarHeight();

#ifdef __IOS__
	// Thêm safe area bottom (home indicator iPhone)
	float safeBottom = getSafeBottom();
	if (safeBottom < 0.0f) safeBottom = 0.0f;
	float posY = viewport->Pos.y + viewport->Size.y - barHeight - safeBottom;
#else
	float posY = viewport->Pos.y + viewport->Size.y - barHeight;
#endif

	ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x, posY));
	ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, barHeight));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 2.0f));
	// Derive from the current theme's WindowBg so this tracks Light/Dark
	// mode (ThemeManager::SetLightMode/SetDarkMode change WindowBg) instead
	// of being pinned to one hardcoded color regardless of theme.
	ImVec4 statusBg = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
#ifdef CASIOEMU_CORE_WEB
	statusBg.w = std::max(statusBg.w, 0.82f);
#else
	statusBg.x *= 0.85f;
	statusBg.y *= 0.85f;
	statusBg.z *= 0.85f;
	statusBg.w = 1.0f;
#endif
	ImGui::PushStyleColor(ImGuiCol_WindowBg, statusBg);
	
	if (ImGui::Begin("##StatusBar", nullptr, 
		ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | 
		ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
		ImGuiWindowFlags_NoDocking)) {
		
		// Run/Pause state status indicator
		if (m_emu->GetPaused()) {
			ImGui::TextColored(UIHelpers::kColorWarning, "\xe2\x8f\xb8 %s", "StatusBar.Paused"_lc);  // ⏸
		} else {
			ImGui::TextColored(UIHelpers::kColorSuccess, "\xe2\x96\xb6 %s", "StatusBar.Running"_lc); // ▶
		}
		
		ImGui::SameLine(0.0f, 20.0f);
		ImGui::TextDisabled("|");
		ImGui::SameLine(0.0f, 20.0f);
		
		// Current PC
		ImGui::Text("PC: %05X", pc_cache);
		
		ImGui::SameLine(0.0f, 20.0f);
		ImGui::TextDisabled("|");
		ImGui::SameLine(0.0f, 20.0f);
		
		// Breakpoints count
		int bpCount = code_viewer ? (int)code_viewer->GetBreakpointCount() : 0;
		ImGui::Text("BP: %d", bpCount);
	}
	ImGui::End();
	ImGui::PopStyleColor();
	ImGui::PopStyleVar();
}

static ImGuiID RenderDockSpace(float reservedBottom) {
	ImGuiViewport* viewport = ImGui::GetMainViewport();
	float dockHeight = viewport->Size.y - reservedBottom;
	if (dockHeight < 1.0f) dockHeight = 1.0f;

	ImGui::SetNextWindowPos(viewport->Pos);
	ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, dockHeight));
	ImGui::SetNextWindowViewport(viewport->ID);
	ImGui::SetNextWindowBgAlpha(0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	ImGuiWindowFlags flags =
		ImGuiWindowFlags_NoDocking |
		ImGuiWindowFlags_NoTitleBar |
		ImGuiWindowFlags_NoCollapse |
		ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoBringToFrontOnFocus |
		ImGuiWindowFlags_NoNavFocus |
		ImGuiWindowFlags_NoBackground;

	ImGui::Begin("##DebuggerDockSpaceHost", nullptr, flags);
	ImGuiID dockspace_id = ImGui::GetID("DebuggerDockSpace");
	ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);
	ImGui::End();
	ImGui::PopStyleVar(3);
	return dockspace_id;
}

static void RenderDebuggerGuiWindows() {
#if !defined(__ANDROID__) && !defined(__IOS__)
	ImGuiID dockspace_id = RenderDockSpace(GetStatusBarHeight());
#endif
	for (auto win : windows) {
#if !defined(__ANDROID__) && !defined(__IOS__)
		if (dockspace_id != 0) {
			ImGui::SetNextWindowDockID(dockspace_id, ImGuiCond_FirstUseEver);
		}
#endif
		win->Render();
	}
}

// Renders the desktop menu bar content (Debugger Windows list + action buttons).
// Mobile (iOS/Android) no longer uses this — see the static Open/Close all
// overlay in RenderDebuggerToolbar below.
[[maybe_unused]] static void RenderToolbarContent(ImGuiViewport* viewport) {
    bool isPaused = m_emu->GetPaused();

    if (ImGui::BeginTabBar("ToolbarTabs", ImGuiTabBarFlags_FittingPolicyScroll | ImGuiTabBarFlags_NoTooltip)) {

        if (ImGui::TabItemButton("Debugger Windows"))
            ImGui::OpenPopup("DebuggerMenuPopup");

        ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 8.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 8.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,   ImVec2(8.0f, 6.0f));

        ImGui::SetNextWindowPos(ImVec2(ImGui::GetItemRectMin().x, ImGui::GetItemRectMax().y + 2.0f));
        ImGui::SetNextWindowSizeConstraints(ImVec2(0, 0), ImVec2(FLT_MAX, viewport->WorkSize.y * 0.75f));

        if (ImGui::BeginPopup("DebuggerMenuPopup")) {
            ImGui::TextDisabled("Select Window");
            ImGui::Separator();
            for (auto* w : windows) {
                if (w) {
                    if (ImGui::Checkbox(w->name, &w->open))
                        SaveUIState();
                }
            }
            ImGui::EndPopup();
        }
        ImGui::PopStyleVar(3);

        if (std::any_of(windows.begin(), windows.end(), [](UIWindow* w){ return !w->open; })) {
            if (ImGui::TabItemButton("Open All"))
                for (auto* w : windows) if (w) w->open = true;
        } else {
            if (ImGui::TabItemButton("Close All"))
                for (auto* w : windows) if (w) w->open = false;
        }

        if (ImGui::TabItemButton(isPaused ? "[>] Resume" : "[||] Pause"))
            m_emu->SetPaused(!isPaused);

#ifndef CASIOEMU_CORE_WEB
        if (ImGui::TabItemButton("[C] Screenshot"))
            ImGui::OpenPopup("ScreenshotMenuPopup");
        ImGui::SetNextWindowPos(ImVec2(ImGui::GetItemRectMin().x, ImGui::GetItemRectMax().y + 4.0f));
        ImGui::SetNextWindowSize(ImVec2(0, 0));
        if (ImGui::BeginPopup("ScreenshotMenuPopup", ImGuiWindowFlags_NoMove)) {
            if (ImGui::MenuItem("Full Calculator")) {
                m_emu->screenshot_full_ui = true;
                m_emu->screenshot_requested = true;
            }
            if (ImGui::MenuItem("Screen Only")) {
                m_emu->screenshot_full_ui = false;
                m_emu->screenshot_requested = true;
            }
            ImGui::EndPopup();
        }

        if (m_emu->recording_active.load()) {
            if (ImGui::TabItemButton("[ ] Stop Rec"))
                m_emu->recording_stop_requested = true;
        } else {
            if (ImGui::TabItemButton("[O] Record"))
                ImGui::OpenPopup("RecordMenuPopup");
            ImGui::SetNextWindowPos(ImVec2(ImGui::GetItemRectMin().x, ImGui::GetItemRectMax().y + 4.0f));
            ImGui::SetNextWindowSize(ImVec2(0, 0));
            if (ImGui::BeginPopup("RecordMenuPopup", ImGuiWindowFlags_NoMove)) {
                if (ImGui::MenuItem("Full Calculator")) {
                    m_emu->recording_full_ui = true;
                    m_emu->recording_requested = true;
                }
                if (ImGui::MenuItem("Screen Only")) {
                    m_emu->recording_full_ui = false;
                    m_emu->recording_requested = true;
                }
                ImGui::EndPopup();
            }
        }
#endif

        if (ImGui::TabItemButton(ThemeManager::Instance().Settings().isDarkMode ? "Light Theme" : "Dark Theme")) {
            if (ThemeManager::Instance().Settings().isDarkMode)
                ThemeManager::Instance().SetLightMode();
            else
                ThemeManager::Instance().SetDarkMode();
        }

        ImGui::EndTabBar();
    }

#ifndef CASIOEMU_CORE_WEB
    if (m_emu->screenshot_taken.exchange(false))
        screenshot_toast_timer = 3.0f;

    if (screenshot_toast_timer > 0.0f) {
        ImGui::SameLine(ImGui::GetWindowWidth() - 250.0f);
        ImGui::TextColored(ImVec4(0.2f,1.0f,0.2f,1.0f), "[C] Screenshot Saved!");
        screenshot_toast_timer -= ImGui::GetIO().DeltaTime;
    }

    if (m_emu->recording_active.load()) {
        ImGui::SameLine(ImGui::GetWindowWidth() - (screenshot_toast_timer > 0.0f ? 450.0f : 200.0f));
        ImGui::TextColored(ImVec4(1.0f,0.2f,0.2f,1.0f), "[O] Recording: %u frames", m_emu->recording_frame_count.load());
    }
#endif
}

#ifdef __IOS__
static float getSafeAreaTop() {
    float safeTop = getSafeTop();
    if (safeTop <= 0.0f) {
        safeTop = 50.0f; // iPhone notch/dynamic island fallback
    }
    return safeTop;
}
#endif

void RenderDebuggerToolbar() {
    bool isCustom = false;
#if defined(__IOS__) || defined(__ANDROID__)
    isCustom = true;
#endif

    if (isCustom) {
        // ── iOS / Android: static overlay (combo + Open + Close all) ───────
        // Same layout as stock Android: no drag, no collapse, no animation.
#if defined(__IOS__) || defined(__ANDROID__)
        ImGui::SetNextWindowBgAlpha(0.0f);
        ImGui::Begin("Overlay", nullptr,
            ImGuiWindowFlags_NoDecoration |
            ImGuiWindowFlags_NoDocking |
            ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoBackground |
            ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoMove);

        auto& tm = ThemeManager::Instance();
        float safeAreaPadding = tm.padding * 1.5f;
#ifdef __IOS__
        float safeAreaTop = getSafeAreaTop();
#else
        float safeAreaTop = 0.0f;
#endif
        ImGui::SetWindowPos(ImVec2(safeAreaPadding, safeAreaPadding + safeAreaTop));

        float displayWidth = ImGui::GetIO().DisplaySize.x;
        float totalWidth = displayWidth - (safeAreaPadding * 2);
        float spacingBetweenElements = tm.padding * 1.2f;
        float buttonWidth = (totalWidth - spacingBetweenElements * 2) * 0.25f;
        float comboWidth = totalWidth - (buttonWidth * 2) - (spacingBetweenElements * 2);

        static UIWindow* current_filter = nullptr;
        static bool comboPopupOpen = false;
        static bool comboJustOpened = false;
        char comboPreview[64];
        std::snprintf(comboPreview, sizeof(comboPreview), "%s %s",
            current_filter ? current_filter->name : "", comboPopupOpen ? "\xe2\x96\xb2" : "\xe2\x96\xbc"); // ▲ / ▼
        ImVec2 comboScreenPos = ImGui::GetCursorScreenPos();
        ImVec2 comboSize(comboWidth, tm.buttonHeight * 1.2f);
        if (ImGui::Button(comboPreview, comboSize)) {
            comboPopupOpen = !comboPopupOpen;
            comboJustOpened = comboPopupOpen;
        }
        if (comboPopupOpen) {
            // Force the dropdown to always open downward from the combo
            // box, never upward. BeginCombo's built-in auto-flip logic
            // (used when there isn't "enough room" below by ImGui's own
            // calculation) is what put the top row past the unsafe area on
            // iOS — a plain window with an explicit position below the
            // combo box removes that guesswork entirely.
            float dropdownY = comboScreenPos.y + comboSize.y;
            float maxPopupHeight = ImGui::GetIO().DisplaySize.y - dropdownY - safeAreaPadding * 2.0f;
            ImGui::SetNextWindowPos(ImVec2(comboScreenPos.x, dropdownY));
            ImGui::SetNextWindowSize(ImVec2(comboWidth, 0));
            ImGui::SetNextWindowSizeConstraints(ImVec2(comboWidth, 0), ImVec2(comboWidth, std::max(maxPopupHeight, tm.buttonHeight * 3.0f)));
#ifdef __IOS__
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(tm.padding, tm.padding * 1.2f));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(tm.padding, tm.padding * 0.9f));
            ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, tm.padding * 2.2f);
#endif
            if (ImGui::Begin("##cb_dropdown", nullptr,
                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDocking |
                ImGuiWindowFlags_NoSavedSettings |
                ImGuiWindowFlags_AlwaysVerticalScrollbar)) {
                ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
                // A left-click while NOT hovering this window means the
                // click landed outside the dropdown — close it. Skip this
                // check on the frame the popup just opened, since that same
                // click (on the combo button) would otherwise immediately
                // close what it just opened.
                bool clickedOutside = !comboJustOpened &&
                    !ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup) &&
                    ImGui::IsMouseClicked(ImGuiMouseButton_Left);
                for (auto* w : windows) {
                    if (!w) continue;
                    bool is_selected = (current_filter == w);
                    if (ImGui::Selectable(w->name, is_selected)) {
                        current_filter = w;
                        comboPopupOpen = false;
                    }
                }
                if (clickedOutside) comboPopupOpen = false;
                comboJustOpened = false;
            }
            ImGui::End();
#ifdef __IOS__
            ImGui::PopStyleVar(3);
#endif
        }

        ImGui::SameLine(0, spacingBetweenElements);
        ImVec2 buttonSize(buttonWidth, tm.buttonHeight * 1.2f);
        if (ImGui::Button("Open", buttonSize)) {
            if (current_filter != nullptr) {
                current_filter->open = true;
                SaveUIState();
            }
        }

        ImGui::SameLine(0, spacingBetweenElements);
        if (ImGui::Button("Close all", buttonSize)) {
            for (auto* w : windows) {
                if (w) w->open = false;
            }
            SaveUIState();
        }

        top_bar_size = (int)ImGui::GetCursorPosY();
        ImGuiWindow* toolbar_win = ImGui::FindWindowByName("Overlay");
        if (toolbar_win) ImGui::BringWindowToDisplayFront(toolbar_win);
        ImGui::End();
#endif

    } else {
        // ── Desktop: toolbar intentionally removed ──────────────────────────
        // No menu bar rendered here anymore.
    }
}

void LoadUIState() {
    std::ifstream f(ui_state_fn);
    if (!f.is_open()) return;
    std::unordered_map<std::string, bool> state;
    std::string line;
    while (std::getline(f, line)) {
        auto pos = line.find('=');
        if (pos == std::string::npos) continue;
        state[line.substr(0, pos)] = (line.substr(pos + 1) == "1");
    }
    for (auto* w : windows) {
        if (!w) continue;
        if (state.count(w->name)) w->open = state[w->name];
    }
}

// ======================== gui_loop ========================
void gui_loop() {
    if (!m_emu->Running()) return;
    ImGuiIO& io = ImGui::GetIO();
    
#if defined(__ANDROID__) || defined(MACOS) || defined(__IOS__)
    ThemeManager::Instance().UpdateUIScale();
#endif

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
#ifndef CASIOEMU_CORE_WEB
        ImGui_ImplSDL2_ProcessEvent(&event);
#endif

        if (event.type == SDL_MOUSEBUTTONDOWN || event.type == SDL_FINGERDOWN) {
            int x, y;
            if (event.type == SDL_MOUSEBUTTONDOWN) {
                x = event.button.x;
                y = event.button.y;
            } else {
                x = (int)(event.tfinger.x * io.DisplaySize.x);
                y = (int)(event.tfinger.y * io.DisplaySize.y);
            }

            if (SDL_IsTextInputActive()) {
                ImGuiWindow* calc_win = ImGui::FindWindowByName("Calculator");
                bool insideKeyboard = false;
                if (calc_win) {
                    insideKeyboard = (x >= calc_win->Pos.x && x <= calc_win->Pos.x + calc_win->Size.x &&
                                      y >= calc_win->Pos.y && y <= calc_win->Pos.y + calc_win->Size.y);
                }
                bool insideToolbar = (y < 60);

                if (!insideKeyboard && !insideToolbar) {
                    SDL_StopTextInput();
                    ImGui::SetWindowFocus(nullptr);
                }
            }
        }
    }

#ifndef CASIOEMU_CORE_WEB
    ImGui_ImplSDLRenderer2_NewFrame();
    ImGui_ImplSDL2_NewFrame();
#endif
    ImGui::NewFrame();

#if !defined(__ANDROID__) && !defined(__IOS__)
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImVec2 dockSize = viewport->WorkSize;
    float barHeight = ImGui::GetFrameHeight() + 4.0f;
    dockSize.y -= barHeight;
    ImGui::SetNextWindowSize(dockSize);
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGuiWindowFlags host_flags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize   | ImGuiWindowFlags_NoMove     |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
        ImGuiWindowFlags_NoBackground;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,  0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,   ImVec2(0.0f, 0.0f));
    ImGui::Begin("MainDockHost", nullptr, host_flags);
    ImGui::PopStyleVar(3);
    ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
    ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);
    ImGui::End();
#endif

    RenderDebuggerToolbar();
    // FIX #2: Removed duplicate BringWindowToDisplayFront here — already done inside RenderDebuggerToolbar

    ImGuiWindow* hovered_win = ImGui::GetCurrentContext()->HoveredWindow;
    bool hovering_other_ui = (hovered_win != nullptr) &&
        (!hovered_win->Name || strstr(hovered_win->Name, "Calculator") == nullptr) &&
        (!hovered_win->Name || strstr(hovered_win->Name, "Overlay") == nullptr) &&
        (!hovered_win->Name || strstr(hovered_win->Name, "##StatusBar") == nullptr) &&
        (!hovered_win->Name || strstr(hovered_win->Name, "DebuggerMenuPopup") == nullptr);

    bool backup_down     = io.MouseDown[0];
    bool backup_clicked  = io.MouseClicked[0];
    bool backup_released = io.MouseReleased[0];

    for (auto win : windows) {
        if (!win) continue;
        bool is_calculator = (win->name && strstr(win->name, "Calculator") != nullptr);
        if (is_calculator) {
            ImGuiWindow* imgui_win = ImGui::FindWindowByName(win->name);
            if (imgui_win) {
                imgui_win->Flags |= ImGuiWindowFlags_NoBringToFrontOnFocus;
                // FIX #4: Use a per-window flag instead of a static bool that
                // never resets across destroy/recreate cycles.
                if (!(imgui_win->Flags & ImGuiWindowFlags_NoNav)) { // sentinel reuse avoided
                    // push back only once per window lifetime using the hidden flag trick:
                    // check if it's already at back by comparing display order
                    if (imgui_win->BeginOrderWithinContext > 0) {
                        ImGui::BringWindowToDisplayBack(imgui_win);
                    }
                }
            }
        }
        if (is_calculator && hovering_other_ui) {
            io.MouseDown[0]     = false;
            io.MouseClicked[0]  = false;
            io.MouseReleased[0] = false;
        }
        win->Render();
        if (is_calculator && hovering_other_ui) {
            io.MouseDown[0]     = backup_down;
            io.MouseClicked[0]  = backup_clicked;
            io.MouseReleased[0] = backup_released;
        }
    }

    top_bar_size = ImGui::GetCursorPosY();
#if defined(__IOS__) || defined(__ANDROID__)
    // Re-assert the toolbar overlay's stacking order now that every other
    // debugger window (Variables, Ram, CodeViewer, ...) has had its Begin()
    // called for this frame. ImGui stacks later-begun windows above earlier
    // ones, so doing this only once right after the toolbar's own End() (as
    // before) got silently overridden by any window opened afterward,
    // making the combo/Open/Close-all buttons unclickable whenever an
    // overlapping debugger window was open.
    {
        ImGuiWindow* toolbar_win = ImGui::FindWindowByName("Overlay");
        if (toolbar_win) ImGui::BringWindowToDisplayFront(toolbar_win);
    }
#endif
#if !defined(__ANDROID__) && !defined(__IOS__)
    RenderStatusBar();
#endif
    ImGui::Render();
#ifndef CASIOEMU_CORE_WEB
    ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData());
#endif
#ifndef SINGLE_WINDOW
    SDL_RenderPresent(renderer);
#endif
#endif
}
static CodeViewer* CreateDebuggerGuiWindows() {
	while (!me_mmu)
		std::this_thread::sleep_for(std::chrono::microseconds(1));
	std::filesystem::path label_file = m_emu->GetModelFilePath("labels.txt");
	if (!label_file.empty() && std::filesystem::exists(label_file))
		g_labels = parseFile(label_file.string());
	else if (!m_emu->IsMemoryModel())
		std::cout << "[Warning] " << label_file.string() << " doesn't exist. You can consider create one for better debugging experiences. Format: address(0x1234),func name(can be quoted)\n";

	if (m_emu->hardware_id == casioemu::HW_FX_5800P) {
		windows.push_back(CreateFx5800FileSystem());
	}

	if (m_emu->hardware_id != casioemu::HW_SOLARII && !casioemu::IsEpsFamily(m_emu->hardware_id)) {
		windows.push_back(new VariableWindow());
	}

	windows.push_back(new HwController());
	windows.push_back(new LabelViewer());
	auto* watch_window = new WatchWindow();
	windows.push_back(watch_window);
	windows.push_back(CreateCallAnalysisWindow());
	windows.push_back(code_viewer = new CodeViewer());
	if (!casioemu::IsEpsFamily(m_emu->hardware_id))
		windows.push_back(injector = new Injector());
	membp = new Breakpoints();
	windows.push_back(membp);
	windows.push_back(CreateAddressWindow());
	if (!casioemu::IsEpsFamily(m_emu->hardware_id)) {
#if !defined(TEST_BUILD)
		windows.push_back(CreateRopCompilerWindow());
#endif
	}
#if !defined(TEST_BUILD) && !defined(CASIOEMU_CORE_WEB)
	windows.push_back(new PluginLogWindow());
#endif
#if !defined(TEST_BUILD)
	windows.push_back(snapshot_window = static_cast<SnapshotWindow*>(CreateSnapshotWindow()));
#endif
#ifndef CASIOEMU_CORE_WEB
	if (!casioemu::IsEpsFamily(m_emu->hardware_id))
		windows.push_back(new QrCodeWindow());
#endif
	windows.push_back(MakeThemeWindow());
	auto* bitmap_window = CreateBitmapViewer();
	windows.push_back(bitmap_window);
	for (auto item : GetEditors()) {
		windows.push_back(item);
	}

#if defined(__IOS__) || defined(__ANDROID__)
	// Mobile: chỉ hiện màn hình máy tính theo mặc định (giống bản gốc),
	// các cửa sổ debugger phải được mở thủ công qua "Debugger Windows".
	for (auto* item : windows) {
		if (!item) continue;
		bool is_calculator = (item->name && strcmp(item->name, "Calculator") == 0);
		item->open = is_calculator;
		item->bring_to_front_requested = false;
	}
#endif

	return 0;
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
#if defined(__ANDROID__) || defined(__IOS__)
    window = SDL_CreateWindow("CasioEmuMsvc Debugger",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
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
        winX, winY, winW, winH, SDL_WINDOW_RESIZABLE);
#endif
#ifdef _WIN32
    EnableDarkTitleBar(GetSDLWindowHandle(window));
#endif
	casioemu::SetPreferredRendererDriverHint();
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
#if defined(__ANDROID__) || defined(__IOS__)
    ThemeManager::Instance().LoadSettings();
    ThemeManager::Instance().UpdateUIScale();
#endif
    RebuildFont();
    io.IniFilename = "imgui.ini";
    io.WantCaptureKeyboard = true;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
#ifndef CASIOEMU_CORE_WEB
    ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer2_Init(renderer);
#endif
    if (guiCreated) *guiCreated = true;

    for (int i = 0; i < 5000 && !me_mmu; i++)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    if (!me_mmu) { SDL_Log("MMU not ready!"); return nullptr; }

	ThemeManager::Instance().RequestFontRebuild();
	ThemeManager::Instance().ProcessFontRebuild();

	if (guiCreated)
		*guiCreated = true;

	auto* result = CreateDebuggerGuiWindows();

    if (!std::filesystem::exists(ui_state_fn)) {
#if defined(__IOS__) || defined(__ANDROID__)
        // Mobile: chỉ hiện màn hình máy tính theo mặc định (giống bản gốc),
        // các cửa sổ debugger phải được mở thủ công qua "Debugger Windows".
        for (auto* w : windows) {
            if (!w) continue;
            bool is_calculator = (w->name && strcmp(w->name, "Calculator") == 0);
            w->open = is_calculator;
            w->bring_to_front_requested = false;
        }
#else
        for (auto* w : windows)
            if (w) { w->open = true; w->bring_to_front_requested = false; }
#endif
    }
    LoadUIState();
    ui_ready = true;
    return result;
}

#ifdef CASIOEMU_CORE_WEB
void InitWebDebuggerGuiWindows() {
	if (windows.empty()) {
		CreateDebuggerGuiWindows();
	}
}

void RenderWebDebuggerGuiWindows() {
	RenderDebuggerGuiWindows();
}

void CleanupWebDebuggerGuiWindows() {
	for (auto* win : windows) {
		delete win;
	}
	windows.clear();
	code_viewer = nullptr;
	injector = nullptr;
	membp = nullptr;
	g_labels.clear();
}
#endif

namespace UIHelpers {
	void JumpToMemory(uint32_t addr) {
		// Prefer the "Ram" window; fall back to any window that overrides GotoMemoryAddress.
		UIWindow* fallback = nullptr;
		for (auto* win : windows) {
			const char* n = win->name;
			if (n && strcmp(n, "Ram") == 0) {
				win->GotoMemoryAddress(addr);
				win->BringToFront();
				return;
			}
			// Track first editor-like window as fallback
			if (!fallback && n && (strcmp(n, "Rom") == 0 || strcmp(n, "All") == 0
				|| strcmp(n, "PRam") == 0 || strcmp(n, "Flash") == 0)) {
				fallback = win;
			}
		}
		if (fallback) {
			fallback->GotoMemoryAddress(addr);
			fallback->BringToFront();
		}
	}

	void ClickableAddress(uint32_t addr, JumpTarget defaultTarget) {
		char addrLabel[16];
		snprintf(addrLabel, sizeof(addrLabel), "%05X", addr);
		ImGui::PushID(addrLabel);
		const ImVec2 textSize = ImGui::CalcTextSize(addrLabel);
		const ImVec2 textPos = ImGui::GetCursorScreenPos();
		ImGui::InvisibleButton("##clickable_address", textSize);
		const bool hovered = ImGui::IsItemHovered();
		const bool leftClicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
		const bool rightClicked = ImGui::IsItemClicked(ImGuiMouseButton_Right);
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		const ImU32 textColor = ImGui::GetColorU32(hovered ? ImVec4(0.55f, 0.72f, 1.0f, 1.0f) : kColorInfo);
		drawList->AddText(textPos, textColor, addrLabel);
		if (hovered) {
			const float underlineY = textPos.y + textSize.y;
			drawList->AddLine(ImVec2(textPos.x, underlineY), ImVec2(textPos.x + textSize.x, underlineY), textColor);
		}

		if (hovered) {
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

		// Left-click: default action
		if (leftClicked) {
			if (defaultTarget == JumpTarget::Code || defaultTarget == JumpTarget::Both) {
				if (code_viewer) {
					code_viewer->JumpTo(addr);
					code_viewer->BringToFront();
				}
			} else {
				JumpToMemory(addr);
			}
		}

		// Right-click: context menu with both options
		char popupId[32];
		snprintf(popupId, sizeof(popupId), "##ca_popup_%05X", addr);
		if (rightClicked) {
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
		ImGui::PopID();
	}
}

void gui_cleanup() {
#ifndef CASIOEMU_CORE_WEB
#if !defined(__ANDROID__) && !defined(__IOS__)
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
#ifndef CASIOEMU_CORE_WEB
    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
#endif
    ImGui::DestroyContext();
    SaveUIState();
    windows.clear();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
#endif
#else
	CleanupWebDebuggerGuiWindows();
#endif
}
