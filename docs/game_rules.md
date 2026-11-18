# Luật game v1 — yêu cầu đã chốt

Dự án cá nhân The Price Is Right. Nguồn: slide checkpoint 1 và quyết định của chủ dự án tại PROJECT_PIPELINE.md mục 1.2. Tài liệu này đặc tả hành vi đích; bản transport tuần 1 chưa triển khai gameplay.

Trạng thái hiện tại: [giai đoạn 5](phase_5_status.md) đã có engine qua CLI và lưu kết quả vào PostgreSQL. GAME_OVER xác nhận DB đã commit; nếu ghi thất bại sau tối đa 3 lần thử, GAME_SAVE_FAILED thông báo chưa lưu và phòng trở về WAITING.

## Tài khoản và phòng

- Đăng ký, đăng nhập, đăng xuất; một tài khoản chỉ online trên một kết nối. Server xác định user từ session của socket, không nhận user_id tự khai cho hành động chơi.
- Tạo phòng riêng, join bằng mã, mời người online chưa ở phòng. Lời mời hết hạn sau 60 giây; chấp nhận vẫn phải kiểm tra điều kiện JOIN_ROOM.
- Room: WAITING → IN_GAME → WAITING; đóng khi không còn thành viên. Host luôn là PLAYER.
- Trong WAITING, bốn slot đầu là PLAYER; người thứ 5 trở đi là SPECTATOR. Đủ bốn PLAYER không phải lỗi ROOM_FULL.
- Trong IN_GAME, mọi người mới vào chỉ là SPECTATOR. Participant list của trận không thay đổi giữa trận.
- Chỉ host được start, khi có 2–4 PLAYER và mọi PLAYER (kể cả host) ready. Khán giả không ready và không tính vào điều kiện start.
- Thay đổi đội hình PLAYER trong WAITING reset ready toàn bộ PLAYER. Khán giả vào/rời không reset ready.

## Khán giả và thay đổi vai trò

- Khán giả xem sản phẩm, countdown, kết quả công khai và tổng điểm. Không được ready, start hoặc submit guess; server trả WRONG_STATE nếu họ tự gửi các request này.
- Khi join giữa trận, nhận snapshot công khai trước các event tiếp theo. Không nhận giá thật hoặc guess của bất kỳ người chơi nào khi vòng còn ACCEPTING.
- Trong WAITING, slot trống được bổ sung bằng khán giả còn kết nối theo thứ tự vào phòng (FIFO), không cần request đổi role riêng. Người được chuyển role có ready=false; reset ready đội hình và phát ROOM_UPDATE.
- Không đổi role giữa trận. Khi trận kết thúc/hủy: về WAITING, loại PLAYER đã rời/disconnected, bổ sung từ FIFO, chọn host mới nếu cần, rồi reset ready.
- Host mới là PLAYER còn ở phòng có thứ tự vào phòng sớm nhất sau bổ sung. Nếu không còn PLAYER nhưng còn khán giả khi trở về WAITING, bổ sung họ trước khi chọn host.
- Khán giả không có submission, điểm cá nhân trong trận, rank hoặc record lịch sử thi đấu của trận đang xem. Không đặt thêm giới hạn nghiệp vụ số khán giả; giới hạn tài nguyên của server vẫn áp dụng.

## Trận và vòng

- Match: PREPARING → RUNNING → FINISHED, có thể chuyển ABORTED. Pha đang lưu kết quả vẫn là RUNNING nhưng không còn vòng nhận guess; chỉ FINISHED sau commit thành công.
- Có đúng 5 vòng: vòng 1–3 mỗi vòng một sản phẩm, 20 giây; vòng 4–5 mỗi vòng một giỏ 2–3 sản phẩm, 30 giây. Tổng giỏ = Σ(unit_price × quantity).
- Mỗi vòng: PREPARING → ACCEPTING → REVEALED. Đợi hết deadline dù tất cả đã nộp; reveal 5 giây giữa các vòng. Sau reveal vòng 5 thì lưu trận và kết thúc.
- Chụp snapshot tên/giá/sản phẩm khi chuẩn bị trận; không dùng giá catalogue đã chỉnh sửa để chấm lại lịch sử.
- Ảnh nằm trong client asset manifest; NEW_ROUND chỉ chứa image_key, tên và số lượng, không chứa giá. Không gói giá vào client assets.

