# PR protocol v1

Nguồn: slide trang 11–16, PROJECT_PIPELINE.md và luật đã chốt. TCP IPv4, một kết nối lâu dài/client, cổng mặc định 6666. Không phụ thuộc ranh giới mỗi lần send/recv. Tất cả số nhiều byte dùng big-endian; không gửi C struct qua mạng.

**Mức triển khai hiện tại — giai đoạn 5:** transport, auth, phòng, engine 5 vòng, history/stats và online list/invite đã hoạt động qua CLI. Schema v2 lưu kết quả nguyên tử; GAME_OVER chỉ phát sau khi DB commit. GAME_SAVE_FAILED thông báo lỗi sau tối đa 3 lần ghi. Xem [nghiệm thu](phase_5_status.md).

## Header cố định 12 byte

| Offset | Wire type | Tên | Quy tắc |
|---:|---|---|---|
| 0 | u16 | magic | 0x5052 (ASCII PR) |
| 2 | u8 | version | 1 |
| 3 | u8 | kind | REQUEST=0, RESPONSE=1, EVENT=2 |
| 4 | u16 | code | Giữ nguyên code trong response |
| 6 | u16 | payload_length | 0–4096, không tính header |
| 8 | u32 | request_id | Khác 0 cho request/response, 0 cho event |

Client chỉ gửi REQUEST. Server chỉ gửi RESPONSE/EVENT. Sai magic/version/kind, request_id sai quy tắc, payload_length>4096 hoặc client gửi response/event: đóng kết nối. Chưa đủ header/payload thì giữ byte trong parser; không trả lỗi chỉ vì recv nhận thiếu.

## Primitive, enum và giới hạn

- u8/u16/u32/u64: số nguyên không dấu, big-endian. Giá tiền/ID: u64; dữ liệu v1 phải nằm trong miền signed BIGINT nếu lưu DB. ID khác 0 trừ sentinel được nêu riêng.
- bool: u8 chỉ nhận 0 hoặc 1. string: `byte_length:u16 + UTF-8 bytes`, không NUL terminator trên wire; decoder nghiệp vụ phải kiểm tra UTF-8 hợp lệ và không chứa NUL. Shared primitive reader chỉ kiểm tra giới hạn byte, không tự xác thực UTF-8.
- list<T>: `count:u16 + count phần tử T`. Decoder phải kiểm tra count, độ dài từng phần và không còn byte thừa. Không cast payload thành struct.
- username: 3–32 byte ASCII `[A-Za-z0-9_]`, phân biệt hoa/thường; password: 8–128 byte UTF-8; display_name/product_name: 1–96 byte UTF-8. REGISTER v1 đặt display_name=username.
- room_code: string đúng 6 ký tự `[A-Z0-9]`; image_key: string 1–64 byte, chỉ `[A-Za-z0-9_./-]`, không `..`, không bắt đầu `/`.
- Tiền không vượt 10^12 VND; product price >0, guess ≥0; quantity:u16 trong [1,10]. Items: 1 ở SINGLE, 2–3 ở BASKET. Round_no:u8 trong [1,5]. Thời gian khoảng: u32 millisecond; thời gian lịch sử: u64 UTC Unix millisecond, 0 nếu chưa có.
- role:u8 PLAYER=0/SPECTATOR=1. room_state:u8 WAITING=0/IN_GAME=1. match_state:u8 PREPARING=0/RUNNING=1/FINISHED=2/ABORTED=3. round_state:u8 PREPARING=0/ACCEPTING=1/REVEALED=2. round_type:u8 SINGLE=0/BASKET=1.
- submission_state:u8 NOT_SUBMITTED=0/SUBMITTED=1. abort_reason:u8 NONE=0/INSUFFICIENT_PLAYERS=1/SERVER_ERROR=2. Score/tổng điểm:u16; rank:u8 (1–4, hoặc 0 nếu chưa xếp hạng).
- Cursor:u64, 0 là bắt đầu/kết thúc danh sách. Request limit:u16 trong [1,20]; client mặc định 10. Cursor trỏ vị trí tiếp theo trong tập kết quả server sắp ổn định. Chọn page vừa payload 4096 byte, không cắt record.

GET_HISTORY sắp theo history_seq giảm dần, cursor là giá trị opaque trả ở next_cursor, không phải match_id. LIST_PLAYERS sắp user_id tăng dần, cursor là ID cuối trang. Danh sách online có thể thay đổi giữa hai request. Mỗi page tối đa 20 record, vừa giới hạn payload kể cả tên dài tối đa.

