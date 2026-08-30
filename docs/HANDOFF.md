# HANDOFF — cho phiên tiếp theo

*2026-08-28, cuối phiên orchestrator kế nhiệm. Đọc file này + `docs/reports/002`
+ `docs/plans/MASTER-EXECUTION-PLAN.md` trước khi đọc bất kỳ dòng code nào.*

---

# 2026-08-30 — lane L4a, MERGE GIỮA CHỪNG (lane CHƯA đóng)

**Đọc mục này trước. Mọi mục bên dưới là lịch sử của các ngày trước đó.**

17 commit, `408d0a5..acaae9a`, đã merge vào `main` cục bộ. Lane L4a xây được
**Task 1, 2, 3, 4 và Novak**; **Task 5 và Task 6 chưa làm**. Kế hoạch thi công
đầy đủ ở `docs/plans/2026-08-30-L4a-sweep-ir-impl-plan.md`, quyết định ở
`docs/dsp/2026-08-30-sweep-ir-l4a.md` — đọc record TRƯỚC plan.

## Baseline đo được (dán từ lệnh, trên cây đã merge)

```
cmake --build build-l2 --config Release --parallel --clean-first  -> 0 warning /W4
ctest --test-dir build-l2 -C Release                              -> 334/334, 0 failed
rta_core_has_no_framework_deps                                    -> OK, 76 file
filter_design_has_no_polynomial_form                              -> OK, 87 file
core_makes_no_class_1_claim                                       -> OK,  9 file
rta_platform_types_has_no_framework_deps                          -> OK,  5 file
app_measure_has_no_framework_deps                                 -> OK, 29 file
```

Đây là cấu hình **`RTA_BUILD_APP=OFF`** (thư mục `build-l2`, gitignore). Cấu
hình `ON` **chưa được đo lại trong phiên này** — đừng cộng gộp hai cấu hình,
đừng đoán số cho cấu hình ON.

## Đã hạ cánh

| | |
|---|---|
| `rta::ir::deconvolve` | giải chập tuyến tính, giữ nguyên vùng thời gian âm, mang `originIndex` và `harmonicSpacingL` |
| `inBandNormalisation` / `bandFlatness` | vô hướng chuẩn hoá trong băng, và **độ phẳng đạt được** — không ai được phép giả định nó bằng 0 |
| `rta::ir::analyseSpectrum` | FFT vùng nhân quả, cửa sổ bắt đầu **trước** t=0 hai chu kỳ của `f_lo`, đã khử pha tuyến tính của lead-in |
| `Sweep::fadeInOctaves` | kẹp fade-in theo **octave** (mặc định 2.0) — ba tầng floor, tầng rộng nhất thắng |
| Novak synchronisation | `f1·L` làm tròn về số nguyên; `durationSec()` trả thời lượng THẬT |
| `buildInverseFilter` | **xoá lớp Tukey thứ hai**; sàn fade-out hai chu kỳ tại `endHz` |

## Việc còn mở

**Task 5 — Polarity. ĐÃ KHẢO SÁT XONG (step 0), CHƯA THI CÔNG. ĐỪNG LÀM THEO
PLAN HIỆN TẠI — plan đã bị chính khảo sát của nó bác.**

Step 0 chạy 2026-08-30 theo đúng điều kiện chủ nhân kèm khi duyệt G21, và kết
quả bác plan. Đọc `docs/dsp/2026-08-30-sweep-ir-l4a.md` **decision 6b** trước
mọi thứ khác. Tóm tắt cái gì đổi:

| plan hiện tại nói | khảo sát đo được |
|---|---|
| gate = `minBandwidthOctaves = 2.5` | **không hằng số bề rộng nào sống**; `butter(8)` 1000–16000 Hz đo 4.18 oct vẫn trả lời ngược |
| `PolarityResult` không có field lý do | phải có `Refusal` enum, lý do **có hướng** |
| `arrivalFraction = 0.5`, chưa nói vì sao | **0.5 là bắt buộc**; `0.2` bị linear-phase FIR bác (pre-ring đối xứng) |
| `arrivalBandwidthOctaves` FFT cửa sổ 50 ms cố định | **hỏng**: sub 50–71 Hz đọc ra 46.9–18270 Hz, sai ~90 dB. Phải adaptive |
| `confidenceDb = 200.0` khi không có noise window | sentinel giả — phải là `Refusal::NoNoiseEstimate`, KHÔNG throw (L=0 là output hợp lệ của `deconvolve`) |
| test narrowband `CHECK(confidenceDb > 20.0)` | xanh nhờ sentinel 200.0 — test vô nghĩa |

