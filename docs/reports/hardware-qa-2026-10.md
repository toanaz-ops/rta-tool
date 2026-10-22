# Phiếu QA phần cứng — 2026-10 (một phiên, ~2–3 giờ)

*Soạn 2026-10-22 bởi phiên điều phối, theo `docs/proposals/2026-10-21-fable-options.md` §5 tuần 1 mục 3. Phiếu này gộp T12 M1–M7 (`docs/reports/T12-hardware-run.md`), hàng B (VERIFY), hàng C (SWEEP), hàng ALIGN và hàng D (RT60) trong `docs/HUMAN-QA-QUEUE.md` (mục H4/H5/H6). Ngày phiên: **chưa xác định** (owner, 2026-10-21).*

Luật dự án: **không ghi "đạt" nếu chưa dán được thứ quan sát thấy.** Cột *Thấy gì thật* và *Đạt?* để trống tới khi chạy. Cột *Kỳ vọng* luôn có con số và nguồn của con số đó.

---

## 0. Chuẩn bị

**Đồ cần có:** audio interface (ưu tiên ASIO); một loa active hoặc ampli + loa; một sub nếu làm hàng ALIGN; một mic đo (omni); dây loopback từ một output vào một input (làm REF); thước đo khoảng cách (≥ 2 m); calibrator 94/114 dB nếu làm SPL (tuỳ chọn); máy có REW để so RT60 (hàng D).

**Bản dựng ASIO:** `build-asio/` trong checkout chính, dựng lại 2026-10-22 từ `origin/main` `d89c2e2` với `RTA_BUILD_APP=ON`, `RTA_ENABLE_ASIO=ON`, `RTA_BUILD_TESTS=OFF`, SDK `D:/DEV CAVE EP3/PROJECT005-AZ-handsfree/external/asiosdk`. Kết quả dựng (exit, kích thước exe, symbol ASIO) ghi ở §7.

Bấm `Esc` để xoá dòng lệnh đang dở, rồi chạy:

```bash
& "D:\DEV CAVE EP3\PRJ010-RTA-TOOL\build-asio\app\rtatool_artefacts\Release\RTA Tool.exe"
```

**Tắt `SYNTHETIC`** trước mọi bước có phần cứng. Ghi lại: tên interface, driver (ASIO/WASAPI), sample rate, buffer, khoảng cách loa–mic.

**Quy đổi dùng trong cả phiếu** (tốc độ âm c = 343 m/s ở ~20 °C):
- 1 m = 1/343 s = **2.915 ms** = **139.9 sample @ 48 kHz** = 279.9 sample @ 96 kHz.
- D (sample) = khoảng cách (m) / 343 × fs. Ví dụ 3.0 m @ 48 kHz → **D ≈ 420 sample (8.75 ms)**, cộng latency hệ thống nếu REF không lấy sau cùng một chuỗi.

**Nguồn của các con số.** Quy đổi khoảng cách–thời gian là vật lý (c = 343 m/s). Các mức, nhãn readout, băng bị từ chối và quy tắc TAIL lấy từ code và record đã ship (H4–H6). Các **ngưỡng chấp nhận** sau là đề xuất của phiên điều phối, không đến từ chuẩn hay record, và có thể chỉnh sau phiên đầu: M7 ±3 dB 40 Hz–16 kHz; C2 ±0.3 m; C3/A5 ±15 sample / ±0.3 ms; A4 ≤ 1.5 dB; D2/D3 ≤ 10 %; D4 IQR < 10 %; S2 ±0.2 dB. Không coi bất kỳ ngưỡng nào trong số này là tuyên bố "superior".

**Thứ tự đề nghị:** M1 → M7 (chuỗi audio) → C (SWEEP) → B (VERIFY) → ALIGN → D (RT60) → SPL (tuỳ chọn). Mỗi hàng sau dựa vào hàng trước: nếu M7 hỏng thì dừng, mọi thứ sau đều vô nghĩa.

---

## 1. Chuỗi audio — T12 M1–M7

