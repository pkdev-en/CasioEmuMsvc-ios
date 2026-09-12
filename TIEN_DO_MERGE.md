# Tiến độ merge upstream (telecomadm1145/CasioEmuMsvc @ stable) vào fork iOS (pkdev-en/CasioEmuMsvc-ios @ hmmmmm)

Nhánh `hmmmmm` đi trước điểm rẽ nhánh 514 commit, upstream `stable` đi trước 129 commit —
hai nhánh phát triển song song, phân kỳ thật. Tổng cộng 23 file xung đột thật khi merge.

**Đã xong 13/23 file** (đóng gói trong thư mục này, giữ nguyên cấu trúc thư mục gốc).
Copy đè các file này vào working copy đã merge (`git merge upstream/stable --no-commit --no-ff`)
rồi tiếp tục resolve 10 file còn lại trước khi commit.

## Danh sách file đã xong

1. `.gitignore` — gộp thêm `build-ios/` của fork vào danh sách ignore chung
2. `.github/workflows/build.yml` — hợp nhất cơ chế build-matrix động (`fromJSON`) của upstream với nhánh `ios` của fork; gộp phần đóng gói `.dmg`/`.ipa`
3. `README.md` — gộp badge platform (thêm cả macOS lẫn iOS), giữ link Discord/Stars của fork
4. `CMakeLists.txt` (root) — giữ cấu hình `CMAKE_OSX_DEPLOYMENT_TARGET`/Xcode attributes của fork cho iOS/macOS
5. `CasioEmuMsvc/CMakeLists.txt` — **phức tạp**: hợp nhất bản gỡ `GameViewController.mm` (đường iOS "chết" không dùng chung GUI) với tính năng mới của upstream (thư viện mã hoá Monocypher/ed25519, macOS bundle metadata). **Đã fix lỗi nguy hiểm**: nếu giữ `elseif(APPLE)` của upstream thay vì `elseif(APPLE AND NOT IOS)` của fork, build iOS sẽ nhầm link framework macOS (Cocoa/AppKit/OpenGL) → lỗi biên dịch ngay trên iOS.
6. `CasioEmuMsvc/src/Emulator.hpp` — giữ `SDL_Texture* tx` (dùng thật trong `CalculatorWindow.h`, xác nhận bằng grep trước khi giữ)
7. `CasioEmuMsvc/src/Emulator.cpp` — **fix lỗi cú pháp thật**: dòng `return` mồ côi của upstream đứng trước `#ifdef` sẽ gây lỗi biên dịch nếu giữ nguyên; đã bỏ theo đúng bản fork
8. `CasioEmuMsvc/src/Ext/SysDialog.cpp` — hợp nhất khối dialog native iOS/macOS (`#ifdef __APPLE__`) của fork với khối Linux (`zenity`/`kdialog`) của upstream; **fix lỗi trùng lặp hàm JNI** `onImportFailed` (Git auto-merge tạo ra 2 định nghĩa giống hệt, sẽ lỗi biên dịch "redefinition")
9. `CasioEmuMsvc/src/Gui/Gui.h` — thêm nhánh `#elif defined(IOS)` cho font CJK/Thai (đặt **trước** `#elif defined(__APPLE__)` vì thứ tự `#elif` quan trọng); **fix thêm 1 dấu `}` thừa** có sẵn từ trước trong nhánh fork (làm lệch brace toàn hàm `GetCJKFontPath`), xác nhận bằng cách so brace-depth trước/sau
10. `CasioEmuMsvc/src/Gui/MemBreakPoint.cpp` — **merge phức tạp nhất tới lúc này**: hợp nhất 2 tính năng breakpoint độc lập không loại trừ nhau (multi-register watch-list của fork + single atomic register-breakpoint của upstream); **fix lỗi trùng lặp định nghĩa hàm** `RegisterBreakpointTriggered`/`UpdateRegisterBreakpointConfig` do Git auto-merge
11. `CasioEmuMsvc/src/Gui/PopUpDisplay.h` — **quan trọng**: đã xác nhận qua grep chéo sang `Screen.cpp` rằng cả 2 bộ API của class `ScreenMirror` đều có caller thật (`update()`/`RenderCore()` kiểu tab-ImGui của fork **và** `renderer()`/`clear()`/`contentRect()`/`present()` kiểu composer trực tiếp của upstream) → giữ cả hai, không được chọn 1 bỏ 1
12. `CasioEmuMsvc/src/Gui/ThemeManager.cpp` — `SetLightMode` dùng bản refactor gọn của upstream (an toàn, không mất gì); `SetDarkMode` **bắt buộc giữ bản fork** vì có "Premium Dark Blue Palette" (17 màu tuỳ chỉnh) mà upstream không có tương đương — nếu dùng bản upstream sẽ mất theme tối đẹp khi người dùng bấm nút chuyển theme
13. `CasioEmuMsvc/src/Gui/Ui.hpp` — **fix bug nghiêm trọng có sẵn trong nhánh `hmmmmm` từ trước khi merge**: `UIWindow::Render()` gọi `ImGui::Begin/End` 2 lần liên tiếp mỗi frame (double-render mọi cửa sổ ImGui); giữ đúng kiểu trả về `bool GotoMemoryAddress()` của fork (upstream đổi thành `void` sẽ vỡ build ở 3 chỗ gọi trong `Ui.cpp`)

## Lỗi case-sensitivity đã phát hiện và sửa (xuất hiện lặp lại ở nhiều file)

Include `#include "Ext/iOSNativeBridge.h"` (chữ **i thường**) sai với tên file thật
`IOSNativeBridge.h` (chữ **I hoa**). Chạy được trên Windows (case-insensitive filesystem)
nhưng **fail build trên Linux/macOS CI** (case-sensitive). Đã sửa ở: `Emulator.cpp`, `ThemeManager.cpp`.
**Cần rà lại các file còn lại (đặc biệt `casioemu.cpp`) khi merge tiếp.**

## Còn lại 10 file (chưa merge)

1. `CasioEmuMsvc/src/Gui/Ui.cpp` (file lớn — đang làm dở)
2. `CasioEmuMsvc/src/Peripheral/Keyboard.cpp`
3. `CasioEmuMsvc/src/Peripheral/Screen.cpp` (lớn — upstream thêm ~2013 dòng)
4. `CasioEmuMsvc/src/Rop/Compiler.h` (lớn nhất — ~3765 dòng khác biệt cả hai phía)
5. `CasioEmuMsvc/src/Rop/RopCompilerUI.cpp`
6. `CasioEmuMsvc/src/StartupUi/StartupUi.cpp`
7. `CasioEmuMsvc/src/casioemu.cpp` (lõi — cần rà lỗi case-sensitivity `IOSNativeBridge.h`)
8. `app/build.gradle`
9. `app/src/main/AndroidManifest.xml`
10. `app/src/main/java/com/tele/u8emulator/Game.java`

## Cách áp dụng

```bash
git clone -b hmmmmm https://github.com/pkdev-en/CasioEmuMsvc-ios.git
cd CasioEmuMsvc-ios
git remote add upstream https://github.com/telecomadm1145/CasioEmuMsvc.git
git fetch upstream
git merge upstream/stable --no-commit --no-ff
# Copy đè 13 file trong gói này vào đúng vị trí tương ứng
# Sau đó tiếp tục resolve 10 file còn lại trong danh sách trên
```
