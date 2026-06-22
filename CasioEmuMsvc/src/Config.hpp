#pragma once

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
    #define MACOS
#endif
#endif

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

// #define SINGLE_WINDOW
#if !defined(SINGLE_WINDOW) && (defined(__ANDROID__) || defined(IOS))
#define SINGLE_WINDOW
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

#include "git_info.h"

#define EMULATOR_VERSION GIT_COMMIT_HASH

#ifndef DISABLE_SENTRY
#define DISABLE_SENTRY
#endif

#if !defined(__ANDROID__) && !defined(__EMSCRIPTEN__) && !defined(DISABLE_SENTRY)
#define ENABLE_SENTRY
#define SENTRY_BUILD_STATIC 1
#endif

#define DISCORD_APP_ID "1494244788055179344"
