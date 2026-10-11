/*

		Screen peripheral implement.
		Copyright (C) 2024 telecomadm1145/Xyzst/user202729/LBPHacker/hieuxyz

		This program is free software: you can redistribute it and/or modify
		it under the terms of the GNU General Public License as published by
		the Free Software Foundation, either version 3 of the License, or
		(at your option) any later version.

		This program is distributed in the hope that it will be useful,
		but WITHOUT ANY WARRANTY; without even the implied warranty of
		MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
		GNU General Public License for more details.

		You should have received a copy of the GNU General Public License
		along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/
#include "Screen.hpp"
#include "TiMultiViewScreen.hpp"
#include "TiLcdTarget.hpp"
#include "ScreenOutput.hpp"
#include "ScreenRenderSupport.hpp"
#include "SolarIIScreen.hpp"
#include "EpsScreen.hpp"
#include "LcdResponse.hpp"
#include "LcdPlatform.hpp"
#include "OrdinaryLcdHistory.hpp"
#include "OrdinaryLcdTarget.hpp"
#include "OrdinaryLcdFrame.hpp"
#include "ScreenScan.hpp"
#include "ScreenScanVisual.hpp"
#include "LcdTemporalReplay.hpp"
#include "Chipset/Chipset.hpp"
#include "Chipset/ePSCpu.h"
#include "Chipset/MMU.hpp"
#include "Chipset/MMURegion.hpp"
#include "Chipset/ePSCpu.h"
#include "Chipset/Eps6800Display.h"
#include "Emulator.hpp"
#include "Ext/Random.hpp"
#include "Gui/HwController.h"
#include "Logger.hpp"
#include "ML620Ports.h"
#include "ModelInfo.h"
#include "Models.h"
#include "PopUpDisplay.h"

ScreenMirror* g_mirror = nullptr;
#include "Ui.hpp"
#include <SDL_image.h>
#include <algorithm> // for std::min, std::max
#include <array>
#include <atomic>
#include <chrono>
#include <climits>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <exception>
#include <functional>
#include <mutex>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>
#ifndef _WIN32
#include <csignal>
#endif

#if !defined(__ANDROID__) && !defined(__EMSCRIPTEN__) && !defined(IOS)
#include "Theme.h"
#elif defined(__ANDROID__)
#include <android/api-level.h>
#include <fcntl.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>
#include <media/NdkMediaMuxer.h>
#include <unistd.h>
#elif defined(IOS)
#include <fcntl.h>
#include <unistd.h>
#endif

#ifdef _WIN32
#include <windows.h>
#include <wingdi.h>
// Undefine Windows min/max macros to avoid conflicts with std::min/std::max
#undef min
#undef max
#endif
extern bool low_perf_ext;

#if !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
#include "Theme.h"
#endif

inline constexpr uint8_t reverse_bits(uint8_t n) {
	uint8_t reversed = 0;
	for (int i = 0; i < 8; ++i) {
		reversed |= ((n >> i) & 1) << (7 - i);
	}
	return reversed;
}

// constexpr 生成查找表
inline constexpr std::array<uint8_t, 256> generate_lookup_table() {
	std::array<uint8_t, 256> table = {};
	for (int i = 0; i < 256; ++i) {
		table[i] = reverse_bits(static_cast<uint8_t>(i));
	}
	return table;
}

// 定义查找表
constexpr auto bit_lookup_table = generate_lookup_table();

inline void fillRandomData(unsigned char* buf, size_t size) {
	util::Random::fillRandomBytes(reinterpret_cast<std::uint8_t*>(buf), size);
}

#pragma warning(disable : 4244)

namespace casioemu {
	SDL_Texture* CreateSvgSpriteTexture(SDL_Renderer* renderer, const SpriteInfo& sprite, int width, int height, SDL_Rect* content_bounds) {
		if (!renderer || sprite.svg_shape.empty() || width <= 0 || height <= 0)
			return nullptr;
		if (content_bounds)
			*content_bounds = {0, 0, width, height};
		SDL_RWops* rw = SDL_RWFromConstMem(sprite.svg_shape.data(), static_cast<int>(sprite.svg_shape.size()));
		if (!rw) {
			SDL_Log("[Screen][Warn] SDL_RWFromConstMem failed for SVG sprite: %s", SDL_GetError());
			return nullptr;
		}
		SDL_Surface* surface = IMG_LoadSizedSVG_RW(rw, width, height);
		SDL_RWclose(rw);
		if (!surface) {
			SDL_Log("[Screen][Warn] IMG_LoadSizedSVG_RW failed for SVG sprite: %s", IMG_GetError());
			return nullptr;
		}
		SDL_Surface* converted = SDL_ConvertSurfaceFormat(surface, SDL_PIXELFORMAT_RGBA32, 0);
		SDL_FreeSurface(surface);
		if (!converted) {
			SDL_Log("[Screen][Warn] SDL_ConvertSurfaceFormat failed for SVG sprite: %s", SDL_GetError());
			return nullptr;
		}
		if (content_bounds) {
			int min_x = converted->w;
			int min_y = converted->h;
			int max_x = -1;
			int max_y = -1;
			if (!SDL_MUSTLOCK(converted) || SDL_LockSurface(converted) == 0) {
				for (int y = 0; y < converted->h; ++y) {
					const auto* row = reinterpret_cast<const Uint32*>(
						static_cast<const Uint8*>(converted->pixels) + y * converted->pitch);
					for (int x = 0; x < converted->w; ++x) {
						Uint8 red = 0, green = 0, blue = 0, alpha = 0;
						SDL_GetRGBA(row[x], converted->format, &red, &green, &blue, &alpha);
						if (alpha == 0)
							continue;
						min_x = std::min(min_x, x);
						min_y = std::min(min_y, y);
						max_x = std::max(max_x, x);
						max_y = std::max(max_y, y);
					}
				}
				if (SDL_MUSTLOCK(converted))
					SDL_UnlockSurface(converted);
			}
			if (max_x >= min_x && max_y >= min_y)
				*content_bounds = {min_x, min_y, max_x - min_x + 1, max_y - min_y + 1};
		}
		SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, converted);
		SDL_FreeSurface(converted);
		if (!texture) {
			SDL_Log("[Screen][Warn] SDL_CreateTextureFromSurface failed for SVG sprite: %s", SDL_GetError());
			return nullptr;
		}
		SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
		return texture;
	}

	class SvgSpriteTextureCache {
	public:
		SvgSpriteTextureCache() = default;
		SvgSpriteTextureCache(const SvgSpriteTextureCache&) = delete;
		SvgSpriteTextureCache& operator=(const SvgSpriteTextureCache&) = delete;
		SvgSpriteTextureCache(SvgSpriteTextureCache&& other) noexcept {
			MoveFrom(other);
		}
		SvgSpriteTextureCache& operator=(SvgSpriteTextureCache&& other) noexcept {
			if (this != &other) {
				Reset();
				MoveFrom(other);
			}
			return *this;
		}
		~SvgSpriteTextureCache() {
			Reset();
		}

		void Reset() {
			if (texture) {
				SDL_DestroyTexture(texture);
				texture = nullptr;
			}
			width = 0;
			height = 0;
			shape_size = 0;
			shape_hash = 0;
			content_bounds = {};
			software_renderer = false;
		}

		SDL_Texture* Get(SDL_Renderer* renderer, const SpriteInfo& sprite, int requested_width, int requested_height) {
			if (sprite.svg_shape.empty())
				return nullptr;
			const int target_width = std::max(1, requested_width);
			const int target_height = std::max(1, requested_height);
			const auto target_shape_size = sprite.svg_shape.size();
			const auto target_shape_hash = std::hash<std::string>{}(sprite.svg_shape);
			if (!texture || width != target_width || height != target_height ||
				shape_size != target_shape_size || shape_hash != target_shape_hash) {
				Reset();
				SDL_RendererInfo renderer_info{};
				software_renderer = SDL_GetRendererInfo(renderer, &renderer_info) == 0 &&
					renderer_info.name && std::string_view(renderer_info.name) == "software";
				texture = CreateSvgSpriteTexture(renderer, sprite, target_width, target_height,
					software_renderer ? &content_bounds : nullptr);
				software_renderer = texture && software_renderer;
				width = texture ? target_width : 0;
				height = texture ? target_height : 0;
				shape_size = texture ? target_shape_size : 0;
				shape_hash = texture ? target_shape_hash : 0;
			}
			return texture;
		}

		bool ContentClip(const SDL_Rect& dest, SDL_Rect& clip) const {
			if (!software_renderer || width <= 0 || height <= 0 || content_bounds.w <= 0 || content_bounds.h <= 0)
				return false;
			const int left = static_cast<int>(std::floor(static_cast<double>(content_bounds.x) * dest.w / width));
			const int top = static_cast<int>(std::floor(static_cast<double>(content_bounds.y) * dest.h / height));
			const int right = static_cast<int>(std::ceil(static_cast<double>(content_bounds.x + content_bounds.w) * dest.w / width));
			const int bottom = static_cast<int>(std::ceil(static_cast<double>(content_bounds.y + content_bounds.h) * dest.h / height));
			clip = {dest.x + left - 1, dest.y + top - 1, std::max(1, right - left + 2), std::max(1, bottom - top + 2)};
			return true;
		}

	private:
		void MoveFrom(SvgSpriteTextureCache& other) noexcept {
			texture = std::exchange(other.texture, nullptr);
			width = std::exchange(other.width, 0);
			height = std::exchange(other.height, 0);
			shape_size = std::exchange(other.shape_size, 0);
			shape_hash = std::exchange(other.shape_hash, 0);
			content_bounds = std::exchange(other.content_bounds, SDL_Rect{});
			software_renderer = std::exchange(other.software_renderer, false);
		}

		SDL_Texture* texture = nullptr;
		int width = 0;
		int height = 0;
		size_t shape_size = 0;
		size_t shape_hash = 0;
		SDL_Rect content_bounds{};
		bool software_renderer = false;
	};

	std::pair<int, int> CurrentRenderTargetSpriteSize(SDL_Renderer* renderer, const SDL_Rect& dest) {
		float scale_x = 1.0f;
		float scale_y = 1.0f;
		if (renderer)
			SDL_RenderGetScale(renderer, &scale_x, &scale_y);
		return {
			std::max(1, static_cast<int>(std::lround(std::abs(static_cast<double>(dest.w) * scale_x)))),
			std::max(1, static_cast<int>(std::lround(std::abs(static_cast<double>(dest.h) * scale_y))))};
	}

	void RenderModelSprite(SDL_Renderer* renderer, SDL_Texture* interface_texture, SvgSpriteTextureCache* svg_texture, const SpriteInfo& sprite, const ColourInfo& ink_colour, uint8_t alpha) {
		SDL_Rect dest = sprite.dest;
		SDL_Texture* texture = nullptr;
		if (svg_texture) {
			auto [target_width, target_height] = CurrentRenderTargetSpriteSize(renderer, dest);
			texture = svg_texture->Get(renderer, sprite, target_width, target_height);
		}
		if (texture) {
			SDL_Rect old_clip{};
			const SDL_bool old_clip_enabled = SDL_RenderIsClipEnabled(renderer);
			if (old_clip_enabled)
				SDL_RenderGetClipRect(renderer, &old_clip);
			SDL_Rect content_clip{};
			bool content_clip_enabled = svg_texture && svg_texture->ContentClip(dest, content_clip);
			if (content_clip_enabled && old_clip_enabled) {
				SDL_Rect intersection{};
				if (SDL_IntersectRect(&content_clip, &old_clip, &intersection))
					content_clip = intersection;
				else
					content_clip = {0, 0, 0, 0};
			}
			if (content_clip_enabled)
				SDL_RenderSetClipRect(renderer, &content_clip);
			SDL_SetTextureColorMod(texture, ink_colour.r, ink_colour.g, ink_colour.b);
			SDL_SetTextureAlphaMod(texture, alpha);
			SDL_RenderCopy(renderer, texture, nullptr, &dest);
			SDL_SetTextureAlphaMod(texture, 255);
			SDL_SetTextureColorMod(texture, 255, 255, 255);
			if (content_clip_enabled)
				SDL_RenderSetClipRect(renderer, old_clip_enabled ? &old_clip : nullptr);
			return;
		}
		if (!interface_texture)
			return;
		SDL_SetTextureAlphaMod(interface_texture, alpha);
		SDL_Rect src = sprite.src;
		SDL_RenderCopy(renderer, interface_texture, &src, &dest);
	}

#ifndef CASIOEMU_CORE_WEB
	SDL_Color ScreenPixelColour(const ColourInfo& ink_colour, float alpha_value) {
		SDL_Color colour{
			static_cast<Uint8>(ink_colour.r),
			static_cast<Uint8>(ink_colour.g),
			static_cast<Uint8>(ink_colour.b),
			Uint8(std::clamp(static_cast<int>(alpha_value), 0, 255))};
		if (alpha_value <= 0.0f) {
			colour.a = 0;
		}
		else if (alpha_value > 255.0f) {
			const int extra = static_cast<int>(alpha_value - 255.0f);
			colour.r = static_cast<uint8_t>(std::max(0, ink_colour.r - extra));
			colour.g = static_cast<uint8_t>(std::max(0, ink_colour.g - static_cast<int>(extra * 0.8f)));
			colour.b = static_cast<uint8_t>(std::max(0, ink_colour.b - static_cast<int>(extra * 0.1f)));
			colour.a = 255;
		}
		return colour;
	}
#endif

    class SolarIIScreen : public Peripheral, public IScreenFrameProvider {
        struct StatusBit {
            uint8_t offset;
            uint8_t bit;
        };

        static constexpr int FRAME_WIDTH = 64;
        static constexpr int FRAME_HEIGHT_WITH_STATUS_ROW = 9;
        static constexpr size_t DISPLAY_BASE_ADDR = 0xF800;
        static constexpr size_t DISPLAY_ADDR = 0xF801;
        static constexpr size_t DISPLAY_LEN = 0x17;
        static constexpr size_t DISPLAY_STORAGE_LEN = 0x18;
        static constexpr std::array<StatusBit, 109> STATUS_BITS = {{
            {0x11, 6}, {0x11, 2}, {0x12, 6}, {0x12, 2}, {0x13, 6}, {0x13, 2},
            {0x14, 6}, {0x14, 2}, {0x15, 6}, {0x15, 2}, {0x16, 4}, {0x16, 2},
            {0x16, 0}, {0x08, 1}, {0x01, 5}, {0x01, 6}, {0x01, 4}, {0x09, 5},
            {0x09, 6}, {0x09, 4}, {0x11, 5}, {0x11, 4}, {0x01, 1}, {0x01, 2},
            {0x01, 0}, {0x09, 1}, {0x09, 2}, {0x09, 0}, {0x11, 1}, {0x11, 0},
            {0x02, 5}, {0x02, 6}, {0x02, 4}, {0x0A, 5}, {0x0A, 6}, {0x0A, 4},
            {0x12, 5}, {0x12, 4}, {0x02, 1}, {0x02, 2}, {0x02, 0}, {0x0A, 1},
            {0x0A, 2}, {0x0A, 0}, {0x12, 1}, {0x12, 0}, {0x03, 5}, {0x03, 6},
            {0x03, 4}, {0x0B, 5}, {0x0B, 6}, {0x0B, 4}, {0x13, 5}, {0x13, 4},
            {0x03, 1}, {0x03, 2}, {0x03, 0}, {0x0B, 1}, {0x0B, 2}, {0x0B, 0},
            {0x13, 1}, {0x13, 0}, {0x04, 5}, {0x04, 6}, {0x04, 4}, {0x0C, 5},
            {0x0C, 6}, {0x0C, 4}, {0x14, 5}, {0x14, 4}, {0x04, 1}, {0x04, 2},
            {0x04, 0}, {0x0C, 1}, {0x0C, 2}, {0x0C, 0}, {0x14, 1}, {0x14, 0},
            {0x05, 5}, {0x05, 6}, {0x05, 4}, {0x0D, 5}, {0x0D, 6}, {0x0D, 4},
            {0x15, 5}, {0x15, 4}, {0x05, 1}, {0x05, 2}, {0x05, 0}, {0x0D, 1},
            {0x0D, 2}, {0x0D, 0}, {0x15, 1}, {0x15, 0}, {0x16, 6}, {0x06, 5},
            {0x06, 6}, {0x06, 4}, {0x0E, 5}, {0x0E, 6}, {0x0E, 4}, {0x16, 5},
            {0x06, 1}, {0x06, 2}, {0x06, 0}, {0x0E, 1}, {0x0E, 2}, {0x0E, 0},
            {0x16, 1},
        }};
        static constexpr std::array<const char*, STATUS_BITS.size()> STATUS_SPRITE_NAMES = {{
            "rsd_shift",
            "rsd_mode",
            "rsd_sto",
            "rsd_rcl",
            "rsd_hyp",
            "rsd_m",
            "rsd_k",
            "rsd_deg",
            "rsd_rad",
            "rsd_gra",
            "rsd_fix",
            "rsd_sci",
            "rsd_sd",
            "rsd_b_minus",
            "rsd_b_1_up",
            "rsd_b_1_up_left",
            "rsd_b_1_up_right",
            "rsd_b_1_mid",
            "rsd_b_1_down_left",
            "rsd_b_1_down_right",
            "rsd_b_1_down",
            "rsd_b_1_dot",
            "rsd_b_2_up",
            "rsd_b_2_up_left",
            "rsd_b_2_up_right",
            "rsd_b_2_mid",
            "rsd_b_2_down_left",
            "rsd_b_2_down_right",
            "rsd_b_2_down",
            "rsd_b_2_dot",
            "rsd_b_3_up",
            "rsd_b_3_up_left",
            "rsd_b_3_up_right",
            "rsd_b_3_mid",
            "rsd_b_3_down_left",
            "rsd_b_3_down_right",
            "rsd_b_3_down",
            "rsd_b_3_dot",
            "rsd_b_4_up",
            "rsd_b_4_up_left",
            "rsd_b_4_up_right",
            "rsd_b_4_mid",
            "rsd_b_4_down_left",
            "rsd_b_4_down_right",
            "rsd_b_4_down",
            "rsd_b_4_dot",
            "rsd_b_5_up",
            "rsd_b_5_up_left",
            "rsd_b_5_up_right",
            "rsd_b_5_mid",
            "rsd_b_5_down_left",
            "rsd_b_5_down_right",
            "rsd_b_5_down",
            "rsd_b_5_dot",
            "rsd_b_6_up",
            "rsd_b_6_up_left",
            "rsd_b_6_up_right",
            "rsd_b_6_mid",
            "rsd_b_6_down_left",
            "rsd_b_6_down_right",
            "rsd_b_6_down",
            "rsd_b_6_dot",
            "rsd_b_7_up",
            "rsd_b_7_up_left",
            "rsd_b_7_up_right",
            "rsd_b_7_mid",
            "rsd_b_7_down_left",
            "rsd_b_7_down_right",
            "rsd_b_7_down",
            "rsd_b_7_dot",
            "rsd_b_8_up",
            "rsd_b_8_up_left",
            "rsd_b_8_up_right",
            "rsd_b_8_mid",
            "rsd_b_8_down_left",
            "rsd_b_8_down_right",
            "rsd_b_8_down",
            "rsd_b_8_dot",
            "rsd_b_9_up",
            "rsd_b_9_up_left",
            "rsd_b_9_up_right",
            "rsd_b_9_mid",
            "rsd_b_9_down_left",
            "rsd_b_9_down_right",
            "rsd_b_9_down",
            "rsd_b_9_dot",
            "rsd_b_10_up",
            "rsd_b_10_up_left",
            "rsd_b_10_up_right",
            "rsd_b_10_mid",
            "rsd_b_10_down_left",
            "rsd_b_10_down_right",
            "rsd_b_10_down",
            "rsd_b_10_dot",
            "rsd_s_minus",
            "rsd_s_1_up",
            "rsd_s_1_up_left",
            "rsd_s_1_up_right",
            "rsd_s_1_mid",
            "rsd_s_1_down_left",
            "rsd_s_1_down_right",
            "rsd_s_1_down",
            "rsd_s_2_up",
            "rsd_s_2_up_left",
            "rsd_s_2_up_right",
            "rsd_s_2_mid",
            "rsd_s_2_down_left",
            "rsd_s_2_down_right",
            "rsd_s_2_down",
        }};

        std::array<uint8_t, STATUS_BITS.size()> status_alpha{};
        std::array<SpriteInfo, STATUS_BITS.size()> status_sprite_info{};
        std::array<bool, STATUS_BITS.size()> status_sprite_present{};
        std::array<SvgSpriteTextureCache, STATUS_BITS.size()> status_svg_textures{};
        std::array<uint8_t, DISPLAY_STORAGE_LEN> display_data{};
        MMURegion region_display_control{}, region_display{};
        MMURegion region_range{}, region_mode{}, region_contrast{}, region_brightness{}, region_refresh_rate{};
        SDL_Renderer* renderer{};
        SDL_Texture* interface_texture{};
        ColourInfo ink_colour{};
        uint8_t display_control = 0;
        uint8_t screen_range = 0, screen_mode = 0, screen_contrast = 0, screen_brightness = 0, screen_refresh_rate = 0;

        const uint8_t* DisplayData() const {
            return display_data.data();
        }

        uint8_t CalculateSolarIIStatusAlpha(bool enabled) const {
            if (!screen_residual_enabled) {
                return enabled ? 255 : 0;
            }

            const float contrast = static_cast<float>(std::clamp<int>(screen_contrast, 0, 0x1F));
            const float ink_alpha_on = 255.0f * std::clamp(0.75f + contrast / 36.0f, 0.0f, 1.0f);
            const float ink_alpha_off = 255.0f * std::clamp(0.01f + contrast * 0.0045f, 0.0f, 1.0f);
            const float alpha = enabled ? ink_alpha_on : ink_alpha_off * screen_residual_alpha_scale;
            return static_cast<uint8_t>(std::clamp(static_cast<int>(alpha + 0.5f), 0, 255));
        }

        bool IsSolarIIDisplayEnabled() const {
            const uint8_t mode = screen_mode & 0x07;
            return mode == 0x05 || mode == 0x06;
        }

    public:
        using Peripheral::Peripheral;
		~SolarIIScreen() override {
			for (auto& texture : status_svg_textures)
				texture.Reset();
		}

        void Initialise() override {
            renderer = emulator.GetRenderer();
            interface_texture = emulator.GetInterfaceTexture();
            ink_colour = emulator.ModelDefinition.ink_color;
            status_sprite_present.fill(false);
			for (auto& texture : status_svg_textures)
				texture.Reset();
            for (size_t i = 0; i < STATUS_SPRITE_NAMES.size(); ++i) {
                auto iter = emulator.ModelDefinition.sprites.find(STATUS_SPRITE_NAMES[i]);
                if (iter == emulator.ModelDefinition.sprites.end())
                    continue;
                status_sprite_info[i] = iter->second;
                status_sprite_present[i] = true;
            }

            region_display_control.Setup(0xF800, 1, "SolarIIScreen/Control", &display_control, MMURegion::DefaultRead<uint8_t>, MMURegion::DefaultWrite<uint8_t>, emulator);
            region_display.Setup(
                DISPLAY_ADDR, DISPLAY_LEN, "SolarIIScreen/Buffer", this,
                [](MMURegion* region, size_t offset) {
                    auto* screen = static_cast<SolarIIScreen*>(region->userdata);
                    return screen->display_data[offset - DISPLAY_BASE_ADDR];
                },
                [](MMURegion* region, size_t offset, uint8_t data) {
                    auto* screen = static_cast<SolarIIScreen*>(region->userdata);
                    screen->display_data[offset - DISPLAY_BASE_ADDR] = data;
                },
                emulator);
            region_range.Setup(0xF030, 1, "SolarIIScreen/Range", &screen_range, MMURegion::DefaultRead<uint8_t>, MMURegion::DefaultWrite<uint8_t>, emulator);
            region_mode.Setup(0xF031, 1, "SolarIIScreen/Mode", &screen_mode, MMURegion::DefaultRead<uint8_t>, MMURegion::DefaultWrite<uint8_t>, emulator);
            region_contrast.Setup(0xF032, 1, "SolarIIScreen/Contrast", &screen_contrast, MMURegion::DefaultRead<uint8_t, 0x1F>, MMURegion::DefaultWrite<uint8_t, 0x1F>, emulator);
            region_brightness.Setup(0xF033, 1, "SolarIIScreen/Brightness", &screen_brightness, MMURegion::DefaultRead<uint8_t, 0x07>, MMURegion::DefaultWrite<uint8_t, 0x07>, emulator);
            region_refresh_rate.Setup(0xF034, 1, "SolarIIScreen/RefreshRate", &screen_refresh_rate, MMURegion::DefaultRead<uint8_t>, MMURegion::DefaultWrite<uint8_t>, emulator);
        }

        void* QueryInterface(const char* name) override {
            if (strcmp(name, typeid(IScreenFrameProvider).name()) == 0) {
                return static_cast<IScreenFrameProvider*>(this);
            }
            return Peripheral::QueryInterface(name);
        }

        void SaveState(std::ostream& os) override {
            os.write(reinterpret_cast<const char*>(&display_control), 1);
            os.write(reinterpret_cast<const char*>(display_data.data()), display_data.size());
            os.write(reinterpret_cast<const char*>(&screen_range), 1);
            os.write(reinterpret_cast<const char*>(&screen_mode), 1);
            os.write(reinterpret_cast<const char*>(&screen_contrast), 1);
            os.write(reinterpret_cast<const char*>(&screen_brightness), 1);
            os.write(reinterpret_cast<const char*>(&screen_refresh_rate), 1);
            os.write(reinterpret_cast<const char*>(status_alpha.data()), status_alpha.size());
        }

        void LoadState(std::istream& is) override {
            is.read(reinterpret_cast<char*>(&display_control), 1);
            is.read(reinterpret_cast<char*>(display_data.data()), display_data.size());
            is.read(reinterpret_cast<char*>(&screen_range), 1);
            is.read(reinterpret_cast<char*>(&screen_mode), 1);
            is.read(reinterpret_cast<char*>(&screen_contrast), 1);
            is.read(reinterpret_cast<char*>(&screen_brightness), 1);
            is.read(reinterpret_cast<char*>(&screen_refresh_rate), 1);
            is.read(reinterpret_cast<char*>(status_alpha.data()), status_alpha.size());
        }

        void UpdateFrameAlpha() override {
            const uint8_t* data = DisplayData();
            if (!data) {
                status_alpha.fill(0);
                return;
            }
            constexpr float kResidualFadeRatio = 0.50f;
            const bool display_enabled = IsSolarIIDisplayEnabled();
            if (!display_enabled) {
                if (screen_residual_enabled) {
                    for (auto& alpha : status_alpha) {
                        alpha = static_cast<uint8_t>(std::clamp(static_cast<int>(static_cast<float>(alpha) * kResidualFadeRatio + 0.5f), 0, 255));
                    }
                }
                else {
                    status_alpha.fill(0);
                }
                return;
            }
            for (size_t i = 0; i < STATUS_BITS.size(); ++i) {
                const auto bit = STATUS_BITS[i];
                const bool enabled = bit.offset < DISPLAY_STORAGE_LEN && (data[bit.offset] & (1 << bit.bit));
                const uint8_t target = CalculateSolarIIStatusAlpha(enabled);
                if (screen_residual_enabled) {
                    const float alpha = static_cast<float>(status_alpha[i]) * kResidualFadeRatio + static_cast<float>(target) * (1.0f - kResidualFadeRatio);
                    status_alpha[i] = static_cast<uint8_t>(std::clamp(static_cast<int>(alpha + 0.5f), 0, 255));
                }
                else {
                    status_alpha[i] = target;
                }
            }
        }

        int GetFrameWidth() const override { return FRAME_WIDTH; }
        int GetFrameHeight() const override { return FRAME_HEIGHT_WITH_STATUS_ROW; }
        void WriteFrameRgba(uint8_t* out, int r, int g, int b) const override {
            (void)r;
            (void)g;
            (void)b;
            if (!out) return;
            std::fill(out, out + FRAME_WIDTH * FRAME_HEIGHT_WITH_STATUS_ROW * 4, 0);
        }
        int GetStatusAlphaCount() const override { return static_cast<int>(status_alpha.size()); }
        void WriteStatusAlpha(uint8_t* out, int max_len) const override {
            if (!out || max_len <= 0) return;
            const int count = std::min(max_len, GetStatusAlphaCount());
            std::copy(status_alpha.begin(), status_alpha.begin() + count, out);
        }
        void Frame() override {
            if (!renderer || !interface_texture)
                return;

            UpdateFrameAlpha();
            SDL_SetTextureColorMod(interface_texture, ink_colour.r, ink_colour.g, ink_colour.b);
            for (size_t i = 0; i < status_alpha.size(); ++i) {
                if (!status_sprite_present[i])
                    continue;
                RenderModelSprite(renderer, interface_texture, &status_svg_textures[i], status_sprite_info[i], ink_colour, status_alpha[i]);
            }
            SDL_SetTextureAlphaMod(interface_texture, 255);
            SDL_SetTextureColorMod(interface_texture, 255, 255, 255);
        }
    };
	struct SpriteBitmap {
		const char* name;
		uint8_t mask, offset;
	};
	inline int update_screen_scan_alpha(float* screen_scan_alpha, Uint64 t, int screen_refresh_rate) {
		int n = (static_cast<Uint64>((t * screen_refresh_rate) / 250)) % 64;

	using SpriteBitmap = ordinary_lcd::SpriteSpec;
	inline int update_screen_scan_alpha(float* alpha, std::array<float, 64>& curve,
		float& coefficient, bool& valid, Uint64 time, int rate, int threshold) {
		return screen_scan::UpdateScanAlpha(alpha, curve, coefficient, valid,
			time, rate, threshold, screen_flashing_brightness_coeff);
	}
	template <HardwareId hardware_id>
	class Screen : public Peripheral, public IScreenFrameProvider, public ITiSvDisplay {
		static constexpr bool kCaptureLcdHistory = ordinary_lcd_history::kEnabled &&
			(hardware_id == HW_CLASSWIZ || hardware_id == HW_CLASSWIZ_II ||
			 hardware_id == HW_ES_PLUS || hardware_id == HW_FX_5800P);
		using LcdHistory = std::conditional_t<kCaptureLcdHistory,
			ordinary_lcd_history::History, ordinary_lcd_history::DisabledHistory>;
		using LcdHistoryWorker = std::conditional_t<kCaptureLcdHistory,
			ordinary_lcd_history::WorkerState, ordinary_lcd_history::DisabledWorkerState>;

		static int const N_ROW,
			ROW_SIZE,
			OFFSET,
			ROW_SIZE_DISP,
			SPR_MAX;

		static size_t RowBufferSize() {
			return (static_cast<size_t>(N_ROW) + 1) * static_cast<size_t>(ROW_SIZE);
		}

		MMURegion region_buffer{}, region_buffer1{}, region_contrast{}, region_brightness{}, region_scan_report_op1{}, region_mode{}, region_range{}, region_select{}, region_offset{}, region_refresh_rate{}, region_scan_report{};
		uint8_t* screen_buffer{}, * screen_buffer1{}, screen_contrast{}, screen_brightness{}, screen_scan_report_op1{}, screen_mode{}, screen_range{}, screen_select{}, screen_offset{}, screen_refresh_rate{}, screen_scan_report{};

		MMURegion region_power{}, region_scan_report_en{};
		uint8_t screen_power{}, screen_scan_report_en{};

		MMURegion region_unk1{}, region_unk2{};

		uint8_t unk_f034{};

		// TI things

		MMURegion ti_port7_data{}, ti_port5_data{};
		int ti_contrast{}, ti_port_status{};
		bool ti_enabled = 0;
		bool ti_a0 = 0;
		int ti_col = 0;
		int ti_page = 0;

		int ti_port7{};
		uint32_t ti_sv_status{};

		float screen_scan_alpha[64]{};
		std::array<float, 64> screen_scan_curve{};
		float screen_scan_curve_coeff = 0.0f;
		bool screen_scan_curve_valid = false;
		std::array<double, 66 * 192> lcd_response_alpha{};
		bool lcd_response_active = false;
		std::chrono::steady_clock::time_point lcd_response_last_tick{};
		std::atomic_bool lcd_response_reset_requested{false};
		screen_scan::State scan_report_state;
		LcdHistory lcd_history;
		LcdHistoryWorker lcd_history_worker;
		bool scan_history_bound = false;
		std::unique_ptr<lcd_temporal::Baseline> temporal_baseline;
		std::unique_ptr<lcd_temporal::ReplaySession> temporal_replay;
		bool temporal_valid = false;
		std::chrono::steady_clock::time_point temporal_retry_after{};
		static constexpr std::chrono::milliseconds kTemporalInterval{2};
		static constexpr std::chrono::milliseconds kTemporalRecoveryInterval{50};
		float position = 0;
		SDL_Renderer* renderer{};
		SDL_Texture* interface_texture{};
#ifndef CASIOEMU_CORE_WEB
		PixelScreenTexture pixel_screen_texture;
#endif
		float screen_ink_alpha[66 * 192]{};
		std::array<float, 66 * 192> eps_screen_ink_alpha{};
		std::mutex eps_screen_alpha_mutex;
		EpsScreenTemporalState eps_temporal_state;
		bool eps_lcd_response_active = false;
		std::chrono::steady_clock::time_point eps_lcd_response_last_tick{};
		std::atomic_bool screen_thread_running{false};
		std::thread screen_thread;
		mutable std::mutex screen_state_mutex;
		mutable std::condition_variable screen_state_ready;
		mutable std::atomic_uint screen_state_waiters{0};
#if !defined(__EMSCRIPTEN__) && !defined(CASIOEMU_CORE_WEB)
		std::mutex output_resource_mutex;
		ScreenOutput* output = nullptr;
#endif
		static const SpriteBitmap sprite_bitmap[];
		std::vector<SpriteInfo> sprite_info;
		std::vector<SvgSpriteTextureCache> sprite_svg_textures;
		std::vector<uint8_t> sprite_available;
		ColourInfo ink_colour{};

		bool inited = 0;
		bool enabled_2 = 0;
		int status_ink_alpha_on = 255;
		int status_ink_alpha_off = 0;

		std::unique_lock<std::recursive_mutex> LockLcdMutation() const {
			if constexpr (kCaptureLcdHistory)
				return lcd_history.Lock();
			return {};
		}

		template <ordinary_lcd_history::Kind kind, uint8_t mask, uint8_t Screen::*member>
		void SetupLcdControl(MMURegion& region, size_t address, const char* name) {
			if constexpr (!kCaptureLcdHistory) {
				region.Setup(address, 1, name, &(this->*member),
					MMURegion::DefaultRead<uint8_t, mask>, MMURegion::DefaultWrite<uint8_t, mask>, emulator);
			}
			else {
				region.Setup(address, 1, name, this,
					[](MMURegion* region, size_t) {
						return static_cast<uint8_t>((static_cast<Screen*>(region->userdata)->*member) & mask);
					},
					[](MMURegion* region, size_t, uint8_t data) {
						auto* screen = static_cast<Screen*>(region->userdata);
						auto history_lock = screen->LockLcdMutation();
						struct WriteContext {
							Screen* screen;
							uint8_t data;
						} context{screen, static_cast<uint8_t>(data & mask)};
						const auto write = [](void* raw) noexcept {
							auto* context = static_cast<WriteContext*>(raw);
							context->screen->*member = context->data;
						};
						const auto prepare = [](void* raw, ordinary_lcd_history::Event& event) noexcept {
							auto* context = static_cast<WriteContext*>(raw);
							event.kind = kind;
							event.old_value = context->screen->*member;
							event.new_value = context->data;
							return event.Changes() ? ordinary_lcd_history::PrepareResult::Commit
								: ordinary_lcd_history::PrepareResult::NoChange;
						};
						if (screen->lcd_history.TryRecord(prepare, write, &context) !=
							ordinary_lcd_history::TransactionResult::Committed)
							write(&context);
					}, emulator);
			}
		}

		// Announce readers/lifecycle operations before waiting for the state lock.
		// The unthrottled LCD worker must let them through before its next tick.
		std::unique_lock<std::mutex> LockScreenState() const {
			screen_state_waiters.fetch_add(1);
			std::unique_lock<std::mutex> lock(screen_state_mutex, std::defer_lock);
			try {
				lock.lock();
			}
			catch (...) {
				screen_state_waiters.fetch_sub(1);
				screen_state_ready.notify_one();
				throw;
			}
			screen_state_waiters.fetch_sub(1);
			screen_state_ready.notify_one();
			return lock;
		}

		struct LcdResponseTick {
			bool enabled = false;
			double rise_gain = 0.0;
			double fall_gain = 0.0;
		};

		bool LcdResponseEligible() const {
			if constexpr (!(hardware_id == HW_FX_5800P || hardware_id == HW_ES_PLUS ||
				hardware_id == HW_CLASSWIZ || hardware_id == HW_CLASSWIZ_II ||
				hardware_id == HW_TI_MATH_PRINT)) {
				return false;
			}
#if defined(CASIOEMU_CORE_WEB) || defined(__EMSCRIPTEN__) || defined(__ANDROID__)
			return false;
#else
			return !ThemeManager::Instance().Settings().lowPerformanceMode && !low_perf_ext;
#endif
		}

		LcdResponseTick BeginLcdResponseTick() {
			if (!LcdResponseEligible()) {
				lcd_response_active = false;
				return {};
			}

			const auto now = std::chrono::steady_clock::now();
			double elapsed_ms = 0.0;
			if (lcd_response_reset_requested.exchange(false, std::memory_order_acq_rel) ||
				!lcd_response_active) {
				for (size_t i = 0; i < lcd_response_alpha.size(); ++i)
					lcd_response_alpha[i] = static_cast<double>(screen_ink_alpha[i]);
				lcd_response_active = true;
				lcd_response_last_tick = now;
			}
			else {
				elapsed_ms = std::chrono::duration<double, std::milli>(now - lcd_response_last_tick).count();
				lcd_response_last_tick = now;
				if (!(elapsed_ms > 0.0))
					elapsed_ms = 0.0;
			}

			const auto config = lcd_response::ForHardware(hardware_id);
			return {true,
				lcd_response::GainForElapsed(elapsed_ms, config.rise_half_life_ms),
				lcd_response::GainForElapsed(elapsed_ms, config.fall_half_life_ms)};
		}

		EpsLcdResponseTick BeginEpsLcdResponseTick() {
			if constexpr (!IsEpsFamily(hardware_id)) {
				return {};
			}
#if defined(CASIOEMU_CORE_WEB) || defined(__EMSCRIPTEN__) || defined(__ANDROID__)
			eps_lcd_response_active = false;
			return {};
#else
			if (ThemeManager::Instance().Settings().lowPerformanceMode || low_perf_ext) {
				eps_lcd_response_active = false;
				return {};
			}

			const auto now = std::chrono::steady_clock::now();
			double elapsed_ms = 0.0;
			if (lcd_response_reset_requested.exchange(false, std::memory_order_acq_rel) ||
				!eps_lcd_response_active) {
				eps_lcd_response_active = true;
			}
			else {
				elapsed_ms = std::chrono::duration<double, std::milli>(
					now - eps_lcd_response_last_tick).count();
				if (!(elapsed_ms > 0.0))
					elapsed_ms = 0.0;
			}
			eps_lcd_response_last_tick = now;

			const auto config = lcd_response::ForHardware(hardware_id);
			return {true,
				lcd_response::GainForElapsed(elapsed_ms, config.rise_half_life_ms),
				lcd_response::GainForElapsed(elapsed_ms, config.fall_half_life_ms)};
#endif
		}

		void ApplyLcdAlpha(
			size_t index,
			float target,
			float ratio,
			const LcdResponseTick& response) {
			if (!response.enabled) {
				screen_ink_alpha[index] = screen_ink_alpha[index] * ratio + target * (1 - ratio);
				return;
			}

			double& alpha = lcd_response_alpha[index];
			alpha = lcd_response::BlendWithGains(
				alpha, static_cast<double>(target), response.rise_gain, response.fall_gain);
			screen_ink_alpha[index] = static_cast<float>(alpha);
		}

		void ApplyLcdAlphaDecay(
			size_t first,
			size_t last,
			float ratio,
			const LcdResponseTick& response) {
			if (!response.enabled) {
				for (size_t i = first; i < last; ++i)
					screen_ink_alpha[i] *= ratio;
				return;
			}
			for (size_t i = first; i < last; ++i)
				ApplyLcdAlpha(i, 0.0f, ratio, response);
		}

		void SetupIndependentScanRegions() {
			if constexpr (screen_scan::kEnableIndependentScanReport &&
				(hardware_id == HW_CLASSWIZ || hardware_id == HW_CLASSWIZ_II)) {
				if (!region_scan_report_op1.setup_done)
					region_scan_report_op1.Setup(
						0xF035, 1, "Screen/ScanReportOption1", &scan_report_state,
						screen_scan::ReadOption1, screen_scan::WriteOption1, emulator);
				if (!region_scan_report_en.setup_done)
					region_scan_report_en.Setup(
						0xF036, 1, "Screen/ScanReportOptionEnable", &scan_report_state,
						screen_scan::ReadOptionEnable, screen_scan::WriteOptionEnable, emulator);
				if (!region_scan_report.setup_done)
					region_scan_report.Setup(
						0xF03B, 1, "Screen/ScanReport", &scan_report_state,
						screen_scan::ReadReport, MMURegion::IgnoreWrite, emulator);
				if (!region_refresh_rate.setup_done)
					region_refresh_rate.Setup(
						0xF034, 1, "Screen/RefreshRate", &scan_report_state,
						screen_scan::ReadRefreshRate, screen_scan::WriteRefreshRate, emulator);
			}
		}

		// Called with Screen held. CPU producers only take History -> Scan;
		// neither target evaluation nor replay holds their mutation lock.
		// -1: legacy fallback, 0: pending cutoff, 1: complete publication.
		int TemporalTick() {
			if constexpr (!kCaptureLcdHistory) {
				return -1;
			}
			else {
				const auto now = std::chrono::steady_clock::now();
				{
					auto settings_lock = ordinary_lcd_history::UntrackedChange::LockSettings();
					if (!LcdResponseEligible() || screen_buffer_select != 0) {
						if (temporal_valid) lcd_history.InvalidateEpoch();
						temporal_valid = false;
						lcd_history_worker.cutoff_pending = false;
						return -1;
					}
				}
				if (now < temporal_retry_after) return -1;
				const auto fail = [&]() {
					// Legacy tick does not consume history. Stop recording until
					// recovery establishes a new baseline and resets the queue.
					lcd_history.InvalidateEpoch();
					temporal_valid = false;
					lcd_history_worker.cutoff_pending = false;
					temporal_retry_after = std::chrono::steady_clock::now() + kTemporalRecoveryInterval;
					return -1;
				};
				if (lcd_history.Incomplete() || lcd_response_reset_requested.load(std::memory_order_acquire))
					temporal_valid = false;
				if (!temporal_replay) temporal_replay = std::make_unique<lcd_temporal::ReplaySession>();
				if (!temporal_valid) {
					if (!temporal_baseline) {
						temporal_baseline = std::make_unique<lcd_temporal::Baseline>();
						temporal_baseline->primary.resize(RowBufferSize());
						if constexpr (hardware_id == HW_CLASSWIZ_II)
							temporal_baseline->secondary.resize(RowBufferSize());
						temporal_baseline->sprites.assign(sprite_bitmap + 1, sprite_bitmap + SPR_MAX);
					}
					auto& b = *temporal_baseline;
					bool cold = !lcd_response_active || lcd_response_reset_requested.load(std::memory_order_acquire);
					for (size_t i = 0; i != b.alpha.size(); ++i)
						b.alpha[i] = cold ? static_cast<double>(screen_ink_alpha[i]) : lcd_response_alpha[i];
					b.status_levels = {status_ink_alpha_on, status_ink_alpha_off};
					std::copy_n(screen_scan_alpha, 64, b.scan_alpha.begin());
					{
						auto settings_lock = ordinary_lcd_history::UntrackedChange::LockSettings();
						auto history_lock = LockLcdMutation();
						if (!LcdResponseEligible() || screen_buffer_select != 0 || !screen_buffer) return fail();
						if (lcd_response_reset_requested.load(std::memory_order_acquire)) {
							cold = true;
							std::copy_n(screen_ink_alpha, b.alpha.size(), b.alpha.begin());
						}
						ordinary_lcd_history::ScanSnapshot scan;
						if constexpr (hardware_id == HW_CLASSWIZ || hardware_id == HW_CLASSWIZ_II) {
							scan_report_state.Advance(SDL_GetTicks64(), screen_scan::CurrentGate());
							scan = scan_report_state.CaptureSnapshot();
						}
						else {
							const auto gate = screen_scan::CurrentGate();
							// ES/5800 F034 is unrelated to the visual rate. Only
							// lifecycle/load can change this legacy visual field.
							screen_refresh_rate = std::max<uint8_t>(screen_refresh_rate, 6);
							scan = {screen_refresh_rate, screen_refresh_rate,
								screen_scan_report_op1, screen_scan_report_en,
								gate.flashing_threshold, gate.fading_enabled, gate.version, true, true};
						}
						b.scan = {scan.raw_rate, scan.effective_rate, scan.option1, scan.option_enable,
							scan.flashing_threshold, scan.fading_enabled, scan.gate_version,
							scan.active, scan.gate_initialized, 0};
						b.controls = {hardware_id, N_ROW, ROW_SIZE, ROW_SIZE_DISP,
							screen_mode, screen_select, screen_range, screen_offset, screen_brightness,
							screen_contrast, screen_power, enabled_2, false, screen_residual_enabled,
							screen_residual_alpha_scale, screen_flashing_brightness_coeff};
						std::copy_n(screen_buffer, b.primary.size(), b.primary.begin());
						if constexpr (hardware_id == HW_CLASSWIZ_II)
							std::copy_n(screen_buffer1, b.secondary.size(), b.secondary.begin());
						// All producers, including invalidating/fallback writes, are
						// excluded through the same recursive mutation lock.
						lcd_history.Reset();
						lcd_history_worker.cutoff = lcd_history.CaptureCutoff();
						b.coverage_complete = !lcd_history.Incomplete();
					}
					const auto& cutoff = lcd_history_worker.cutoff;
					b.epoch = cutoff.epoch; b.seq = cutoff.end_seq;
					b.steady_ns = cutoff.steady_ns; b.sdl_ms = cutoff.sdl_ms;
					b.scan.sdl_ms = b.sdl_ms;
					b.alpha_steady_ns.fill(b.steady_ns);
					const uint64_t previous_ns = cold ? b.steady_ns : static_cast<uint64_t>(
						std::chrono::duration_cast<std::chrono::nanoseconds>(lcd_response_last_tick.time_since_epoch()).count());
					if (!temporal_replay->BeginFromLive(b, cutoff, previous_ns) || !temporal_replay->Finish()) return fail();
					temporal_valid = true;
					lcd_history_worker.cutoff_pending = false;
				}
				else {
					if (!lcd_history_worker.cutoff_pending) {
						if constexpr (hardware_id == HW_CLASSWIZ || hardware_id == HW_CLASSWIZ_II)
							scan_report_state.Advance(SDL_GetTicks64(), screen_scan::CurrentGate());
						lcd_history_worker.cutoff = lcd_history.CaptureCutoff();
						if (!temporal_replay->Continue(lcd_history_worker.cutoff)) return fail();
						lcd_history_worker.cutoff_pending = true;
					}
					const auto consumed = lcd_history.ConsumeUntil(lcd_history_worker.cutoff, lcd_history_worker.batch);
					if (!temporal_replay->AppendBatch(lcd_history_worker.batch, consumed)) return fail();
					if (!consumed.cutoff_complete) return 0;
					lcd_history_worker.cutoff_pending = false;
				}
				const auto* result = temporal_replay->Result();
				if (!result) return fail();
				{
					auto settings_lock = ordinary_lcd_history::UntrackedChange::LockSettings();
					auto history_lock = LockLcdMutation();
					if (!lcd_history.Covers(lcd_history_worker.cutoff) ||
						!LcdResponseEligible() || screen_buffer_select != 0) return fail();
					lcd_response_reset_requested.store(false, std::memory_order_release);
				} // Publication commits here; subsequent mutations belong to the next observation.
				{
					// Publish only the complete private frame. Newer queued writes
					// belong to the next cutoff, not this observation time.
					lcd_response_alpha = result->alpha;
					for (size_t i = 0; i != result->alpha.size(); ++i)
						screen_ink_alpha[i] = static_cast<float>(result->alpha[i]);
					std::copy(result->scan_alpha.begin(), result->scan_alpha.end(), screen_scan_alpha);
					status_ink_alpha_on = result->status_levels.ink_alpha_on;
					status_ink_alpha_off = result->status_levels.ink_alpha_off;
					lcd_response_last_tick = std::chrono::steady_clock::time_point(
						std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::nanoseconds(result->steady_ns)));
					lcd_response_active = true;
				}
				return 1;
			}
		}
		void StartUpdateThread();
		void StopUpdateThread();

		int SpriteCount() const {
			if constexpr (IsEpsFamily(hardware_id))
				return static_cast<int>(emulator.ModelDefinition.status_indicators.size()) + 1;
			return SPR_MAX;
		}

		bool StatusEnabled() const {
			if (!(hardware_id == HW_CLASSWIZ || hardware_id == HW_CLASSWIZ_II) && hardware_id != HW_FX_5800P && hardware_id != HW_ES_PLUS && !IsEpsFamily(hardware_id)) {
				return true;
			}
			if (!enabled_2 || (screen_range & 0b100000)) {
				return false;
			}
			const auto mode = screen_mode & 7;
			return mode == 5 || mode == 6;
		}

		uint8_t LogicalAlpha(float value) const {
			return static_cast<uint8_t>(std::clamp(static_cast<int>(std::lround(value * 255.0f)), 0, 255));
		}

		static constexpr float kClassWizIILowerPlaneWeight = 1.0f / 3.0f;
		static constexpr float kClassWizIIUpperPlaneWeight = 2.0f / 3.0f;

		uint8_t ClassWizIIStatusAlpha(uint8_t offset, uint8_t mask) const {
			if (!StatusEnabled() || !screen_buffer || !screen_buffer1) return 0;
			const auto gate = screen_gate::Get();
			const auto status_offset = (offset + screen_offset * ROW_SIZE) % RowBufferSize();
			const bool lower = (screen_buffer[status_offset] & mask) != 0;
			const bool upper = (screen_buffer1[status_offset] & mask) != 0;
			if (!screen_residual_enabled) {
				return LogicalAlpha((lower ? kClassWizIILowerPlaneWeight : 0.0f) + (upper ? kClassWizIIUpperPlaneWeight : 0.0f));
			}
			float alpha = static_cast<float>(status_ink_alpha_off);
			alpha += (static_cast<float>(status_ink_alpha_on - status_ink_alpha_off)) * (lower ? kClassWizIILowerPlaneWeight : 0.0f);
			alpha += (static_cast<float>(status_ink_alpha_on - status_ink_alpha_off)) * (upper ? kClassWizIIUpperPlaneWeight : 0.0f);
			if (screen_refresh_rate >= gate.flashing_threshold) {
				alpha *= screen_scan_alpha[0];
			}
			return static_cast<uint8_t>(std::clamp(static_cast<int>(alpha), 0, 255));
		}

#ifndef CASIOEMU_CORE_WEB
		void ResetPixelScreenTexture() {
			if (pixel_screen_texture) {
				SDL_DestroyTexture(pixel_screen_texture);
				pixel_screen_texture = nullptr;
			}
			pixel_screen_texture_width = 0;
			pixel_screen_texture_height = 0;
			pixel_screen_pixels.clear();
		}

		bool EnsurePixelScreenTexture(int width, int height) {
			if (!renderer || width <= 0 || height <= 0)
				return false;
			if (pixel_screen_texture && pixel_screen_texture_width == width && pixel_screen_texture_height == height)
				return true;

			ResetPixelScreenTexture();
			pixel_screen_texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, width, height);
			if (!pixel_screen_texture) {
				SDL_Log("[Screen][Warn] SDL_CreateTexture failed for pixel screen: %s", SDL_GetError());
				return false;
			}
			SDL_SetTextureBlendMode(pixel_screen_texture, SDL_BLENDMODE_BLEND);
#if SDL_VERSION_ATLEAST(2, 0, 12)
			SDL_SetTextureScaleMode(pixel_screen_texture, SDL_ScaleModeNearest);
#endif
			pixel_screen_texture_width = width;
			pixel_screen_texture_height = height;
			pixel_screen_pixels.resize(static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
			return true;
		}

		void WritePixelScreenTexture(int logical_width, int logical_height) {
			if (pixel_screen_pixels.size() < static_cast<size_t>(logical_width) * static_cast<size_t>(logical_height) * 4)
				return;
			for (int y = 0; y != logical_height; ++y) {
				const int source_y = y + 1;
				for (int x = 0; x != logical_width; ++x) {
					const float alpha_value = screen_ink_alpha[x + source_y * 192];
					const SDL_Color colour = ScreenPixelColour(ink_colour, alpha_value);

					const size_t pixel_offset = (static_cast<size_t>(y) * static_cast<size_t>(logical_width) + static_cast<size_t>(x)) * 4;
					pixel_screen_pixels[pixel_offset + 0] = colour.r;
					pixel_screen_pixels[pixel_offset + 1] = colour.g;
					pixel_screen_pixels[pixel_offset + 2] = colour.b;
					pixel_screen_pixels[pixel_offset + 3] = colour.a;
				}
			}
		}

		bool RenderPixelScreenTexture(const SDL_Rect& lcd_dest, int logical_width, int logical_height) {
			if (!EnsurePixelScreenTexture(logical_width, logical_height))
				return false;
			WritePixelScreenTexture(logical_width, logical_height);
			if (SDL_UpdateTexture(pixel_screen_texture, nullptr, pixel_screen_pixels.data(), logical_width * 4) != 0) {
				SDL_Log("[Screen][Warn] SDL_UpdateTexture failed for pixel screen: %s", SDL_GetError());
				return false;
			}
			if (SDL_RenderCopy(renderer, pixel_screen_texture, nullptr, &lcd_dest) != 0) {
				SDL_Log("[Screen][Warn] SDL_RenderCopy failed for pixel screen: %s", SDL_GetError());
				return false;
			}
			return true;
		}
#endif

	public:
		Screen(Emulator& emu)
			: Peripheral(emu) {
		}
		~Screen() {
			StopUpdateThread();
#if !defined(__EMSCRIPTEN__) && !defined(CASIOEMU_CORE_WEB)
			{
				std::lock_guard<std::mutex> output_lock(output_resource_mutex);
				if (output) {
					output->Stop();
					delete output;
					output = nullptr;
				}
			}
#endif
			for (auto& texture : sprite_svg_textures)
				texture.Reset();
			if (screen_buffer)
				delete[] screen_buffer;
			if (screen_buffer1)
				delete[] screen_buffer1;
		}
		void Initialise() override;
		void Uninitialise() override;
		void Frame() override;
		void Reset() override;
		void* QueryInterface(const char* name) override {
			if constexpr (hardware_id == HW_TI_MATH_PRINT) {
				if (strcmp(name, typeid(ITiSvDisplay).name()) == 0)
					return static_cast<ITiSvDisplay*>(this);
			}
			if (strcmp(name, typeid(IScreenFrameProvider).name()) == 0) {
				return static_cast<IScreenFrameProvider*>(this);
			}
			return Peripheral::QueryInterface(name);
		}
		void SetTiSvStatus(uint32_t status) override {
			if constexpr (hardware_id == HW_TI_MATH_PRINT) {
				auto state_lock = LockScreenState();
				ti_sv_status = status;
				if (!emulator.ModelDefinition.real_hardware) {
					ti_enabled = true;
					ti_contrast = 112;
				}
			}
		}
		void SetTiSvFrame(const std::array<uint8_t, ITiSvDisplay::FrameBytes>& frame) override {
			if constexpr (hardware_id == HW_TI_MATH_PRINT) {
				auto state_lock = LockScreenState();
				if (screen_buffer)
					std::copy(frame.begin(), frame.end(), screen_buffer);
				ti_enabled = true;
				ti_contrast = 112;
			}
		}
		void UpdateFrameAlpha() override {
#ifdef __EMSCRIPTEN__
			auto state_lock = LockScreenState();
			tick();
			if constexpr (IsEpsFamily(hardware_id)) {
				std::lock_guard<std::mutex> lock(eps_screen_alpha_mutex);
				std::copy(eps_screen_ink_alpha.begin(), eps_screen_ink_alpha.end(), screen_ink_alpha);
			}
#endif
		}
		int GetFrameWidth() const override {
			if constexpr (IsEpsFamily(hardware_id)) {
				return GetEpsScreenSpec(
					hardware_id,
					emulator.ModelDefinition.screen_width,
					emulator.ModelDefinition.screen_height).export_width;
			}
			return hardware_id == HW_EPS6800 || hardware_id == HW_EPS9500 ? 96 : 192;
		}
		int GetFrameHeight() const override {
			if constexpr (IsEpsFamily(hardware_id)) {
				return GetEpsScreenSpec(
					hardware_id,
					emulator.ModelDefinition.screen_width,
					emulator.ModelDefinition.screen_height).export_height;
			}
			if constexpr (hardware_id == HW_TI_MATH_PRINT)
				return N_ROW + 1;
			return hardware_id == HW_FX_5800P || hardware_id == HW_ES_PLUS ||
				hardware_id == HW_EPS6800 ? 32 : 64;
		}
		void WriteFrameRgba(uint8_t* out, int r, int g, int b) const override {
			if (!out) return;
			std::array<float, 66 * 192> alpha_snapshot{};
			{
				auto state_lock = LockScreenState();
				std::copy(std::begin(screen_ink_alpha), std::end(screen_ink_alpha), alpha_snapshot.begin());
			}
			const int width = GetFrameWidth();
			const int height = GetFrameHeight();
			if constexpr (hardware_id == HW_EPS6009) {
				std::fill(out, out + static_cast<size_t>(width) * static_cast<size_t>(height) * 4, 0);
				return;
			}
			for (int y = 0; y < height; ++y) {
				for (int x = 0; x < width; ++x) {
					const float alpha = alpha_snapshot[y * 192 + x];
					const int idx = (y * width + x) * 4;
					out[idx + 0] = static_cast<uint8_t>(std::clamp(r, 0, 255));
					out[idx + 1] = static_cast<uint8_t>(std::clamp(g, 0, 255));
					out[idx + 2] = static_cast<uint8_t>(std::clamp(b, 0, 255));
					out[idx + 3] = static_cast<uint8_t>(std::clamp(static_cast<int>(alpha), 0, 255));
				}
			}
		}
		int GetStatusAlphaCount() const override {
			return std::max(0, SpriteCount() - 1);
		}
		void WriteStatusAlpha(uint8_t* out, int max_len) const override {
			if (!out || max_len <= 0) return;
			const int count = std::min(max_len, GetStatusAlphaCount());
			for (int i = 0; i < count; ++i) out[i] = 0;
			std::array<float, 66 * 192> alpha_snapshot{};
			{
				auto state_lock = LockScreenState();
				std::copy(std::begin(screen_ink_alpha), std::end(screen_ink_alpha), alpha_snapshot.begin());
			}
			for (int i = 0; i < count; ++i) {
				out[i] = static_cast<uint8_t>(std::clamp(static_cast<int>(alpha_snapshot[i]), 0, 255));
			}
		}
		void SaveState(std::ostream& os) override {
			if constexpr (hardware_id == HW_TI_MATH_PRINT) {
				auto state_lock = LockScreenState();
				os.write("SVL2", 4);
				os.write(reinterpret_cast<const char*>(screen_buffer), 192 * 9);
				const std::array<uint8_t, 11> state{
					static_cast<uint8_t>(ti_sv_status), static_cast<uint8_t>(ti_sv_status >> 8),
					static_cast<uint8_t>(ti_sv_status >> 16), static_cast<uint8_t>(ti_sv_status >> 24),
					static_cast<uint8_t>(ti_contrast), static_cast<uint8_t>(ti_enabled),
					static_cast<uint8_t>(ti_col), static_cast<uint8_t>(ti_page),
					static_cast<uint8_t>(ti_port_status), static_cast<uint8_t>(ti_a0),
					static_cast<uint8_t>(ti_port7)};
				os.write(reinterpret_cast<const char*>(state.data()), state.size());
				return;
			}
			size_t bufSize = RowBufferSize();
			if (screen_buffer)
				os.write(reinterpret_cast<const char*>(screen_buffer), bufSize);
			uint8_t hasBuf1 = (screen_buffer1 != nullptr) ? 1 : 0;
			os.write(reinterpret_cast<const char*>(&hasBuf1), 1);
			if (screen_buffer1)
				os.write(reinterpret_cast<const char*>(screen_buffer1), bufSize);
			os.write(reinterpret_cast<const char*>(&screen_contrast), 1);
			os.write(reinterpret_cast<const char*>(&screen_brightness), 1);
			os.write(reinterpret_cast<const char*>(&screen_mode), 1);
			os.write(reinterpret_cast<const char*>(&screen_range), 1);
			os.write(reinterpret_cast<const char*>(&screen_offset), 1);
			if constexpr (screen_scan::kEnableIndependentScanReport &&
				(hardware_id == HW_CLASSWIZ || hardware_id == HW_CLASSWIZ_II)) {
				const uint8_t refresh_rate = scan_report_state.GetRateForState();
				os.write(reinterpret_cast<const char*>(&refresh_rate), 1);
			}
			else {
				os.write(reinterpret_cast<const char*>(&screen_refresh_rate), 1);
			}
			os.write(reinterpret_cast<const char*>(&screen_power), 1);
		}
		void LoadState(std::istream& is) override {
			if constexpr (hardware_id == HW_TI_MATH_PRINT) {
				char magic[4]{};
				std::array<uint8_t, 192 * 9> buffer{};
				std::array<uint8_t, 11> state{};
				is.read(magic, sizeof(magic));
				is.read(reinterpret_cast<char*>(buffer.data()), buffer.size());
				is.read(reinterpret_cast<char*>(state.data()), state.size());
				if (!is || std::string(magic, 4) != "SVL2" || state[5] > 1 ||
					state[7] > 15 || state[8] > 1 || state[9] > 1)
					throw std::runtime_error("Invalid SmartView LCD state");
				auto state_lock = LockScreenState();
				std::copy(buffer.begin(), buffer.end(), screen_buffer);
				ti_sv_status = uint32_t(state[0]) | (uint32_t(state[1]) << 8) |
					(uint32_t(state[2]) << 16) | (uint32_t(state[3]) << 24);
				ti_contrast = state[4]; ti_enabled = state[5] != 0;
				ti_col = state[6]; ti_page = state[7]; ti_port_status = state[8];
				ti_a0 = state[9] != 0; ti_port7 = state[10];
				lcd_response_reset_requested.store(true, std::memory_order_release);
				return;
			}
			auto history_lock = LockLcdMutation();
			if constexpr (kCaptureLcdHistory)
				lcd_history.InvalidateEpoch();
			size_t bufSize = RowBufferSize();
			if (screen_buffer)
				is.read(reinterpret_cast<char*>(screen_buffer), bufSize);
			uint8_t hasBuf1 = 0;
			is.read(reinterpret_cast<char*>(&hasBuf1), 1);
			if (hasBuf1 && screen_buffer1)
				is.read(reinterpret_cast<char*>(screen_buffer1), bufSize);
			is.read(reinterpret_cast<char*>(&screen_contrast), 1);
			is.read(reinterpret_cast<char*>(&screen_brightness), 1);
			is.read(reinterpret_cast<char*>(&screen_mode), 1);
			is.read(reinterpret_cast<char*>(&screen_range), 1);
			is.read(reinterpret_cast<char*>(&screen_offset), 1);
			if constexpr (screen_scan::kEnableIndependentScanReport &&
				(hardware_id == HW_CLASSWIZ || hardware_id == HW_CLASSWIZ_II)) {
				uint8_t refresh_rate = scan_report_state.GetRateForState();
				if (is.read(reinterpret_cast<char*>(&refresh_rate), 1))
					scan_report_state.LoadRate(refresh_rate);
			}
			else {
				is.read(reinterpret_cast<char*>(&screen_refresh_rate), 1);
			}
			is.read(reinterpret_cast<char*>(&screen_power), 1);
			lcd_response_reset_requested.store(true, std::memory_order_release);
		}
		void tick() {
			float ratio = 0;
			if constexpr (hardware_id == HW_FX_5800P || hardware_id == HW_ES_PLUS)
				ratio = 1 - 1e-4;
			else
				ratio = 1 - 5e-4;
#ifdef __EMSCRIPTEN__
			ratio = 0.0f;
#elif defined(__ANDROID__) || defined(IOS)
			ratio = 0.80f;
#else
			if (ThemeManager::Instance().Settings().lowPerformanceMode || low_perf_ext) {
				ratio = 0.80f;
			}
#endif
			if constexpr (hardware_id == HW_TI) {
				ratio = 1 - 1e-4;
#ifdef __EMSCRIPTEN__
				ratio = 0.0f;
#elif defined(__ANDROID__) || defined(IOS)
				ratio = 0.80f;
#if !defined(CASIOEMU_CORE_WEB) && !defined(__EMSCRIPTEN__) && !defined(__ANDROID__)
			const bool low_performance = ThemeManager::Instance().Settings().lowPerformanceMode || low_perf_ext;
#else
			constexpr bool low_performance = false;
#endif
			ratio = lcd_platform::LegacyBlendRatio(ratio, low_performance);
			const auto lcd_response = BeginLcdResponseTick();
			if constexpr (hardware_id == HW_TI_MATH_PRINT) {
				ratio = lcd_platform::LegacyBlendRatio(0.0f, low_performance);
				if (!ti_enabled) {
					ApplyLcdAlphaDecay(0, 65 * 192, ratio, lcd_response);
					return;
				float ink_alpha_on = (ti_contrast - 100) * 20.0;
				float ink_alpha_off = std::clamp(ink_alpha_on * 0.1, 0.0, 255.0);
				ink_alpha_off = screen_residual_enabled ? ink_alpha_off * screen_residual_alpha_scale : 0.0f;
				ink_alpha_on = std::clamp(ink_alpha_on, 0.0f, 255.0f);
				if (!screen_residual_enabled) {
					ink_alpha_on = 255.0f;
				}
				uint8_t* screen_buffer = (uint8_t*)n_ram_buffer - casioemu::GetRamBaseAddr(hardware_id) + 0xE708;
				if (emulator.ModelDefinition.real_hardware) {
					screen_buffer = this->screen_buffer;
				}
				const auto levels = ti_lcd::CalculateTargetLevels(ti_contrast, screen_residual_enabled, screen_residual_alpha_scale);
				const float ink_alpha_on = levels.on;
				const float ink_alpha_off = levels.off;
				const uint8_t* screen_buffer = this->screen_buffer;
				for (int ix = 0; ix < 192; ++ix) {
					for (int iy = 0; iy < 64; ++iy) {
						uint32_t i = (ix << 6) | iy;
						int bIndx = (i >> 3);
						int subIndx = (i & 7);
						int mask = (1 << subIndx);
						bool on = (screen_buffer[bIndx] & mask) != 0;
						ApplyLcdAlpha((iy * 192 + 192) + ix,
							on ? ink_alpha_on : ink_alpha_off, ratio, lcd_response);
					}
				}
				for (int ix = 1; ix != SPR_MAX; ++ix) {
					const auto& indicator = sprite_bitmap[ix];
					const bool on = ((ti_sv_status >> (indicator.offset * 8)) & indicator.mask) != 0;
					ApplyLcdAlpha(ix - 1, on ? ink_alpha_on : ink_alpha_off, ratio, lcd_response);
				}

				return;
			}
			else if (hardware_id == HW_EPS6800) {
				ratio = 1 - 1e-4;
#ifdef __EMSCRIPTEN__
				ratio = 0.0f;
#elif defined(__ANDROID__) || defined(IOS)
				ratio = 0.80f;
#else
				if (ThemeManager::Instance().Settings().lowPerformanceMode || low_perf_ext) {
					// Match the deterministic ES Plus low-performance cadence: one
					// update every 10 ms, retaining 80% of the preceding LCD state.
					ratio = 0.80f;
				}
#endif
				std::array<uint8_t, EPS6800_LCD_RAW_SIZE> lcd{};
				Eps6800LcdControl control{};
				if (!emulator.chipset.epscpu ||
					emulator.chipset.epscpu->CopyLcd(lcd.data(), lcd.size(), &control) != lcd.size())
					return;
				float ink_alpha_on = Eps6800ActiveAlpha(control.contrast);
				float ink_alpha_off = Eps6800InactiveAlpha(control.contrast);
				ink_alpha_off = screen_residual_enabled ? ink_alpha_off * screen_residual_alpha_scale : 0.0f;
				if (!control.visible()) {
					ink_alpha_on = 0.0f;
					ink_alpha_off = 0.0f;
				}
				const float transition_ratio = screen_residual_enabled ? ratio : 0.0f;
				std::lock_guard<std::mutex> lock(eps_screen_alpha_mutex);

				// EPS stores four 8-pixel pages bottom-to-top. The physical top
				// row is a 96-bit segmented annunciator bus; it must not be drawn
				// as dot-matrix pixels. The remaining 31 rows form the 96x31 LCD.
				const auto decoded = DecodeEps6800Display(lcd.data(), lcd.size());
				for (int y = 0; y < static_cast<int>(EPS6800_LCD_PIXEL_HEIGHT); ++y) {
					for (int x = 0; x < static_cast<int>(EPS6800_LCD_WIDTH); ++x) {
						const bool on = decoded.pixels[y * EPS6800_LCD_WIDTH + x] != 0;
						auto& alpha = eps_screen_ink_alpha[(y + 1) * 192 + x];
						alpha = alpha * transition_ratio +
							(on ? ink_alpha_on : ink_alpha_off) * (1 - transition_ratio);
					}
				}

				const auto& status_indicators = emulator.ModelDefinition.status_indicators;
				for (size_t ix = 0; ix < status_indicators.size(); ++ix) {
					const auto& indicator = status_indicators[ix];
					const bool on = indicator.byte_offset < decoded.status.size() &&
						(decoded.status[indicator.byte_offset] & (1u << indicator.bit)) != 0;
					auto& alpha = eps_screen_ink_alpha[ix];
					alpha = alpha * transition_ratio +
						(on ? ink_alpha_on : ink_alpha_off) * (1 - transition_ratio);
				}
				return;
			}
			else if (hardware_id == HW_EPS6800_W192) {
				ratio = 0.80f;
				std::array<uint8_t, EPS6800_W192_LCD_RAW_SIZE> lcd{};
				Eps6800LcdControl control{};
				if (!emulator.chipset.epscpu ||
					emulator.chipset.epscpu->CopyLcd(lcd.data(), lcd.size(), &control) != lcd.size())
					return;
				float ink_alpha_on = Eps6800W192ActiveAlpha(control.contrast);
				float ink_alpha_off = Eps6800W192InactiveAlpha(control.contrast);
				ink_alpha_off = screen_residual_enabled ? ink_alpha_off * screen_residual_alpha_scale : 0.0f;
				if (!control.visible()) {
					ink_alpha_on = 0.0f;
					ink_alpha_off = 0.0f;
				}
				const float transition_ratio = screen_residual_enabled ? ratio : 0.0f;
				std::lock_guard<std::mutex> lock(eps_screen_alpha_mutex);
				const auto decoded = DecodeEps6800W192Display(lcd.data(), lcd.size());
				for (size_t y = 0; y < EPS6800_W192_LCD_HEIGHT; ++y) {
					for (size_t x = 0; x < EPS6800_W192_LCD_WIDTH; ++x) {
						const bool on = decoded.pixels[y * EPS6800_W192_LCD_WIDTH + x] != 0;
						auto& alpha = eps_screen_ink_alpha[(y + 1) * 192 + x];
						alpha = alpha * transition_ratio +
							(on ? ink_alpha_on : ink_alpha_off) * (1 - transition_ratio);
					}
				}
				const auto& status_indicators = emulator.ModelDefinition.status_indicators;
				for (size_t ix = 0; ix < status_indicators.size(); ++ix) {
					const auto& indicator = status_indicators[ix];
					const bool on = indicator.byte_offset < decoded.status.size() &&
						(decoded.status[indicator.byte_offset] & (1u << indicator.bit)) != 0;
					auto& alpha = eps_screen_ink_alpha[ix];
					alpha = alpha * transition_ratio +
						(on ? ink_alpha_on : ink_alpha_off) * (1 - transition_ratio);
				}
				return;
			}
			else if (hardware_id == HW_EPS9500) {
				ratio = 0.80f;
				std::array<uint8_t, EPS9500_LCD_RAW_SIZE> lcd{};
				Eps6800LcdControl control{};
				if (!emulator.chipset.epscpu ||
					emulator.chipset.epscpu->CopyLcd(lcd.data(), lcd.size(), &control) != lcd.size())
					return;
				float ink_alpha_on = Eps6800ActiveAlpha(control.contrast);
				float ink_alpha_off = Eps6800InactiveAlpha(control.contrast);
				ink_alpha_off = screen_residual_enabled ? ink_alpha_off * screen_residual_alpha_scale : 0.0f;
				if (!control.visible()) {
					ink_alpha_on = 0.0f;
					ink_alpha_off = 0.0f;
				}
				const float transition_ratio = screen_residual_enabled ? ratio : 0.0f;
				std::lock_guard<std::mutex> lock(eps_screen_alpha_mutex);
				const auto decoded = DecodeEps9500Display(lcd.data(), lcd.size());
				for (int y = 0; y < static_cast<int>(EPS9500_LCD_HEIGHT); ++y) {
					for (int x = 0; x < static_cast<int>(EPS9500_LCD_WIDTH); ++x) {
						const bool on = decoded.pixels[y * EPS9500_LCD_WIDTH + x] != 0;
						auto& alpha = eps_screen_ink_alpha[(y + 1) * 192 + x];
						alpha = alpha * transition_ratio +
							(on ? ink_alpha_on : ink_alpha_off) * (1 - transition_ratio);
					}
				}
				const auto& status_indicators = emulator.ModelDefinition.status_indicators;
				for (size_t ix = 0; ix < status_indicators.size(); ++ix) {
					const auto& indicator = status_indicators[ix];
					const bool on = indicator.byte_offset < decoded.status.size() &&
						(decoded.status[indicator.byte_offset] & (1u << indicator.bit)) != 0;
					auto& alpha = eps_screen_ink_alpha[ix];
					alpha = alpha * transition_ratio +
						(on ? ink_alpha_on : ink_alpha_off) * (1 - transition_ratio);
				}
				return;
			}
			else if (hardware_id == HW_EPS6009) {
				ratio = 0.80f;
				std::array<uint8_t, 0x88> lcd{};
				Eps6800LcdControl control{};
				if (!emulator.chipset.epscpu ||
					emulator.chipset.epscpu->CopyLcd(lcd.data(), lcd.size(), &control) != lcd.size())
					return;
				float ink_alpha_on = 230.0f;
				float ink_alpha_off = 8.0f;
				ink_alpha_off = screen_residual_enabled ? ink_alpha_off * screen_residual_alpha_scale : 0.0f;
				if (!control.visible()) {
					ink_alpha_on = 0.0f;
					ink_alpha_off = 0.0f;
				}
				const float transition_ratio = screen_residual_enabled ? ratio : 0.0f;
				std::lock_guard<std::mutex> lock(eps_screen_alpha_mutex);
				const auto& status_indicators = emulator.ModelDefinition.status_indicators;
				for (size_t ix = 0; ix < status_indicators.size(); ++ix) {
					const auto& indicator = status_indicators[ix];
					const bool on = indicator.byte_offset < lcd.size() &&
						(lcd[indicator.byte_offset] & (1u << indicator.bit)) != 0;
					auto& alpha = eps_screen_ink_alpha[ix];
					alpha = alpha * transition_ratio +
						(on ? ink_alpha_on : ink_alpha_off) * (1 - transition_ratio);
				}
			else if constexpr (IsEpsFamily(hardware_id)) {
				ratio = lcd_platform::LegacyBlendRatio(0.0f, low_performance);
				bool eps_residual_enabled;
				float eps_residual_alpha_scale;
				EpsLcdResponseTick eps_response;
				{
					auto settings_lock = ordinary_lcd_history::UntrackedChange::LockSettings();
					eps_residual_enabled = screen_residual_enabled;
					eps_residual_alpha_scale = screen_residual_alpha_scale;
					eps_response = BeginEpsLcdResponseTick();
				}
				EpsScreenContext eps_context{
					hardware_id,
					emulator.chipset.epscpu,
					emulator.ModelDefinition.status_indicators,
					eps_screen_ink_alpha,
					eps_screen_alpha_mutex,
					eps_temporal_state,
					eps_residual_enabled,
					eps_residual_alpha_scale,
					ratio,
					eps_response};
				UpdateEpsScreen(eps_context);
				return;
			}

			int report_refresh_rate = 0;
			int visual_refresh_rate = 0;
			Uint64 scan_now = 0;
			const auto gate = screen_gate::Get();
			screen_scan::Gate visual_gate = gate;
			if constexpr (screen_scan::kEnableIndependentScanReport &&
				(hardware_id == HW_CLASSWIZ || hardware_id == HW_CLASSWIZ_II)) {
				scan_now = SDL_GetTicks64();
				const auto scan_result = scan_report_state.Advance(scan_now, gate);
				// Advance returns the gate it actually accepted. This may be newer
				// than the snapshot captured before waiting for scan state, so all
				// ON-path visual decisions must use this returned snapshot.
				visual_gate = scan_result.gate;
				scan_now = scan_result.effective_now_ms;
				report_refresh_rate = scan_result.phase_rate;
				visual_refresh_rate = scan_result.effective_rate;
			}
			else {
				report_refresh_rate = screen_refresh_rate;
			}
			const int visual_flashing_threshold = visual_gate.flashing_threshold;
			const bool scan_frozen = [&]() {
				return report_refresh_rate < visual_gate.flashing_threshold && !visual_gate.fading_enabled;
			}();
			if (!scan_frozen) {
				int n = 0;
				if constexpr (screen_scan::kEnableIndependentScanReport &&
					(hardware_id == HW_CLASSWIZ || hardware_id == HW_CLASSWIZ_II)) {
					n = update_screen_scan_alpha(
						screen_scan_alpha,
						screen_scan_curve,
						screen_scan_curve_coeff,
						screen_scan_curve_valid,
						scan_now,
						report_refresh_rate,
						visual_gate.flashing_threshold);
				}
				else {
					n = update_screen_scan_alpha(
						screen_scan_alpha,
						screen_scan_curve,
						screen_scan_curve_coeff,
						screen_scan_curve_valid,
						SDL_GetTicks64(),
						screen_refresh_rate,
						visual_gate.flashing_threshold);
				}
				if constexpr (!(screen_scan::kEnableIndependentScanReport &&
					(hardware_id == HW_CLASSWIZ || hardware_id == HW_CLASSWIZ_II))) {
					screen_scan_report = screen_scan::CalculateScanReportFromPhase(n, screen_scan_report_op1, screen_scan_report_en);
				}
			}
			if constexpr (!(screen_scan::kEnableIndependentScanReport &&
				(hardware_id == HW_CLASSWIZ || hardware_id == HW_CLASSWIZ_II))) {
				if (screen_refresh_rate < 6)
					screen_refresh_rate = 6;
				visual_refresh_rate = screen_refresh_rate;
			}
			const auto target_levels = ordinary_lcd::CalculateTargetLevels<hardware_id == HW_CLASSWIZ_II>(
				screen_brightness,
				static_cast<int>(screen_contrast),
				screen_residual_enabled,
				screen_residual_alpha_scale);

			auto screen_buffer = this->screen_buffer;
			uint8_t* screen_buffer1 = nullptr;
			size_t row_size = ROW_SIZE;
			if constexpr (hardware_id == HW_CLASSWIZ_II) {
				screen_buffer1 = this->screen_buffer1;
			}
			if (screen_buffer_select != 0) {
				screen_buffer = (uint8_t*)n_ram_buffer - casioemu::GetRamBaseAddr(hardware_id) + casioemu::GetScreenBufferOffset(emulator.hardware_id, screen_buffer_select);
				if (hardware_id == HW_CLASSWIZ_II) {
					screen_buffer1 = screen_buffer + 0x600;
				}
				row_size = ROW_SIZE_DISP;
			}

			const ordinary_lcd::FrameControls frame_controls{
				N_ROW, static_cast<int>(row_size), ROW_SIZE_DISP,
				screen_mode, screen_range, screen_offset, enabled_2,
				hardware_id == HW_CLASSWIZ || hardware_id == HW_CLASSWIZ_II,
				target_levels};
			ordinary_lcd::VisitFrame<hardware_id == HW_CLASSWIZ_II>(frame_controls,
				std::span<const ordinary_lcd::SpriteSpec>(sprite_bitmap + 1, SPR_MAX - 1),
				[&](size_t index, const ordinary_lcd::PixelSource& source) {
					const float target = ordinary_lcd::EvaluatePixel<hardware_id == HW_CLASSWIZ_II>(
						source, screen_buffer, screen_buffer1, screen_scan_alpha,
						visual_refresh_rate >= visual_flashing_threshold);
					ApplyLcdAlpha(index, target, ratio, lcd_response);
				},
				[&](size_t begin, size_t end) {
					ApplyLcdAlphaDecay(begin, end, ratio, lcd_response);
				},
				[&](ordinary_lcd::TargetLevels levels) {
					status_ink_alpha_on = levels.ink_alpha_on;
					status_ink_alpha_off = levels.ink_alpha_off;
				});
		}
	};

	template <>
	const int Screen<HW_TI_MATH_PRINT>::N_ROW = 64;
	template <>
	const int Screen<HW_TI_MATH_PRINT>::ROW_SIZE = 32;
	template <>
	const int Screen<HW_TI_MATH_PRINT>::OFFSET = 32;
	template <>
	const int Screen<HW_TI_MATH_PRINT>::ROW_SIZE_DISP = 24;
	template <>
	const int Screen<HW_TI_MATH_PRINT>::SPR_MAX = 20;
	template <>
	const SpriteBitmap Screen<HW_TI_MATH_PRINT>::sprite_bitmap[] = {
		{"rsd_pixel", 0, 0},
		// TI SV SWI 4 status bytes, least significant byte first.
		{"rsd_l1", 0x01, 0x00},
		{"rsd_2nd", 0x02, 0x00},
		{"rsd_fix", 0x04, 0x00},
		{"rsd_h", 0x08, 0x00},
		{"rsd_b", 0x10, 0x00},
		{"rsd_o", 0x20, 0x00},
		{"rsd_l2", 0x40, 0x00},
		{"rsd_sci", 0x80, 0x00},
		{"rsd_eng", 0x01, 0x01},
		{"rsd_de", 0x02, 0x01},
		{"rsd_g", 0x04, 0x01},
		{"rsd_rad", 0x08, 0x01},
		{"rsd_l3", 0x10, 0x01},
		{"rsd_bat", 0x20, 0x01},
		{"rsd_wait", 0x40, 0x01},
		{"rsd_left", 0x80, 0x01},
		{"rsd_up", 0x01, 0x02},
		{"rsd_down", 0x02, 0x02},
		{"rsd_right", 0x04, 0x02},
	};

	template <>
	const int Screen<HW_CLASSWIZ_II>::N_ROW = 63;
	template <>
	const int Screen<HW_CLASSWIZ_II>::ROW_SIZE = 32;
	template <>
	const int Screen<HW_CLASSWIZ_II>::OFFSET = 32;
	template <>
	const int Screen<HW_CLASSWIZ_II>::ROW_SIZE_DISP = 24;
	template <>
	const int Screen<HW_CLASSWIZ_II>::SPR_MAX = 21;

	template <>
	const int Screen<HW_CLASSWIZ>::N_ROW = 63;
	template <>
	const int Screen<HW_CLASSWIZ>::ROW_SIZE = 32;
	template <>
	const int Screen<HW_CLASSWIZ>::OFFSET = 32;
	template <>
	const int Screen<HW_CLASSWIZ>::ROW_SIZE_DISP = 24;
	template <>
	const int Screen<HW_CLASSWIZ>::SPR_MAX = 21;

	template <>
	const int Screen<HW_ES_PLUS>::N_ROW = 31;
	template <>
	const int Screen<HW_ES_PLUS>::ROW_SIZE = 16;
	template <>
	const int Screen<HW_ES_PLUS>::OFFSET = 16;
	template <>
	const int Screen<HW_ES_PLUS>::ROW_SIZE_DISP = 12;
	template <>
	const int Screen<HW_ES_PLUS>::SPR_MAX = 19;

	template <>
	const int Screen<HW_FX_5800P>::N_ROW = 31;
	template <>
	const int Screen<HW_FX_5800P>::ROW_SIZE = 16;
	template <>
	const int Screen<HW_FX_5800P>::OFFSET = 16;
	template <>
	const int Screen<HW_FX_5800P>::ROW_SIZE_DISP = 12;
	template <>
	const int Screen<HW_FX_5800P>::SPR_MAX = 20;

	// that's meaningless, just make compiler happy xd
	template <>
	const int Screen<HW_FX_5800P>::N_ROW = 31;
	template <>
	const int Screen<HW_FX_5800P>::ROW_SIZE = 16;
	template <>
	const int Screen<HW_FX_5800P>::OFFSET = 16;
	template <>
	const int Screen<HW_FX_5800P>::ROW_SIZE_DISP = 12;
	template <>
	const int Screen<HW_FX_5800P>::SPR_MAX = 20;

	// that's meaningless, just make compiler happy xd
	template <>
	const int Screen<HW_EPS6800>::N_ROW = 31;
	template <>
	const int Screen<HW_EPS6800>::ROW_SIZE = 16;
	template <>
	const int Screen<HW_EPS6800>::OFFSET = 16;
	template <>
	const int Screen<HW_EPS6800>::ROW_SIZE_DISP = 12;
	template <>
	const int Screen<HW_EPS6800>::SPR_MAX = 1;

	template <>
	const int Screen<HW_EPS6800_W192>::N_ROW = 63;
	template <>
	const int Screen<HW_EPS6800_W192>::ROW_SIZE = 24;
	template <>
	const int Screen<HW_EPS6800_W192>::OFFSET = 0;
	template <>
	const int Screen<HW_EPS6800_W192>::ROW_SIZE_DISP = 24;
	template <>
	const int Screen<HW_EPS6800_W192>::SPR_MAX = 1;

	template <>
	const int Screen<HW_EPS6009>::N_ROW = 0;
	template <>
	const int Screen<HW_EPS6009>::ROW_SIZE = 1;
	template <>
	const int Screen<HW_EPS6009>::OFFSET = 0;
	template <>
	const int Screen<HW_EPS6009>::ROW_SIZE_DISP = 1;
	template <>
	const int Screen<HW_EPS6009>::SPR_MAX = 1;

	template <>
	const int Screen<HW_EPS9500>::N_ROW = 32;
	template <>
	const int Screen<HW_EPS9500>::ROW_SIZE = 16;
	template <>
	const int Screen<HW_EPS9500>::OFFSET = 16;
	template <>
	const int Screen<HW_EPS9500>::ROW_SIZE_DISP = 12;
	template <>
	const int Screen<HW_EPS9500>::SPR_MAX = 1;

	template <>
	const SpriteBitmap Screen<HW_CLASSWIZ_II>::sprite_bitmap[] = {
		{"rsd_pixel", 0, 0},
		{"rsd_s", 0x01, 0x01},
		{"rsd_math", 0x01, 0x03},
		{"rsd_d", 0x01, 0x04},
		{"rsd_r", 0x01, 0x05},
		{"rsd_g", 0x01, 0x06},
		{"rsd_fix", 0x01, 0x07},
		{"rsd_sci", 0x01, 0x08},
		{"rsd_fx", 0x01, 0x09},
		{"rsd_e", 0x01, 0x0A},
		{"rsd_cmplx", 0x01, 0x0B},
		{"rsd_angle", 0x01, 0x0C},
		{"rsd_wdown", 0x01, 0x0D},
		{"rsd_verify", 0x01, 0x0E},
		{"rsd_gx", 0x01, 0x0F},
		{"rsd_left", 0x01, 0x10},
		{"rsd_down", 0x01, 0x11},
		{"rsd_up", 0x01, 0x12},
		{"rsd_right", 0x01, 0x13},
		{"rsd_pause", 0x01, 0x15},
		{"rsd_sun", 0x01, 0x16} };

	template <>
	const SpriteBitmap Screen<HW_CLASSWIZ>::sprite_bitmap[] = {
		{"rsd_pixel", 0, 0},
		{"rsd_s", 0x01, 0x00},
		{"rsd_a", 0x01, 0x01},
		{"rsd_m", 0x01, 0x02},
		{"rsd_sto", 0x01, 0x03},
		{"rsd_math", 0x01, 0x05},
		{"rsd_d", 0x01, 0x06},
		{"rsd_r", 0x01, 0x07},
		{"rsd_g", 0x01, 0x08},
		{"rsd_fix", 0x01, 0x09},
		{"rsd_sci", 0x01, 0x0A},
		{"rsd_e", 0x01, 0x0B},
		{"rsd_cmplx", 0x01, 0x0C},
		{"rsd_angle", 0x01, 0x0D},
		{"rsd_wdown", 0x01, 0x0F},
		{"rsd_left", 0x01, 0x10},
		{"rsd_down", 0x01, 0x11},
		{"rsd_up", 0x01, 0x12},
		{"rsd_right", 0x01, 0x13},
		{"rsd_pause", 0x01, 0x15},
		{"rsd_sun", 0x01, 0x16} };

	template <>
	const SpriteBitmap Screen<HW_ES_PLUS>::sprite_bitmap[] = {
		{"rsd_pixel", 0, 0},
		{"rsd_s", 0x10, 0x00},
		{"rsd_a", 0x04, 0x00},
		{"rsd_m", 0x10, 0x01},
		{"rsd_sto", 0x02, 0x01},
		{"rsd_rcl", 0x40, 0x02},
		{"rsd_stat", 0x40, 0x03},
		{"rsd_cmplx", 0x80, 0x04},
		{"rsd_mat", 0x40, 0x05},
		{"rsd_vct", 0x02, 0x05},
		{"rsd_d", 0x20, 0x07},
		{"rsd_r", 0x02, 0x07},
		{"rsd_g", 0x10, 0x08},
		{"rsd_fix", 0x01, 0x08},
		{"rsd_sci", 0x20, 0x09},
		{"rsd_math", 0x40, 0x0A},
		{"rsd_down", 0x08, 0x0A},
		{"rsd_up", 0x80, 0x0B},
		{"rsd_disp", 0x10, 0x0B} };

	template <>
	const SpriteBitmap Screen<HW_FX_5800P>::sprite_bitmap[] = {
		{"rsd_pixel", 0, 0},
		{"rsd_s", 0x10, 0x00},
		{"rsd_a", 0x04, 0x00},
		{"rsd_m", 0x10, 0x01},
		{"rsd_sto", 0x02, 0x01},
		{"rsd_rcl", 0x40, 0x02},
		{"rsd_sd", 0x40, 0x03},
		{"rsd_reg", 0x80, 0x04},
		{"rsd_fmla", 0x40, 0x05},
		{"rsd_prgm", 0x10, 0x05},
		{"rsd_eng", 0x02, 0x05},
		{"rsd_d", 0x20, 0x07},
		{"rsd_r", 0x02, 0x07},
		{"rsd_g", 0x10, 0x08},
		{"rsd_fix", 0x01, 0x08},
		{"rsd_sci", 0x20, 0x09},
		{"rsd_math", 0x40, 0x0A},
		{"rsd_down", 0x08, 0x0A},
		{"rsd_up", 0x80, 0x0B},
		{"rsd_disp", 0x10, 0x0B} };

	template <>
	const SpriteBitmap Screen<HW_EPS6800>::sprite_bitmap[] = {
		{"rsd_pixel", 0, 0} };

	template <>
	const SpriteBitmap Screen<HW_EPS6800_W192>::sprite_bitmap[] = {
		{"rsd_pixel", 0, 0} };

	template <>
	const SpriteBitmap Screen<HW_EPS6009>::sprite_bitmap[] = {
		{"rsd_pixel", 0, 0} };

	template <>
	const SpriteBitmap Screen<HW_EPS9500>::sprite_bitmap[] = {
		{"rsd_pixel", 0, 0} };

	template <HardwareId hardware_id>
	void Screen<hardware_id>::StartUpdateThread() {
#if !defined(TEST_BUILD) && !defined(__EMSCRIPTEN__)
		if (screen_thread.joinable()) {
			if (screen_thread_running.load(std::memory_order_acquire))
				return;
			if (screen_thread.get_id() == std::this_thread::get_id())
				std::terminate();
			screen_thread.join();
		}
		screen_thread_running.store(true, std::memory_order_release);
		screen_thread = std::thread([this]() {
			while (screen_thread_running.load(std::memory_order_acquire)) {
				int temporal_status = -1;
				{
					std::unique_lock<std::mutex> state_lock(screen_state_mutex);
					screen_state_ready.wait(state_lock, [this]() {
						return screen_state_waiters.load() == 0 || !screen_thread_running.load();
					});
					if (!screen_thread_running.load())
						break;
					temporal_status = TemporalTick();
					if (temporal_status < 0) {
						// Settings writers use UntrackedChange. Keep the legacy
						// renderer on the same synchronization contract.
						auto settings_lock = ordinary_lcd_history::UntrackedChange::LockSettings();
						tick();
					}
				}
				if constexpr (kCaptureLcdHistory) {
					if (temporal_status >= 0) {
						if (temporal_status == 1) lcd_history.WaitForWork(kTemporalInterval, screen_thread_running);
						continue;
					}
				}
			if constexpr (IsEpsFamily(hardware_id) || hardware_id == HW_TI_MATH_PRINT) {
				SDL_Delay(10);
				}
#ifdef __ANDROID__
				else {
					SDL_Delay(10);
				}
#elif !defined(__EMSCRIPTEN__)
				else {
					bool low_performance;
					{
						auto settings_lock = ordinary_lcd_history::UntrackedChange::LockSettings();
						low_performance = ThemeManager::Instance().Settings().lowPerformanceMode || low_perf_ext;
					}
					if (low_performance)
						SDL_Delay(10);
				}
#endif
			}
		});
#endif
	}

	template <HardwareId hardware_id>
	void Screen<hardware_id>::StopUpdateThread() {
#if !defined(TEST_BUILD) && !defined(__EMSCRIPTEN__)
		{
			auto state_lock = LockScreenState();
			screen_thread_running.store(false, std::memory_order_release);
		}
		screen_state_ready.notify_one();
		if constexpr (kCaptureLcdHistory) lcd_history.Wake();
		if (screen_thread.joinable()) {
			// Screen destruction is owned by the Emulator/Chipset thread. The
			// update worker never destroys its owning Screen instance.
			if (screen_thread.get_id() == std::this_thread::get_id())
				std::terminate();
			screen_thread.join();
		}
		if constexpr (IsEpsFamily(hardware_id)) {
			if (emulator.chipset.epscpu)
				emulator.chipset.epscpu->SetLcdHistoryEnabled(false);
		}
#endif
	}

	template <HardwareId hardware_id>
	void Screen<hardware_id>::Initialise() {
		auto state_lock = LockScreenState();
		auto history_lock = LockLcdMutation();
		if (!inited) {
			if constexpr (kCaptureLcdHistory) lcd_history.InvalidateEpoch();
			renderer = emulator.GetRenderer();
			interface_texture = emulator.GetInterfaceTexture();
			const int sprite_count = SpriteCount();
			sprite_info.resize(sprite_count);
			for (auto& texture : sprite_svg_textures)
				texture.Reset();
			sprite_svg_textures.clear();
			sprite_svg_textures.resize(sprite_count);
			sprite_available.assign(sprite_count, 0);
			for (int ix = 0; ix != sprite_count; ++ix) {
				const char* static_name = nullptr;
				std::string dynamic_name;
				if constexpr (IsEpsFamily(hardware_id)) {
					dynamic_name = ix == 0 ? "rsd_pixel" : emulator.ModelDefinition.status_indicators[static_cast<size_t>(ix - 1)].sprite_name;
				}
				else {
					static_name = sprite_bitmap[ix].name;
				}
				auto sprite = emulator.ModelDefinition.sprites.find(static_name ? static_name : dynamic_name);
				if (sprite == emulator.ModelDefinition.sprites.end())
					continue;
				sprite_info[ix] = sprite->second;
				sprite_available[ix] = 1;
			}

			ink_colour = emulator.ModelDefinition.ink_color;
			if constexpr (hardware_id == HW_TI_MATH_PRINT) {
				screen_buffer = new uint8_t[192 * 9];
				// TODO: remove this
				memset(screen_buffer, 0, 192 * 9);
				// fillRandomData(screen_buffer, 192*9);
			}
			else {
				screen_buffer = new uint8_t[RowBufferSize()];
				fillRandomData(screen_buffer, RowBufferSize());
			}
			if constexpr (hardware_id == HW_CLASSWIZ || hardware_id == HW_CLASSWIZ_II) {
				region_power.Setup(
					0xF03D, 1, "Screen/Power", this,
					[](MMURegion* region, size_t offset) {
						return ((Screen*)region->userdata)->screen_power;
					},
					[](MMURegion* region, size_t offset, uint8_t data) {
						auto* screen = static_cast<Screen*>(region->userdata);
						bool a;
						{
							auto history_lock = screen->LockLcdMutation();
							a = (screen->screen_power & 1) ^ (data & 1);
							if constexpr (kCaptureLcdHistory) {
								if (screen->screen_power != (data & 0xf))
									screen->lcd_history.InvalidateEpoch();
							}
							screen->screen_power = data & 0xf;
						} // Release History before Initialise/Uninitialise takes Screen.
						if (a && ((data & 1) == 0)) { // 关闭屏幕
							((Screen*)region->userdata)->Uninitialise();
						}
						else {
							((Screen*)region->userdata)->Initialise();
						}
					},
					emulator);
			}
			if constexpr (hardware_id == HW_CLASSWIZ_II) {
				screen_buffer1 = new uint8_t[RowBufferSize()];
				fillRandomData(screen_buffer1, RowBufferSize());
			}
			inited = true;
		}
		if constexpr (IsEpsFamily(hardware_id)) {
			// CPU-visible LCD registers and RAM are owned by EPS6800Core. This
			// peripheral is only the CasioEmuMsvc presentation/resource layer.
			StartUpdateThread();
			return;
		}
		if constexpr (hardware_id == HW_TI_MATH_PRINT) {
			auto pp = emulator.chipset.QueryInterface<IPortProvider>();
			pp->SetPortOutputCallback(7, [&](uint8_t data) {
				auto state_lock = LockScreenState();
				ti_port7 = data;
				});
			pp->SetPortOutputCallback(5, [&](uint8_t data) {
				auto state_lock = LockScreenState();
				if (ti_a0 && !(data & 0x40)) {
					if ((data & 0x10)) {
						auto bit_off = ti_col;
						auto off = bit_off + ti_page * 192;
						if (ti_col >= 192 || ti_page >= 9) {
							ti_a0 = false;
							return;
						}
						screen_buffer[off] = ti_port7;
						ti_col++;
						if (ti_col >= 192) {
							ti_col = 0;
							ti_page++;
						}
					}
					else {
						auto data = ti_port7;
						switch (ti_port_status) {
						case 0: {
							auto dh = data >> 4;
							if (dh == 0) {
								ti_col = (ti_col & 0xf0) | (data & 0xf);
							}
							else if (dh == 1) {
								ti_col = (ti_col & 0xf) | ((data & 0xf) << 4);
							}
							else if ((dh & 0b1100) == 0b0100) {
								// std::cout << "Set Scroll line " << (data & 0x3f) << "\n";
							}
							else if (dh == 0b1011) {
								// std::cout << "Set page  " << (data & 0xf) << "\n";
								ti_page = (data & 0xf);
							}
							else if ((data >> 3) == 17) {
								// std::cout << "Set addressing mode\n";
							}
							else if ((data >> 2) == 58) {
								// std::cout << "Set bias\n";
							}
							else if ((data >> 2) == 40) {
								// std::cout << "Set frame rate\n";
							}
							else if ((data >> 1) == 82) {
								// std::cout << "Clear all display segments\n";
							}
							else if ((data >> 1) == 83) {
								// std::cout << "Set inverse display\n";
							}
							else if ((data & 0xf9) == 0xc0) {
								// std::cout << "Set Com Seg Scan Direction\n";
							}
							else if (data == 0xe3) {
								// std::cout << "Nop\n";
							}
							else if (data == 0xe2) {
								// std::cout << "Software reset\n";
							}
							else if (data == 0xaf) {
								// std::cout << "Enabled screen!\n";
								ti_enabled = 1;
							}
							else if (data == 0x81) {
								ti_port_status = 1;
							}
							else if (data == 0xae) {
								// std::cout << "Disabled screen!\n";
								ti_enabled = 0;
							}
							else {
								std::cout << "[Screen][Warn] Unknown ST7525 command: 0x" << std::hex << (int)data << "\n";
							}
							break;
						}
						case 1:
							// std::cout << "Set contrast!\n";
							ti_contrast = data;
							ti_port_status = 0;
							break;
						}
					}
				}
				ti_a0 = (data & 0x40);
				});
			StartUpdateThread();
			return;
		}
		if (!(hardware_id == HW_CLASSWIZ || hardware_id == HW_CLASSWIZ_II) || (!enabled_2 && (screen_power & 1))) {
			if constexpr (kCaptureLcdHistory) {
				// Repeated power writes while already enabled leave history intact.
				lcd_history.InvalidateEpoch();
				if (!scan_history_bound) {
					scan_report_state.SetHistory(&lcd_history);
					scan_history_bound = true;
				}
			}
			if constexpr (hardware_id != HW_CLASSWIZ_II) {
				region_buffer.Setup(
					0xF800, RowBufferSize(), "Screen/Buffer", this, [](MMURegion* region, size_t offset) {
						offset -= region->base;
						if (offset % ROW_SIZE >= ROW_SIZE_DISP)
							return (uint8_t)0;
						return ((Screen*)region->userdata)->screen_buffer[offset]; },
					[](MMURegion* region, size_t offset, uint8_t data) {
						offset -= region->base;
						if (offset % ROW_SIZE >= ROW_SIZE_DISP)
							return;

						auto this_obj = (Screen*)region->userdata;
						auto history_lock = this_obj->LockLcdMutation();
						if constexpr (kCaptureLcdHistory) {
							struct WriteContext {
								Screen* screen;
								size_t offset;
								uint8_t data;
							};
							const auto write = [](void* raw) noexcept {
								auto* context = static_cast<WriteContext*>(raw);
								context->screen->screen_buffer[context->offset] = context->data;
							};
							const auto prepare = [](void* raw, ordinary_lcd_history::Event& event) noexcept {
								auto* context = static_cast<WriteContext*>(raw);
								event.buffer = ordinary_lcd_history::BufferId::F800;
								event.offset = static_cast<uint32_t>(context->offset);
								event.old_primary = context->screen->screen_buffer[context->offset];
								event.new_primary = context->data;
								event.write_plane_mask = ordinary_lcd_history::kPrimaryPlane;
								return event.Changes()
									? ordinary_lcd_history::PrepareResult::Commit
									: ordinary_lcd_history::PrepareResult::NoChange;
							};
							WriteContext context{this_obj, offset, data};
							if (this_obj->lcd_history.TryRecord(prepare, write, &context) ==
								ordinary_lcd_history::TransactionResult::Committed)
								return;
						}
						this_obj->screen_buffer[offset] = data; },
					emulator);
			}
			else {
				region_buffer.Setup(
					0xF800, RowBufferSize(), "Screen/Buffer", this,
					[](MMURegion* region, size_t offset) {
						offset -= region->base;
						if (offset % ROW_SIZE >= ROW_SIZE_DISP)
							return (uint8_t)0;
						if (((Screen*)region->userdata)->screen_select & 0x04) {
							return ((Screen*)region->userdata)->screen_buffer1[offset];
						}
						else {
							return ((Screen*)region->userdata)->screen_buffer[offset];
						}
					},
					[](MMURegion* region, size_t offset, uint8_t data) {
						offset -= region->base;
						if (offset % ROW_SIZE >= ROW_SIZE_DISP)
							return;

						auto this_obj = (Screen*)region->userdata;
						auto history_lock = this_obj->LockLcdMutation();
						if constexpr (kCaptureLcdHistory) {
							struct WriteContext {
								Screen* screen;
								size_t offset;
								uint8_t data;
								uint8_t plane_mask;
							};
							const auto write = [](void* raw) noexcept {
								auto* context = static_cast<WriteContext*>(raw);
								if (context->plane_mask == (ordinary_lcd_history::kPrimaryPlane | ordinary_lcd_history::kSecondaryPlane)) {
									context->screen->screen_buffer1[context->offset] = context->screen->screen_buffer[context->offset] = context->data;
								}
								else if (context->plane_mask & ordinary_lcd_history::kSecondaryPlane) {
									context->screen->screen_buffer1[context->offset] = context->data;
								}
								else {
									context->screen->screen_buffer[context->offset] = context->data;
								}
							};
							const auto prepare = [](void* raw, ordinary_lcd_history::Event& event) noexcept {
								auto* context = static_cast<WriteContext*>(raw);
								context->plane_mask = !(context->screen->screen_mode & 0x40)
									? (ordinary_lcd_history::kPrimaryPlane | ordinary_lcd_history::kSecondaryPlane)
									: ((context->screen->screen_select & 0x04)
										? ordinary_lcd_history::kSecondaryPlane
										: ordinary_lcd_history::kPrimaryPlane);
								event.buffer = ordinary_lcd_history::BufferId::F800;
								event.offset = static_cast<uint32_t>(context->offset);
								event.write_plane_mask = context->plane_mask;
								if (context->plane_mask & ordinary_lcd_history::kPrimaryPlane)
									event.old_primary = context->screen->screen_buffer[context->offset];
								if (context->plane_mask & ordinary_lcd_history::kSecondaryPlane)
									event.old_secondary = context->screen->screen_buffer1[context->offset];
								event.new_primary = context->data;
								event.new_secondary = context->data;
								return event.Changes()
									? ordinary_lcd_history::PrepareResult::Commit
									: ordinary_lcd_history::PrepareResult::NoChange;
							};
							WriteContext context{this_obj, offset, data, 0};
							if (this_obj->lcd_history.TryRecord(prepare, write, &context) ==
								ordinary_lcd_history::TransactionResult::Committed)
								return;
						}
						if (!(this_obj->screen_mode & 0x40)) {
							this_obj->screen_buffer1[offset] = this_obj->screen_buffer[offset] = data;
							return;
						}
						if (this_obj->screen_select & 0x04)
							this_obj->screen_buffer1[offset] = data;
						else
							this_obj->screen_buffer[offset] = data;
					},
					emulator);
				if (!emulator.ModelDefinition.real_hardware) {
					// region_buffer.Setup(
					//	0xF800, (N_ROW + 1) * ROW_SIZE, "Screen/Buffer", this,
					//	[](MMURegion* region, size_t offset) {
					//		offset -= region->base;
					//		if (offset % ROW_SIZE >= ROW_SIZE_DISP)
					//			return (uint8_t)0;
					//		return ((Screen*)region->userdata)->screen_buffer[offset];
					//	},
					//	[](MMURegion* region, size_t offset, uint8_t data) {
					//		offset -= region->base;
					//		if (offset % ROW_SIZE >= ROW_SIZE_DISP)
					//			return;

					//                auto this_obj = (Screen*)region->userdata;
					//                this_obj->screen_buffer[offset] = data;
					//        },
					//        emulator);
					region_buffer1.Setup(
						0x89000, RowBufferSize(), "Screen/Buffer1", this,
						[](MMURegion* region, size_t offset) {
							offset -= region->base;
							if (offset % ROW_SIZE >= ROW_SIZE_DISP)
								return (uint8_t)0;
							return ((Screen*)region->userdata)->screen_buffer1[offset];
						},
						[](MMURegion* region, size_t offset, uint8_t data) {
							offset -= region->base;
							if (offset % ROW_SIZE >= ROW_SIZE_DISP)
								return;

							auto this_obj = (Screen*)region->userdata;
						auto history_lock = this_obj->LockLcdMutation();
							if constexpr (kCaptureLcdHistory) {
								struct WriteContext {
									Screen* screen;
									size_t offset;
									uint8_t data;
								};
								const auto write = [](void* raw) noexcept {
									auto* context = static_cast<WriteContext*>(raw);
									context->screen->screen_buffer1[context->offset] = context->data;
								};
								const auto prepare = [](void* raw, ordinary_lcd_history::Event& event) noexcept {
									auto* context = static_cast<WriteContext*>(raw);
									event.buffer = ordinary_lcd_history::BufferId::F800Secondary;
									event.offset = static_cast<uint32_t>(context->offset);
									event.old_secondary = context->screen->screen_buffer1[context->offset];
									event.new_secondary = context->data;
									event.write_plane_mask = ordinary_lcd_history::kSecondaryPlane;
									return event.Changes()
										? ordinary_lcd_history::PrepareResult::Commit
										: ordinary_lcd_history::PrepareResult::NoChange;
								};
								WriteContext context{this_obj, offset, data};
								if (this_obj->lcd_history.TryRecord(prepare, write, &context) ==
									ordinary_lcd_history::TransactionResult::Committed)
									return;
							}
							this_obj->screen_buffer1[offset] = data;
						},
						emulator);
				}
			}
			if constexpr (hardware_id == HW_CLASSWIZ || hardware_id == HW_CLASSWIZ_II) {
				SetupLcdControl<ordinary_lcd_history::Kind::Range, 0x2F, &Screen::screen_range>(
					region_range, 0xF030, "Screen/Range");
			}
			else {
				SetupLcdControl<ordinary_lcd_history::Kind::Range, 0x07, &Screen::screen_range>(
					region_range, 0xF030, "Screen/Range");
			}

			if constexpr (hardware_id == HW_CLASSWIZ_II) {
				region_mode.Setup(
					0xF031, 1, "Screen/Mode", this,
					[](MMURegion* region, size_t offset) {
						auto screen = ((Screen*)region->userdata);
						auto history_lock = screen->LockLcdMutation();
						return screen->screen_mode;
					},
						[](MMURegion* region, size_t offset, uint8_t data) {
							auto screen = ((Screen*)region->userdata);
						auto history_lock = screen->LockLcdMutation();
							if constexpr (kCaptureLcdHistory) {
								struct WriteContext {
									Screen* screen;
									uint8_t data;
								};
								const auto write = [](void* raw) noexcept {
									auto* context = static_cast<WriteContext*>(raw);
									context->screen->screen_mode = context->data & 127;
								};
								const auto prepare = [](void* raw, ordinary_lcd_history::Event& event) noexcept {
									auto* context = static_cast<WriteContext*>(raw);
									const uint8_t old_value = context->screen->screen_mode & 127;
									const uint8_t new_value = context->data & 127;
									if ((old_value ^ new_value) & 0b1000)
										return ordinary_lcd_history::PrepareResult::Reject;
									event.kind = ordinary_lcd_history::Kind::Mode;
									event.old_value = old_value;
									event.new_value = new_value;
									return old_value == new_value
										? ordinary_lcd_history::PrepareResult::NoChange
										: ordinary_lcd_history::PrepareResult::Commit;
								};
								WriteContext context{screen, data};
								const auto result = screen->lcd_history.TryRecord(prepare, write, &context);
								if (result == ordinary_lcd_history::TransactionResult::Committed)
									return;
							}
							auto old = screen->screen_mode & 0b1000;
						auto new_ = data & 0b1000;
						if (old ^ new_) {
							if constexpr (kCaptureLcdHistory)
								screen->lcd_history.InvalidateEpoch();
							auto sb = screen->screen_buffer;
							for (int iy = 0; iy != (N_ROW + 1); ++iy) {
								for (int ix = 0; ix != ROW_SIZE_DISP; ++ix) {
									sb[ix + iy * ROW_SIZE] = bit_lookup_table[sb[(ix)+iy * ROW_SIZE]];
								}
							}
							for (int iy = 0; iy != (N_ROW + 1); ++iy) {
								for (int ix = 0; ix != (ROW_SIZE_DISP / 2); ++ix) {
									std::swap(sb[ix + iy * ROW_SIZE], sb[(ROW_SIZE_DISP - 1 - ix) + iy * ROW_SIZE]);
								}
							}
							if constexpr (hardware_id == HW_CLASSWIZ_II) {
								sb = screen->screen_buffer1;
								for (int iy = 0; iy != (N_ROW + 1); ++iy) {
									for (int ix = 0; ix != ROW_SIZE_DISP; ++ix) {
										sb[ix + iy * ROW_SIZE] = bit_lookup_table[sb[(ix)+iy * ROW_SIZE]];
									}
								}
								for (int iy = 0; iy != (N_ROW + 1); ++iy) {
									for (int ix = 0; ix != (ROW_SIZE_DISP / 2); ++ix) {
										std::swap(sb[ix + iy * ROW_SIZE], sb[(ROW_SIZE_DISP - 1 - ix) + iy * ROW_SIZE]);
									}
								}
							}
						}
						screen->screen_mode = data & 127;
					},
					emulator);
			}
			else if constexpr (hardware_id == HW_CLASSWIZ) {
				if constexpr (kCaptureLcdHistory) {
					region_mode.Setup(
						0xF031, 1, "Screen/Mode", this,
						[](MMURegion* region, size_t) {
							return static_cast<uint8_t>(static_cast<Screen*>(region->userdata)->screen_mode & 63);
						},
						[](MMURegion* region, size_t, uint8_t data) {
							auto screen = static_cast<Screen*>(region->userdata);
							auto history_lock = screen->LockLcdMutation();
							struct WriteContext {
								Screen* screen;
								uint8_t data;
							};
							const auto write = [](void* raw) noexcept {
								auto* context = static_cast<WriteContext*>(raw);
								context->screen->screen_mode = context->data;
							};
							const auto prepare = [](void* raw, ordinary_lcd_history::Event& event) noexcept {
								auto* context = static_cast<WriteContext*>(raw);
								const uint8_t old_value = context->screen->screen_mode & 63;
								const uint8_t new_value = context->data & 63;
								event.kind = ordinary_lcd_history::Kind::Mode;
								event.old_value = old_value;
								event.new_value = new_value;
								return old_value == new_value
									? ordinary_lcd_history::PrepareResult::NoChange
									: ordinary_lcd_history::PrepareResult::Commit;
							};
							WriteContext context{screen, static_cast<uint8_t>(data & 63)};
							if (screen->lcd_history.TryRecord(prepare, write, &context) ==
								ordinary_lcd_history::TransactionResult::Committed)
								return;
							screen->screen_mode = data & 63;
						},
						emulator);
				}
				else {
					region_mode.Setup(0xF031, 1, "Screen/Mode", &screen_mode, MMURegion::DefaultRead<uint8_t, 63>,
						MMURegion::DefaultWrite<uint8_t, 63>, emulator);
				}
			}
			else {
				SetupLcdControl<ordinary_lcd_history::Kind::Mode, 0x07, &Screen::screen_mode>(
					region_mode, 0xF031, "Screen/Mode");
			}
			if constexpr (hardware_id == HW_CLASSWIZ || hardware_id == HW_CLASSWIZ_II) {
				SetupLcdControl<ordinary_lcd_history::Kind::Contrast, 0x3F, &Screen::screen_contrast>(
					region_contrast, 0xF032, "Screen/Contrast");
				region_unk1.Setup(
					0xF03E, 1, "Screen/Unk1", this,
					[](MMURegion* region, size_t offset) {
						return (uint8_t)0;
					},
					[](MMURegion* region, size_t offset, uint8_t data) {
						((Screen*)region->userdata)->emulator.chipset.mmu.WriteData(0xF817, data);
					},
					emulator);
				region_unk2.Setup(
					0xF03F, 1, "Screen/Unk2", this,
					[](MMURegion* region, size_t offset) {
						return (uint8_t)0;
					},
					[](MMURegion* region, size_t offset, uint8_t data) {
						((Screen*)region->userdata)->emulator.chipset.mmu.WriteData(0xF817, data);
					},
					emulator);
			}
			else {
				SetupLcdControl<ordinary_lcd_history::Kind::Contrast, 0x1f, &Screen::screen_contrast>(
					region_contrast, 0xF032, "Screen/Contrast");
			}

			if constexpr (hardware_id == HW_CLASSWIZ || hardware_id == HW_CLASSWIZ_II) {
				if constexpr (kCaptureLcdHistory) {
					region_select.Setup(
						0xF037, 1, "Screen/Select", this,
						[](MMURegion* region, size_t) {
							return static_cast<uint8_t>(static_cast<Screen*>(region->userdata)->screen_select & (0x04 | 1));
						},
						[](MMURegion* region, size_t, uint8_t data) {
							auto screen = static_cast<Screen*>(region->userdata);
							auto history_lock = screen->LockLcdMutation();
							struct WriteContext {
								Screen* screen;
								uint8_t data;
							};
							const auto write = [](void* raw) noexcept {
								auto* context = static_cast<WriteContext*>(raw);
								context->screen->screen_select = context->data & (0x04 | 1);
							};
							const auto prepare = [](void* raw, ordinary_lcd_history::Event& event) noexcept {
								auto* context = static_cast<WriteContext*>(raw);
								const uint8_t old_value = context->screen->screen_select & (0x04 | 1);
								const uint8_t new_value = context->data & (0x04 | 1);
								event.kind = ordinary_lcd_history::Kind::Select;
								event.old_value = old_value;
								event.new_value = new_value;
								return old_value == new_value
									? ordinary_lcd_history::PrepareResult::NoChange
									: ordinary_lcd_history::PrepareResult::Commit;
							};
							WriteContext context{screen, data};
							if (screen->lcd_history.TryRecord(prepare, write, &context) ==
								ordinary_lcd_history::TransactionResult::Committed)
								return;
							screen->screen_select = data & (0x04 | 1);
						},
						emulator);
				}
				else {
					region_select.Setup(0xF037, 1, "Screen/Select", &screen_select, MMURegion::DefaultRead < uint8_t, 0x04 | 1 >,
						MMURegion::DefaultWrite < uint8_t, 0x04 | 1 >, emulator);
				}

				SetupLcdControl<ordinary_lcd_history::Kind::Brightness, 0x07, &Screen::screen_brightness>(
					region_brightness, 0xF033, "Screen/Brightness");

				/*
cwx中F03B的值应该是由屏幕扫描和F035/F036决定的
1.每行扫描的时间大概是( [0xF034] * 25 ) us
2.F03B的mask是3，屏幕每扫描( [0xF036] == 0 ? 64 : [0xF035] )行后F03B的基础值会在0和3之间切换，如果F036是0的话这个循环的半周期和屏幕扫描应该是对齐的，也就是F03B的基础值切换后对应屏幕的第0行扫描（注：F035.0始终为1）
3.扫描屏幕的第0行 (对应bit0?) 及第32行 (对应bit1?) 时，F03B对应的bit会反转

n为行扫描计数，[0xF03B] = ( ( n / ( [0xF036] == 0 ? 64 : [0xF035] ) ) % 2 ? 3 : 0 ) ^ ( n % 64 == 0 ? 1 : ( n % 64 == 32 ? 2 : 0)  )
				*/

				if constexpr (screen_scan::kEnableIndependentScanReport) {
					SetupIndependentScanRegions();
				}
				else {
					region_scan_report_op1.Setup(0xF035, 1, "Screen/ScanReportOption1", &screen_scan_report_op1, MMURegion::DefaultRead<uint8_t, 0x1E>,
						MMURegion::DefaultWrite<uint8_t, 0x1E>, emulator);

					region_scan_report_en.Setup(0xF036, 1, "Screen/ScanReportOptionEnable", &screen_scan_report_en, MMURegion::DefaultRead<uint8_t, 0b1001>,
						MMURegion::DefaultWrite<uint8_t, 0b1001>, emulator);

					region_scan_report.Setup(0xF03B, 1, "Screen/ScanReport", &screen_scan_report, MMURegion::DefaultRead<uint8_t, 0x3>,
						MMURegion::IgnoreWrite, emulator);
				}
			}
			else {
				screen_scan_report_op1 = 0x17;
				screen_scan_report_en = 1;
			}

			if constexpr (screen_scan::kEnableIndependentScanReport &&
				(hardware_id == HW_CLASSWIZ || hardware_id == HW_CLASSWIZ_II)) {
				SetupLcdControl<ordinary_lcd_history::Kind::Offset, 0x3F, &Screen::screen_offset>(
					region_offset, 0xF039, "Screen/DSPOFST");
			}
			else if constexpr (hardware_id == HardwareId::HW_FX_5800P || hardware_id == HardwareId::HW_ES_PLUS) {
				region_refresh_rate.Setup(0xF034, 1, "Screen/Unknown_F034", &unk_f034, MMURegion::DefaultRead<uint8_t, 0b11>,
					MMURegion::DefaultWrite<uint8_t, 0b11>, emulator);
			}
			else {
				region_offset.Setup(0xF039, 1, "Screen/DSPOFST", &screen_offset, MMURegion::DefaultRead<uint8_t, 0x3F>,
					MMURegion::DefaultWrite<uint8_t, 0x3F>, emulator);

				// 25us
				region_refresh_rate.Setup(0xF034, 1, "Screen/RefreshRate", &screen_refresh_rate, MMURegion::DefaultRead<uint8_t, 0x7F>,
					MMURegion::DefaultWrite<uint8_t, 0x7F>, emulator);
			}
			enabled_2 = true;
			if constexpr (screen_scan::kEnableIndependentScanReport &&
				(hardware_id == HW_CLASSWIZ || hardware_id == HW_CLASSWIZ_II))
				scan_report_state.Activate();
		}
		StartUpdateThread();
	}

	template <HardwareId hardware_id>
	void Screen<hardware_id>::Uninitialise() {
		auto state_lock = LockScreenState();
		auto history_lock = LockLcdMutation();
		if (!enabled_2)
			return;
		if constexpr (kCaptureLcdHistory)
			lcd_history.InvalidateEpoch();
		fillRandomData(screen_buffer, RowBufferSize());
		if constexpr (hardware_id == HW_CLASSWIZ_II) {
			fillRandomData(screen_buffer1, RowBufferSize());
		}
		if constexpr (hardware_id != HW_CLASSWIZ_II) {
			region_buffer.Kill();
		}
		else {
			if (!emulator.ModelDefinition.real_hardware) {
				region_buffer.Kill();
				region_buffer1.Kill();
			}
			else {
				region_buffer.Kill();
			}
		}
		screen_range = 0;
		region_range.Kill();
		screen_mode = 0;
		region_mode.Kill();
		screen_contrast = 0;
		region_contrast.Kill();
		if constexpr (hardware_id == HW_CLASSWIZ || hardware_id == HW_CLASSWIZ_II) {
			screen_select = 0;
			region_select.Kill();
			if constexpr (screen_scan::kEnableIndependentScanReport) {
				scan_report_state.Deactivate();
				region_scan_report_op1.Kill();
				region_scan_report_en.Kill();
				region_scan_report.Kill();
			}
			else {
				screen_scan_report_op1 = 0;
				region_scan_report_op1.Kill();
				screen_scan_report_en = 0;
				region_scan_report_en.Kill();
				screen_scan_report = 0;
				region_scan_report.Kill();
			}
			region_unk1.Kill();
			region_unk2.Kill();
			screen_brightness = 0;
			region_brightness.Kill();
		}
		if constexpr (!screen_scan::kEnableIndependentScanReport ||
			!(hardware_id == HW_CLASSWIZ || hardware_id == HW_CLASSWIZ_II))
			screen_refresh_rate = 0;
		region_refresh_rate.Kill();
		if constexpr (hardware_id != HardwareId::HW_FX_5800P && hardware_id != HardwareId::HW_ES_PLUS) {
			screen_offset = 0;
			region_offset.Kill();
		}
		enabled_2 = false;
	}

