#include <TargetConditionals.h>
#if TARGET_OS_IPHONE
#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>
#import <AudioToolbox/AudioToolbox.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include <string>

// Include the header we just made
#include "IOSNativeBridge.h"

// Singleton to act as the UIDocumentPickerDelegate
// _isOpenMode tracks whether the last-presented picker was an Open (YES) or Export/Save (NO) picker,
// replacing the deprecated UIDocumentPickerMode / controller.documentPickerMode property removed in iOS 16+.
@interface iOSNativeBridge : NSObject <UIDocumentPickerDelegate, UIDocumentInteractionControllerDelegate>
+ (instancetype)sharedInstance;
@property (nonatomic, assign) BOOL isOpenMode;
// Must be strongly retained: UIDocumentInteractionController does not keep
// itself alive while its "Open In"/install menu is on screen.
@property (nonatomic, strong) UIDocumentInteractionController *webClipInteractionController;
@end

@implementation iOSNativeBridge

+ (instancetype)sharedInstance {
    static iOSNativeBridge *instance = nil;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        instance = [[iOSNativeBridge alloc] init];
    });
    return instance;
}

- (instancetype)init {
    self = [super init];
    if (self) {
        [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(applicationDidBecomeActive:) name:UIApplicationDidBecomeActiveNotification object:nil];
        [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(applicationWillResignActive:) name:UIApplicationWillResignActiveNotification object:nil];
        [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(applicationDidEnterBackground:) name:UIApplicationDidEnterBackgroundNotification object:nil];
        [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(applicationWillEnterForeground:) name:UIApplicationWillEnterForegroundNotification object:nil];
        [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(applicationWillTerminate:) name:UIApplicationWillTerminateNotification object:nil];
        onAppCreate();
    }
    return self;
}

- (void)dealloc {
    [[NSNotificationCenter defaultCenter] removeObserver:self];
}

#pragma mark - App Lifecycle Observers

- (void)applicationDidBecomeActive:(NSNotification *)notification {
    onAppResume();
}

- (void)applicationWillResignActive:(NSNotification *)notification {
    onAppPause();
}

- (void)applicationDidEnterBackground:(NSNotification *)notification {
    onAppBackground();
}

- (void)applicationWillEnterForeground:(NSNotification *)notification {
    onAppForeground();
}

- (void)applicationWillTerminate:(NSNotification *)notification {
    onAppTerminate();
}



// Get the root view controller to present dialogs
- (UIViewController*)rootViewController {
    UIWindowScene *scene = (UIWindowScene *)UIApplication.sharedApplication.connectedScenes.allObjects.firstObject;
    
    for (UIWindow *window in scene.windows) {
        if (window.isKeyWindow) {
            return window.rootViewController;
        }
    }
    
    return scene.windows.firstObject.rootViewController;
}

// UIDocumentInteractionControllerDelegate: needed so the "Install Profile"
// menu has a view controller to anchor its popover to on iPad.
- (UIViewController *)documentInteractionControllerViewControllerForPreview:(UIDocumentInteractionController *)controller {
    return [self rootViewController];
}

// Helper: lấy key window theo cách tương thích iOS 13+
static UIWindow* getKeyWindow() {
    if (@available(iOS 13.0, *)) {
        for (UIWindowScene *scene in UIApplication.sharedApplication.connectedScenes) {
            if (scene.activationState == UISceneActivationStateForegroundActive &&
                [scene isKindOfClass:[UIWindowScene class]]) {
                for (UIWindow *window in ((UIWindowScene *)scene).windows) {
                    if (window.isKeyWindow) return window;
                }
            }
        }
    }
    // Fallback iOS 12 trở xuống
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    return UIApplication.sharedApplication.keyWindow;
#pragma clang diagnostic pop
}

#pragma mark - Safe Area Insets

float getSafeTop() {
    if (@available(iOS 11.0, *)) {
        UIWindow *window = getKeyWindow();
        if (window) return window.safeAreaInsets.top;
    }
    return 20.0f; // status bar fallback
}

float getSafeBottom() {
    if (@available(iOS 11.0, *)) {
        UIWindow *window = getKeyWindow();
        if (window) return window.safeAreaInsets.bottom;
    }
    return 0.0f;
}

float getSafeLeft() {
    if (@available(iOS 11.0, *)) {
        UIWindow *window = getKeyWindow();
        if (window) return window.safeAreaInsets.left;
    }
    return 0.0f;
}

