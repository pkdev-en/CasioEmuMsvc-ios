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
/*#include <fstream>

void DebugLog(const std::string& msg) {
    static std::ofstream log("debug_log.txt", std::ios::app);
    if (log.is_open()) {
        log << msg << std::endl;
        log.flush();
    }
}*/

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
void RenderDebuggerToolbar() {
    bool isCustom = false;
#if defined(IOS)
    isCustom = true;
#endif

    bool opened = false;
    if (isCustom) {
#if defined(IOS)
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        float headerY = std::max(viewport->WorkPos.y, 55.0f);
        ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x, headerY));
        ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x, ImGui::GetFrameHeight() + 8.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x, ImGui::GetStyle().FramePadding.y + 4.0f));
        
        opened = ImGui::Begin("##DebuggerToolbar", nullptr, 
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | 
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_MenuBar | 
            ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NavFlattened);
#endif
    } else {
        opened = ImGui::BeginMainMenuBar();
    }

    if (opened) {
        bool showMenu = true;
        if (isCustom) {
            showMenu = ImGui::BeginMenuBar();
        }
        
        if (showMenu) {
            if (ImGui::BeginTabBar("ToolbarTabs", ImGuiTabBarFlags_FittingPolicyScroll | ImGuiTabBarFlags_NoTooltip)) {
                
                if (ImGui::TabItemButton("Debugger Windows")) {
                    ImGui::OpenPopup("DebuggerMenuPopup");
                }
                ImGui::SetNextWindowPos(ImVec2(ImGui::GetItemRectMin().x, ImGui::GetItemRectMax().y));
                if (ImGui::BeginPopup("DebuggerMenuPopup")) {
                    for (auto* w : windows) {
                        if (w && ImGui::MenuItem(w->name, nullptr, &w->open)) {
                            SaveUIState();
                        }
                    }
                    ImGui::EndPopup();
                }

                if (std::any_of(windows.begin(), windows.end(), [](UIWindow* w){ return !w->open; })) {
                    if (ImGui::TabItemButton("Open All")) {
                        for (auto* w : windows) if (w) w->open = true;
                    }
                }
                else {
                    if (ImGui::TabItemButton("Close All")) {
                        for (auto* w : windows) if (w) w->open = false;
                    }
                }

#if defined(__ANDROID__) || defined(IOS)
                if (ImGui::TabItemButton("[v] Hide KB")) {
                    SDL_StopTextInput();
                    ImGui::SetWindowFocus(nullptr);
                }
#endif

                bool isPaused = m_emu->GetPaused();
                if (ImGui::TabItemButton(isPaused ? "[>] Resume" : "[||] Pause")) {
                    m_emu->SetPaused(!isPaused);
                }

                if (ImGui::TabItemButton("[C] Screenshot")) {
                    ImGui::OpenPopup("ScreenshotMenuPopup");
                }
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
                    if (ImGui::TabItemButton("[ ] Stop Rec")) {
                        m_emu->recording_stop_requested = true;
                    }
                } else {
                    if (ImGui::TabItemButton("[O] Record")) {
                        ImGui::OpenPopup("RecordMenuPopup");
                    }
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

                if (ImGui::TabItemButton(ThemeManager::Instance().Settings().isDarkMode ? "Light Theme" : "Dark Theme")) {
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
                ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "[C] Screenshot Saved!");
                screenshot_toast_timer -= ImGui::GetIO().DeltaTime;
            }

            if (m_emu->recording_active.load()) {
                ImGui::SameLine(ImGui::GetWindowWidth() - (screenshot_toast_timer > 0.0f ? 450.0f : 200.0f));
                ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), "[O] Recording: %u frames", m_emu->recording_frame_count.load());
            }

            if (isCustom) {
                ImGui::EndMenuBar();
            }
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
		
		// Run/Pause state status indicator
		if (m_emu->GetPaused()) {
			ImGui::TextColored(UIHelpers::kColorWarning, "[||] %s", "StatusBar.Paused"_lc);  // ⏸
		} else {
			ImGui::TextColored(UIHelpers::kColorSuccess, "[>] %s", "StatusBar.Running"_lc); // ▶
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
    
      // --- BẮT ĐẦU DOCKSPACE ---
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    
    ImVec2 dockSize = viewport->WorkSize;
    float barHeight = ImGui::GetFrameHeight() + 4.0f;
    dockSize.y -= barHeight;
    ImGui::SetNextWindowSize(dockSize);
    
    ImGui::SetNextWindowViewport(viewport->ID);

    ImGuiWindowFlags host_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | 
                                  ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | 
                                  ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
                                  ImGuiWindowFlags_NoBackground;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

    ImGui::Begin("MainDockHost", nullptr, host_flags);
    ImGui::PopStyleVar(3);

    // Lệnh này tạo ra vùng để bạn gộp Tab
    ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
    ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);
    
    ImGui::End(); // Kết thúc Host
    // --- KẾT THÚC DOCKSPACE ---
    
    /*
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
    ImGui::SetNextWindowViewport(viewport->ID);
    
    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoBringToFrontOnFocus;
    
    ImGui::Begin("DockSpaceWnd", nullptr, flags);
    
    ImGuiID dockspace_id = ImGui::GetID("DockSpace");
    ImGuiDockNodeFlags dock_flags = ImGuiDockNodeFlags_PassthruCentralNode;
    
    ImGui::DockSpace(dockspace_id, ImVec2(0, 0), dock_flags);
    
    ImGui::End();*/
