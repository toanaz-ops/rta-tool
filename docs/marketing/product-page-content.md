# RTA Tool — website content package

*For the azsoundtech.com page. Owned by this repo so copy and screenshots track
the actual build; regenerate assets after each landed phase (lane L-web in the
master plan). VI is the primary voice; EN follows each block. Screenshots are
real offscreen renders from the app, not design mockups — that is a selling
point, say so on the page.*

## Hero

**VI —** Phần mềm đo và tinh chỉnh hệ thống âm thanh, sinh ra từ phòng máy của
AZ Soundtech. Dual-FFT thời gian thực, đo quét sweep, SPL chuẩn IEC — và một
điều không phần mềm nào cùng loại dám hứa: **mọi con số đều chứng minh được**.
Từng thuật toán được kiểm tự động trên CI với chuẩn tham chiếu độc lập, không
cần sound card, không cần niềm tin.

**EN —** Live sound measurement and tuning software, born in AZ Soundtech's
workshop. Real-time dual-FFT, swept-sine capture, IEC-grade SPL — and one thing
no competitor promises: **every number is provable.** Each algorithm is checked
on CI against independent references. No sound card, no faith required.

Tagline ngắn: **Đo được. Chứng minh được. / Measured. Proven.**

## Ba trụ (three pillars)

1. **Provable DSP** — VI: Bộ lọc dải 1/3-octave đạt IEC 61260 Class 1, kiểm
   1408 điểm chuẩn trên CI mỗi lần build; FFT đối chiếu NumPy; băng tần đối
   chiếu scipy. EN: Class-1 verified per build — 1408 mask breakpoints on CI.
2. **Sinh ra cho show thật** — VI: audio callback không cấp phát, không khoá,
   không FFT — không dropout đúng lúc đang chỉnh. ASIO đa kênh mặc định (điều
   các tool mã nguồn mở khác chưa làm được). EN: allocation-free callback;
   multichannel ASIO by default.
3. **Tune có đích đến** — VI: target + đường đo + phán quyết xanh/đỏ theo vùng
   tin cậy; auto-EQ và auto-delay vừa gợi ý vừa tự giải; điểm match cho biết
   khi nào là XONG. EN: target-match judgement, dual-mode solvers, and a match
   score that defines "done".

## Feature list for the page (status-tagged — the agency renders the tags)

| Tính năng | EN | Status |
|---|---|---|
| RTA 1/1–1/48 octave, chuẩn IEC 61260 base-10 | Fractional-octave RTA | ✅ shipping |
| Filterbank Class 1 (đường thứ hai, khớp máy đo SPL) | Class-1 filter bank path | ✅ shipping |
| Phổ Welch + banding trọng số (không lệch 1.76 dB như tool tự chế) | Weighted banding, ENBW-correct | ✅ shipping |
| SPL: A/C/Z, Fast/Slow, Leq, L10/50/90 | IEC 61672 metering | 🔧 building |
| Generator: pink/white/sine/sweep/MLS | Signal generator suite | 🔧 building |
| ASIO đa kênh + vai trò kênh đo/tham chiếu | Multichannel ASIO routing | 🔧 building |
| Transfer function: magnitude, phase, coherence, delay finder | Dual-FFT TF | 🗓 next |
| Đo sweep ngắn: FR + IR một phát | One-shot swept-sine measure | 🗓 planned |
| MTW đồng thời với FFT cố định | Concurrent MTW | 🗓 planned |
| RT60, STI, C50/80, gating kéo-thả | Room acoustics toolkit | 🗓 planned |
| Target-match + match score + auto-EQ | Tuning visuals & solvers | 🗓 planned |
| Đồng pha: Δφ, ghost tổng hợp, auto-delay | Phase alignment suite | 🗓 planned |
| Virtual processor + xuất FIR | Predictive summation, FIR export | 🗓 planned |

Nguồn sự thật của bảng này: docs/dsp/2026-08-28-competitive-parity.md và
docs/plans/MASTER-EXECUTION-PLAN.md — cập nhật tag khi lane đóng.

## Tính năng ưu việt sắp tới (the superiority section — the page's second act)

*Đây là mục làm trang này khác một trang RTA thường. Mỗi dòng đều đã có hồ sơ
thiết kế trong repo (docs/dsp/2026-08-28-competitive-parity.md và
docs/specs/2026-08-28-interactive-tuning-visuals.md). Agency dựng thành các
khối feature lớn có ảnh preview đi kèm (assets/preview-*.png khi có).*

### Tune có phán quyết — không tool nào trên thị trường có
- **Target-match view**: đường target + hành lang dung sai + đường đo; vùng
  khớp bật XANH, kẻ lệch tệ nhất bật ĐỎ kèm gợi ý xử lý; vùng dữ liệu không
  đáng tin bị gạch chéo và KHÔNG bị phán — phần mềm từ chối phán trên rác.
  EN: judged tuning — match turns green, worst offenders turn red with ranked
  suggestions, and untrusted data is refused judgement, not painted over.