float getSafeRight() {
    if (@available(iOS 11.0, *)) {
        UIWindow *window = getKeyWindow();
        if (window) return window.safeAreaInsets.right;
    }
    return 0.0f;
}

#pragma mark - System Dialogs (File & Folder Pickers)

- (void)openFileDialog {
    dispatch_async(dispatch_get_main_queue(), ^{
        // kUTTypeItem was deprecated in iOS 14 and removed in iOS 16+.
        // Use UTTypeItem from UniformTypeIdentifiers (already imported above).
        self.isOpenMode = YES; // Track intent: open
        UIDocumentPickerViewController *picker;
        picker = [[UIDocumentPickerViewController alloc]
            initForOpeningContentTypes:@[UTTypeItem]];
        picker.delegate = self;
        picker.allowsMultipleSelection = NO;
        [[self rootViewController] presentViewController:picker animated:YES completion:nil];
    });
}

// filePath must already point to a real, already-written file (see the
// contract documented in IOSNativeBridge.h) -- this presents the system
// export/share picker so the user can choose where that file actually goes.
- (void)saveFileDialog:(NSString*)filePath {
    dispatch_async(dispatch_get_main_queue(), ^{
        if (filePath.length == 0 || ![[NSFileManager defaultManager] fileExistsAtPath:filePath]) {
            NSLog(@"[iOSNativeBridge] saveFileDialog: no such file to export: %@", filePath);
            onExportFailed();
            return;
        }
        self.isOpenMode = NO; // Track intent: save/export
        NSURL *fileURL = [NSURL fileURLWithPath:filePath];
        UIDocumentPickerViewController *picker;
        picker = [[UIDocumentPickerViewController alloc]
            initForExportingURLs:@[fileURL] asCopy:YES];
        picker.delegate = self;
        [[self rootViewController] presentViewController:picker animated:YES completion:nil];
    });
}

// .mobileconfig needs iOS to recognize it as an installable profile and
// offer "Install Profile" -- exporting it as a plain file copy via
// UIDocumentPickerViewController (like saveFileDialog above) wouldn't
// trigger that; it would just save an inert copy with no install prompt.
// UIDocumentInteractionController's presentOpenInMenuFromRect: is what
// actually surfaces "Install Profile" once iOS reads the file's UTI.
// Strongly retained as an ivar (not a local/property) because
// UIDocumentInteractionController does NOT retain itself while its menu is
// showing, and the delegate reference alone isn't enough -- if this were a
// stack-local or weak reference it could be deallocated mid-presentation.
- (void)presentWebClipInstall:(NSString*)filePath {
    dispatch_async(dispatch_get_main_queue(), ^{
        if (filePath.length == 0 || ![[NSFileManager defaultManager] fileExistsAtPath:filePath]) {
            NSLog(@"[iOSNativeBridge] presentWebClipInstall: no such file: %@", filePath);
            return;
        }
        NSURL *fileURL = [NSURL fileURLWithPath:filePath];
        self.webClipInteractionController = [UIDocumentInteractionController interactionControllerWithURL:fileURL];
        self.webClipInteractionController.delegate = self;
        UIViewController *root = [self rootViewController];
        CGRect anchor = CGRectMake(root.view.bounds.size.width / 2.0, root.view.bounds.size.height / 2.0, 1.0, 1.0);
        BOOL opened = [self.webClipInteractionController presentOpenInMenuFromRect:anchor
                                                                              inView:root.view
                                                                            animated:YES];
        if (!opened) {
            NSLog(@"[iOSNativeBridge] presentWebClipInstall: presentOpenInMenuFromRect returned NO (no app -- including Settings' own handler -- registered to open .mobileconfig).");
        }
    });
}

- (void)openFolderDialog {
    dispatch_async(dispatch_get_main_queue(), ^{
        // kUTTypeFolder was deprecated in iOS 14 and removed in iOS 16+.
        self.isOpenMode = YES; // Track intent: open
        UIDocumentPickerViewController *picker;
        picker = [[UIDocumentPickerViewController alloc]
            initForOpeningContentTypes:@[UTTypeFolder]];
        picker.delegate = self;
        [[self rootViewController] presentViewController:picker animated:YES completion:nil];
    });
}

- (void)saveFolderDialog {
    // iOS doesn't distinguish between open/save folder, just open a folder picker
    self.isOpenMode = NO; // Track intent: save
    [self openFolderDialog];
}

