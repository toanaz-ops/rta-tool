# Hàng đợi câu hỏi cần con người

*Mỗi mục là một thứ agent KHÔNG tự quyết được. Trả lời xong thì đánh dấu và ghi
ngày + phiên nào nhận, rồi chuyển nội dung vào record/HANDOFF — file này là hàng
đợi, không phải nơi lưu quyết định.*

**Quy ước:** `[ ]` chờ · `[x]` đã trả lời · `[!]` đang CHẶN việc

---

## Chặn việc ngay bây giờ

- [ ] **`[!]` Skill nhắn tin cho chủ nhân không tồn tại trên máy này.**
  Chủ nhân bảo dùng "skill Ask Seph (suy ra từ skill ask-LUNA)" để nhắn Telegram.
  Kiểm: `~/.claude/skills/` không có mục nào tên `ask-seph`, `ask-luna`, hay
  chứa `telegram`; `SearchSkills` trả rỗng. Agent **không tự dựng** một đường
  gửi ra dịch vụ ngoài.
  **Cần:** tên/đường dẫn skill đúng, hoặc xác nhận cứ dùng file này thay thế.

## Mua tiêu chuẩn — hai lane đứng yên vì chúng

- [ ] **ISO 2969:2015 / SMPTE ST 202:2010** — bảng dung sai X-curve. Chặn **L5b**.
- [ ] **IEC 60268-16** — STI. Chặn **L4d**.
- [ ] **ISO 18233:2006** — tiêu chuẩn đúng cho quy tắc độ dài capture. Record chỉ
  *nêu tên* và không trích một dòng nào vì chưa ai mua. **Đừng trích khi chưa có
  bản thật** — lane này đã trả học phí một lần với AES-2id (bẫy #16).

## Quyết định sản phẩm

- [ ] **Tên sản phẩm chính thức.**
- [ ] **Push lên `origin`?** `main` cục bộ đang đi trước `origin/main` — đo bằng
  `git rev-list --count origin/main..main`, đừng cộng nhẩm. Chủ nhân đã chủ động
  chọn chưa push; mục này chỉ để hỏi lại khi thấy hợp lý.
- [ ] **Ba câu giao diện treo từ L5c:** cap 3 pane; dải màu spectrograph; unwrap
  có hiện trace pha đã lưu không.

## Từ lane L4a, sau khi step 0 chạy (2026-08-30)

- [ ] **Hai hằng số gate `100 Hz` / `8 kHz` — chủ nhân có muốn duyệt con số
  không, hay để agent chốt?** Chúng là **quan sát, chưa dẫn xuất**, đọc từ lưới
  hợp nhất ba nguồn. Chi phí đã đo: `bessel` order 6/8 design đúng 100 Hz đo ra
  100.6 / 110.6 Hz nên bị **từ chối oan** — nhất quán (0.006 oct repeat spread),
  có số kèm trong refusal reason, không nhấp nháy. Không chặn: nếu không có ý
  kiến, agent giữ (100, 8000) và ghi rõ là observed.
- [ ] **Chuỗi chữ giao diện cho `Unknown`** (thuộc L4c, hỏi trước để khỏi làm
  lại): `BandTooLow` → trỏ sang so sánh tương đối; `BandTooHigh` → bảo đo
  full-range. Chủ nhân có muốn duyệt câu chữ, hay để L4c đề xuất rồi duyệt sau?
- [ ] **Có công bố giới hạn multi-way ra tài liệu người dùng không?** Một thùng
  2-way có driver đảo cực theo thiết kế là **ill-posed cho mọi absolute polarity
  checker**, kể cả Smaart/OSM/REW — họ ship và không nói. Nói ra là trung thực
  hơn đối thủ; cũng là tự nêu một giới hạn đối thủ giấu. **Quyết định marketing,
  không phải kỹ thuật.**

---

## Đã trả lời

- [x] **G21 tầng 1 — ship absolute polarity hay không (B1 vs B2).**
  → **B1**: ship, kèm gate, docs nói thật envelope. 2026-08-30, trong phiên thi
  công. Lý do quyết: mọi đối thủ ship G21 **không gate**, nên gate nào cũng giảm
  nói dối so với thị trường.
- [x] **Biến gác: margin hay hộp hai band edge.**
  → **Hộp hai band edge.** 2026-08-30. Margin từ chối 74–88% box tốt.
- [x] **G21 tầng 3 — workflow "đo main → đo sub tương đối".**
  → **Duyệt sửa**: chuyển sang G17 như câu hỏi PHA. 2026-08-30.
- [x] **Ngôn ngữ báo cáo.** → Technical terms giữ nguyên tiếng Anh, không dịch
  sang tiếng Việt. 2026-08-30.
