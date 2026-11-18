# Giai đoạn 5 — Lưu kết quả và các tính năng phụ trợ

Cập nhật ngày 18/11/2026. Đã triển khai trên nền gameplay 5 vòng qua CLI. Luật 2–4 PLAYER, người thứ 5 là SPECTATOR và cập nhật guess bằng giá hợp lệ mới nhất vẫn được giữ. Bước tiếp theo là giao diện GTK ở giai đoạn 6.

## Kết quả triển khai

- Migration [002_match_history.sql](../db/migrations/002_match_history.sql) nâng schema lên v2, thêm matches, match_players, rounds, round_items và submissions. DB dev của dự án tại 127.0.0.1:55432 đã được migrate; users/products hiện có được giữ lại.
- Mỗi kết quả FINISHED/ABORTED được ghi bằng một statement gọi hàm pr_save_match: hoặc có toàn bộ dữ liệu, hoặc rollback toàn bộ. Match FINISHED bắt buộc có đủ 5 vòng. Chỉ roster PLAYER có record thi đấu; khán giả không có submissions.
- Snapshot giữ tên người chơi, tên/ảnh/đơn giá/số lượng sản phẩm ở thời điểm chơi; sửa hoặc xóa catalogue sau đó không làm mất dữ liệu lịch sử. Submission chưa gửi có guess/response_ms NULL, điểm 0. Đã gửi nhiều lần thì giữ guess và response_ms hợp lệ cuối.
- Ghi lặp cùng match/document không tạo bản trùng; cùng ID khác document bị từ chối. GAME_OVER chỉ phát sau khi driver nhận kết quả commit, rồi phòng trở về WAITING và reset ready/FIFO.
- Lưu DB bất đồng bộ, tối đa 64 document chờ, tối đa 3 lần thử với khoảng nghỉ 2 giây. Callback có token match để kết quả cũ không áp vào phòng đã tái sử dụng. Socket/PING vẫn được xử lý khi DB đang chậm hoặc khóa.
- GET_HISTORY chỉ xem trận của tài khoản đang login, gồm FINISHED và ABORTED; phân trang mặc định 10, tối đa 20. Thứ tự theo history_seq giảm dần, cursor opaque tách biệt match_id ngẫu nhiên.
- GET_STATS tổng hợp FINISHED: số trận, số lần hạng nhất (tính cả đồng hạng), tỷ lệ thắng, điểm trung bình và điểm cao nhất. Không tính ABORTED.
- LIST_PLAYERS trả người online ngoài phòng, loại bản thân, phân trang theo user_id. PLAYER trong phòng được INVITE_PLAYER; SPECTATOR không được mời. Lời mời có hạn mặc định 60 giây, CLI nhận thông báo, liệt kê và chấp nhận bằng mã phòng.

## Chạy và thử thủ công

Dừng server/CLI cũ bằng Ctrl+C hoặc quit, sau đó:

```sh
make all
make db-start
make run-server
```

Chờ `DB ready (schema v2)`. Nếu DB đã chạy, có thể dùng `make db-migrate` thay bước db-start. Makefile không tự đọc .env.

Mỗi người dùng một CLI và tài khoản riêng:

```sh
./build/pr_cli 127.0.0.1 6666 --interactive
```

1. Đăng nhập 4 tài khoản ở 4 terminal. Host gõ create, rồi players để lấy ID những người online chưa ở phòng.
2. Host gõ invite USER_ID với ID thật. Bên được mời thấy INVITE_RECEIVED; gõ invitations rồi accept CODE với mã thật trong 60 giây.
3. Cả 4 người ready 1, host start. Mỗi vòng dùng guess 3000; có thể sửa thành guess 4000 trước deadline. ACK phải in giá được nhận mới nhất.
4. Người thứ 5 join CODE sẽ là SPECTATOR. Khi hết trận, chờ GAME_OVER có dòng saved in PostgreSQL.
5. Mỗi PLAYER gõ history và stats. Khán giả không được thêm trận vào lịch sử. Gõ history NEXT_CURSOR 10 với cursor thật trả về để lấy trang tiếp; next_cursor=0 là hết.

| Lệnh | Công dụng |
|---|---|
| history [cursor] [limit] | Xem tóm tắt trận của tài khoản hiện tại |
| stats | Xem thống kê cá nhân |
| players [cursor] [limit] | Liệt kê người có thể mời |
| invite USER_ID | Mời người online ngoài phòng |
| invitations | Xem thông báo lời mời còn hạn trong CLI |
| accept CODE | Chấp nhận thông báo còn hạn rồi JOIN_ROOM |

