# Nghiệm thu tuần 1 / giai đoạn nền tảng

Đây là ghi nhận tại mốc tuần 1. Tài khoản và PostgreSQL đã được triển khai tiếp ở [giai đoạn 2](phase_2_status.md); các mục “chưa có auth/DB” bên dưới mô tả phạm vi mốc cũ.

Ngày cập nhật: 16/11/2026. Áp dụng pipeline dự án cá nhân; tuần 1 được hiểu là giai đoạn 1 sau khi bỏ lịch phân công ba người. Phạm vi không bao gồm auth, schema/seed DB, scoring thực thi hoặc GTK game UI — các việc này nằm ở giai đoạn tiếp theo.

## Đầu ra đã có

- [x] `docs/game_rules.md`: luật, 2–4 PLAYER, SPECTATOR, FIFO, deadline, scoring, disconnect và persistence.
- [x] `docs/protocol.md`: header, primitive, opcode/status cố định, schema payload, enum, ordering, snapshot và golden vectors.
- [x] Cấu trúc source cá nhân và Makefile build server/CLI/unit tests.
- [x] Shared codec: u8/u16/u32/u64 và length-prefixed string, kiểm tra biên.
- [x] Stream parser: frame chia nhỏ, nối frame, giữ phần chưa đủ, loại header sai.
- [x] Server TCP persistent, select một thread, non-blocking, giới hạn connection và fd_set.
- [x] RX parser riêng từng connection; TX queue có offset, xử lý partial send/EAGAIN và giới hạn 256 KiB.
- [x] CLI persistent, non-blocking connect/RX/TX, batch/interactive và ghép response theo ID.
- [x] PING/PONG chẩn đoán thật; code nghiệp vụ chưa có trả NOT_IMPLEMENTED, không giả lập gameplay.
- [x] Kiểm thử 4 client persistent và 40 connection đồng thời.
- [x] Kiểm tra GTK4/libpq compile/link trên máy hiện tại.
- [x] README hướng dẫn build, chạy, test và chuẩn bị Linux; cấu hình GitHub Actions Ubuntu.
- [ ] Nghiệm thu build/test/dependencies trên Linux thực tế: chưa có Linux VM/container hoặc CI run trong phiên này.
- [ ] AddressSanitizer chạy thành công: runtime Apple Clang hiện tại lỗi trước main; cần chạy lại bằng toolchain tương thích hoặc Linux CI.

## Bằng chứng kiểm thử trên máy hiện tại

Môi trường: macOS 26.5 arm64, Apple Clang 16.0.0, make, Python 3. GTK4 4.24.1, libpq 18.7 và pkgconf 3.0.10 đã được cài qua Homebrew với quyền thực thi được cấp trong phiên. Homebrew cũng cài/nâng cấp các dependency liên quan; không thay đổi shell profile để cấu hình libpq.

| Lệnh | Kết quả |
|---|---|
| `make all unit` | PASS, C11 với Wall/Wextra/Wpedantic/Werror; primitive, header golden bytes, parser và output queue |
| `make integration` | PASS, 8 bài black-box TCP với server/CLI thật |
| Build + unit bằng UBSan (`build/ubsan`) | PASS, không có báo lỗi undefined behavior |
| `make integration BUILD=build/ubsan` | PASS, 8 bài TCP |
| `PKG_CONFIG_PATH=/opt/homebrew/opt/libpq/lib/pkgconfig make check-deps smoke-deps` | PASS; GTK runtime 4.24.1, libpq runtime 180007 |
| `make debug` | FAIL lúc khởi tạo AddressSanitizer, cả trong và ngoài sandbox |
| Chương trình ASan độc lập chỉ chứa `int main(void) { return 0; }` | Cùng lỗi runtime, không gọi bất kỳ mã dự án nào |

Lỗi ASan quan sát được: `sanitizer_malloc_mac.inc:189`, điều kiện `!asan_init_is_running`. Không coi kết quả UBSan thay thế cho ASan. `make sanitizer-smoke` tái hiện riêng để tránh nhầm lỗi môi trường với lỗi test.

8 bài integration:

1. Ngắt giữa frame, đóng/mở lại connection 50 lần và server vẫn phục vụ.
2. Request lỗi trả status đúng, giữ kết nối và echo code/request_id.
3. 40 connection cùng mở, mỗi connection trao đổi 10 PING/PONG.
4. 4 client persistent trao đổi nhiều lượt, không lẫn response.
5. Gửi từng byte, gộp 100 frame, payload 4096 byte và request tiếp theo vẫn parse đúng.
6. Client giữ dở header không cản client khác hoàn thành request.
7. Magic/version/kind/length/request_id/direction sai chỉ đóng client vi phạm.
8. CLI thật ở batch 64 request và interactive nhận đủ PONG.

Unit output queue dùng socketpair non-blocking với send buffer nhỏ, ép budget gửi 1 byte và đầy buffer; sau đó kiểm tra nhận đủ 63 frame lớn, không còn byte thừa và queue trở về rỗng. Đây là bằng chứng partial-write handling; integration gửi từng byte có thể bị TCP gộp nên không dùng riêng bài đó để chứng minh mọi ranh giới parser.

## Hoàn tất phần Linux còn lại

Trên Linux VM/máy Linux, hoặc sau khi đưa source lên GitHub để workflow chạy:

```sh
make all test
make check-deps smoke-deps
make debug
```

Ghi phiên bản compiler/GTK/libpq và kết quả vào tài liệu này. Workflow hiện chỉ là file cấu hình trong workspace, chưa có bằng chứng run trên GitHub. Chưa tự tạo remote hoặc đẩy mã nguồn lên tài khoản nào.

## Bước tiếp theo

Giai đoạn 2: migration/seed PostgreSQL, scoring tests và auth qua CLI. Giữ các quyết định người chơi/khán giả đã chốt; không chuyển sang làm Ranked/Quick Match khi nền tảng nghiệp vụ chính còn thiếu.