- **Match score theo vùng (LF/MF/HF)**: tune có định nghĩa "xong". Không sản
  phẩm nào được khảo sát có điểm số này. EN: per-zone match scores give tuning
  a definition of done — surveyed nowhere else on the market.
- **Auto-EQ hai chế độ**: gợi ý từng filter cho người muốn tự tay, hoặc một
  chạm giải trọn bộ PEQ — luôn vẽ ghost DỰ ĐOÁN trước khi commit, và không bao
  giờ boost vào chỗ triệt pha (lỗi đó bị gắn cờ "lỗi phase" và chuyển đúng
  view). EN: dual-mode auto-EQ that previews before commit and never boosts
  into a cancellation null.

### Đồng pha như có thêm một kỹ sư
- **Phase view có ghost tổng hợp**: kéo delay là thấy ngay đường cộng gộp dự
  đoán — thấy combing TRƯỚC khi nó ra loa. **Auto-delay** giải delay + cực
  tính một chạm, hai chế độ như auto-EQ. EN: predicted-summation ghost while
  you scrub delay; one-tap auto-delay with polarity.
- **Wizard căn sub/main**: quy trình lặp lại nhiều nhất của nghề thành một
  luồng có hướng dẫn. EN: guided sub/main alignment.
- **Bù môi trường**: nhập nhiệt độ/độ ẩm — delay quy đổi đúng tốc độ âm hôm
  đó, và cảnh báo khi bản tune 3 giờ chiều đã trôi lúc 9 giờ tối. EN:
  temperature-aware delay and drift warnings for outdoor shows.

### Đo kiểu phòng thí nghiệm, chạy ở hiện trường
- **Sweep một phát ra cả FR + IR**; RT60/EDT, C50/C80, STI/STIPA; **gating
  kéo-thả trên IR** cho đo quasi-anechoic; **tách minimum/excess phase** — trả
  lời thẳng "EQ sửa được hay không". EN: one-shot sweep to FR+IR, drag-gating,
  min/excess-phase split.
- **MTW chạy ĐỒNG THỜI với FFT cố định** trên cùng một phép đo. EN: concurrent
  MTW + fixed-FFT engines.
- **Gộp nhiều mic theo trọng số coherence** + đo tuần tự tự động có loại
  capture hỏng. EN: coherence-weighted spatial averaging; sequenced capture
  with auto-discard.

### Mô phỏng trước, hệ thống sau
- **Virtual processor**: cộng gộp nhiều nguồn từ phép đo ĐÃ BẮT — chỉnh
  delay/cực/PEQ ảo và xem kết quả tổng trước khi phát một tiếng ồn nào; xuất
  FIR/filter cho DSP ngoài (miniDSP, v.v.). Ranh giới ghi rõ trên trang: mô
  phỏng và xuất — KHÔNG bao giờ chen vào đường tín hiệu live. EN: predictive
  summation over captured measurements, FIR export; never in the live chain.
- **SPL chuyên nghiệp**: logging + lịch sử + cảnh báo + báo cáo PDF + xem từ
  điện thoại; liều tiếng ồn IEC 61252. EN: SPL logging, alarms, PDF reports,
  phone viewing, noise dose.

## Screenshots (assets/)

- `rta-view.png` — RTA thật render offscreen: pink noise phẳng đúng lý thuyết,
  vùng thiếu phân giải được ĐÁNH DẤU thay vì vẽ liều. Caption gợi ý VI: "Phần
  mềm nói thật cả khi dữ liệu không đủ — vùng dưới 141 Hz được đánh dấu thay vì
  vẽ đại."
- `specimen.png` — design system SODIUM RACK: graphite ấm + sodium amber,
  typography đo lường. Caption: "Ngôn ngữ thị giác sinh ra cho FOH — đọc được
  từ 2 mét, trong bóng tối."
- `preview-tf.png`, `preview-target.png`, `preview-phase.png` —
  ba view tương lai dựng SỚM bằng chính code sản phẩm với dữ liệu minh hoạ:
  transfer function + coherence, target-match với phán quyết xanh/đỏ + match
  score, đồng pha với ghost tổng hợp + auto-delay. Caption chung: "Preview
  build — render từ chính app, dữ liệu minh hoạ." KHÔNG ghi là ảnh đo thật.

## Licence & positioning notes for the agency

- Mã nguồn mở **AGPL-3.0** — nói rõ, đây là điểm khác biệt với Smaart, không
  phải điều phải giấu. "Free as in trust."
- KHÔNG dùng chữ "đạt chuẩn Class 1" trần trụi cho toàn app — câu đúng là
  "filter bank verified against the IEC 61260 class-1 mask on CI" (honesty rule
  trong docs/dsp/2026-08-27-weighting-and-meters.md).
- Tên sản phẩm "RTA Tool" là tên làm việc; trang chờ tên chính thức từ chủ.
