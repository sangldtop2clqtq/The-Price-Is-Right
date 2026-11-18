# Giai đoạn 4 — Một vòng rồi đủ năm vòng

Đây là biên bản tại mốc giai đoạn 4. Persistence, lịch sử, thống kê và lời mời đã được bổ sung ngày 18/11/2026; xem [giai đoạn 5](phase_5_status.md) để biết hành vi hiện tại và kết quả kiểm thử mới.

Cập nhật ngày 17/11/2026. Đã triển khai engine gameplay qua CLI trên nền TCP/auth/phòng hiện có. Kết quả trận ở mốc này lưu trong RAM; transaction lịch sử và API history/stats thuộc giai đoạn 5.

## Hành vi đã có

- START_GAME thực thi thật: host, 2–4 PLAYER và tất cả ready → room IN_GAME, match PREPARING, response trả match_id khác 0.
- Đọc catalogue PostgreSQL bằng query bất đồng bộ, tối đa 300 sản phẩm active. Cần ít nhất 3 sản phẩm hợp lệ. Chụp tên/image_key/giá/số lượng riêng cho cả trận; sửa catalogue sau đó không làm đổi giá dùng chấm vòng đang chơi.
- GAME_STARTED khóa roster ban đầu. NEW_ROUND bắt đầu vòng và timer phía server; không chờ client xác nhận nhận event.
- Đúng 5 vòng: 1–3 SINGLE × 20 giây; 4–5 BASKET × 30 giây. Giỏ vòng 4 có 2 sản phẩm, vòng 5 có 3 sản phẩm, số lượng 1–3. Tổng giỏ = Σ(unit_price × quantity).
- Cập nhật theo yêu cầu 17/11/2026: được sửa guess nhiều lần trong hạn, lấy giá hợp lệ mới nhất và thời gian của lần gửi mới nhất để chấm. Sai match/vòng hoặc spectator trả WRONG_STATE, quá hạn trả ROUND_EXPIRED; cập nhật bị từ chối không làm mất giá đã nộp trước đó. Guess không âm, tối đa 10^12 VND.
- Dùng CLOCK_MONOTONIC; timer được kiểm tra trước dispatch và sau select, select timeout bám deadline gần nhất. Chấm response_ms theo thời điểm parse đủ frame, không tin đồng hồ client. Frame parse trước khi vòng bắt đầu cũng không được nhận.
- Đợi hết deadline dù mọi PLAYER đã nộp; công bố ROUND_RESULT một lần, nghỉ 5 giây giữa các vòng rồi tự chuyển tiếp. Reveal vòng 5 xong thì GAME_OVER ngay; thời lượng mặc định khoảng 140 giây, chưa tính chuẩn bị/mạng.
- Công thức score đúng slide, 0–1000/vòng, tối đa 5000/trận. Không nộp giữ trạng thái NOT_SUBMITTED và điểm 0; đồng tổng điểm thì đồng hạng 1,1,3.
- GAME_OVER gửi bảng tổng điểm/rank cho PLAYER và SPECTATOR. Room về WAITING, bổ sung PLAYER từ FIFO, chọn host mới khi cần và reset ready. Có thể ready để chơi trận mới.

## Khán giả và rời trận

- Người join trong IN_GAME luôn là SPECTATOR, kể cả chỉ có 2 PLAYER hoặc có PLAYER đã rời. JOIN response → GAME_SNAPSHOT riêng → các event tiếp theo; không đổi đội hình giữa trận.
- Snapshot chứa roster, connected flags, sản phẩm hiện tại, thời gian còn lại và tổng điểm đã công bố. Khi ACCEPTING không có giá thật/guess. Khi REVEALED có kết quả công khai vòng hiện tại. Khi PREPARING có thể chưa có sản phẩm.
- PLAYER rời/mất kết nối giữ guess và điểm đã có; vòng sau không nộp thì 0. Còn ≥2 PLAYER kết nối thì tiếp tục, còn <2 thì ABORTED. Khán giả không giúp đạt điều kiện đủ người.
- Rời rồi join lại chỉ được xem, không khôi phục slot tham gia trận. Host có thể rời khi đang chơi; host_id của trận/phòng giữ tới khi về WAITING và chọn lại host.
- Query chuẩn bị lỗi, thiếu catalogue hoặc giá giỏ vượt giới hạn → ABORTED với SERVER_ERROR. Callback chuẩn bị chỉ áp vào đúng match_id hiện tại, không khởi chạy lại trận đã hủy hoặc slot phòng được tái sử dụng.

## Thử bằng CLI

Nếu server cũ còn chạy, Ctrl+C rồi chạy bản mới:

```sh
make all
make db-start
make run-server
```

Mở ít nhất hai CLI, mỗi CLI một tài khoản:

```sh
./build/pr_cli 127.0.0.1 6666 --interactive
```

Sau login: host `create`, người khác `join <mã>`, mỗi PLAYER `ready 1`, host `start`. Khi NEW_ROUND xuất hiện, nhập giá VND nguyên, không dấu phân cách:

```text
guess 150000
game
```

