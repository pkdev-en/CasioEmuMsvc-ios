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
#include "imgui/imgui_internal.h"
#include "imgui/imgui_impl_sdl2.h"
#include "imgui/imgui_impl_sdlrenderer2.h"
#include <Gui.h>
#include <SDL.h>
#include <algorithm>
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

// ===================== TOOLBAR STATE (iOS) =====================
static float g_toolbar_posY     = -1.0f;
static float g_toolbar_targetY  = -1.0f;
static bool  g_toolbar_visible  = true;
static float g_toolbar_anim     = 1.0f;

static bool  g_toolbar_dragging    = false;
static float g_toolbar_drag_startY = 0.0f;
static float g_toolbar_drag_origY  = 0.0f;

static const float TOOLBAR_ANIM_SPEED = 8.0f;
static const float STATUS_BAR_HEIGHT = 50.0f;

static void SaveToolbarPos(float y, bool visible) {
    std::ofstream f("toolbar_pos.txt");
    if (f.is_open()) f << y << " " << (visible ? 1 : 0);
}
static void LoadToolbarPos(float& y, bool& visible) {
    std::ifstream f("toolbar_pos.txt");
    int v = 1;
    if (f.is_open()) { f >> y >> v; }
    visible = (v != 0);
}
// ==============================================================

#ifdef __IOS__
// getSafeTop() is implemented in IOSNativeBridge.mm (compiled as
// Objective-C++) — Ui.cpp itself is plain C++ and cannot contain
// Objective-C syntax like [UIApplication sharedApplication] directly.
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
#if defined(__IOS__)
    isCustom = true;
#endif

    bool opened = false;
    if (isCustom) {
#if defined(__IOS__)
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        float toolbarH = ImGui::GetFrameHeight() + 8.0f;
        float dt = ImGui::GetIO().DeltaTime;

        // Lấy safe area top
        float safeAreaTop = getSafeAreaTop();

        // ── Khởi tạo lần đầu ──────────────────────────────────
        if (g_toolbar_posY < 0.0f) {
            float savedY = -1.0f; bool savedVis = true;
            LoadToolbarPos(savedY, savedVis);
            // Toolbar nằm DƯỚI safe area (thanh status bar)
            float defaultY = viewport->WorkPos.y + safeAreaTop;
            g_toolbar_posY    = (savedY >= 0.0f) ? savedY : defaultY;
            g_toolbar_targetY = g_toolbar_posY;
            g_toolbar_visible = savedVis;
            g_toolbar_anim    = savedVis ? 1.0f : 0.0f;
        }

        // ── Clamp vùng hợp lệ ─────────────────────────────────
        float minY = viewport->WorkPos.y + safeAreaTop;
        float maxY = viewport->WorkPos.y + viewport->WorkSize.y - toolbarH;
        g_toolbar_targetY = std::clamp(g_toolbar_targetY, minY, maxY);

        // ── Animation ───────────────────────────────────────────
        float animTarget = g_toolbar_visible ? 1.0f : 0.0f;
        float animDelta  = TOOLBAR_ANIM_SPEED * dt;
        if (g_toolbar_anim < animTarget)
            g_toolbar_anim = std::min(g_toolbar_anim + animDelta, animTarget);
        else
            g_toolbar_anim = std::max(g_toolbar_anim - animDelta, animTarget);

        float t = 1.0f - (1.0f - g_toolbar_anim) * (1.0f - g_toolbar_anim) * (1.0f - g_toolbar_anim);
        float hiddenY = g_toolbar_targetY - toolbarH - 4.0f;
        float renderY = hiddenY + t * (g_toolbar_targetY - hiddenY);

        if (!g_toolbar_dragging) {
            float lerpSpeed = TOOLBAR_ANIM_SPEED * dt;
            g_toolbar_posY += (g_toolbar_targetY - g_toolbar_posY) * std::min(lerpSpeed, 1.0f);
        }

        if (g_toolbar_anim <= 0.001f && !g_toolbar_visible)
            return;

        ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x, renderY), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x, toolbarH));
        
        ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize,   ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,   ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,     ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
            ImVec2(ImGui::GetStyle().FramePadding.x,
                   ImGui::GetStyle().FramePadding.y + 4.0f));

        ImGui::SetNextWindowBgAlpha(t);

        opened = ImGui::Begin("##DebuggerToolbar", nullptr,
            ImGuiWindowFlags_NoTitleBar  | ImGuiWindowFlags_NoResize  |
            ImGuiWindowFlags_NoMove      | ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_MenuBar |
            ImGuiWindowFlags_NoDocking   | ImGuiWindowFlags_NavFlattened |
            ImGuiWindowFlags_AlwaysAutoResize);

        // Đưa toolbar lên trên cùng
        ImGuiWindow* toolbar_win = ImGui::FindWindowByName("##DebuggerToolbar");
        if (toolbar_win) {
            ImGui::BringWindowToDisplayFront(toolbar_win);
        }

        // ── Drag handle ────────────────────────────────────────
        ImVec2 winSize = ImGui::GetWindowSize();
        ImGui::SetCursorPos(ImVec2(0, 0));
        ImGui::InvisibleButton("##toolbar_drag_handle", winSize);

        ImGuiIO& io = ImGui::GetIO();
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 4.0f)) {
            if (!g_toolbar_dragging) {
                g_toolbar_dragging     = true;
                g_toolbar_drag_startY  = io.MousePos.y;
                g_toolbar_drag_origY   = g_toolbar_targetY;
            }
            float delta = io.MousePos.y - g_toolbar_drag_startY;
            g_toolbar_targetY = std::clamp(g_toolbar_drag_origY + delta, minY, maxY);
        } else {
            if (g_toolbar_dragging) {
                g_toolbar_dragging = false;
                float centerY = (minY + maxY) / 2.0f;
                if (g_toolbar_targetY < centerY) {
                    g_toolbar_targetY = minY;
                } else {
                    g_toolbar_targetY = maxY;
                }
                g_toolbar_posY = g_toolbar_targetY;
                SaveToolbarPos(g_toolbar_targetY, g_toolbar_visible);
            }
        }