## Guess và deadline

- Tiền dùng VND nguyên không âm, wire u64, database BIGINT. Giá thật P phải >0. Giới hạn mặc định đặc tả v1: unit price, guess và tổng giỏ không vượt 1.000.000.000.000 VND; quantity trong [1, 10]. Đây là giới hạn kỹ thuật bổ sung, có thể đổi đồng bộ trong spec và validation trước khi triển khai gameplay.
- Theo yêu cầu cập nhật ngày 17/11/2026, PLAYER được sửa guess nhiều lần khi vòng còn ACCEPTING. Giá hợp lệ mới nhất thay thế giá trước; response_ms cũng tính từ lúc bắt đầu vòng tới lần gửi hợp lệ mới nhất. Guess bằng 0 được phép; bỏ lỡ vòng lưu riêng với guessed_price/response_ms NULL và score=0.
- Server dùng CLOCK_MONOTONIC. receive_time là lúc parse đủ frame hợp lệ; chỉ nhận nếu receive_time < deadline. Đúng tại deadline bị từ chối. Không dùng timestamp client gửi để xét hạn hoặc chấm điểm.
- Kiểm tra role, membership và match/round identity trước; sai identity → WRONG_STATE. Đúng vòng nhưng hết hạn → ROUND_EXPIRED. Guess hợp lệ trong hạn ghi đè giá và response_ms cũ; request bị từ chối giữ nguyên giá hợp lệ trước đó. Không reset hoặc kéo dài deadline khi sửa giá.
- Client countdown chỉ phục vụ hiển thị. Từ thời điểm server bắt đầu ACCEPTING, timer chạy ngay, không chờ tất cả client nhận được event.

## Chấm điểm và xếp hạng

```text
A = max(0, 1 - abs(G - P) / P)
score = round_half_up(1000 * A * (0.9 + 0.1 * (1 - T / T_max)))
```

G là giá đoán, P là giá thật/tổng giỏ, T là receive_time - start_time, T_max là thời lượng vòng. Phép chia dùng số thực; tránh trừ unsigned trực tiếp. Chỉ chấm guess được nhận hợp lệ. Clamp T trong [0,T_max] để bảo vệ tính toán, không dùng clamp để nhận guess quá hạn.

- P=500.000, G=490.000, T_max=20s: T=5s → 956 điểm; T=10s → 931 điểm.
- G=P, T=0 → 1000; độ lệch ≥P → 0. Không có luật “đoán cao hơn giá thì thua”.
- Cộng điểm 5 vòng, tối đa 5000. Đồng tổng điểm thì đồng hạng theo competition ranking (1,1,3); mọi người hạng nhất cùng thắng.

## Rời phòng, mất mạng và lưu kết quả

- PLAYER mất kết nối/rời trận: giữ các guess và điểm đã có; những vòng sau không nộp được 0 điểm. Còn ≥2 PLAYER đang kết nối thì tiếp tục, còn <2 thì ABORTED. Khán giả không giúp đạt điều kiện này.
- Khán giả rời không tác động tiến trình trận. Không tự động phục hồi slot PLAYER khi reconnect; đăng nhập và join lại giữa trận chỉ được xem.
- Trận FINISHED lưu nguyên tử bằng transaction, bao gồm đội hình, vòng, snapshot sản phẩm, submission, tổng điểm và rank. Chỉ gửi GAME_OVER FINISHED sau commit. Retry phải không tạo dữ liệu trùng.
- Trận ABORTED lưu trạng thái và lý do, không tính vào win rate hoặc điểm trung bình trận hoàn thành. Khán giả không tính vào thống kê thi đấu.
- Không cam kết khôi phục kết quả chưa commit khi process crash. Khôi phục trận giữa chừng, Quick Match, Ranked, quan hệ kết bạn và spectator chat là ngoài phạm vi bản chính.