CLI in tên/số lượng/image_key, countdown, ACK kèm giá được nhận, giá thật/điểm khi reveal và ranking cuối. `game` xem match/round/phase. Có thể nhập `guess 3000` rồi `guess 4000`, kể cả khi ACK trước chưa về; TCP/request ID giữ thứ tự, server chốt 4000 nếu lần cập nhật đó còn hợp lệ trong hạn.

Khán giả thấy WATCHING, nhận snapshot khi vào giữa vòng và không gửi guess qua UI. Countdown chỉ là hiển thị; server là nơi quyết định hạn. CLI hiển thị image_key, chưa render ảnh; ảnh và giao diện GTK ở giai đoạn 6.

## Demo tự động

Server đang chạy, terminal khác:

```sh
make demo-game
make demo-game DEMO_ARGS='--skip-last'
```

Mặc định demo dùng 4 PLAYER + 1 SPECTATOR vào giữa vòng 1, tự gửi guess cố định 100000 VND cho mỗi vòng và đối chiếu kết quả giữa mọi client. `--skip-last` để PLAYER cuối không nộp, kiểm tra 0 điểm. Không đọc giá từ DB hoặc sửa state server. Tài khoản riêng game_auto_* dùng credential file `.local/demo_accounts.json`; rerun tái sử dụng tài khoản.

`make demo`/`make demo-spectator` vẫn kiểm tra lobby/FIFO và không start trận. `make demo-game` mới là demo gameplay. Các demo cùng lúc cần account-prefix riêng, ví dụ `DEMO_ARGS='--account-prefix game_other --port 16666'`.

## Kiểm thử

```sh
make unit
make game-test
make game-real-test
make ubsan
```

- Unit game dùng đồng hồ giả: đủ 5 vòng, 20/30 giây và nghỉ 5 giây, không reveal sớm, deadline trước/đúng/sau hạn, duplicate/stale/invalid, tie ranking, disconnect và wire privacy.
- Game integration dùng PostgreSQL tạm, timer rút ngắn riêng cho fixture (600/900/200 ms). Kiểm tra đủ 5 vòng và scoring/rank, spectator snapshot accepting/revealed, reconnect, abort/FIFO, catalogue lỗi, hai phòng độc lập, DB chuẩn bị bị khóa không làm đứng phòng đang chơi, CLI thật và game demo.
- game-real-test chỉ chạy một trận với 20.000/30.000/5.000 ms như luật chính, khoảng 140 giây; không dùng timer rút ngắn.
- Các test dùng DB/server/cổng riêng, không sửa tài khoản/catalogue trong DB dev.

Kết quả nghiệm thu trên macOS/Apple Clang 16:

| Bộ kiểm chứng | Kết quả |
|---|---|
| Build C11, Wall/Wextra/Wpedantic/Werror | PASS |
| 5 bộ unit: protocol, auth/scoring, room, client, game | PASS |
| TCP/auth/room/game integration qua UBSan | 12 + 15 + 10 + 9 = 46/46 PASS |
| Game integration chạy thường | 9/9 PASS |
| Trận thời lượng thật, 4 PLAYER + spectator | PASS, đủ 5 vòng, suite hoàn tất trong 142,487 giây |

Không ghi nhận lỗi undefined behavior trong `make ubsan`. Trận thật kiểm chứng đúng 20/30 giây, scoring/rank đồng nhất, spectator không nhận giá sớm và room trở lại WAITING/reset ready.

## Cấu trúc và giới hạn

- server/src/game/match.c: state machine thuần, scoring, deadline, participant và kết quả.
- server/src/game/engine.c: adapter TCP/DB, chuẩn bị catalogue, fan-out event và timer integration.
- shared/src/game_wire.c: codec dùng chung cho public events; NEW_ROUND không có trường giá thật, snapshot chỉ nhúng result khi đã REVEALED.
- client/src/core/commands.c và tools/cli_client.c: submit guess, event decode, trạng thái/countdown và hiển thị.
- tools/demo_game.py, tools/game_protocol.py: bot/demo và decoder event qua TCP.

Match_id là số ngẫu nhiên 63 bit, kiểm tra trùng với các match còn giữ trong process; chưa là ID bản ghi DB. Chỉ giữ match hiện tại/gần nhất mỗi slot phòng, giải phóng khi bắt đầu match mới hoặc server dừng. Restart server mất kết quả chưa lưu. GAME_OVER FINISHED ở giai đoạn 4 nghĩa là hoàn thành gameplay trong RAM, không xác nhận DB đã commit. Đây là phần còn lại rõ ràng cho giai đoạn 5, cùng history/stats và invitations.

Mặc định cấu hình đúng slide. Để kiểm thử nhanh trên server riêng có thể override PR_GAME_SINGLE_MS, PR_GAME_BASKET_MS, PR_GAME_REVEAL_MS (50–60000); giá trị rút ngắn không phải luật chính. Không thay cấu hình đang chơi để suy diễn thời lượng trận khác. Linux/ASan vẫn cần môi trường kiểm chứng phù hợp như đã ghi trước đó.
