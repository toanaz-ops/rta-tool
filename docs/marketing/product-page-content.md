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

## Screenshots (assets/)

- `rta-view.png` — RTA thật render offscreen: pink noise phẳng đúng lý thuyết,
  vùng thiếu phân giải được ĐÁNH DẤU thay vì vẽ liều. Caption gợi ý VI: "Phần
  mềm nói thật cả khi dữ liệu không đủ — vùng dưới 141 Hz được đánh dấu thay vì
  vẽ đại."
- `specimen.png` — design system SODIUM RACK: graphite ấm + sodium amber,
  typography đo lường. Caption: "Ngôn ngữ thị giác sinh ra cho FOH — đọc được
  từ 2 mét, trong bóng tối."

## Licence & positioning notes for the agency

- Mã nguồn mở **AGPL-3.0** — nói rõ, đây là điểm khác biệt với Smaart, không
  phải điều phải giấu. "Free as in trust."
- KHÔNG dùng chữ "đạt chuẩn Class 1" trần trụi cho toàn app — câu đúng là
  "filter bank verified against the IEC 61260 class-1 mask on CI" (honesty rule
  trong docs/dsp/2026-08-27-weighting-and-meters.md).
- Tên sản phẩm "RTA Tool" là tên làm việc; trang chờ tên chính thức từ chủ.
