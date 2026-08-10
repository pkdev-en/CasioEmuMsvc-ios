# 📲 Hướng dẫn cài CasioEmuMsvc trên iOS bằng SideStore

> Hướng dẫn này dành cho người dùng **iPhone/iPad** muốn cài trình giả lập Casio cho ios.

---

## 📋 Yêu cầu

- iPhone / iPad chạy **iOS 14 trở lên**
- **Apple ID** (tài khoản Apple miễn phí là đủ)
- **SideStore** đã được cài và thiết lập trên thiết bị
- File **CasioEmuMsvc.ipa** (tải từ [GitHub Releases](https://github.com/pkdev-en/CasioEmuMsvc-ios/releases))

---

## 🔧 Bước 1 — Cài SideStore (nếu chưa có)

> Nếu đã có SideStore, bỏ qua bước này và đến thẳng Bước 2.

SideStore là app sideload không cần kết nối máy tính sau khi thiết lập. Có hai cách cài:

### Dùng AltServer trên máy tính (cách truyền thống)

1. Tải **AltServer** tại [altstore.io](https://altstore.io) và cài trên máy tính.
2. Tải file **SideStore.ipa** từ [github.com/SideStore/SideStore/releases](https://github.com/SideStore/SideStore/releases).
3. Kết nối iPhone vào máy tính qua cáp USB.
4. Click vào icon AltServer trong thanh taskbar → **Sideload .ipa** → chọn file `SideStore.ipa`.
5. Nhập Apple ID và mật khẩu → chờ cài xong.
6. Vào **Cài đặt → Cài đặt chung → VPN & Quản lý thiết bị**, tin tưởng chứng chỉ.

---

## 📥 Bước 2 — Tải file IPA

1. Truy cập trang **Releases** của repo:  
   👉 [github.com/pkdev-en/CasioEmuMsvc-ios/releases](https://github.com/pkdev-en/CasioEmuMsvc-ios/releases)

2. Tải về file **`CasioEmuMsvc.ipa`** phiên bản mới nhất.

3. Lưu file vào **Files → Trên iPhone này** (hoặc iCloud Drive).

---

## 📲 Bước 3 — Cài IPA bằng SideStore

1. Mở app **SideStore** trên iPhone.

2. Kết nối VPN của SideStore: nhấn vào nút **Connect** / **WireGuard** ở góc trên (cần thiết để ký và cài app).

3. Chuyển sang tab **My Apps** (biểu tượng ứng dụng ở thanh dưới).

4. Nhấn dấu **＋** ở góc trên bên phải.

5. Chọn **File** → duyệt đến file `CasioEmuMsvc.ipa` vừa tải → nhấn **Open**.

6. Đăng nhập Apple ID nếu được yêu cầu, rồi nhấn **Install**.

7. Chờ vài giây — ứng dụng sẽ xuất hiện trên màn hình chính.

---

## ✅ Bước 4 — Khởi động lần đầu

1. Mở **CasioEmuMsvc** từ màn hình chính.

2. Ứng dụng sẽ hiện giao diện **Startup UI** — chọn mô hình Casio bạn muốn mô phỏng.

3. Cung cấp file **ROM** tương ứng khi được yêu cầu.  
   _(Ứng dụng hỗ trợ: fx-9860G, fx-CG50, và nhiều mô hình khác dùng chip nX-U8/100 hoặc nX-U16/100)_

4. Nhấn **Start** để bắt đầu giả lập.

---

## 🔄 Gia hạn app (tránh hết hạn sau 7 ngày)

Apple ID miễn phí chỉ ký app có hiệu lực **7 ngày**. Để gia hạn:

- Mở **SideStore**, kết nối VPN, vào **My Apps**.
- Nhấn **Refresh All** hoặc nhấn vào tên app rồi chọn **Refresh**.
- Bật **Background Refresh** trong Cài đặt SideStore để tự động gia hạn.

> **Mẹo:** Dùng **LiveContainer** nếu bạn muốn cài nhiều app vượt giới hạn 3 app của Apple ID miễn phí.

---

## ❗ Xử lý lỗi thường gặp

| Lỗi | Giải pháp |
|-----|-----------|
| App bị xóa sau 7 ngày | Gia hạn qua SideStore trước khi hết hạn |
| Không kết nối được VPN SideStore | Kiểm tra lại **pairing file** trong Settings của SideStore |
| Lỗi "App could not be installed" | Đảm bảo đã tin tưởng chứng chỉ trong Cài đặt iOS |
| Màn hình trắng / crash khi mở | Kiểm tra lại file ROM — đảm bảo đúng mô hình |
| Không tìm thấy ROM | Đặt file ROM cùng thư mục với app hoặc import thủ công qua Files |

---

## 💬 Hỗ trợ & Cộng đồng

- 💬 **Discord**: [discord.gg/NM39VPdJTf](https://discord.gg/NM39VPdJTf)
- 🐛 **Báo lỗi**: [GitHub Issues](https://github.com/pkdev-en/CasioEmuMsvc-ios/issues)
- 📖 **README đầy đủ**: [docs/README.vi.md](docs/README.vi.md)

---

> ⚠️ **Lưu ý pháp lý**: Ứng dụng này được cấp phép theo **GPL-3.0**. Người dùng tự chịu trách nhiệm về tính hợp pháp của các file ROM sử dụng tại khu vực của mình.
