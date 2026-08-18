#include <TargetConditionals.h>
#if TARGET_OS_IPHONE
#import <UIKit/UIKit.h>

#include "ShortcutLaunch.h"

// SDL's iOS app delegate is documented as subclassable specifically for
// cases like this (see SDL/src/video/uikit/SDL_uikitappdelegate.m, comment
// on +getAppDelegateClassName: "when you subclass this appdelegate, make
// sure to add a category to override this method and return the actual
// name of the delegate"). Its real header lives under SDL's internal source
// tree (src/video/uikit/SDL_uikitappdelegate.h) rather than somewhere this
// target necessarily has on its include path, so we re-declare just enough
// of its interface here to subclass it -- the actual @implementation used
// at runtime is still SDL's own, compiled from SDL_uikitappdelegate.m.
@interface SDLUIKitDelegate : NSObject <UIApplicationDelegate>
+ (id)sharedAppDelegate;
+ (NSString *)getAppDelegateClassName;
- (void)hideLaunchScreen;
@property(nonatomic) UIWindow *window;
@end

@interface CasioEmuAppDelegate : SDLUIKitDelegate
@end

@implementation CasioEmuAppDelegate

+ (void)handleShortcutItem:(UIApplicationShortcutItem *)shortcutItem {
	NSString *modelId = shortcutItem.userInfo[@"model"];
	if (modelId.length == 0)
		return;

	auto resolved = ResolveShortcutModelId(std::string(modelId.UTF8String));
	if (resolved.empty())
		return;

	SetPendingShortcutModel(resolved.string());
	PushShortcutWakeEvent();
}

// Cold start (app was not running): iOS hands the tapped shortcut item to us
// here rather than through performActionForShortcutItem: below.
- (BOOL)application:(UIApplication *)application didFinishLaunchingWithOptions:(NSDictionary *)launchOptions {
	BOOL result = [super application:application didFinishLaunchingWithOptions:launchOptions];

	UIApplicationShortcutItem *shortcutItem = launchOptions[UIApplicationLaunchOptionsShortcutItemKey];
	if (shortcutItem) {
		[CasioEmuAppDelegate handleShortcutItem:shortcutItem];
	}

	return result;
}

// Warm launch (app was already running, foreground or suspended in the
// background): iOS calls this directly instead.
- (void)application:(UIApplication *)application performActionForShortcutItem:(UIApplicationShortcutItem *)shortcutItem completionHandler:(void (^)(BOOL))completionHandler {
	[CasioEmuAppDelegate handleShortcutItem:shortcutItem];
	completionHandler(YES);
}

@end

// The mechanism SDL documents for actually installing this subclass: a
// category on SDLUIKitDelegate itself that overrides which class name
// +getAppDelegateClassName (used by SDL_UIKitRunApp when it calls
// UIApplicationMain) reports.
@interface SDLUIKitDelegate (CasioEmuOverride)
@end

@implementation SDLUIKitDelegate (CasioEmuOverride)
+ (NSString *)getAppDelegateClassName {
	return @"CasioEmuAppDelegate";
}
@end

#endif