#endif
    RenderDebuggerToolbar();
    for (auto win : windows) {
        if (!win) continue;
        win->Render();
    }

    //    ImGui::Begin("Testing");
    //    if (ImGui::Button("Crash"_lc)) {
    //        throw 0;
    //    }
    //    // --- 新增：手动反馈选项 ---
    // #ifdef ENABLE_SENTRY
    //    ImGui::SameLine(); // 放在 Crash 按钮旁边
    //    if (ImGui::Button("Send Feedback"_lc)) {
    //        // 重置之前的输入内容
    //        memset(sentry_user_comments, 0, sizeof(sentry_user_comments));
    //        show_sentry_feedback = true;
    //    }
    // #endif
    //    ImGui::End();
    //    // --- Sentry 反馈对话框逻辑 ---
    // #ifdef ENABLE_SENTRY
    //    if (show_sentry_feedback) {
    //        // 确保每一帧都调用 OpenPopup，直到它真正打开
    //        ImGui::OpenPopup("User Feedback");
    //    }
    //
    //    // 使用 Modal 窗口确保反馈过程不被打断
    //    if (ImGui::BeginPopupModal("User Feedback", &show_sentry_feedback, ImGuiWindowFlags_AlwaysAutoResize)) {
    //        ImGui::Text("Help us improve CasioEmuMsvc!");
    //        ImGui::Separator();
    //
    //        ImGui::Text("Email (Optional):");
    //        ImGui::InputText("##email", sentry_user_email, IM_ARRAYSIZE(sentry_user_email));
    //
    //        ImGui::Text("What happened?");
    //        ImGui::InputTextMultiline("##comments", sentry_user_comments, IM_ARRAYSIZE(sentry_user_comments),
    //            ImVec2(350, 120), ImGuiInputTextFlags_AllowTabInput);
    //
    //        if (ImGui::Button("Submit", ImVec2(120, 0))) {
    //            auto uuid = Binary::LoadOrInit("uuid.bin", util::Random::getRandomObject<sentry_uuid_t>());
    //            char buf[37]{};
    //            sentry_uuid_as_string(&uuid, buf);
    //            sentry_value_t feedback = sentry_value_new_feedback(sentry_user_comments, sentry_user_email, buf, 0);
    //            sentry_capture_feedback(feedback);
    //
    //            show_sentry_feedback = false;
    //            ImGui::CloseCurrentPopup();
    //        }
    //
    //        ImGui::SameLine();
    //        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
    //            show_sentry_feedback = false;
    //            ImGui::CloseCurrentPopup();
    //        }
    //        ImGui::EndPopup();
    //    }
    // #endif
    top_bar_size = ImGui::GetCursorPosY();