**Cái phải xây thay vào:** gate = hộp hai band edge trên **measured** edges
(`low ≤ 100 Hz` VÀ `high ≥ 8 kHz`), stateless, không hysteresis, không widening
constant trong `core/`. `margin` ship như **số hiển thị**, không phải gate, và
được phép > 1. Estimator phải mang cả ba fix (adaptive window ≥10 chu kỳ của
low edge; clamp vào excited band — nên `Deconvolution` phải mang band edges;
nội suy crossing giữa hai bin). Bản tham chiếu để C++ transcribe:
`tools/probe_polarity_edges.py`.

**Ba mệnh đề scope phải vào header comment, không được bỏ:** multi-way có
driver đảo theo thiết kế là ill-posed cho MỌI absolute polarity checker (kể cả
đối thủ); minimum-phase FIR mới thử một construction; mid-band resonance Q cao
chưa khảo sát.

**Task 6 — closeout.** Chưa làm. Gồm: golden vector cho `rta::ir`, đọc lại số
guard, và đồng bộ tài liệu. Hai việc cụ thể phải mang theo:

1. **Hai bất đối xứng Python/C++** trong bộ sinh golden, chưa kích hoạt nhưng sẽ
   thành bẫy: `tools/gen_generator.py` kẹp fade ở `len//2` còn C++ không có kẹp
   tương đương; `raw` của Python có biên độ đỉnh 1.0 còn C++ nhân `10^(-6/20)`.
   Cả hai **vô hại CHỈ VÌ** field duy nhất được tiêu thụ là một tỉ số. Thêm một
   field không phải tỉ số vào golden này là kích hoạt cả hai.
2. Đo lại cấu hình `RTA_BUILD_APP=ON`.

**Chưa merge lên `origin`.** Sau merge, `main` cục bộ đi trước `origin/main`
**57 commit** — đo bằng `git rev-list --count origin/main..main`, không phải
cộng nhẩm. Chủ nhân đã chủ động chọn chưa push.

## Quyết định của con người

**G21 — ĐÃ DUYỆT 2026-08-30 qua đường chuyển tiếp, rồi ĐÃ XÁC NHẬN VÀ SỬA HAI
LẦN trong phiên thi công cùng ngày.** Nguồn đầy đủ:
`docs/dsp/2026-08-30-sweep-ir-l4a.md` **decision 6b** + section "Owner decisions
recorded". Mục này chỉ là con trỏ; đừng sửa số ở đây.

Bản chuyển tiếp ban đầu, ba tầng — **tầng 1 và tầng 3 đã bị đo và phải sửa**:

1. ~~Task 5 xây polarity TUYỆT ĐỐI với cổng bề rộng **2.5 octave**~~
   → **SAI, đã bỏ.** 2.5 octave là đáy của một lưới một-family-một-order
   (`butter(4)`). Mở rộng khảo sát: `butter(8)` 1000–16000 Hz đo được **4.18
   octave** và vẫn trả lời ngược. Thay bằng **hộp hai band edge**:
   `low ≤ 100 Hz` VÀ `high ≥ 8 kHz`, trên **measured** edges. Yêu cầu "Unknown
   kèm lý do" GIỮ NGUYÊN và mạnh hơn: lý do có hướng
   (`BandTooLow` → sub, dùng tương đối; `BandTooHigh` → horn, đo full-range).
2. **Polarity TƯƠNG ĐỐI** — giữ, nhưng ~~"đúng ở mọi bề rộng băng"~~ **SAI**.
   Tuyến tính chỉ chứng minh ca **cùng một hệ** đo trước/sau. Xuyên qua một
   crossover nó đọc SAI DẤU ở order 2 và 4 khi đấu đúng. Cần gate riêng:
   `ρ = |xcorr peak| / √(E₁E₂)`, chịu được SNR 20 dB, lệch 30 ms, phòng vang tới
   D/R = −6 dB. Việc của L4b.
3. ~~Workflow dẫn dắt "đo main → đo sub tương đối với main" để L7~~
   → **ĐÃ SỬA, chủ nhân duyệt trong phiên thi công 2026-08-30.** Chuyển sang
   **G17 như câu hỏi PHA**: offset pha đúng tại crossover phụ thuộc topology
   định trước (LR 0°, BW2 180°, BW lẻ 90°) mà phép đo không khôi phục được.