## Status

Response luôn bắt đầu bằng `status:u16`. Nếu status khác 0, payload **chỉ có 2 byte status**; không có success body hoặc text tự do.

| Value | Tên |
|---:|---|
| 0 | OK |
| 1 | INVALID_MESSAGE |
| 2 | NOT_AUTHENTICATED |
| 3 | AUTH_FAILED |
| 4 | USER_EXISTS |
| 5 | ALREADY_ONLINE |
| 6 | ROOM_NOT_FOUND |
| 7 | ROOM_FULL (dự phòng; đủ 4 PLAYER phải chuyển sang SPECTATOR) |
| 8 | NOT_ROOM_OWNER |
| 9 | PLAYER_UNAVAILABLE |
| 10 | WRONG_STATE |
| 11 | ROUND_EXPIRED |
| 12 | ALREADY_SUBMITTED (giữ mã để tương thích; không dùng để chặn cập nhật guess) |
| 13 | INTERNAL_ERROR |
| 14 | NOT_IMPLEMENTED (bổ sung cho các mốc triển khai) |

PING sai payload hoặc account request sai schema → INVALID_MESSAGE. Account payload được kiểm tra cả khi chưa cấu hình DB. Code chưa biết → INVALID_MESSAGE. Nghiệp vụ phòng/game/history/social yêu cầu login trước khi xử lý body; chưa login trả NOT_AUTHENTICATED. Các code trong bảng dưới đã được triển khai; NOT_IMPLEMENTED được giữ làm mã dự phòng.

Phòng và ready yêu cầu auth trước, rồi validate payload: CREATE/LEAVE/START rỗng; JOIN là chuỗi đúng 6 ký tự A–Z/0–9, không byte thừa; READY đúng một byte 0/1. Tài khoản đã ở phòng CREATE/JOIN tiếp trả WRONG_STATE; mã đúng định dạng nhưng không tồn tại trả ROOM_NOT_FOUND. Khán giả READY/START trả WRONG_STATE. PLAYER khác host START trả NOT_ROOM_OWNER; host thiếu người/ready trả WRONG_STATE. Đủ điều kiện và DB nhận được job chuẩn bị: START trả match_id, room chuyển IN_GAME/PREPARING rồi GAME_STARTED/NEW_ROUND khi catalogue sẵn sàng. DB chưa sẵn sàng trả INTERNAL_ERROR và không đổi room. Chuẩn bị đã được nhận nhưng catalogue lỗi/thiếu thì GAME_OVER ABORTED/SERVER_ERROR rồi room WAITING.

SUBMIT_GUESS kiểm tra schema và giới hạn giá trước. Không ở roster PLAYER đang kết nối, sai match/vòng hoặc chưa có vòng chạy: WRONG_STATE. Đúng vòng nhưng đã đóng hoặc receive_time tại/sau deadline: ROUND_EXPIRED. Theo cập nhật 17/11/2026, mọi lần gửi hợp lệ trong ACCEPTING đều trả accepted=1, thay thế guess và response_ms bằng lần gửi mới nhất. Gửi lại cùng giá cũng cập nhật response_ms. Request sai/quá hạn giữ nguyên giá hợp lệ trước, không kéo dài deadline. Không đóng vòng sớm khi tất cả đã nộp; payload wire không thay đổi.

CREATE/JOIN thành công: response chứa RoomSnapshot, sau đó ROOM_UPDATE cho mọi thành viên kể cả người gửi. READY thành công: response rỗng sau status rồi ROOM_UPDATE; đặt cùng giá trị ready có thể phát lại snapshot với revision không đổi. LEAVE/LOGOUT: response trước, loại người gửi khỏi phòng, rồi ROOM_UPDATE cho người còn lại; người rời không nhận update mới. Disconnect không có response. Phòng rỗng đóng ngay, code cũ JOIN trả ROOM_NOT_FOUND.

REGISTER thành công không tự đăng nhập. REGISTER/LOGIN có payload hợp lệ khi connection đã đăng nhập trả WRONG_STATE. LOGOUT payload phải rỗng; connection chưa login trả NOT_AUTHENTICATED. Sai username/password trả AUTH_FAILED; username đã tồn tại khi register trả USER_EXISTS. Cùng tài khoản đăng nhập ở connection khác trả ALREADY_ONLINE sau khi xác minh đúng password. Logout hoặc disconnect giải phóng online session.

