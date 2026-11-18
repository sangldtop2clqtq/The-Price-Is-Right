# Nghiệm thu giai đoạn 2 — PostgreSQL và tài khoản

Ghi nhận tại mốc auth/DB. Phần phòng đã được triển khai tiếp ở [giai đoạn 3](phase_3_status.md); các mục “chưa có phòng” bên dưới mô tả mốc cũ.

Ngày cập nhật: 16/11/2026. Đây là phần tiếp theo của nền tảng tuần 1, dành cho dự án cá nhân. Giữ nguyên quyết định 2–4 PLAYER và SPECTATOR từ người thứ 5; chưa triển khai phòng/gameplay ở mốc này.

## Đã hoàn thành

- [x] Migration version 1: users, products, schema_migrations; constraints và unique username phân biệt hoa/thường.
- [x] Seed 30 sản phẩm giá minh họa; chạy lại không nhân bản hoặc ghi đè catalogue đã chỉnh sửa. Image key chỉ là tham chiếu, chưa có bộ ảnh thực tế.
- [x] DB adapter libpq non-blocking: connect/poll, select readiness, parameterized queries, flush/consume/drain, queue giới hạn 64 job.
- [x] Kiểm tra schema v1 trước khi nhận auth query; connect/query timeout 5 giây, reconnect sau 2 giây, không tự retry mutation.
- [x] REGISTER/LOGIN/LOGOUT: validate username, mật khẩu UTF-8 8–128 byte, không NUL hoặc byte thừa; error response giữ đúng code/request_id.
- [x] Lưu password hash Argon2id, salt riêng bằng libsodium; một worker hash/verify để không chặn network loop. Không lưu mật khẩu rõ trong DB/log.
- [x] Một session online/tài khoản; logout/disconnect giải phóng session. Callback đối chiếu slot + generation để không áp trạng thái cho fd được tái sử dụng.
- [x] Request cùng connection đi theo FIFO, không để LOGIN vượt REGISTER hoặc LOGOUT vượt LOGIN đang pending.
- [x] CLI lệnh register/login/logout, hiển thị user_id/tên/status, prompt ẩn mật khẩu trên TTY và giữ kết nối lâu dài.
- [x] Hàm scoring độc lập và unit tests đúng hai ví dụ slide 956/931 điểm, đối xứng đoán cao/thấp và biên deadline.
- [x] Công cụ tạo/start/stop cluster dev riêng, migration/seed, config mẫu và README.
- [x] CI Linux đã bổ sung PostgreSQL và libsodium; chưa có run Linux thực tế trong phiên này.

## Kiểm chứng

Môi trường hiện tại: macOS arm64, Apple Clang 16, PostgreSQL/libpq 18.7, libsodium 1.0.22. PostgreSQL server và libsodium đã được cài bằng Homebrew trong phiên này; không bật service tự khởi động cùng hệ điều hành.

| Bộ kiểm tra | Kết quả |
|---|---|
| Build C11 với Wall/Wextra/Wpedantic/Werror | PASS |
| Unit codec/parser/partial output | PASS |
| Unit credentials/UTF-8/scoring | PASS |
| TCP integration tuần 1 | 8/8 PASS |
| Auth integration với PostgreSQL tạm | 12/12 PASS |
| `make ubsan` toàn bộ unit + TCP + auth | PASS, không báo undefined behavior |
| Linux và ASan | Chưa xác nhận; giữ giới hạn môi trường đã ghi ở tuần 1 |

Auth integration tạo cluster tạm dưới /tmp, dọn sau khi kết thúc và không dùng DB dev. Đã kiểm tra:

1. Register → login → logout, auth guards và trạng thái đã login.
2. 4 đăng ký trùng đồng thời: chỉ một thành công, DB chỉ có một user.
3. 3 login cùng tài khoản: chỉ một session, logout cho phép connection khác login.
4. Disconnect giải phóng online session.
5. Disconnect khi INSERT đã được xác nhận đang chạy trong DB: callback không gửi response sang connection mới.
6. Disconnect khi login/hash pending: slot mới vẫn chưa đăng nhập.
7. Payload sai, UTF-8 sai, NUL, byte thừa, độ dài vượt giới hạn bị từ chối; password tiếng Việt hoạt động.
8. Hash Argon2id có salt khác nhau cho cùng mật khẩu, không phải plaintext.
9. Gửi gộp REGISTER/LOGIN/LOGOUT giữ đúng thứ tự xử lý và response.
10. Migration/seed chạy lại vẫn chỉ có 30 sản phẩm và một version record.
11. Giữ DB lock khiến query timeout: client khác vẫn PING được; adapter reconnect và auth hoạt động lại.
12. CLI thật tạo/login/logout và tài khoản vẫn dùng được sau restart cả server lẫn PostgreSQL.

## Dùng ngay trên máy hiện tại

DB dev đã được khởi tạo và để chạy ở 127.0.0.1:55432, tên DB/role `price_game`, dữ liệu trong `.local/postgres`. Cổng game mặc định 6666. Game server không được để chạy nền sau kiểm thử; bạn mở bằng terminal:

```sh
make run-server
```

Terminal khác:

```sh
./build/pr_cli 127.0.0.1 6666 --interactive
```

Trong CLI nhập `register tuan`, nhập mật khẩu ở prompt, rồi `login tuan` và `logout`. Nếu DB đã dừng, chạy `make db-start` trước. Tắt DB khi không dùng bằng `make db-stop`; lệnh này không xóa dữ liệu.

## Giới hạn và bước tiếp theo

- DB dev chỉ bind loopback và dùng trust authentication để phát triển; không phải cấu hình triển khai công khai. Account transport chưa có TLS.
- Một DB connection và một hash worker; queue có giới hạn, chưa có pool hoặc cam kết throughput auth. Unknown username và sai password cùng trả AUTH_FAILED; chưa có rate limiting hoặc cam kết chống phân biệt tài khoản qua thời gian phản hồi.
- Nếu client mất response khi REGISTER đang được DB xử lý, tài khoản có thể vẫn được tạo; thử lại có thể nhận USER_EXISTS. Không tự retry mutation.
- Chưa có migration match/round/history hoặc lưu kết quả trận vì gameplay chưa triển khai.
- Tiếp theo: giai đoạn 3 — CREATE/JOIN/LEAVE phòng, PLAYER/SPECTATOR, FIFO, ready, host và ROOM_UPDATE qua CLI.

Nguồn thiết kế: [libpq async](https://www.postgresql.org/docs/current/libpq-async.html), [libsodium password hashing](https://doc.libsodium.org/password_hashing/default_phf).
