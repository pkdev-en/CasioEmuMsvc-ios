#pragma once
#include <SDL.h>
#include "Ui.hpp"
#include "imgui/imgui.h"
#include "RendererBackend.h"

inline constexpr const char* SCREEN_MIRROR_WINDOW_DATA_KEY = "CasioEmu.ScreenMirror";

class ScreenMirror;
extern ScreenMirror* g_mirror;
class ScreenMirror : public UIWindow {
public:
	bool is_tab;
private:
	SDL_Window* mirrorWindow = nullptr;
	SDL_Renderer* mirrorRenderer = nullptr;
	SDL_Texture* mirrorTexture = nullptr;
	int captureWidth;
	int captureHeight;
	Uint32 windowId;
	SDL_Rect displayRect;
	int lastWindowWidth = 0;
	int lastWindowHeight = 0;
	bool watchingEvents = false;

	static int SDLCALL eventWatch(void* userdata, SDL_Event* event) {
		auto* self = static_cast<ScreenMirror*>(userdata);
		if (!self || !event || !self->open || event->type != SDL_WINDOWEVENT || event->window.windowID != self->windowId) {
			return 0;
		}

		if (event->window.event == SDL_WINDOWEVENT_CLOSE) {
			self->open = false;
		}
		else if (event->window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
			self->updateDisplayRect(event->window.data1, event->window.data2);
		}
		return 0;
	}

	void updateDisplayRect(int windowWidth, int windowHeight) {
		float aspectRatio = (float)captureWidth / captureHeight;
		float windowRatio = (float)windowWidth / windowHeight;

		if (windowRatio > aspectRatio) {
			displayRect.h = windowHeight;
			displayRect.w = (int)(windowHeight * aspectRatio);
			displayRect.x = (windowWidth - displayRect.w) / 2;
			displayRect.y = 0;
		}
		else {
			displayRect.w = windowWidth;
			displayRect.h = (int)(windowWidth / aspectRatio);
			displayRect.x = 0;
			displayRect.y = (windowHeight - displayRect.h) / 2;
		}
		lastWindowWidth = windowWidth;
		lastWindowHeight = windowHeight;
	}

public:
	ScreenMirror(int captureWidth, int captureHeight, bool is_tab)
		: UIWindow("Screen Mirror"), is_tab(is_tab), captureWidth(captureWidth), captureHeight(captureHeight), windowId(0), watchingEvents(false) {
	}

	~ScreenMirror() override {
		destroy();
	}

	bool create() {
		if (is_tab) {
			mirrorTexture = SDL_CreateTexture(renderer,
				SDL_PIXELFORMAT_RGBA32,
				SDL_TEXTUREACCESS_STREAMING,
				captureWidth, captureHeight);

			if (!mirrorTexture) {
				SDL_Log("Error creating mirror texture: %s", SDL_GetError());
				return false;
			}

			open = true;
			bring_to_front_requested = true;
			return true;
		} else {
			mirrorWindow = SDL_CreateWindow("Live Mirror",
				SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
				captureWidth, captureHeight,
				SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);

			if (!mirrorWindow) {
				SDL_Log("Error creating mirror window: %s", SDL_GetError());
				return false;
			}

			windowId = SDL_GetWindowID(mirrorWindow);
			SDL_SetWindowData(mirrorWindow, SCREEN_MIRROR_WINDOW_DATA_KEY, this);

			casioemu::SetPreferredRendererDriverHint();
			mirrorRenderer = SDL_CreateRenderer(mirrorWindow, -1,
				SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

			if (!mirrorRenderer) {
				SDL_Log("Error creating mirror renderer: %s", SDL_GetError());
				SDL_SetWindowData(mirrorWindow, SCREEN_MIRROR_WINDOW_DATA_KEY, nullptr);
				SDL_DestroyWindow(mirrorWindow);
				mirrorWindow = nullptr;
				return false;
			}

			mirrorTexture = SDL_CreateTexture(mirrorRenderer,
				SDL_PIXELFORMAT_RGBA32,
				SDL_TEXTUREACCESS_STREAMING,
				captureWidth, captureHeight);

			if (!mirrorTexture) {
				SDL_DestroyRenderer(mirrorRenderer);
				SDL_DestroyWindow(mirrorWindow);
				mirrorRenderer = nullptr;
				mirrorWindow = nullptr;
				return false;
			}

			updateDisplayRect(captureWidth, captureHeight);

			open = true;
			SDL_AddEventWatch(eventWatch, this);
			watchingEvents = true;
			return true;
		}
	}

	void destroy() {
		if (watchingEvents) {
			SDL_DelEventWatch(eventWatch, this);
			watchingEvents = false;
		}
		if (mirrorRenderer) {
			SDL_DestroyRenderer(mirrorRenderer);
			mirrorRenderer = nullptr;
		}
		if (mirrorWindow) {
			SDL_SetWindowData(mirrorWindow, SCREEN_MIRROR_WINDOW_DATA_KEY, nullptr);
			SDL_DestroyWindow(mirrorWindow);
			mirrorWindow = nullptr;
		}
		open = false;
		windowId = 0;
	}

	Uint32 getWindowID() const {
		return mirrorWindow ? SDL_GetWindowID(mirrorWindow) : 0;
	}

	bool handleEvent(const SDL_Event& event) {
		if (!open || is_tab)
			return false;

		switch (event.type) {
		case SDL_QUIT:
			open = false;
			break;
		case SDL_WINDOWEVENT:
			if (event.window.windowID == getWindowID()) {
				if (event.window.event == SDL_WINDOWEVENT_CLOSE) {
					open = false;
				}
				else if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
					updateDisplayRect(event.window.data1, event.window.data2);
				}
			}
			break;
		}
		return open;
	}

