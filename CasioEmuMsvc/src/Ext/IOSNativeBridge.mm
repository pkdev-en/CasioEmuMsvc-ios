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
@interface iOSNativeBridge : NSObject <UIDocumentPickerDelegate>
+ (instancetype)sharedInstance;
@property (nonatomic, assign) BOOL isOpenMode;
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

    NSString *modelId = [NSString stringWithUTF8String:modelIdentifier];
    NSString *label = (shortcutName && shortcutName[0] != '\0')
        ? [NSString stringWithUTF8String:shortcutName]
        : modelId;
    (void)iconPathOrNull; // Quick Actions use a fixed icon; there's no per-shortcut custom-icon UI anymore.

    // UIApplication.shortcutItems must only be touched from the main thread,
    // but this function is called from the SDL/C++ thread. dispatch_sync
    // (rather than _async) is used deliberately so the return value below
    // accurately reflects whether the shortcut was actually added -- this
    // is fast, local, non-blocking-UI work, so waiting for it is cheap and
    // safe (never called from the main thread itself, so it can't deadlock).
    __block BOOL succeeded = NO;
    dispatch_sync(dispatch_get_main_queue(), ^{
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

        // iOS only ever shows the first 4 Quick Actions; keep the array at
        // that size ourselves (dropping the oldest) so what we hand back
        // always matches what's actually shown to the user.
        while (items.count > kMaxShortcutItems) {
            [items removeObjectAtIndex:0];
        }

        UIApplication.sharedApplication.shortcutItems = items;
        succeeded = YES;
    });

    return succeeded == YES;
}
#endif