Request hợp lệ cần DB trong lúc DB chưa sẵn sàng, schema chưa đúng, queue đầy hoặc query timeout trả INTERNAL_ERROR. DB connect/query timeout 5 giây, thử reconnect sau 2 giây; không tự retry query thay đổi dữ liệu. REGISTER đã gửi vào DB có thể đã commit dù client mất kết nối/không nhận response; khi thử lại có thể nhận USER_EXISTS. Không gửi SQL error, password hoặc hash về client.

## Request và success response

Mọi response dưới đây có thêm status=0 đứng trước success body. Empty nghĩa payload request 0 byte hoặc success body 0 byte.

| Code | Request | Request body | Success body |
|---|---|---|---|
| 0x0001 | REGISTER | username:string, password:string | user_id:u64 |
| 0x0002 | LOGIN | username:string, password:string | user_id:u64, display_name:string |
| 0x0003 | LOGOUT | empty | empty |
| 0x00F0 | PING | nonce:u32 | nonce:u32 |
| 0x0101 | CREATE_ROOM | empty | RoomSnapshot |
| 0x0102 | JOIN_ROOM | room_code:string | RoomSnapshot |
| 0x0103 | LEAVE_ROOM | empty | empty |
| 0x0104 | SET_READY | ready:bool | empty |
| 0x0105 | LIST_PLAYERS | cursor:u64, limit:u16 | list<OnlinePlayer>, next_cursor:u64 |
| 0x0106 | INVITE_PLAYER | target_user_id:u64 | empty |
| 0x0201 | START_GAME | empty | match_id:u64 |
| 0x0202 | SUBMIT_GUESS | match_id:u64, round_no:u8, amount:u64 | accepted:bool=1 |
| 0x0301 | GET_HISTORY | cursor:u64, limit:u16 | list<HistoryEntry>, next_cursor:u64 |
| 0x0302 | GET_STATS | empty | Stats |

PING là công cụ kiểm tra kết nối, không thay đổi game và không yêu cầu login. Nó được thêm vào v1 cho phát triển, không nằm trong slide gốc.

GET_HISTORY/GET_STATS chỉ dùng user_id từ session hiện tại, không nhận user_id tùy chọn. LIST_PLAYERS chỉ trả người đã login, còn kết nối và chưa ở phòng, loại chính người gửi. INVITE_PLAYER yêu cầu người gửi là PLAYER trong phòng và người nhận đang có mặt trong danh sách có thể mời. SPECTATOR gửi invite trả WRONG_STATE; target là chính mình/offline/đang ở phòng hoặc lời mời trùng còn hạn trả PLAYER_UNAVAILABLE. INVITE_RECEIVED mặc định có hạn 60000 ms, cấu hình PR_INVITE_TTL_MS trong [50,60000]. Server giữ tối đa 128 lời mời đang hoạt động, xóa khi hết hạn hoặc các bên join/leave/logout/disconnect. Đây là thông báo mã phòng; không phải token cấp quyền. CLI accept chỉ dùng thông báo còn hạn, sau đó gửi JOIN_ROOM thông thường; hết hạn vẫn có thể join bằng mã nếu phòng tồn tại.

## Cấu trúc dùng chung

Thứ tự trường dưới đây cũng là thứ tự trên wire. Dùng tuần tự primitive writer/reader; tên cấu trúc không phải C memory layout.

```text
Player           = user_id:u64, name:string, ready:bool, connected:bool
Participant      = user_id:u64, name:string, connected:bool
OnlinePlayer     = user_id:u64, name:string
RoomSnapshot     = room_code:string, room_state:u8, host_id:u64,
                   revision:u32, self_role:u8, spectator_count:u32,
                   players:list<Player>                         [tối đa 4]
Item             = product_id:u64, product_name:string, image_key:string, quantity:u16
ScoreEntry       = user_id:u64, submission_state:u8,
                   [guess:u64, response_ms:u32 nếu SUBMITTED],
                   score:u16, total_score:u16
PublicScore      = user_id:u64, total_score:u16
Ranking          = user_id:u64, total_score:u16, rank:u8
RoundResult      = match_id:u64, round_no:u8, actual_total:u64,
                   scores:list<ScoreEntry>                      [2–4]
HistoryEntry     = match_id:u64, state:u8, started_at:u64, ended_at:u64,
                   total_score:u16, rank:u8
Stats            = finished_matches:u32, wins:u32, average_score_x100:u32,
                   best_score:u16
```

