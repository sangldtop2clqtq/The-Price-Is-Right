# The Price Is Right — Game đoán giá nhiều người

Dự án cá nhân xây dựng game đoán giá bằng **C, TCP, GTK4 và PostgreSQL**. Người chơi vào phòng, đoán giá sản phẩm hoặc giỏ hàng qua 5 vòng và nhận điểm theo độ chính xác, thời gian gửi giá. Một trận có **2–4 người chơi**; người thứ 5 trở đi tham gia với vai trò **khán giả**.

Ứng dụng gồm server quản lý luật chơi, client desktop GTK4 và client dòng lệnh (CLI) để chơi thử/debug. Code được chia module để có thể phát triển từng tính năng riêng.

## Mục lục

- [Chức năng và luật chơi](#chức-năng-và-luật-chơi)
- [Kiến trúc](#kiến-trúc)
- [Cài đặt môi trường](#cài-đặt-môi-trường)
- [Build và chạy lần đầu](#build-và-chạy-lần-đầu)
- [Chơi thử](#chơi-thử)
- [Database và cấu hình](#database-và-cấu-hình)
- [Cấu trúc code](#cấu-trúc-code)
- [Kiểm thử](#kiểm-thử)
- [Xử lý lỗi thường gặp](#xử-lý-lỗi-thường-gặp)
- [Giới hạn và tài liệu chi tiết](#giới-hạn-và-tài-liệu-chi-tiết)

## Chức năng và luật chơi

| Nhóm | Chức năng hiện có |
|---|---|
| Tài khoản | Đăng ký, đăng nhập, đăng xuất; mật khẩu hash bằng Argon2id; một phiên online/tài khoản |
| Phòng | Tạo phòng riêng, vào bằng mã 6 ký tự, ready, chuyển chủ phòng khi cần |
| Gameplay | 5 vòng, đếm giờ phía server, cập nhật giá đoán, điểm vòng và xếp hạng cuối |
| Khán giả | Xem trận, không gửi giá; được bổ sung vào slot PLAYER khi phòng chờ có chỗ trống theo thứ tự vào phòng |
| Kết quả | Lưu trận, người chơi, vòng, sản phẩm và giá đoán vào PostgreSQL |
| Cá nhân/xã hội | Lịch sử, thống kê, người online ngoài phòng và lời mời có thời hạn |
| Giao diện | GUI GTK4 và CLI; có bot để demo nhiều người trên một máy |

**Một trận gồm 5 vòng:**

| Vòng | Nội dung | Thời gian đoán |
|---|---|---|
| 1–3 | Một sản phẩm mỗi vòng | 20 giây/vòng |
| 4 | Giỏ 2 loại sản phẩm, có số lượng | 30 giây |
| 5 | Giỏ 3 loại sản phẩm, có số lượng | 30 giây |

Có 5 giây xem kết quả giữa các vòng. Một trận bình thường khoảng 140 giây, chưa tính chuẩn bị và lưu DB. Catalogue demo dùng giá minh họa cố định, không phải giá bán cập nhật ngoài thị trường.

- Chủ phòng bắt đầu khi có 2–4 PLAYER và mọi PLAYER, kể cả chủ phòng, đã ready. SPECTATOR không ready và không được tính vào điều kiện này.
- Trong phòng chờ, 4 người đầu là PLAYER; người thứ 5 trở đi là SPECTATOR. Mọi người vào giữa trận đều là SPECTATOR, kể cả khi chưa đủ 4 PLAYER.
- Giá đoán là **tổng giá trị** sản phẩm/giỏ hàng, dùng số VND nguyên không âm, tối đa 10^12.
- Được sửa giá trước deadline. **Lần gửi hợp lệ cuối cùng** thay thế cả giá và thời gian phản hồi để chấm điểm. Gửi 3000 rồi 4000 hợp lệ thì lấy 4000.
- Mỗi vòng tối đa 1000 điểm, cả trận tối đa 5000; càng gần giá thật và gửi càng sớm thì điểm càng cao. Đồng tổng điểm được đồng hạng.
- Khán giả không có record thi đấu trong lịch sử của trận đang xem. Nếu còn dưới 2 PLAYER đang kết nối, trận bị hủy (ABORTED); trận hủy không tính vào thống kê trận hoàn thành.

Công thức và tình huống biên: [luật game](docs/game_rules.md).

## Kiến trúc

```text
Client GTK4 ─┐
             ├── TCP ── Server C ── PostgreSQL
Client CLI ──┘                     tài khoản, catalogue, kết quả
```

Server quyết định quyền, deadline, điểm và kết quả. Client gửi thao tác và hiển thị dữ liệu nhận được, không truy cập DB trực tiếp. CLI và GTK dùng chung lớp kết nối/command; client và server dùng chung protocol/codec trong shared/.

Server dùng socket nonblocking và select để phục vụ nhiều connection. PostgreSQL được truy vấn qua libpq bất đồng bộ; hash mật khẩu chạy trong worker riêng. Phòng, ready, session và lời mời nằm trong RAM; kết quả trận được lưu bền vững sau khi DB commit.

## Cài đặt môi trường

Chạy các lệnh từ **thư mục gốc dự án**, nơi có Makefile. Máy phát triển/kiểm thử hiện tại là macOS. Có cấu hình CI Ubuntu nhưng chưa có kết quả nghiệm thu Linux trong lần bàn giao này. Chưa có bản build Windows native.

| Thành phần | Mục đích |
|---|---|
| Compiler C11, make, pkg-config | Build và tìm thư viện |
| GTK4, glib-compile-resources | GUI, resource và nhóm test UI |
| libpq, PostgreSQL binaries | Kết nối, khởi tạo DB dev và DB tạm khi test |
| libsodium | Hash/verify mật khẩu |
| Python 3 | DB helper, demo bot và integration tests |

### macOS với Homebrew

Nếu chưa có compiler, cài Xcode Command Line Tools; bỏ qua lệnh đầu nếu đã cài:

```sh
xcode-select --install
brew install gtk4 libpq pkgconf postgresql@18 libsodium
```

Makefile tự tìm pkg-config của libpq trong đường dẫn Homebrew thông dụng.

### Ubuntu/Debian

```sh
sudo apt-get update
sudo apt-get install -y build-essential pkg-config python3 libgtk-4-dev libpq-dev libsodium-dev postgresql
```

GUI cần desktop/display. Chạy DB helper và tests bằng user thường; PostgreSQL initdb không chạy bằng root. Trên Windows, cần chuẩn bị môi trường Linux và display phù hợp nếu muốn chạy GTK.

Kiểm tra sau khi cài:

```sh
make check-deps
make smoke-deps
```

smoke-deps chỉ kiểm tra compile/link thư viện, chưa mở GUI hoặc kết nối DB.

## Build và chạy lần đầu

### 1. Build và chuẩn bị database

```sh
make all gui
make db-start
```

- make all tạo build/pr_server và build/pr_cli; make gui tạo build/pr_gui.
- make db-start tạo cluster PostgreSQL riêng ở .local/postgres, khởi động DB, áp dụng migration và seed 30 sản phẩm demo.
- Script không quản lý service PostgreSQL hệ thống. Có thể chạy lại db-start; dữ liệu được giữ, seed không ghi đè sản phẩm đã chỉnh sửa.

### 2. Chạy server — terminal thứ nhất

```sh
make run-server
```

Chờ **DB ready (schema v2)** trước khi đăng nhập/chơi. Dòng READY của TCP chỉ cho biết cổng đã mở, chưa đảm bảo DB sẵn sàng. Giữ terminal này chạy.

### 3. Chạy client — terminal khác

Giao diện desktop:

```sh
make run-gui
```

GUI hiện tự kết nối tới **127.0.0.1:6666**. Chạy lệnh trên ở nhiều terminal để mở nhiều cửa sổ độc lập; mỗi người chơi cần một tài khoản riêng.

Hoặc dùng CLI:

```sh
./build/pr_cli 127.0.0.1 6666 --interactive
```

### 4. Dừng ứng dụng

Đóng cửa sổ GUI hoặc nhập quit trong CLI. Dừng server bằng Ctrl+C tại terminal server. Khi không cần DB nữa:

```sh
make db-stop
```

db-stop giữ dữ liệu. make clean chỉ xóa build/, không xóa .local/.

## Chơi thử

### Bằng GUI

1. Mở ít nhất 2 cửa sổ GUI; mở 5 cửa sổ để thử đầy đủ PLAYER/SPECTATOR.
2. Đăng ký và đăng nhập tài khoản khác nhau ở mỗi cửa sổ. Username 3–32 ký tự gồm chữ ASCII, số hoặc dấu gạch dưới; password 8–128 byte. Đăng ký thành công chưa đồng nghĩa đã đăng nhập.
3. Một người tạo phòng và chia sẻ mã; những người còn lại vào bằng mã đó.
4. Mọi PLAYER bật ready, kể cả chủ phòng. Chủ phòng bắt đầu trận.
5. Nhập giá rồi bấm gửi hoặc Enter mỗi vòng. Có thể gửi lại để sửa khi còn giờ; phân biệt giá đang nhập với giá đã được server xác nhận.
6. Xem kết quả vòng và xếp hạng cuối. Sau khi lưu thành công, xem lịch sử/thống kê của tài khoản vừa chơi hoặc quay lại phòng cho trận tiếp.

### Bằng CLI

Mở một CLI cho mỗi tài khoản. Người tạo phòng nhập lần lượt:

```text
register player1
login player1
create
```

register và login hỏi mật khẩu riêng, không hiện ký tự nhập. Người thứ hai đăng ký/login bằng tên khác rồi nhập join với **mã thật** do create trả về. ABC123 dưới đây là mã minh họa:

```text
join ABC123
ready 1
```

Người tạo phòng cũng nhập ready 1, sau đó start. Khi vòng mới xuất hiện, mỗi PLAYER có thể nhập:

```text
guess 3000
guess 4000
```

Không thay chữ guess bằng tên người chơi. Nếu lần cập nhật hợp lệ, CLI in accepted=1 guess=4000. Cập nhật bị từ chối do hết hạn không thay đổi giá hợp lệ đã nhận trước đó.

| Lệnh CLI | Công dụng |
|---|---|
| create / join CODE / leave | Tạo, vào hoặc rời phòng |
| room / game | Xem trạng thái phòng/trận CLI đang giữ |
| ready 1 / ready 0 | Bật/tắt ready trong phòng chờ |
| start | Chủ phòng bắt đầu khi đủ điều kiện |
| guess AMOUNT | Gửi/cập nhật giá, không dùng dấu phân tách hàng nghìn |
| history [cursor] [limit] | Lịch sử cá nhân; mặc định cursor 0, limit 10, tối đa 20 |
| stats | Thống kê cá nhân, chỉ tính trận FINISHED |
| players [cursor] [limit] | Người online ngoài phòng có thể mời |
| invite USER_ID | PLAYER trong phòng mời theo ID |
| invitations / accept CODE | Xem lời mời / chấp nhận lời mời còn hạn |
| ping 42 / logout / quit | Kiểm tra kết nối / đăng xuất / thoát |

Dùng next_cursor trả về để lấy trang lịch sử tiếp; next_cursor=0 là hết. Lời mời mặc định có hạn 60 giây, chứa mã phòng và vẫn áp dụng luật JOIN bình thường.

### Demo tự động trên một máy

Khi server và DB sẵn sàng, chọn một lệnh trong terminal khác:

| Lệnh | Kiểm tra |
|---|---|
| make demo | Tạo/vào phòng với 4 PLAYER |
| make demo-spectator | Thêm khán giả và FIFO bổ sung PLAYER |
| make demo-game | Bot chơi đủ 5 vòng với 4 PLAYER và 1 SPECTATOR |

Ví dụ:

```sh
make demo-game
```

Demo dùng tài khoản riêng, lưu thông tin đăng nhập trong .local/demo_accounts.json. Kết quả thuộc tài khoản bot, không tự xuất hiện trong lịch sử tài khoản cá nhân của bạn.

## Database và cấu hình

### Thông số mặc định

| Thành phần | Địa chỉ / giá trị |
|---|---|
| Game server | 127.0.0.1:6666 |
| PostgreSQL dev | 127.0.0.1:55432 |
| Database / user | price_game / price_game |
| Dữ liệu PostgreSQL | .local/postgres/ |
| Log PostgreSQL | .local/postgres.log |
| Log server qua Makefile | .local/server.jsonl |

Cluster dev dùng trust authentication trên loopback, thường không cần password DB. Đây là cấu hình demo local. Khi triển khai ngoài máy cá nhân cần cấu hình quyền DB/kết nối phù hợp; ứng dụng hiện chưa có TLS.

| Lệnh | Hành vi |
|---|---|
| make db-start | Tạo/start DB dev riêng, migrate và seed |
| make db-status | Xem DB dev đang chạy hay dừng |
| make db-migrate | Áp dụng migration |
| make db-seed | Thêm sản phẩm demo còn thiếu |
| make db-stop | Dừng DB dev, giữ dữ liệu |

db-start luôn chuẩn bị cluster dev riêng. db-migrate và db-seed dùng PR_DATABASE_URL nếu được đặt, nếu không dùng DB dev mặc định. Có thể đặt PR_PG_BIN tới thư mục PostgreSQL binaries nếu script không tìm được phiên bản trên máy.

### Kiểm tra bằng pgAdmin 4

Tạo kết nối với host 127.0.0.1, port **55432**, username price_game; mở database price_game. Trong Query Tool:

```sql
SELECT current_database(), inet_server_port();
SELECT version FROM public.schema_migrations ORDER BY version;
SELECT id, room_code_snapshot, status, started_at, ended_at
FROM public.matches ORDER BY history_seq DESC LIMIT 20;
```

Schema v2 có 8 bảng: users, products, schema_migrations, matches, match_players, rounds, round_items, submissions. Refresh Schemas → public → Tables nếu chưa thấy bảng mới. Bảng kết quả có dữ liệu sau khi trận được lưu; phòng hiện tại không có bảng riêng vì giữ trong RAM.

### Biến môi trường

Không cần .env cho luồng chạy mặc định. Nếu muốn tùy chỉnh, sao chép .env.example thành .env khi chưa có cấu hình riêng, chỉnh giá trị rồi nạp trong terminal chạy server:

```sh
set -a
. ./.env
set +a
make run-server
```

Makefile không tự đọc .env. Cấu hình mẫu: [.env.example](.env.example).

| Biến | Ý nghĩa |
|---|---|
| PR_DATABASE_URL | Chuỗi kết nối libpq; dùng hostaddr IPv4 hoặc host Unix socket |
| PR_LOG_FILE / PR_LOG_LEVEL | Nơi ghi log và mức info/debug/off |
| PR_GAME_SINGLE_MS / PR_GAME_BASKET_MS / PR_GAME_REVEAL_MS | Thời lượng vòng đơn/giỏ/reveal; giữ mặc định khi chơi theo luật chính |
| PR_INVITE_TTL_MS | Hạn lời mời, mặc định 60000 ms |
| PR_FRAME_TIMEOUT_MS | Deadline nhận đủ frame, không phải timeout chờ người chơi |

Chạy trực tiếp binary server cần truyền PR_DATABASE_URL; make run-server đã đặt mặc định. Để thử CLI qua LAN, server có thể bind bằng ./build/pr_server 0.0.0.0 6666 trong terminal đã export cấu hình DB, rồi CLI dùng IPv4 của máy server. GUI hiện chưa có màn hình chọn server khác.

## Cấu trúc code

```text
shared/                    Protocol, codec và dữ liệu wire dùng chung
server/src/
  main.c                   TCP event loop và vòng đời connection
  auth/                    Tài khoản, validation, password worker
  db/                      libpq async, serialize và lưu kết quả
  game/                    Phòng, match, timer và scoring
  handlers/                Request phòng/tài khoản/social
client/src/
  core/                    Kết nối TCP và command cho CLI/GTK
  gui/
    app.c                  Khởi tạo cửa sổ và điều hướng
    model/                 State và reset dữ liệu UI
    network/               GLib adapter, controller, response/event
    features/              auth, lobby, room, gameplay, results, account, social
    common/                Hiển thị tên và ảnh sản phẩm
client/include/ui/         Header/API từng feature
client/resources/          CSS, ảnh và GResource manifest
db/                        Migration và seed
tools/                     CLI, demo bot và DB helper
tests/                     Unit và integration tests
docs/                      Luật, protocol, pipeline và nghiệm thu
```

shared/module.mk, server/module.mk và client/module.mk khai báo source theo package. Khi thêm file .c, cập nhật manifest tương ứng để file tham gia build.

**Gợi ý đọc code:** client/src/gui/main.c → app.c → feature cần xem. Luồng nhận dữ liệu nằm ở gui/network/events.c. Phía server bắt đầu từ server/src/main.c, rồi theo handler vào auth/game/db. Không thêm luật chơi hoặc query DB trực tiếp vào widget.

API và cách mở rộng: [CODE_STRUCTURE.md](docs/CODE_STRUCTURE.md). Đặc tả byte/opcode: [protocol.md](docs/protocol.md).

## Kiểm thử

```sh
make unit
make test
```

make unit chạy 15 binary test, gồm core và UI; nhóm UI cần GTK4. make test còn chạy 57 integration tests của transport/auth/room/game/history-social và GUI smoke. Integration tạo cluster PostgreSQL tạm rồi dọn khi xong, không dùng DB dev. GUI smoke cần display, kiểm tra startup/nhiều instance; chưa thay thế nghiệm thu thủ công mọi thao tác trên màn hình.

| Lệnh bổ sung | Phạm vi |
|---|---|
| make integration | TCP/transport |
| make auth-test / make room-test | Tài khoản / phòng |
| make game-test / make history-test | Gameplay / lưu kết quả và social |
| make gui-smoke | Build và thử khởi động GUI |
| make game-real-test | Trận thời lượng thật, khoảng 140 giây |
| make ubsan | Build/test với UndefinedBehaviorSanitizer |
| make debug | Build/test với AddressSanitizer và UBSan |

Sau lần chia package, build server/CLI/GUI, 15 bộ unit, 57 integration tests và GUI smoke đã đạt trên máy macOS phát triển. Sanitizer chưa được chạy lại cho lần chia package. ASan trên môi trường Apple Clang của máy phát triển từng lỗi khởi tạo ngay cả với chương trình tối giản; xem [trạng thái tuần 1](docs/week_1_status.md). Có [workflow Linux](.github/workflows/ci.yml); cần xem kết quả chạy thực tế khi đưa code lên GitHub.

## Xử lý lỗi thường gặp

| Hiện tượng | Kiểm tra / xử lý |
|---|---|
| pkg-config không tìm được thư viện | Cài đủ dependency, chạy make check-deps; kiểm tra PKG_CONFIG_PATH nếu cài ngoài đường dẫn chuẩn |
| Missing PostgreSQL executable | Cài PostgreSQL server binaries hoặc đặt PR_PG_BIN tới thư mục chứa initdb/pg_ctl/psql |
| initdb báo không được chạy bằng root | Dùng user thường, không chạy DB helper/tests bằng sudo |
| Client không kết nối được | Chạy server trước; kiểm tra cổng, địa chỉ và log; GUI dùng localhost cố định |
| Auth trả INTERNAL_ERROR | Chờ DB ready (schema v2), kiểm tra make db-status và cấu hình DB; migrate nếu schema cũ |
| ALREADY_ONLINE | Dùng tài khoản khác cho cửa sổ thứ hai hoặc logout phiên cũ |
| Không start được | Cần 2–4 PLAYER và mọi PLAYER ready; khán giả không tính vào điều kiện |
| pgAdmin thiếu bảng/dữ liệu | Kiểm tra price_game tại port 55432, refresh bảng và thực sự hoàn thành/lưu một trận |
| History cá nhân rỗng sau demo | Demo dùng tài khoản bot; tham gia trận bằng tài khoản cá nhân để có lịch sử |
| GUI test không mở được display | Chạy trong desktop session hoặc thiết lập display ảo phù hợp trên Linux |
| Build xong nhưng hành vi vẫn cũ | Dừng server/client cũ rồi mở lại binary vừa build |

Xem log server:

```sh
tail -f .local/server.jsonl
```

## Giới hạn và tài liệu chi tiết

Kết quả chỉ được xác nhận lưu sau commit. Khi ghi lỗi, server thử tối đa 3 lần rồi thông báo GAME_SAVE_FAILED và đưa phòng về chờ. Hàng đợi ghi nằm trong RAM: crash trước commit có thể mất kết quả; mất kết nối sau commit nhưng trước thông báo có thể khiến client chưa biết DB đã lưu. Kiểm tra lịch sử sau khi DB phục hồi.

Chưa có khôi phục trận đang chơi sau restart, tự phục hồi vai trò PLAYER khi reconnect, TLS, Quick Match hoặc Ranked. Các ý tưởng tăng độ khó theo vòng mới là đề xuất, chưa đưa vào luật. GUI đã có code chạy; kiểm chứng đầy đủ trải nghiệm trên Linux và các tình huống UI vẫn cần tiếp tục nghiệm thu.

| Tài liệu | Nội dung |
|---|---|
| [PROJECT_PIPELINE.md](PROJECT_PIPELINE.md) | Kế hoạch tổng thể theo giai đoạn |
| [CODE_STRUCTURE.md](docs/CODE_STRUCTURE.md) | Package, trách nhiệm file và cách thêm tính năng |
| [UI_PIPELINE.md](docs/UI_PIPELINE.md) | Thiết kế/tiêu chí UI; ví dụ cũ cần đối chiếu bản đồ package |
| [game_rules.md](docs/game_rules.md) | Luật chơi, công thức điểm và xử lý mất kết nối |
| [protocol.md](docs/protocol.md) | Wire format, request/response/event |
| [phase_5_status.md](docs/phase_5_status.md) | Persistence, history/stats, truy vấn DB và kiểm thử |

Biên bản theo giai đoạn ghi trạng thái tại thời điểm lập; một số mô tả cũ chưa bao gồm tính năng bổ sung sau đó. Dùng README để bắt đầu chạy và CODE_STRUCTURE.md để tra source hiện tại. Khi chia sẻ code, giữ cấu hình mẫu; không đưa mật khẩu trong .env, dữ liệu .local/ hoặc tài khoản demo cá nhân vào gói source.