#endif
    } else {
        opened = ImGui::BeginMainMenuBar();
    }

    if (opened) {
        bool showMenu = true;
        if (isCustom) showMenu = ImGui::BeginMenuBar();

        if (showMenu) {
            if (ImGui::BeginTabBar("ToolbarTabs",
                ImGuiTabBarFlags_FittingPolicyScroll | ImGuiTabBarFlags_NoTooltip))
            {
#if defined(__IOS__)
                if (ImGui::TabItemButton(g_toolbar_visible ? "[–] Hide" : "[+] Show")) {
                    g_toolbar_visible = !g_toolbar_visible;
                    SaveToolbarPos(g_toolbar_targetY, g_toolbar_visible);
                }
#endif

                if (ImGui::TabItemButton("Debugger Windows"))
                    ImGui::OpenPopup("DebuggerMenuPopup");
                ImGui::SetNextWindowPos(ImVec2(ImGui::GetItemRectMin().x, ImGui::GetItemRectMax().y));
                if (ImGui::BeginPopup("DebuggerMenuPopup")) {
                    for (auto* w : windows) {
                        if (w && ImGui::MenuItem(w->name, nullptr, &w->open))
                            SaveUIState();
                    }
                    ImGui::EndPopup();
                }

                if (std::any_of(windows.begin(), windows.end(), [](UIWindow* w){ return !w->open; })) {
                    if (ImGui::TabItemButton("Open All"))
                        for (auto* w : windows) if (w) w->open = true;
                } else {
                    if (ImGui::TabItemButton("Close All"))
                        for (auto* w : windows) if (w) w->open = false;
                }

#if defined(__ANDROID__) || defined(__IOS__)
                if (ImGui::TabItemButton("[v] Hide KB")) {
                    SDL_StopTextInput();
                    ImGui::SetWindowFocus(nullptr);
                }
#endif

                bool isPaused = m_emu->GetPaused();
                if (ImGui::TabItemButton(isPaused ? "[>] Resume" : "[||] Pause"))
                    m_emu->SetPaused(!isPaused);

                if (ImGui::TabItemButton("[C] Screenshot"))
                    ImGui::OpenPopup("ScreenshotMenuPopup");
                ImGui::SetNextWindowPos(ImVec2(ImGui::GetItemRectMin().x, ImGui::GetItemRectMax().y));
                if (ImGui::BeginPopup("ScreenshotMenuPopup")) {
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
                    ImGui::SetNextWindowPos(ImVec2(ImGui::GetItemRectMin().x, ImGui::GetItemRectMax().y));
                    if (ImGui::BeginPopup("RecordMenuPopup")) {
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

                if (ImGui::TabItemButton(ThemeManager::Instance().Settings().isDarkMode
                    ? "Light Theme" : "Dark Theme"))
                {
                    if (ThemeManager::Instance().Settings().isDarkMode)
                        ThemeManager::Instance().SetLightMode();
                    else
                        ThemeManager::Instance().SetDarkMode();
                }

                ImGui::EndTabBar();
            }

            if (m_emu->screenshot_taken.exchange(false))
                screenshot_toast_timer = 3.0f;

            if (screenshot_toast_timer > 0.0f) {
                ImGui::SameLine(ImGui::GetWindowWidth() - 250.0f);
                ImGui::TextColored(ImVec4(0.2f,1.0f,0.2f,1.0f), "[C] Screenshot Saved!");
                screenshot_toast_timer -= ImGui::GetIO().DeltaTime;
            }

            if (m_emu->recording_active.load()) {
                ImGui::SameLine(ImGui::GetWindowWidth() -
                    (screenshot_toast_timer > 0.0f ? 450.0f : 200.0f));
                ImGui::TextColored(ImVec4(1.0f,0.2f,0.2f,1.0f),
                    "[O] Recording: %u frames", m_emu->recording_frame_count.load());
            }

            if (isCustom) ImGui::EndMenuBar();
        }

        if (isCustom) {
            ImGui::End();
            ImGui::PopStyleVar(4);
        } else {
            ImGui::EndMainMenuBar();
        }
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

void RenderStatusBar() {
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    float barHeight = ImGui::GetFrameHeight() + 4.0f;
    ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x, viewport->Pos.y + viewport->Size.y - barHeight));
    ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, barHeight));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 2.0f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.08f, 0.08f, 0.12f, 1.0f));
    if (ImGui::Begin("##StatusBar", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove     | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoDocking))
    {
        if (m_emu->GetPaused())
            ImGui::TextColored(UIHelpers::kColorWarning, "[||] %s", "StatusBar.Paused"_lc);
        else
            ImGui::TextColored(UIHelpers::kColorSuccess, "[>] %s",  "StatusBar.Running"_lc);
        ImGui::SameLine(0.0f, 20.0f); ImGui::TextDisabled("|");
        ImGui::SameLine(0.0f, 20.0f); ImGui::Text("PC: %05X", pc_cache);
        ImGui::SameLine(0.0f, 20.0f); ImGui::TextDisabled("|");
        ImGui::SameLine(0.0f, 20.0f);
        int bpCount = code_viewer ? (int)code_viewer->GetBreakpointCount() : 0;
        ImGui::Text("BP: %d", bpCount);
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}