#if !defined(__ANDROID__) && !defined(IOS)
	RenderStatusBar();
#endif

	//	ImGui::Begin("Testing");
	//	if (ImGui::Button("Crash"_lc)) {
	//		throw 0;
	//	}
	//	// --- 新增：手动反馈选项 ---
	// #ifdef ENABLE_SENTRY
	//	ImGui::SameLine(); // 放在 Crash 按钮旁边
	//	if (ImGui::Button("Send Feedback"_lc)) {
	//		// 重置之前的输入内容
	//		memset(sentry_user_comments, 0, sizeof(sentry_user_comments));
	//		show_sentry_feedback = true;
	//	}
	// #endif
	//	ImGui::End();
	//	// --- Sentry 反馈对话框逻辑 ---
	// #ifdef ENABLE_SENTRY
	//	if (show_sentry_feedback) {
	//		// 确保每一帧都调用 OpenPopup，直到它真正打开
	//		ImGui::OpenPopup("User Feedback");
	//	}
	//
	//	// 使用 Modal 窗口确保反馈过程不被打断
	//	if (ImGui::BeginPopupModal("User Feedback", &show_sentry_feedback, ImGuiWindowFlags_AlwaysAutoResize)) {
	//		ImGui::Text("Help us improve CasioEmuMsvc!");
	//		ImGui::Separator();
	//
	//		ImGui::Text("Email (Optional):");
	//		ImGui::InputText("##email", sentry_user_email, IM_ARRAYSIZE(sentry_user_email));
	//
	//		ImGui::Text("What happened?");
	//		ImGui::InputTextMultiline("##comments", sentry_user_comments, IM_ARRAYSIZE(sentry_user_comments),
	//			ImVec2(350, 120), ImGuiInputTextFlags_AllowTabInput);
	//
	//		if (ImGui::Button("Submit", ImVec2(120, 0))) {
	//			auto uuid = Binary::LoadOrInit("uuid.bin", util::Random::getRandomObject<sentry_uuid_t>());
	//			char buf[37]{};
	//			sentry_uuid_as_string(&uuid, buf);
	//			sentry_value_t feedback = sentry_value_new_feedback(sentry_user_comments, sentry_user_email, buf, 0);
	//			sentry_capture_feedback(feedback);
	//
	//			show_sentry_feedback = false;
	//			ImGui::CloseCurrentPopup();
	//		}
	//
	//		ImGui::SameLine();
	//		if (ImGui::Button("Cancel", ImVec2(120, 0))) {
	//			show_sentry_feedback = false;
	//			ImGui::CloseCurrentPopup();
	//		}
	//		ImGui::EndPopup();
	//	}
	// #endif

    ImGui::Render();
    //SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
    
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
		winX,
		winY,
		winW, winH,
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
    // SetupDefaultTheme();

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
        std::cout << "[Warning] labels.txt doesn't exist. You can consider create one for better debugging experiences. Format: address(0x1234),func name(can be quoted)\n";

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
             // MakeAssemblerUI(),
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
    /*for (auto* w : windows) {
        if (!w) continue;
    
        char buf[256];
        snprintf(buf, sizeof(buf),
            "%s ptr=%p open=%d",
            w->name,
            (void*)w,
            w->open
        );
    
        DebugLog(buf);
    }*/
    
    return nullptr;
}

namespace UIHelpers {

	void JumpToMemory(uint32_t addr) {
		// Try Ram first
		for (auto* win : windows) {
			if (win->name && strcmp(win->name, "Ram") == 0) {
				if (win->GotoMemoryAddress(addr)) return;
			}
		}
		// Try PRam next
		for (auto* win : windows) {
			if (win->name && strcmp(win->name, "PRam") == 0) {
				if (win->GotoMemoryAddress(addr)) return;
			}
		}
		// Try any remaining
		for (auto* win : windows) {
			if (win->GotoMemoryAddress(addr)) return;
		}
	}

	void ClickableAddress(uint32_t addr, JumpTarget defaultTarget) {
		// Render the colored address text
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

		// Left-click: default action
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

		// Right-click: context menu with both options
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
