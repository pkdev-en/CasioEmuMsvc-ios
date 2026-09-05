#pragma once
#include "Ui.hpp"
#include "Emulator.hpp"
#include "imgui/imgui.h"
#include <vector>

class CalculatorWindow : public UIWindow {
private:
	SDL_Texture* calcTexture = nullptr;
	int texW = 0, texH = 0;
	std::vector<uint8_t> pixelBuffer;

public:
	CalculatorWindow() : UIWindow("Calculator") {}

	~CalculatorWindow() override {
		if (calcTexture) {
			SDL_DestroyTexture(calcTexture);
			calcTexture = nullptr;
		}
	}

	void Render() override {
#if !defined(__ANDROID__) && !defined(IOS)
		if (!m_emu || !m_emu->tx || !m_emu->calculator_as_tab.load()) return;
		UIWindow::Render();
#endif
	}

	void RenderCore() override {
		int w = 0, h = 0;
		SDL_QueryTexture(m_emu->tx, nullptr, nullptr, &w, &h);
		if (w == 0 || h == 0) return;

		if (!calcTexture || texW != w || texH != h) {
			if (calcTexture) SDL_DestroyTexture(calcTexture);
			calcTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, w, h);
			texW = w;
			texH = h;
		}

		size_t neededSize = (size_t)w * h * 4;
		if (pixelBuffer.size() != neededSize) {
			pixelBuffer.resize(neededSize);
		}

		if (SDL_SetRenderTarget(m_emu->renderer, m_emu->tx) == 0) {
			SDL_Rect rect{ 0, 0, w, h };
			if (SDL_RenderReadPixels(m_emu->renderer, &rect, SDL_PIXELFORMAT_RGBA32, pixelBuffer.data(), w * 4) == 0) {
				SDL_UpdateTexture(calcTexture, nullptr, pixelBuffer.data(), w * 4);
			}
			SDL_SetRenderTarget(m_emu->renderer, nullptr);
		}

		if (!calcTexture) return;

		ImVec2 avail = ImGui::GetContentRegionAvail();
		float aspect = (float)w / h;
		float window_aspect = avail.x / avail.y;

		ImVec2 display_size;
		if (window_aspect > aspect) {
			display_size.y = avail.y;
			display_size.x = avail.y * aspect;
		} else {
			display_size.x = avail.x;
			display_size.y = avail.x / aspect;
		}

		ImVec2 cursor = ImGui::GetCursorPos();
		cursor.x += (avail.x - display_size.x) * 0.5f;
		cursor.y += (avail.y - display_size.y) * 0.5f;
		ImGui::SetCursorPos(cursor);

		ImVec2 p0 = ImGui::GetCursorScreenPos();
		ImGui::Image((void*)calcTexture, display_size);

		m_emu->emu_rect.x = (int)p0.x;
		m_emu->emu_rect.y = (int)p0.y;
		m_emu->emu_rect.w = (int)display_size.x;
		m_emu->emu_rect.h = (int)display_size.y;

		if (ImGui::IsItemHovered()) {
			ImVec2 mouse = ImGui::GetMousePos();
			if (ImGui::IsMouseClicked(0)) {
				SDL_Event event{};
				event.type = SDL_MOUSEBUTTONDOWN;
				event.button.button = SDL_BUTTON_LEFT;
				event.button.x = (int)mouse.x;
				event.button.y = (int)mouse.y;
				m_emu->UIEvent(event);
			} else if (ImGui::IsMouseReleased(0)) {
				SDL_Event event{};
				event.type = SDL_MOUSEBUTTONUP;
				event.button.button = SDL_BUTTON_LEFT;
				event.button.x = (int)mouse.x;
				event.button.y = (int)mouse.y;
				m_emu->UIEvent(event);
			}
			if (ImGui::IsMouseDragging(0)) {
				SDL_Event event{};
				event.type = SDL_MOUSEMOTION;
				event.button.button = SDL_BUTTON_LEFT;
				event.motion.x = (int)mouse.x;
				event.motion.y = (int)mouse.y;
				m_emu->UIEvent(event);
			}
		}
	}
};