Lời mời là thông báo chứa mã phòng. Hết hạn thông báo không vô hiệu hóa mã phòng với join thông thường; mọi lần join vẫn áp dụng luật PLAYER/SPECTATOR hiện tại. Chưa có danh sách bạn bè hoặc lời mời offline.

Có thể chạy make demo-game để tạo một trận tự động 4 PLAYER + 1 SPECTATOR. Lịch sử trận đó thuộc tài khoản bot trong .local/demo_accounts.json, không thuộc tài khoản cá nhân của bạn. Tài khoản cá nhân phải thực sự tham gia trận để history có dữ liệu.

## Xem bằng pgAdmin 4

Kết nối Price Game Local: host 127.0.0.1, port 55432, database price_game, username price_game. Refresh Schemas → public → Tables sau migration. Hiện có 8 bảng, gồm 3 bảng cũ và 5 bảng mới. Mở Query Tool của đúng database rồi chạy riêng từng truy vấn:

```sql
SELECT current_database(), inet_server_port();
SELECT version FROM public.schema_migrations ORDER BY version;

SELECT id, room_code_snapshot, status, started_at, ended_at
FROM public.matches ORDER BY history_seq DESC LIMIT 20;

SELECT mp.match_id, mp.user_id, mp.display_name_snapshot,
       mp.total_score, mp.rank, mp.disconnected
FROM public.match_players mp
JOIN public.matches m ON m.id = mp.match_id
ORDER BY m.history_seq DESC, mp.rank, mp.user_id;

SELECT r.match_id, r.round_no, s.user_id, s.guessed_price_vnd,
       s.response_ms, s.score, s.submission_status
FROM public.rounds r
JOIN public.submissions s ON s.round_id = r.id
JOIN public.matches m ON m.id = r.match_id
ORDER BY m.history_seq DESC, r.round_no, s.user_id;
```

status=2 là FINISHED, status=3 là ABORTED. Bảng kết quả chỉ xuất hiện dữ liệu sau commit; tạo tài khoản/phòng hoặc mới bắt đầu trận chưa tạo record lịch sử. Kiểm thử tự động dùng DB tạm riêng, không tạo dữ liệu trong DB dev để bạn xem.

## Kiểm thử và giới hạn

| Kiểm tra | Kết quả |
|---|---|
| 6 bộ unit: protocol, auth/scoring, room, client, game, account/JSON | PASS |
| Toàn bộ qua make ubsan | 57/57 integration PASS |
| Nhóm history/social chạy thường | 11/11 PASS |
| Nhóm game sau tích hợp persistence chạy thường | 9/9 PASS |
| Migration v2 trên DB dev | OK |

57 integration gồm 12 transport + 15 auth + 10 room + 9 game + 11 history/social. Các ca mới kiểm tra transaction rollback, retry sau lỗi tạm, timeout DB không chặn PING, ghi lại không trùng, giá mới nhất 4000, snapshot không đổi khi sửa giá catalogue, submission NULL khi bỏ lượt, privacy, phân trang, stats loại ABORTED, invitation expiry/role, CLI thật, callback sau tái sử dụng slot, và dữ liệu còn sau restart server/PostgreSQL.

```sh
make history-test
make game-test
make ubsan
```

Test dùng cluster PostgreSQL tạm, timer ngắn và cấu hình lời mời ngắn riêng; không sửa DB dev. Thời lượng game mặc định vẫn 20/30 giây với nghỉ 5 giây. Linux CI/ASan chưa được xác nhận trong phiên này; giới hạn ASan trên macOS được ghi ở README.

Hàng đợi ghi hiện nằm trong RAM, chưa có journal phục hồi sau crash trước commit. Hết 3 lần thử hoặc không enqueue được thì phát GAME_SAVE_FAILED, CLI báo chưa lưu và phòng về WAITING; model lỗi được giữ tới trận mới hoặc shutdown. Nếu mất kết nối sau commit nhưng trước receipt, có thể đã có bản ghi: kiểm tra history khi DB phục hồi. Không coi thiếu GAME_OVER là bằng chứng chưa ghi. Lịch sử API hiện là tóm tắt trận; chi tiết từng vòng có thể xem trong DB, chưa có màn hình/API riêng. Giao diện hiện vẫn là CLI, chưa có GTK.
