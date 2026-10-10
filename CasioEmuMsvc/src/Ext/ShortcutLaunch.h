#pragma once
//
// Shared state/logic for Home Screen shortcuts (Quick Actions -- see
// IOSNativeBridge.mm's presentCreateHomeScreenShortcut, which adds entries
// to UIApplication.shared.shortcutItems) calling back into this app to
// launch a specific model.
//
// The app can be in any of three situations when a shortcut is tapped, and
// each hands the requested model off differently:
//   - Not running at all (cold start): CasioEmuAppDelegate.mm's
//     application:didFinishLaunchingWithOptions: reads
//     UIApplicationLaunchOptionsShortcutItemKey and calls
//     SetPendingShortcutModel() *before* casioemu.cpp's C++ main() has even
//     started -- main() checks TakePendingShortcutModel() right at the top,
//     before deciding whether to show the StartupUi menu at all.
//   - Sitting at the StartupUi menu (already running, no model loaded yet):
//     StartupUi.cpp's sui_loop() checks TakePendingShortcutModel() each time
//     its event loop wakes up.
//   - Already running a *different* model: casioemu.cpp's active
//     `while (emulator.Running())` loop also checks
//     TakePendingShortcutModel() each time it wakes up; if a switch was
//     requested it calls emulator.Shutdown() (the same mechanism a normal
//     quit uses) and main()'s outer loop notices the pending model right
//     after the loop ends and loops back with it instead of exiting.
//
// SetPendingShortcutModel() can be called from UIKit's main thread (Quick
// Action taps arrive via application:performActionForShortcutItem:, which
// iOS always calls on the main thread) while SDL's own event loop runs on a
// *different* thread -- so the pending-model string is mutex-guarded, and
// PushShortcutWakeEvent() wakes up a blocked SDL_WaitEvent() via a harmless
// synthetic SDL user event (SDL_PushEvent() is documented as safe to call
// from any thread).
//
// Also kept here for backward compatibility: ResolveShortcutLaunchEvent(),
// which recognises the older casioemu://launch?model=<id> URL scheme (still
// registered in Info.plist) in case a shortcut created by an earlier build
// of this app is still installed on someone's device.
//
// Only wired up on iOS: this is the only platform where the app is a single
// long-lived process that can receive a second, different launch request
// while still running an earlier one. Desktop shortcuts relaunch the
// executable as a brand new process, and Android shortcuts start a new
// Activity/Intent, so neither needs any of this.

#include <SDL.h>
#include <cctype>
#include <cerrno>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <string>

#ifdef __IOS__

// Resolves a model identifier (a folder name under "models/", as handed to
// us via a shortcut's userInfo or the legacy URL scheme) to an on-disk model
// path. Returns an empty path if the model no longer exists.
inline std::filesystem::path ResolveShortcutModelId(const std::string& modelId) {
	if (modelId.empty())
		return {};
	std::filesystem::path candidate = std::filesystem::path("models") / modelId;
	std::error_code ec;
	if (std::filesystem::is_regular_file(candidate / "config.bin", ec))
		return candidate;
	std::cerr << "[Shortcut] Requested a model that no longer exists: " << modelId << "\n";
	return {};
}

// --- Pending-model channel (thread-safe) -----------------------------------

namespace ShortcutLaunchDetail {
inline std::mutex g_mutex;
inline std::string g_pending_model; // guarded by g_mutex
} // namespace ShortcutLaunchDetail

// Records that `model` (an on-disk model path, as returned by
// ResolveShortcutModelId()) should be launched as soon as a loop notices.
// Safe to call from any thread.
inline void SetPendingShortcutModel(std::string model) {
	std::lock_guard<std::mutex> lock(ShortcutLaunchDetail::g_mutex);
	ShortcutLaunchDetail::g_pending_model = std::move(model);
}

// Returns and clears the pending model, or an empty string if none is
// pending. Safe to call from any thread.
inline std::string TakePendingShortcutModel() {
	std::lock_guard<std::mutex> lock(ShortcutLaunchDetail::g_mutex);
	std::string result = std::move(ShortcutLaunchDetail::g_pending_model);
	ShortcutLaunchDetail::g_pending_model.clear();
	return result;
}

// --- Waking a blocked SDL_WaitEvent() from another thread ------------------

namespace ShortcutLaunchDetail {
inline Uint32 g_wake_event_type = static_cast<Uint32>(-1);
}

