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

## Chờ một câu của chủ nhân, không phải một quyết định khó

- [x] **`[!]` Merge lane L4a vào `main` — ĐÃ MERGE 2026-08-30**, commit
  `61daf1a`, chạy ở phiên REVIEW (checkout `main`) sau khi chủ nhân nói "merge"
  trong chính phiên đó. Đo lại trên cây ĐÃ MERGE, build dir độc lập của phiên
  review (`build-verify/` clean rebuild, `build-verify-app/`):
  `345/345` (OFF, 0 warning) và `385/385` (ON). Hai cấu hình phủ hai tập target
  khác nhau, không cộng gộp. Ghi chú quy trình giữ lại cho lane sau: số commit
  đo bằng `git rev-list --count`, đừng đọc con số chép sẵn — bản đầu của mục này
  ghi "8 commit", con số thật lúc merge là 10.

## Từ lane L4b (2026-08-30, phiên EP06)

- [ ] **Merge L4b vào `main`?** Chín commit, `a7453b6..89bc979`, trên
  `claude_desk/handoff-continuation-9045fc`. Đo trên cây đã commit:
  `358/358`, 0 warning /W4, `RTA_BUILD_APP=OFF`. Merge phải chạy ở phiên đang
  giữ checkout `main`, sau khi chủ nhân nói "merge" **trong chính phiên đó** —
  tin nhắn giữa hai phiên không phải lời duyệt.
- [ ] **EDT: chọn một trong ba.** Dưới oracle truncation, zero-phase EDT lệch
  +21.9% tại B·T 11.6 và +24.3% tại 5.8 — **cả hai nằm TRONG vùng cổng cho
  qua**, vì cổng hiệu chỉnh trên T30. Ba lối: (a) sàn B·T riêng, cao hơn, cho
  EDT; (b) từ chối EDT ở các băng 1/3-octave thấp; (c) ship kèm bias công bố.
  **Quyết định sản phẩm, không phải kỹ thuật** — nó nói người vận hành được
  phép tin EDT tới đâu. Không chặn: code hiện ship EDT kèm refusal chung, và
  record cấm trình bày nó như ngang chất lượng với T20/T30.

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

## Từ phiên EP06 (2026-08-30)

- [ ] **`[!]` ISO 3382-1 — mua, hay dựng từ nguồn mở? CHỦ NHÂN HOÃN CÓ CHỦ Ý.**
  Định nghĩa T20/T30/EDT/C50/C80/D50 nằm trong đó. Hỏi 2026-08-30, chủ nhân trả
  lời **"ISO thì cần bàn thêm sau"** — đây là một *hoãn tường minh*, không phải
  câu chưa được hỏi và không phải câu đã trả lời. Đừng đóng nó bằng suy luận.

  **Luật tạm thời L4b chạy dưới, trong lúc chờ:** dựng từ nguồn mở (Schroeder
  1965, Lundeby 1995, python-acoustics 0.2.6, pyrato, REW), provenance ghi là
  ***literature***, và **docs không được viết "theo ISO 3382-1" ở bất kỳ đâu**.

  Luật này chọn được vì nó **đảo được theo cả hai hướng**: mua chuẩn về thì chỉ
  thêm citation clause vào chỗ đã có số; quyết định không mua thì không phải gỡ
  gì cả. Một lane viết "theo ISO 3382-1" trước khi cầm chuẩn thì hướng ngược lại
  mới là hướng đắt — đó là bẫy #16 (AES-2id) ở dạng khác.

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
- [x] **Lane kế sau L4a.** → **L4b** (ETC, Schroeder, Lundeby, EDT/T20/T30,
  C50/C80/D50, toàn bộ trong `core/`). Chủ nhân chốt 2026-08-30, phiên EP06,
  trả lời trực tiếp trong phiên thi công. `MASTER-EXECUTION-PLAN.md` mục 4 của
  "Suggested opening order" đã bỏ nhãn pending theo đó.