#if !defined(__EMSCRIPTEN__) && !defined(CASIOEMU_CORE_WEB)
	bool GetPhysicalCaptureRect(SDL_Renderer* renderer, const SDL_Rect& logicalRect, SDL_Rect& physicalRect) {
		int renderW, renderH;
		if (SDL_GetRendererOutputSize(renderer, &renderW, &renderH) != 0) return false;
		int windowW, windowH;
		SDL_Window* win = SDL_RenderGetWindow(renderer);
		if (!win) return false;
		SDL_GetWindowSize(win, &windowW, &windowH);
		if (windowW == 0 || windowH == 0) return false;
		
		float scaleX = (float)renderW / windowW;
		float scaleY = (float)renderH / windowH;
		
		physicalRect.x = (int)(logicalRect.x * scaleX);
		physicalRect.y = (int)(logicalRect.y * scaleY);
		physicalRect.w = (int)(logicalRect.w * scaleX);
		physicalRect.h = (int)(logicalRect.h * scaleY);
		return true;
	}

	bool GetCaptureRect(const std::vector<SDL_Rect>& spriteRects, const std::vector<SDL_Rect>& pixelRects, SDL_Rect& captureRect) {
		if (spriteRects.empty() && pixelRects.empty()) {
			return false;
		}

		int minX = INT_MAX, minY = INT_MAX, maxX = INT_MIN, maxY = INT_MIN;
		auto extendBounds = [&](const SDL_Rect& rect) {
			minX = std::min(minX, rect.x);
			minY = std::min(minY, rect.y);
			maxX = std::max(maxX, rect.x + rect.w);
			maxY = std::max(maxY, rect.y + rect.h);
		};

		for (const auto& rect : spriteRects) {
			extendBounds(rect);
		}
		for (const auto& rect : pixelRects) {
			extendBounds(rect);
		}

		if (maxX <= minX || maxY <= minY) {
			return false;
		}

		captureRect = { minX, minY, maxX - minX, maxY - minY };
		return true;
	}

	std::string MakeTimestampedName(const char* prefix, const char* suffix) {
		std::time_t t = std::time(nullptr);
		std::tm tm = *std::localtime(&t);
		std::ostringstream filename;
		filename << prefix
			<< std::put_time(&tm, "%Y-%m-%d-%H-%M-%S-")
			<< util::Random::uniform_uint32(0, 999)
			<< suffix;
		return filename.str();
	}

	std::filesystem::path GetRecordingOutputPath(const std::string& name) {
#ifdef __ANDROID__
		const char* externalPath = SDL_AndroidGetExternalStoragePath();
		if (externalPath && *externalPath) {
			return std::filesystem::path(externalPath) / "recordings" / name;
		}
#endif
		return std::filesystem::path(name);
	}

	void SaveScreenshotSurface(SDL_Surface* screenSurface, const std::string& filename) {
		if (!screenSurface)
			return;
#ifdef __ANDROID__
		bool success = saveImageToMediaStore(screenSurface->pixels, screenSurface->w, screenSurface->h, screenSurface->pitch, filename.c_str());
		if (!success) {
			SDL_Log("Error saving screenshot using MediaStore API");
		}
		else {
			SDL_Log("Screenshot saved successfully with MediaStore API");
		}

		JNIEnv* env = (JNIEnv*)SDL_AndroidGetJNIEnv();
		jobject activity = (jobject)SDL_AndroidGetActivity();

		if (env && activity) {
			jobject byteBuffer = env->NewDirectByteBuffer(screenSurface->pixels,
				screenSurface->h * screenSurface->pitch);

			jclass activityClass = env->GetObjectClass(activity);
			jmethodID copyToClipboardMethod = env->GetMethodID(activityClass, "copyImageToClipboard",
				"(Ljava/nio/ByteBuffer;III)Z");

			if (copyToClipboardMethod != NULL) {
				jboolean result = env->CallBooleanMethod(activity, copyToClipboardMethod,
					byteBuffer, screenSurface->w,
					screenSurface->h, screenSurface->pitch);
				if (result) {
					SDL_Log("Screenshot copied to clipboard");
				}
				else {
					SDL_Log("Failed to copy screenshot to clipboard");
				}
			}
			else {
				SDL_Log("copyImageToClipboard method not found. Add it to your Java activity.");
			}

			env->DeleteLocalRef(byteBuffer);
			env->DeleteLocalRef(activityClass);
			env->DeleteLocalRef(activity);
		}
#else
		if (IMG_SavePNG(screenSurface, filename.c_str()) != 0) {
			SDL_Log("Error saving screenshot: %s", IMG_GetError());
		}
		else {
			SDL_Log("Screenshot saved to %s", filename.c_str());
		}

#ifdef _WIN32
		HDC hdcScreen = GetDC(NULL);
		HDC hdcMem = CreateCompatibleDC(hdcScreen);

		BITMAPINFOHEADER bi;
		ZeroMemory(&bi, sizeof(BITMAPINFOHEADER));
		bi.biSize = sizeof(BITMAPINFOHEADER);
		bi.biWidth = screenSurface->w;
		bi.biHeight = -screenSurface->h;
		bi.biPlanes = 1;
		bi.biBitCount = 32;
		bi.biCompression = BI_RGB;

		void* bits = NULL;
		HBITMAP hBitmap = CreateDIBSection(hdcMem, (BITMAPINFO*)&bi, DIB_RGB_COLORS, &bits, NULL, 0);

		if (hBitmap) {
			HGDIOBJ oldBitmap = SelectObject(hdcMem, hBitmap);

			uint8_t* dst = (uint8_t*)bits;

			for (int y = 0; y < screenSurface->h; y++) {
				uint8_t* src = (uint8_t*)screenSurface->pixels + y * screenSurface->pitch;
				for (int x = 0; x < screenSurface->w; x++) {
					dst[0] = src[2];
					dst[1] = src[1];
					dst[2] = src[0];
					dst[3] = src[3];

					src += 4;
					dst += 4;
				}
			}
			if (oldBitmap)
				SelectObject(hdcMem, oldBitmap);

			bool clipboardOwnsBitmap = false;
			if (OpenClipboard(NULL)) {
				EmptyClipboard();
				if (SetClipboardData(CF_BITMAP, hBitmap)) {
					clipboardOwnsBitmap = true;
				}
				else {
					SDL_Log("Failed to set clipboard bitmap");
				}
				CloseClipboard();
				if (clipboardOwnsBitmap)
					SDL_Log("Screenshot copied to clipboard");
			}
			else {
				SDL_Log("Failed to open clipboard");
			}
			if (!clipboardOwnsBitmap) {
				DeleteObject(hBitmap);
			}

			DeleteDC(hdcMem);
		}
		else {
			SDL_Log("Failed to create DIB section for clipboard");
		}

		ReleaseDC(NULL, hdcScreen);
#else
		SDL_Log("Clipboard copy not implemented for this platform");
#endif
#endif
	}

	bool EnsureParentDirectory(const std::filesystem::path& path) {
		const auto parent = path.parent_path();
		if (parent.empty()) {
			return true;
		}

		std::error_code ec;
		std::filesystem::create_directories(parent, ec);
		if (ec) {
			SDL_Log("Could not create recording directory %s: %s",
				parent.string().c_str(), ec.message().c_str());
			return false;
		}
		return true;
	}