// ======================== FIX: gui_loop với xử lý ẩn bàn phím ========================
void gui_loop() {
    if (!m_emu->Running()) return;
    ImGuiIO& io = ImGui::GetIO();
    
#if defined(__ANDROID__) || defined(MACOS) || defined(__IOS__)
    ThemeManager::Instance().UpdateUIScale();
#endif

    // ─── Xử lý sự kiện SDL để ẩn bàn phím khi click ngoài ───
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        // Đẩy event cho ImGui xử lý
        ImGui_ImplSDL2_ProcessEvent(&event);

        // Bắt click chuột hoặc chạm
        if (event.type == SDL_MOUSEBUTTONDOWN || event.type == SDL_FINGERDOWN) {
            int x, y;
            if (event.type == SDL_MOUSEBUTTONDOWN) {
                x = event.button.x;
                y = event.button.y;
            } else {
                x = (int)(event.tfinger.x * io.DisplaySize.x);
                y = (int)(event.tfinger.y * io.DisplaySize.y);
            }

            // Nếu bàn phím đang mở
            if (SDL_IsTextInputActive()) {
                // Lấy vùng cửa sổ Calculator
                ImGuiWindow* calc_win = ImGui::FindWindowByName("Calculator");
                bool insideKeyboard = false;
                if (calc_win) {
                    insideKeyboard = (x >= calc_win->Pos.x && x <= calc_win->Pos.x + calc_win->Size.x &&
                                      y >= calc_win->Pos.y && y <= calc_win->Pos.y + calc_win->Size.y);
                }
                // Toolbar nằm ở đỉnh (y < 60) - điều chỉnh nếu cần
                bool insideToolbar = (y < 60);

                // Click ngoài → ẩn bàn phím
                if (!insideKeyboard && !insideToolbar) {
                    SDL_StopTextInput();
                    ImGui::SetWindowFocus(nullptr);
                }
            }
        }
    }
    // ──────────────────────────────────────────────────────────

    ImGui_ImplSDLRenderer2_NewFrame();
    ImGui_ImplSDL2_NewFrame();
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

    ImGuiWindow* hovered_win = ImGui::GetCurrentContext()->HoveredWindow;
    bool hovering_other_ui = (hovered_win != nullptr) &&
        (!hovered_win->Name || strstr(hovered_win->Name, "Calculator") == nullptr);

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
                static bool calc_pushed_back = false;
                if (!calc_pushed_back) {
                    ImGui::BringWindowToDisplayBack(imgui_win);
                    calc_pushed_back = true;
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
#if !defined(__ANDROID__) && !defined(__IOS__)
    RenderStatusBar();
#endif
    ImGui::Render();
    ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData());
#ifndef SINGLE_WINDOW
    SDL_RenderPresent(renderer);
#endif
}
// =====================================================================================

