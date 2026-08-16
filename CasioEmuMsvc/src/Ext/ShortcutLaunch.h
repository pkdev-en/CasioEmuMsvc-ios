#pragma once
//
// Shared logic for resolving a Home Screen shortcut's
// casioemu://launch?model=<id> URL (delivered by SDL as an SDL_DROPFILE
// event -- see IOSNativeBridge.mm's LocalProfileServer /
// presentCreateHomeScreenShortcut, and SDL_uikitappdelegate.m's
// sendDropFileForURL:) back into an on-disk model path.
//
// This is used from TWO places, because the app can be sitting in either of
// two different SDL event loops when the user taps a shortcut icon:
//   - StartupUi.cpp's sui_loop() (the model-picker menu), when the app was
//     not already running a model.
//   - casioemu.cpp's active `while (emulator.Running())` loop, when the app
//     was already running a *different* model and the user tapped another
//     shortcut to switch. In that case ResolveShortcutLaunchEvent() alone
//     isn't enough to switch models -- casioemu.cpp additionally stashes the
//     resolved path in g_pending_shortcut_model and calls emulator.Shutdown()
//     to unwind the current session, then checks g_pending_shortcut_model
//     right after the loop to decide whether to loop back with the new
//     model instead of exiting (see casioemu.cpp, near the end of main()).
//
// Only wired up on iOS: this is the only platform where the app is a
// single long-lived process that can receive a second, different launch
// request via URL scheme while still running an earlier one. Desktop
// shortcuts relaunch the executable as a brand new process, and Android
// shortcuts start a new Activity/Intent, so neither needs this.

#include <SDL.h>
#include <cctype>
#include <cerrno>
#include <filesystem>
#include <iostream>
#include <string>

#ifdef IOS

// Set by casioemu.cpp's SDL_DROPFILE case when a shortcut asks for a model
// other than the one currently running. Read and cleared by main()'s outer
// loop right after the emulator loop ends, to decide whether to loop back
// with the new model instead of exiting. Only ever touched from the single
// SDL-event-processing thread, so no locking is needed.
inline std::string g_pending_shortcut_model;

inline std::string ShortcutLaunch_UrlPercentDecode(const std::string& in) {
	std::string out;
	out.reserve(in.size());
	for (size_t i = 0; i < in.size(); ++i) {
		if (in[i] == '%' && i + 2 < in.size() && isxdigit((unsigned char)in[i + 1]) && isxdigit((unsigned char)in[i + 2])) {
			auto hexval = [](char c) -> int {
				if (c >= '0' && c <= '9') return c - '0';
				return tolower((unsigned char)c) - 'a' + 10;
			};
			out += (char)((hexval(in[i + 1]) << 4) | hexval(in[i + 2]));
			i += 2;
		}
		else {
			out += in[i];
		}
	}
	return out;
}

// Resolves an SDL event to an on-disk model path if it is our Home Screen
// shortcut calling back into the app; returns an empty path otherwise (not
// a DROPFILE event, doesn't match our URL scheme, or the referenced model
// no longer exists on disk). Consumes (and SDL_free()s) event.drop.file if
// present, per SDL's ownership contract for SDL_DROPFILE events.
inline std::filesystem::path ResolveShortcutLaunchEvent(const SDL_Event& event) {
	if (event.type != SDL_DROPFILE || !event.drop.file)
		return {};

	std::string dropped = event.drop.file;
	SDL_free(event.drop.file);

	const std::string prefix = "casioemu://";
	if (dropped.compare(0, prefix.size(), prefix) != 0)
		return {};
	auto qpos = dropped.find("model=");
	if (qpos == std::string::npos)
		return {};
	qpos += 6; // strlen("model=")
	auto ampersand = dropped.find('&', qpos);
	std::string raw = (ampersand == std::string::npos) ? dropped.substr(qpos) : dropped.substr(qpos, ampersand - qpos);
	std::string modelId = ShortcutLaunch_UrlPercentDecode(raw);
	if (modelId.empty())
		return {};

	std::filesystem::path candidate = std::filesystem::path("models") / modelId;
	std::error_code ec;
	if (std::filesystem::is_regular_file(candidate / "config.bin", ec))
		return candidate;

	std::cerr << "[Shortcut] Launch URL referenced a model that no longer exists: " << modelId << "\n";
	return {};
}

#else // !IOS -- no-op everywhere else, so call sites don't need #ifdef guards

inline std::filesystem::path ResolveShortcutLaunchEvent(const SDL_Event&) {
	return {};
}

#endif