#ifdef __ANDROID__
	inline uint8_t ClampByte(int value) {
		return static_cast<uint8_t>(std::clamp(value, 0, 255));
	}

	class AndroidVideoEncoder {
	public:
		~AndroidVideoEncoder() {
			Stop();
		}

		bool Start(const std::filesystem::path& path, int videoWidth, int videoHeight, int videoFps) {
			Stop();
			if (android_get_device_api_level() < 21) {
				SDL_Log("Android recording requires API level 21 or newer.");
				return false;
			}
			if (!EnsureParentDirectory(path)) {
				return false;
			}

			width = videoWidth;
			height = videoHeight;
			fps = std::max(1, videoFps);
			frameIndex = 0;

			const std::string pathString = path.string();
			fd = open(pathString.c_str(), O_CREAT | O_TRUNC | O_WRONLY, 0666);
			if (fd < 0) {
				SDL_Log("Could not open recording output %s", pathString.c_str());
				return false;
			}

			muxer = AMediaMuxer_new(fd, AMEDIAMUXER_OUTPUT_FORMAT_MPEG_4);
			codec = AMediaCodec_createEncoderByType("video/avc");
			if (!muxer || !codec) {
				SDL_Log("Could not create Android media encoder.");
				Stop();
				return false;
			}

			AMediaFormat* format = AMediaFormat_new();
			AMediaFormat_setString(format, AMEDIAFORMAT_KEY_MIME, "video/avc");
			AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_WIDTH, width);
			AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_HEIGHT, height);
			AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_COLOR_FORMAT, kColorFormatYuv420SemiPlanar);
			AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_BIT_RATE, std::max(256000, width * height * fps / 2));
			AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_FRAME_RATE, fps);
			AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_I_FRAME_INTERVAL, 2);

			media_status_t status = AMediaCodec_configure(codec, format, nullptr, nullptr, AMEDIACODEC_CONFIGURE_FLAG_ENCODE);
			AMediaFormat_delete(format);
			if (status != AMEDIA_OK) {
				SDL_Log("Could not configure Android media encoder: %d", status);
				Stop();
				return false;
			}

			status = AMediaCodec_start(codec);
			if (status != AMEDIA_OK) {
				SDL_Log("Could not start Android media encoder: %d", status);
				Stop();
				return false;
			}

			started = true;
			return true;
		}

		bool WriteRgbaFrame(const uint8_t* rgba, int pitch) {
			if (!started) {
				return false;
			}

			if (!Drain(false)) {
				return false;
			}

			ssize_t inputIndex = AMediaCodec_dequeueInputBuffer(codec, 10000);
			if (inputIndex < 0) {
				SDL_Log("Android media encoder input buffer was not available.");
				return false;
			}

			size_t inputSize = 0;
			uint8_t* input = AMediaCodec_getInputBuffer(codec, inputIndex, &inputSize);
			const size_t needed = static_cast<size_t>(width) * height * 3 / 2;
			if (!input || inputSize < needed) {
				SDL_Log("Android media encoder input buffer is too small.");
				return false;
			}

			ConvertRgbaToNv12(rgba, pitch, input);
			const int64_t ptsUs = static_cast<int64_t>(frameIndex) * 1000000 / fps;
			media_status_t status = AMediaCodec_queueInputBuffer(codec, inputIndex, 0, needed, ptsUs, 0);
			if (status != AMEDIA_OK) {
				SDL_Log("Could not queue Android media encoder input: %d", status);
				return false;
			}

			++frameIndex;
			return Drain(false);
		}

		void Stop() {
			if (started && codec) {
				ssize_t inputIndex = AMediaCodec_dequeueInputBuffer(codec, 10000);
				if (inputIndex >= 0) {
					const int64_t ptsUs = static_cast<int64_t>(frameIndex) * 1000000 / std::max(1, fps);
					AMediaCodec_queueInputBuffer(codec, inputIndex, 0, 0, ptsUs, AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
					Drain(true);
				}
				AMediaCodec_stop(codec);
			}
			if (codec) {
				AMediaCodec_delete(codec);
				codec = nullptr;
			}
			if (muxer) {
				if (muxerStarted) {
					AMediaMuxer_stop(muxer);
				}
				AMediaMuxer_delete(muxer);
				muxer = nullptr;
			}
			if (fd >= 0) {
				close(fd);
				fd = -1;
			}

			started = false;
			muxerStarted = false;
			trackIndex = -1;
			frameIndex = 0;
		}

		bool IsOpen() const {
			return started;
		}

	private:
		void ConvertRgbaToNv12(const uint8_t* rgba, int pitch, uint8_t* yuv) const {
			uint8_t* yPlane = yuv;
			uint8_t* uvPlane = yuv + static_cast<size_t>(width) * height;

			for (int y = 0; y < height; ++y) {
				const uint8_t* row = rgba + static_cast<size_t>(y) * pitch;
				for (int x = 0; x < width; ++x) {
					const uint8_t* px = row + x * 4;
					const int r = px[0];
					const int g = px[1];
					const int b = px[2];
					yPlane[y * width + x] = ClampByte(((66 * r + 129 * g + 25 * b + 128) >> 8) + 16);
				}
			}

			for (int y = 0; y < height; y += 2) {
				for (int x = 0; x < width; x += 2) {
					int uSum = 0;
					int vSum = 0;
					for (int yy = 0; yy < 2; ++yy) {
						const uint8_t* row = rgba + static_cast<size_t>(y + yy) * pitch;
						for (int xx = 0; xx < 2; ++xx) {
							const uint8_t* px = row + (x + xx) * 4;
							const int r = px[0];
							const int g = px[1];
							const int b = px[2];
							uSum += ((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128;
							vSum += ((112 * r - 94 * g - 18 * b + 128) >> 8) + 128;
						}
					}

					const size_t uvIndex = static_cast<size_t>(y / 2) * width + x;
					uvPlane[uvIndex] = ClampByte(uSum / 4);
					uvPlane[uvIndex + 1] = ClampByte(vSum / 4);
				}
			}
		}

		bool Drain(bool endOfStream) {
			while (true) {
				AMediaCodecBufferInfo info{};
				ssize_t outputIndex = AMediaCodec_dequeueOutputBuffer(codec, &info, endOfStream ? 10000 : 0);
				if (outputIndex >= 0) {
					if ((info.flags & AMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG) != 0) {
						info.size = 0;
					}

					if (info.size > 0) {
						if (!muxerStarted) {
							SDL_Log("Android media encoder produced data before muxer was ready.");
							return false;
						}

						size_t outputSize = 0;
						uint8_t* output = AMediaCodec_getOutputBuffer(codec, outputIndex, &outputSize);
						if (!output || static_cast<size_t>(info.offset + info.size) > outputSize) {
							SDL_Log("Android media encoder output buffer is invalid.");
							AMediaCodec_releaseOutputBuffer(codec, outputIndex, false);
							return false;
						}
						AMediaCodecBufferInfo sampleInfo = info;
						sampleInfo.offset = 0;
						media_status_t status = AMediaMuxer_writeSampleData(muxer, trackIndex, output + info.offset, &sampleInfo);
						if (status != AMEDIA_OK) {
							SDL_Log("Could not write Android media sample: %d", status);
							AMediaCodec_releaseOutputBuffer(codec, outputIndex, false);
							return false;
						}
					}

					AMediaCodec_releaseOutputBuffer(codec, outputIndex, false);
					if ((info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) != 0) {
						return true;
					}
				}
				else if (outputIndex == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
					AMediaFormat* outputFormat = AMediaCodec_getOutputFormat(codec);
					trackIndex = AMediaMuxer_addTrack(muxer, outputFormat);
					AMediaFormat_delete(outputFormat);
					if (trackIndex < 0 || AMediaMuxer_start(muxer) != AMEDIA_OK) {
						SDL_Log("Could not start Android media muxer.");
						return false;
					}
					muxerStarted = true;
				}
				else if (outputIndex == AMEDIACODEC_INFO_TRY_AGAIN_LATER) {
					if (endOfStream) {
						continue;
					}
					return true;
				}
				else {
					return true;
				}
			}
		}

		static constexpr int32_t kColorFormatYuv420SemiPlanar = 21;

		AMediaCodec* codec = nullptr;
		AMediaMuxer* muxer = nullptr;
		int fd = -1;
		int trackIndex = -1;
		bool started = false;
		bool muxerStarted = false;
		int width = 0;
		int height = 0;
		int fps = 30;
		int64_t frameIndex = 0;
	};
#endif

#if !defined(__ANDROID__) && !defined(IOS)
	class RawVideoPipe {
	public:
		~RawVideoPipe() {
			Stop();
		}

		bool Start(const std::string& command) {
			Stop();
#ifdef _WIN32
			SECURITY_ATTRIBUTES securityAttrs{};
			securityAttrs.nLength = sizeof(securityAttrs);
			securityAttrs.bInheritHandle = TRUE;

			HANDLE stdinRead = nullptr;
			if (!CreatePipe(&stdinRead, &stdinWrite, &securityAttrs, 0)) {
				return false;
			}
			if (!SetHandleInformation(stdinWrite, HANDLE_FLAG_INHERIT, 0)) {
				CloseHandle(stdinRead);
				CloseHandle(stdinWrite);
				stdinWrite = nullptr;
				return false;
			}

			HANDLE nullOutput = CreateFileA("NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
				&securityAttrs, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

			STARTUPINFOA startupInfo{};
			startupInfo.cb = sizeof(startupInfo);
			startupInfo.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
			startupInfo.wShowWindow = SW_HIDE;
			startupInfo.hStdInput = stdinRead;
			startupInfo.hStdOutput = nullOutput != INVALID_HANDLE_VALUE ? nullOutput : GetStdHandle(STD_OUTPUT_HANDLE);
			startupInfo.hStdError = nullOutput != INVALID_HANDLE_VALUE ? nullOutput : GetStdHandle(STD_ERROR_HANDLE);

			PROCESS_INFORMATION processInfo{};
			std::string mutableCommand = command;
			BOOL created = CreateProcessA(nullptr, mutableCommand.data(), nullptr, nullptr, TRUE,
				CREATE_NO_WINDOW, nullptr, nullptr, &startupInfo, &processInfo);

			CloseHandle(stdinRead);
			if (nullOutput != INVALID_HANDLE_VALUE) {
				CloseHandle(nullOutput);
			}

			if (!created) {
				CloseHandle(stdinWrite);
				stdinWrite = nullptr;
				return false;
			}

			processHandle = processInfo.hProcess;
			CloseHandle(processInfo.hThread);
			return true;
#else
			// Ignore SIGPIPE so broken pipe won't crash the process
			std::signal(SIGPIPE, SIG_IGN);
			pipe = ::popen(command.c_str(), "w");
			return pipe != nullptr;
#endif
		}

		bool Write(const uint8_t* data, size_t size) {
#ifdef _WIN32
			if (!stdinWrite) {
				return false;
			}

			size_t offset = 0;
			while (offset < size) {
				DWORD chunk = static_cast<DWORD>(std::min<size_t>(size - offset, 1 << 20));
				DWORD written = 0;
				if (!WriteFile(stdinWrite, data + offset, chunk, &written, nullptr) || written == 0) {
					return false;
				}
				offset += written;
			}
			return true;
#else
			if (!pipe) {
				return false;
			}
			return std::fwrite(data, 1, size, pipe) == size;
#endif
		}

		void Stop() {
#ifdef _WIN32
			if (stdinWrite) {
				CloseHandle(stdinWrite);
				stdinWrite = nullptr;
			}
			if (processHandle) {
				DWORD waitResult = WaitForSingleObject(processHandle, 5000);
				if (waitResult == WAIT_TIMEOUT) {
					SDL_Log("Timed out while finalizing recording; terminating ffmpeg.");
					TerminateProcess(processHandle, 1);
				}
				CloseHandle(processHandle);
				processHandle = nullptr;
			}
#else
			if (pipe) {
				::pclose(pipe);
				pipe = nullptr;
			}
#endif
		}

		bool IsOpen() const {
#ifdef _WIN32
			return stdinWrite != nullptr;
#else
			return pipe != nullptr;
#endif
		}

	private:
#ifdef _WIN32
		HANDLE stdinWrite = nullptr;
		HANDLE processHandle = nullptr;
#else
		FILE* pipe = nullptr;
#endif
	};
#endif

	SDL_Color CaptureBackgroundColour(uint32_t rgb) {
		return {
			static_cast<Uint8>((rgb >> 16) & 0xff),
			static_cast<Uint8>((rgb >> 8) & 0xff),
			static_cast<Uint8>(rgb & 0xff),
			255};
	}

	Uint32 MapScreenshotPixel(SDL_PixelFormat* format, const ColourInfo& ink_colour, const SDL_Color& background, float alpha_value) {
		if (alpha_value > 255.0f) {
			const SDL_Color colour = ScreenPixelColour(ink_colour, alpha_value);
			return SDL_MapRGBA(format, colour.r, colour.g, colour.b, colour.a);
		}

		const int alpha = std::clamp(static_cast<int>(std::lround(alpha_value)), 0, 255);
		const auto blend = [alpha](int foreground, int background_channel) {
			return static_cast<uint8_t>((foreground * alpha + background_channel * (255 - alpha) + 127) / 255);
		};
		return SDL_MapRGBA(format,
			blend(ink_colour.r, background.r),
			blend(ink_colour.g, background.g),
			blend(ink_colour.b, background.b),
			255);
	}

	void FillScaledPixel(SDL_Surface* surface, int x, int y, int scale, Uint32 colour) {
		for (int dy = 0; dy < scale; ++dy) {
			const int py = y + dy;
			if (py < 0 || py >= surface->h)
				continue;
			auto* row = reinterpret_cast<Uint32*>(static_cast<uint8_t*>(surface->pixels) + py * surface->pitch);
			for (int dx = 0; dx < scale; ++dx) {
				const int px = x + dx;
				if (px >= 0 && px < surface->w)
					row[px] = colour;
			}
		}
	}

	SDL_Rect ScaleScreenshotRect(const SDL_Rect& rect, const SDL_Rect& capture_rect, double sx, double sy) {
		const int x0 = static_cast<int>(std::floor((rect.x - capture_rect.x) * sx));
		const int y0 = static_cast<int>(std::floor((rect.y - capture_rect.y) * sy));
		const int x1 = static_cast<int>(std::ceil((rect.x + rect.w - capture_rect.x) * sx));
		const int y1 = static_cast<int>(std::ceil((rect.y + rect.h - capture_rect.y) * sy));
		return {x0, y0, std::max(1, x1 - x0), std::max(1, y1 - y0)};
	}

	Rect ToModelRect(const SDL_Rect& rect) {
		return {rect.x, rect.y, rect.w, rect.h};
	}

	struct ScreenCaptureSource {
		SDL_Texture* interface_texture = nullptr;
		SDL_Surface* interface_surface = nullptr;
		const std::vector<SpriteInfo>* sprite_info = nullptr;
		const std::vector<uint8_t>* sprite_available = nullptr;
		const ColourInfo* ink_colour = nullptr;
		const float* screen_ink_alpha = nullptr;
		int logical_width = 0;
		int logical_height = 0;
		SDL_Rect lcd_dest{};
		bool render_pixel_layer = true;
	};

	struct ScreenCaptureLayout {
		SDL_Rect capture_rect{};
		SDL_Rect scaled_lcd{};
		int scale = 3;
		int content_width = 0;
		int content_height = 0;
		int output_width = 0;
		int output_height = 0;
		double sx = 1.0;
		double sy = 1.0;
	};

	bool BuildScreenCaptureLayout(const ScreenCaptureSource& source, int requested_scale, bool even_output, ScreenCaptureLayout& layout, const char* purpose) {
		if (!source.sprite_info || !source.sprite_available ||
			!source.ink_colour || !source.screen_ink_alpha ||
			source.logical_width <= 0 || source.logical_height <= 0 || source.lcd_dest.w <= 0 || source.lcd_dest.h <= 0) {
			SDL_Log("%s failed: invalid capture source: texture=%p sprites=%p available=%p ink=%p alpha=%p logical=%dx%d lcd=%d,%d %dx%d.",
				purpose,
				static_cast<void*>(source.interface_texture),
				static_cast<const void*>(source.sprite_info),
				static_cast<const void*>(source.sprite_available),
				static_cast<const void*>(source.ink_colour),
				static_cast<const void*>(source.screen_ink_alpha),
				source.logical_width,
				source.logical_height,
				source.lcd_dest.x,
				source.lcd_dest.y,
				source.lcd_dest.w,
				source.lcd_dest.h);
			return false;
		}

		std::vector<SDL_Rect> spriteRects;
		for (size_t ix = 1; ix < source.sprite_info->size() && ix < source.sprite_available->size(); ++ix) {
			if (!(*source.sprite_available)[ix])
				continue;
			spriteRects.push_back((*source.sprite_info)[ix].dest);
		}

		layout = {};
		if (!GetCaptureRect(spriteRects, std::vector<SDL_Rect>{source.lcd_dest}, layout.capture_rect)) {
			SDL_Log("%s failed: invalid capture region.", purpose);
			return false;
		}

		layout.scale = std::max(1, requested_scale);
		layout.sx = static_cast<double>(source.logical_width * layout.scale) / static_cast<double>(source.lcd_dest.w);
		layout.sy = static_cast<double>(source.logical_height * layout.scale) / static_cast<double>(source.lcd_dest.h);
		layout.content_width = std::max(1, static_cast<int>(std::ceil(layout.capture_rect.w * layout.sx)));
		layout.content_height = std::max(1, static_cast<int>(std::ceil(layout.capture_rect.h * layout.sy)));
		layout.output_width = even_output ? ((layout.content_width + 1) & ~1) : layout.content_width;
		layout.output_height = even_output ? ((layout.content_height + 1) & ~1) : layout.content_height;
		layout.scaled_lcd = ScaleScreenshotRect(source.lcd_dest, layout.capture_rect, layout.sx, layout.sy);
		return true;
	}

	class ScreenCaptureComposer {
	public:
		~ScreenCaptureComposer() {
			Reset();
		}

		void Reset() {
			if (target) {
				SDL_DestroyTexture(target);
				target = nullptr;
			}
			target_width = 0;
			target_height = 0;
		}

		bool Render(SDL_Renderer* renderer, const ScreenCaptureSource& source, const ScreenCaptureLayout& layout, SDL_Surface* surface, const SDL_Color& background, const char* purpose) {
			if (!renderer || !surface || surface->w != layout.output_width || surface->h != layout.output_height) {
				SDL_Log("%s failed: invalid capture surface.", purpose);
				return false;
			}
			if (!EnsureTarget(renderer, layout.output_width, layout.output_height, purpose)) {
				return false;
			}

			SDL_Texture* old_target = SDL_GetRenderTarget(renderer);
			SDL_Rect old_viewport{};
			SDL_Rect old_clip{};
			float old_scale_x = 1.0f;
			float old_scale_y = 1.0f;
			SDL_BlendMode old_blend_mode{};
			SDL_RenderGetViewport(renderer, &old_viewport);
			SDL_RenderGetClipRect(renderer, &old_clip);
			const SDL_bool old_clip_enabled = SDL_RenderIsClipEnabled(renderer);
			SDL_RenderGetScale(renderer, &old_scale_x, &old_scale_y);
			SDL_GetRenderDrawBlendMode(renderer, &old_blend_mode);
			bool render_target_active = false;
			auto restore = [&]() {
				if (!render_target_active)
					return;
				SDL_SetRenderTarget(renderer, old_target);
				SDL_RenderSetViewport(renderer, &old_viewport);
				SDL_RenderSetClipRect(renderer, old_clip_enabled ? &old_clip : nullptr);
				SDL_RenderSetScale(renderer, old_scale_x, old_scale_y);
				SDL_SetRenderDrawBlendMode(renderer, old_blend_mode);
				render_target_active = false;
			};

			if (SDL_SetRenderTarget(renderer, target) != 0) {
				SDL_Log("%s failed: cannot bind render target: %s", purpose, SDL_GetError());
				return false;
			}
			render_target_active = true;
			SDL_RenderSetViewport(renderer, nullptr);
			SDL_RenderSetClipRect(renderer, nullptr);
			SDL_RenderSetScale(renderer, 1.0f, 1.0f);
			SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
			SDL_SetRenderDrawColor(renderer, background.r, background.g, background.b, background.a);
			SDL_RenderClear(renderer);

			if (svg_texture_cache.size() < source.sprite_info->size())
				svg_texture_cache.resize(source.sprite_info->size());
			for (size_t ix = 1; ix < source.sprite_info->size() && ix < source.sprite_available->size(); ++ix) {
				if (!(*source.sprite_available)[ix])
					continue;
				const int alpha_index = static_cast<int>(ix - 1);
				SpriteInfo sprite = (*source.sprite_info)[ix];
				sprite.dest = ToModelRect(ScaleScreenshotRect(sprite.dest, layout.capture_rect, layout.sx, layout.sy));
				const uint8_t alpha = Uint8(std::clamp(static_cast<int>(source.screen_ink_alpha[alpha_index]), 0, 255));
				RenderModelSprite(renderer, source.interface_texture, &svg_texture_cache[ix], sprite, *source.ink_colour, alpha);
			}

			if (SDL_MUSTLOCK(surface) && SDL_LockSurface(surface) != 0) {
				SDL_Log("%s failed: cannot lock capture surface: %s", purpose, SDL_GetError());
				restore();
				return false;
			}

			if (SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_RGBA32, surface->pixels, surface->pitch) != 0) {
				SDL_Log("%s failed: cannot read capture target pixels: %s", purpose, SDL_GetError());
				if (SDL_MUSTLOCK(surface))
					SDL_UnlockSurface(surface);
				restore();
				return false;
			}
			restore();

			if (source.render_pixel_layer) {
				for (int y = 0; y < source.logical_height; ++y) {
					const int source_y = y + 1;
					for (int x = 0; x < source.logical_width; ++x) {
						const float alpha_value = source.screen_ink_alpha[x + source_y * 192];
						if (alpha_value <= 0.0f)
							continue;
						FillScaledPixel(surface, layout.scaled_lcd.x + x * layout.scale, layout.scaled_lcd.y + y * layout.scale, layout.scale, MapScreenshotPixel(surface->format, *source.ink_colour, background, alpha_value));
					}
				}
			}
			if (SDL_MUSTLOCK(surface))
				SDL_UnlockSurface(surface);
			return true;
		}

	private:
		bool EnsureTarget(SDL_Renderer* renderer, int width, int height, const char* purpose) {
			if (target && target_width == width && target_height == height)
				return true;
			Reset();
			target = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, width, height);
			if (!target) {
				SDL_Log("%s failed: cannot create render target: %s", purpose, SDL_GetError());
				return false;
			}
			SDL_SetTextureBlendMode(target, SDL_BLENDMODE_NONE);
			target_width = width;
			target_height = height;
			return true;
		}

		SDL_Texture* target = nullptr;
		int target_width = 0;
		int target_height = 0;
		std::vector<SvgSpriteTextureCache> svg_texture_cache;
	};

#ifndef IOS

	class ScreenRecorder {
	public:
		~ScreenRecorder() {
			Stop();
		}

		bool Start(const ScreenCaptureSource& source, int capture_scale, int requestedFps = 30) {
			Stop();
			if (!BuildScreenCaptureLayout(source, capture_scale, true, layout, "Recording")) {
				return false;
			}

			fps = std::max(1, requestedFps);
			outputWidth = layout.output_width;
			outputHeight = layout.output_height;
			frameCount = 0;
			nextCaptureTick = 0;
			if (!EnsureFrameSurface()) {
				return false;
			}

			const std::string stem = MakeTimestampedName("recording-", "");
			outputPath = GetRecordingOutputPath(stem + ".mp4");
#ifdef __ANDROID__
			if (encoder.Start(outputPath, outputWidth, outputHeight, fps)) {
				frameSequence = false;
				recording = true;
				SDL_Log("Recording started: %s", outputPath.string().c_str());
				return true;
			}
#elif !defined(IOS)
			const std::string command = BuildFfmpegCommand(outputPath);
			std::string check_cmd = command + " -version > /dev/null 2>&1";
			// Check if ffmpeg exists by running it with -version
			std::string ffmpeg_path = "ffmpeg";
			if (std::filesystem::exists("/opt/homebrew/bin/ffmpeg")) ffmpeg_path = "/opt/homebrew/bin/ffmpeg";
			else if (std::filesystem::exists("/usr/local/bin/ffmpeg")) ffmpeg_path = "/usr/local/bin/ffmpeg";
#ifndef IOS
			if (std::system((ffmpeg_path + " -version > /dev/null 2>&1").c_str()) == 0) {
				if (encoder.Start(command)) {
					frameSequence = false;
					recording = true;
					SDL_Log("Recording started: %s", outputPath.string().c_str());
					return true;
				}
			}
#endif
#endif

			frameSequence = true;
			frameDirectory = GetRecordingOutputPath(stem + "-frames");
			std::error_code ec;
			std::filesystem::create_directories(frameDirectory, ec);
			if (ec) {
				SDL_Log("Recording failed: cannot create frame directory %s (%s)",
					frameDirectory.string().c_str(), ec.message().c_str());
				ResetFrameSurface();
				return false;
			}

			recording = true;
#ifdef __ANDROID__
			SDL_Log("Android video encoder was not available; recording PNG frames to %s", frameDirectory.string().c_str());
#else
			SDL_Log("ffmpeg was not available; recording PNG frames to %s", frameDirectory.string().c_str());
#endif
			return true;
		}

		void Stop() {
#if !defined(IOS)
			if (!recording && !encoder.IsOpen()) {
				ResetFrameSurface();
				composer.Reset();
				return;
			}
			encoder.Stop();
#else
			if (!recording) {
				return;
			}
#endif
			if (recording) {
				if (frameSequence) {
					SDL_Log("Recording stopped: %u frames saved to %s",
						frameCount, frameDirectory.string().c_str());
				}
				else {
					SDL_Log("Recording stopped: %u frames saved to %s",
						frameCount, outputPath.string().c_str());
				}
			}
			recording = false;
			ResetFrameSurface();
			composer.Reset();
		}

		bool CaptureFrame(SDL_Renderer* renderer, const ScreenCaptureSource& source, const SDL_Color& background) {
			if (!recording) {
				return false;
			}

			Uint64 now = SDL_GetTicks64();
			if (nextCaptureTick != 0 && now < nextCaptureTick) {
				return true;
			}
			nextCaptureTick = now + static_cast<Uint64>(1000 / fps);

			if (!frameSurface || !composer.Render(renderer, source, layout, frameSurface, background, "Recording")) {
				Stop();
				return false;
			}

			bool success = frameSequence
				? SaveFrameAsPng()
#ifdef __ANDROID__
				: encoder.WriteRgbaFrame(framePixels.data(), frameSurface->pitch);
#elif !defined(IOS)
				: encoder.Write(framePixels.data(), framePixels.size());
#else
				: false;
#endif
			if (!success) {
				SDL_Log("Recording stopped because frame writing failed.");
				Stop();
				return false;
			}

			++frameCount;
			return true;
		}

		bool IsRecording() const {
			return recording;
		}

		unsigned int FrameCount() const {
			return frameCount;
		}

	private:
		std::string BuildFfmpegCommand(const std::filesystem::path& path) const {
			std::string ffmpeg_path = "ffmpeg";
#if !defined(_WIN32) && !defined(__ANDROID__) && !defined(IOS)
			if (std::filesystem::exists("/opt/homebrew/bin/ffmpeg")) {
				ffmpeg_path = "/opt/homebrew/bin/ffmpeg";
			} else if (std::filesystem::exists("/usr/local/bin/ffmpeg")) {
				ffmpeg_path = "/usr/local/bin/ffmpeg";
			}
#endif
			std::ostringstream command;
			command << ffmpeg_path << " -y -hide_banner -loglevel error"
				<< " -f rawvideo -vcodec rawvideo"
				<< " -pixel_format rgba"
				<< " -video_size " << outputWidth << "x" << outputHeight
				<< " -framerate " << fps
				<< " -i - -an -c:v mpeg4 -q:v 3 -pix_fmt yuv420p "
				<< "\"" << path.string() << "\"";
			return command.str();
		}

		bool EnsureFrameSurface() {
			ResetFrameSurface();
			const int pitch = outputWidth * 4;
			framePixels.assign(static_cast<size_t>(pitch) * outputHeight, 255);
			frameSurface = SDL_CreateRGBSurfaceWithFormatFrom(
				framePixels.data(),
				outputWidth,
				outputHeight,
				32,
				pitch,
				SDL_PIXELFORMAT_RGBA32);
			if (!frameSurface) {
				SDL_Log("Recording failed: cannot create frame surface: %s", SDL_GetError());
				framePixels.clear();
				return false;
			}
			return true;
		}

		void ResetFrameSurface() {
			if (frameSurface) {
				SDL_FreeSurface(frameSurface);
				frameSurface = nullptr;
			}
			framePixels.clear();
		}

		bool SaveFrameAsPng() const {
			std::ostringstream filename;
			filename << "frame-" << std::setw(6) << std::setfill('0') << frameCount << ".png";
			std::filesystem::path framePath = frameDirectory / filename.str();

			const std::string pathString = framePath.string();
			int result = IMG_SavePNG(frameSurface, pathString.c_str());
			if (result != 0) {
				SDL_Log("Error saving recording frame: %s", IMG_GetError());
				return false;
			}
			return true;
		}

#ifdef __ANDROID__
		AndroidVideoEncoder encoder;
#elif !defined(IOS)
		RawVideoPipe encoder;
#endif
		ScreenCaptureLayout layout{};
		ScreenCaptureComposer composer;
		std::vector<uint8_t> framePixels;
		SDL_Surface* frameSurface = nullptr;
		int fps = 30;
		int outputWidth = 0;
		int outputHeight = 0;
		Uint64 nextCaptureTick = 0;
		unsigned int frameCount = 0;
		bool recording = false;
		bool frameSequence = false;
		std::filesystem::path outputPath;
		std::filesystem::path frameDirectory;
	};
#endif

	class ScreenMirrorComposer {
	public:
		~ScreenMirrorComposer() {
			Reset();
		}

		void Reset() {
			if (pixel_texture) {
				SDL_DestroyTexture(pixel_texture);
				pixel_texture = nullptr;
			}
			if (interface_texture) {
				SDL_DestroyTexture(interface_texture);
				interface_texture = nullptr;
			}
			pixel_width = 0;
			pixel_height = 0;
			interface_surface = nullptr;
			pixel_pixels.clear();
			svg_texture_cache.clear();
		}

		bool Render(ScreenMirror& mirror, const ScreenCaptureSource& source, const SDL_Color& background) {
			SDL_Renderer* mirror_renderer = mirror.renderer();
			if (!mirror_renderer)
				return false;

			SDL_Rect capture_rect{};
			if (!BuildCaptureRect(source, capture_rect)) {
				SDL_Log("Mirror update failed: invalid capture region.");
				return false;
			}

			mirror.clear(background);
			const SDL_Rect content_rect = mirror.contentRect();
			if (content_rect.w <= 0 || content_rect.h <= 0)
				return false;

			const double sx = static_cast<double>(content_rect.w) / static_cast<double>(capture_rect.w);
			const double sy = static_cast<double>(content_rect.h) / static_cast<double>(capture_rect.h);
			SDL_Texture* fallback_texture = EnsureInterfaceTexture(mirror_renderer, source);

			if (svg_texture_cache.size() < source.sprite_info->size())
				svg_texture_cache.resize(source.sprite_info->size());
			for (size_t ix = 1; ix < source.sprite_info->size() && ix < source.sprite_available->size(); ++ix) {
				if (!(*source.sprite_available)[ix])
					continue;
				const int alpha_index = static_cast<int>(ix - 1);
				SpriteInfo sprite = (*source.sprite_info)[ix];
				SDL_Rect dest = ScaleScreenshotRect(sprite.dest, capture_rect, sx, sy);
				dest.x += content_rect.x;
				dest.y += content_rect.y;
				sprite.dest = ToModelRect(dest);
				const uint8_t alpha = Uint8(std::clamp(static_cast<int>(source.screen_ink_alpha[alpha_index]), 0, 255));
				RenderModelSprite(mirror_renderer, fallback_texture, &svg_texture_cache[ix], sprite, *source.ink_colour, alpha);
			}

			if (source.render_pixel_layer) {
				SDL_Rect lcd_dest = ScaleScreenshotRect(source.lcd_dest, capture_rect, sx, sy);
				lcd_dest.x += content_rect.x;
				lcd_dest.y += content_rect.y;
				if (!RenderPixelTexture(mirror_renderer, source, lcd_dest))
					return false;
			}
			mirror.present();
			return true;
		}

	private:
		bool BuildCaptureRect(const ScreenCaptureSource& source, SDL_Rect& capture_rect) const {
			if (!source.sprite_info || !source.sprite_available || !source.ink_colour || !source.screen_ink_alpha ||
				source.logical_width <= 0 || source.logical_height <= 0 || source.lcd_dest.w <= 0 || source.lcd_dest.h <= 0)
				return false;

			std::vector<SDL_Rect> sprite_rects;
			for (size_t ix = 1; ix < source.sprite_info->size() && ix < source.sprite_available->size(); ++ix) {
				if ((*source.sprite_available)[ix])
					sprite_rects.push_back((*source.sprite_info)[ix].dest);
			}
			return GetCaptureRect(sprite_rects, std::vector<SDL_Rect>{source.lcd_dest}, capture_rect);
		}

		SDL_Texture* EnsureInterfaceTexture(SDL_Renderer* renderer, const ScreenCaptureSource& source) {
			if (!renderer || !source.interface_surface)
				return nullptr;
			if (interface_texture && interface_surface == source.interface_surface)
				return interface_texture;
			if (interface_texture) {
				SDL_DestroyTexture(interface_texture);
				interface_texture = nullptr;
			}
			interface_surface = source.interface_surface;
			interface_texture = SDL_CreateTextureFromSurface(renderer, source.interface_surface);
			if (!interface_texture) {
				SDL_Log("Mirror update failed: cannot create interface texture: %s", SDL_GetError());
				interface_surface = nullptr;
				return nullptr;
			}
			SDL_SetTextureBlendMode(interface_texture, SDL_BLENDMODE_BLEND);
			return interface_texture;
		}

		bool EnsurePixelTexture(SDL_Renderer* renderer, int width, int height) {
			if (!renderer || width <= 0 || height <= 0)
				return false;
			if (pixel_texture && pixel_width == width && pixel_height == height)
				return true;
			if (pixel_texture) {
				SDL_DestroyTexture(pixel_texture);
				pixel_texture = nullptr;
			}
			pixel_texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, width, height);
			if (!pixel_texture) {
				SDL_Log("Mirror update failed: cannot create pixel texture: %s", SDL_GetError());
				pixel_width = 0;
				pixel_height = 0;
				pixel_pixels.clear();
				return false;
			}
			SDL_SetTextureBlendMode(pixel_texture, SDL_BLENDMODE_BLEND);
#if SDL_VERSION_ATLEAST(2, 0, 12)
			SDL_SetTextureScaleMode(pixel_texture, SDL_ScaleModeNearest);
#endif
			pixel_width = width;
			pixel_height = height;
			pixel_pixels.resize(static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
			return true;
		}

		bool RenderPixelTexture(SDL_Renderer* renderer, const ScreenCaptureSource& source, const SDL_Rect& dest) {
			if (!EnsurePixelTexture(renderer, source.logical_width, source.logical_height))
				return false;
			for (int y = 0; y < source.logical_height; ++y) {
				const int source_y = y + 1;
				for (int x = 0; x < source.logical_width; ++x) {
					const SDL_Color colour = ScreenPixelColour(*source.ink_colour, source.screen_ink_alpha[x + source_y * 192]);
					const size_t pixel_offset = (static_cast<size_t>(y) * static_cast<size_t>(source.logical_width) + static_cast<size_t>(x)) * 4;
					pixel_pixels[pixel_offset + 0] = colour.r;
					pixel_pixels[pixel_offset + 1] = colour.g;
					pixel_pixels[pixel_offset + 2] = colour.b;
					pixel_pixels[pixel_offset + 3] = colour.a;
				}
			}
			if (SDL_UpdateTexture(pixel_texture, nullptr, pixel_pixels.data(), source.logical_width * 4) != 0) {
				SDL_Log("Mirror update failed: cannot update pixel texture: %s", SDL_GetError());
				return false;
			}
			if (SDL_RenderCopy(renderer, pixel_texture, nullptr, &dest) != 0) {
				SDL_Log("Mirror update failed: cannot render pixel texture: %s", SDL_GetError());
				return false;
			}
			return true;
		}

		SDL_Texture* pixel_texture = nullptr;
		int pixel_width = 0;
		int pixel_height = 0;
		std::vector<uint8_t> pixel_pixels;
		SDL_Surface* interface_surface = nullptr;
		SDL_Texture* interface_texture = nullptr;
		std::vector<SvgSpriteTextureCache> svg_texture_cache;
	};

	bool CapturePixelPerfectScreenshot(
		SDL_Renderer* renderer,
		SDL_Texture* interface_texture,
		SDL_Surface* interface_surface,
		const std::vector<SpriteInfo>& sprite_info,
		const std::vector<uint8_t>& sprite_available,
		const ColourInfo& ink_colour,
		const float* screen_ink_alpha,
		int logical_width,
		int logical_height,
		const SDL_Rect& lcd_dest,
		int capture_scale,
		const SDL_Color& background,
		bool render_pixel_layer = true) {
		ScreenCaptureSource source{
			interface_texture,
			interface_surface,
			&sprite_info,
			&sprite_available,
			&ink_colour,
			screen_ink_alpha,
			logical_width,
			logical_height,
			lcd_dest,
			render_pixel_layer};

		ScreenCaptureLayout layout{};
		if (!BuildScreenCaptureLayout(source, capture_scale, false, layout, "Screenshot"))
			return false;

		SDL_Surface* screenSurface = SDL_CreateRGBSurfaceWithFormat(0, layout.output_width, layout.output_height, 32, SDL_PIXELFORMAT_RGBA32);
		if (!screenSurface) {
			SDL_Log("Error creating screenshot surface: %s", SDL_GetError());
			return false;
		}

		ScreenCaptureComposer composer;
		if (!composer.Render(renderer, source, layout, screenSurface, background, "Screenshot")) {
			SDL_FreeSurface(screenSurface);
			return false;
		}
		SaveScreenshotSurface(screenSurface, MakeTimestampedName("screenshot-", ".png"));
		SDL_FreeSurface(screenSurface);
		return true;
	}

	std::pair<int, int> GetSize(const ScreenCaptureSource& source) {
		SDL_Rect captureRect{};
		if (!source.sprite_info || !source.sprite_available)
			return {0, 0};
		std::vector<SDL_Rect> spriteRects;
		for (size_t ix = 1; ix < source.sprite_info->size() && ix < source.sprite_available->size(); ++ix) {
			if ((*source.sprite_available)[ix])
				spriteRects.push_back((*source.sprite_info)[ix].dest);
		}
		if (!GetCaptureRect(spriteRects, std::vector<SDL_Rect>{source.lcd_dest}, captureRect)) {
			return { 0, 0 };
		}
		return { captureRect.w, captureRect.h };
	}
#endif
	// Function to collect all sprite and pixel rectangles
	template <HardwareId hardware_id>
	void Screen<hardware_id>::Frame() {
		std::array<float, 66 * 192> frame_screen_ink_alpha{};
		{
			auto state_lock = LockScreenState();
#ifdef __EMSCRIPTEN__
			tick();
#elif defined(TEST_BUILD)
			if constexpr (IsEpsFamily(hardware_id))
				tick();
#endif
			if constexpr (IsEpsFamily(hardware_id)) {
				std::lock_guard<std::mutex> lock(eps_screen_alpha_mutex);
				std::copy(eps_screen_ink_alpha.begin(), eps_screen_ink_alpha.end(), screen_ink_alpha);
			}
			std::copy(std::begin(screen_ink_alpha), std::end(screen_ink_alpha), frame_screen_ink_alpha.begin());
		}
		int screenWidth = 0, screenHeight = 0;

		// Get the renderer output size if not already available
		SDL_GetRendererOutputSize(renderer, &screenWidth, &screenHeight);

		if (!emulator.ModelDefinition.enable_new_screen) {
			SDL_SetTextureColorMod(interface_texture, ink_colour.r, ink_colour.g, ink_colour.b);
		}

		for (int ix = 1; ix != SpriteCount(); ++ix) {
			if (ix >= static_cast<int>(sprite_available.size()) || !sprite_available[ix])
				continue;
			const int alpha_index = ix - 1;
			const uint8_t alpha = Uint8(std::clamp((int)frame_screen_ink_alpha[alpha_index], 0, 255));
			if (alpha == 0)
				continue;
			RenderModelSprite(renderer, interface_texture,
				ix < static_cast<int>(sprite_svg_textures.size()) ? &sprite_svg_textures[ix] : nullptr,
				sprite_info[ix], ink_colour, alpha);
		}

		static constexpr auto SPR_PIXEL = 0;
		SDL_Rect dest = Screen<hardware_id>::sprite_info[SPR_PIXEL].dest;
		const bool board_screen_slot = !emulator.ModelDefinition.board_path.empty();
		const bool segment_lcd = IsEpsSegmentLcd(hardware_id);
		int logical_width = 0;
		int logical_height = 0;
		if constexpr (IsEpsFamily(hardware_id)) {
			const auto eps_spec = GetEpsScreenSpec(
				hardware_id,
				emulator.ModelDefinition.screen_width,
				emulator.ModelDefinition.screen_height);
			logical_width = eps_spec.logical_width;
			logical_height = eps_spec.logical_height;
		}
		else if (segment_lcd) {
			logical_width = std::max(1, emulator.ModelDefinition.screen_width);
			logical_height = std::max(1, emulator.ModelDefinition.screen_height);
		}
		else {
			logical_width = ROW_SIZE_DISP * 8;
			logical_height = N_ROW;
		}
		SDL_Rect lcd_dest = dest;
		if (!board_screen_slot) {
			lcd_dest.w = std::max(1, (logical_width - 1) * sprite_info[SPR_PIXEL].src.w + sprite_info[SPR_PIXEL].dest.w);
			lcd_dest.h = std::max(1, (logical_height - 1) * sprite_info[SPR_PIXEL].src.h + sprite_info[SPR_PIXEL].dest.h);
		}

#ifndef CASIOEMU_CORE_WEB
		if (!segment_lcd)
			pixel_screen_texture.Render(renderer, lcd_dest, logical_width, logical_height, ink_colour, frame_screen_ink_alpha.data());
#endif

#if !defined(__EMSCRIPTEN__) && !defined(CASIOEMU_CORE_WEB)
		ScreenOutputFrame output_frame{
			renderer, interface_texture, emulator.interface_surface, &sprite_info,
			&sprite_available, &ink_colour, frame_screen_ink_alpha.data(),
			logical_width, logical_height, lcd_dest, !segment_lcd};
		std::lock_guard<std::mutex> output_lock(output_resource_mutex);
		if (!output)
			output = new ScreenOutput(emulator);
		output->Render(output_frame);
#endif
	}

	template <HardwareId hardware_id>
	void Screen<hardware_id>::Reset() {
		if constexpr (hardware_id == HW_TI_MATH_PRINT) {
			auto state_lock = LockScreenState();
			ti_sv_status = 0;
			ti_enabled = false;
			ti_a0 = false;
			ti_port_status = ti_col = ti_page = 0;
		}
	}

	Peripheral* CreateScreen(Emulator& emulator) {
		switch (emulator.hardware_id) {
		case HW_FX_5800P:
			return new Screen<HW_FX_5800P>(emulator);
		case HW_ES_PLUS:
			return new Screen<HW_ES_PLUS>(emulator);

		case HW_CLASSWIZ:
			return new Screen<HW_CLASSWIZ>(emulator);

		case HW_CLASSWIZ_II:
			return new Screen<HW_CLASSWIZ_II>(emulator);

		case HW_SOLARII:
			return CreateSolarIIScreen(emulator);

		case HW_TI_MULTI_VIEW:
			return CreateTiMultiViewScreen(emulator);
		case HW_TI_MATH_PRINT:
			return new Screen<HW_TI_MATH_PRINT>(emulator);
		case HW_EPS6800:
			return new Screen<HW_EPS6800>(emulator);
		case HW_EPS6800_W192:
			return new Screen<HW_EPS6800_W192>(emulator);
		case HW_EPS6009:
			return new Screen<HW_EPS6009>(emulator);
		case HW_EPS9500:
			return new Screen<HW_EPS9500>(emulator);
		default:
			PANIC("Unknown hardware id\n");
		}
		std::abort();
	}
} // namespace casioemu