	void update(void* pixels, int pitch) {
		if (!open) return;

		if (is_tab) {
			SDL_UpdateTexture(mirrorTexture, nullptr, pixels, pitch);
		} else {
			int windowWidth = 0, windowHeight = 0;
			SDL_GetWindowSize(mirrorWindow, &windowWidth, &windowHeight);

			if (windowWidth != lastWindowWidth || windowHeight != lastWindowHeight) {
				updateDisplayRect(windowWidth, windowHeight);
			}

			SDL_UpdateTexture(mirrorTexture, nullptr, pixels, pitch);
			SDL_SetRenderDrawColor(mirrorRenderer, 0, 0, 0, 255);
			SDL_RenderClear(mirrorRenderer);
			SDL_RenderCopy(mirrorRenderer, mirrorTexture, nullptr, &displayRect);
			SDL_RenderPresent(mirrorRenderer);
		}
	}

	void RenderCore() override {
		if (!mirrorTexture || !is_tab) return;

		ImVec2 avail = ImGui::GetContentRegionAvail();
		if (avail.x < 10) avail.x = captureWidth;
		if (avail.y < 10) avail.y = captureHeight;

		float aspectRatio = (float)captureWidth / captureHeight;
		float windowRatio = avail.x / avail.y;

		ImVec2 displaySize;
		if (windowRatio > aspectRatio) {
			displaySize.y = avail.y;
			displaySize.x = avail.y * aspectRatio;
		} else {
			displaySize.x = avail.x;
			displaySize.y = avail.x / aspectRatio;
		}

		ImVec2 cursor = ImGui::GetCursorPos();
		cursor.x += (avail.x - displaySize.x) * 0.5f;
		cursor.y += (avail.y - displaySize.y) * 0.5f;

		ImVec2 p0 = ImGui::GetCursorScreenPos();
		ImVec2 rect_min = ImVec2(p0.x + cursor.x - ImGui::GetCursorPos().x - 10.0f, p0.y + cursor.y - ImGui::GetCursorPos().y - 10.0f);
		ImVec2 rect_max = ImVec2(rect_min.x + displaySize.x + 20.0f, rect_min.y + displaySize.y + 20.0f);

		ImDrawList* draw_list = ImGui::GetWindowDrawList();
		draw_list->AddRectFilled(rect_min, rect_max, IM_COL32(30, 30, 30, 255), 8.0f);
		draw_list->AddRect(rect_min, rect_max, IM_COL32(100, 100, 100, 255), 8.0f, 0, 2.0f);

		ImGui::SetCursorPos(cursor);
		ImGui::Image((void*)mirrorTexture, displaySize);
	}

	bool isAlive() const { return open; }
};
