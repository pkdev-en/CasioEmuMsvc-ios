#include <TargetConditionals.h>
#if TARGET_OS_IPHONE
#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>
#import <AudioToolbox/AudioToolbox.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#import <SafariServices/SafariServices.h>

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

#pragma mark - Home Screen Shortcut (WebClip) Creation
//
// iOS has no public API to add an icon to the Home Screen directly. This
// follows the same process LiveContainer uses
// (https://github.com/LiveContainer/LiveContainer -- see
// LCAppInfo.m:generateWebClipConfigWithContainerId:iconStyle: and
// LCAppListView.swift:installMdm):
//
//   1. Build a WebClip configuration profile (PayloadType
//      com.apple.webClip.managed) whose URL points back into this app via
//      the private casioemu:// scheme, carrying the target model's folder
//      name as a query parameter.
//   2. Serialize the profile to plist XML, base64-encode it, and wrap it in
//      a "data:application/x-apple-aspen-config;base64,..." URL -- iOS
//      recognises that MIME type and routes it into the system
//      "Install Profile" flow.
//   3. Load that URL in an in-app SFSafariViewController, exactly like
//      LiveContainer's installMdm(data:).
//
// Once the user installs the profile, a Home Screen icon appears with the
// requested name/icon; tapping it relaunches this app via the casioemu://
// URL, which SDL's iOS backend forwards as an SDL_DROPFILE event carrying
// the full URL string (see SDL_uikitappdelegate.m: sendDropFileForURL:).
// StartupUi.cpp's HandlePotentialShortcutLaunch() parses that back into a
// direct model launch, the same way tapping a LiveContainer web clip drops
// you straight into the contained app instead of LiveContainer's own menu.

static NSString *PercentEncodeShortcutQueryValue(NSString *value) {
    NSMutableCharacterSet *allowed = [[NSCharacterSet URLQueryAllowedCharacterSet] mutableCopy];
    // Also escape '&' and '=' so the value can never be mistaken for a
    // second query key/value pair when the C++ side re-parses the URL.
    [allowed removeCharactersInString:@"&="];
    NSString *encoded = [value stringByAddingPercentEncodingWithAllowedCharacters:allowed];
    return encoded ?: @"";
}

static UIImage* LoadShortcutIcon(const char* iconPathOrNull) {
    if (iconPathOrNull && iconPathOrNull[0] != '\0') {
        NSString *path = [NSString stringWithUTF8String:iconPathOrNull];
        UIImage *custom = [UIImage imageWithContentsOfFile:path];
        if (custom) return custom;
        NSLog(@"[Shortcut] Could not load custom icon at %@, falling back to the app icon.", path);
    }
    // Fall back to the app's own bundled icon. This project ships raw PNGs
    // (see iOSresources/Assets.xcassets/AppIcon.appiconset + CMakeLists.txt)
    // rather than an .xcassets-driven app icon, so we read one of those
    // directly instead of using +[UIImage imageNamed:].
    NSString *defaultIconPath = [[NSBundle mainBundle] pathForResource:@"180" ofType:@"png"];
    if (defaultIconPath) {
        UIImage *def = [UIImage imageWithContentsOfFile:defaultIconPath];
        if (def) return def;
    }
    return nil;
}

bool presentCreateHomeScreenShortcut(const char* modelIdentifier, const char* shortcutName, const char* iconPathOrNull) {
    if (!modelIdentifier || modelIdentifier[0] == '\0') {
        NSLog(@"[Shortcut] Missing model identifier.");
        return false;
    }

    NSString *modelId = [NSString stringWithUTF8String:modelIdentifier];
    NSString *label = (shortcutName && shortcutName[0] != '\0')
        ? [NSString stringWithUTF8String:shortcutName]
        : modelId;
    NSString *bundleId = [[NSBundle mainBundle] bundleIdentifier];
    if (bundleId.length == 0) bundleId = @"com.pkdevvn.casioemu";

    UIImage *icon = LoadShortcutIcon(iconPathOrNull);
    NSData *iconData = icon ? UIImagePNGRepresentation(icon) : [NSData data];

    NSString *launchUrl = [NSString stringWithFormat:@"casioemu://launch?model=%@",
        PercentEncodeShortcutQueryValue(modelId)];
    NSString *webClipUUID = [[NSUUID UUID] UUIDString];
    NSString *profileUUID = [[NSUUID UUID] UUIDString];

    NSDictionary *webClipPayload = @{
        @"FullScreen": @YES,
        @"Icon": iconData,
        @"IgnoreManifestScope": @YES,
        @"IsRemovable": @YES,
        @"Label": label,
        @"PayloadDescription": [NSString stringWithFormat:@"Web Clip for launching \"%@\" in CasioEmuMsvc", label],
        @"PayloadDisplayName": label,
        @"PayloadIdentifier": [NSString stringWithFormat:@"%@.shortcut.%@", bundleId, webClipUUID],
        @"PayloadType": @"com.apple.webClip.managed",
        @"PayloadUUID": webClipUUID,
        @"PayloadVersion": @(1),
        @"Precomposed": @NO,
        @"URL": launchUrl
    };

    NSDictionary *profile = @{
        @"ConsentText": @{
            @"default": [NSString stringWithFormat:@"This installs a Home Screen shortcut that opens \"%@\" directly in CasioEmuMsvc.", label]
        },
        @"PayloadContent": @[webClipPayload],
        @"PayloadDescription": [NSString stringWithFormat:@"Home Screen shortcut for \"%@\"", label],
        @"PayloadDisplayName": [NSString stringWithFormat:@"%@ Shortcut", label],
        @"PayloadIdentifier": [NSString stringWithFormat:@"%@.shortcut.%@", bundleId, profileUUID],
        @"PayloadOrganization": @"CasioEmuMsvc",
        @"PayloadRemovalDisallowed": @NO,
        @"PayloadType": @"Configuration",
        @"PayloadUUID": profileUUID,
        @"PayloadVersion": @(1)
    };

    NSError *plistError = nil;
    NSData *plistData = [NSPropertyListSerialization dataWithPropertyList:profile
                                                                     format:NSPropertyListXMLFormat_v1_0
                                                                    options:0
                                                                      error:&plistError];
    if (!plistData || plistError) {
        NSLog(@"[Shortcut] Failed to serialize the configuration profile: %@", plistError);
        return false;
    }

    NSString *dataUrlString = [NSString stringWithFormat:@"data:application/x-apple-aspen-config;base64,%@",
        [plistData base64EncodedStringWithOptions:0]];
    NSURL *dataUrl = [NSURL URLWithString:dataUrlString];
    if (!dataUrl) {
        NSLog(@"[Shortcut] Failed to build the profile-install URL.");
        return false;
    }

    dispatch_async(dispatch_get_main_queue(), ^{
        SFSafariViewController *safari = [[SFSafariViewController alloc] initWithURL:dataUrl];
        safari.modalPresentationStyle = UIModalPresentationFormSheet;
        [[[iOSNativeBridge sharedInstance] rootViewController] presentViewController:safari animated:YES completion:nil];
    });

    return true;
}
#endif
