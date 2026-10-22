# HANDOFF — cho phiên tiếp theo

*2026-08-28, cuối phiên orchestrator kế nhiệm. Đọc file này + `docs/reports/002`
+ `docs/plans/MASTER-EXECUTION-PLAN.md` trước khi đọc bất kỳ dòng code nào.*

---

# 2026-10-21 (tối) — **H7 wave A2 (`app/generator-a`): GEN / − / + trên rail, an toàn và loại trừ lẫn nhau xong. Chờ wave B1.**

*Plan: `docs/plans/2026-10-21-h7-generator-output-plan.md` (T1–T5). Chưa có T6+ (signal/tần số, OUT, FOLLOW GEN).*

- **Mới:** `measure/GeneratorModel.*` (ceiling −12 ép trong `makeSource`, floor −60, default −30, step 1 dB), `measure/GeneratorRunner.*` (Idle/Playing/Swapping/Stopping, không bao giờ tự re-arm, timeout 500 ms), `MainComponentGenerator.*` (GEN/STOP, −, +, readout, poll 100 Hz khi chờ callback), `MainComponentGeneratorWiring.cpp`.
- **Loại trừ:** `generator_.active()` vào `outputBusy()` và env của VERIFY, KHÔNG vào `captureBusy()`. SWEEP/LOCATE/CAL/VERIFY từ chối nêu tên GEN; ALIGN nêu GEN trong câu chung (không đụng `AlignRunner.cpp`). Rời SYNTHETIC huỷ GEN.
- **Thử tay (SYNTHETIC):** bật SYNTHETIC; bấm GEN: readout `GEN: playing pink -30.0 dBFS RMS on out 1`, nút thành STOP; `+` đến `-12.0 ... (ceiling)`, `−` đến `-60.0`; khi GEN chạy bấm SWEEP/LOCATE/CAL START: từ chối nêu GEN; STOP rồi SWEEP chạy; tắt SYNTHETIC: GEN dừng, lý do `input mode changed`, bật lại vẫn dừng.
- **Biết trước:** lần bấm GEN đầu tiên fade-in là g(n)² (master và gate output cùng tăng, như `test_output_engine.cpp`); từ lần thứ hai gate đã Running nên là g(n) thuần. Layout (sửa sau verifier PR #88): GEN/−/+ và readout nằm ở masthead bên phải (STOP luôn thấy ở mọi pane), các hàng đo ở rail cao 44 px thay vì 62, nên ở 1280x800 viewport của rail còn ≥ 260 px: device combo, START/STOP và dòng fault thấy hẳn không cần cuộn (có test). Hint nhỏ dưới nhãn nút hàng đo không còn chỗ vẽ ở 44 px.
- **Tiếp theo:** wave B1 (`app/generator-b`): T6–T9 + T13.

---

# 2026-10-21 (muộn) — **Vòng sửa audit Fable xong phần HIGH/MEDIUM: #76, #78, #80 đã merge. Main = `dfbfb62`. Còn lại: nợ perf và hạng mục cần chủ dự án.**

*Entry này thay phần "Audit Fable" của entry H6 bên dưới: #78 không còn "đang mở" và finding analyser rebuild không còn "chưa có PR".*

| PR | merge | what |
|---|---|---|
| #76 | `df3d444` | decay (core): truncation của Lundeby đọc đường hồi quy chứ không đọc một sample; bound cho clarity pre-ring; `lastUsable` |
| #78 | `63bd500` | `CaptureBus` resync gate + consumer drain có ack, SPL Gap cho frame mất do resync, sweep/locate abort khi resync, fault slot + detach on fault, `pairedHopCount` |
| #80 | `dfbfb62` | analyser rebuild MỘT slot mỗi tick (hết đỉnh 424 MB), dừng thread không kill, trạng thái `recovering` của fault |

Cũng đã sửa trong các PR trên: Polarity, LS-centring, các chỗ doc-drift. LOW batch `core/audit-low-batch` (PR mở): `Weighting::responseDb/analyticDb` ném `invalid_argument` khi f không hữu hạn hoặc <= 0 (trước đây -inf), `Window::apply` ném khi lệch kích thước (trước đây cắt im lặng ở Release), `RingBuffer::write/peek` hai đoạn memcpy, bỏ `Analyser::latest_` (không code production nào đọc; `AnalysisThread::latest()` vẫn là đường publish duy nhất), CLAUDE.md không còn nói pyrato.

**Còn lại từ audit (DEFER, không làm trong LOW batch):**

- Perf (M): FFT scalar (`RealFft`), lưới `crossoverBandFit`.
- Cần chủ dự án / cần operator path: API core chết hoặc chỉ test dùng; nhánh config chỉ có một giá trị trong production. Quyết định theo gu chủ dự án, không sửa mù.

---

# 2026-10-21 — **Lane order của plan review #2 xong: H4, H5, H6 đều BUILT và operator-reachable. Main = `7b3375f` (+ #78 đang mở); PR closeout H6 + LOW batch đang mở.**

*H6 `app/rt60-ui` xong: pane RT60 (SWEEP riêng với TAIL, bảng T30 / T20 / EDT theo octave hoặc third-octave, ensemble qua nhiều sweep, EDC chart). Không còn lane nào đang mở trong thứ tự EQ-UI → SWEEP → ALIGN-UI → RT60 UI. Việc còn lại là nợ LOW, #78, và các lane NEW-LANE chưa xếp lịch.*

## Phần trăm sau H6

Cùng công thức, cùng cách đếm (unit trọng số bằng nhau) như "Status snapshot — 2026-09-29" của `docs/plans/MASTER-EXECUTION-PLAN.md`; block "After H6 wave A" ở đó có phép tính. Tính lại từ bảng 19 hàng: hàng 4 giờ là BUILT, không partial (bảng ghi "BUILT (both waves ...)"). So với entry H5 bên dưới (sau #72: 13 / 9), hàng 4 lật nên (b) tăng một và (b-strict) tăng một; (c) từ dự phóng thành số đo.

| | sau #72 (entry H5) | sau H6 (#74, #77) |
|---|---|---|
| (a) engine built | 15 / 19 = 78.9 % | 15 / 19 = 78.9 % |
| (b) operator-reachable (BUILT, gồm partial) | 13 / 19 = 68.4 % | **14 / 19 = 73.7 %** |
| (b-strict) BUILT không partial | 9 / 19 = 47.4 % | **10 / 19 = 52.6 %** |
| (c) dự phóng sau H6 | 14 / 19 = 73.7 % | 14 / 19 = 73.7 % — **đã đo, không còn là dự phóng** |

Arithmetic: 14 / 19 = 0.7368, 10 / 19 = 0.5263. (a) không đổi vì H6 là lane `app/`. Năm hàng còn lại ngoài BUILT: hàng 5 (L4c), 6 (THD/STI, chờ IEC 60268-16), 8 (targets, chờ ISO 2969 / ST 202), 12 (L-API, CORE ONLY), 19 (productization). Hàng "BUILT (partial)" vẫn được headline đếm là BUILT và nằm ngoài (b-strict); danh sách ở bảng của master plan.

## PR đã merge từ entry trước (đối chiếu `gh pr view N --json title,mergeCommit`)

| PR | merge | what |
|---|---|---|
| #70 | `87157df` | closeout H4 SWEEP (LOW L1/L2/L4/L6, amendment "H4 as built") |
| #71, #72 | `148b784`, `7fd8e4c` | H5 wave A / B (pane ALIGN, IR arrival fix, SWEEP MAIN/SUB) |
| #73 | `bc9b78d` | closeout H5 ALIGN-UI (D9/D6/D4, C1-C5) |
| #74 | `17f3010` | H6 wave A: pane RT60, `Rt60Analysis`, SWEEP tail override |
| #75 | `971daba` | sửa `-Wformat-truncation` của gcc trong `EqPaneModel` (main đỏ trên ubuntu) |
| #76 | `df3d444` | audit fix (core): truncation của Lundeby đọc đường hồi quy, không đọc một sample; bound cho clarity pre-ring; crossing search |
| #77 | `7b3375f` | H6 wave B: ensemble, EDC plot, third-octave |
| closeout H6 | (PR mở, chưa merge) | `app+docs: close out H6 RT60-UI + LOW batch` — plan/record/QA queue/handoff này; fix Z8 Z11 Z13 Z17 Z22 Z24 Z29 Z30, E2, E3, F1 |
| #78 | (PR mở, của phiên khác) | `app/capture-bus-drain-race`: `CaptureBus::prepare` không reset ring đang sống, fault slot, `pairedHopCount` |

## Audit Fable 2026-10-21 (`D:\DEV CAVE EP3\shared\audits\prj010-fable-audit-20261021.md`)

- **Đã sửa, đã merge:** #76 (Lundeby truncation seed một sample; `lastUsable` lấy block cuối; clarity pre-ring).
- **Đang bay: #78** — drain race của capture bus, fault slot, `pairedHopCount`. Các file #78 đang sửa (`CaptureBus.cpp/.h`, `AudioIo.*`, `AnalysisThread*`, `PairedDrain.h`, `DevicePanel*`, `FaultLine.h`, `MainComponent.cpp/.h`, `SyntheticInput*`, hai tài liệu audioio) KHÔNG được đụng trước khi #78 merge; mọi LOW chạm các file đó ghi `DEFER(PR #78 area)` trong bảng QA.
- **Còn mở (chưa có PR): analyser 424 MB rebuild** — theo lời phiên điều phối giao việc; phiên viết entry này không đọc nội dung finding đó trong file audit, nên không mô tả thêm. Chưa có nhánh, chưa có plan; cần một lượt research trước khi code (CLAUDE.md "research, then argue, then build").

## Human try — RT60 (đã chạy được, không cần phần cứng cho bước 1-5)

Build bằng `rtatool_snapshot` hoặc chạy `rtatool.exe`. Mỗi bước nói rõ sẽ THẤY gì:

1. **SYNTHETIC -> nút RT60 (nút pane thứ bảy) -> SWEEP** (chọn output và TAIL 3 s, mặc định). Sau khoảng 15 s bảng octave đầy, nhưng **mọi band bị từ chối kèm lý do** ("band too narrow for a decay this short (B·T ... < 6)" hoặc "no noise floor found"). Đây là cách đọc đúng: đường SYNTHETIC mặc định là band-pass + trễ 4 mẫu, không có phòng; không phải lỗi. Trace `SWEEP @ hh:mm:ss` xuất hiện trong picker.
2. **Phòng comb (T = 0.80 s mọi band)** chỉ vào được qua test hook (`restartSyntheticForTest`); xem `shots/main-live-rt60.png` và `main-live-rt60-third.png` (`rtatool_snapshot`, 1280x800). Ở đó SWEEP cho **T30 / T20 / EDT ≈ 0.80 s** ở 125-8000 Hz (63 Hz bị từ chối: ngoài trusted band từ 80 Hz).
3. **Bấm một hàng của bảng:** chart dưới bảng vẽ đường decay (dB xuống, giây ngang) của band đó, kèm hai đoạn fit EDT / T20 / T30 và con số ở legend; thẳng nghĩa là một slope, cong nghĩa là trung bình hai slope. Band bị từ chối thì chart nói vì sao không có đường. Chưa bấm: chart hiện band gần 1 kHz.
4. **Nút OCTAVE / THIRD:** bảng thành 24 hàng third-octave, 50 Hz đến 10 kHz; 50, 63, 80 Hz bị từ chối; pane ghi "analysing in the background" một lúc (worker thread), không đứng.
5. **ENSEMBLE trên 3 sweep:** SWEEP ba lần liên tiếp (mỗi lần một trace mới), bấm ENSEMBLE, tick cả ba. Bảng hiện **median** qua các capture và, từ ba capture trở lên, "IQR n%" cạnh mỗi số; tick hai thì chỉ median và header ghi "IQR needs 3 captures". Tối đa tám.
6. **Cần phần cứng (hàng "later" trong QA queue):** out 1 -> loa trong phòng, REF loop vào in 2, mic ở in 1 tại hoặc xa hơn critical distance; so với REW cùng vị trí (T30 trong dải spread của core, vài % ở mid). Chưa đo; giữ nguyên "later".

Chi tiết và mười một bước đầy đủ: `docs/plans/2026-10-20-rt60-ui-lane-plan.md` mục "Human-try row", và `docs/HUMAN-QA-QUEUE.md` mục "H6 RT60-UI".

## Baseline

Theo CLAUDE.md global rule 16, máy local chỉ chạy test hẹp; full suite là việc của CI (OFF ba OS + ON Windows) trên PR. Tally cục bộ của PR closeout nằm trong body PR, không chép lại ở đây.

## Nợ

**Máy làm được:** analyser rebuild 424 MB (chưa có PR); E4 (`p_c` đọc từ đường fit 20 dB trên cùng rồi ngoại suy ~40 dB — cần research); F2/F3 và G1 (`DEFER(PR #78 area)`, chờ #78 merge); Z21, Z26, Z27, Z28, L9, D7 (xem bảng LOW trong QA queue). NEW-LANE chưa xếp lịch: TF slot picker, multi-pane selector, L-API enable switch, generator/output control, XOVER `appliedDelaySamples`. `CaptureSequencer`, MTW storage vẫn hoãn.

**Cần người:** hàng B (VERIFY), C (SWEEP), ALIGN và RT60 trên phần cứng thật; ISO 3382-1 vẫn là hoãn có chủ ý của chủ nhân (2026-08-30).

## Pitfall học được trong đợt này

- **Một mutant có thể tương đương với code đúng:** `noisePower * tau` thay `line * tau` sống sót vì sau vòng lặp hội tụ hai số bằng nhau theo cấu trúc (crossing là nơi đường thẳng gặp noise). Chỉ tách được khi vòng lặp dừng trước hội tụ (`maxIterations = 1`) trên fixture có burst trong đoạn noise estimate (E3).
- **Cận từ công thức iid áp cho ripple xác định thì thấp hơn thực:** 0.25 dB là RMS; worst-case là tổng trị tuyệt đối trọng số LS = 0.42 dB (E2).
- **Một test evict qua đường pane tick không chứng minh prune trong `reanalyse`:** F1 gọi `onSwept` không tick pane.
- **Hook worktree chặn heredoc/redirect phức tạp:** ghi script bằng Write rồi chạy `python <file>`; `printf >>` đơn giản qua được.

## Kế tiếp

Chờ #78 merge, rồi: (1) F2/F3/G1, (2) analyser rebuild 424 MB (research trước), (3) chọn trong các NEW-LANE. Không lane tuần tự nào còn mở.

---


---

*Các entry cũ hơn (2026-10-20 trở về trước): [`docs/handoff-archive/HANDOFF-through-2026-10-20.md`](handoff-archive/HANDOFF-through-2026-10-20.md). Trích dẫn dòng cũ: xem `docs/HANDOFF.md` tại `ea539fc`.*
