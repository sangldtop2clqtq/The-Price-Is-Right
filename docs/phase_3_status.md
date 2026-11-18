# Nghiệm thu giai đoạn 3 — phòng chơi, khán giả và ready

Đây là ghi nhận tại mốc phòng/lobby. Engine và START_GAME thực thi thật đã được triển khai tiếp ở [giai đoạn 4](phase_4_status.md); các mục “chưa có gameplay” dưới đây mô tả mốc cũ.

Ngày cập nhật: 16/11/2026. Dự án cá nhân, tiếp nối PostgreSQL/auth. Phạm vi mốc này là lobby và phòng WAITING qua CLI, chưa phải trận đấu chạy được.

## Đã triển khai

- CREATE_ROOM với mã ngẫu nhiên 6 ký tự A–Z/0–9, kiểm tra collision, người tạo làm host.
- JOIN_ROOM: một tài khoản chỉ ở một phòng; bốn slot PLAYER, người thứ 5 trở đi SPECTATOR.
- LEAVE_ROOM và cleanup cùng một luồng khi logout, disconnect hoặc TX queue lỗi; phòng hết thành viên được đóng.
- SET_READY cho PLAYER, host cũng phải ready; khán giả bị từ chối. PLAYER vào/rời reset ready đội hình; khán giả vào/rời không reset ready.
- Slot PLAYER trống trong WAITING tự bổ sung bằng khán giả vào sớm nhất còn kết nối, theo FIFO dựa trên thứ tự join chứ không phải vị trí mảng. Người được bổ sung phải ready lại.
- Host rời được chuyển cho PLAYER hiện tại vào phòng sớm nhất, sau bước bổ sung FIFO.
- ROOM_UPDATE broadcast theo từng người nhận: code/state/host/revision, self_role, số khán giả và tối đa bốn PLAYER gồm tên, ready, connected.
- Snapshot codec dùng chung server/CLI, kiểm tra giới hạn số người, bool, code, ID trùng, byte thiếu/thừa và host trong WAITING.
- CLI xử lý được event xen giữa response mà vẫn ghép request_id đúng. Có create/join/leave/ready/room/start.
- START_GAME kiểm tra host, 2–4 PLAYER và mọi PLAYER ready; khán giả không ảnh hưởng điều kiện này. Khi hợp lệ vẫn trả NOT_IMPLEMENTED vì engine trận thuộc giai đoạn 4; không tạo match_id hoặc giả chuyển IN_GAME.
- SUBMIT_GUESS được kiểm tra schema; request hợp lệ trả WRONG_STATE vì chưa có vòng nhận guess, bao gồm người gửi là khán giả.

## Kiểm chứng

Trên macOS/Apple Clang 16, giữ PostgreSQL/libsodium của mốc trước:

| Kiểm tra | Kết quả |
|---|---|
| Build C11, Wall/Wextra/Wpedantic/Werror | PASS |
| Unit codec/parser/output queue và auth/scoring | PASS |
| Unit phòng + snapshot wire | PASS |
| TCP integration | 8/8 PASS |
| Auth integration (sau khi refactor fixture DB dùng chung) | 12/12 PASS |
| Room integration với server và PostgreSQL tạm | 8/8 PASS |
| `make ubsan`: 3 bộ unit + toàn bộ 28 integration | PASS, không báo undefined behavior |

Room integration kiểm tra: response đứng trước ROOM_UPDATE; role cá nhân; 4 PLAYER rồi SPECTATOR; khán giả không reset ready; bổ sung FIFO sau rejoin; chuyển host và đóng phòng khi logout người cuối; payload sai không đổi state; 5 người join đồng thời chỉ thêm 3 PLAYER và 2 SPECTATOR; hai phòng không nhận event của nhau; đủ ready chỉ vượt qua bước kiểm tra, chưa khởi chạy trận; CLI thật nhận event/ready/leave đúng.

Unit kiểm tra thêm quy tắc IN_GAME ở mức domain bằng cách dựng state trực tiếp: người mới chỉ là SPECTATOR, không promote/chuyển host giữa trận. Chưa có đường TCP chuyển IN_GAME, GAME_SNAPSHOT hay spectator xem sản phẩm/countdown; đó là giai đoạn 4.

## Cách thử

Khởi động DB/server nếu chưa chạy:

```sh
make all
make db-start
make run-server
```

Mở hai terminal CLI, mỗi terminal dùng tài khoản riêng:

```sh
./build/pr_cli 127.0.0.1 6666 --interactive
```

Terminal 1: login bằng tài khoản của bạn, rồi nhập `create`. CLI in `CREATE_ROOM code=XXXXXX ...`. Terminal 2: đăng ký/login tài khoản khác, nhập `join XXXXXX` với mã thật vừa nhận.

Cả hai nhập `ready 1`; host nhập `start`. Ở mốc này host nhận NOT_IMPLEMENTED khi đủ điều kiện; non-host nhận NOT_ROOM_OWNER; chưa đủ ready hoặc một người chơi nhận WRONG_STATE. Dùng `room` xem snapshot gần nhất đã nhận. `room` đọc cache CLI, không gửi request mới; chờ ROOM_UPDATE trước khi kiểm tra trạng thái vừa thay đổi.

Mở thêm CLI với tài khoản thứ 3, 4, 5 để thử khán giả. Người thứ 5 thấy `role=SPECTATOR`, không ready/start được. Cho một PLAYER nhập `leave`; khán giả vào sớm nhất nhận ROOM_UPDATE với role PLAYER và ready=0. Logout/rút kết nối host cũng phải chuyển host đúng.

Kiểm thử tự động: `make room-test`; toàn bộ: `make test`; sanitizer: `make ubsan`. Các integration dùng PostgreSQL tạm riêng, không thêm tài khoản vào DB dev của bạn.

## Dữ liệu và giới hạn

- Phòng, thành viên, ready và hàng đợi khán giả giữ trong RAM; không có bảng rooms trong PostgreSQL. Restart server làm mất phòng đang chờ; tài khoản/sản phẩm vẫn còn trong DB.
- Mỗi server tối đa 64 kết nối, tối đa 64 phòng; giới hạn thành viên thực tế bị ràng buộc bởi tổng connection. Đủ 4 PLAYER không phải ROOM_FULL.
- Chưa có match/round/timer gameplay, ảnh, GAME_SNAPSHOT, mời người chơi, history/stats hoặc GTK UI.
- Linux và ASan vẫn chưa được nghiệm thu trong môi trường này; workflow Linux có sẵn.

Bước tiếp theo: giai đoạn 4 — một vòng chơi hoàn chỉnh qua CLI, sau đó đủ 5 vòng, timer phía server và snapshot cho người vào xem giữa trận.
