# Bàn giao trang sản phẩm RTA Tool — cho session dự án web

## Gói bàn giao gồm (tất cả trong thư mục này)

| File | Là gì |
|---|---|
| `product-page-content.md` | Toàn bộ copy VI/EN: hero, 3 trụ, bảng tính năng có status, caption ảnh, ghi chú licence |
| `assets/rta-view.png` | Screenshot THẬT render offscreen từ app (1100×760) — không phải mockup đồ hoạ |
| `assets/specimen.png` | Trang mẫu design system SODIUM RACK (1100×760) |
| `WEB-HANDOFF.md` | File này |

Nguồn gốc: repo `D:\DEV CAVE EP3\PRJ010-RTA-TOOL`, thư mục `docs/marketing/`.
Ảnh sẽ được render lại theo tiến độ build — khi cần bản mới, lấy lại từ đúng
đường dẫn này (hoặc yêu cầu phiên dev của RTA Tool render và cập nhật).

---

## PROMPT BÀN GIAO — dán nguyên khối này vào session dự án web

```
Dựng một trang giới thiệu sản phẩm mới trên azsoundtech.com cho phần mềm đo âm
thanh "RTA Tool" (tên làm việc, sẽ đổi sau — làm slug/URL dễ đổi tên).

NGUỒN NỘI DUNG (không tự bịa thêm nội dung kỹ thuật):
  D:\DEV CAVE EP3\PRJ010-RTA-TOOL\docs\marketing\product-page-content.md
  D:\DEV CAVE EP3\PRJ010-RTA-TOOL\docs\marketing\assets\rta-view.png
  D:\DEV CAVE EP3\PRJ010-RTA-TOOL\docs\marketing\assets\specimen.png
Copy 3 file đó vào repo web rồi làm việc trên bản copy.

CẤU TRÚC TRANG theo đúng thứ tự trong product-page-content.md:
  hero (headline "Đo được. Chứng minh được." / EN "Measured. Proven.")
  → ảnh rta-view.png + caption của nó
  → 3 trụ → dải số liệu tự kiểm (1408 breakpoints / 0.3 dB / 103 tests / 0 warnings)
  → bảng tính năng với chip trạng thái (SHIPPING xanh lá / BUILDING amber /
    PLANNED xám — trạng thái là dữ liệu thật, không phải trang trí)
  → ảnh specimen.png + caption → khối AGPL-3.0.

ART DIRECTION — theo design system của chính sản phẩm (SODIUM RACK), tối:
  nền #0a0b0d, panel #131519, viền #2b2f37, chữ #e8eaed, phụ #868d98,
  accent sodium amber #ff9f1c, ok #6ee7a0, danger #ff5a4e.
  Font: Saira Condensed (heading, uppercase, tracking rộng), IBM Plex Sans
  (body), IBM Plex Mono (mọi con số) — đều có trên Google Fonts.
  Trang một theme tối duy nhất, không auto light-mode.

LUẬT NỘI DUNG (bắt buộc, có lý do pháp lý/kỹ thuật):
  1. KHÔNG viết "đạt chuẩn Class 1" trần trụi cho toàn app. Câu được duyệt:
     "filter bank verified against the IEC 61260 class-1 mask on CI".
  2. Ghi rõ screenshot là render thật từ app, không phải mockup — đó là điểm bán.
  3. Song ngữ: VI chính, EN phụ ngay dưới mỗi khối, đúng như file content.
  4. Số Hz nguyên không thập phân; dB một số lẻ (quy ước sản phẩm).
  5. AGPL-3.0 nói công khai như một tính năng, không giấu.

Sau khi dựng xong: cho tôi xem preview trước khi publish.
```

---

## Quy trình cập nhật về sau

Mỗi khi một phase của RTA Tool đóng (xem docs/plans/MASTER-EXECUTION-PLAN.md,
lane L-web): phiên dev render lại ảnh, cập nhật status trong
product-page-content.md, và bên web chỉ cần đồng bộ lại 3 file nguồn.