#pragma mark - UIDocumentPickerDelegate

- (void)documentPicker:(UIDocumentPickerViewController *)controller didPickDocumentsAtURLs:(NSArray<NSURL *> *)urls {
    if (urls.count == 0) return;

    NSURL *url = urls.firstObject;
    // UIDocumentPickerMode / controller.documentPickerMode was deprecated in iOS 13 and removed in iOS 16+.
    // Use our own isOpenMode flag that is set before presenting each picker.
    BOOL openMode = self.isOpenMode;

    // Defer the actual processing (and any resulting error alert) to the
    // next run loop turn. Doing this synchronously here races with the
    // document picker's own dismissal animation, which can leave a
    // subsequently-presented alert's buttons unresponsive.
    dispatch_async(dispatch_get_main_queue(), ^{
        [url startAccessingSecurityScopedResource]; // Required for iOS file access

        NSString *path = url.path;

        // Check if it's a directory (Folder)
        NSError *error = nil;
        NSDictionary *attrs = [[NSFileManager defaultManager] attributesOfItemAtPath:path error:&error];
        BOOL isDir = (attrs.fileType == NSFileTypeDirectory);

        if (isDir) {
            if (openMode) {
                onFolderSelected(path.UTF8String);
            } else {
                onFolderSaved(path.UTF8String);
            }
        } else {
            // It's a file, read the data
            NSData *fileData = [NSData dataWithContentsOfURL:url options:0 error:&error];
            if (fileData && !error) {
                if (openMode) {
                    onFileSelected(path.UTF8String, (const unsigned char*)fileData.bytes, (int)fileData.length);
                } else {
                    onFileSaved(path.UTF8String);
                }
            } else {
                onImportFailed();
            }
        }

        [url stopAccessingSecurityScopedResource];
    });
}

- (void)documentPickerWasCancelled:(UIDocumentPickerViewController *)controller {
    // User cancelled the dialog
    onImportFailed();
}

@end

#pragma mark - C++ Bridge Functions

void nativeVibrate(long milliseconds) {
    dispatch_async(dispatch_get_main_queue(), ^{
        // iOS doesn't support exact ms vibration like Android.
        // We map the duration to Human Interface Guidelines Haptics
        if (milliseconds < 100) {
            UIImpactFeedbackGenerator *generator = [[UIImpactFeedbackGenerator alloc] initWithStyle:UIImpactFeedbackStyleLight];
            [generator impactOccurred];
        } else if (milliseconds < 300) {
            UIImpactFeedbackGenerator *generator = [[UIImpactFeedbackGenerator alloc] initWithStyle:UIImpactFeedbackStyleMedium];
            [generator impactOccurred];
        } else {
            // Long vibrations usually indicate errors/notifications
            UINotificationFeedbackGenerator *generator = [[UINotificationFeedbackGenerator alloc] init];
            [generator notificationOccurred:UINotificationFeedbackTypeError];
        }
    });
}

void onNativeCrash(const char* message) {
    NSString *msg = [NSString stringWithUTF8String:message];
    
    dispatch_async(dispatch_get_main_queue(), ^{
        UIAlertController *alert = [UIAlertController alertControllerWithTitle:@"Crash Detected"
                                                                       message:msg
                                                                preferredStyle:UIAlertControllerStyleAlert];
        
        UIAlertAction *copyAction = [UIAlertAction actionWithTitle:@"Copy" style:UIAlertActionStyleDefault handler:^(UIAlertAction * action) {
            // Copy to clipboard
            [UIPasteboard generalPasteboard].string = msg;
            
            // Exit cleanly
            exit(0);
        }];
        
        UIAlertAction *closeAction = [UIAlertAction actionWithTitle:@"Close" style:UIAlertActionStyleCancel handler:^(UIAlertAction * action) {
            exit(0);
        }];
        
        [alert addAction:copyAction];
        [alert addAction:closeAction];
        
        [[iOSNativeBridge sharedInstance].rootViewController presentViewController:alert animated:YES completion:nil];
    });
}

void openFileDialog() {
    [[iOSNativeBridge sharedInstance] openFileDialog];
}

void saveFileDialog(const char* filePath) {
    NSString *path = filePath ? [NSString stringWithUTF8String:filePath] : @"";
    [[iOSNativeBridge sharedInstance] saveFileDialog:path];
}

void openFolderDialog() {
    [[iOSNativeBridge sharedInstance] openFolderDialog];
}