Lists players/participants/scores/rankings chỉ chứa PLAYER, không chứa SPECTATOR. ROUND_RESULT và GAME_OVER vẫn được broadcast cho khán giả. Stats chỉ tính FINISHED, average_score_x100 là điểm trung bình ×100 làm tròn half-up, 0 nếu chưa có trận; win rate tính từ wins/finished_matches. ABORTED có rank=0 trong history và không nằm trong Stats.

ROOM_UPDATE phải encode self_role theo từng người nhận. revision tăng mỗi khi snapshot phòng thay đổi; khi gần tràn thì đóng phòng sau trận, không wrap gây nhầm state. Host_id luôn trỏ một PLAYER trong WAITING; trong IN_GAME nếu host đã rời thì giữ ID host của trận tới lúc về WAITING và chọn lại host.

## Events

Event không có status, request_id luôn bằng 0.

| Code | Event | Payload theo thứ tự |
|---|---|---|
| 0x8101 | ROOM_UPDATE | RoomSnapshot |
| 0x8102 | INVITE_RECEIVED | inviter_id:u64, room_code:string, expires_in_ms:u32 |
| 0x8201 | GAME_STARTED | match_id:u64, participants:list<Participant>, round_count:u8=5 |
| 0x8202 | NEW_ROUND | match_id:u64, round_no:u8, type:u8, items:list<Item>, duration_ms:u32, remaining_ms:u32 |
| 0x8203 | ROUND_RESULT | RoundResult |
| 0x8204 | GAME_OVER | match_id:u64, outcome:u8 (match_state FINISHED/ABORTED), rankings:list<Ranking>, abort_reason:u8 |
| 0x8205 | GAME_SNAPSHOT | MatchSnapshot |
| 0x8206 | GAME_SAVE_FAILED | match_id:u64, status:u16=13 (INTERNAL_ERROR) |

```text
MatchSnapshot = match_id:u64, match_state:u8, participants:list<Participant>,
                round_no:u8, round_state:u8, type:u8,
                items:list<Item>, duration_ms:u32, remaining_ms:u32,
                public_scores:list<PublicScore>, has_revealed_result:bool,
                [RoundResult nếu has_revealed_result=1]
```

PREPARING chưa có vòng: round_no=0, round_state=PREPARING, type=SINGLE, items rỗng, duration/remaining=0, không revealed_result. Khi đang ACCEPTING không có revealed_result, public_scores chỉ tính các vòng đã công bố. Khi REVEALED có kết quả vòng hiện tại; lúc đang lưu trận giữ snapshot công khai của vòng cuối. Không nhúng cả lịch sử các vòng vào snapshot.

GAME_OVER FINISHED: rank 1–4, abort_reason=NONE. ABORTED: rank=0 cho mọi entry, tổng điểm chỉ mang tính thông tin, abort_reason khác NONE. Không publish giá thật từ catalogue trước REVEALED dưới bất kỳ role nào.

GAME_OVER FINISHED hoặc ABORTED xác nhận DB đã commit toàn bộ match, roster, rounds, item snapshots và submissions. Match_id ngẫu nhiên 63 bit là khóa chính matches; ghi lại cùng document là no-op, trùng ID khác document bị từ chối. Khi đợi lưu, room vẫn IN_GAME; snapshot chưa công bố outcome cuối và không lộ dữ liệu chưa reveal. Sau commit, GAME_OVER được gửi trước ROOM_UPDATE về WAITING/reset ready/FIFO.

Hàng đợi ghi tối đa 64 document độc lập với slot phòng, thử tối đa 3 lần với khoảng nghỉ 2 giây; mỗi DB query timeout 5 giây. Hết lượt hoặc không thể enqueue thì GAME_SAVE_FAILED rồi ROOM_UPDATE WAITING, không phát GAME_OVER giả thành công. Lỗi mất kết nối có thể xảy ra sau commit nhưng trước receipt: thiếu GAME_OVER không chứng minh DB chưa ghi, có thể kiểm tra history sau đó. Hàng đợi chưa bền vững qua crash trước commit; bản lỗi trong model chỉ giữ đến trận tiếp theo.

## Thứ tự và quyền