// ==================== CLASS ERROR LOG WINDOW ====================
class ErrorLogWindow : public UIWindow {
public:
    ErrorLogWindow() : UIWindow("Error Log") {}
    virtual void RenderCore() override {
        if (ImGui::Button("Copy All")) {
            std::string full_log;
            for (const auto& line : g_error_logs) full_log += line + "\n";
            ImGui::SetClipboardText(full_log.c_str());
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear")) g_error_logs.clear();
        ImGui::SameLine();
        ImGui::TextDisabled("(max %zu lines)", MAX_ERROR_LOGS);
        ImGui::Separator();
        ImGui::BeginChild("ErrorLogScrolling", ImVec2(0,0), false, ImGuiWindowFlags_HorizontalScrollbar);
        for (const auto& line : g_error_logs) {
            if (line.find("Function:") == 0)
                ImGui::TextColored(ImVec4(0.2f,0.8f,0.2f,1.0f), "%s", line.c_str());
            else if (line.find("0x") != std::string::npos)
                ImGui::TextColored(ImVec4(0.3f,0.6f,1.0f,1.0f), "%s", line.c_str());
            else
                ImGui::TextWrapped("%s", line.c_str());
        }
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
            ImGui::SetScrollHereY(1.0f);
        ImGui::EndChild();
    }
};
// ==============================================================

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
    ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer2_Init(renderer);
    if (guiCreated) *guiCreated = true;

    // Load vị trí toolbar đã lưu ngay khi khởi động
    {
        float savedY = -1.0f; bool savedVis = true;
        LoadToolbarPos(savedY, savedVis);
        if (savedY >= 0.0f) {
            g_toolbar_posY    = savedY;
            g_toolbar_targetY = savedY;
        } else {
            // Nếu chưa có file lưu, đặt mặc định dưới status bar
            ImGuiViewport* viewport = ImGui::GetMainViewport();
#ifdef __IOS__
            float safeAreaTop = getSafeAreaTop();
            g_toolbar_posY = viewport->WorkPos.y + safeAreaTop;
#else
            g_toolbar_posY = viewport->WorkPos.y + STATUS_BAR_HEIGHT;
#endif
            g_toolbar_targetY = g_toolbar_posY;
        }
        g_toolbar_visible = savedVis;
        g_toolbar_anim    = savedVis ? 1.0f : 0.0f;
    }

    for (int i = 0; i < 5000 && !me_mmu; i++)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    if (!me_mmu) { SDL_Log("MMU not ready!"); return nullptr; }

    auto label_file = m_emu->GetModelFilePath("labels.txt");
    if (std::filesystem::exists(label_file))
        g_labels = parseFile(label_file);
    else
        std::cout << "[Warning] labels.txt doesn't exist.\n";

    if (m_emu->hardware_id == casioemu::HW_FX_5800P)
        windows.push_back(CreateFx5800FileSystem());

    for (auto item : std::initializer_list<UIWindow*>{
             new CalculatorWindow(),
             new VariableWindow(),
             new HwController(),
             new LabelViewer(),
             new WatchWindow(),
             CreateCallAnalysisWindow(),
             code_viewer = new CodeViewer(),
             injector    = new Injector(),
             membp       = new Breakpoints(),
             CreateAddressWindow(),
             CreateRopCompilerWindow(),
             new PluginLogWindow(),
             CreateSnapshotWindow(),
             MakeThemeWindow(),
             CreateBitmapViewer(),
             new ErrorLogWindow()
         })
        windows.push_back(item);

    for (auto item : GetEditors())
        windows.push_back(item);

    if (!std::filesystem::exists(ui_state_fn)) {
        for (auto* w : windows)
            if (w) { w->open = true; w->bring_to_front_requested = false; }
    }
    LoadUIState();
    ui_ready = true;
    return nullptr;
}

namespace UIHelpers {
    void JumpToMemory(uint32_t addr) {
        for (auto* win : windows)
            if (win->name && strcmp(win->name, "Ram") == 0 && win->GotoMemoryAddress(addr)) return;
        for (auto* win : windows)
            if (win->name && strcmp(win->name, "PRam") == 0 && win->GotoMemoryAddress(addr)) return;
        for (auto* win : windows)
            if (win->GotoMemoryAddress(addr)) return;
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
                if (code_viewer) { code_viewer->JumpTo(addr); code_viewer->BringToFront(); }
            } else { JumpToMemory(addr); }
        }
        char popupId[32];
        snprintf(popupId, sizeof(popupId), "##ca_popup_%05X", addr);
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
            ImGui::OpenPopup(popupId);
        if (ImGui::BeginPopup(popupId)) {
            ImGui::TextDisabled("0x%05X", addr);
            ImGui::Separator();
            if (ImGui::MenuItem("ClickableAddress.CodeJump"_lc))
                if (code_viewer) { code_viewer->JumpTo(addr); code_viewer->BringToFront(); }
            if (ImGui::MenuItem("ClickableAddress.MemJump"_lc))
                JumpToMemory(addr);
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
