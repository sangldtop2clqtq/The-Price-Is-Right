# Hoàn thiện các mục 1–4

Ngày nghiệm thu: 17/11/2026. Phạm vi gồm demo tự động, client core có thể tái sử dụng, cleanup/lỗi kết nối và structured log. Giữ nguyên luật 2–4 PLAYER và khán giả; gameplay và GTK chưa triển khai.

Sau mốc này, [giai đoạn 4](phase_4_status.md) đã có gameplay. Demo lobby hiện không gọi START_GAME; dùng `make demo-game` để chơi 5 vòng. Các số liệu/giới hạn dưới đây ghi nhận tại mốc củng cố nền tảng.

## 1. Demo tự động

Khởi động bản server mới (Ctrl+C server cũ nếu còn chạy):

```sh
make all
make db-start
make run-server
```

Terminal khác:

```sh
make demo
make demo-spectator
```

`demo` tự tạo tài khoản còn thiếu, đăng nhập 4 người, tạo/join phòng, ready và đối chiếu snapshot. `demo-spectator` thêm người thứ 5, kiểm tra SPECTATOR không được ready, rồi cho một PLAYER rời để kiểm tra FIFO và reset ready.

- Dùng TCP thật, không gọi SQL trực tiếp hoặc sửa state trong server. Đây là công cụ Python chẩn đoán; CLI C dùng client core ở mục 2.
- Tài khoản mặc định `demo_auto_1` … `demo_auto_5`, tách khỏi tài khoản cá nhân. Mật khẩu ngẫu nhiên nằm trong `.local/demo_accounts.json`, quyền 0600, có khóa file khi tạo/đọc; không in hoặc truyền password qua command line.
- Giữ file credential để tái sử dụng các tài khoản. Không tự đổi mật khẩu tài khoản đã có, không xóa tài khoản và không tự retry mutation khi gặp lỗi DB. Nếu trùng tài khoản dùng mật khẩu khác, script dừng và báo lỗi.
- Khi kết thúc, các connection demo đóng và phòng được cleanup; tài khoản vẫn còn. START_GAME trả NOT_IMPLEMENTED đúng với mốc hiện tại, chưa chạy ván game.
- Script cần server đang chạy; không tự bật/tắt server hoặc DB của bạn.

Tùy chọn:

```sh
make demo DEMO_ARGS='--hold 60'
make demo-spectator DEMO_ARGS='--port 16666 --account-prefix demo_other'
python3 tools/demo_room.py --players 2
```

`--hold` giữ connection để quan sát; Ctrl+C dọn các connection demo. Mặc định host 127.0.0.1, port 6666; để có spectator tự động cần đủ 4 PLAYER. Các demo chạy đồng thời phải dùng account-prefix khác nhau.

## 2. Tách client để tái sử dụng cho GTK

| File | Trách nhiệm |
|---|---|
| `client/include/pr_client.h` | API connection, pending requests và callback |
| `client/src/core/connection.c` | Connect non-blocking, RX/TX, response/event, timeout và disconnect |
| `client/src/core/commands.c` | Encode PING/auth/join/ready/lệnh payload rỗng |
| `tools/cli_client.c` | Đọc lệnh, prompt mật khẩu, in kết quả và cache snapshot |

Core không phụ thuộc GTK, printf, stdin hoặc signal handler toàn tiến trình. Mỗi request có ID, code, tag ứng dụng và deadline riêng; event không hoàn tất pending request. ID wrap bỏ qua 0 và ID còn pending. Connect timeout 5 giây, request timeout 15 giây từ enqueue; tối đa 64 pending request và giới hạn TX dùng chung.

Khi làm GTK: sở hữu connection ở main thread, chuyển readiness của main loop thành fd_set cho process và gọi tick định kỳ khi không có I/O. Không gọi select blocking trong callback widget. Payload chỉ được mượn trong callback; copy nếu cần giữ lại. Không close/reenter connection ngay trong callback, trả -1 để từ chối message sai. Khi API trả lỗi, UI phải hủy pending state của mình và xóa snapshot/session cục bộ. Chưa có adapter GTK, reconnect tự động hoặc callback riêng cho từng request bị hủy.

## 3. Củng cố cleanup/lỗi kết nối