// Must be called once from the SDL/main-loop thread, after SDL_Init(), before
// PushShortcutWakeEvent() can do anything useful. Safe to call more than
// once (only the first call has any effect).
inline void InitShortcutWakeEvent() {
	if (ShortcutLaunchDetail::g_wake_event_type == static_cast<Uint32>(-1)) {
		ShortcutLaunchDetail::g_wake_event_type = SDL_RegisterEvents(1);
	}
}

// Pushes a no-op SDL event purely to wake up a blocked SDL_WaitEvent() call
// after SetPendingShortcutModel() was just called from a non-SDL thread
// (namely UIKit's main thread, from a Quick Action tap). If
// InitShortcutWakeEvent() hasn't run yet this is a harmless no-op -- the
// pending model is still safely stored and will be picked up whenever the
// loop next wakes up for any other reason.
inline void PushShortcutWakeEvent() {
	if (ShortcutLaunchDetail::g_wake_event_type == static_cast<Uint32>(-1))
		return;
	SDL_Event event{};
	event.type = ShortcutLaunchDetail::g_wake_event_type;
	SDL_PushEvent(&event);
}

// --- Legacy casioemu://launch?model=<id> URL scheme (backward compat) ------

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

// Resolves an SDL event to an on-disk model path if it is a legacy Home
// Screen shortcut calling back via the casioemu://launch?model=<id> URL
// scheme; returns an empty path otherwise (not a DROPFILE event, doesn't
// match our URL scheme, or the referenced model no longer exists). Consumes
// (and SDL_free()s) event.drop.file if present, per SDL's ownership
// contract for SDL_DROPFILE events.
// Case-insensitive prefix match against "casioemu://". iOS/Safari/older
// builds don't guarantee the scheme is handed back in the exact casing it
// was registered with (URI scheme comparison is case-insensitive per RFC
// 3986 ยง3.1 -- only the host/path may be case-sensitive) so a strict
// std::string::compare() here silently drops otherwise-valid legacy
// shortcuts: the app launches to the normal menu instead of the requested
// model, with nothing in the console to explain why.
inline bool ShortcutLaunch_MatchesSchemeCI(const std::string& s, const std::string& prefix) {
	if (s.size() < prefix.size())
		return false;
	for (size_t i = 0; i < prefix.size(); ++i) {
		if (tolower((unsigned char)s[i]) != tolower((unsigned char)prefix[i]))
			return false;
	}
	return true;
}

// Resolves an SDL event to an on-disk model path if it is a legacy Home
// Screen shortcut calling back via the casioemu://launch?model=<id> URL
// scheme; returns an empty path otherwise (not a DROPFILE event, doesn't
// match our URL scheme, or the referenced model no longer exists). Consumes
// (and SDL_free()s) event.drop.file if present, per SDL's ownership
// contract for SDL_DROPFILE events.
inline std::filesystem::path ResolveShortcutLaunchEvent(const SDL_Event& event) {
	if (event.type != SDL_DROPFILE || !event.drop.file) {
		if (event.type == SDL_DROPFILE)
			SDL_free(event.drop.file); // file is non-null but we're not consuming it below; still owned by us
		return {};
	}

	std::string dropped = event.drop.file;
	SDL_free(event.drop.file);

	const std::string prefix = "casioemu://";
	if (!ShortcutLaunch_MatchesSchemeCI(dropped, prefix)) {
		std::cerr << "[Shortcut] Dropped URL did not match casioemu:// scheme: " << dropped << "\n";
		return {};
	}

	// "model=" itself is a fixed query-key name, not the scheme, so it stays
	// case-sensitive on purpose -- only the scheme portion is normalized.
	auto qpos = dropped.find("model=");
	if (qpos == std::string::npos) {
		std::cerr << "[Shortcut] casioemu:// URL had no model= parameter: " << dropped << "\n";
		return {};
	}
	qpos += 6; // strlen("model=")
	auto ampersand = dropped.find('&', qpos);
	std::string raw = (ampersand == std::string::npos) ? dropped.substr(qpos) : dropped.substr(qpos, ampersand - qpos);
	std::string decoded = ShortcutLaunch_UrlPercentDecode(raw);
	if (decoded.empty()) {
		std::cerr << "[Shortcut] casioemu:// URL had an empty model id: " << dropped << "\n";
		return {};
	}
	return ResolveShortcutModelId(decoded);
}

#else // !__IOS__ -- no-ops everywhere else, so call sites don't need #ifdef guards

inline void SetPendingShortcutModel(std::string) {}
inline std::string TakePendingShortcutModel() { return {}; }
inline void InitShortcutWakeEvent() {}
inline void PushShortcutWakeEvent() {}
inline std::filesystem::path ResolveShortcutLaunchEvent(const SDL_Event&) { return {}; }

#endif
