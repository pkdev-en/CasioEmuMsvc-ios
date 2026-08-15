#include <TargetConditionals.h>
#if TARGET_OS_IPHONE
#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>
#import <AudioToolbox/AudioToolbox.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#import <SafariServices/SafariServices.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <thread>
#include <mutex>
#include <map>
#include <vector>
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

#pragma mark - Local Loopback HTTP Server (serves the shortcut profile to Safari)
//
// iOS's "download a configuration profile" flow needs Safari to actually
// fetch the profile over HTTP(S) from somewhere. Rather than the
// "data:application/x-apple-aspen-config;base64,..." trick (which gets
// unwieldy once the profile's Icon payload makes the URL long), this app
// runs a tiny loopback-only HTTP server inside the process: each generated
// profile is registered under a random one-time path, and
// presentCreateHomeScreenShortcut() below points an in-app Safari view at
// "http://localhost:<port>/shortcut/<token>.mobileconfig" -- Safari
// downloads it like any other configuration-profile link and iOS takes over
// with the normal "Install Profile" flow in Settings.
//
// The listener is bound to 127.0.0.1 only (never 0.0.0.0), so nothing
// outside the device -- not even other devices on the same Wi-Fi -- can ever
// reach it; this also keeps it outside the scope of iOS's "Local Network"
// permission prompt, which only applies to traffic that can reach other
// devices.
namespace {

class LocalProfileServer {
public:
	static LocalProfileServer& Shared() {
		static LocalProfileServer instance;
		return instance;
	}

	// Registers `data` to be served at
	// http://localhost:<port>/shortcut/<token>.mobileconfig, starting the
	// server on first use. Returns that full URL, or an empty string if the
	// server could not be started.
	std::string Publish(NSData* data) {
		if (!EnsureRunning())
			return "";

		std::string token = [[[NSUUID UUID] UUIDString] lowercaseString].UTF8String;

		const uint8_t* bytes = (const uint8_t*)data.bytes;
		{
			std::lock_guard<std::mutex> lock(mutex_);
			pending_[token] = std::vector<uint8_t>(bytes, bytes + data.length);
		}

		char urlBuf[128];
		snprintf(urlBuf, sizeof(urlBuf), "http://localhost:%d/shortcut/%s.mobileconfig", port_, token.c_str());
		return std::string(urlBuf);
	}

private:
	LocalProfileServer() = default;
	LocalProfileServer(const LocalProfileServer&) = delete;
	LocalProfileServer& operator=(const LocalProfileServer&) = delete;

	bool EnsureRunning() {
		std::lock_guard<std::mutex> lock(mutex_);
		if (running_)
			return true;

		int fd = socket(AF_INET, SOCK_STREAM, 0);
		if (fd < 0) {
			NSLog(@"[Shortcut] LocalProfileServer: socket() failed: %s", strerror(errno));
			return false;
		}

		int yes = 1;
		setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
#ifdef SO_NOSIGPIPE
		setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &yes, sizeof(yes));
#endif

		sockaddr_in addr{};
		addr.sin_family = AF_INET;
		addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // 127.0.0.1 -- device-local only
		addr.sin_port = 0; // ask the OS for any free port

		if (bind(fd, (sockaddr*)&addr, sizeof(addr)) != 0) {
			NSLog(@"[Shortcut] LocalProfileServer: bind() failed: %s", strerror(errno));
			close(fd);
			return false;
		}

		socklen_t addrLen = sizeof(addr);
		if (getsockname(fd, (sockaddr*)&addr, &addrLen) != 0) {
			NSLog(@"[Shortcut] LocalProfileServer: getsockname() failed: %s", strerror(errno));
			close(fd);
			return false;
		}
		port_ = ntohs(addr.sin_port);

		if (listen(fd, 8) != 0) {
			NSLog(@"[Shortcut] LocalProfileServer: listen() failed: %s", strerror(errno));
			close(fd);
			return false;
		}

		listenFd_ = fd;
		running_ = true;
		std::thread(&LocalProfileServer::AcceptLoop, this).detach();
		return true;
	}

	void AcceptLoop() {
		while (true) {
			sockaddr_in clientAddr{};
			socklen_t clientLen = sizeof(clientAddr);
			int clientFd = accept(listenFd_, (sockaddr*)&clientAddr, &clientLen);
			if (clientFd < 0) {
				if (errno == EINTR)
					continue;
				break; // listener was closed or hit a fatal error; stop serving
			}
#ifdef SO_NOSIGPIPE
			int yes = 1;
			setsockopt(clientFd, SOL_SOCKET, SO_NOSIGPIPE, &yes, sizeof(yes));
#endif
			std::thread(&LocalProfileServer::HandleClient, this, clientFd).detach();
		}
	}