| # | Làm gì | Kỳ vọng (có số) | Thấy gì thật | Đạt? |
|---|---|---|---|---|
| M1 | Chưa cắm interface. `TYPE` = ASIO, bấm `START`, rồi mới nhìn. | Một trong hai, đều hợp lệ: (a) không có driver ASIO → combo tự về `Windows Audio`, dòng warn `ASIO unavailable - using Windows Audio`; (b) có driver nhưng không có thiết bị → fault rõ ràng ở panel DEVICE. Chi tiết: `T12-hardware-run.md` §2. | | |
| M2 | Cắm interface, chọn ở `DEVICE`. Kênh 1 = `MEAS`, kênh 2 = `REF`. Nói vào mic. | Bar nhảy theo giọng; `DROPS` = **0** sau 60 s. | | |
| M3 | Đang chạy, đổi `RATE` 48000 → 96000. Phát tone 1 kHz (GEN SINE 1000 Hz −30.0 dBFS nếu H7 đã có, không thì trình phát ngoài). | Readout tần số vẫn **1000 Hz**; `… FRAMES` tụt gần 0 rồi đếm lên; snapshot đầu sau khi đổi không có cụm năng lượng cao tần (dấu hiệu ring cũ nối sai — PR #78 đã đổi sang resync gate). `DROPS` = 0. | | |
| M4 | Đang chạy, đổi `BUFFER`. | Thiết bị nạp lại, app không đơ; tiếng chạy tiếp trong < 2 s. | | |
| M5 | Rút interface giữa chừng. | Dòng fault đỏ ở panel DEVICE, app vẫn bấm được, **không tự kết nối lại**. Cắm lại, `START` → phục hồi. | | |
| M6 | Đang có tiếng, bấm `STOP`. | Không crash, không assertion. Thoát app sau đó: đóng trong < 3 s (PR #80: không còn force-kill thread). | | |
| M7 | Loopback: output 1 → input 2. Phát pink noise (GEN PINK −30.0 dBFS nếu có). | Đồ thị 1/3 octave phẳng **±3 dB từ 40 Hz đến 16 kHz** (đáp tuyến loopback của chính interface; ngoài dải đó roll-off của converter là bình thường). Mức đọc ≈ mức phát trừ gain loopback của interface. | | |

---

## 2. Hàng C — SWEEP (impulse response)

Nối: out 1 → Y-split → ampli loa **và** in 2 (REF); mic ở in 1 (MEAS); cả hai route 1. Đặt mic cách loa d = ____ m (đo bằng thước).

| # | Làm gì | Kỳ vọng (có số) | Thấy gì thật | Đạt? |
|---|---|---|---|---|
| C1 | Bấm `SWEEP`. | Nghe ~10 s sweep, **không click** ở hai đầu; sau ~16 s readout `IR +D samples (+t ms) · loop a..b dB · IRs 1/8`. | | |
| C2 | Đọc D. | **D ≈ d / 343 × fs** (48 kHz: 139.9 sample/m). Sai lệch cho phép: ± 0.3 m tương đương (± 42 sample @ 48 kHz) do tâm âm của loa và mic không trùng mốc thước. | | |
| C3 | Dời mic ra xa thêm **1.00 m**, SWEEP lại. | D tăng **140 ± 15 sample @ 48 kHz** (2.92 ± 0.3 ms). Đây là phép thử chính xác nhất của hàng: hiệu hai lần đo khử được latency hệ thống. | | |
| C4 | Hai trace SWEEP → XOVER, chọn làm HP và LP. | Đường tổng vẽ ra. | | |
| C5 | Chọn một trace STORE với một trace SWEEP trong XOVER. | Pane từ chối và nêu cả hai kích thước (`… <n>-POINT VS <m>-POINT`). | | |

---

## 3. Hàng B — VERIFY (EQ end-to-end)

SYNTHETIC từ chối VERIFY theo thiết kế, nên đây là bằng chứng end-to-end duy nhất của VERIFY.

| # | Làm gì | Kỳ vọng (có số) | Thấy gì thật | Đạt? |
|---|---|---|---|---|
| B1 | LOCATE → APPLY. | Delay áp dụng ≈ D của hàng C2 (cùng vị trí mic), sai khác ≤ 1 sample nếu vị trí không đổi. | | |
| B2 | TRANSFER → STORE → EQ → AUTO EQ. | Danh sách filter hiện ra; boost ≤ +6.0 dB, Q trong giới hạn hiển thị ở readout (`boost <= +x dB  Q <= …`). | | |
| B3 | Nhập các filter vào processor, rồi **VERIFY**. | Readout `VERIFY: x of y bins trusted, z flagged; residual a -> b dB` với **b < a** (residual giảm). | | |
| B4 | ADOPT. | Trace mới vào library; không lỗi. | | |

---

## 4. Hàng ALIGN — main + sub

Nối: out 1 → ampli main, out 2 → ampli sub; loop từ split console vào in 2 (REF); mic in 1 (MEAS); route 1.

| # | Làm gì | Kỳ vọng (có số) | Thấy gì thật | Đạt? |
|---|---|---|---|---|
| A1 | Mở ALIGN, trả lời theo preset processor (topology, đảo cực lấy từ preset). SEED chỉ khi crossing pane báo nằm trong 1/3 octave quanh tần cắt preset. | MEASURE bật khi đủ câu trả lời. | | |
| A2 | SWEEP MAIN rồi SWEEP SUB. | Mỗi IR delay ≈ khoảng cách / 343 × fs (như C2). | | |
| A3 | MEASURE. | Crossover Hz nằm trong **1/3 octave** quanh tần cắt preset; dòng "delay X by n ms"; R (so với vạch 0.5 **unmeasured**) và Δ pha. | | |
| A4 | Nhập delay vào processor, bấm SUM. | Tổng đo được nằm trên tổng dự đoán quanh fc, chênh ≤ 1.5 dB trong ±1/3 octave quanh fc. | | |
| A5 | Dời sub ra xa thêm **1.00 m**, lặp A2–A3. | Delay đề xuất tăng **2.9 ± 0.3 ms**. | | |

---

## 5. Hàng D — RT60 (so với REW)

Nối: out 1 → loa trong phòng; REF loop in 2; mic in 1 đặt **≥ khoảng cách tới hạn** (ước lượng r_c ≈ 0.057·√(V/T60) m, V thể tích phòng m³).

| # | Làm gì | Kỳ vọng (có số) | Thấy gì thật | Đạt? |
|---|---|---|---|---|
| D1 | RT60 pane, out 1, TAIL 3 s, SWEEP. | ~15 s sau bảng đầy; 63 Hz bị từ chối (băng dưới 80 Hz không tin được); 125–8000 Hz có T30 / T20 / EDT kèm range. | | |
| D2 | Đo REW cùng vị trí. | T30 các băng 500 Hz–2 kHz lệch REW **≤ 10 %**; ở 125 Hz phòng khô lệch lớn hơn là bình thường (ghi lại số). | | |
| D3 | Dời mic 2 m. | T30, T20 đổi **≤ 10 %**; EDT có thể đổi nhiều (phụ thuộc vị trí, đúng lý thuyết). | | |
| D4 | THIRD, ENSEMBLE với 3 lần sweep. | Median và IQR % hiện; IQR mid-band thường **< 10 %** ở một vị trí cố định. | | |
| D5 | Hội trường T > 2 s: TAIL 3 s rồi TAIL 6 s. | TAIL 3 s → `NoNoiseEstimate` hoặc dòng gợi ý; TAIL 6 s → có số. | | |

---

## 6. SPL (tuỳ chọn, cần calibrator)

| # | Làm gì | Kỳ vọng (có số) | Thấy gì thật | Đạt? |
|---|---|---|---|---|
| S1 | Calibrator 94.0 dB @ 1 kHz lên mic, chạy CAL. | Sau CAL, readout SPL **94.0 dB ± dung sai class của calibrator** (IEC 60942 class 1: ± 0.4 dB — đối chiếu chứng nhận của chính calibrator). | | |
| S2 | Chuyển 114.0 dB (nếu calibrator có). | Đọc **114.0 ± cùng dung sai**; chênh S2 − S1 = 20.0 ± 0.2 dB (tuyến tính). | | |

---

## 7. Kết quả dựng `build-asio/`

Dựng 2026-10-22 14:16 từ `d89c2e2`:

| | |
|---|---|
| Lệnh | `cmake -S . -B build-asio -G "Visual Studio 18 2026" -A x64 -DRTA_BUILD_APP=ON -DRTA_ENABLE_ASIO=ON -DRTA_BUILD_TESTS=OFF -DRTA_ASIO_SDK_PATH=<asiosdk> -DRTA_JUCE_PATH=<JUCE>` rồi `cmake --build build-asio --config Release --target rtatool --parallel` |
| Exit | `BUILD_EXIT=0`; configure in `ASIO SDK found at .../asiosdk` |
| Cảnh báo | 0 (regex `warning( [A-Z]+[0-9]+)?:` trên log build) |
| Exe | `build-asio/app/rtatool_artefacts/Release/RTA Tool.exe`, 12.001.792 byte |
| ASIO vào binary | chuỗi `ASIOAudioIODeviceType` có trong exe (2 lần khớp) |

Bản này **chưa có H7 GEN** (PR #88 chưa merge). Nếu phiên phần cứng diễn ra sau khi H7 merge, dựng lại bằng đúng lệnh trên để có GEN cho M3/M7.

## 8. Sau phiên

Dán kết quả vào các cột trên, commit phiếu, và mở một mục trong `docs/HUMAN-QA-QUEUE.md` cho từng hàng **không đạt** — agent sẽ triage thành lane sửa trước khi mở lane đo mới (Fable review §5 tuần 2).
