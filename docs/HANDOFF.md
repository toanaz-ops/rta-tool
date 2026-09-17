# HANDOFF — cho phiên tiếp theo

*2026-08-28, cuối phiên orchestrator kế nhiệm. Đọc file này + `docs/reports/002`
+ `docs/plans/MASTER-EXECUTION-PLAN.md` trước khi đọc bất kỳ dòng code nào.*

---

# 2026-09-16 — Remote API trạm 1+2 đã viết (DOCS-ONLY, không đụng code)

Lane **L-API** (remote read-only API) — trạm 1 nghiên cứu và trạm 2 record đã
xong, nhánh `remote-api/stations-1-2` từ `6d9a53d`, PR docs-only, **CHƯA
merge**. **Trạm 3 (impl plan) là việc kế tiếp.**

- Nghiên cứu: [`docs/research/2026-09-16-remote-api-station1-research.md`](research/2026-09-16-remote-api-station1-research.md)
- Record: [`docs/dsp/2026-09-16-remote-api.md`](dsp/2026-09-16-remote-api.md)
- Câu hỏi chủ nhân: `docs/HUMAN-QA-QUEUE.md`, mục "Từ lane Remote API (2026-09-16)" — 5 câu, **không câu nào chặn trạm 3 viết plan**.

Ba điều phiên sau đừng suy lại: transport là **HTTP/1.1 + JSON over TCP** (OSC
không tải nổi một curve 2049 điểm trong một datagram 1472 byte, và blob/bundle
không cứu được); thư viện là **cpp-httplib (MIT)**, **Mongoose bị loại vì
GPL-2.0-only** không tương thích AGPLv3 và mua licence thương mại cũng không
gỡ được; và `Host`-header allowlist là phòng thủ chính chứ không phải bind
localhost — DNS rebinding làm origin khớp thật nên CORS không dính dáng.

Hai thứ **không ship được ở v1** và record nói thẳng: solver suggestions
(`EqSession`/`AlignmentWizard` chưa có instance nào trong composition root) và
SPL/Leq (`Snapshot` chỉ mang dBFS; `rta::meter::Leq` chưa có caller trong
`app/`). Điều thứ hai chặn **L6a G7**, không chặn lane này.

## Vòng verify đối kháng + fix (2026-09-17), PR #11

Verifier (không có `Edit`/`Write`) đọc lại diff tại `828c223`, đối chiếu repo ở
`6d9a53d` và fetch lại mọi nguồn ngoài. **Verdict: SOUND-WITH-FIXES, trạm 3 mở
được.** Không quyết định kiến trúc nào bị bác. Hai defect CONFIRMED, cả hai đã
sửa trong vòng này:

1. **Lập luận cookie bị đảo ngược ở bốn chỗ** (research `:42` và Part D mục 2,
   record §9, `HUMAN-QA-QUEUE.md`). Bản cũ viết "request bị rebinding là
   same-origin nên **sẽ** mang cookie của origin đó" — **ngược với nguồn được
   trích**. Cookie jar key theo **host name**, rebinding chỉ đổi cái name đó
   resolve ra IP nào, nên rebound request mang cookie của `attacker.example`.
   Quyết định "Bearer, không bao giờ cookie" **giữ nguyên**, nhưng lý do đúng là
   **ambient authority / CSRF**, còn phòng thủ rebinding là **`Host`-header
   allowlist**. Câu hỏi §14 q.4 trong QA queue đã được **đặt lại trên tiền đề
   đã sửa**, kèm một đoạn đính chính tường minh phòng khi chủ nhân đã đọc bản
   cũ.
2. **§11 acceptance test 10 được đặc tả ở một cấu hình không thể pass.**
   `-DDIRS=core;platform;ui;tools` thiếu `app`, mà sentinel
   `ALLOW IN_LIST SOURCES` (`check_no_std_atomic_shared_ptr.cmake:65`) đòi file
   được ALLOW phải nằm trong tập quét → `FATAL_ERROR` **mọi lần chạy**. Đã đổi
   thành `core;platform;ui;tools;app`, và ghi rõ guard khi đó chứng minh hai
   việc: bốn tầng dưới không có server library nào, **và** trong `app/` chỉ
   `ApiServer.cpp` include nó — tức `ApiSerialise.cpp` không include
   `httplib.h`, đúng cái §10 tách ra để chứng minh.

Năm mục non-blocking cũng đã làm luôn: §10 bỏ chữ "therefore compiled" (glob
list chỉ là textual scan — muốn compile phải thêm vào `rtatool_analysis_tests`);
§4 tính lại accounting ở **30 rps** (default của §8) thay vì 20 Hz; endpoint
spec ghép `If-None-Match` với **`ETag` server phát ra**; §11 thêm **item 13** —
một hàm format dùng chung, test chuỗi readout so với giá trị float32 của golden,
và nói thẳng nửa JavaScript của §12 constraint 2 chỉ L6a mới discharge được;
JSON examples đổi sang **shortest-round-trip float32** kèm caption. Cộng hai
nhóm citation nhỏ: JUCE `*Server*` là **ba** hit (thêm `HubPipeServer`,
`juce_Direct2DMetrics_windows.h:264`, khai bằng `struct`), và ba số dòng lệch
một (`check_no_framework_deps.cmake:50`→49, `CMakeLists.txt:71`→72,
`AudioIo.cpp:117-148`→116-148) — đã mở từng file xác nhận trước khi sửa.

**Vẫn DOCS-ONLY, chưa build gì, chưa merge.**

**Mục dưới đây vẫn là mục đọc trước tiên cho trạng thái build.**

---

# 2026-09-16 — **L7 (Solvers) CLOSED OUT. PR #9 đã merge tại `6d9a53d`. Lane report: `docs/reports/007-solvers.md`.**

**Đọc mục này trước tiên.** Toàn bộ lane L7 — OUT, FIR, DELAY, EQ (core A–D +
app E/F/G), ALIGN (Wave 3a + 3b) — đã BUILT và đã nằm trên `origin/main`.
Mảnh cuối, ALIGN Wave 3b, merge bằng **PR #9 tại `6d9a53d`**. Lane report
`docs/reports/007-solvers.md` mang đầy đủ: xây gì ở từng sub-lane, năm
amendment của các decision record (và một cái còn nợ), ρ ship KHÔNG ngưỡng vì
sao, verifier bác được gì, và các mục còn chờ CHỦ NHÂN.

`docs/plans/MASTER-EXECUTION-PLAN.md` đã có "Status snapshot — 2026-09-16":
hàng L7 → **BUILT 2026-09-16, merged**; lane kế theo chính "opening order" của
plan là **L6a (SPL-pro)**.

## Số đo — **verifier-measured**, lượt dựng lại độc lập tại `6d9a53d` đã XONG

```
6d9a53d (merge commit PR #9) -- worktree sạch, verifier dựng lại độc lập
  RTA_BUILD_APP=OFF                                            -> 649/649, 0 failed
  RTA_BUILD_APP=ON                                             -> 717/717, 0 failed
  forced fallback (-DRTA_FORCE_ATOMIC_SHARED_PTR_FALLBACK=ON)  -> 649/649, 0 failed
  "warning C" trong các build log                              -> 0
  guard xanh                                                   -> 11/11
  rtatool_snapshot                                             -> 8 PNG (bytes ở mục 4)
```

Đây **không còn là số của builder** — lượt dựng lại độc lập đã chạy xong và
xác nhận đúng hai con số builder báo, cộng cấu hình thứ ba (forced fallback).
Vẫn **không phải CI**: GitHub Actions vẫn bị chặn billing, nên bằng chứng là
ba cấu hình chạy cục bộ + một verifier độc lập, không hơn.

`git diff --stat 6d9a53d^2 6d9a53d` **RỖNG**, nên các con số trên là của cây
merge commit chứ không chỉ của tip nhánh.

Một chỗ trung thực cần giữ: **OFF baseline 624 (tại `02bd02a`) chưa ai đo trực
tiếp.** ON 692 thì verifier PR #9 đã đo trên cây sạch. 624 khép bằng số học:
`717 − 692 = 25 = 649 − 624`.

---

## Rule 12 vế 2 — người có thể tự chạy cái gì, và trông đợi THẤY gì

### 0. ĐỌC TRƯỚC: mở `rtatool.exe` lên thì thấy được gì của L7?

Câu này dễ bị bỏ sót nhất trong một closeout, nên nó đứng đầu và **đo được**,
không phải nhớ được.