void saveFolderDialog() {
    [[iOSNativeBridge sharedInstance] saveFolderDialog];
}

#pragma mark - Home Screen Shortcut Creation (Quick Actions)
//
// A truly separate Home Screen *icon* on iOS can only be faked via a WebClip
// configuration profile (what LiveContainer does, and what an earlier
// version of this function did too) -- which needs a working local HTTP
// server, an ATS exception, Safari, and the user manually walking through
// Settings' "Install Profile" flow. That's a lot of independently-failing
// pieces, and in practice it turned out unreliable (e.g. an active VPN can
// interfere with local loopback networking or "localhost" resolution).
//
// This uses Home Screen Quick Actions instead
// (UIApplicationShortcutItem / UIApplication.shortcutItems):
// long-pressing the app's own icon shows up to 4 of these as a menu, and
// tapping one launches straight into the model it names. It's a different
// gesture than a separate icon (long-press menu vs. a whole new icon on the
// Home Screen), but the outcome -- "jump straight into this model" -- is
// the same, and the mechanism is a fully native, synchronous, offline API
// that either succeeds immediately or reports a real error, with no network
// request or user-facing install flow involved anywhere.
//
// See CasioEmuAppDelegate.mm for the launch side (reading the tapped
// shortcut back out via UIApplicationLaunchOptionsShortcutItemKey /
// performActionForShortcutItem:) and Ext/ShortcutLaunch.h for how that
// then reaches whichever loop needs to act on it.

static NSString *const kCasioEmuShortcutType = @"com.pkdevvn.casioemu.shortcut";
static const NSUInteger kMaxShortcutItems = 4; // iOS shows at most 4 Quick Actions total, combined across static + dynamic

bool presentCreateHomeScreenShortcut(const char* modelIdentifier, const char* shortcutName, const char* iconPathOrNull) {
    if (!modelIdentifier || modelIdentifier[0] == '\0') {
        NSLog(@"[Shortcut] Missing model identifier.");
        return false;
    }

    // stringWithUTF8String: returns nil if the C string isn't valid UTF-8.
    // modelIdentifier comes from a sanitized on-disk folder name so this
    // shouldn't happen, but shortcutName is whatever the user just typed
    // into the ImGui text field (unsanitized, and truncated to 260 bytes by
    // that field's buffer) -- guard both rather than risk passing nil into
    // a UIKit initializer that doesn't expect it.
    NSString *modelId = [NSString stringWithUTF8String:modelIdentifier];
    if (!modelId) {
        NSLog(@"[Shortcut] modelIdentifier was not valid UTF-8.");
        return false;
    }
    NSString *label = modelId;
    if (shortcutName && shortcutName[0] != '\0') {
        NSString *typed = [NSString stringWithUTF8String:shortcutName];
        if (typed) {
            label = typed;
        }
        else {
            NSLog(@"[Shortcut] shortcutName was not valid UTF-8 (likely truncated mid-character); falling back to the model name.");
        }
    }
    (void)iconPathOrNull; // Quick Actions use a fixed icon; there's no per-shortcut custom-icon UI anymore.

    // UIApplication.shortcutItems must only be touched from the main
    // thread. This function is, in fact, *always* called from the main
    // thread already: SDL's iOS backend runs the app's entire C++ main()
    // (and therefore sui_loop() and everything it calls, including this)
    // directly and synchronously on the main thread -- see
    // SDL_uikitappdelegate.m's postFinishLaunch, which calls forward_main()
    // right there rather than spawning a separate thread. So this must NOT
    // use dispatch_sync(dispatch_get_main_queue(), ...): dispatch_sync onto
    // the main queue while already running on the main thread is a
    // guaranteed deadlock (the calling thread blocks waiting for the main
    // queue to run the block, but only the main thread -- which is the one
    // blocked -- can ever run it), which iOS eventually kills as an
    // unresponsive app. The block below runs directly/synchronously in the
    // (confirmed, normal) case where we're already on the main thread, and
    // falls back to a non-deadlocking dispatch_async only as a defensive
    // safety net in case that ever changes.
    //
    // Everything UIKit-facing is also wrapped in @try/@catch: if any of
    // these calls throws an NSException for a reason we haven't
    // anticipated, this converts that into a logged failure instead of a
    // hard crash. If shortcut creation is still failing after this change,
    // the exact reason will now be in the device console/Xcode log instead
    // of just "the app crashed".
    __block BOOL succeeded = NO;
    void (^addShortcut)(void) = ^{
        @try {
            UIMutableApplicationShortcutItem *item = [[UIMutableApplicationShortcutItem alloc]
                initWithType:kCasioEmuShortcutType
              localizedTitle:label];
            item.localizedSubtitle = @"CasioEmuMsvc";
            item.icon = [UIApplicationShortcutIcon iconWithSystemImageName:@"calculator"];
            item.userInfo = @{@"model": modelId};

            NSMutableArray<UIApplicationShortcutItem *> *items =
                [UIApplication.sharedApplication.shortcutItems mutableCopy];
            if (!items) {
                items = [NSMutableArray array];
            }

            // Replace any existing shortcut for this same model instead of
            // piling up duplicates every time the user re-creates it.
            NSPredicate *notSameModel = [NSPredicate predicateWithBlock:^BOOL(UIApplicationShortcutItem *existing, NSDictionary *bindings) {
                return ![existing.userInfo[@"model"] isEqual:modelId];
            }];
            [items filterUsingPredicate:notSameModel];

            [items addObject:item];

            // iOS only ever shows the first 4 Quick Actions; keep the array
            // at that size ourselves (dropping the oldest) so what we hand
            // back always matches what's actually shown to the user.
            while (items.count > kMaxShortcutItems) {
                [items removeObjectAtIndex:0];
            }

            UIApplication.sharedApplication.shortcutItems = items;
            succeeded = YES;
        }
        @catch (NSException *exception) {
            NSLog(@"[Shortcut] Exception while creating the shortcut: %@ -- %@\n%@",
                exception.name, exception.reason, exception.callStackSymbols);
            succeeded = NO;
        }
    };

    if ([NSThread isMainThread]) {
        addShortcut();
    }
    else {
        NSLog(@"[Shortcut] presentCreateHomeScreenShortcut called off the main thread unexpectedly; dispatching asynchronously.");
        dispatch_async(dispatch_get_main_queue(), addShortcut);
        succeeded = YES; // optimistic: we can't wait for the async result without risking the same deadlock class of bug
    }

    return succeeded == YES;
}