Điều kiện chủ nhân kèm khi duyệt ("mở rộng khảo sát sang họ lọc và bậc khác
trước khi tin nó") **đã thực hiện, và chính nó bác plan** — đó là lý do lane này
không viết dòng code nào cho Task 5 trong phiên đó.

**Còn chờ, không ai quyết được thay:**

- Mua **ISO 2969:2015 / SMPTE ST 202:2010** (bảng dung sai X-curve, L5b) và
  **IEC 60268-16** (STI, L4d). Cả hai lane đứng yên vì chúng.
- **ISO 18233:2006** — tiêu chuẩn đúng cho quy tắc độ dài capture. Record chỉ
  *nêu tên* nó và không trích một dòng nào, vì chưa ai mua. Đừng trích.
- Tên sản phẩm chính thức.
- Ba câu hỏi giao diện còn treo từ L5c (cap 3 pane, dải màu spectrograph,
  unwrap có hiện trace pha đã lưu không).

## Người có thể tự chạy gì

Thứ đáng chạy trước tiên, và là thứ duy nhất **không cần build**:

```bash
"D:/DEV CAVE EP3/PRJ010-RTA-TOOL/.venv/Scripts/python.exe" tools/verify_l4a_measurement.py
```

Nó in ra sáu mục đo (D, E, G, H, K, L). Mục **L** là bảng 28 ô cho thấy không
ngưỡng symmetry nào tách được hai quần thể, đó là lý do cổng symmetry bị rút.
⚠️ **Trục `width` của mục L là bề rộng DANH ĐỊNH khi thiết kế bộ lọc**
(`log2(hi/lo)` tại điểm −3 dB), KHÔNG phải đại lượng code đo. Câu "từ 2.5 octave
trở lên mọi ô đọc đúng dấu" từng đứng ở đây là một hiện tượng của
`butter(4)` — mở sang họ và bậc khác thì nó sập. Xem decision 6b.

Bốn lệnh còn lại, cùng venv, không lệnh nào ghi file:

```bash
"D:/DEV CAVE EP3/PRJ010-RTA-TOOL/.venv/Scripts/python.exe" tools/probe_polarity_edges.py
```

In ra ba defect của band-edge estimator và cái gì đổi khi sửa — dễ đọc nhất là
dòng sub 50–71 Hz: `46.9 – 18270 Hz` (fiction) → `51.5 – 72 Hz`.

`tools/probe_polarity_bandwidth.py` (trục octave và vì sao không hằng số nào
sống), `tools/probe_polarity_margin.py` (trục margin, **peak-sign rule** — đọc
docstring attribution trước khi trích số), `tools/verify_l4a_pulse.py`
(mục A B C F I J), `tools/probe_sweep_fade.py` (hình học xung, luật fade).

Muốn thấy test chạy:

```bash
ctest --test-dir build-l2 -C Release --output-on-failure
```

**Chưa có gì để NHÌN.** L4a nằm hoàn toàn trong `core/`; không widget, không
ảnh chụp. Thứ đầu tiên người vận hành nhìn thấy được thuộc về L4c.

## Bẫy phiên này trả học phí (ngoài danh sách cũ bên dưới)

13. **Chạy script Python không phải chạy test.** Ba vòng làm tài liệu, tôi chạy
    ba script verify và báo "cây sạch" — đúng về git, nhưng
    `filter_design_has_no_polynomial_form` đã ĐỎ suốt ba commit vì một
    `scipy.signal.lfilter` trong `tools/`. Guard quét cả `tools/*.py`. Chạy
    `ctest` sau bất kỳ thay đổi nào dưới `tools/`.
14. **Một field golden không ai đọc là một tripwire không tồn tại.** `peak_index`
    được ghi từ ngày đầu và **không test nào đọc**. Bốn commit của lane này viện
    dẫn "peak_index không được nhúc nhích" như một tripwire; nó chỉ sống trong
    việc một con người nhìn diff. Đã vá ở `82ece26`. Grep xem field golden nào
    thực sự có người đọc trước khi tin nó.
15. **Fixture tối ưu cho số ngoan có thể đo được số không.** Xem
    `memory/a-fixture-can-be-too-well-behaved-to-fail.md`.
16. **Fetch một trang web không phải đọc một tiêu chuẩn.** Record từng trích
    "AES-2id" cho các khuyến nghị về sweep. AES-2id là hướng dẫn về **giao diện
    số AES3** — dây tín hiệu. Nguồn là một trang web tự nhận. Kiểm phạm vi tiêu
    chuẩn trước khi trích tên nó.
17. **Hai đặc tả có thể mâu thuẫn, và code sẽ theo cái cũ.**
    `plans/2026-08-27:317` bắt fade inverse filter lần hai; `Sweep.h:44-50` tả
    chỉ thừa kế. Code theo plan, nên header **nói dối về code** suốt bốn ngày.
    Đã đánh dấu supersede. Khi sửa một hành vi, grep xem tài liệu nào ĐANG yêu
    cầu hành vi cũ — nếu không, phiên sau khôi phục nó với đầy thiện chí.
18. **Fade đối xứng làm mọi cấu trúc trùng nhau.** Không test nào bắt được lớp
    fade thứ hai vì mọi fixture đều dùng fade-in = fade-out. Khi kiểm một cấu
    trúc, dùng cấu hình **bất đối xứng**.

---

**Cập nhật 2026-08-29 (lần hai — lane L5c ĐÃ XÂY XONG).** Mười nhiệm vụ, 35
commit, `78ef14f..e14d0a0`. Hợp thể Bode (ribbon coherence + pane biên độ +
pane pha trên MỘT trục tần số dùng chung), workspace 1–3 pane lưu xuống đĩa, và
**cầu nối đầu tiên từ `app/` tới engine dual-FFT** — thứ đã xây và test xong
trong `core/` nhưng chưa từng có ai gọi. Số đo ở block baseline bên dưới. Mục
"Việc còn mở" đã viết lại: khoản nợ mà file này mang suốt hai phiên
("stored-trace CHƯA NỐI") **nay đã đóng**.

**Cập nhật 2026-08-29 (lần một):** lane L2 (xương sống P2) đã xây xong ở phiên
sau file này được viết — xem `docs/reports/003-dual-fft-engine.md`. Phần bên dưới là ảnh
chụp cuối phiên 2026-08-28 và giữ nguyên như lịch sử của ngày đó (mục "Hôm nay
hạ cánh" nói L2 "bắt đầu từ trạm 3" — đúng tại thời điểm viết, không còn đúng
hiện tại). "Việc còn mở" và "Lane tiếp theo" bên dưới đã được sửa để phản ánh
trạng thái hiện tại; baseline đo được có thêm một mục cho cấu hình đo ở lane L2.

## Baseline đo được (dán từ lệnh, không từ trí nhớ)

Đo lại cuối phiên, trên đúng cây đã commit:

```
ctest --test-dir build -C Release        -> 226/226, 0 fail      (đầu ngày: 149/149)
clean rebuild /W4                        -> 0 warning
git rev-list --count c573d47..HEAD       -> 24 commit chờ merge vào main
rtatool_snapshot shots 1100 760          -> exit 0, 6 PNG
shots/rta-view.png (md5)                 -> d3e698964c8ffd7bcf2148b154f96e3d
```

**226 ở trên là cấu hình `RTA_BUILD_APP=ON`, đo cuối phiên 2026-08-28 trên cây
đã commit.** Lane L2 (2026-08-29) đo lại cấu hình `RTA_BUILD_APP=OFF` trên cây
CHƯA COMMIT của phiên đó và ra một số khác — con số đó, lệnh đo, và lý do hai
cấu hình không cộng gộp được nằm ở đúng MỘT chỗ:
`docs/reports/003-dual-fft-engine.md`. Cấu hình ON **chưa được đo lại** trong
phiên L2; không đoán số đó ở đây hay bất cứ đâu khác.

**Đo lại cuối lane L5c (2026-08-29), trên cây ĐÃ COMMIT tại `e14d0a0`.** Hai
cấu hình, đo riêng, **không cộng gộp** — chúng phủ hai tập target khác nhau:

```
ctest --test-dir build-l5c     -C Release   -> 357/357   (RTA_BUILD_APP=ON)
ctest --test-dir build-l5c-off -C Release   -> 317/317   (RTA_BUILD_APP=OFF)
measure_has_no_framework_deps               -> OK, 29 file quét (trước lane: 21)
shots/rta-view.png (md5)                    -> d3e698964c8ffd7bcf2148b154f96e3d
```

`rta-view.png` **không đổi** qua cả lane — đó là bằng chứng `RtaView` vẫn vẽ
đúng như cũ. `shots/transfer.png` và `shots/workspace.png` là hai ảnh mới và
đều tất định; `main-live.png` đổi như dự kiến (bảng kênh giờ có MEAS/REF) và
**không tất định theo thiết kế** — nó render từ một thread thời gian thực, nên
đừng ghim băm cho nó.

**Có BA guard framework khác nhau, đừng nhầm chúng với nhau:**

```
core_has_no_framework_deps            -> OK (70 files scanned)
measure_has_no_framework_deps         -> OK (29 files scanned)   <- app/src/trace + view + measure
platform_types_has_no_framework_deps  -> OK (5 files scanned)
```

Nói "guard đếm bao nhiêu" mà không nói guard nào là một cái bẫy nhỏ của chính nó.

**Đo lại 2026-08-29** (`ctest -R "framework_deps|coherence_gate|polynomial|class_1" -V`):
53 -> 70 và 19 -> 21. Số cũ đã mục nát đúng như bẫy #10 cảnh báo — L2 thêm
file vào `core/`, và `PhaseUnwrap.*` được thêm vào danh sách của
`measure_has_no_framework_deps` sau khi phát hiện guard đó KHÔNG canh chúng.

**Từ 2026-08-29, tổng số guard framework/representation là SÁU, không phải BA**
— ba guard trên, cộng thêm
`filter_design_has_no_polynomial_form`, `core_makes_no_class_1_claim` (đã có từ
trước, không nằm trong danh sách "BA guard" ở trên vì chúng canh biểu diễn DSP
chứ không phải include framework), và guard mới `coherence_gate_is_not_bypassed`.
Danh sách đầy đủ và guard nào canh gì ở `docs/reports/003-dual-fft-engine.md`.
Báo cáo đó cũng ghi `measure_has_no_framework_deps` từng bỏ sót
`PhaseUnwrap.*` khỏi danh sách file quét trước khi được vá — nên số **19 file
quét ở trên có thể đã lỗi thời**; số mới chưa được đo lại, không đoán ở đây.

## Hôm nay hạ cánh những gì

**1. Phase 1 — T12 từ bảy bước tay xuống còn hai.**
Lý do thật khiến M1-M7 phải chạy tay không phải phần cứng, mà là `AudioIo` chưa
từng có target test nào: `rta_platform_tests` chỉ link `rta::platform_types`,
tức nửa không biết JUCE. Nhưng `AudioIo` kế thừa **public**
`juce::AudioIODeviceCallback` — nên "rút dây giữa chừng" ở tầng code chỉ là một
lời gọi `audioDeviceError()`. Target mới `platform/tests_juce/` dùng một
`juce::AudioIODevice` giả, không mở thiết bị nào, và phủ M1/M3/M4/M5/M6.

Nó **bắt được một lỗi thật ngay lần chạy đầu**: `audioDeviceError()` xoá
`running_` và ghi fault nhưng không hạ cờ hoạt động của capture bus, trong khi
cả hai đường chết thiết bị còn lại đều hạ. Callback bắn sau khi thiết bị chết
vẫn được nhận và ghi vào ring của một thiết bị không còn tồn tại. **Phiên chạy
tay không thể bắt được lỗi này** — M5 bảo người kiểm tra xác nhận có fault, app
còn sống, không tự kết nối lại; cả ba đều đúng, còn cờ `isActive` thì không hiện
trên màn hình.

Trước đó M1 và M3 còn **không quan sát được**: M1 đòi một lời giải thích chưa
từng được vẽ, M3 đòi `framesAnalysed` mà không widget nào hiển thị. Đã vá.

**2. L2 có decision record.** `docs/dsp/2026-08-28-dual-fft.md`, sáu quyết định
kèm cái giá của phương án bị loại. Lane đó **bắt đầu từ trạm 3**, không phải
trạm 1.

**3. L5 tách bốn, L5a xây xong.** Spec ở
`docs/specs/2026-08-28-trace-library-and-session.md`, kế hoạch tám nhiệm vụ ở
`docs/plans/2026-08-28-L5a-trace-session-impl-plan.md`. Gồm: mô hình trace theo
trường có-hoặc-không, rút gọn theo cột, codec phiên, kho phiên ghi nguyên tử,
thư viện + bộ đếm revision, cổng vẽ lại hai vế + tầng ảnh đệm, một target test
JUCE cho lớp view, và một lượt nối liền các bin ở dải thấp.

## Việc còn mở

**Cần tay người, không phải thêm agent:** M2 (nói vào mic thật, xem bar nhảy) và
M7 (cáp loopback vật lý). Phiếu ở `docs/reports/T12-hardware-run.md`, bản dựng
ASIO sẵn ở `build-asio/`. Không chặn lane nào.

**~~Đường stored-trace được xây nhưng CHƯA NỐI~~ — ĐÃ NỐI (2026-08-29, L5c
nhiệm vụ 10).** `MainComponent` giờ sở hữu một `TraceLibrary` và trao nó cho mọi
pane qua `WorkspaceView`. Test `"the library reaches every pane"` khẳng định
**định danh** (`rta->library() == &library` và `transfer->library() == &library`),
không phải khác-null, và khẳng định `library() == nullptr` *trước* lời gọi để
phép khẳng định sau đó có thể sai được. Đó đúng là phép khẳng định lẽ ra đã bắt
được khoảng trống này hai phiên trước.

**~~`app/` chưa bao giờ gọi engine dual-FFT~~ — ĐÃ NỐI (L5c nhiệm vụ 5 và 6).**
`measure::Snapshot` giờ mang `std::optional<TransferBlock>`;
`Analyser::pushPair` lái `DualFftEngine` và chuyển radian sang **độ đúng một
lần**, tại đúng một chỗ. `AnalysisThread` rút hai ring **đồng bộ** —
`drainRole` cũ rút cạn từng ring độc lập, và một callback rơi vào giữa hai lần
gọi sẽ ghép reference *t* với measurement *t*−hop, tức 42 ms lệch vĩnh viễn
giết coherence ở tần số cao trước, đúng triệu chứng mà người vận hành sẽ đọc
thành loa hỏng.

**L2 (2026-08-29) và L5c (2026-08-29) đều đã xây xong.** L2: tám nhiệm vụ,
`docs/reports/003-dual-fft-engine.md`. L5c: mười nhiệm vụ, số liệu ở block
baseline phía trên — không lặp lại ở đây. **(Đoạn dưới đây đã LỖI THỜI từ 2026-08-30: L4 đã có decision record và L4a đã xây một phần. Xem mục 2026-08-30 ở đầu file.)** Lane tiếp theo, theo thứ tự
(chi tiết ở "Suggested opening order" trong
`docs/plans/MASTER-EXECUTION-PLAN.md`): **L4** (sweep/IR — cần một lượt nghiên
cứu trạm 1 trước, nó chưa có decision record), rồi **L3** + **L6b**, rồi **L7**
giờ cả L2 và L5c đã vào. **L5b** vẫn chờ mua chuẩn.

## Người có thể tự chạy gì, và sẽ thấy gì

Không cần phần cứng nào cả.

```
cmake --build build-l5c --config Release --target rtatool_snapshot --parallel
```

```
build-l5c/app/rtatool_snapshot_artefacts/Release/rtatool_snapshot.exe shots 1100 760
```

(Từ Git Bash phải gọi qua `cmd //c`; gọi thẳng trả 127.) Đọc `shots/`:

- **`transfer.png`** — hợp thể Bode: ribbon coherence, pane biên độ, pane pha,
  cả ba thẳng hàng trên cùng những cột pixel tần số.
- **`workspace.png`** — đồ thị dải tần trên, hợp thể Bode dưới, trong một
  workspace hai pane.
- **`rta-view.png`** — **phải không đổi**; đó là phép kiểm rằng lane này không
  đụng vào thứ vốn đã chạy.

**Chạy app và xem một hàm truyền được ĐO thật.** Mở `rtatool.exe`, bật
SYNTHETIC. Kênh 0 là Measurement, kênh 1 là Reference, đặt tự động. Kênh đo
mang một delay 4 mẫu và một nền nhiễu −30 dB, nên:

- pane biên độ nằm phẳng quanh 0 dB,
- pane pha dốc xuống rồi wrap — **−30° tại 1 kHz, −120° tại 4 kHz**, đúng dạng
  đóng φ(f) = −360·f·D/fs,
- ribbon coherence sáng ở dải giữa và tối đi nơi nhiễu lấn át,
- và các đường trace cũng mờ đi ở đúng những chỗ đó.

Nếu pane pha là một vạch phẳng ở 0°, phép đổi radian→độ đã mất. Nếu coherence
sập ở tần số cao mà chỗ khác không sao, phép rút hai ring không còn đồng bộ.

**Ba điều không test nào chứng minh được — phải nhìn bằng mắt** (record §8):
một đường ở alpha 0.25 có đọc ra là *thứ yếu nhưng vẫn hiện diện* trên nền
graphite không; tỉ lệ 5:3 có đúng ở khoảng cách hai mét không; một dải pha kín
chiều cao có đọc ra "không phân giải nổi" thay vì "hỏng" không.

## Nợ kỹ thuật L5c để lại — đã phân loại, không có cái nào chặn merge

Lượt review toàn nhánh đã xét từng khoản. Bản đầy đủ, cùng các bẫy và chín
tuyên bố-test-sai mà lane này tìm ra, nằm ở `docs/reports/004-display-layer-l5c.md`.

- **`MainComponent.cpp` không link vào target test nào**, nên phép reset vai trò
  kênh khi rời SYNTHETIC không có test tự động. Reviewer đã xét cả phương án
  tách hàm tự do và kết luận nó *không* đóng được lỗ: rủi ro nằm ở **chỗ gọi**,
  không ở logic reset. Đóng thật thì cần một target test dựng được
  `MainComponent` mà không có thiết bị âm thanh. **Đây là khoản đáng làm nhất.**
- `frequencyAxis()` trả trục đảo ngược với bề rộng dưới ~66 px — **đã vá** ở
  `e14d0a0` cùng lỗi mực tràn lề.
- Không có gì khẳng định dòng chữ `STORED PHASE HIDDEN` tồn tại; xoá dòng đó
  vẫn xanh. Nó cũng vẽ khi thư viện rỗng, tức không giấu gì cả.
- Nhãn **và lưới** pane pha chen nhau khi unwrap lớn: `drawGrid` vẽ một đường
  mỗi 10 đơn vị, nên delay 100 mẫu (~15000°) thành ~1500 đường mỗi lần paint.
  Bước nhảy phải thích ứng theo `(dbTop−dbBottom)/paneHeight`, cho cả hai.
- `Layout.cpp` còn một phép kẹp trọng số **bất khả quan sát** — không test dựa
  trên đầu ra nào bắt được. Xoá nó, hoặc để lại kèm comment nói rõ là phòng thủ
  và không quan sát được. **Đừng cố viết test cho nó** — test đó không tồn tại.
- `coherence_gate_is_not_bypassed` chỉ quét `core/`. Reviewer kết luận **chấp
  nhận được**: phía `app/` đã có test hành vi (`"coherence is withheld until
  enough averages exist"`), mạnh hơn một phép grep. Ghi lại để không ai "sửa"
  guard theo phản xạ.

## Quyết định CHỜ NGƯỜI — không tự quyết

- **Mua ISO 2969:2015 hoặc SMPTE ST 202:2010** — bảng dung sai X-curve cho L5b.
  Hình dạng đường cong tra được công khai; **dung sai thì không**, và một dung
  sai đoán ra là một tuyên bố Class sai.
- **Mua IEC 60268-16** — STI cho P4b. Cùng lý do.
- Tên sản phẩm chính thức (đang là "RTA Tool" working name).
- **Unwrap có nên hiện các trace pha đã lưu không?** (record §5a, câu hỏi 3.)
  Hiện tại: ẩn, và pane nói ra là đang ẩn. Muốn hiện thì phải unwrap từng trace
  lưu, rồi quyết trục có giãn ra để ôm chúng không — với vài capture ở các delay
  khác nhau, dải đó chạy tới hàng nghìn độ và đường live thành một vạch phẳng
  giữa pane. Đây là câu hỏi "một pane có chứa nổi hai delay không", không phải
  chi tiết cài đặt.
- **Cap 3 pane và workspace có tên** (record câu hỏi 1) và **dải màu cho
  spectrograph** (câu hỏi 2) vẫn đang chờ.

## Bẫy đã trả học phí (đừng trả lần hai)

1. **Thông báo hoàn tất của agent con nổi lên SESSION ROOT**, không tới
   coordinator đã spawn nó — coordinator phải poll file, cấm chờ tin nhắn.
2. **Relay giữa agent chỉ mang sự kiện định tuyến**; mọi thuộc tính kỹ thuật do
   bên nhận đọc từ đĩa.
3. Hai file CMake của core là điểm tranh chấp; một commit tích hợp tuần tự.
4. `cmd //c` để chạy exe từ Git Bash (gọi thẳng = exit 127); app đang mở giữ
   lock .exe — taskkill trước khi build lại.
5. Heredoc bash chứa văn bản dài/đặc biệt hay vỡ ngầm; dùng Write tool cho file.
6. Guard cắn chéo track là TÍNH NĂNG — conform, đừng đục lỗ.
7. Literal trong tài liệu plan là gợi ý, không phải oracle: test khẳng định
   CÔNG THỨC.
8. **Venv nằm ở checkout chính, worktree KHÔNG thấy nó.**
   `D:\DEV CAVE EP3\PRJ010-RTA-TOOL\.venv` có numpy 2.5.2 + scipy 1.18.1 và đã
   sinh sáu golden vector, nhưng `.venv` bị gitignore nên `git worktree add`
   không mang theo. Gọi interpreter đó bằng đường dẫn tuyệt đối; đừng dựng venv
   thứ hai, đừng kết luận máy thiếu numpy.
9. **`build-*/` đã được thêm vào `.gitignore`.** Nếu thấy `git status` đầy file
   object thì kiểm tra dòng đó còn không.
10. **Con số trong tài liệu mục nát nhanh hơn ta tưởng — kể cả trong tay người
    vừa viết ra cảnh báo.** Sáng nay tôi sửa ba con số mâu thuẫn (46/149, 119,
    103) về 159. Đến chiều, sau tám nhiệm vụ, **năm file lại ghi 159 trong khi
    thực tế là 226** — chỉ review toàn nhánh mới bắt được. "Nhớ cập nhật" không
    phải cơ chế; nó là ý định, và ý định không sống qua một ngày làm việc.
    - **Mỗi tài liệu giữ con số ở ĐÚNG MỘT CHỖ.** Lặp ở n chỗ sẽ sai ở n−1 chỗ.
    - **Mốc số trong plan là dự kiến.** Một vòng sửa ở nhiệm vụ 3 thêm 8 test,
      thế là mọi mốc cho nhiệm vụ 4-6 sai hết và phải nói đè trong từng lệnh phái.
11. **Build tăng dần có thể im lặng dùng lại `.obj` cũ.** Khôi phục một file
    `.cpp` bằng `cp`/`mv` có thể để mtime cũ hơn object đã build, và lần build
    sau dùng lại binary sai. Dùng `--clean-first` trước bất kỳ con số nào định
    báo cáo.
12. **Dựng sạch giờ lâu hơn hẳn** — ba target cùng biên dịch nguồn JUCE
    (`rtatool`, `rtatool_snapshot`, `rtatool_view_tests`). Tính giờ cho nó.

## Bài học lớn nhất của phiên: "xanh mà không chứng minh gì"

Bảy lần trong một ngày, một tín hiệu xanh hoá ra không khẳng định điều gì. Khác
nhau về cơ chế, giống hệt nhau về hình dạng:

| # | Thứ báo xanh | Vì sao nó vô nghĩa |
|---|---|---|
| 1 | `measure_has_no_framework_deps` | `file(GLOB_RECURSE)` trên đường dẫn sai trả về rỗng; chỉ khi TOÀN BỘ rỗng mới FATAL. Gõ sai tên file → test **im lặng ngừng canh** mà vẫn xanh |
| 2 | `ctest -R "<catch2 tag>"` | Tag không nằm trong không gian tên `-R` tìm. Khớp 0 test, báo thành công |
| 3 | Test round-trip số thực | Ba giá trị test (48000.0, 9.5, 94.0) đều biểu diễn đúng trong 6 chữ số, nên `to_string` cắt cụt mọi số khác mà test vẫn xanh |
| 4 | Test "ghi nguyên tử" | Không gọi `writeIndex` lần thứ hai để bị gián đoạn; cài đặt ghi-thẳng vẫn qua |
| 5 | Build tăng dần | Xem bẫy 11 |
| 6 | Bảng đột biến của agent | Một dòng khẳng định sai; đột biến được nêu sẽ **không** làm test đó đỏ |
| 7 | Test "dưới đáy đồ thị" | `bottom` nguyên khiến toạ độ bị kẹp rơi **một hàng ngoài canvas**, nên code có lỗi cũng cho 0 mực |

**Câu hỏi đúng để hỏi về một test không phải "nó có xanh không", mà "cài đặt sai
nào sẽ làm nó đỏ?"** Nếu không có cái nào, đó không phải test.

Và suy luận là chưa đủ — cả bảy trường hợp đều có người đã suy luận rằng chúng
ổn. **Cách duy nhất biết chắc là chạy test lên phiên bản code có lỗi**, rồi khôi
phục. Ba agent trong phiên này đã làm đúng thế và mỗi lần đều tìm ra thứ mà suy
luận bỏ sót.

## Nề nếp đã dùng và nên giữ

- Mỗi nhiệm vụ: đề bài trích từ plan → agent thi công → gói diff → agent review
  đọc **file thật** → sạch thì ghi sổ, không thì vòng sửa. Sổ tiến độ ở
  `.superpowers/sdd/<tên-plan>/progress.md` (gitignore) là thứ sống sót qua nén
  ngữ cảnh.
- **Không finding nào của phiên này đến từ một test đỏ.** Tất cả đến từ reviewer
  đọc code. Ba lỗi Critical ở nhiệm vụ codec lọt qua một bộ 186 test xanh.
- Review dùng mô hình mạnh hơn ở chỗ rủi ro cao (định dạng file, lớp view, review
  toàn nhánh) đã trả công xứng đáng: vòng đắt nhất tìm ra ba Critical.