1. Response echo nguyên code/request_id; client ghép pending request theo request_id. Không dùng lại một ID khi còn pending; event không được hoàn tất pending request.
2. Handler được chấp nhận xếp response của người gửi trước, sau đó xếp các event phát sinh. Không tự retry request thay đổi state khi chưa biết kết quả.
3. JOIN_ROOM giữa trận: snapshot phòng trong response → GAME_SNAPSHOT → các event tiếp theo. Chụp snapshot và thêm subscriber trong cùng lượt event loop để tránh mất sự kiện chuyển vòng.
4. Server xử lý các timer tới hạn trước request của lượt I/O đó và kiểm tra monotonic time khi parse đủ frame. Tại/sau deadline từ chối guess; rejected request không thay đổi state.
5. Khán giả gửi SET_READY/START_GAME/SUBMIT_GUESS → WRONG_STATE, ngay cả khi họ từng là PLAYER trước khi reconnect. Non-host PLAYER start → NOT_ROOM_OWNER.
6. Khi đổi role trong WAITING, broadcast ROOM_UPDATE trước khi nhận hành động dành cho role mới. Chỉ PLAYER trong participant list mới có submission.
7. Trong triển khai auth hiện tại, mỗi connection có hàng đợi request FIFO. REGISTER/LOGIN đang chờ DB hoặc hash worker sẽ giữ các request tiếp theo, gồm LOGOUT, để không vượt state. Các connection khác vẫn được phục vụ. Callback chỉ áp dụng khi slot và generation còn khớp, không chỉ dựa vào fd.

## Golden vectors để hai đầu đối chiếu

PING request, request_id=0x01020304, nonce=42:

```text
50 52 01 00 00 f0 00 04 01 02 03 04 00 00 00 2a
```

Response OK cho request trên:

```text
50 52 01 01 00 f0 00 06 01 02 03 04 00 00 00 00 00 2a
```

PING payload sai, cùng request_id:

```text
50 52 01 01 00 f0 00 02 01 02 03 04 00 01
```

## Giới hạn transport và auth hiện tại

Server hỗ trợ tối đa 64 connection, không phải 64 người/phòng. RX parser giữ tối đa một frame 4108 byte; TX pending tối đa 256 KiB/connection. Hàng đợi request riêng tối đa 64 KiB/connection (tính cả node metadata); vượt giới hạn thì đóng connection đó. Read/write budget mỗi connection mỗi iteration là 64 KiB, dispatch tối đa 32 request/connection mỗi iteration, accept tối đa 16 mỗi iteration. Select timeout bám deadline game gần nhất, tối đa 100 ms, bằng 0 nếu còn request xử lý ngay. Các giới hạn này không đổi luật 2–4 PLAYER + khán giả.

Mỗi socket đã accept là non-blocking; chỉ watch writable khi TX còn dữ liệu. Giữ offset qua partial send, xử lý EINTR/EAGAIN, EOF/error cleanup và chặn SIGPIPE làm chết process. Client có thể half-close chiều gửi ở ranh giới frame: server xử lý hết request đã nhận, gồm auth bất đồng bộ, gửi hết response rồi đóng. EOF giữa frame thì đóng với truncated_frame. Chưa có heartbeat timeout, TLS hoặc khôi phục kết nối trong bản tuần 1.

Một password worker xử lý Argon2id với interactive limits của libsodium (64 MiB mỗi job đang chạy); queue tối đa 64 job, không sửa trực tiếp client/session. Event loop nhận completion qua pipe. DB adapter dùng một kết nối libpq non-blocking với tối đa 64 job, parameter binding (tổng giá trị parameter tối đa 65536 byte/job), bản sao parameter và drain result tới NULL trước callback; kiểm tra schema_migrations version 2 trước khi nhận query. GET_HISTORY/GET_STATS giữ FIFO cùng connection trong lúc chờ DB. PR_DATABASE_URL phải có host Unix socket hoặc IPv4 dạng số để tránh DNS blocking trong connect. Không đọc file .env tự động.

Tài liệu tham chiếu: [Linux select(2)](https://man7.org/linux/man-pages/man2/select.2.html), [libpq asynchronous processing](https://www.postgresql.org/docs/current/libpq-async.html), [libsodium password storage](https://doc.libsodium.org/password_hashing/default_phf), [GTK threading](https://docs.gtk.org/gtk4/section-threading.html).

## Cập nhật củng cố nền tảng ngày 17/11/2026

Client core đã tách khỏi CLI; mỗi pending request có deadline riêng 15 giây và ID không trùng khi wrap. Frame dở có deadline tuyệt đối PR_FRAME_TIMEOUT_MS (mặc định 15000), không phải timeout idle. Cleanup, half-close, log JSON và demo tự động được mô tả trong [nghiệm thu các mục 1–4](foundation_improvements.md). Wire format và mã message không thay đổi.
