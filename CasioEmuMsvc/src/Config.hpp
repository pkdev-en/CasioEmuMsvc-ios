#pragma once

// ── iOS / macOS platform detection (phải đứng đầu tiên) ──────────────────────
#if defined(__APPLE__)
#include <TargetConditionals.h>
#if TARGET_OS_IPHONE
    #ifndef IOS
    #define IOS
    #endif
    #ifndef __IOS__
    #define __IOS__
    #endif
#elif TARGET_OS_MAC
    #ifndef MACOS
    #define MACOS
    #endif
#endif
#endif
// ─────────────────────────────────────────────────────────────────────────────

#include "Containers/ConcurrentObject.h"
#include <cstdint>
#include <cstdio>
#include <exception>
#include <iostream>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#ifdef __GNUG__
#define FUNCTION_NAME __PRETTY_FUNCTION__
#else
#define FUNCTION_NAME __func__
#endif

#ifndef __debugbreak
    #if defined(_MSC_VER)
    #elif defined(__GNUC__) || defined(__clang__)
        #define __debugbreak __builtin_trap
    #else
        #define __debugbreak() ((void)0)
    #endif
#endif

#define NOOP() ((void)0)

#define ENABLE_CRASH_CHECK
#ifdef ENABLE_CRASH_CHECK
#ifndef PANIC
#define PANIC(...)           \
	{                        \
		printf(__VA_ARGS__); \
		__debugbreak();      \
	}
#endif
#else
#ifndef PANIC
#define PANIC(...) 0;
#endif
#endif

#define LOCK(x) \
	std::lock_guard<std::mutex> lock_##x{x};

// Enable debug feature
#define DBG

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

// ── SINGLE_WINDOW guard ───────────────────────────────────────────────────
// SINGLE_WINDOW drives the compact single-viewport overlay toolbar built for
// touch/small-screen targets (iOS, Android). It must never be hand-defined --
// the line below stays commented out on purpose. It is only ever turned on
// automatically, and only for __ANDROID__ or IOS.
//
// If it ever gets defined for a desktop target -- an accidental uncomment, a
// bad merge, a stray -DSINGLE_WINDOW passed to the compiler, a stale build
// cache from a mobile config -- every Windows/Linux/macOS build will silently
// ship the mobile overlay UI instead of the docked debugger panels. The
// static_assert below turns that mistake into a compile error the moment it
// happens, instead of a silent bug someone has to spot by eye in a running
// app.
// SINGLE_WINDOW: iOS và Android dùng cùng cửa sổ SDL với emulator
// #define SINGLE_WINDOW
#if !defined(SINGLE_WINDOW) && (defined(__ANDROID__) || defined(IOS))
#define SINGLE_WINDOW
#endif

#if defined(SINGLE_WINDOW) && !defined(__ANDROID__) && !defined(IOS)
static_assert(false,
	"SINGLE_WINDOW is defined on a non-mobile target. This macro must only "
	"ever be active for __ANDROID__ or IOS builds. Check for an accidental "
	"uncomment on the '#define SINGLE_WINDOW' line above, or check the "
	"compiler invocation / build cache for a stray -DSINGLE_WINDOW flag.");
#endif
// ─────────────────────────────────────────────────────────────────────────

// Disable Sentry trên iOS (không hỗ trợ)
#if defined(IOS)
#ifndef DISABLE_SENTRY
#define DISABLE_SENTRY
#endif
#endif

#if defined(_MSC_VER) || (defined(__clang__) && defined(_WIN32))
#define DLLEXPORT __declspec(dllexport)
#define DLLIMPORT __declspec(dllimport)
#elif defined(__clang__) || defined(__GNUC__)
#define DLLEXPORT __attribute__((visibility("default")))
#define DLLIMPORT
#else
#define DLLEXPORT
#define DLLIMPORT
#endif

#define PROP(x)                                  \
public:                                          \
	virtual decltype(x) Get##x##() { return x; } \
	virtual void Set##x##(decltype(x) a) { x = a; }

#define PROPABS(y, x)         \
public:                       \
	virtual y Get##x##() = 0; \
	virtual void Set##x##(y a) = 0;

#include <git_info.h>

#define EMULATOR_VERSION GIT_COMMIT_HASH

#if !defined(__ANDROID__) && !defined(__EMSCRIPTEN__) && !defined(DISABLE_SENTRY)
#define ENABLE_SENTRY
#define SENTRY_BUILD_STATIC 1
#endif

#define DISCORD_APP_ID "1494244788055179344"
