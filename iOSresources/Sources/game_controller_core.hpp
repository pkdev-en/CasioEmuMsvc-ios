// game_controller_core.hpp
// Logic thuần C++, port lại từ app/src/main/java/com/tele/u8emulator/Game.java
// Không phụ thuộc UIKit — chỉ chứa state và hàm xử lý, để lớp vỏ ObjC++ gọi vào.

#pragma once

#include <string>
#include <functional>
#include <cstdint>
#include <vector>

namespace game_core {

// Tương đương các hằng số trong Game.java
constexpr long BACKGROUND_TIMEOUT_MS = 5 * 60 * 1000; // 5 phút
constexpr long NOTIFICATION_POST_DELAY_MS = 1500;

enum class PendingRequestType {
    None = 0,
    ImportFile = 1,   // case 1 bên Java
    SaveFile = 2,      // case 2
    SelectFolder = 3,  // case 3
    SaveFolder = 4     // case 4
};

// Callback ra ngoài lớp vỏ ObjC++ để show UI / toast / notification thật.
struct GameControllerCallbacks {
    std::function<void(const std::string &title, const std::string &body)> showNotification;
    std::function<void()> cancelRunningNotification;
    std::function<void(const std::string &title, const std::string &body)> showStoppedNotification;
    std::function<void()> finishAndExit;
    std::function<void(const std::string &message)> showToast;
    std::function<void(const std::string &path, const std::vector<uint8_t> &data)> onFileSelected;
    std::function<void()> onImportFailed;
    std::function<void(const std::string &uriOrPath)> onFileSaved;
    std::function<void(const std::string &path)> onFolderSelected;
    std::function<void(const std::string &path)> onFolderSaved;
    std::function<void()> onExportFailed;
};

class GameController {
public:
    explicit GameController(GameControllerCallbacks callbacks);

    // Tương đương onCreate()
    void onCreate(const std::string &modelPathArg);

    // Tương đương onResume()
    void onResume();

    // Tương đương onPause() / app vào background
    void onPause();

    // Được gọi định kỳ (timer) để mô phỏng Handler.postDelayed bên Android
    void tick(long elapsedMsSincePause);

    // Tương đương exportData(byte[], Uri)
    void exportData(const std::vector<uint8_t> &data, const std::string &uriOrPath);

    // Tương đương saveImageToMediaStore(...)
    bool saveScreenshot(const uint8_t *pixels, int width, int height, int pitch,
                         const std::string &filename);

    // Tương đương processPendingOperations() / handlePendingRequest()
    void handlePickerResult(PendingRequestType type, const std::string &path,
                             const std::vector<uint8_t> &data, bool success);

    void setPermissionsGranted(bool granted);

    const std::string &modelPath() const { return modelPath_; }
    bool isStoppingEmulation() const { return isStoppingEmulation_; }

private:
    void resetPendingState();

    GameControllerCallbacks callbacks_;
    std::string modelPath_;
    bool isStoppingEmulation_ = false;
    bool notificationScheduled_ = false;
    bool backgroundTimerScheduled_ = false;
    long backgroundElapsedMs_ = 0;

    // pending* tương đương các static field bên Game.java
    std::string pendingUri_;
    std::vector<uint8_t> pendingData_;
    PendingRequestType pendingRequestType_ = PendingRequestType::None;
    bool hasPendingUri_ = false;
    bool hasPendingData_ = false;
};

} // namespace game_core