- **DELAY là solver DUY NHẤT của L7 mà operator chạm tới được trong app.**
  `MainComponent` có nút **`LOCATE`** (hint: "pink noise, output ch 1,
  one-shot"), một readout delay, và nút **`APPLY`** chỉ bật khi verdict là
  `DelayVerdict::Accepted` (`app/src/MainComponentDelay.cpp`, task F2). Thứ tự
  §8 của record được tôn trọng ngay trong wiring: `disarmSource()` chạy TRƯỚC
  `suggestDelay`.
- **`rta::view::CrossoverSurface` là một model KHÔNG có mặt.** Nó JUCE-free và
  được chứng minh ở cấu hình OFF; pixel của nó đến từ **đúng một chỗ**:
  `app/src/dev/preview/PhaseAlignPreview.{h,cpp}`, render offscreen bởi
  `tools/snapshot.cpp` ra `shots/preview-phase.png`. **KHÔNG được nối vào
  `MainComponent`** — đo: `grep -c CrossoverSurface` trên `MainComponent.cpp`,
  `MainComponent.h`, `MainComponentDelay.cpp` ra **0, 0, 0**. Đó **không phải
  thiếu sót**: đó là quyết định **ALIGN-R8** ("G18 ships as the existing
  dev-preview specimen driven by a real model, not as `MainComponent` wiring"),
  kết thúc bằng đúng câu "nobody should hunt for a `MainComponent` hook". Nên
  trạng thái đúng của ALIGN là **BUILT (model + specimen)**, còn **nối vào
  `MainComponent` là follow-up CHƯA BẮT ĐẦU**.
- **`AlignmentWizard` còn ít hơn thế — nó KHÔNG có UI nào cả.** Toàn bộ tham
  chiếu tới nó trong `app/src` là ba file của chính nó
  (`measure/AlignmentWizard.h`, `AlignmentWizard.cpp`,
  `AlignmentWizardSignals.cpp`); không view, không panel, **không cả một
  dev-preview specimen**. Nó chỉ chạm tới được **từ `app/tests/`** — bốn file
  test cộng `AlignmentWizardFixture.h`. Bốn câu hỏi nó HỎI, solo sequence và
  chín refusal đều do ctest chứng minh và không có đường nào khác. **Không có
  gì trong app đang chạy khởi động được một wizard.**
- Các solver còn lại cũng vậy, đo được: `EqSession`, `EqVerify`,
  `RawCaptureBuffer`, `CaptureSequencer` đều **0** tham chiếu trong
  `MainComponent.{h,cpp}` và `MainComponentDelay.cpp`.

Nói cách khác: bằng chứng của L7 là **ctest + snapshot offscreen**, không phải
một cái pane mở lên nhìn. Đừng viết, và đừng để ai đọc thành, "operator thấy bề
mặt crossover trong `rtatool.exe`".

Mọi lệnh dưới đây viết cho **PowerShell 7** trong terminal của chủ nhân: một
lệnh một block, không `&&`. Chạy từ gốc checkout. `[verified]` = đã chạy thật
trong phiên closeout này; `[not run here]` = chưa chạy (build nặng, worktree
docs-only không có build dir).

### 1. Hai cấu hình test — cái gì cũng bắt đầu từ đây

`[not run here]` Configure OFF (core-only, không cần JUCE):

```bash
cmake -S . -B build -G "Visual Studio 18 2026" -A x64
```

`[not run here]` Build OFF:

```bash
cmake --build build --config Release --parallel
```

`[not run here]` Chạy test OFF. **Sẽ thấy:** `100% tests passed, 0 tests failed
out of 649`.

```bash
ctest --test-dir build -C Release --output-on-failure
```

`[not run here]` Configure ON (cần JUCE 9.0.1; đường dẫn dưới là checkout đang
chạy production của PROJECT005):

```bash
cmake -S . -B build-on -G "Visual Studio 18 2026" -A x64 -DRTA_BUILD_APP=ON -DRTA_JUCE_PATH="D:\DEV CAVE EP3\PROJECT005-AZ-handsfree\external\JUCE"
```

`[not run here]` Build ON:

```bash
cmake --build build-on --config Release --parallel
```

`[not run here]` Chạy test ON. **Sẽ thấy:** `100% tests passed, 0 tests failed
out of 717`.

```bash
ctest --test-dir build-on -C Release --output-on-failure
```

### 2. Liệt kê test của từng solver

**Đọc kỹ chỗ này trước khi gõ:** `-R` là regex trên **tên case Catch2**, mà tên
case ở repo này là một CÂU tiếng Anh chứ không phải tên file — và nó **phân biệt
hoa thường**. Nên mỗi pattern dưới đây đã được đếm bằng grep trên chính chuỗi
`TEST_CASE("…")` của cây `9698447`; con số kèm theo là số case pattern đó chạm
tới, không phải ước lượng.

`[not run here]` Một lệnh gom đủ mọi họ solver của L7 `[verified: 104 matches]`:

```bash
ctest --test-dir build-on -C Release -N -R "^[GHI][0-9]|Eq|crossoverBandFit|spectralCrossover|rho|delay|polarity"
```

**Sẽ thấy:** tên case — là câu, không phải tên file — rồi dòng `Total Tests: N`.
Đổi `-R` sang đúng một pattern trong bảng để xem riêng một họ:

| pattern | chạm | tên CÓ THẬT trong danh sách |
|---|---|---|
| `^H[0-9]` | **14** | `H1: feeding captures in never moves an asked answer`, `H4b: EVERY named refusal is reachable, and each one is reached here` — 13 case của wizard, cộng `H1 over H2 is exactly the coherence` của transfer estimator |
| `^G[0-9]` | **15** | `G3: a VirtualTrace cannot become a Trace and cannot reach the library`, `G4: a VirtualTrace has no CaptureMeta`, `G11 delay carries the NEGATIVE exponent -- the sign a flipped convention loses` |
| `^I[0-9]` | **5** | `I1b: the target line carries a SIGN, so a flipped odd-order row goes red` |
| `Eq` | **27** | `EqSession: …` (9), `EqVerify: …` (9), `EqTextExport: …` (7), `EqTrustMask: a plain coherence floor, and absent coherence is untrusted`, `rankCandidates/autoEq refuse malformed input and degrade honestly otherwise` |
| `crossoverBandFit` | **8** | `crossoverBandFit returns the competing delay candidates instead of hiding them` |
| `spectralCrossover` | **5** | `spectralCrossover on an analytic BW4 pair lands within half a bin of fc` |
| `rho` | **8** | `rho is scale-invariant and the SIGN survives the scaling` |
| `delay` | **29** | `an integer delay is found exactly`, `a compensated delay leaves the phase flat` |
| `polarity` | **3** | `G11 polarity is the sign bit -- magnitude bitwise unchanged, phase turned by pi` |

Ba cái bẫy mà bộ lọc cũ ở đây (`"eq|delay|align|crossover|virtual|polarity|fir"`)
mắc phải, ghi ra để đừng ai viết lại: `align` **không chạm case nào** — wizard
tên là `H1:`…`H10:`, vì `app/tests/CMakeLists.txt:186` gọi
`catch_discover_tests(rtatool_analysis_tests)` không có `TEST_PREFIX`;
`crossover` bỏ sót cả 5 case `spectralCrossover` (chữ `C` hoa); `virtual` bỏ sót
`G3`/`G4` vì tên viết `VirtualTrace`; và trong 7 case `EqTextExport` thì bộ lọc
cũ chạm đúng **1**, mà chạm tình cờ — `fir` nằm trong chữ `first` của
`EqTextExport: a Windows BOM does not rewrite the first row's filter type`.

Muốn chắc chắn không sót gì thì liệt kê hết rồi lọc bằng PowerShell:

`[not run here]`

```bash
ctest --test-dir build-on -C Release -N | Select-String -Pattern 'rho|eq|delay|align|crossover|virtual|polarity|fir' -CaseSensitive:$false
```

### 3. Guard — tám cái, và chúng là thứ giữ kiến trúc

`[not run here]` **Sẽ thấy:** mỗi guard in `OK (N files scanned)`. Ở `6d9a53d`:
`core_has_no_framework_deps` **157**, `measure_has_no_framework_deps` **64**,
`coherence_gate_is_not_bypassed` **96**,
`filter_design_has_no_polynomial_form` **183**;
`output_render_has_no_rt_hazards` quét dòng 126–210 của `OutputEngine.cpp`,
`audioio_callback_has_no_rt_hazards` (chỉ ON) dòng 119–146 của `AudioIo.cpp`.

```bash
ctest --test-dir build-on -C Release --output-on-failure -R "has_no_framework_deps|coherence_gate_is_not_bypassed|filter_design_has_no_polynomial_form|rt_hazards|denormals"
```

Lượt dựng lại độc lập đếm **11/11 guard xanh** — tám cái trên cộng
`core_makes_no_class_1_claim`, `no_std_atomic_over_shared_ptr`,
`test_names_are_ascii`.

**MỘT LỖ trong guard, tìm thấy trong lúc viết closeout, không phải do closeout
gây ra.** `core/tests/check_no_framework_deps.cmake:37-43` glob
`${CORE_DIR}/include/*.h`, `*.hpp`, `${CORE_DIR}/src/*.h`, `*.cpp` và
`${CORE_DIR}/tests/*.cpp` — **nhưng KHÔNG glob `${CORE_DIR}/tests/*.h`**. Bốn
test header vì thế chưa bao giờ bị quét: `core/tests/CrossoverBandFixture.h`,
`core/tests/DelayFilterFixtures.h`, `core/tests/support/Golden.h`,
`core/tests/guard_fixtures/hex_escape_comment.h`. Một `#include <juce…>` trong
bất kỳ file nào trong số đó vẫn biên dịch vào `rta_core_tests` và guard **vẫn
in OK**. Hôm nay không file nào có — lỗ nằm ở phép quét, không nằm ở cây. Fix
một dòng đang chạy ở nhánh **`ci/guard-scan-test-headers`**, KHÔNG thuộc PR
closeout này. Ghi ở `docs/HUMAN-QA-QUEUE.md`.

### 4. Snapshot offscreen — cách DUY NHẤT để nhìn GUI

**Đừng screen-capture app đang chạy** (CLAUDE.md "Seeing the GUI"): cửa sổ khác
trôi lên trước, DPI scaling co lại, layout chưa ổn định sau resize, và binary
đang chạy giữ lock chính file .exe của nó nên lần build sau không link được.

`[not run here]` Build target snapshot:

```bash
cmake --build build-on --config Release --target rtatool_snapshot --parallel
```

`[not run here]` Render tám PNG vào `shots/` (`shots/` đã gitignore). **Sẽ
thấy:** exit 0 và tám file. Kích thước byte dưới đây là của lượt dựng lại ĐỘC
LẬP tại `6d9a53d` — nếu máy bạn ra số lệch đáng kể thì có gì đó đã đổi:

| file | bytes | là cái gì |
|---|---|---|
| `preview-phase.png` | 45851 | **bằng chứng nhìn được của L7-ALIGN** — bề mặt G18 trên cặp BW4 synthetic |
| `preview-target.png` | 46908 | dev-preview specimen, target/corridor |
| `preview-tf.png` | 45311 | dev-preview specimen, transfer function |
| `transfer.png` | 42213 | Bode composite trên fixture tất định — **chỗ dữ liệu ĐO nằm** |
| `workspace.png` | 55099 | stack 1–3 pane dọc (L5c decision 6) |
| `main-live.png` | 39853 | cửa sổ measurement thật, `MainComponent`, 1280×800 |
| `specimen.png` | 42023 | swatch design system `az_ui` — **KHÔNG** phải `Snapshot`, không phải dữ liệu đo |
| `rta-view.png` | 21384 | riêng plot RTA, diff được byte-for-byte |

```bash
.\build-on\app\rtatool_snapshot_artefacts\Release\rtatool_snapshot.exe shots 1100 760
```

**`shots/preview-phase.png` là bằng chứng nhìn được của L7-ALIGN** (1100×760).
Mở ra sẽ thấy:

- **Pane trên** — đường relative phase `arg(H_A conj H_B)` PHẲNG ở 0° suốt cửa
  sổ fit 50–200 Hz (vệt amber), nằm ĐÚNG trên đường target gạch đứt mà BW4 đặt
  ở `N·90° = 360° ≡ 0`. "Đúng target" hiện ra như một tính chất của trace, không
  phải một con số — đúng điều record §6 đòi. Chip bên phải đọc
  `TOPOLOGY -- ASKED, NEVER INFERRED / BW4  ASKED TARGET 0 deg`.
- **Pane dưới** — HP side (amber) đi lên, LP side (trắng) đi xuống, cắt nhau ở
  100 Hz. GHOST gạch đứt màu xám (tổng TRƯỚC khi align, lệch 4 ms) khoét một hố
  triệt tiêu xuống khoảng **−11 dB** ngay trên 110 Hz, trong khi PREDICTED (xanh
  lá, liền) lên khoảng **+3 dB** tại crossover và đậu đúng mark 3.0 dB. Hai mark
  6.0 dB và 3.0 dB ghi ở mép phải.

**Và đây là chỗ DUY NHẤT `CrossoverSurface` biến thành pixel.** Nó là một model
headless; `PhaseAlignPreview.{h,cpp}` là consumer duy nhất, `rtatool_snapshot`
là thứ render. Không có pane nào trong `rtatool.exe` mở ra cái này (ALIGN-R8 —
xem mục 0 ở đầu phần này). Ai muốn nó vào app thì đó là một follow-up **chưa
bắt đầu**, không phải một cái hook đi tìm là thấy.

**`shots/specimen.png`** là swatch của design system `az_ui` — bảng màu, thang
chữ, primitive. **Nó KHÔNG phải một `Snapshot` đo được**; bằng chứng đo nằm ở
`transfer.png` (Bode composite) và `main-live.png` (cửa sổ measurement thật).
Lỗi này từng bị mắc một lần ở L3 — đừng đọc `specimen.png` như dữ liệu.

### 5. Hai survey ρ, và probe order-4

Ba script này **không ghi gì** và `--help` không chạy pipeline (chúng có
argparse thật — khác với `tools/gen_*.py`, xem
`memory/a-gen-script-runs-the-moment-you-invoke-it.md`). Interpreter phải là
venv của **checkout chính**; worktree không thấy numpy/scipy
(`memory/build-toolchain-on-this-machine.md`).

`[verified]` **Sẽ thấy:**
`usage: probe_rho_a.py [-h] [--quick] [--seed SEED] [--csv CSV] [--no-crossover]`
và dòng `Survey A of rho. Prints distributions; adopts no threshold.`

```bash
& "D:\DEV CAVE EP3\PRJ010-RTA-TOOL\.venv\Scripts\python.exe" tools\probe_rho_a.py --help
```

`[verified]` **Sẽ thấy:**
`Survey B of rho. Prints distributions and a trade-off curve; adopts nothing.`

```bash
& "D:\DEV CAVE EP3\PRJ010-RTA-TOOL\.venv\Scripts\python.exe" tools\probe_rho_b.py --help
```

`[not run here]` Chạy thật survey A ở chế độ smoke (subsample mọi trục). **Sẽ
thấy:** phân bố ρ theo từng ô, KHÔNG có dòng nào chốt một ngưỡng — đó là điểm
của nó. Grid A đầy đủ đọc 2 ô sai dấu trên 43200 (sàn ρ > 0.0640) nhưng có một
ô ĐÚNG dấu ở 0.0520, tức hai phân bố CHỒNG nhau; grid B không có ô sai dấu nào
trên 2000 nên không đặt được sàn. Hai lưới **không đồng ý** — đó là lý do
`relativePolarity` ship không verdict.

```bash
& "D:\DEV CAVE EP3\PRJ010-RTA-TOOL\.venv\Scripts\python.exe" tools\probe_rho_a.py --quick
```

`[verified]` Probe order-4 (`--help`). **Sẽ thấy:** các section
`{identity,sum,xcorr,repo,forensic,all}` và dòng `Writes nothing.`

```bash
& "D:\DEV CAVE EP3\PRJ010-RTA-TOOL\.venv\Scripts\python.exe" tools\probe_align_order4.py --help
```

`[not run here]` Chạy section identity. **Sẽ thấy:** đồng nhất `N·90°` đúng tới
độ chính xác máy — analog `0.00e+00°`, digital `~1.16e-11°` — ở mọi bậc 1–8,
BW lẫn LR. Đây là thứ đã đóng ALIGN §13.1.

```bash
& "D:\DEV CAVE EP3\PRJ010-RTA-TOOL\.venv\Scripts\python.exe" tools\probe_align_order4.py --section identity
```

### 6. FIR export — text và WAV

**Không có menu nào gọi nó.** `app/src/export/` là thư viện; đường export được
chứng minh bằng test, và đó là chỗ để nhìn nó làm gì.

`[not run here]` Text writer (`FirExport.h` + `FirTextWriter.cpp`, chạy ở CẢ
HAI config). **Sẽ thấy:** năm case xanh — mọi key trong header parse lại được,
`sample_rate_hz` trong file bằng `FirResult::sampleRate`, số coefficient khớp
số tap và round-trip trong 1e-6, một request "bare" (không header) bị TỪ CHỐI,
và `Peak0dBFS` co tap sao cho đỉnh đúng bằng 1.0 đồng thời ghi ra mức trim.

```bash
ctest --test-dir build-on -C Release --output-on-failure -R "headerless|sample_rate_hz|Coefficient count|Peak0dBFS|documented header key"
```

`[not run here]` WAV writer (`FirWavExport.h` + `FirWavWriter.cpp`, **chỉ ON**
— nó nhận `juce::File`, nên nó ở `app/tests_juce/`). **Sẽ thấy:** ghi rồi đọc
lại round-trip đủ sample rate / channel count / length / samples; file là
**32-bit IEEE float** và một yêu cầu định dạng integer bị TỪ CHỐI; và
`firFilenameStem` mang sample rate, số tap, và phase. Lý do từ chối int16 nằm
ở research FIR: **trường sample-rate trong WAV không đáng tin** — CamillaDSP
bỏ qua nó — nên tên file phải mang thông tin đó.

```bash
ctest --test-dir build-on -C Release --output-on-failure -R "32-bit IEEE float|firFilenameStem|Write then read back"
```

### 7. EQ text export, và cột `applied`

`[not run here]` **Sẽ thấy:** bảy case xanh của `EqTrustMask` +
`EqTextExport`. Định dạng là một filter một dòng,
`type fc_hz q gain_db [applied]`, ở đúng độ chính xác readout của repo — **Hz
nguyên**, Q 2 chữ số thập phân, dB 1 chữ số. Độ chính xác GHI chính là độ chính
xác round-trip.

```bash
ctest --test-dir build-on -C Release --output-on-failure -R "EqTextExport|EqTrustMask"
```

**Cột thứ năm `applied` là một deviation có chủ ý so với plan Task E** (plan
chốt bốn cột `type, fc, Q, gain`). Vòng verify thứ hai chỉ ra: một danh sách
KHÔNG mang bit đó thì không an toàn khi nạp ngược vào chính processor đã sinh
ra phép đo — hàng đã applied sẽ đáp xuống lần hai, **−7.1 dB thành −14.2 dB**
trên một bump 8 dB. Nên: chỉ GHI cho hàng đã applied, **tuỳ chọn khi ĐỌC** (file
bốn cột vẫn import được), và một hàng không có cờ đọc là **chưa** applied. Một
token thứ năm lạ thì bị TỪ CHỐI chứ không đọc thành "chưa applied" — đoán ở đây
là đoán nguy hiểm. Ghi thành amendment §7 trong
`docs/dsp/2026-09-06-l7-auto-eq.md`, kèm đính chính rằng ghost identity của
record là trên các filter **CHƯA** applied, không phải trên mọi filter đã
commit.

---

## Rule 12 vế 3 — HANDOFF cho lane kế: **L6a (SPL-pro)**

Lane kế **không do closeout này chọn** — nó là thứ "Suggested opening order"
của `docs/plans/MASTER-EXECUTION-PLAN.md` đã ghi sẵn: sau L7 là **L6a**, rồi
**L9** cuối cùng, còn **L8** (research) bắn lúc nào cũng được vì read-only.
L6a từng chờ Meters track; Meters track (Weighting, Detector, Leq) **đã hạ
cánh**, nên nó hết chặn.

> **Cập nhật 2026-09-17 — L6a KHÔNG phải lane duy nhất đang mở, và stations 1+2
> của chính nó đã bay.** Khi mục này được viết, closeout chỉ thấy L6a; hai lane
> dưới đây mở sau đó vài chục giây tới một ngày, nên đoạn bên dưới đọc như thể
> chưa có gì. Trước khi mở nhánh mới, đọc hai PR này đã — cả hai đều
> PARALLEL-SAFE với nhau:
>
> - **L-API (remote API)** — stations 1+2 trên **PR #11**, nhánh
>   `remote-api/stations-1-2`, head `828c223`, OPEN. Đây đúng là mảnh L6b scope
>   out mà `MASTER-EXECUTION-PLAN.md` từng ghi là "chưa có lane"; hàng **L-API**
>   đã được thêm vào plan TRÊN NHÁNH CỦA PR ĐÓ, nên nhánh này chưa thấy nó.
> - **L6a (SPL-pro)** — stations 1+2 trên **PR #12**, nhánh `l6a/stations-1-2`,
>   OPEN. Trạm 1 của L6a đã có người làm. Đừng làm lại nó; đọc PR #12 rồi tiếp
>   từ chỗ nó dừng. Phần "Nó bắt đầu ở TRẠM 1" bên dưới là đúng lúc viết, không
>   còn đúng hôm nay.

**Phạm vi L6a:** SPL logging / history / alarms / PDF / web viewer (G7), và
dose theo IEC 61252 (G8).

**Nó bắt đầu ở TRẠM 1, không phải trạm 3.** Chưa có `docs/dsp/` record nào cho
SPL-pro. Theo CLAUDE.md "Before each phase: research, then argue, then build":
đọc chuẩn và nêu đúng clause, đọc CODE của sản phẩm khác (không đọc README),
viết ra hai-ba phương án kèm trade-off và cãi lại cái trông hiển nhiên nhất,
rồi chốt và ghi lý do vào `docs/dsp/`.

**Đọc trước, theo thứ tự:**

1. `docs/GIT-WORKFLOW.md` — luật hiện hành: `origin/main` là sự thật, không
   commit nào lên `main` ngoài đường PR, merge là lời của chủ nhân **trong
   chính phiên đó**.
2. `docs/plans/MASTER-EXECUTION-PLAN.md` — "Status snapshot — 2026-09-16",
   hàng **L6a**, và cột PARALLEL-SAFE (L6a an toàn song song với mọi thứ trừ
   L6b).
3. `docs/reports/007-solvers.md` — L7 vừa đóng; §7 "Known gaps" và §8 "Open,
   and it is the owner's" là những thứ có thể đụng vào L6a.
4. `docs/dsp/2026-08-27-weighting-and-meters.md` và
   `docs/plans/2026-08-27-weighting-meters-impl-plan.md` — Meters track là nền
   trực tiếp của L6a.
5. `docs/HUMAN-QA-QUEUE.md` — mục `[!]` đầu tiên (Actions billing) và mục
   `test_weighting.cpp` chờ duyệt: **cả hai nằm đúng trên đường của L6a**, vì
   L6a xây trên chính weighting đó và sẽ mở PR cần CI.

**Đang chặn, và chỉ chủ nhân gỡ được:**

- **GitHub Actions bị chặn billing.** Cho tới khi mở lại, gate CI của
  `docs/GIT-WORKFLOW.md` luật 3 không kiểm được; PR của L6a sẽ phải merge trên
  bằng chứng local hai cấu hình + verifier độc lập, và **phải ghi rõ điều đó
  trong PR body**.
- **Duyệt assertion `core/tests/test_weighting.cpp`** (hoãn từ PR #5). L6a động
  thẳng vào weighting; nếu chủ nhân muốn quay lại `isinf` thì tốt nhất là biết
  TRƯỚC khi L6a xây lên trên nó.
- **L5b vẫn chặn** vì ISO 2969 / SMPTE ST 202 (bảng dung sai X-curve) và
  **L4d** vì IEC 60268-16 (STI). Không phải việc của L6a, nhưng đừng để một
  phiên nào mở nhầm chúng.
- **ISO 61252 thì sao?** Câu hỏi "mua hay dựng từ nguồn mở" cho dose CHƯA được
  hỏi. Đừng suy ra câu trả lời; luật tạm của L4b là mẫu tốt — dựng từ literature,
  ghi provenance là *literature*, và **không được viết "theo IEC 61252" ở bất kỳ
  đâu** cho tới khi cầm bản thật (bẫy AES-2id).

**Trạm đầu tiên, cụ thể:** một agent Explore (không có write tool) đọc
IEC 61252 (nêu tên clause, không trích), IEC 61672-1 cho detector/weighting mà
Meters track đã thi công, và **code** của các sản phẩm đang làm cùng việc
(Open Sound Meter, NIOSH SLM, các SPL logger thương mại) — rồi trả về chỗ chúng
**bất đồng với nhau**, vì đó mới là chỗ có quyết định thật.

**Món nợ doc mà L7 để lại, nên trả sớm:** ALIGN-R1's `1e-12` vẫn chưa được
amend trong §5 của `docs/dsp/2026-09-06-l7-alignment-wizard.md` và trong Wave 0
plan — nó là thuộc tính của fixture Butterworth-SOS, không phải của lock. Chi
tiết ở `docs/reports/007-solvers.md` §5 mục 6.

---

# 2026-09-16 — **L7-ALIGN Wave 3b (tasks G–J) XONG — nhánh `l7/align-wave3b-app`, ĐÃ MERGE (PR #9, `6d9a53d`). ALIGN BUILT.**

> **Cập nhật 2026-09-16 bởi closeout L7:** mục này ghi "PR mở, CHƯA merge" khi
> viết. PR #9 đã merge vào `origin/main` tại **`6d9a53d`**, và
> `git diff 6d9a53d^2 6d9a53d` RỖNG nên số 649/717 dưới đây đúng cho cả cây
> merge. Lane report: `docs/reports/007-solvers.md`.

**Đọc mục này trước tiên.** Nửa sau của plan
`docs/plans/2026-09-15-L7-align-impl-plan.md` đã xây và verify cục bộ trên
worktree riêng, nhánh từ `02bd02a` (Wave 3a đã merge, PR #8). Với G–J xong,
**toàn bộ lane L7-ALIGN đã BUILT** — G11 + G17 + G18 + ρ.

**GitHub Actions vẫn bị chặn ở mức tài khoản (billing).** Bằng chứng dưới đây
là hai config chạy cục bộ, không phải CI xanh. Verifier phải đo lại; đừng tin
bảng này.

## Baseline đo được (dán từ lệnh, đừng chép số cũ)

Generator Visual Studio 18 2026, MSVC 14.51, JUCE qua `RTA_JUCE_PATH`.
`build-align3b` (OFF) và `build-align3b-on` (ON):

```
TRƯỚC (02bd02a):  ctest OFF -> 624/624, 0 failed     ctest ON -> 692/692, 0 failed
SAU  (tip nhánh):  ctest OFF -> 649/649, 0 failed     ctest ON -> 717/717, 0 failed
warning C trong cả hai build log -> 0
```

**ON baseline 692 nay đã ĐO ĐỘC LẬP.** Builder không đo được (configure ON đầu
phiên bị một edit đồng thời chen vào trước lần ctest đầu tiên) và chỉ suy ra
`716 − 24`. Verifier PR #9 dựng cây sạch tại `02bd02a` và chạy ON: **692/692**,
đúng con số suy ra, và `measure_has_no_framework_deps` ở cây đó in
`OK (57 files scanned)` nên **57 → 64** khớp cả hai đầu.

Con số duy nhất chưa ai đo trực tiếp là **OFF baseline 624**. Nó khép lại bằng
số học: ba task G/H/I thêm 24 test (`716 − 692 = 24`), và vòng sửa theo verifier
thêm 1 (case H4b), nên `649 − 25 = 624` và `717 − 25 = 692`.

## Commit — ba task một commit, cộng docs, memory và một vòng sửa theo verifier

**PR #9** (`https://github.com/toanaz-ops/rta-tool/pull/9`), ~~**CHƯA merge**~~ —
**ĐÃ MERGE tại `6d9a53d`** sau khi chủ nhân nói "merge"
(`docs/GIT-WORKFLOW.md` luật 4). Sửa 2026-09-16 bởi closeout L7.

**Head sha KHÔNG ghi ở đây, có chủ ý.** Bản trước ghi `e5b1c84` và nó đã sai
ngay khi commit kế tiếp hạ xuống — verifier PR #9 bắt đúng lỗi này (D6). Một
hash viết trong chính commit nó đặt tên thì không thể đúng. Lấy bằng lệnh:

```bash
gh pr view 9 --repo toanaz-ops/rta-tool --json headRefOid --jq .headRefOid
```

| commit | task | nội dung |
|---|---|---|
| `3effb67` | G | `VirtualTrace` — điểm chuyển dB↔complex DUY NHẤT; 4/7 case là NEGATIVE (không thành `Trace` được, không tới `TraceLibrary` được, không có `CaptureMeta`, tổng không mang field tên `coherence`) |
| `a340b8b` | H | `AlignmentWizard` — bốn câu HỎI, chuỗi solo L7-OUT, refusal có tên, bảng polarity-signal HỎI chứ không chọn |
| `b30901f` | I | `CrossoverSurface` (G18) + `PhaseAlignPreview` trỏ vào model thật; snapshot ON đọc được |
| `60cc605` | J | mục HANDOFF này, hàng roadmap, và sửa tại chỗ dòng acceptance I4 của plan |
| `e5b1c84` | — | memory `a-prescribed-mutation-is-not-proof-the-check-catches-it.md` + index |
| `703e777` | — | sửa bảng commit này (bản trước ghi placeholder "(mục này)") |
| `c3b9579` | — | vòng sửa theo verifier #1 (D1–D6) |
| `f507f96` | — | vòng sửa theo verifier #2 (R1, R2, N1, N2) |
| `46c1dd0` | — | vòng sửa theo verifier #3 (V1, V2, V3) |
| (commit này) | — | vòng sửa theo verifier #4 (W1) |

Task J không đổi một dòng code nào: mọi mutation bên dưới đã revert, và cây đã
được chứng minh trùng HEAD (`git status --porcelain` rỗng, `git diff HEAD` rỗng,
`git stash list` rỗng) TRƯỚC lần rebuild cuối — nên màu xanh ở trên đến từ code
đã commit, không phải từ một bản sửa chưa commit
(`memory/a-verifier-with-bash-can-git-checkout-your-uncommitted-fix.md`).

## Vòng verifier (PR #9, verdict **SOUND-WITH-FIXES**) — sáu mục, đã sửa hết

Verifier độc lập (không có `Edit`/`Write`) dựng lại cả hai config trong worktree
riêng, đo lại mọi con số, và tự áp cả ba mutation. Sáu defect, không cái nào là
lỗi hành vi của code đã ship — hai cái là **lời tuyên bố vươn xa hơn bằng chứng
của chính nó**, đúng thứ mà memory mới của lane này nói về.

| # | defect | đã sửa thế nào |
|---|---|---|
| D1 | "xoá exe" KHÔNG tái hiện được đỏ của G3. Verifier đo được **false PASS**: mutation nằm ở header, MSBuild biên dịch lại `VirtualTrace.cpp`, link lại exe, và `test_virtual_trace.cpp` — TU DUY NHẤT chứa ba `static_assert` — không hề được biên dịch lại | chạy lại mutation có ép TU đó (xoá `.obj` + `touch`), dán đỏ; thêm mục mới vào `memory/mutation-testing-needs-the-exe-deleted-first.md`: **một assertion compile-time chỉ chạy bởi lần biên dịch đọc nó**, nên phải ép đúng TU chứa nó, không phải "một `.cpp` nào đó" |
| D2 | whitelist I4 chỉ quét giữa `class crossoversurface` và `};` cột 0, nên một **free function** `bestDelayForLoudestSum(const CrossoverSurface&)` khai báo sau class vẫn XANH — và word-grep của plan cũng trượt nó, tức hình dạng này **không cái gì bắt** | quét mở rộng ra CẢ header và các định nghĩa non-member cột 0 của `.cpp`; whitelist giờ liệt kê **mọi callable** hai file khai báo (21 cái). Tái hiện xanh với mutation cũ, rồi đỏ: `declared.size() == allowed.size()` → `22 == 21` |
| D3 | 3/9 enumerator `WizardRefusal` không test, gồm `TraceNotUsable` là nhánh SỐNG | case mới `H4b` (file riêng `test_alignment_wizard_refusals.cpp`) chạm cả chín. Mutation (đổi `TraceNotUsable` thành `WrongStep`) → đỏ hai chỗ. **Backstop cho enumerator thứ mười ở bản sửa này là SAI và đã thay** — xem R2 ở bảng dưới; đừng đọc dòng này như mô tả cái đang ship |
| D4 | bốn kỳ vọng số không có suy dẫn | ρ: `> 0.9` → **`== 1` trong 1e-12**, vì b = −a BITWISE (đã assert tiền đề đó) nên Cauchy–Schwarz đạt dấu bằng. R của H5: `> 0.999` → chặn dẫn xuất `1 − R ≤ 1.01(πΔ·dτ)²(N²−1)/6 + 1e-12` với dτ ĐO được (1.77e-14 s) — đo thực 3.33e-16. R của H10: giữ 0.5 nhưng **viết ra vì sao** (bước ngẫu nhiên Rayleigh, R → 1/√N_eff; đo 0.16141 ⇒ N_eff ≈ 38 ⇒ P(R>0.5) ≈ 7e-5). Ô bất đồng H7: đổi tên case thành **REGRESSION LOCK** |
| D5 | vượt budget per-file của plan mà không khai | khai đủ bốn chỗ ở mục "Deviation" dưới, và trong PR body |
| D6 | HANDOFF ghi head sha sai | bỏ hẳn hash khỏi HANDOFF, thay bằng lệnh `gh pr view`; hai danh sách deviation (PR 8 mục vs HANDOFF 5 mục) nay khớp 1–1 ở tám |

### Vòng verifier #2 — hai khẳng định bị BÁC, ba lỗi doc

| # | bác cái gì | đã sửa thế nào |
|---|---|---|
| R1 | whitelist I4 (bản đã mở rộng) vẫn **không bắt** một callable gán vào OBJECT: `inline constexpr auto bestDelayForLoudestSum = [](const CrossoverSurface&, double) noexcept {...};` ở namespace scope trong header — vì luật "bỏ qua dòng có `=` trước `(`" chính là hình dạng đó. Toàn bộ 649 test OFF vẫn xanh | scan đọc thêm dạng 2 (`= [`, `= +[`, `std::function<...> name =`); đỏ đúng lý do: `22 == 21` với `bestdelayforloudestsum` CÓ trong danh sách in ra (lần verifier thử trước, nó đỏ vì THÂN lambda chứa lời gọi — đỏ nhầm lý do). **Phạm vi scan nay viết thẳng trong test**, gồm cả cái nó KHÔNG phủ (macro, TU khác) |
| R2 | backstop C4062 **KHÔNG TỒN TẠI**: MSVC 14.51 tắt C4062 mặc định, `/W4` không bật (`-W4` im, `-W4 -w14062` mới kêu). Verifier thêm enumerator thứ mười: build sạch, H4b vẫn xanh | bỏ hẳn câu đó; thay bằng sentinel `WizardRefusal::Count` + `kWizardRefusalCount`, H4b so `seen.size()` với nó. Đỏ: `9 == 10`. Không phụ thuộc cờ cảnh báo. Switch vẫn không `default:` vì `-Wswitch` của GCC/Clang CÓ kêu — backstop trên 2/3 OS, sentinel trên cả 3 |
| N1 | hai danh sách deviation lệch 11 vs 8 | đánh số 0–10 ở cả hai bản |
| N2 | mục "người chạy được gì" còn 648/716 trong khi đầu mục ghi 649/717 | sửa, và ghi rõ vì sao |

Verifier #2 cũng đúng khi nói **bằng chứng dán trong reply vòng 1 là số dòng
CŨ** (220/222/226 thay vì 196/198/202, và `test_alignment_wizard.cpp` thay vì
`test_alignment_wizard_refusals.cpp`): kết luận đúng nhưng output không sinh ra
trên cây mà reply đặt tên. Vòng này chạy lại mọi mutation trên tip và dán số
dòng thật.

### Vòng verifier #3 — một lỗ thật, hai lỗi doc

| # | bác cái gì | đã sửa thế nào |
|---|---|---|
| V1 | scan I4 vẫn theo DÒNG, nên **cùng cái lambda đó xuống dòng** giữa `=` và `[](` là xanh — và đó đúng là cách chính comment của scan lẫn memory viết ví dụ | scan không còn đọc dòng nào cả: `rta::test::codeText` bỏ comment, gộp cả file thành MỘT chuỗi, thu mọi khoảng trắng về một dấu cách; đơn vị khai báo cắt ở `;`/`{`, và thân hàm bị bỏ qua bằng đếm ngoặc. **Năm dạng đều đỏ** (lambda xuống dòng, lambda một dòng, `std::function`, free function, static member), mỗi lần `22 == 21` và tên CÓ trong danh sách in ra. Case dọn sang file riêng `test_crossover_surface_objective.cpp` |
| V2 | số dòng ở deviation 5 đã cũ | sinh lại bằng `wc -l` tại head cuối, dán nguyên khối |
| V3 | dòng D3 ở bảng vòng 1 vẫn kể backstop C4062 như thể đang ship, mâu thuẫn với R2 ngay dưới | dòng đó nay trỏ thẳng sang R2 |

### Vòng verifier #4 — mọi khẳng định XÁC NHẬN, còn một lỗ: W1

| # | lỗ gì | đã sửa thế nào |
|---|---|---|
| W1 | bộ bỏ comment của `codeText` không có trạng thái STRING LITERAL. Một `//` trong chuỗi hầu như luôn là URL: `kRecordUrl = "https://…/docs/dsp";`. Bộ strip ăn từ `//` tới hết dòng, **nuốt luôn `";` đóng chuỗi**, nên khai báo KẾ TIẾP (cách bao xa cũng được, kể cả qua một dòng trống) nhập vào cùng đơn vị với `kRecordUrl =`, và luật initialiser "`=` trước `(`" làm nó câm. Objective export ra header mà cả bộ test vẫn xanh 649/649 | stripper nay theo dõi `"`/`'` với escape `\`; chỉ coi `//` và `/* */` NGOÀI literal là comment, và **rỗng hoá nội dung literal** để một `;` hay `(` bên trong không dời được ranh giới đơn vị. Tiền đề cũ ("hai file này không có string literal") **không có gì thực thi** — nay thay bằng CONTROL chạy được: `codeTextOf` tách khỏi `codeText` để test lái thẳng bằng đoạn mã tổng hợp |

**Tám dạng đều đỏ**, mỗi lần `22 == 21` và tên CÓ trong danh sách in ra: năm dạng
của vòng 3 (lambda xuống dòng, lambda một dòng, `std::function`, free function,
static member) cộng ba dạng W1 của vòng 4 (URL cùng dòng, URL dòng trước, URL
cách một dòng trống). Verifier #4 cũng xác nhận dạng **macro** đã bị bắt sẵn.

Bài học sau bốn vòng, đã ghi vào memory: **viết PHẠM VI của một structural check
vào chính artefact, đừng viết vào văn xuôi quanh nó**; một scan theo dòng thua
một phím Enter; và **một tiền đề không ai thực thi không phải là "limitation",
nó là cái lỗ có chú thích**.

Hai ghi chú nhỏ của verifier #1 cũng đã lấy: scan H1 trước đây tìm chuỗi
`member_ + " ="` nên `inversion_= x;` (không dấu cách) lọt — nay dùng
`rta::test::assignsTo`, chịu được mọi kiểu đặt dấu cách và vẫn phân biệt `==`
(mutation không-dấu-cách đã chạy, đã đỏ); và `codeLines()` từng có hai bản
giống hệt, nay là một, ở `app/tests/CodeLines.h`.

## Ba điều load-bearing phiên sau KHÔNG suy diễn lại

1. **Bốn câu hỏi được HỎI, và scan cấu trúc là thứ giữ điều đó.**
   `test_alignment_wizard_signals.cpp` bám theo hàm bao quanh mỗi phép gán vào
   `topology_` / `inversion_` / `highPassSide_` / `seedHz_` / `seedAnswered_` /
   `cycleAnswer_` và bắt lỗi nếu hàm đó không bắt đầu bằng `answer`. Mutation
   "để `compute()` đặt `inversion_` từ dấu của intercept" làm ĐỎ cả scan lẫn
   case hành vi. **Scan phải phân biệt `x_ =` với `x_ ==`** — bản đầu báo 5
   offender toàn là chỗ code đang ĐỌC đúng như phải đọc.
2. **Không có objective nào trong `CrossoverSurface`, và cách CHỨNG MINH điều đó
   đã đổi HAI LẦN.** Word-grep của plan (I4) KHÔNG bắt được chính mutation mà
   plan chỉ định (`bestDelayForLoudestSum()`): identifier không chứa từ nào
   trong danh sách, còn dòng duy nhất chứa thì là COMMENT giải thích vì sao nước
   cờ đó bị cấm. Thay bằng **whitelist** — nhưng bản đầu chỉ quét thân class,
   nên một **free function** cùng tên ở namespace scope vẫn lọt (verifier D2).
   Bản thứ hai vẫn lọt dạng **lambda gán vào object** (round-2 R1). Bản ship đọc
   HAI dạng — `... name(` và `... name = [` / `std::function<...> name =` —
   trong hai file `CrossoverSurface.{h,cpp}`, và **phạm vi đó viết thẳng trong
   test**, kể cả cái nó không phủ. Câu đúng là câu hẹp đó; ba lần liên tiếp câu
   rộng hơn đều sai.
3. **`summationTrust` vẫn không được đổi tên thành `coherence`.** Guard
   `coherence_gate_is_not_bypassed` bắt PHÉP GÁN chứ không bắt khai báo, nên
   mutation phải làm cả hai (đổi tên field VÀ `result.coherence = ...`) mới đỏ.
   Đã làm, đã đỏ, đã revert.

## Task J — guard nào xanh, và mỗi cái đã ĐỎ một lần ở đúng hình dạng làm nó đỏ

| guard | xanh, scanned count | đỏ bằng gì |
|---|---|---|
| `core_has_no_framework_deps` | OK (157 files scanned) | `#include <juce_core/juce_core.h>` trên đầu `CrossoverFit.h` → "rta_core must not depend on a GUI/audio framework. Offending files: .../CrossoverFit.h" |
| `measure_has_no_framework_deps` | OK (64 files scanned; `main` là **57**, +7 file Wave 3b) | JUCE include trên đầu `AlignmentWizard.h` → "app_measure must not depend on a GUI/audio framework. Offending files: .../AlignmentWizard.h" |
| `coherence_gate_is_not_bypassed` | OK (96 files scanned) | đổi tên field THÀNH `coherence` **và** `result.coherence = std::vector<float>(n);` trong `VirtualProcessor.cpp` → "coherence assigned outside the gate: .../VirtualProcessor.cpp" |
| `filter_design_has_no_polynomial_form` | OK (183 files scanned) | không đổi ở Wave 3b (script ρ của task F đã trong scope từ Wave 3a) |
| `output_render_has_no_rt_hazards` | OK (scanned lines 126-210 of OutputEngine.cpp) | `git diff --stat origin/main -- platform/` RỖNG — lane này không thêm gì vào audio callback |
| `audioio_callback_has_no_rt_hazards` (ON) | OK (scanned lines 119-146 of AudioIo.cpp) | như trên |
| `audioio_scoped_no_denormals_is_first` (ON) | OK | như trên |
| `rtatool_snapshot` link | `rtatool_snapshot.vcxproj -> ...\Release\rtatool_snapshot.exe` | PR #8 "take to the orchestrator" mục 6 đã trả: `target_sources` tại `app/CMakeLists.txt` giờ mang `src/view/CrossoverSurface.cpp`, `src/measure/CrossoverTopology.cpp` **và** `src/trace/VirtualTrace.cpp` |

`git diff --stat origin/main --` cho `ui/`, `platform/`,
`core/include/rta/dsp/Biquad.h`, `core/include/rta/dsp/BiquadResponse.h`,
`core/include/rta/eq/` đều **RỖNG**. Không có file mới nào quá 400 dòng; dài
nhất là `test_virtual_trace.cpp` 354 và `AlignmentWizard.cpp` 318.

## Người có thể tự chạy cái gì, và trông đợi thấy gì

Snapshot offscreen (đừng screen-capture app đang chạy — CLAUDE.md):

```bash
cmake --build build-align3b-on --config Release --target rtatool_snapshot --parallel
```

rồi chạy exe `build-align3b-on\app\rtatool_snapshot_artefacts\Release\rtatool_snapshot.exe shots 1100 760`
và mở `shots/preview-phase.png`. **Sẽ thấy:** pane trên, đường relative phase
`arg(H_A conj H_B)` PHẲNG ở 0° suốt cửa sổ fit 50–200 Hz (vệt amber) và nằm
ĐÚNG trên đường target gạch đứt mà BW4 đặt ở 0 — "đúng target" hiện ra như một
tính chất của trace chứ không phải một con số, đúng điều record §6 đòi. Chip
bên phải đọc "TOPOLOGY -- ASKED, NEVER INFERRED / BW4 ASKED TARGET 0 deg".
Pane dưới: HP side (amber) đi lên, LP side (trắng) đi xuống, cắt nhau ở 100 Hz;
GHOST gạch đứt (xám, tổng TRƯỚC khi align, lệch 4 ms) khoét một hố triệt tiêu
xuống ~−11 dB ngay trên 110 Hz, trong khi PREDICTED (xanh lá, liền) lên ~+3 dB
tại crossover và đậu đúng mark 3.0 dB. Hai mark 6.0 dB và 3.0 dB ghi ở mép
phải. `shots/` đã gitignore, không commit gì trong đó.

Toàn bộ test: `ctest --test-dir build-align3b -C Release` (OFF, **649**) và
`ctest --test-dir build-align3b-on -C Release` (ON, **717**) — cùng con số với
mục baseline ở đầu mục này. (Hai dòng này từng kẹt ở 648/716 sau vòng verifier
thứ nhất; round-2 verifier bắt được, N2. Luật 12 của CLAUDE.md, vế hai: grep
những câu mà thay đổi vừa làm sai.)

## Deviation phải mang lên orchestrator

**Mười một mục, ĐÁNH SỐ 0–10 khớp 1–1 với phần "Deviations" của PR #9.** Vòng
verifier #1 thấy hai bản lệch (PR 8 / HANDOFF 5); reconcile khi đó chỉ đi một
chiều rồi PR mọc thêm ba mục, nên round-2 lại thấy lệch (PR 11 / HANDOFF 8,
N1). Lần này đánh số giống hệt để lần sau chỉ cần đếm.

0. **Hai tuyên bố vươn xa hơn bằng chứng, nay đã khép.** (a) whitelist I4 từng
   chỉ quét thân class → free function lọt; sau khi mở rộng vẫn còn lọt dạng
   **lambda gán vào object** (`inline constexpr auto f = [](...){...}`, round-2
   R1) vì luật "bỏ qua dòng có `=` trước `(`" đúng là hình dạng đó. Nay scan đọc
   HAI dạng và phạm vi được viết thẳng vào test. (b) "xoá exe trước mỗi lần
   build mutation" không đủ cho mutation trong header mà guard là assertion
   compile-time.
1. **Plan I4 sai về chính mutation của nó** — xem mục "Ba điều load-bearing" #2.
   Plan cần sửa dòng acceptance đó, giống hệt cách D6 đã được sửa ở Wave 3a.
2. **Tolerance H9 là 1e-6 chứ không phải 1e-9 của plan.** Phase của fixture đi
   qua kho `float` của `Trace`; `float(pi/2)` cao hơn `pi/2` 4.37e-8, nên 1e-9
   là tolerance mù float32 (`memory/float32-fft-precision.md`). Điều case đó
   CHỨNG MINH — nửa vòng giữa BW1 (+π/2) và BW3 (−π/2) — không float nào làm mờ
   được, và giá trị `expectedOffset` vẫn giữ 1e-15. H5 cùng lý do: 1e-5 cho τ,
   nhưng phép transpose (thứ chịu lực) vẫn 1e-10.
3. **H7 cell bất đồng là ĐO được, không phải giả định.** ρ và tích hai
   `findPolarity` ĐỒNG Ý ở mọi cặp thông thường (đã probe 5 cấu hình). Chỗ
   chúng tách nhau là limit 1 của `Polarity.h` — thùng two-way có tweeter đảo
   pha — và phụ thuộc tần số cắt: 800 Hz và 1200 Hz đồng ý, **2000 Hz bất
   đồng** (findPolarity âm, margin 1.00; ρ +0.7986), 3500 Hz đồng ý lại.
   Fixture lấy đúng ô 2000 Hz.
4. **G1 residual ở mức float bằng ĐÚNG 0** ở mọi bin, cả dB lẫn phase. Đó là
   thật (vòng double rơi trong nửa ULP của float), nhưng một residual bằng 0 là
   fixture không thể đỏ nếu nó là con số DUY NHẤT trong case
   (`memory/a-fixture-can-be-too-well-behaved-to-fail.md`). Nên case đo thêm
   residual của chính phép tính TRƯỚC khi ép về float và chặn ở 1e-9.
5. **Vượt budget per-file của plan ở ba chỗ** (trần cứng 400 của CLAUDE.md thì
   KHÔNG chỗ nào vượt). Số dưới đây sinh từ `wc -l` **tại head cuối cùng của
   nhánh** — bản trước ghi số của một vòng sửa cũ và round-3 verifier bắt được
   (V2):

   ```
   300 app/src/measure/AlignmentWizard.h          (plan: 180)
   318 app/src/measure/AlignmentWizard.cpp        (plan: 300; + AlignmentWizardSignals.cpp 69)
   330 app/tests/test_virtual_trace.cpp           (plan: 280)
   151 app/src/view/CrossoverSurface.h            (plan: 160)  OK
   137 app/src/view/CrossoverSurface.cpp          (plan: 240)  OK
   141 app/src/trace/VirtualTrace.h               (plan: 150)  OK
   147 app/src/trace/VirtualTrace.cpp             (plan: 200)  OK
   ```

   Test của task H plan cho MỘT file ≤ 340; ship thành **bốn**
   (256 + 203 + 241 + 358) vì một file duy nhất là 538 dòng. Test của task I
   plan cho một file ≤ 280; ship thành **hai** — `test_crossover_surface.cpp`
   **231** và `test_crossover_surface_objective.cpp` **250** — vì scan khai báo
   qua bốn vòng verify đã thành một chủ đề riêng. Fixture dùng chung:
   `AlignmentWizardFixture.h` 178, `CodeLines.h` **183**.
6. **ALIGN-R7 vẫn là proxy chuỗi.** `ReferenceMismatch` so sánh
   `CaptureMeta::channelRoles` string-equal. Muốn structural thì phải thêm
   field vào `CaptureMeta` + bump schema session — ngoài phạm vi lane.
7. **Record §10.11 nói nhẹ về mutation của coherence gate** — chỉ đổi tên field
   thì KHÔNG đỏ, vì guard bắt phép GÁN. Chính bullet task J của plan đã đoán
   trước; ghi lại đây như record correction mà nó xin.
8. **`SignalStanding::Authoritative` tồn tại mà không gì sinh ra nó.** Không có
   nó thì "qua crossover không dấu time-domain nào là authoritative" là mệnh đề
   không thể bác bỏ.

9. **3/9 enumerator `WizardRefusal` không có test** (verifier #1 D3), gồm nhánh
   SỐNG `TraceNotUsable`. Case H4b chạm cả chín. Backstop cho enumerator thứ
   mười **đã phải làm lại**: khẳng định "switch không `default:` → MSVC bắn
   C4062 ở /W4" là **SAI** (round-2 R2 đo: C4062 tắt mặc định, `/W4` không bật;
   thêm enumerator thứ mười vẫn build sạch và H4b vẫn xanh). Nay là sentinel
   `WizardRefusal::Count` + `kWizardRefusalCount` mà H4b so với `seen.size()` —
   không phụ thuộc cờ cảnh báo, giống nhau trên cả ba OS.
10. **Bốn kỳ vọng số không có suy dẫn** (verifier #1 D4) — chi tiết ở bảng vòng
    verifier phía trên.

*(Mục 0, 9, 10 đến từ hai vòng verifier; 1–8 là danh sách gốc.)*

## Việc còn để lại cho người (không phải cho agent)

Năm mục "Open, needs a human" của plan vẫn nguyên, cộng mục 3 ở trên. Đáng chú
ý nhất: **một cặp sub/main THẬT** (record §13.2) — mọi thứ ở đây là synthetic;
và **nhánh "unknown" của câu hỏi (c)** (record §13.3) nay đã hiện lên bề mặt
G18 dưới dạng hai đường candidate, chủ nhân nên nhìn trước khi nó đi tiếp.

---

# 2026-09-16 — **L7-ALIGN Wave 3a (tasks A–F) XONG — nhánh `l7/align-wave3a-core`, ĐÃ MERGE (PR #8, `02bd02a`)**

> **Cập nhật 2026-09-16 bởi Wave 3b:** mục này ghi "PR mở, CHƯA merge" khi viết.
> PR #8 đã merge vào `origin/main` tại **`02bd02a`**, và Wave 3b nhánh từ đó.
> Mục "VIỆC ĐẦU TIÊN của Wave 3b" bên dưới **đã làm xong** — chi tiết ở mục
> Wave 3b phía trên.

**Nửa đầu** của plan `docs/plans/2026-09-15-L7-align-impl-plan.md` đã xây và
verify cục bộ trên worktree riêng, nhánh từ `a2de06e`. **GitHub Actions đang bị
chặn ở mức tài khoản (billing), nên bằng chứng là hai config chạy cục bộ, không
phải CI xanh** — verifier phải đo lại, đừng tin bảng dưới.

## Baseline đo được (dán từ lệnh, đừng chép số cũ)

Đo trên **cây chưa sửa** `a2de06e` TRƯỚC dòng code đầu tiên, rồi đo lại trên
tip nhánh. Generator Visual Studio 18 2026, MSVC 14.51, JUCE qua `RTA_JUCE_PATH`.
`build-align` (OFF) và `build-align-on` (ON):

```
TRƯỚC (a2de06e):  ctest OFF -> 591/591, 0 failed     ctest ON -> 659/659, 0 failed
SAU  (tip nhánh):  ctest OFF -> 624/624, 0 failed             ctest ON -> 692/692, 0 failed
warning C trong cả bốn build log -> 0
```

Chú ý: ON baseline **659**, không phải 632 của mục 2026-09-15 và không phải 597
của HANDOFF cũ. Cả hai config đều tăng vì `app/tests` được `add_subdirectory`
NGOÀI guard `RTA_BUILD_APP`.

## Sáu commit, mỗi task một commit

| commit | task | nội dung |
|---|---|---|
| `37d8e5b` | A | năm op G11 trong `rta::dsp` — `applyDelay/Polarity/Gain/Biquads`, `sumResponses` với `summationTrust` (KHÔNG tên `coherence`) |
| `1e5bbc4` | B | `spectralCrossover` — giao điểm \|H_A\|=\|H_B\| có gate coherence, nội suy hai điểm, liệt kê MỌI giao điểm |
| `f6b9e52` | C | `crossoverBandFit` — tìm τ trên miền phức, intercept là circular mean, R bounded, trọng số là cross-term |
| `8d8e733` | D | `expectedOffset` trong `app/src/measure/` — bảng §3 dưới dạng MỘT closed form |
| `5a05904` | E | `relativePolarity` trong `rta::ir` — ρ bounded, KHÔNG verdict |
| `2659ee7` | F | hai script khảo sát ρ, **không chốt số nào** |
| `e1419a4` | docs | hai memory + dòng roadmap |
| `c520310` | test | tách `test_virtual_processor.cpp` (395/400) thành hai file |

## Ba điều load-bearing phiên sau KHÔNG suy diễn lại

1. **Bảng §3 giữ NGUYÊN, và không đọc dấu topology từ BẤT KỲ đỉnh tương quan
   nào.** Ruling ở `docs/research/2026-09-15-l7-align-order4-probe.md` §8 (PR #3
   đã merge). `expectedOffset` không dùng `family` trong số học: LR-N là BW-(N/2)
   nối tiếp chính nó nên kế thừa identity với CÙNG N. Cái mà family đổi là
   DESIGNED SUM, không phải offset.
2. **`summationTrust` không được đổi tên thành `coherence`.** Tổng không phải
   ước lượng mà cross-spectrum định nghĩa; đặt tên `coherence` sẽ thêm người ghi
   thứ hai bên cạnh `makeSnapshot()` và guard `coherence_gate_is_not_bypassed`
   phải miễn trừ thay vì đúng-do-cấu-trúc.
3. **ρ ship KHÔNG ngưỡng.** Hai grid đã chạy và **không đồng ý**: grid A có 2 ô
   sai dấu trên 43200 (sàn ρ > 0.0640, nhưng một ô ĐÚNG dấu nằm ở 0.0520 — hai
   phân bố CHỒNG nhau), grid B không có ô sai dấu nào trên 2000 nên không đặt
   được sàn. Đó chính là kết quả `memory/a-threshold-read-off-a-grid-is-that-grids-floor.md`
   cảnh báo. Test E6 (`test_relative_polarity_guard.cpp`) giữ điều này bằng cấu
   trúc, có positive control trên `DelayPolicy.h`.

## VIỆC ĐẦU TIÊN của Wave 3b (tasks G–J) — **ĐÃ LÀM XONG 2026-09-16**

- ~~**`app/CMakeLists.txt` dòng `target_sources(rtatool_snapshot PRIVATE ...)` tại
  `:169` PHẢI thêm `src/measure/CrossoverTopology.cpp` VÀ
  `src/view/CrossoverSurface.cpp`.**~~ **Đã thêm ở commit `b30901f`, cùng với
  `src/trace/VirtualTrace.cpp` — file thứ ba mà mục này không lường tới, cần vì
  specimen vẽ qua `VirtualTrace`.** Plan giao việc này cho task D; Wave 3a CỐ Ý
  không làm, vì ship một source không ai tham chiếu vào target không dùng nó thì
  tệ hơn là ghi lại yêu cầu. Không có cả ba → I7 link lỗi undefined symbol
  (verifier defect 6).
- Task G (`VirtualTrace`) cần A; task H cần B, C, D, E, G; task I cần D, G, H.
- `RTA_REPO_ROOT` đã có sẵn ở CẢ HAI test target (`rta_core_tests`,
  `rtatool_analysis_tests`) — dùng lại cho các grep cấu trúc của H1/I3/I4 thay vì
  viết grep trong tài liệu.

## Ba deviation phải mang lên orchestrator (record cần sửa)

1. **Record §4 sai về khoảng cách các ứng viên τ.** Không phải `τ* ± n/f̄`. `|S(τ)|`
   là một magnitude nên f̄ triệt tiêu thành một phép quay toàn cục; khoảng cách do
   BỀ RỘNG dải quyết định, `1/(N·Δ)`. Đo: cửa sổ 40–160 Hz và 240–360 Hz (cùng 121
   bin 1 Hz, f̄ 100 vs 300) cho ứng viên đầu ở **0.0118209 s ở cả hai**, trong khi
   `1/f̄` đổi gấp ba.
2. **Record §7 sai khi nói ρ "cao" qua một crossover BW2.** ρ **thấp**: đo 0.0676,
   và closed form Parseval dự đoán 0.0675611 — trùng sáu chữ số. Lý do: `|L|` và
   `|H|` gần như không CHỒNG dải. Điều này LÀM MẠNH kết luận của record chứ không
   yếu đi: qua crossover ρ vừa thấp vừa sai dấu.
3. **ALIGN-R1 + tolerance kế thừa.** 1e-12 của record là thuộc tính của fixture
   Butterworth-SOS nó đo trên. Xem memory mới
   `a-tolerance-inherited-from-a-plan-is-that-plans-fixture.md`.

Ngoài ra: plan B1 (hằng 0.7 / 12.4 không biểu diễn được trong `float`), plan C6
(τ tự do trượt tới 2.96e-5 s), plan D6 (word-grep bắt cả prose sẵn có trên cây
sạch) — chi tiết và số đo nằm trong commit message của từng task.

## Cạm bẫy đã trả giá

- **Xoá exe test của CẢ HAI target trước mỗi lần build mutation.** Revert
  mutation của task D rồi chỉ build lại `rta_core_tests` → `rtatool_analysis_tests`
  vẫn là bản mutate và ctest đỏ hai test ở task E.
  (`memory/mutation-testing-needs-the-exe-deleted-first.md`)
- `check_no_std_atomic_shared_ptr.cmake` re-lex file nó quét như chuỗi CMake, nên
  `'\0'` trong test làm nó chết. **ĐÃ FIX trên `main` tại `f93d0dd`** (PR #7) —
  nhánh này ĐÃ merge `origin/main` (`be52a62`) vào nên mang luôn bản vá.
  Cách viết `substr` trong `test_crossover_topology.cpp` giữ nguyên: không
  còn bắt buộc, nhưng cũng không hại và dễ đọc hơn một escape NUL.
---

# 2026-09-16 — **Quy trình GitHub-oriented đã áp dụng; năm PR đã MERGE vào `origin/main` tại `a2de06e`**

**Đọc mục này trước tiên.** Chủ nhân ra lệnh 2026-09-15: quản lý commit theo
GitHub thay vì `main` local. `main` local (36 commit chưa push) đã push
`4b05049→23b7ea0`; từ đó mọi thay đổi đi qua PR + verifier độc lập. Quy ước:
`docs/GIT-WORKFLOW.md` (PR #1). Chủ nhân nói "merge" 2026-09-16; đã merge theo
thứ tự dưới, `gh pr merge --merge`, `origin/main..main` = 0 (đo).

| PR | nhánh | merge commit | nội dung | verify |
|---|---|---|---|---|
| #5 | `ci/portability-fixes` | `00d9571` | CI 3 OS xanh: `AtomicSharedPtr` (libc++ không có `atomic<shared_ptr>`), sàn CMP0057 cho script `-P`, tên test ASCII, `--parallel 4`, assertion Nyquist của weighting | 3 vòng verifier, CI 3/3 xanh tại `512029a` (run 35003607391) |
| #1 | `docs/github-workflow` | `1b0d133` | `docs/GIT-WORKFLOW.md`, PR template, CLAUDE.md | docs |
| #3 | `l7/align-order4-probe` | `0855e8f` | Chốt ALIGN §13.1: đồng nhất N·90° đúng; L4a sai dấu do luật peak-sign của ρ KHÔNG whitened (dự kiến, chưa ship), KHÔNG do PHAT `DelayFinder`, KHÔNG do convention; +5 test core 551→556 | 2 vòng; mutation LR 32/192 đỏ |
| #2 | `l7/align-impl-plan` | `1852dff` | Plan trạm 3 L7-ALIGN, Task A–J, ALIGN-R1..R14 | 2 vòng, 12 lỗi đã sửa |
| #4 | `l7/eq-app-session-verify` | `a2de06e` | EQ Task E/F/G + precision fix; sau merge với main: OFF **591/591**, ON **659/659** (builder đo, clean) | 4 vòng, verdict cuối SOUND |

**CI GitHub Actions ĐANG BỊ CHẶN ở mức tài khoản** từ sau merge #5: mọi run chết
sau 3 s với "recent account payments have failed or your spending limit needs
to be increased" (Billing & plans). #1/#3/#2/#4 merge trên bằng chứng local hai
cấu hình; cây merge `a2de06e` đang được verifier dựng lại độc lập (OFF/ON/fallback).
Chủ nhân cần mở lại Actions trước khi PR kế tiếp có CI.

**Bẫy merge #4 phơi ra (chưa sửa trên main):** guard `no_std_atomic_over_shared_ptr`
(PR #5) đưa NỘI DUNG file nguồn qua tham số `macro()` CMake → re-lex; một hex
escape (`ï`) trong code hay comment làm guard chết vì syntax error thay vì
báo finding. #4 né bằng `static_cast<char>`; fix thật (`function()` +
`PARENT_SCOPE`) đang làm ở nhánh `ci/guard-no-relex`.

**Chủ nhân hoãn (2026-09-16):** duyệt thay đổi assertion `core/tests/test_weighting.cpp`
trong #5 (`isinf` → `> 200 dB` tại Nyquist; mạnh hơn về cấu trúc, yếu hơn tại
đúng một giá trị). Ghi ở `HUMAN-QA-QUEUE`. Đã merge nguyên trạng.

**Repo setting:** `delete_branch_on_merge` = true (bật 2026-09-16). `allow_auto_merge`
KHÔNG bật được — GitHub gắn nó với branch protection, thứ repo private gói free
không có (HTTP 403). Gate merge là thủ tục theo `docs/GIT-WORKFLOW.md`.

**Việc đang chạy khi ghi mục này:** Wave 3a ALIGN (Task A–F, nhánh
`l7/align-wave3a-core`) theo plan PR #2; Task G–J là PR sau.

---

# 2026-09-15 — **L7-EQ Task E/F/G XONG + precision fix — nhánh `l7/eq-app-session-verify`**

**Đọc mục này trước tiên.** Việc-đầu-tiên mà mục "Wave 2 (2026-09-07)" giao cho
phiên sau đã xong: EQ Task E (`EqSession`), Task F (`EqVerify`), Task G (guard),
cộng khoản nợ CONCERN precision của EQ closeout. Ba commit + một commit docs,
**chưa merge, chưa push** lúc viết mục này.

## Baseline đo được (dán từ lệnh, đừng chép số cũ)

Đo trên **cây chưa sửa** `23b7ea0` TRƯỚC khi gõ dòng code đầu tiên, rồi đo lại
trên tip nhánh. Generator Visual Studio 18 2026, MSVC 14.51, JUCE qua
`RTA_JUCE_PATH`. `build-eq-off` (OFF) và `build-eq-app` (ON):

```
TRƯỚC (23b7ea0):  ctest OFF -> 551/551, 0 failed     ctest ON -> 619/619, 0 failed
SAU  (tip nhánh):  ctest OFF -> 564/564, 0 failed     ctest ON -> 632/632, 0 failed
warning C trong cả bốn build log -> 0
```

**+13 Ở CẢ HAI CONFIG, không phải chỉ ON.** Đây là điều plan nói sai và phiên sau
cần biết: `app/tests` được `add_subdirectory` **NGOÀI** guard `RTA_BUILD_APP`
(root `CMakeLists.txt`, có chú thích lý do), nên `rtatool_analysis_tests` biên dịch
trong CẢ HAI config. Task E/F là JUCE-free nên chúng nâng cả OFF lẫn ON. Plan
Task E/F ghi "ON `base_on + N_E`" — đúng phần ON, thiếu phần OFF. ON baseline 619
cũng **không** phải 597 của HANDOFF cũ (597 là mốc trước EQ core).

## Ba điều load-bearing của mục "Wave 2" — chỗ nào trong code tôn trọng chúng

1. **Sign convention.** `EqSession::workingResidualDb`
   (`app/src/measure/EqSession.cpp:53-65`) trả **`m - t + Σ R_i`** THÔ: không
   offset, không negate. Auto-offset `c` và phép negate là việc của
   `EqAllocator` (`core/src/eq/EqAllocator.cpp:203` `negated()`, `:32` `autoOffset()` gọi ở `:213`/`:238`
   ); làm thêm ở tầng app là làm hai lần và lật dấu. Doc comment ở
   `EqSession.h:119-126` nói đúng câu đó cho người sửa sau. Test canh:
   "Auto EQ leaves the ghost closer to target than the measurement" — dấu lật
   thì `after < before` đỏ ngay.
2. **mean-not-median + `EqInput::hHalfGrid`.** `EqSession` mang `hHalfGrid_` và
   nạp vào `EqInput::hHalfGrid` (`EqSession.cpp:92`), **được phép rỗng** —
   fallback đã ghi trong `EqGainSolve.h` (không có G24 gate, không phải "cứ cho
   là Boostable"). Test session dùng span rỗng có chủ ý: G24 gate đã có test
   riêng trong core, đưa vào đây chỉ làm fixture nặng mà không thêm bằng chứng.
   Không đụng gì tới `excessPhase`, nên deviation mean-not-median vẫn nguyên.
3. **Shelves CHƯA đặt.** `EqSession` không tự tạo shelf; mọi `FilterSpec` đến từ
   `EqAllocator` (peaking-only pass này). `EqTextExport` ĐỌC/GHI được cả ba type
   (`lowshelf`/`highshelf`/`peaking`) vì một file text nhập tay có thể chứa
   chúng — nhưng **không có đường nào trong app tự sinh shelf**, nên clamp
   `(Q,gainDb)` trước `designBiquad` (EQ-R5/D6, `c61b5dc` throw
   `std::invalid_argument`) **vẫn CHƯA thực thi** và vẫn là việc của người thêm
   shelf. `filterBandHz` cho shelf trả về nửa dải bên phía shelf (`0..fc` hoặc
   `fc..inf`), KHÔNG dùng công thức bandwidth của peaking — Q của shelf là độ
   dốc, không phải bề rộng.

## Đã hạ cánh

- `1a5df1a` Task E — `app/src/measure/EqTrustMask.h` (`kEqTrustFloor = 0.7`,
  EQ-R2; coherence vắng mặt ⇒ **toàn bộ untrusted**, không phải pass),
  `EqSession.{h,cpp}` (committed set + `applied` mark + exclusion mask + Auto EQ
  + Suggest accept/decline/re-rank + ghost dB-add chính xác),
  `app/src/export/EqTextExport.h` (header-only render/parse; whole Hz, Q 2dp,
  dB 1dp — độ chính xác GHI chính là độ chính xác round-trip).
- `976f0e3` Task F — `EqVerify.{h,cpp}`: `h1SigmaDb` (Bendat & Piersol H1, số
  hiệu phương trình vẫn UNVERIFIED y như L6b §1 mang nó), `compareToPrediction`
  (cờ chỉ bật khi vượt **CẢ** corridor **VÀ** `3σ`; bin untrusted không bao giờ
  bị cờ), và state machine chạy `OutputEngine` THẬT theo đúng thứ tự §6.
- `c8e1769` precision fix (dưới).

## Precision fix: 64× do TAIL-ENERGY, không phải swing

Đo lại bằng chính pure functions của `tools/gen_autoeq_algo.py` (không gọi CLI
driver — `memory/a-gen-script-runs-the-moment-you-invoke-it.md`). F nhỏ nhất mà
TỪNG tiêu chí RIÊNG hội tụ:

| a | D | swing-only | tail-only | both |
|---|---|---|---|---|
| 1.25 | 37 | 1 | 4 | 4 |
| 1.25 | 144 | 4 | 16 | 16 |
| 1.25 | 511 | 8 | **64** | **64** |
| 2.0 | 37 | 1 | 2 | 2 |
| 2.0 | 144 | 1 | 4 | 4 |
| 2.0 | 511 | 4 | 16 | 16 |
| 4.0 | 37 | 1 | 1 | 1 |
| 4.0 | 144 | 1 | 2 | 2 |
| 4.0 | 511 | 2 | 8 | 8 |
| **worst** | | **8** | **64** | **64** |

Cột "both" trùng từng dòng với output của `gen_autoeq.py --check`, nên đây là
đọc lại sweep đã ship chứ không phải mô hình thứ hai. **128× GIỮ NGUYÊN** —
tail-energy CHÍNH LÀ aliasing, và aliasing phá kernel bất kể swing đã nhận ra
hay chưa. Sửa ở ba nơi: docstring `gen_autoeq_algo.py`, amendment §4.3 trong
`docs/dsp/2026-09-06-l7-auto-eq.md` (record vốn KHÔNG có đoạn nào về
oversampling — nó viết ở trạm 2, trước khi Task A đo, nên đây là THÊM chứ không
phải sửa), và comment hằng số `core/include/rta/dsp/ExcessPhase.h`.
**`core/` chỉ đổi một comment** — không đổi giá trị, chữ ký hay hành vi:
`git diff --stat main -- core/` ra đúng một file, 8+/1-, toàn comment.
Golden `core/tests/golden/autoeq.txt` KHÔNG regenerate; `--check` sau đó báo
"byte-identical to a fresh regeneration", SHA-256 `2D791B81…2FD3C4DA` trước và
sau bằng nhau.

## Guard đã làm ĐỎ rồi XANH (Task G, cả hai config)

| guard | scanned | đỏ bằng gì |
|---|---|---|
| `core_has_no_framework_deps` | 142 (không đổi — E/F không thêm file core) | `#include <juce_core/juce_core.h>` vào `ExcessPhase.h` → FAILED, nêu đúng tên file (đỏ ở CẢ OFF và ON) |
| `measure_has_no_framework_deps` | 48 → **54** | JUCE include vào `EqSession.h` (OFF) và `EqVerify.h` (ON) → FAILED, nêu đúng tên file |
| `filter_design_has_no_polynomial_form` | 165 | thêm một dòng chứa `signal.lfilter` vào `gen_autoeq_algo.py` → FAILED ở cả hai config |

Mỗi lần revert xong `git diff --stat <file>` ra RỖNG rồi mới chạy lại xanh.
`coherence_gate_is_not_bypassed` (89), `platform_types` (8),
`output_render_has_no_rt_hazards`, `audioio_scoped_no_denormals_is_first`,
`audioio_callback_has_no_rt_hazards` đều xanh, không đụng tới.

Wave 0 kernel: `git diff --stat main -- core/include/rta/dsp/MinimumPhase.h
core/src/dsp/MinimumPhase.cpp core/include/rta/eq/BiquadDesign.h
core/src/eq/BiquadDesign.cpp` → **RỖNG** (EQ-R5, tái dùng verbatim).

Độ dài file mới (cap cứng 400): EqSession.h 143, EqSession.cpp 146,
EqTrustMask.h 54, EqVerify.h 124, EqVerify.cpp 134, EqTextExport.h 84,
test_eq_session.cpp 286, test_eq_verify.cpp 198.

## Bẫy phiên này trả học phí

- **Một build nền đọc cây ĐANG SỬA.** Baseline ON chạy nền trong khi phiên chính
  sửa `app/tests/CMakeLists.txt`; generator Visual Studio có `ZERO_CHECK` nên
  `cmake --build` TỰ chạy lại configure khi CMakeLists đổi timestamp — build
  "baseline" nuốt luôn file test mới và chết ở link. Số nó cho ra là vô nghĩa.
  Cách chữa đã dùng: commit hết, `git checkout --detach 23b7ea0`, build ON lấy
  baseline thật (619), rồi `git checkout` về nhánh. Object của JUCE vẫn ấm nên
  lần dựng thứ hai rẻ. **Quy tắc: đừng để baseline chạy nền song song với lần
  sửa đầu tiên — đo xong rồi mới gõ.**
- `PinkNoise` nhận `Pcg32`, không nhận sample rate — `EqVerify::Config` vì thế
  mang `noiseSeed` + `excitationDbFsRms` (giống `DelayLocator` nhận source từ
  caller) để một lượt verify tái lập được bit-for-bit.

## Vòng verify độc lập (PR #4) — SOUND-WITH-FIXES, hai defect đã sửa

Verifier (không có Edit/Write) dựng lại toàn bộ trên worktree riêng: bảy claim
đều đứng, mọi số khớp, năm mutation của nó đều đỏ được. Nó tìm ra hai defect
THẬT trong code mới, cả hai không fixture nào chạm tới. Đã sửa TDD trên cùng
nhánh.

**Defect 1 — `applied` tự mâu thuẫn, và session gợi ý lại đúng filter vừa apply.**
`ghostDb()` cộng MỌI committed filter còn `workingResidualDb()` bỏ filter đã
applied, hai hàm đọc chung `measuredDb_`. Sau `markApplied` chúng lệch đúng
bằng `R_applied` (verifier đo: ghost−target 0.115 dB vs residual 3.573 dB) và
`suggest()` trả về y hệt filter đó (fc=993.951, −7.11 dB) → −14.2 dB lên một
bump 8 dB. Tệ hơn: `setMeasurement` xoá `committed_`, nên nhánh "đo lại sau khi
apply" mà doc comment mô tả KHÔNG BAO GIỜ chạy được.

*Semantics đã chốt (một luật, hai tổng):* **`measuredDb_` LUÔN là phép đo mới
nhất, và `applied` khẳng định filter đó ĐÃ NẰM TRONG đường tín hiệu của phép đo
ấy.** Vậy filter applied rời **CẢ HAI** tổng:

```
ghost_k    = m_k + Σ_{chưa applied} R_i(f_k)
residual_k = ghost_k − t_k
```

Dòng thứ hai là một **identity** và giờ có test canh nó ở mọi trạng thái
applied. `setMeasurement` GIỮ `committed_` (cùng mark) và các vùng declined;
vùng declined lưu dạng **băng tần** (`declinedBands_`) chứ không phải cờ theo
bin, nên sống sót qua lưới đổi độ dài. Bắt đầu lại = `clearFilters()` +
`clearExclusions()`, nói rõ ra.

**Defect 2 — verify mù không phân biệt được với verify hoàn hảo.** `trustedBins
== 0` để hai trường RMS ở mặc định `0.0`, và `VerifyReport` không mang số bin
tin cậy nào — đúng
`memory/a-placeholder-for-an-absent-result-erases-its-state.md`. Thêm
`trustedBins` + `std::optional` cho hai RMS + `renderVerifySummary()` in
"no trusted bins".

**Observation 3 đã sửa luôn:** `EqVerify::arm()` giờ KIỂM `setSource`'s bool và
từ chối (`VerifyRefusal::EngineNotQuiescent`, state ở nguyên `Idle`, KHÔNG đụng
routing). **`DelayLocator.cpp:32` có đúng lỗi bỏ sót đó và CHƯA sửa** — ngoài
phạm vi PR này, ghi lại làm việc tiếp theo: một Locate arm lên engine đang bận
sẽ solo + arm nguồn của người khác rồi correlate nhầm excitation.

**Observation 4 (E1 gần như tautology) đã xử:** test mới
"ghost minus target IS the working residual" kiểm identity giữa HAI hàm, không
phải kiểm một hàm bằng chính công thức của nó.

Mutation chứng minh test mới cắn (xoá .exe trước mỗi lần dựng):

| mutation | đỏ ở |
|---|---|
| A — bỏ `if (filter.applied) continue;` trong `ghostDb` (defect 1 nguyên bản) | identity test, `0.00244626728573394 <= 0.00001` "first filter marked applied"; 3 test case / 768 assertion đỏ |
| B — cho `setMeasurement` xoá `committed_` lại | "an applied filter is never re-suggested", `REQUIRE( session.committed().size() == 1 ) ... 0 == 1` |
| C — gán `0.0` cho hai RMS khi không có bin tin cậy | `CHECK_FALSE( blindReport.residualRmsBeforeDb.has_value() ) ... !true` |
| D — bỏ chữ "no trusted bins" khỏi summary | `CHECK( blindText.find("no trusted bins") != std::string::npos )` |

**Bẫy phương pháp verifier tặng, đã ghi vào memory:** mutation nằm trong
**header** KHÔNG được biên dịch lại dù đã xoá .exe — MSBuild báo `MSB8029`
(build tree dưới `%TEMP%`) rồi chỉ relink, không `.cpp` nào đổi timestamp nên
header không được đọc lại. Phải `touch` một `.cpp` cùng TU. Xem
`memory/mutation-testing-needs-the-exe-deleted-first.md` mục mới.

## Vòng verify độc lập THỨ HAI — hai finding nữa, đã sửa

Verifier vòng 2 xác nhận cả ba fix vòng 1 (tự mutate đỏ được từng cái), OFF 568
/ ON 636 khớp, và test flaky pass 5/5 nên "pre-existing timing race" đứng vững.
Nó tìm thêm hai thứ, cả hai đều là **code chưa bị khoá**, không phải code sai.

**Finding 1 — "declined region sống sót qua đổi lưới" KHÔNG có test nào.** Xoá
hẳn hành vi đó đi thì toàn bộ app suite vẫn xanh (32543 assertion). Lý do: test
duy nhất gọi `setMeasurement` hai lần thì không decline gì, test duy nhất
decline thì không đo lại. Đã thêm test đóng khe: decline trên lưới 193 bin
(binWidth 125 Hz), `setMeasurement` lưới 257 bin (93.75 Hz), khẳng định mask
phủ **đúng** tập bin có `f = k·binWidth` nằm trong `[lowHz, highHz]` — dạng
đóng, không đếm theo output. Cùng test xài luôn `clearExclusions()` và
`clearFilters()`, hai hàm trước đó không test và không caller nào gọi.

**Finding 2 — `EqTextExport` vứt bit `applied`.** Export một session có filter
0 applied ra `peaking 994 1.15 -7.1` không cờ; nạp file đó lại vào chính con
DSP đã sinh ra phép đo là **−14.2 dB trên bump 8 dB** — đúng số học của defect 1
vòng 1, rò ra qua biên export. Đã thêm cột thứ năm `applied` (chỉ ghi cho hàng
applied, **tuỳ chọn khi đọc**, hàng không cờ đọc thành not-applied nên file bốn
cột cũ vẫn nhập được). Kiểu mới `rta::eqexport::ExportedFilter` — CỐ Ý tách
khỏi `CommittedFilter` để export/ không include measure/ và ngược lại; caller
copy hai dòng, đúng việc UI sẽ làm.

**DEVIATION phải ghi:** plan Task E chốt format `type, fc, Q, gain`. Cột thứ
năm là đi lệch plan, đã ghi amendment vào record §7
(`docs/dsp/2026-09-06-l7-auto-eq.md`), cùng với đính chính ghost identity
`ghost = m + Σ_{chưa applied} R` (record §7 viết "over every committed filter",
sai từ vòng 1).

**Bốn observation nhỏ đã xử:** (a) trạng thái mark-applied-trước-khi-đo-lại giờ
mang chữ `TRANSIENT` trong TÊN test + một đoạn comment nói rõ ghost nhảy lại lên
đúng bằng gain filter và trạng thái này kéo dài bằng bước 4/5 của thao tác viên;
(b) `renderVerifySummary` guard trên `has_value()` chứ không trên `trustedBins`;
(c) `arm()` khi đang chạy giờ trả `VerifyRefusal::AlreadyRunning` thay vì để lại
refusal cũ; (d) `EqVerify.cpp` include `<cstdio>` cho `std::snprintf`.

Mutation khoá hai finding: xoá `declinedBands_` trong `setMeasurement` →
`CHECK( (excluded[k] != 0) == inBand ) ... false == true`; bỏ token `applied`
khi ghi → `CHECK( parsed[i].applied == filters[i].applied ) ... false == true`.

**Chia file lần hai:** `test_eq_session.cpp` chạm 429 dòng (quá cap 400) nên
tách thành `test_eq_session.cpp` (semantics, 229) +
`test_eq_session_lifecycle.cpp` (decline / đo lại / export, 157), fixture dùng
chung ở `app/tests/EqSessionFixture.h` (92, mọi hàm `inline` vì hai TU include).

## Vòng verify THỨ BA — ba defect nhỏ + một observation, đã sửa

Verifier vòng 3 xác nhận cả bốn claim vòng 2 và cả hai tally (OFF 572 / ON
640), tự chạy lại mutation E và F. Ba thứ mới, đều nhỏ nhưng đều là **thông tin
bị mất âm thầm**:

1. **BOM UTF-8 viết lại TYPE của hàng đầu tiên.** Notepad và PowerShell 5.1
   (`Out-File` / `Set-Content`) mặc định ghi BOM; nó dính vào token đầu, và
   fallback cũ của `typeFromName` biến `BOM+lowshelf` thành **Peaking** —
   một filter KHÁC, nằm trên rig, không ai được báo. Sửa: strip BOM trước khi
   parse, và `typeFromName` trả `std::optional` — **không còn fallback**. Từ
   khoá lạ ⇒ hàng bị **từ chối kèm số dòng** (`FilterListParse{filters,
   rejectedLines}`), vì hàng bị bỏ âm thầm cũng là state bị xoá. CRLF không
   cần xử lý: `'\r'` là whitespace với `operator>>` (verifier đã dò 10 biến
   thể line-ending, đều an toàn).
2. **`renderVerifySummary` in câu SAI cho một trong hai trạng thái vắng.**
   Guard `has_value()` thêm ở vòng 2 dùng CHUNG nhánh với `trustedBins == 0`,
   nên một report có `trustedBins = 4` vẫn in "no trusted bins (4 bins
   measured, all under the coherence floor)" — và test vòng 2 KHOÁ LUÔN câu
   sai đó. Tách hai nhánh: `trustedBins == 0` giữ câu cũ; `trustedBins > 0`
   mà thiếu RMS in "N of M bins trusted, residual not computed".
3. **Assertion rỗng.** `CHECK(text.find("applied") != npos)` không bao giờ đỏ
   được vì header luôn chứa từ đó. Đổi sang khẳng định trên ĐÚNG DÒNG:
   `"\npeaking 994 1.15 -7.1 applied\n"`, và dòng không-applied phải KHÔNG có
   token.
4. **Observation — test grid-survival không khẳng định được tính bao gồm của
   biên.** Nó so với chính `>=`/`<=` của implementation, và trên fixture đó
   không bin nào rơi đúng biên (gần nhất lệch 2.23 Hz) nên `>=`→`>` vẫn xanh.
   Đã thêm fixture **dựng, không dò**: lấy `h = 1/(2Q) = 3/4` ⇒ `1+h² = 25/16`
   ⇒ `sqrt = 5/4` CHÍNH XÁC, nên `low = fc/2`, `high = 2·fc` đều đúng bit. Với
   lưới 193 bin (binWidth đúng 125 Hz) và `fc = 1000`: low = 500 = bin 4,
   high = 2000 = bin 16, cả hai biểu diễn chính xác ở float lẫn double, không
   tolerance chỗ nào. Test tự kiểm (`REQUIRE(band.lowHz == 500.0)`) để nếu số
   học thôi chính xác thì nó báo chứ không lặng lẽ test hụt biên.

Mutation khoá bốn thứ trên: G (trả lại fallback Peaking) → `REQUIRE(
parsed.filters.size() == 1 ) ... 3 == 1`; H (bỏ strip BOM) → `CHECK(
withHeader.rejectedLines.empty() ) ... false`; I (gộp lại hai nhánh summary) →
`CHECK( noResidualText.find("residual not computed") != npos )` đỏ; J (`>=`
thành `>`) → `CHECK( excluded[kLowBin] != 0 ) ... 0 != 0` đúng hai bin biên.
Chạy lại F sau khi sửa assertion rỗng: nay đỏ ở `CHECK( text.find(
"\npeaking 994 1.15 -7.1 applied\n") != npos )`.

**API đổi:** `parseFilterList` trả `FilterListParse` thay vì
`std::vector<ExportedFilter>`. Bốn call site trong test đã đổi theo.

## Vòng verify CUỐI — SOUND, một minor đóng nốt

Verifier cuối dựng lại 575/643, mọi mutation đỏ đúng như khai. Còn một minor +
hai việc ghi chép:

1. **Dòng trắng có indent bị tính là hàng hỏng.** Parser chỉ xét `line.front()`
   nên ba dấu cách, một tab, hay một comment người ta canh lề bằng tay đều rơi
   vào `rejectedLines` (verifier đo: `rejected=[2]` cho ba dấu cách). Sửa: tìm
   ký tự không-whitespace ĐẦU TIÊN rồi mới quyết định đó là dòng gì. **Báo động
   giả không vô hại** — rejected line là một cảnh báo, và cảnh báo kêu nhầm là
   cách cảnh báo thật sau đó bị bỏ qua.
2. **Cột thứ năm: CHỌN "từ chối", không phải "ghi chú ngoại lệ".** Verifier cho
   hai lựa chọn; chọn từ chối vì ngoại lệ ấy chính là cái hại mà cột này sinh ra
   để chặn: `appllied` gõ sai ⇒ đọc thành not-applied ⇒ thao tác viên land lại
   filter rig đã có ⇒ đúng −14.2 dB. Nay: flag CÓ MẶT mà không đọc được (kể cả
   token thừa phía sau) ⇒ **từ chối hàng**. Flag VẮNG MẶT vẫn đọc là not-applied
   — đó không phải đoán, đó là hình dạng bốn-cột cũ của chính format, và điền
   theo hướng an toàn (filter hiện ra để người ta thấy, thay vì bị giấu đi như
   đã xử lý). **Vắng mặt có nghĩa xác định; hiện diện mà không đọc được thì
   không** — hai thứ khác nhau, không xử như nhau. Đã ghi vào record §7.3.
3. **Memory `mutation-testing-...` thêm mục nửa-RESTORE.** Touch một `.cpp` để
   mutation ĐƯỢC biên dịch vào; không có gì bắt nó biên dịch RA. Khôi phục
   header xong, object build từ bản mutated vẫn nằm đó và MSBuild chỉ relink —
   cây sạch, `git diff` rỗng, mà binary vẫn mang mutation. Đó là kiểu hỏng tệ
   hơn vì nó đến ở CUỐI chu trình, lúc mọi thứ trông đã đúng. Luật: **rebuild
   TOÀN BỘ sau lần revert cuối**, và **md5 file đã mutate với blob `HEAD`**
   (`git status` không thấy được một revert sai mà byte-identical). Phiên này
   làm đúng vậy: `EqSession.cpp` và `EqVerify.cpp` md5 trùng HEAD
   (`681479c3…`, `e98f34c4…`), OFF dựng lại `--clean-first`.

Mutation vòng này: K (chỉ xét `front()`) → `CHECK(parsed.rejectedLines.empty())
... false`; L (cột năm đoán lại) → `REQUIRE(parsed.filters.size() == 2) ...
4 == 2`.

## Còn mở

- **CHƯA MERGE, CHƯA PUSH** lúc viết. Nhánh `l7/eq-app-session-verify` từ
  `23b7ea0`; PR mở lên `main`, KHÔNG tự merge.
- **`DelayLocator::arm()` bỏ qua `setSource`'s bool** (`DelayLocator.cpp:32`) —
  cùng lỗi với observation 3, chưa sửa, chưa có test cho đường non-quiescent.
- **Lane lớn kế tiếp: Wave 3 (ALIGN)** — G11 virtual processor (nó tiêu thụ đúng
  `std::vector<FilterSpec>` mà `EqSession` giữ), G17 wizard (HỎI topology),
  G18 crossover, ρ fold. Câu hỏi order-4 (ALIGN §13.1) vẫn chờ chủ nhân.
- **EQ chưa có:** shelf placement + domain clamp (trên), panel JUCE sống trong
  `MainComponent` (EQ-R4 nói là specimen dev-preview — specimen `EqPreview.*`
  CŨNG CHƯA dựng, phiên này chỉ làm phần ctest-provable), overload `FirDesign`
  nhận per-bin `Σ R_i` (record §7 amend FIR §11), re-linearise lần hai.
- Judgement chờ duyệt vẫn nguyên: `kEqTrustFloor = 0.7` là interim cho tới L5b,
  `G_cap +6 dB`, `Q_max` 10/20, `N` cap 6, NotMinimumPhase→V2, −120 dB floor.

---

# 2026-09-06 — **L6b: HAI NOTE F3 ĐÃ ĐÓNG (chưa commit, working tree trên `claude_desk/tiepto-2d388b`)**

**Đọc mục này trước tiên.** Hai việc-còn-mở của L6b (verifier F3 NOTE) đã đóng.
Thay đổi nằm trong working tree, **chưa commit** — 7 file, `git diff --stat` ra
192 insertions / 20 deletions.

**Item 1 — không phải "đọc Member", mà là UB thật.** `drainPaired` chặn route
vị trí ≥ `kMaxTransferFunctions` (8) khỏi audio, nhưng `syncAverageGroupMembership`
KHÔNG chặn — nó nạp mọi route vào `addMember`, nên route 9+ chung reference thành
member, rồi `publishAverageGroup` index `analysers[8+]` trên vector đúng 8 phần
tử = đọc heap ngoài biên. `kMaxChannels = 64` nên reachable trên card Dante/MADI
thật. Sửa: cap cả hai vòng (dự đoán + rebuild) ở `i < kMaxTransferFunctions`;
route quá cap nhận `Membership::ExcludedOverCapacity` (nhãn "CAP" ở RoutingMatrix).
Hằng số `kMaxTransferFunctions` dời từ `AnalysisThread.h` xuống `RoutingPlan.h`
(publish layer không include ngược được). Bài học:
`memory/a-cap-checked-on-the-drain-path-is-unchecked-on-the-publish-path.md`.

**Item 2 — test-gap.** Test 3-route cũ nuôi cả ba analyser cùng audio (average
2-way = 3-way, không bắt được rò rỉ — đúng bẫy fixture-quá-ngoan). Thêm test số
học: route bị từ chối nuôi audio khác (gain 0.1 vs 0.7), khẳng định average
publish **đúng bằng** `spatialAverage(2 member)` và **khác** `spatialAverage(3)`.

## Baseline đo được (dán từ lệnh)

Build dir `build-verify-app` (VS 18 2026, MSVC 14.51, RTA_BUILD_APP=ON, JUCE qua
`RTA_JUCE_PATH`), rồi verifier độc lập configure lại `build-vfy-closeout` từ đầu:

```
cmake --build build-verify-app --config Release --parallel   -> 0 warning C
ctest --test-dir build-verify-app -C Release                 -> 511/511, 0 failed  (ON)
```

511 = 508 baseline của lane L6b + 3 test mới trong `test_analysis_publish.cpp`.
TDD chứng kiến fail đúng chỗ: Item 1 RED `10 == 8`; Item 2 mutate `addMember` bỏ
refusal → test số học FAILED (average thành 3-way) → revert → GREEN. Verifier
đối kháng (không Write) CONFIRMED, chạy lại 511/511 trên build dir riêng và làm
đỏ được cả hai guard. `measure_has_no_framework_deps` vẫn PASS (test #451/#... ).

## Bẫy phiên này trả học phí
- **Verifier có Bash vẫn sửa được cây dù không có Write.** Nó chạy
  `git checkout -- AnalysisPublish.cpp` để revert mutation probe, nhưng fix chưa
  commit nên lệnh đó reset file về HEAD (pre-fix), xoá luôn fix. Nó tự phát hiện,
  reconstruct lại; phiên chính tái kiểm độc lập (rebuild từ cây hiện tại: 158/158)
  và `git diff --stat` khớp đúng bản gốc. Bài học:
  `memory/a-verifier-with-bash-can-git-checkout-your-uncommitted-fix.md`. **Commit
  trước khi dispatch verifier chạy lệnh git.**

## Còn mở
- **ĐÃ MERGE VÀO `main` VÀ PUSH LÊN `origin`, 2026-09-06.** Chủ nhân ra lệnh
  "merge to main and push" cuối phiên. Merge `--no-ff` trong primary worktree
  (`git -C`, đã kiểm primary sạch trước) tại `ed2a5e0`; `git push origin main`
  → `7122070..ed2a5e0`; `git rev-list --count origin/main..main` → 0 (đo, đừng
  chép). Hai commit của phiên: `404739a` (fix) + `cbca9bf` (docs).
- ~~Ba câu hỏi chủ nhân L6b~~ **ĐÃ TRẢ LỜI 2026-09-06** (xem `HUMAN-QA-QUEUE`
  mục L6b): (1) capture gate = giữ report-only, SysTune soft down-weight →
  `docs/UPGRADE-BACKLOG.md`; (2) remote API = localhost mặc định + read-only
  trước, write routing → backlog; (3) generator output = **gộp vào L7**.
- Lane lớn kế tiếp theo master plan: **L7 (Solvers)** — cần research trạm 1
  (chưa có decision record). Prompt mở lane đã soạn trong phiên này. Theo quyết
  định (3), trạm 1 của L7 **mở bằng research đường output lock-free** trước
  (prerequisite cho mọi solver + G20). G17 là câu hỏi PHA (wizard hỏi topology).

---

# 2026-09-06 (tối) — L7 (Solvers) TRẠM 1+2 XONG cho cả năm sub-lane — nhánh `claude_desk/l7-solvers-station-1-874518`

**Đọc mục này trước.** Phiên orchestrator L7 (Opus) đi trạm 1 (research) và trạm 2
(decision record) cho toàn bộ lane L7, chia **năm sub-lane**. **Chưa viết một dòng
code nào** (trạm 3 chưa mở). Mọi claim mã-nguồn trong record đã qua verifier độc lập
(đọc file thật, không đọc record). Nhánh đã fast-forward absorb `main` tại `4b05049`.

## Quyết định của chủ nhân trong phiên này
1. **Q3 — đường output generator GỘP VÀO L7, research TRƯỚC** (đã có trên `main` qua
   `cbca9bf`; phiên này xác nhận và mở sub-lane **L7-OUT** làm prerequisite).
2. **Hai tiền đề chưa xây GỘP vào sub-lane phụ thuộc**: G24 (min/excess-phase) vào
   **L7-EQ**; relative-polarity ρ vào **L7-ALIGN**. Không xây lane riêng.
3. **Lane split = Wave 0 shared-foundation trước**, rồi solver theo sóng (dưới).
4. **Solo default = CÓ setting, mặc định option 1**: sequencer/auto-step strict
   single-output solo; manual toggle additive. Trả lời chung OUT §13.2 + DELAY §14.2.

## Năm research doc + năm decision record
- research: `docs/research/2026-09-06-l7-{output-path,auto-delay,auto-eq,fir-export,alignment-wizard}-station1-research.md`
- record:  `docs/dsp/2026-09-06-l7-{output-path,auto-delay,auto-eq,fir-export,alignment-wizard}.md`
- Đọc record TRƯỚC research. L7-OUT là interface mà bốn record kia trích dẫn.

## Thành phần core DÙNG CHUNG (Wave 0) — ba record hội tụ độc lập
`MinimumPhase` (EQ + FIR), `FilterSpec` (EQ/FIR/ALIGN), biquad design/response
(EQ `designBiquad` + ALIGN `BiquadResponse`). Verifier xác nhận **chưa có cái nào
trong core hôm nay**. Xây MỘT LẦN ở Wave 0, tránh ba lane cùng thêm file chồng nhau
vào `core/CMakeLists`.

## Kế hoạch sóng (trạm 3 viết impl plan theo đây)
- **Wave 0**: shared foundation (MinimumPhase, FilterSpec, BiquadDesign, BiquadResponse) — core-only, một integration.
- **Wave 1**: L7-OUT (platform/app, KHÔNG đụng core) ∥ L7-FIR (core+app).
- **Wave 2**: L7-DELAY ∥ L7-EQ (cả hai cần OUT; serialize integration core).
- **Wave 3**: L7-ALIGN (cần OUT + DELAY + biquad dùng chung + ρ).

## Wave 0 ĐÃ XÂY VÀ VERIFY (2026-09-07)
Bốn commit trên nhánh: `b2172b3` BiquadResponse, `24e3093` MinimumPhase (cepstral,
log KHÔNG halved, floor −120 dB), `08723bd` FilterSpec + RBJ `designBiquad`,
`c61b5dc` fix domain guard shelf. **470/470 OFF** (build-l7w0, Visual Studio gen,
`--clean-first`), 0 warning. Acceptance TOÀN closed-form, KHÔNG golden vector.

Verifier độc lập (scratch worktree riêng tại `08723bd`) không bác được gì: rebuild
469/469 (trước fix shelf), tolerance nới VẪN giết mutation (perturb `A` divisor →
192/192 đỏ ở 1e-9; bỏ fold-doubling → 2060/4096 đỏ ở 3e-7), fixture MinimumPhase
sửa cho conjugate-symmetric là HỢP LỆ (đường vào thật của kernel là `|FFT(h_lin)|`
của dữ liệu real nên đối xứng theo cấu trúc; `Re(IFFT(L))` chỉ là phần chẵn của L).
Guard còn canh: framework 117 file, polynomial 135.

**Bug thật đã sửa (`c61b5dc`):** `designBiquad` shelf Q cao + cut lớn (vd Q=8,
−15 dB) cho radicand `(A+1/A)(1/Q−1)+2 < 0` → alpha NaN → coefficient NaN.
`validate()` nay throw `std::invalid_argument` (đúng convention có sẵn) cho shelf
ngoài miền; Peaking KHÔNG dính (alpha peaking không có gain term).

**Tech-debt còn mở (KHÔNG sửa, ghi lại):** `Biquad.h::maxPoleRadius` dùng
`std::max(0.0, NaN)` = 0.0 nên một filter NaN đọc thành "ổn định tối đa". Đã MOOT
trên đường shelf (NaN không còn sinh ra) nhưng là bẫy cho MỌI nguồn NaN sau. Không
sửa vì `Biquad.h` là file frozen; cần một task riêng nếu chủ nhân muốn.

**~~Record touch-up còn nợ (closeout)~~ — ĐÃ TRẢ 2026-09-15.** ALIGN record §5 nói
`|H|` = `sectionAttenuationDb` nhưng field đó là attenuation (+=xuống); plan W0-R3
đã khoá đúng `−attenuationDb`. Đã sửa cả §5 lẫn §10 mục 3. Tolerance giữ nguyên
`1e-12` như plan W0-R3/T3 — đo lại: khoá đó ĐẠT 1e-12 (384/384 assertion), 1e-13
thì 3 cái đỏ. Test đang dựng lại assert `1e-9`
(`core/tests/test_biquad_response.cpp:78`, từ `b2172b3`, không có lý do ghi lại);
§5 ghi rõ chênh lệch đó, KHÔNG sửa test (core ngoài scope PR này).

**Order-4 (ALIGN §13.1) — ĐÃ SETTLE 2026-09-15, chủ nhân không phải trả lời.**
Probe độc lập: `docs/research/2026-09-15-l7-align-order4-probe.md`, script
`tools/probe_align_order4.py`, khoá CI `core/tests/test_align_order4_identity.cpp`.
Identity `N·90°` ĐÚNG chính xác tới máy (analog `0.00e+00°`, digital `1.16e-11°`);
L4a đo sai dấu bậc 4 vì fixture là **hai box band-pass** chứ không phải cặp
crossover matched-cutoff, và **không** convention nào chạm tới được bậc chẵn.
**Wave 3 (ALIGN) hết chặn.** ctest 551 → 556 (build dir `build-probe`,
`-DRTA_BUILD_APP=OFF`), đã làm đỏ một lần rồi xanh lại.

## Wave 1 ĐÃ XÂY VÀ VERIFY (2026-09-07) — FIR rồi OUT (tuần tự, tránh git-index race)

**Số cuối Wave 1: OFF 505/505, ON 571/571, 0 warning cả hai** (Wave 0 tip: 470 OFF).
FIR: 470→489 OFF, →553 ON. OUT: 489→505 OFF, 553→571 ON. Đo, không chép.

**FIR (G10) — commit `4facbd8..74bd03a` + `d0246fe`.** Freq-sampling linear +
min-phase (dùng lại `minimumPhaseFromMagnitude` verbatim), log-f interp, text +
32-bit-float WAV export (`app/src/export/`), golden `fir.txt` (scipy second author,
`gen_fir.py --check` byte-identical). Verifier độc lập: 489/489, guard canh (framework
122, polynomial 142 gồm `gen_fir.py`), symmetry test bắt được mutation, T7 N=511 là
trần mô hình thật (không phải né tolerance).

**⚠️ G24 CAVEAT — LOAD-BEARING cho Wave 2 (EQ).** FIR ship oversampling cepstral
**8×**. Bằng chứng "magnitude-identity residual phẳng mọi factor" mà builder đầu nêu
là **tautology đại số** (`Re(FFT(fold(c)))==FFT(c)`, không phân biệt factor đủ/thiếu)
— comment `FirDesign.cpp` đã sửa (`d0246fe`). Bằng chứng THẬT: hội tụ impulse
min-phase; 8× đủ **CHỈ VÌ** `designLinearPhaseCore` cửa sổ hoá target TRƯỚC, giới hạn
độ sắc notch. **G24 (excess-phase trong L7-EQ) DÙNG CHUNG kernel này nhưng có thể nạp
magnitude ĐO THÔ không qua cửa sổ → 8× có thể alias → G24 phải TỰ biện minh
oversampling, KHÔNG tái dùng lập luận 8× của FIR.** (verifier Wave 1)

**OUT (output path) — commit `881862b..16ec825`.** `RampedGain::prepare`,
`OutputEngine` (platform/types, JUCE-free), callback render MỘT dòng sau
`pushFromCallback` (`ScopedNoDenormals` vẫn câu đầu), `OutputPolicy` strict-solo/
additive. Verifier: **render RT-SAFE** (không alloc/lock/IO/FFT — scratch prealloc ở
ctor, `std::visit` trên source trivially-copyable, `Sweep::buildInverseFilter` không
với tới được), guard mới `check_no_rt_hazards` (render unconditional + callback ON)
đỏ-rồi-xanh. `kRequestedOutputChannels` 2→`kMaxChannels`, không over-read.

**Hai ghi chú OUT phiên sau cần biết:**
1. **G20 auto solo/mute là KHẢ NĂNG + binding pattern, CHƯA nối UI sống.** Không
   `CaptureSequencer` nào có chủ UI hôm nay; nối `onStep` vào MainComponent = phải
   dựng panel sequencer (record §12 ngoài phạm vi). `soloOutput` + pattern chứng minh
   device-free ở `test_output_policy.cpp`. Solo default = **SETTING, mặc định option 1**.
2. **Test "complementary handover" chứng minh reversal-continuity, KHÔNG chứng minh
   HÌNH DẠNG ramp** (mọi đường đối xứng lẻ thoả `g(p)+g(1-p)=1`). Hình dạng do hai
   test anh em bắt (raised-cosine closed-form + ramped-sine, cả hai đỏ dưới mutation).
   Không lỗi sống; đừng lặp lại claim "handover chứng minh raised-cosine".

**Tech-debt Wave 0 vẫn mở:** `Biquad.h::maxPoleRadius` dùng `std::max(0.0,NaN)`=0.0
(moot trên đường shelf sau fix `c61b5dc`, nhưng bẫy cho nguồn NaN khác; `Biquad.h` frozen).

## Wave 2 (2026-09-07): DELAY XONG+verify; EQ CORE (A-D) XONG+verify; EQ E/F (app) CHƯA XÂY

**Số:** DELAY OFF 505→529 / ON 571→597. EQ core OFF 529→551 (ON chưa đo — E/F chưa
xây). 0 warning mọi nơi. Tuần tự DELAY rồi EQ (git-index + core/CMakeLists contention).

**DELAY — commit `7e1c209..753d522`, verify SOUND.** `suggestDelay` (policy trên
`findDelayPhat`, refactor bit-for-bit qua `PhatCorrelation.h` private — verify diff
verbatim vs `6d22342^`), trust=peak/f_band vs null floor `√(ln m/M_in)` c=4 (survey
105 trial 0 sai), `ResidualDelayTracker` (đọc γ² không ghi, zero-alloc chứng minh
bằng counting allocator THẬT), `RawCaptureBuffer` + `DelayLocator` (Mls từ chối,
strict solo qua OutputEngine). Sub-sample gap frac 0.3/0.7 ~0.19 mẫu = bias nội suy
parabol 3 điểm đã biết (≈4µs@48k, bỏ qua được), KHÔNG phải bug (verify đo độc lập).

**EQ CORE (A-D) — commit `7756da9..6940f92`, verify SOUND.** `excessPhase` (rta::dsp,
EQ-R1) + `classifyDip` (rta::eq, `S*=2·asin r_D`) + `solveGains` (ridge Cholesky,
golden mang cond) + `EqAllocator` (greedy peaking placement + autoEq). Mọi claim
load-bearing mutate-test ĐỎ-ĐƯỢC: sign/negation (bỏ negate → residual TĂNG, D5 đỏ),
S* threshold, hai guard. Wave 0 kernel diff RỖNG (tái dùng verbatim).

**⚠️ G24 CAVEAT ĐÃ CHỨNG MINH (không bị bác):** oversampling cho raw measured magnitude
cần tối thiểu **64×**, ship **128×** (= 16× của FIR 8×). Nếu G24 mù quáng tái dùng 8×
của FIR thì min-phase null test ALIAS trên phép đo thật → phân loại dip sai. Đúng là
thứ pipeline sinh ra để bắt.

**CONCERN precision — ĐÃ SỬA 2026-09-15** (đo lại, bảng chín fixture trong mục đầu
file; `gen_autoeq.py --check` byte-identical sau khi sửa). Nguyên văn dưới đây giữ lại:

**~~CONCERN precision (SỬA ở EQ closeout — CHƯA sửa)~~:** 64× bị đẩy gần như HOÀN TOÀN
bởi metric phụ **tail-energy** (proxy nhiễm aliasing cepstral), KHÔNG bởi excess-phase
**swing** mà `classifyDip` thực đọc (swing hội tụ ở 8×-16× mọi fixture). 128× vẫn đúng
và bảo thủ hợp lý, nhưng docstring `tools/gen_autoeq_algo.py` + framing record "64× là
F nhỏ nhất swing hội tụ" KHÔNG chính xác — phải sửa thành "tail-energy quyết định 64×".

**EQ E/F ĐÃ XÂY 2026-09-15** (nhánh `l7/eq-app-session-verify`, xem mục đầu file).
Ba điều dưới đây vẫn đúng và ĐÃ được tôn trọng trong code — giữ lại làm lịch sử.

**~~EQ E/F CHƯA XÂY~~ (app, ON) — việc đầu tiên của phiên sau:** Task E (`EqSession` +
trust mask `kEqTrustFloor=0.7` + text export) và Task F (`EqVerify` qua OutputEngine).
Builder hết budget sau core. BA điều người xây E phải biết:
1. **Sign convention (LOAD-BEARING):** ghost `m+ΣR` chỉ hội tụ khi solve fit
   `−workingResidual`; `EqAllocator` đã negate trước khi gọi `solveGains`. Xác nhận
   với EQ-R3 TRƯỚC khi ghost của E land.
2. Deviation đã ship: **mean KHÔNG median** cho broadband delay (record §4.3.4 nói
   median; đo 57.5 mẫu lệch vs 0.28 → chuyển mean). `EqInput` thêm field `hHalfGrid`.
3. **Shelves CHƯA đặt** (chỉ peaking pass này) — shelf domain clamp (EQ-R5/D6) chưa
   thực thi; người thêm shelf phải clamp `(Q,gainDb)` trước `designBiquad` (nó throw
   `std::invalid_argument` ngoài miền — Wave 0 `c61b5dc`).

**Bẫy build (verifier gặp 2 lần, đã thành memory):** `cmake --build --target X` báo
"-> X.exe" mà KHÔNG relink thật (exe byte-identical) → mutation test đọc binary cũ,
false PASS. Xoá `.exe` TRƯỚC mỗi rebuild khi mutation-test. Xem
`memory/mutation-testing-needs-the-exe-deleted-first.md`.

**Còn lại của L7:** EQ E/F (app), rồi **Wave 3 (ALIGN)** — G11 virtual processor +
G17 wizard (HỎI topology) + G18 crossover + relative-polarity ρ fold (dựng lại ngưỡng
hai lưới độc lập, đừng ship số một-lưới). ALIGN record `docs/dsp/2026-09-06-l7-alignment-wizard.md`.

**2026-09-15 — L7-ALIGN station 3 plan written: `docs/plans/2026-09-15-L7-align-impl-plan.md`**
(nhánh `l7/align-impl-plan`, PR vào `main`). Mười task A–J, mười reconciliation
ALIGN-R1..R10 cần orchestrator sửa record trước khi build (đáng chú ý: record §5 gọi
`sectionAttenuationDb` — field đó KHÔNG tồn tại, chỉ có `BiquadCascade::attenuationDb`
dấu ngược, `Biquad.h:75,80,87`).

**Cùng ngày, sau vòng verify đối kháng (SOUND-WITH-FIXES, 12 defect đã sửa hết):**
thêm ALIGN-R11..R14 — R11 thu hẹp block Wave 3 của `HUMAN-QA-QUEUE.md:82` (cần chủ
nhân/orchestrator phê), R12 record §10.4 `R=1−1e-12` KHÔNG thoả đồng thời với dung sai
τ (`1−R ≃ (πΔf·dτ)²/6`; cần `dτ ≤ grid/154`, không phải `grid/20`), R13 câu `(−s)^N/D`
của record §3 là ánh xạ đồng nhất ở mọi bậc CHẴN nên không giải thích được bậc 4,
R14 hợp đồng đối số **A = phía HP, B = phía LP** (record không nói; đảo là lật dấu τ
lẫn φ₀ trong im lặng).

**Order-4 ĐÃ NGÃ NGŨ** theo PR #3 (`docs/research/2026-09-15-l7-align-order4-probe.md`,
~~CHƯA merge~~ — **ĐÃ MERGE tại `0855e8f`**): identity `N·90°` đúng tuyệt đối theo convention của repo, bảng §3 giữ
nguyên; dấu sai của L4a là do quy tắc **dấu-đỉnh tương quan KHÔNG whitened** của
L4a decision 6b (`docs/dsp/2026-08-30-sweep-ir-l4a.md:1195` — ρ; ~~estimator repo này
CHƯA ship; `relativePolarity()` không tồn tại ở `core/` lẫn `app/`~~ — **ĐÃ SHIP
2026-09-16: `rta::ir::relativePolarity` vào ở Wave 3a commit `5a05904`, bounded,
KHÔNG verdict, KHÔNG ngưỡng**) áp qua hai hệ khác
passband, KHÔNG phải do convention. **Correlator repo ĐANG ship (PHAT `findDelayPhat`,
`DelayFinder.cpp:34`) KHÔNG tái tạo L4a — nó đọc gương lại: đúng ở bậc 4, sai ở bậc 8.**
Hai correlator bất đồng trên cùng một cặp loa ở 2/6 bậc → luật: **đừng đọc dấu topology
từ BẤT KỲ đỉnh tương quan nào, whitened hay không**. (PR #3 sửa quy kết ở head `cdd8a25`
sau khi verifier của chính nó bác bản đầu; plan đã đồng bộ.) Ba chỗ tiêu thụ phán quyết,
mỗi chỗ có fixture đỏ được: **D5, H9, I1b**; thêm **E9** dựng lại đúng hình học L4a làm
documented failure của ρ. Không còn task nào "probe-dependent".

## Verifier đã xác nhận (đọc file thật)
- OUT 5/5: RampedGain non-movable (static_assert biên dịch thật); callback xoá output
  + `ScopedNoDenormals` là câu ĐẦU + guard `audioio_scoped_no_denormals_is_first`;
  sáu source RT-safe; `Oscillator.h:69-77` "one ramp for the whole generator".
- EQ/ALIGN/DELAY 8/8: coherence gate CHƯA xây (chỉ mockup dev-preview
  `TargetMatchPreview.cpp`); không MinimumPhase trong core; không RBJ design (chỉ
  Biquad apply-only); `Biquad.h` né `<complex>`; `Trace` ba vector real, không
  VirtualTrace; `findDelayPhat` 0 caller trong `app/` + không có raw-capture path;
  `DelayEstimate::peak` whitened 1e-10 khác ρ; `Mls` tuần hoàn.

## Ba quyết định phiên sau KHÔNG suy diễn lại
1. **G24 null test: a<1 = comb MIN-phase (boost được), chỉ a>1 mới NON-min-phase.**
   `|1+a·e^{-jθ}| = a·|1+(1/a)·e^{-jθ}|` nên hai loại CÙNG magnitude, chỉ pha tách.
   Ngưỡng = swing excess-phase phụ thuộc độ sâu `S*=2·arcsin r_D`, KHÔNG phải sàn lưới.
2. **Topology là MỘT closed form**: BW-N HP dẫn LP `N·90°` ở mọi tần số; LR-N kế thừa.
   Wizard HỎI topology + hỏi thêm "processor đã đảo một output chưa" (180° wiring =
   180° topology, đo không phân biệt được). Bác "maximize measured sum" (tái suy
   topology từ đo). Sửa lỗi research D1/D6: BW2 chưa đảo là NULL, đảo mới +3 dB.
3. **Auto-delay hai mode do TOÁN ép**, không phải taste: Locate (linear corr, span
   riêng, không coherence) vs Track (circular corr trên `Sxy` averaged, coherence là
   trọng số). Bác first-arrival-fraction (PHAT tạo ghost đảo pha ở `D₁−Δ` cao ≈ a/2)
   và phase-slope (cần unwrap, cấm trong core theo L2 §6).

## Câu hỏi chờ chủ nhân (KHÔNG chặn trạm 3 Wave 0/1)
- **Order-4 mâu thuẫn** (ALIGN §13.1): identity `N·90°` dự đoán ĐÚNG dấu ở BW4, nhưng
  L4a ĐO sai dấu ở bậc 2 VÀ 4. Cần trí nhớ chủ nhân về fixture L4a, hoặc một ô grid.
  Chỉ ảnh hưởng Wave 3 (ALIGN). Đề xuất: probe settle trước khi ALIGN build.
- Judgement record tự chọn default, chờ duyệt: `G_cap +6dB` / `Q_max` 10-20 (EQ §12.2),
  N cap (EQ §12.3), NotMinimumPhase→V2 (EQ §12.4), −120dB floor cho `|H|` đo (EQ §12.5),
  plausibility window Locate (DELAY §14.1), tracker on-by-default (DELAY §14.3),
  64-output hardware check (OUT §13.1), device-reconfig-while-armed (OUT §13.3, đã chọn default).

## Việc còn mở

> **Cập nhật 2026-09-16 bởi closeout L7 (bổ sung 2026-09-17):** gạch đầu dòng ngay
> dưới đây nói "CHƯA push" và "`origin/main..main` > 0". **Cả hai đã sai.** `main`
> local đã push 2026-09-15 (`4b05049→23b7ea0`), và `git rev-list --count
> origin/main..main` đo hôm nay ra **0**. Từ đó lane này đi tiếp hoàn toàn qua PR —
> #4 / #8 / #9 — và `a937a98` giờ là **lịch sử, không phải chỗ để đi tìm cái gì**
> (`docs/plans/MASTER-EXECUTION-PLAN.md:42-43` nói đúng câu đó). Đừng đi push theo lời
> dòng dưới; không còn gì để push.

- **ĐÃ MERGE vào `main` tại `a937a98` (--no-ff, 2026-09-07), CHƯA push.** *(đúng
  lúc viết; xem banner ngay trên.)* Chủ nhân nói "Merge master local". `git diff a937a98^2 a937a98` rỗng → cây merge === tip nhánh đã
  verify `afffedc`, nên OFF 551 / ON 597 vẫn đúng, không cần build lại. `origin/main..main`
  > 0 (đo, đừng chép) — push là lệnh riêng. Nhánh `claude_desk/l7-solvers-station-1-874518`
  + worktree giữ nguyên nhưng ĐÃ MERGE HẾT — phiên sau nên nhánh MỚI từ `main`, đừng commit
  tiếp lên nhánh cũ (sẽ phân kỳ với merge commit). Wave 0 + Wave 1 (FIR+OUT) + **Wave 2
  DELAY + Wave 2 EQ CORE (A-D) ĐÃ XÂY + verify**. **VIỆC ĐẦU TIÊN phiên sau: EQ Task E/F
  (app, ON) — CHƯA XÂY**; rồi **Wave 3 (ALIGN)**. Xem mục "Wave 2" dưới cho ba điều người
  xây E phải biết + CONCERN precision cần sửa.
- Record FIR + EQ còn claim NGOÀI (scipy/rePhase/REW/CamillaDSP/RBJ coefficient) đánh
  dấu UNVERIFIED trong §ledger — plan phải đọc lại cookbook / venv main-checkout TRƯỚC
  khi build (bẫy AES-2id / parity-table). FIR đã web-verify và bác 2 premise (Toeplitz
  +Hankel không phải Levinson; WAV sample-rate KHÔNG an toàn — CamillaDSP bỏ qua field).

---

# 2026-09-06 — **L6b (multichannel) ĐÃ MERGE VÀO `main` tại `4edcf82`**

Chủ nhân nói "merge" trong phiên orchestrator L6b. Merge `--no-ff` trong checkout
`main` (sạch, đang ở `60ba99c` = merge-base, không có commit nào ngoài nhánh),
không xung đột. `git diff --stat 1eacb6a main` rỗng và `git diff --stat e048443
main -- . ':!docs'` chỉ ra **bốn file `memory/`**, nên các con số 447/508 verify
trên `e048443` vẫn đúng cho cây đã merge — không cần rebuild (memory "re-verify
what the change could have changed"). `main` **chưa push**
(`git rev-list --count origin/main..main` — đo, đừng chép). Nhánh và worktree
`continue-pending-work-f8aa84` giữ nguyên; dọn hay không là quyết định của chủ nhân.

Mục dưới đây là trạng thái lúc xây, giữ làm lịch sử cùng ngày.

---

# 2026-09-06 — L6b (multichannel) đã xây — nhánh `claude_desk/l6b-multichannel-research-4b3876`

**Đọc mục này trước tiên.** Lane L6b đi trọn năm trạm trong một phiên
orchestrator (Fable). Research `docs/research/2026-09-06-l6b-station1-research.md`,
record `docs/dsp/2026-09-06-multichannel-l6b.md`, plan
`docs/plans/2026-09-06-L6b-impl-plan.md`, report `docs/reports/006-multichannel.md`.
Đọc record TRƯỚC plan; trong record, §3 (hai lý do vắng mặt), §6 (average là
trace được publish, kèm amendment B2 về chỉ số ordinal) và §8 (không có
đường output generator) là ba chỗ phiên sau dễ suy diễn ngược.

## Baseline đo được (dán từ lệnh) — re-measure ĐỘC LẬP trên `e048443`

Verifier cuối, scratch `git worktree` riêng, `--clean-first`, generator Visual
Studio, MSVC 14.51:

```
ctest --test-dir build-off -C Release  -> 447/447, 0 failed   (RTA_BUILD_APP=OFF)
ctest --test-dir build-on  -C Release  -> 508/508, 0 failed   (RTA_BUILD_APP=ON)
warning C trong cả hai build log        -> 0
```

Đo trên `e048443` (commit cuối của lane, fix F3). Cùng verifier đo `10dd94f`
(trước F3): 446 / 505, cũng sạch.

Đầu phiên, đo trên `60ba99c`: 390 (OFF) / 436 (ON). Guard sau lane, đã làm
ĐỎ rồi XANH: `core_has_no_framework_deps` 107 (96), `coherence_gate_is_not_bypassed`
68 (62), `filter_design_has_no_polynomial_form` 125 (113),
`measure_has_no_framework_deps` 42 (30), `platform_types` 5.

## Đã hạ cánh
- `core/`: `spatialAverage` / `spatialAverageBins` (trung bình dB có trọng số
  `W = u·γ²`, tuỳ chọn power; pha = trung bình vòng có `R`; `weightedCoherence`,
  `phaseAgreement`; hai lý do vắng `NoContributor` / `NoWeight`),
  `spatialAverageMtw` (từng band rồi stitch cũ), `hasOverload` (≥ 3 mẫu liên
  tiếp tại `1 − 2⁻¹⁵`); golden `spatial.txt` từ `tools/gen_spatial.py` (argparse).
- `platform/`: `ChannelConfig` có chỉ số TF per channel (tag nhóm) và lookup
  mọi channel của một role. Audio callback KHÔNG đổi.
- `app/`: N `Analyser` sau `RoutingPlan`; `AverageGroup` nối vào publish thật
  (average + một solo, còn lại `PositionSummary`); `LevelAlign`;
  `CaptureSequencer` từ chối `Overload` / `GateNotCleared`, không phát output;
  session schema 3 (`[tf]`, `[average]`, device theo tên VÀ số channel,
  unbound khi lệch); `RoutingMatrix` gắn vào `MainComponent`; `ui/` thêm
  `GridPanel` chung, không từ vựng đo lường.
- `DualFftEngine.{h,cpp}`, `test_dualfft.cpp`, `AudioIo.cpp` không bị đụng.

## Ba con số / quyết định phiên sau KHÔNG được suy diễn lại
1. **`W = u·γ²`, không phải inverse-variance `n_d·γ²/(1−γ²)`.** Vị trí mic
   không phải replicate của một đại lượng; trọng số tối ưu thống kê cho một
   mic sạch 0.99 thắng mic 0.9 với tỉ lệ 11:1. Memory `positions-are-not-replicates`.
2. **Hai lý do vắng mặt là hai sự kiện khác nhau.** `NoWeight` với
   `contributors = 2` là hợp lệ (γ² đúng bằng 0 tại một bin). Placeholder cho
   band `nullopt` từng xoá nó — fix `1b7d9a0`.
3. **Không có đường output generator.** Callback xoá mọi output theo thiết
   kế Phase 1; solo/mute tự động là record riêng, không "viết thêm vào callback".

## Việc còn mở
- **Chưa merge.** `git rev-list --count main..HEAD` — đo, đừng chép. Merge là
  lệnh của chủ nhân.
- ~~Route thứ 9+ (vượt cap 8 `Analyser`) ... cần một trạng thái membership
  riêng; và test ba-route chưa chứng minh average loại route bị từ chối bằng
  số.~~ **ĐÓNG 2026-09-06** — xem mục "HAI NOTE F3 ĐÃ ĐÓNG" ở đầu file. NOTE 1
  hoá ra là UB (đọc `analysers[8+]` ngoài biên), không chỉ "đọc Member": cap
  membership ở `kMaxTransferFunctions` + `ExcludedOverCapacity`. NOTE 2: test số
  học `spatialAverage(2) == average, != spatialAverage(3)`.
- Ba câu hỏi chủ nhân trong `HUMAN-QA-QUEUE` (mục L6b): ngưỡng trusted-fraction,
  remote API bind/read-only, generator output là lane riêng hay gộp L7.
- Một average group live; reference thứ hai là trạng thái hiển thị, chưa phải
  group thứ hai.
- Average trace live-only (kế thừa amendment L5 của MTW).

## Người có thể tự chạy gì
```bash
ctest --test-dir build-l6b -C Release
```
```bash
ctest --test-dir build-l6b-on -C Release
```
Để NHÌN: build target `rtatool_snapshot` rồi chạy
`rtatool_snapshot.exe shots 1100 760`; `shots/transfer.png` có trace average
trắng phủ lên và readout `4 of 4   R 0.32`; `shots/main-live.png` có
`RoutingMatrix` với hàng 0 = MEAS / AVG, hàng 1 = REF / `--` ở synthetic mode.

## Bẫy phiên này gặp
- **Hai agent trong một worktree dùng chung git index** — `git add` đường
  dẫn tường minh xen kẽ có thể gói file của agent kia vào commit của mình.
  Phiên này tuần tự hoá: fix core đợi builder app xong. Verifier luôn build
  trong scratch worktree riêng.
- Baseline trong plan (390 OFF) lỗi thời ngay khi commit nợ nhỏ hạ cánh
  (393); builder đo lại và báo, đúng luật.
- MSBuild trong cây build dưới `%TEMP%` không recompile `.cpp` chỉ phụ thuộc
  header đã đổi (verifier L6b-a phải xoá `.obj`/`.exe` để mutation chạy thật).

---

# 2026-09-06 — **L3 (MTW) ĐÃ MERGE VÀO `main` tại `a1a9ebf`**

Chủ nhân nói "Merge" trong phiên orchestrator L3. Merge `--no-ff` trong checkout
`main` (sạch, đang ở `04bc1ab`), không xung đột; `git diff --stat 4fc30ea a1a9ebf`
chỉ ra **duy nhất `CLAUDE.md` +2 dòng** (commit `04bc1ab` bên `main`), nên các con
số 436/390 verify trên `55d7314` vẫn đúng cho cây đã merge — không cần rebuild
(memory "re-verify what the change could have changed"). `main` **chưa push**
(`git rev-list --count origin/main..main` — đo, đừng chép). Nhánh và worktree
`continue-6bacde` giữ nguyên; dọn hay không là quyết định của chủ nhân.

Mục dưới đây là trạng thái lúc xây, giữ làm lịch sử cùng ngày.

---

# 2026-09-06 — L3 (MTW) đã xây — nhánh `claude_desk/fable-orchestration-planning-bebcb3`

**Đọc mục này trước tiên.** Lane L3 đi trọn năm trạm trong một phiên
orchestrator (Fable). Record `docs/dsp/2026-09-05-mtw-l3.md`, plan
`docs/plans/2026-09-05-L3-mtw-impl-plan.md`, report `docs/reports/005-mtw-engine.md`.
Đọc record TRƯỚC plan; trong record đọc §5 **bản đã đảo** (frames-uniform),
không phải bản nháp "seconds" mà plan mục C3 trích lại làm bằng chứng.

## Baseline đo được (dán từ lệnh) — re-measure ĐỘC LẬP trên `55d7314`

Build dir riêng của verifier cuối, `--clean-first`, generator Visual Studio,
MSVC 14.51.36231:

```
ctest --test-dir build-final-off -C Release   -> 390/390, 0 failed   (RTA_BUILD_APP=OFF)
ctest --test-dir build-final-on  -C Release   -> 436/436, 0 failed    (RTA_BUILD_APP=ON)
warning C trong cả hai build log               -> 0
```

Trước lane: 362 (OFF) / 402 (ON), đo trên `7e10eb6` cùng phiên. Guard sau lane:
`core_has_no_framework_deps` 96 file (87), `coherence_gate_is_not_bypassed`
62 (57), `filter_design_has_no_polynomial_form` 113 (103),
`measure_has_no_framework_deps` 30 (29) — cả bốn đã được làm ĐỎ rồi XANH.

## Đã hạ cánh
- `core/`: `MtwLayout`, `MtwEngine`, `MtwResult` — **không decimate**, bảy
  `DualFftEngine` full-rate với fftSize nhân đôi mỗi octave xuống; 1281 điểm,
  0.73 Hz dưới 187.5 Hz; golden `core/tests/golden/mtw.txt` từ `tools/gen_mtw.py`.
- `app/`: `Snapshot::mtw` (vector tần số riêng), `Analyser` chạy hai engine
  song song trên cùng cặp mẫu (G2), `TransferView` vẽ từ vector tần số, seam
  hairline trên cả ba pane, toggle nguồn per-plot (mặc định MTW), strip
  `MtwReadout` in thời gian tích phân từng band.
- `DualFftEngine.cpp` / `test_dualfft.cpp` KHÔNG bị đụng (321 / 400 dòng).

## Ba con số phiên sau KHÔNG được suy diễn lại
1. **Averaging theo FRAME đồng nhất, không theo giây.** Bản nháp đầu chọn giây;
   với 0.5 s ba band đáy không bao giờ mở cổng coherence 8 (Neff trần 2.06 /
   3.55 / 6.58). Neff/giây ~ 1/T_window nên giây đồng nhất không thể cho độ tin
   cậy đồng nhất. Giây được **báo** từng band: 0.09 s trên cùng, **5.5 s dưới
   187.5 Hz** ở depth 16.
2. **`exponentialEffectiveAverages` không đạt `(2-a)/a`** — trả về
   `1 + (Neff_raw - 1)/D`, `D = 1.9246` với Hann overlap 75 %, giống nhau ở mọi
   fftSize. `fifoEffectiveAverages(hann, N/4, 16) = 8.5866`, cổng 8 mở lần đầu
   ở frame 15.
3. **Bin sở hữu là `N_0/8..N_0/4-1 = 128..255` ở MỌI band**, không phải `N_k/8`.
   Tổng 1281, không 1282 (1282 nghĩa là trùng 187.5 Hz tại seam).

## Việc còn mở
- **Chưa merge.** `git rev-list --count main..HEAD` — đo, đừng chép. Merge là
  lệnh của chủ nhân.
- ~~**Chưa có fixture snapshot mang `MtwBlock`**~~ — **ĐÓNG 2026-09-06** tại
  `a777887` (`makeSyntheticMtw`, `tools/snapshot.cpp` render `transfer.png` và
  `workspace.png` có seam + strip). Verifier độc lập build trong scratch
  worktree: 442/442 (ON), 0 warning C. Lưu ý: `specimen.png` là bảng swatch
  design-system, không mang `Snapshot`; hình MTW nằm ở `transfer.png`.
- ~~**Toggle nguồn per-plot chưa có control UI**~~ — **ĐÓNG 2026-09-06** tại
  `959a197` (`TransferSourceToggle`, nút MTW/FIXED trên cả ba pane; test âm
  "FIXED bỏ mọi seam" render pixel thật; mutation sai-pane bị bắt 15/28).
  `TransferView.cpp` 388 dòng — lần thêm sau phải tách.
- **MTW trace live-only.** Lưu trace MTW là amendment L5 (`Trace.h:91-93` lấy trục
  từ `fftSize` có chủ ý).
- **Câu hỏi sân khấu, không phải số học:** 5.5 s fill dưới 187.5 Hz có bị người
  vận hành đọc thành lỗi không — cần một người với hệ thống thật. Strip readout
  tồn tại để họ có con số trước mắt.
- Lane kế theo MASTER-EXECUTION-PLAN mục 5: **L6b**, rồi **L7**.

## Người có thể tự chạy gì
```bash
ctest --test-dir build-final-off -C Release
```
```bash
ctest --test-dir build-final-on -C Release
```
(hoặc cấu hình lại theo lệnh trong report 005). Để NHÌN: chạy `rtatool`, bật
synthetic mode (gán role Reference/Measurement sẵn, delay 4 mẫu), mở pane
Transfer — seam tại 187.5 / 375 / 750 / 1500 / 3000 / 6000 Hz, strip
`< 188 Hz 5.5 s | 188-375 2.7 s | ... | > 6000 0.09 s`, đầu thấp mịn hơn đầu cao.

## Bẫy phiên này gặp
- Verifier mutate `core/src/dsp/*.cpp` trong worktree đang có builder khác build —
  đã revert sạch, không hỏng gì, nhưng **verifier phải build trong scratch
  worktree riêng** (verifier L3b và verifier cuối đã làm đúng vậy).
- Diagnostics clang trong IDE báo hàng chục lỗi "file not found" trên file mới —
  IDE thiếu include path và C++20; MSVC build thật xanh. Bỏ qua.

---

# 2026-09-05 — **EDT ENSEMBLE ĐÃ MERGE VÀO `main` tại `a2cbd02`**

**Đọc mục này trước tiên.** Chủ nhân ra lệnh "merge hết code mới về master"
trong phiên này. Chỉ một nhánh đi trước `main`: `handoff-continuation-9045fc`
(4 commit — 1 feat + 3 docs). Merge `--no-ff`, xung đột **chỉ** ở
`docs/HUMAN-QA-QUEUE.md` (cả hai nhánh cùng viết lại mục "Merge L4b"); giải quyết
bằng cách giữ bản nhánh nguồn (đầy đủ hơn) và bỏ mục `[ ] EDT chờ chủ nhân chốt`
đã lỗi thời — nhánh đó đã tự trả lời và chuyển EDT sang "Đã trả lời". Toàn bộ
code mới nằm trong `core/`, không đụng `app/`.

## Đã hạ cánh
`rta::ir::decayTimesAcross()` (`DecayEnsemble`) — EDT/T20/T30 báo độ tin cậy
bằng **median + IQR qua nhiều capture**, không bằng một điểm số một-lần-đọc. Đây
là câu trả lời cho việc-còn-mở "EDT chưa có envelope riêng" mà L4b để lại: hai
ứng viên điểm-số một-lần-đọc đều bị đo bác (|corr| 0.02–0.16 với sai số thật);
IQR co theo `1/√N`. Cơ sở ở record §4f và header `DecayEnsemble.h`.

## Baseline đo được (dán từ lệnh) — kiểm chứng ĐỘC LẬP sau merge

Đo trên cây đã merge, build dir riêng của phiên merge chưa từng bị ai chạm,
MSVC 14.51.36231 Release, generator Visual Studio (KHÔNG Ninja — xem memory
"một build cấu hình sai đi 99% quãng đường rồi hỏng"):

```
cmake -S . -B build-verify -G "Visual Studio 18 2026" -A x64 -DRTA_BUILD_APP=OFF
cmake --build build-verify --config Release --parallel      -> link sạch
ctest  --test-dir build-verify -C Release                   -> 362/362, 0 failed   (OFF)
```

362 = **359 của baseline L4b + 3 test EDT ensemble mới** trong `test_ir_decay.cpp`.
Sáu guard kiến trúc/biểu diễn đều PASS; `core_has_no_framework_deps` quét
**87 file** (L4b: 85 → +2, đúng `DecayEnsemble.h/.cpp`) — guard **thật sự canh**
code mới, không chỉ còn xanh.

## Việc còn mở của đợt này
- ~~`RTA_BUILD_APP=ON` CHƯA đo lại sau merge này~~ — **ĐÃ ĐO 2026-09-05** trên
  HEAD `7e10eb6`, build dir riêng `build-verify-app`, generator Visual Studio,
  MSVC 14.51.36231, JUCE qua `RTA_JUCE_PATH`:

  ```
  ctest --test-dir build-verify-app -C Release   -> 402/402, 0 failed   (ON)
  ```

  402 = 362 (OFF) + 40 test chỉ build khi ON (1 guard callback-shape + 10
  platform-JUCE + 4 az_ui + 25 view), phân rã đọc từ cấu trúc gating trong
  bốn file CMakeLists, không chỉ trừ hai tổng. 0 `warning C` trên app target.
  Bốn guard PASS: core 87 file, polynomial-form 103, platform-types 5, measure 29.
- **Golden vector cho `rta::ir` decay/EDT vẫn chưa viết** (nợ mang sang từ L4b;
  ⚠️ ràng buộc "hai bất đối xứng Python/C++ trong bộ sinh golden" ở mục L4b bên
  dưới **vẫn hiệu lực**, phải đo trước khi commit golden đầu tiên).
- **`main` chưa push lên `origin`** (`git rev-list --count origin/main..main` > 0
  — đo bằng lệnh, đừng chép số). Push là lệnh riêng của con người.
- **Lane tiếp theo** theo MASTER-EXECUTION-PLAN mục 5: **L3 (MTW)** + **L6b**,
  rồi **L7**. L3 cần một lượt research trạm 1 trước (chưa có decision record).

---

# 2026-08-30 (tối) — **L4b ĐÃ MERGE VÀO `main` tại `e77e0e1`**

**Đọc mục này trước tiên. Mục "L4a" bên dưới là lịch sử của cùng ngày.**

Chủ nhân nói "merge" trong phiên REVIEW (EP06 THINKER, giữ checkout `main`),
2026-08-30 tối. Merge `--no-ff`, ort strategy, không xung đột. Branch
`claude_desk/handoff-continuation-9045fc` và worktree của nó GIỮ NGUYÊN —
dọn hay không là quyết định của chủ nhân. Đếm gì cũng bằng `git rev-list
--count`, đừng chép số vào đây.

## Baseline đo được (dán từ lệnh)

Phiên xây đo trên cây đã commit của branch (build-l4b, MSVC 14.51.36231):

```
cmake --build build-l4b --config Release --parallel   -> 0 warning /W4
ctest  --test-dir build-l4b -C Release                -> 359/359, 0 failed   (RTA_BUILD_APP=OFF)
```

Phiên review đo lại ĐỘC LẬP **sau merge**, trên `main` đã ghép, build dir
riêng chưa từng bị phiên xây chạm, `--clean-first`:

```
ctest --test-dir build-verify     -C Release  -> 359/359, 0 failed   (RTA_BUILD_APP=OFF)
ctest --test-dir build-verify-app -C Release  -> 399/399, 0 failed   (RTA_BUILD_APP=ON)
```

Baseline trước lane: 345/345 (OFF) và 385/385 (ON) — cùng chênh lệch mười bốn
case ở cả hai cấu hình. Số ON là lần đo ĐẦU TIÊN của cấu hình đó với code L4b;
nó không còn là lỗ hổng. (Bốn dòng "CMAKE_GENERATOR_PLATFORM will be ignored"
trong build ON là cảnh báo môi trường từ JUCEUtils CUSTOMBUILD, không phải
warning /W4 trên code — có từ trước lane.)

Guard sau lane: `core_has_no_framework_deps` quét **85 file** (trước: 79).

**Hai guard được chứng minh CÒN CANH, không chỉ còn xanh:**
- chèn `#include <juce_core/juce_core.h>` vào `core/src/ir/DecayLundeby.cpp` →
  `core_has_no_framework_deps` ĐỎ; bỏ ra → XANH; `git diff` rỗng.
- hạ `minBandwidthTime` 6.0 → 4.0 → test refusal ĐỎ, còn test *"same band passes
  once the room is slow enough"* vẫn XANH (nên cổng không bị hàn chết).

## Đã hạ cánh

`rta::ir::Decay` — lọc băng **zero-phase**, tích phân Schroeder, **truncation
Lundeby**, EDT/T20/T30, C50/C80/D50. Record:
`docs/dsp/2026-08-30-ir-decay-l4b.md`. Probe:
`tools/probe_l4b_filter_mode.py`, `tools/probe_l4b_truncation.py`.

**Kết quả trung tâm:** truncation **gỡ bỏ sự phụ thuộc vào chiều dài đuôi**.
Không truncation, T30 đọc dài từ +1.8% tới +3286% tuỳ đuôi dài bao nhiêu — tức
tuỳ người vận hành bấm stop lúc nào. Có Lundeby: −0.8% tới +19.5%, và **phẳng**
theo chiều dài đuôi. Đó là khác biệt giữa một phép đo và một sự trùng hợp.

## Ba con số một phiên sau KHÔNG được suy diễn lại

1. **Ngưỡng gate là `B·T < 6`, không phải 4.** Sàn 4 là của văn liệu và tính
   trên B·T **thật**. Code chỉ đọc được B·T **đo được**, mà ở B·T thấp chính bộ
   lọc thổi phồng nó (×1.42 tại true 3.71, ×1.91 tại 2.78). Hai quần thể
   **không tách sạch được**; 6.0 là ngưỡng nhỏ nhất cho 0 false-accept.
   **Phụ thuộc bậc bộ lọc** (`kSections = 4`, tức bậc 8) — đổi bậc phải đo lại.
2. **Gate đọc T30 (hoặc T20), không đọc dốc làm việc của Lundeby.** Bản đầu đọc
   dốc đó và sai 2.4× theo hướng mở cổng. Luật: **gate trên con số mày ship.**
3. **EDT chưa có envelope riêng và bảng của record chứng minh nó cần.** Dưới
   oracle, zero-phase EDT đọc +21.9% tại B·T 11.6 và +24.3% tại 5.8 — **cả hai
   NẰM TRONG vùng cổng cho qua**, vì cổng hiệu chỉnh trên T30. **Cho tới khi
   quyết, đừng trình bày EDT như ngang chất lượng với T20/T30.**

## Việc còn mở của L4b

- **Chưa có golden vector.** Record §7 đã chốt luật (commit **mẫu IR**, không
  commit tham số để C++ sinh lại; nhiễu đặc tả bằng **SNR**, không bằng biên độ
  tuyệt đối; mẫu qua float32 TRƯỚC khi Python tính kỳ vọng; fixture đặt XA mọi
  ngưỡng refusal) — nhưng chưa ai viết.
- ~~§7a "who guards what" chưa ghi vào hai file test~~ — **XONG.** Mỗi file nay
  mang con trỏ ngắn gọi tên file kia và mục §7a của record.
- ~~Một tầng của G3 chưa đo~~ — **ĐÃ ĐO VÀ ĐÃ ĐẢO QUYẾT ĐỊNH.** `filtfilt` đặt
  ~50% năng lượng band của direct sound ra **trước** t=0 ở mọi band. Bản đầu
  loại nó đi như "rò rỉ bộ lọc"; sai — đó là năng lượng của chính direct sound
  bị dời đối xứng và **được bảo toàn**. Đo cả hai convention đối chiếu C50 của
  IR **chưa lọc**: exclude sai −3.06 dB tại 1/3-oct 40 Hz, include sai −0.33 dB.
  Nay **include**, có test khoá và test đó **đỏ được**.
- **Ngưỡng 6.0: hai lưới độc lập đều cho 0 false-accept** (25 ô và 30 ô, khớp
  inflation trong 0.4%). **Nhưng giá là một DẢI, không phải một ô**: cả vùng
  true B·T ≈ 4–6 đọc ra 5.6–5.9 và bị từ chối — bốn ô dùng được. **Phòng khô đo
  ở 1/3-octave thấp sẽ gặp refusal THƯỜNG XUYÊN, không hãn hữu.** Bất cứ thứ gì
  trình bày số này cho người vận hành phải nói ra điều đó.
- ~~Một test dựa vào một seed may~~ — **XONG.** Case *"A pure exponential decay
  reads back the T60"* nay chạy năm seed và khẳng định trên **median**.
- **Cấu hình `RTA_BUILD_APP=ON` chưa đo lại.** Đây là việc còn mở DUY NHẤT không
  cần một quyết định của con người.
- **Chưa có gì để NHÌN.** L4b nằm hoàn toàn trong `core/`. Widget thuộc L4c.

## Người có thể tự chạy gì

```bash
"D:/DEV CAVE EP3/PRJ010-RTA-TOOL/.venv/Scripts/python.exe" tools/probe_l4b_truncation.py
```
In ra bảng truncation (cột Lundeby **phẳng** theo chiều dài đuôi, cột không
truncation thì không), bảng B·T, và so ba chế độ lọc. Không ghi file nào.

```bash
"D:/DEV CAVE EP3/PRJ010-RTA-TOOL/.venv/Scripts/python.exe" tools/probe_l4b_filter_mode.py
```
Bảng ba chế độ lọc, kèm docstring ghi lại **hai lần probe này tự sai** trước khi
đúng được gì — 38.7 dB năng lượng bịa ra do thiếu lead-in, và trục chiều dài đuôi
bị giấu.

---

# 2026-08-30 — lane L4a ĐÃ ĐÓNG VÀ ĐÃ MERGE. Lane tiếp theo: **L4b**

**Đọc mục này trước. Mọi mục bên dưới là lịch sử của các ngày trước đó.**

**Cập nhật 2026-08-30 (phiên EP06 DOER).** Mục này từng viết "Task 5 CHƯA THI
CÔNG" và "chưa merge". **Cả hai đã hết đúng** trước khi phiên EP06 mở ra, và
file này không được cập nhật theo — đúng loại lỗi bẫy #10 mô tả. Trạng thái
thật, đo bằng lệnh chứ không đọc từ file:

```
git log --oneline -3   -> a7453b6 / 61daf1a (merge lane L4a) / fc776f3
git rev-list --count main..HEAD  và  HEAD..main   -> 0 và 0
```

Task 5 (`rta::ir::Polarity`) và Task 6 (golden + đo hai cấu hình + đồng bộ tài
liệu) **đã hạ cánh**, lane L4a **đã merge vào `main` cục bộ** ở `61daf1a`, sau
khi chủ nhân nói "merge" trong phiên REVIEW đang giữ checkout `main`. Bảng
commit ngay dưới đây giữ lại làm lịch sử của branch đó; nó **không còn là danh
sách việc chờ merge**.

Phiên trước: 17 commit `408d0a5..acaae9a` đã merge vào `main` cục bộ (Task 1–4
+ Novak). Sau đó, trên branch `claude_desk/handoff-workflow-continuation-81650b`
(nay đã merge qua `61daf1a`):

| commit | nội dung |
|---|---|
| `46012f6` | record decision **6b** + ba probe script |
| `d9345a6` | plan Task 5 viết lại, HANDOFF, `docs/HUMAN-QA-QUEUE.md` |
| `7eb3809` | **`rta::ir::Polarity`** — implementation + test |
| `48f1c2e` | hai fix từ review: claim quá tay, test dễ sửa-cho-xanh |
| `115da7d` | `CaptureTooShort` — fiction quay lại qua nhánh không-recurse |
| `5a7bb22` | golden `rta::ir`, quy tắc biên độ **enforce bằng máy** |

Kế hoạch ở `docs/plans/2026-08-30-L4a-sweep-ir-impl-plan.md`, quyết định ở
`docs/dsp/2026-08-30-sweep-ir-l4a.md` — **đọc record TRƯỚC plan**, và trong
record đọc **decision 6b**, không phải decision 6 (đã bị chính khảo sát của nó
bác, giữ lại có khung SUPERSEDED).

## Baseline đo được (dán từ lệnh)

```
ctest --test-dir build-l4a     -C Release   -> 345/345, 0 failed   (RTA_BUILD_APP=OFF)
ctest --test-dir build-l4a-app -C Release   -> 385/385, 0 failed   (RTA_BUILD_APP=ON)
cmake --build build-l4a --config Release --parallel --clean-first -> 0 warning /W4
core_has_no_framework_deps                  -> OK, 79 file
filter_design_has_no_polynomial_form        -> OK, 93 file
platform_types_has_no_framework_deps        -> OK,  5 file
measure_has_no_framework_deps               -> OK, 29 file
```

**Hai cấu hình phủ hai tập target khác nhau — đừng cộng gộp.** 385 = 345 của
cấu hình OFF cộng 40 test app/platform mà cấu hình ON mới build được. Cả hai đo
trong phiên này, trên cùng cây, sau commit cuối.

Guard `core_has_no_framework_deps` đã được **chứng minh là còn canh**, không chỉ
còn xanh: chèn `#include <juce_core/juce_core.h>` vào `core/src/ir/Polarity.cpp`
làm nó ĐỎ, bỏ ra làm nó XANH, `git diff` rỗng sau khi khôi phục. Trap #1 nói
guard hỏng vẫn xanh; cách duy nhất biết là làm nó đỏ.

## Một dòng cho phiên sau, đừng bỏ qua

`tools/probe_polarity_margin.py` và `probe_polarity_bandwidth.py` chứa đường tụt
"0.708 → 0.300 theo bậc lọc" và bảng cliff `cheby1` order 9 / `butter` order 16.
**Những số đó đo bằng PEAK-SIGN rule, không phải rule đang ship** (first arrival
tại 0.5). Dưới rule đang ship, cùng lưới cho **0 câu sai** ở order 2–16. Đừng
khôi phục một envelope "order ≤ 8" từ chúng — docstring của cả hai file đã mang
cảnh báo này, đọc trước khi trích.

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

**Task 5 và Task 6 KHÔNG còn mở — xem khối cập nhật ở đầu mục này.** Hai tiểu
mục ngay dưới đây (`Task 5`, `Task 6`) giữ lại vì chúng ghi *cái gì đã được xây
và vì sao*, không phải vì còn việc. Bảng "plan nói / khảo sát đo được" vẫn là
cách nhanh nhất hiểu vì sao `Polarity.h` có hình dạng hiện tại.

**Việc thật sự còn mở là lane tiếp theo: L4b** (ETC, Schroeder, Lundeby,
EDT/T20/T30, C50/C80/D50 — `core/`). Nó chưa có decision record, nên bắt đầu ở
**trạm 1**, không phải trạm 3. L4a giao cho nó hai mệnh lệnh đã đo, ghi ở
`docs/dsp/2026-08-30-sweep-ir-l4a.md` mục "What this record does not decide":

1. **Đuôi nhiễu của giải chập đọc ra như hồi âm.** `apparent RT60 ≈
   3·T / log10(f2/f1)`, cơ chế đã dẫn xuất, biên độ đúng ~10% qua ba cấu hình.
   L4b dùng nó để **nhận ra** artefact, **không bao giờ để trừ đi**, và phải tự
   tìm điểm cắt chứ không tin đường dốc nhìn thấy.
2. **Các con số ρ của relative polarity đến từ MỘT lưới, chưa ai dựng lại độc
   lập.** Step 0 của L4b phải dẫn xuất lại toàn bộ trước khi bất kỳ ngưỡng ρ nào
   được ship. Đừng để số một-nguồn mặc quân phục của số ba-nguồn.

**Task 5 — Polarity (ĐÃ XÂY, `7eb3809` + `48f1c2e` + `115da7d`).** Lý do plan
gốc bị bác, giữ lại làm lịch sử: step 0 chạy 2026-08-30 theo đúng điều kiện chủ
nhân kèm khi duyệt G21, và kết quả bác plan. Đọc
`docs/dsp/2026-08-30-sweep-ir-l4a.md` **decision 6b** trước mọi thứ khác. Tóm
tắt cái gì đổi:

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

**Task 6 — closeout (ĐÃ XONG, `5a7bb22` + `f28a58b`).** Golden vector cho
`rta::ir` ở `5a7bb22`; đo hai cấu hình, chứng minh guard còn canh, và đồng bộ
tài liệu ở `f28a58b`. Cả hai đã merge qua `61daf1a`.

**Nhưng một trong hai hạng mục nó mang theo KHÔNG phải việc đã xong — nó là
ràng buộc còn hiệu lực.** Tách ra thành mục riêng ngay dưới đây, vì đọc lướt
qua header "ĐÃ XONG" sẽ nuốt mất nó.

### ⚠️ RÀNG BUỘC ĐANG SỐNG — hai bất đối xứng Python/C++ trong bộ sinh golden

**Chưa được vá. Chỉ được ghi nhận.** Áp dụng cho MỌI phiên sinh golden mới, kể
cả L4b tuần này. Kiểm lại 2026-08-30 bằng file thật, hai phiên độc lập:

```
grep -n "len(x) // 2" tools/gen_generator.py     -> 317, 318
grep -n "amplitudeFromDbFsPeak" core/src/gen/Sweep.cpp
```

| | Python (`tools/gen_generator.py`) | C++ (`core/src/gen/Sweep.cpp`) |
|---|---|---|
| kẹp bề rộng fade | `min(fade_len, len(x)//2)`, dòng 317–318 | **không có kẹp trên**. Ba tầng floor (`fadeInSec`, `2/startHz`, `fadeInOctaves·ln2·L`) và sàn 2 mẫu — không tầng nào chặn fade vượt nửa chiều dài |
| biên độ đỉnh của `raw` | `np.sin(phase)` → **1.0** | `amplitude_ · sin(...)`, `amplitude_ = 10^(levelDbFsPeak/20)`, mặc định −6 dB → **0.5012** |

**Cả hai vô hại CHỈ VÌ** field duy nhất golden hiện tiêu thụ là một **tỉ số**
(`test_generator_sweep.cpp`, case `sweep_params`: chỉ L, K và mẫu phase — toàn
phase-domain, mù với fade và biên độ). Điều kiện vô hại đang treo trên một sợi
chỉ: **thêm một field không-phải-tỉ-số vào golden là kích hoạt cả hai.**

**Cái này bắn thẳng vào L4b.** Một phần lớn tham số L4b định ship lại tình cờ
bất biến theo tỉ lệ — T20/T30/EDT là **độ dốc dB**, C50/C80/D50 là **tỉ số năng
lượng trong cùng một tín hiệu** — nên bất đối xứng *biên độ* nhiều khả năng vẫn
ngủ. Bất đối xứng *bề rộng fade* thì **không** bất biến theo tỉ lệ: nó đổi hình
dạng kích thích, do đó đổi xung giải chập, do đó đổi đoạn suy giảm sớm — đúng
chỗ EDT đọc. **Đó là giả thuyết chưa đo, không phải kết luận.** L4b phải đo nó
trước khi commit golden đầu tiên, hoặc phải chứng minh field nó ghi là bất biến
tỉ lệ. Đừng ship rồi mới hỏi.

Hạng mục còn lại của Task 6 — đo lại cấu hình `RTA_BUILD_APP=ON` — **đã xong**,
`385/385`, ghi ở `f28a58b` và ở `docs/HUMAN-QA-QUEUE.md`.

**ĐÃ PUSH lên `origin` — 2026-08-30 tối, sau merge L4b.** Chủ nhân ra lệnh
trong phiên EP06 THINKER (nguyên văn gõ: "merge master" — phiên hỏi lại và
chủ nhân xác nhận nghĩa là push, theo đúng luật câu-ngắn-hơn-câu-hỏi);
`git push origin main` → `29b464e..3c74294`. Khoảng cách đo lại bằng
`git rev-list --count origin/main..main` → 0. Trạng thái push về sau đo bằng
chính lệnh đó, đừng chép số vào đây.

> Câu này từng viết "**57 commit**" ngay sau mệnh lệnh "đo bằng
> `git rev-list --count`, không phải cộng nhẩm". Đo lại 2026-08-30 (phiên EP06,
> hai phiên độc lập cùng chạy lệnh): **71**. Một con số đứng cạnh chính lệnh
> bác bỏ nó vẫn mục sau bốn ngày — nên con số đã bị rút, không phải cập nhật.
> Cùng lớp lỗi với `fc776f3`; đây là lần thứ ba trong repo này.

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
