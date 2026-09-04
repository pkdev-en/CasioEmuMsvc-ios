// game_controller_core.cpp
// Triển khai logic thuần C++, port lại từ Game.java (Android).

#include "game_controller_core.hpp"

namespace game_core {

GameController::GameController(GameControllerCallbacks callbacks)
    : callbacks_(std::move(callbacks)) {}

void GameController::onCreate(const std::string &modelPathArg) {
    isStoppingEmulation_ = false;
    modelPath_ = modelPathArg;
    resetPendingState();
    // Việc tạo notification channel, set immersive mode, extract assets
    // được thực hiện ở lớp vỏ ObjC++ (cần gọi API UIKit/FileManager trực tiếp).
}

void GameController::onResume() {
    isStoppingEmulation_ = false;
    notificationScheduled_ = false;
    backgroundTimerScheduled_ = false;
    backgroundElapsedMs_ = 0;
    if (callbacks_.cancelRunningNotification) {
        callbacks_.cancelRunningNotification();
    }
}

void GameController::onPause() {
    // Lên lịch: sau NOTIFICATION_POST_DELAY_MS thì hiện notification "đang chạy nền",
    // sau BACKGROUND_TIMEOUT_MS thì tự dừng emulator.
    notificationScheduled_ = true;
    backgroundTimerScheduled_ = true;
    backgroundElapsedMs_ = 0;
}

void GameController::tick(long elapsedMsSincePause) {
    if (isStoppingEmulation_) return;

    backgroundElapsedMs_ = elapsedMsSincePause;

    if (notificationScheduled_ && backgroundElapsedMs_ >= NOTIFICATION_POST_DELAY_MS) {
        notificationScheduled_ = false;
        if (callbacks_.showNotification) {
            callbacks_.showNotification("Emulation Running",
                                         "Emulation is currently running in the background.");
        }
    }

    if (backgroundTimerScheduled_ && backgroundElapsedMs_ >= BACKGROUND_TIMEOUT_MS) {
        backgroundTimerScheduled_ = false;
        isStoppingEmulation_ = true;

        if (callbacks_.cancelRunningNotification) {
            callbacks_.cancelRunningNotification();
        }
        if (callbacks_.showStoppedNotification) {
            callbacks_.showStoppedNotification("Emulation Stopped",
                "Emulation was stopped after 5 minutes in background.");
        }
        if (callbacks_.finishAndExit) {
            callbacks_.finishAndExit();
        }
    }
}

void GameController::exportData(const std::vector<uint8_t> &data, const std::string &uriOrPath) {
    // Trên iOS không có runtime permission dạng Android Storage Access Framework;
    // UIDocumentPickerViewController đã tự xin quyền qua sandbox khi người dùng chọn nơi lưu.
    // Nên ở đây coi như luôn "đã có quyền" và ghi thẳng.
    pendingUri_ = uriOrPath;
    pendingData_ = data;
    hasPendingUri_ = true;
    hasPendingData_ = true;

    // Lớp vỏ ObjC++ chịu trách nhiệm ghi file thật (FileManager) rồi gọi lại
    // handlePickerResult(SaveFile, ...) để báo kết quả, giữ đối xứng với luồng Android.
}

bool GameController::saveScreenshot(const uint8_t *pixels, int width, int height, int pitch,
                                     const std::string &filename) {
    // Việc dựng ảnh PNG thật (CGImage/UIImage) nằm ở lớp vỏ ObjC++,
    // vì cần Core Graphics — hàm này chỉ validate input theo đúng logic Java gốc.
    if (pixels == nullptr || width <= 0 || height <= 0 || pitch <= 0) {
        if (callbacks_.onExportFailed) callbacks_.onExportFailed();
        return false;
    }
    if (filename.empty()) {
        if (callbacks_.onExportFailed) callbacks_.onExportFailed();
        return false;
    }
    return true; // lớp vỏ tiếp tục xử lý ghi file thật sau khi validate pass
}

void GameController::handlePickerResult(PendingRequestType type, const std::string &path,
                                         const std::vector<uint8_t> &data, bool success) {
    if (!success) {
        switch (type) {
            case PendingRequestType::ImportFile:
                if (callbacks_.onImportFailed) callbacks_.onImportFailed();
                break;
            default:
                if (callbacks_.onExportFailed) callbacks_.onExportFailed();
                break;
        }
        resetPendingState();
        return;
    }

    switch (type) {
        case PendingRequestType::ImportFile:
            if (callbacks_.onFileSelected) callbacks_.onFileSelected(path, data);
            break;
        case PendingRequestType::SaveFile:
            if (callbacks_.onFileSaved) callbacks_.onFileSaved(path);
            break;
        case PendingRequestType::SelectFolder:
            if (callbacks_.onFolderSelected) callbacks_.onFolderSelected(path);
            break;
        case PendingRequestType::SaveFolder:
            if (callbacks_.onFolderSaved) callbacks_.onFolderSaved(path);
            break;
        case PendingRequestType::None:
        default:
            break;
    }
    resetPendingState();
}

void GameController::setPermissionsGranted(bool granted) {
    if (!granted) {
        if (hasPendingUri_ && hasPendingData_) {
            if (callbacks_.onExportFailed) callbacks_.onExportFailed();
        }
        resetPendingState();
        return;
    }
    if (hasPendingUri_ && hasPendingData_) {
        // tương đương processPendingOperations() nhánh exportData
        handlePickerResult(PendingRequestType::SaveFile, pendingUri_, pendingData_, true);
    } else if (pendingRequestType_ != PendingRequestType::None) {
        handlePickerResult(pendingRequestType_, pendingUri_, pendingData_, true);
    }
}

void GameController::resetPendingState() {
    pendingUri_.clear();
    pendingData_.clear();
    pendingRequestType_ = PendingRequestType::None;
    hasPendingUri_ = false;
    hasPendingData_ = false;
}

} // namespace game_core