#pragma mark - Home Screen Shortcut Creation (Web Clip / separate icon)
//
// presentCreateHomeScreenShortcut above uses Quick Actions, which need a
// long-press on the app's own existing icon. This produces something
// different: a genuinely separate icon that sits directly on the Home
// Screen. iOS has no public API for a sideloaded app to place that icon
// itself -- a Web Clip Configuration Profile (.mobileconfig), installed
// once through Settings, is the only supported mechanism. The profile's
// URL points at casioemu://launch?model=<id>, the exact scheme
// ShortcutLaunch.h's ResolveShortcutLaunchEvent() already decodes, so no
// changes were needed on the launch-handling side.

// RFC 3986 unreserved characters are left alone; everything else
// (including '&', '=', '?', which would otherwise break the query string
// this gets embedded in) is percent-encoded. NSString's built-in
// URL-encoding methods were deprecated/removed across iOS versions for
// this exact use case, so this is done by hand rather than depending on
// whichever one happens to still exist on a given OS version.
static NSString *WebClipPercentEncode(NSString *raw) {
    NSMutableCharacterSet *allowed = [[NSCharacterSet alphanumericCharacterSet] mutableCopy];
    [allowed addCharactersInString:@"-._~"];
    NSString *encoded = [raw stringByAddingPercentEncodingWithAllowedCharacters:allowed];
    return encoded ?: @"";
}