- Close idempotent; dọn RX, TX, request queue, session và membership. Shutdown server không broadcast dây chuyền.
- EOF khi frame dở: đóng với truncated_frame. EOF ở ranh giới frame (half-close): xử lý request đã nhận, kể cả auth bất đồng bộ, gửi hết response rồi đóng.
- Frame chưa hoàn chỉnh có deadline tuyệt đối 15 giây từ byte đầu; không gia hạn bằng cách gửi byte chậm. Connection idle không có frame dở không bị timeout này.
- RX/request/TX đều có giới hạn. Client flood hoặc đọc chậm bị đóng riêng; broadcast đánh dấu drop và cleanup ở vòng lặp sau, tránh sửa membership đệ quy trong fan-out.
- Send dùng MSG_NOSIGNAL/SO_NOSIGPIPE theo hệ điều hành; app dùng client core không cần tự cài global signal handler để tránh SIGPIPE.
- DB lỗi/timeout vẫn kết thúc auth request rõ ràng và reconnect; PING tiếp tục được phục vụ. Callback cũ tiếp tục đối chiếu generation trước khi đổi session.

`PR_FRAME_TIMEOUT_MS` nhận [50,60000], mặc định 15000. Đây không phải thời lượng vòng chơi.

## 4. Log có ngữ cảnh và thời gian xử lý

`make run-server` mặc định ghi JSON Lines vào `.local/server.jsonl`. Binary chạy trực tiếp không đặt PR_LOG_FILE ghi stderr:

```sh
tail -f .local/server.jsonl
```

Mỗi record có time_ms (UTC Unix milliseconds), event, connection_id (generation), user_id, room_code, request_id, code, status, elapsed_us, reason và dropped_logs.

- response: elapsed_us từ lúc parse đủ request tới khi xếp response vào TX; gồm chờ FIFO/DB/hash, chưa bao gồm thời gian đến client.
- db_result: thời gian từ enqueue DB job tới callback; status là PGresult enum hoặc -1, khác wire status của game.
- connect/disconnect/room_leave/db_ready/db_timeout/db_disconnect ghi lifecycle và nguyên nhân cleanup. ID chưa có dùng 0, room chưa có dùng `-`.
- Không log payload, password, hash, username, SQL parameter hoặc connection string. Identifier bị giới hạn ký tự; trường còn lại là số.
- `PR_LOG_LEVEL=info` mặc định bỏ PING thành công; debug ghi cả PING, off tắt structured log. Một số lỗi khởi động/readiness DB vẫn có thông báo stderr ngắn.
- Queue logger tối đa 128 × 512 byte; pipe/terminal dùng non-blocking writes. Sink đầy thì bỏ log mới thay vì chặn mạng; record tiếp theo sau khi sink thông mang bộ đếm dropped_logs. Shutdown không chờ sink vô thời hạn. File thường vẫn chịu độ trễ filesystem; không cam kết giữ log đang đệm lúc crash/thoát hoặc bộ đếm cuối cùng khi sink luôn đầy.
- Chưa có rotation tự động hoặc audit bền vững. `.local/` không được commit.

Biến môi trường mẫu ở `.env.example`; chương trình không tự source `.env`.

## Kết quả nghiệm thu

Build C11 với Wall/Wextra/Wpedantic/Werror đạt. `make ubsan` đạt toàn bộ:

| Bộ kiểm thử | Kết quả |
|---|---|
| Unit protocol/parser/output queue | PASS |
| Unit credentials/scoring | PASS |
| Unit room/snapshot | PASS |
| Unit client core | PASS |
| TCP integration | 12/12 PASS |
| Auth integration | 15/15 PASS |
| Room/demo integration | 10/10 PASS |

Test mới kiểm chứng: event không tiêu thụ pending request, deadline riêng/ID wrap, SIGPIPE, half-close, frame timeout, RX overflow, log pipe đầy không chặn PING, ngắt DB backend rồi phục hồi, log không chứa credential, demo chạy lại không thêm tài khoản trùng, spectator ngừng đọc bị loại ở TX limit trong khi host tiếp tục hoạt động.

Integration dùng PostgreSQL tạm và log riêng, không ghi vào DB dev/log vận hành. Linux và ASan vẫn cần môi trường kiểm chứng phù hợp như đã ghi ở các mốc trước. Bước tiếp theo vẫn là một vòng gameplay hoàn chỉnh, sau đó đủ 5 vòng.