	void HandleClient(int clientFd) {
		char request[2048];
		ssize_t n = recv(clientFd, request, sizeof(request) - 1, 0);
		if (n > 0) {
			std::string token = ExtractToken(ExtractPath(std::string(request, (size_t)n)));

			std::vector<uint8_t> bytes;
			bool found = false;
			if (!token.empty()) {
				std::lock_guard<std::mutex> lock(mutex_);
				auto it = pending_.find(token);
				if (it != pending_.end()) {
					bytes = it->second;
					found = true;
				}
			}

			if (found) {
				std::string header = "HTTP/1.1 200 OK\r\n"
					"Content-Type: application/x-apple-aspen-config\r\n"
					"Content-Length: " + std::to_string(bytes.size()) + "\r\n"
					"Cache-Control: no-store\r\n"
					"Connection: close\r\n\r\n";
				send(clientFd, header.data(), header.size(), 0);
				send(clientFd, bytes.data(), bytes.size(), 0);
			}
			else {
				static const char notFound[] =
					"HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
				send(clientFd, notFound, sizeof(notFound) - 1, 0);
			}
		}
		close(clientFd);
	}

	static std::string ExtractPath(const std::string& request) {
		// First line looks like "GET /shortcut/<token>.mobileconfig HTTP/1.1"
		auto lineEnd = request.find("\r\n");
		std::string line = (lineEnd == std::string::npos) ? request : request.substr(0, lineEnd);
		auto sp1 = line.find(' ');
		if (sp1 == std::string::npos)
			return "";
		auto sp2 = line.find(' ', sp1 + 1);
		if (sp2 == std::string::npos)
			return "";
		return line.substr(sp1 + 1, sp2 - sp1 - 1);
	}

	static std::string ExtractToken(const std::string& path) {
		auto slash = path.find_last_of('/');
		std::string file = (slash == std::string::npos) ? path : path.substr(slash + 1);
		const std::string suffix = ".mobileconfig";
		if (file.size() <= suffix.size() || file.compare(file.size() - suffix.size(), suffix.size(), suffix) != 0)
			return "";
		return file.substr(0, file.size() - suffix.size());
	}

	std::mutex mutex_;
	std::map<std::string, std::vector<uint8_t>> pending_;
	int listenFd_ = -1;
	int port_ = 0;
	bool running_ = false;
};

} // namespace

#pragma mark - Home Screen Shortcut (WebClip) Creation
//
// iOS has no public API to add an icon to the Home Screen directly. This
// follows the same overall process LiveContainer uses
// (https://github.com/LiveContainer/LiveContainer -- see
// LCAppInfo.m:generateWebClipConfigWithContainerId:iconStyle: and
// LCAppListView.swift:installMdm):
//
//   1. Build a WebClip configuration profile (PayloadType
//      com.apple.webClip.managed) whose URL points back into this app via
//      the private casioemu:// scheme, carrying the target model's folder
//      name as a query parameter.
//   2. Serialize the profile to plist XML and publish it on the loopback
//      HTTP server above, at "http://localhost:<port>/shortcut/<token>.mobileconfig".
//   3. Load that URL in an in-app SFSafariViewController; Safari downloads
//      it, recognises the application/x-apple-aspen-config content type,
//      and routes it into the system "Install Profile" flow.
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

    std::string localUrl = LocalProfileServer::Shared().Publish(plistData);
    if (localUrl.empty()) {
        NSLog(@"[Shortcut] Failed to start the local profile server.");
        return false;
    }

    NSURL *profileUrl = [NSURL URLWithString:[NSString stringWithUTF8String:localUrl.c_str()]];
    if (!profileUrl) {
        NSLog(@"[Shortcut] Failed to build the profile-install URL.");
        return false;
    }

    dispatch_async(dispatch_get_main_queue(), ^{
        SFSafariViewController *safari = [[SFSafariViewController alloc] initWithURL:profileUrl];
        safari.modalPresentationStyle = UIModalPresentationFormSheet;
        [[[iOSNativeBridge sharedInstance] rootViewController] presentViewController:safari animated:YES completion:nil];
    });

    return true;
}
#endif