bool presentCreateHomeScreenWebClip(const char* modelIdentifier, const char* shortcutName) {
    if (!modelIdentifier || modelIdentifier[0] == '\0') {
        NSLog(@"[WebClip] Missing model identifier.");
        return false;
    }

    NSString *modelId = [NSString stringWithUTF8String:modelIdentifier];
    if (!modelId) {
        NSLog(@"[WebClip] modelIdentifier was not valid UTF-8.");
        return false;
    }
    NSString *label = modelId;
    if (shortcutName && shortcutName[0] != '\0') {
        NSString *typed = [NSString stringWithUTF8String:shortcutName];
        if (typed) {
            label = typed;
        }
        else {
            NSLog(@"[WebClip] shortcutName was not valid UTF-8 (likely truncated mid-character); falling back to the model name.");
        }
    }

    NSString *targetURL = [NSString stringWithFormat:@"casioemu://launch?model=%@", WebClipPercentEncode(modelId)];

    // Two distinct UUIDs are required: PayloadUUID must be unique per
    // payload dictionary (the outer profile AND the inner Web Clip payload
    // each need their own), and PayloadIdentifier should stay stable across
    // re-installs of the *same* shortcut so iOS treats re-creating it as an
    // update rather than piling up duplicate profiles. Deriving the
    // identifier from the model id (rather than a fresh UUID each time)
    // gets that for free.
    NSString *profileUUID = [[NSUUID UUID] UUIDString];
    NSString *clipUUID = [[NSUUID UUID] UUIDString];
    NSString *safeModelIdForIdentifier = [[modelId componentsSeparatedByCharactersInSet:
        [[NSCharacterSet alphanumericCharacterSet] invertedSet]] componentsJoinedByString:@"-"];
    NSString *profileIdentifier = [NSString stringWithFormat:@"com.pkdevvn.casioemu.webclip.%@", safeModelIdForIdentifier];

    // Label/Full Screen keys: Full Screen=false so the Web Clip target URL
    // is handled as a normal URL open (routing straight to casioemu://,
    // which iOS resolves via CFBundleURLTypes in Info.plist) rather than
    // being rendered inside a stripped-down in-app Safari chrome, which is
    // Full Screen=true's behavior and pointless for a non-http(s) scheme.
    NSString *plist = [NSString stringWithFormat:
        @"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
        "<plist version=\"1.0\">\n"
        "<dict>\n"
        "  <key>PayloadContent</key>\n"
        "  <array>\n"
        "    <dict>\n"
        "      <key>PayloadType</key>\n"
        "      <string>com.apple.webClip.managed</string>\n"
        "      <key>PayloadVersion</key>\n"
        "      <integer>1</integer>\n"
        "      <key>PayloadIdentifier</key>\n"
        "      <string>%@.clip</string>\n"
        "      <key>PayloadUUID</key>\n"
        "      <string>%@</string>\n"
        "      <key>PayloadDisplayName</key>\n"
        "      <string>%@</string>\n"
        "      <key>Label</key>\n"
        "      <string>%@</string>\n"
        "      <key>URL</key>\n"
        "      <string>%@</string>\n"
        "      <key>FullScreen</key>\n"
        "      <false/>\n"
        "      <key>Precomposed</key>\n"
        "      <true/>\n"
        "      <key>RemovalDisallowed</key>\n"
        "      <false/>\n"
        "      <key>IsRemovable</key>\n"
        "      <true/>\n"
        "    </dict>\n"
        "  </array>\n"
        "  <key>PayloadDisplayName</key>\n"
        "  <string>%@ Shortcut</string>\n"
        "  <key>PayloadDescription</key>\n"
        "  <string>Adds a Home Screen icon that jumps straight to \"%@\" in CasioEmuMsvc.</string>\n"
        "  <key>PayloadIdentifier</key>\n"
        "  <string>%@</string>\n"
        "  <key>PayloadType</key>\n"
        "  <string>Configuration</string>\n"
        "  <key>PayloadUUID</key>\n"
        "  <string>%@</string>\n"
        "  <key>PayloadVersion</key>\n"
        "  <integer>1</integer>\n"
        "  <key>PayloadRemovalDisallowed</key>\n"
        "  <false/>\n"
        "</dict>\n"
        "</plist>\n",
        profileIdentifier, clipUUID, label, label, targetURL,
        label, label, profileIdentifier, profileUUID];

    NSData *plistData = [plist dataUsingEncoding:NSUTF8StringEncoding];
    if (!plistData) {
        NSLog(@"[WebClip] Failed to encode the profile as UTF-8.");
        return false;
    }

    // File extension matters here, not just content: iOS identifies
    // .mobileconfig by extension/UTI to know it should offer "Install
    // Profile" at all.
    NSString *tempDir = NSTemporaryDirectory();
    NSString *fileName = [NSString stringWithFormat:@"casioemu-%@.mobileconfig", safeModelIdForIdentifier];
    NSString *filePath = [tempDir stringByAppendingPathComponent:fileName];

    NSError *writeError = nil;
    BOOL wrote = [plistData writeToFile:filePath options:NSDataWritingAtomic error:&writeError];
    if (!wrote) {
        NSLog(@"[WebClip] Failed to write .mobileconfig to disk: %@", writeError);
        return false;
    }

    [[iOSNativeBridge sharedInstance] presentWebClipInstall:filePath];
    return true;
}
#endif
