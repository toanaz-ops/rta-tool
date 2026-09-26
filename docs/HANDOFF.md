# HANDOFF — cho phiên tiếp theo

*2026-08-28, cuối phiên orchestrator kế nhiệm. Đọc file này + `docs/reports/002`
+ `docs/plans/MASTER-EXECUTION-PLAN.md` trước khi đọc bất kỳ dòng code nào.*

---

# 2026-09-26 (tối) — **Process audit 1–7 + quyết định 1, 2, 4 XONG.** Sáu PR merge, main = `8f8326b`.

*Owner, trong chat: "Làm từ 1 tới 7 tối ưu quy trình. Và 1-2-4 trong 4 qđ";
sau đó, khi hỏi "sao chậm thế": chọn verify-at-push, bỏ ON job cho PR chỉ
docs/tools, và rubric MEDIUM cần một instance thật trong repo (KHÔNG cap số
vòng).*

## PR đã merge

| PR | merge | what |
|---|---|---|
| #36 | `1928030` | CI job `RTA_BUILD_APP=ON` trên windows-latest (`ci-app-on.yml`), JUCE cache dạng git clone; `permissions: contents: read` |
| #37 | `4cd5866` | Bộ chọn pane RTA / TRANSFER / SPL — pane SPL giờ mở được live |
| #38 | `67eda30` | `tools/diffmut.py` (differential mutation, sidecar crash recovery) + ctest `source_files_are_under_400_lines` |
| #40 | `e06faa9` | LOW batch: `MainComponentRail` (rail cuộn, hết chồng chữ), `MainComponentTestAccess.h` (#error ngoài test), specimen SPL có số thật, CI gate `CMake (Deprecation )?Warning` |
| #39 | `b562209` | `tools/orphan_check.py` v2 — liveness do **linker MSVC** quyết (`RTA_ORPHAN_LINKMAP`), không phải grep; CI job `tools (pytest)` |
| #35 | `8f8326b` | Review loop mới trong `docs/GIT-WORKFLOW.md`, CLAUDE.md rule 5/6, builder không lồng sub-agent, speed-ups của owner |

Tallies trên main (CI của #35 tại `abbada9`): OFF 1022 (3 OS), ON 1116,
pytest tools 135, warning 0, CMake warning 0.

## Người thử được gì, bằng cách nào

1. **Cái đáng thử nhất — bộ chọn pane.** Build `rtatool` (ON), bấm SYNTHETIC,
   rồi bấm **SPL** trên hàng nút phía trên đồ thị: workspace chuyển sang
   `SplView`, thấy một dòng metric có mức dB và buffer fill. Bấm RTA/TRANSFER
   để quay lại; log SPL đang chạy không bị ảnh hưởng. Rail trái giờ cuộn khi
   cửa sổ thấp — không còn chữ chồng lên nhau ở 1280x800.
2. **Artifact ảnh GUI trên mỗi PR:** tab Actions → run "CI (app ON)" → artifact
   `rtatool-snapshot-<run_number>` (9 PNG, gồm `main-live-spl.png` có số thật).
3. **orphan_check** (chạy khi đóng wave, ~2 phút lần đầu, ~10 s sau đó):
   ```
   python tools/orphan_check.py --base <wave start> --build-dir build-orphan --cmake-generator "Visual Studio 18 2026" --cmake-arch x64 --juce-path "D:/DEV CAVE EP3/PROJECT005-AZ-handsfree/external/JUCE"
   ```
   Exit 0 = mọi hàm mới đều reachable từ `rtatool`; exit 1 in danh sách
   orphan. Hôm nay với `--base 411f1e7` (lane L6a) nó exit 1 — xem mục
   "Product gap" dưới; đó là số thật, không phải lỗi tool.
4. **diffmut:** `python tools/diffmut.py --base origin/main --build-dir <dir> --dry-run`
   liệt kê mutant trên các dòng PR thêm; bỏ `--dry-run` để chạy.

## Product gap orphan_check tìm ra (chờ owner xếp ưu tiên)

16 file `.cpp` dưới `app/src` không được compile vào `rtatool`:
- 12 chỉ vào test target: `FirTextWriter`, `FirWavWriter`, `AlignmentWizard`,
  `AlignmentWizardSignals`, `CaptureSequencer`, `DelayLocator`, `EqSession`,
  `EqVerify`, `SessionCodec`, `SessionDecode`, `SessionStore`, `TraceBlobCodec`.
- 4 chỉ vào `rtatool_snapshot`: `CrossoverTopology`, `SyntheticSnapshot`,
  `VirtualTrace`, `CrossoverSurface`.
Đây là tính năng đã build + unit-test nhưng chưa wire (có lẽ thuộc phase nạp
session). PR nào chạm tới chúng giờ sẽ bị orphan_check fail cho tới khi wire.
Dead code thật (0 caller ở đâu cả): `AnalysisThread::isSplLoggingEnabled`,
`MainComponent::currentPaneView`. Mục trong `docs/HUMAN-QA-QUEUE.md`.

## Pitfalls phiên này

| bẫy | cách đúng |
|---|---|
| grep không trả lời được "có ai gọi không" — v1 sai cả hai chiều sau 2 vòng vá | hỏi linker (`/OPT:REF /MAP`), `memory/reachability-is-the-linkers-question.md` |
| Verifier chấm MEDIUM cho edge case 0 instance → vòng không hội tụ | rubric mới: MEDIUM phải có instance thật trong repo |
| Builder đợi CI 20–29 phút rồi mới báo → mỗi vòng thêm ~25 phút | builder báo khi push; verifier chạy song song CI |
| Orchestrator viết sai quy tắc NOT IN TARGET (snapshot-only được miễn) | verifier bắt ở vòng 3 — đúng việc của verifier; đọc lại rule theo mục đích của tool |
| Builder claim billing quota 2000 phút | repo **public** — phút Actions miễn phí; chạy `gh api repos/toanaz-ops/rta-tool --jq .visibility` trước khi trích |

---

# 2026-09-26 — **L6a (SPL-pro) lane CLOSED.** Waves 0-3, 4a, Task G, W2-E BUILT và MERGED; Wave 4b (served web viewer) CẮT theo owner. Report [`docs/reports/009-spl-pro.md`](reports/009-spl-pro.md).

*Owner decisions 2026-09-25, trong chat với orchestrator: (1) orchestrator được
merge từng wave PR khi CI 3/3 xanh và không còn HIGH/MEDIUM verifier finding
nào — chính sách review ở `memory/merge-when-no-high-or-medium-remains.md`;
(2) **Wave 4b (served web viewer) CẮT** — report ghi ràng buộc rounding §12
constraint-2 của record là "recorded as untested for the viewer" (fallback đã
có sẵn ở plan dòng ~649), nên Chrome LNA test moot cho lane này.*

## PR đã merge trong phiên này

| PR | merge commit | what |
|---|---|---|
| #26 | `8c5d407` | W2-A..D: history/alarms/log/pane |
| #27 | `8f8df27` | W3-A/B: calibration flow |
| #28 | `66c5f32` | W4a report + W3-C |
| #29 | `f992ee7` | W2-E1: `SplChannelState` per-chain feeds — dose/Ln từ chain A-weighted (auto-created); alarms đọc đúng chain của metric mình; Ln từ mẫu Fast mỗi 100 ms (record §5); sample accounting chính xác trong `SplMeter` |
| #30 | `e3253d7` | Task G guards + `report_makes_no_class_1_claim` |
| #31 | `10da3dc` | W2-E2a: log pipeline SPSC→writer thread, composition-root enable/disable + epoch restart, write-failure được publish, `AllocationProbe` per-thread |
| #32 | `65d8efc` | W2-E2b: calibration áp vào live session (mở log mới), calibration record + `CalibrationInvalid` lúc đọc, refuse khi channel không khớp, export report từ một live session |
| #33 | `04b42a6` | LOW batch: 17 mục LOW hoãn, gồm chia `ApiServer.cpp`, chia CMake, chia `AnalysisThread.h` |

Mọi SHA trên đã xác nhận có trong `git log --oneline origin/main`.

## Tallies cuối cùng

Đo tại `04b42a6` (builder + verifier trên fix head của PR #33, `34cbf69`; các
commit sau đó chỉ sửa test-claim, không đổi số):

```
ctest --test-dir build -C Release      (RTA_BUILD_APP=OFF)  -> 1019/1019, 0 failed
ctest --test-dir build-on -C Release   (RTA_BUILD_APP=ON)   -> 1101/1101, 0 failed
grep -E "warning( [A-Z]+[0-9]+)?:" trên mọi build log Release -> 0
CI (ubuntu-latest / macos-latest / windows-latest)            -> 3/3 xanh
```

Baseline Wave-0/1 lúc phiên này bắt đầu, `main` tại `411f1e7`: **OFF 824**.

## Record amendment cần biết trước khi đọc code

`docs/dsp/2026-09-16-spl-pro-l6a.md` §15 **A6** (sliding-window headroom): `T`
là mẫu số của compliance window KẾ TIẾP, không phải window hiện tại; đang lấp
đầy thì `t = đã đo`, `T = nominal − excluded`; đầy rồi thì `t = s_recent`,
`T = s_recent + Δ`; `windowBlocks == 1` đọc thẳng `L_lim`; alarm chờ đủ MỘT
window có nội dung trước khi phán, trạng thái đó là `SplAlarmState::Filling`.
Plan amendment tương ứng: mục "### W2-E — the wiring nobody was assigned"
trong `docs/plans/2026-09-17-L6a-spl-pro-impl-plan.md`.

## Defect thật verifier độc lập bắt được (đưa vào report, mục "what review caught")

- Dose/alarm C-weighted publish dưới nhãn LAeq — lệch 340x dose
- Ln fed từ level max-held (khiến L90 = L1) thay vì mẫu Fast 100 ms
- `SplMeter` mất mẫu không đếm khi một hop đóng nhiều block hơn ready buffer
- Allocation trong `feedHop` của analysis thread
- Đổi sample rate giữa phiên làm hỏng log header/filters
- Route index dùng làm channel index trong calibration — verdict mất sau khi
  đổi role
- Một dangling reference chỉ đỏ trên gcc/clang
- `Clear` được publish cho một so sánh chưa từng chạy (đúng ra phải `Filling`)
- Report thiếu câu "untested for the viewer"
- Lỗi x-scale của marker/excluded-region trong report

## What the human can try, and how

**Thử cái NÀY trước** — đường export-report với một calibrator chạm mọi phần
của lane trong một lượt: build, bật synthetic mode, calibrate, export report,
đọc `report.html`.

`[not run here]` Configure + build ON (cần JUCE 9.0.1 — CLAUDE.md "Build" và
"JUCE version"):

```bash
cmake -S . -B build-on -G "Visual Studio 18 2026" -A x64 -DRTA_BUILD_APP=ON -DRTA_JUCE_PATH="D:\DEV CAVE EP3\PROJECT005-AZ-handsfree\external\JUCE"
```

```bash
cmake --build build-on --config Release --parallel
```

`[not run here]` Chạy app:

```bash
build-on/app/rtatool_artefacts/Release/rtatool.exe
```

### (a) Bật synthetic mode → logging tự khởi động, KHÔNG cần mở pane SPL

**Sẽ thấy**: bấm nút mode-switch (hint "no hardware needed") sang SYNTHETIC.
`pollSplLogging()` (`app/src/MainComponentSpl.cpp:140-174`) tự enable logging
trên cạnh off→on — không cần mở pane nào. Ba nút **CAL START / CAL END /
EXPORT REPORT** cộng readout của chúng nằm ngay trên `MainComponent`, không
nằm trong một pane, và bấm được ngay.

**Điều cần nói thẳng, không tô hồng: pane SPL sống (Leq trực tiếp, trạng thái
alarm Filling/Clear/Fired, headroom, dose, Ln) KHÔNG mở được trong
`rtatool.exe` đang chạy hôm nay.** `MainComponent` dựng đúng MỘT workspace mặc
định — một pane `"rta"` — và comment tại chính `MainComponent.cpp:51-56` nói
thẳng: *"Nothing in this class loads a session yet ... this is the only
workspace shape MainComponent ever builds today."* Pane `"spl"`
(`app/src/view/PaneRegistry.h`, `PaneView::Spl`) tồn tại và có test
(`app/src/view/SplView.cpp`, `SplStrip.h`), nhưng chỉ một session/workspace đặt
`PaneSpec::view = "spl"` mới gọi tới nó, và không phiên nào đã dựng UI nạp một
session như vậy. Bằng chứng sống của pane SPL hôm nay là **ctest cộng offscreen
snapshot** — mục kế tiếp.

### (b) Nhìn pane SPL — offscreen, theo đúng "Seeing the GUI" của CLAUDE.md

```bash
cmake --build build-on --config Release --target rtatool_snapshot --parallel
```

```bash
build-on/app/rtatool_snapshot_artefacts/Release/rtatool_snapshot.exe shots 1100 760
```

**Sẽ thấy** `shots/preview-spl.png` — SPL strip vẽ trên 10 phút dữ liệu
synthetic cố định (`app/src/dev/preview/SplPreview.cpp`), không cần thiết bị
audio, exit 0.

### (c) Thư mục log xuất hiện ở đâu, và đổi sample rate mở thư mục MỚI

**Sẽ thấy** (sau khi bật synthetic mode ở mục a):
`Documents/RTA Tool/spl/<UTC timestamp>-e<epoch>/`, chứa `ch<N>.gen*.seg*.csv`
— một file mỗi channel mang role Measurement, generation tăng khi
reconfigure, segment tăng khi rotate (`app/src/export/SplLogWriter.cpp:35`) —
cộng `session.header.txt`. Đổi sample rate hoặc device list giữa phiên (epoch
đổi) → `pollSplLogging` đóng log cũ rồi mở thư mục MỚI
(`app/src/MainComponentSpl.cpp:164-173`), không bao giờ append vào phiên cũ.

### (d) Calibrate bằng calibrator 94 dB, rồi export report

**Sẽ thấy**: bấm **CAL START** (hint "94 dB calibrator, route 0's mic
channel"), giữ calibrator 94 dB áp vào mic đo, rồi bấm **CAL END**. Readout đổi
từ "calibration: not started" sang hiện drift đo được. Bấm **EXPORT REPORT** —
**sẽ thấy** `report.html` xuất hiện đúng trong thư mục session ở mục (c), và
readout đổi thành `export: wrote <path>`.

Mục "Calibration" của report hiện: pre-check level + timestamp, post-check
level + timestamp, drift, và dòng "Compared against: **ISO 1996-2:2017 cl.
5.2**" (hằng `CalibrationSession::kClause`, `CalibrationSession.h:89`).
**Drift hỏng (> 0.5 dB)** → verdict Fail; report vẽ một vùng excluded-region tô
màu trên time-history strip (`SplReportHistorySections.cpp:154`, CSS class
`excluded-region`) và mục Validity in rõ khoảng block bị loại cộng lý do —
không bao giờ im lặng in "0.0 dB" cho một phép đo chưa từng chạy.

### (e) Suite OFF (không cần JUCE)

```bash
cmake -S . -B build -G "Visual Studio 18 2026" -A x64
```

```bash
cmake --build build --config Release --parallel
```

```bash
ctest --test-dir build -C Release --output-on-failure
```

**Sẽ thấy** `100% tests passed, 1019 tests passed, 0 tests failed out of 1019`.

## Thật thà: chưa chạy trên phần cứng thật

**Không phiên nào trong lane này đã cắm một calibrator thật hay một mic thật
vào máy.** Mọi con số ở trên đến từ synthetic mode cộng fixture đo được trên
CI. `docs/HUMAN-QA-QUEUE.md` mục "Từ lane L6a" có một item MỚI xin đúng việc
này.

## Lane kế tiếp

Không có lane L6a nào "kế tiếp" — lane đã đóng. Xem
`docs/plans/MASTER-EXECUTION-PLAN.md` cho lane mở tiếp theo (L8 nghiên cứu, L9
productization) hoặc các mục `[!]`/`[ ]` còn mở trong
`docs/HUMAN-QA-QUEUE.md`.

---

# 2026-09-18 — **Probe: bộ over-aligned `operator new/delete` ĐÃ replace — nhánh `ci/probe-aligned-new`, PR #23, owner đã nói "merge".**

Đóng gap mà PR #22 ghi lại thay vì đóng (`AllocationProbe.h` "WHAT IS
REPLACED", bullet "Gap over-aligned" ở mục PR #22 bên dưới). Trước đó
`app/tests/AllocationProbe.cpp` chỉ replace bộ unaligned; mọi allocation của
type có alignment > `__STDCPP_DEFAULT_NEW_ALIGNMENT__` (16 trên cả ba OS) đi
qua `operator new(size_t, align_val_t)` chưa replace và **counter không thấy**.
`rta::dsp::RingBuffer` (`alignas(64)`) là type như vậy: một RingBuffer
heap-allocate đo được **0 byte**.

## Đã làm (commit `f37e7f9`, merge-up `92e558e`)

- **`AllocationProbe.cpp`**: replace đủ bộ ba aligned — `operator new(size_t,
  align_val_t)`, `operator delete(void*, align_val_t)`, `operator delete(void*,
  size_t, align_val_t)`. Một block `#ifdef _MSC_VER` duy nhất:
  `_aligned_malloc`/`_aligned_free` trên MSVC (CRT không có `std::aligned_alloc`
  lẫn `posix_memalign`), `posix_memalign`/`free` nơi khác — KHÔNG dùng
  `std::aligned_alloc` vì libc của Apple enforce `size` phải là bội của
  `alignment`. Hai delete đi cùng một release path với new. Array/nothrow form
  không cần replace: default của standard forward vào ba hàm này, cùng lý lẽ
  bộ unaligned đã dùng.
- **`test_allocation_probe.cpp`**: ba case B0d — direct aligned `::operator new`
  (anchor không elide được), `std::vector<alignas(64)>` (count từ `volatile`,
  escape qua `volatile` sink), và `make_unique<RingBuffer<float>>`.
- `AllocationProbe.h`, comment CMake, memory note: gap đổi thành "đã đóng".

## Đo được (build-l4b, VS 18 2026, RTA_BUILD_APP=OFF, tại `92e558e`)

```
ctest --test-dir build-l4b -C Release                     -> 100% passed, 824/824  (main 821 + 3 B0d)
rtatool_analysis_tests.exe [allocationprobe] --order decl -> 83 assertions in 6 test cases; lex và rand seed 7 cũng xanh
M1  bộ aligned #if 0 (trạng thái trước PR)               -> 3 B0d ĐỎ: 0 == 8192, 0 >= 65536, 4135 >= 4288; B0b/B0c xanh
M2  chỉ bỏ nhánh đếm trong aligned new                    -> cùng 3 đỏ
```
Verifier độc lập (Sonnet 5, worktree riêng, `--clean-first`): CONFIRMED, có
mutation M3 riêng — xem comment trên PR #23. CI ba OS xanh tại `f37e7f9`;
`app/tests` build dưới `RTA_BUILD_APP=OFF` nên nhánh `posix_memalign` được
gcc/libstdc++ và Apple clang/libc++ biên dịch và chạy thật.

## Hai điều trả giá trên đường

- **Bound `counted >= sizeof(RingBuffer<float>)` KHÔNG phải gate.** RingBuffer
  allocate HAI lần: object qua aligned new, `storage_` (4096 byte) qua unaligned
  new mà counter vốn đã thấy. 192 byte aligned chìm dưới 4096, case vẫn xanh
  dưới M1 (đọc 4135). Bound ship là object **cộng** storage = 4288, M1 đỏ tại
  4135. Verifier tự tái hiện bằng M3. Mẫu cũ:
  `memory/a-prescribed-mutation-is-not-proof-the-check-catches-it.md`.
- **Đụng PR #25.** Nhánh này cũng sửa lỗi phụ thuộc thứ tự của B0c
  (`allocationBytes() == before`); PR #25 sửa cùng lỗi cùng ngày bằng
  `resetAllocationProbe()` đầu case + hai ctest entry one-process, và đó là bản
  ship. Merge-up `92e558e` bỏ biến thể của nhánh này. Diff so với `origin/main`
  giờ đúng năm file: bộ aligned, header, ba B0d, một comment CMake, memory note.

## Chưa làm / chờ

- `gh pr merge 23 --merge` chờ CI ba OS xanh tại commit merge-up. Sau merge:
  ghi sha merge vào mục này, `git pull --ff-only` ở checkout chính.

---

# 2026-09-18 — **L6a station 4, WAVE 1 BUILT and VERIFIED (round 1).** Branch `l6a/wave1-core-metrics`, PR #20 open, NOT merged.

Worktree `.claude\worktrees\agent-a8081552a1df7d90f`, branched from `main` at
**`b1e14a9`** (where PR #17, Wave 0, merged), then **merged up to `origin/main`
`d071269`**. Eight commits, `9a36c24..HEAD`. **Read the round-1 section first**:
the verification found three real defects, and the tallies below the first table
are the pre-merge ones.

Actions was billing-blocked while this wave was built, so every number here was
measured on this machine and pasted. **The block is now lifted and PR #20 has a
real three-OS matrix** — see the last round-1 subsection for what that does and
does not tell us.

Wave 1 is the lane's **core pure-math layer**: the Ln histogram, the windowed
energy recompute, the headroom identity and alarm latch, and dose. All of it is
closed-form or clause-derived — **no golden vector, no sound card, no socket** —
so the whole wave is provable on three operating systems.

## Round 1 of verification (2026-09-18) — three confirmed defects, eight lesser, all fixed

PR #20's verifier rebuilt both configurations clean, diffed all 222 table rows
against the primaries, re-ran every guard at both commits and ran seven
mutations including a positive control. Verdict **SOUND-WITH-FIXES**. Fixes are
`9e4b264`, then merged up to `origin/main` `d071269` at `590cdeb`.

**Tallies after the fixes and the merge**, measured here:

| config | build dir | `main` at `d071269` | at `HEAD` |
|---|---|---|---|
| OFF | `build-spl1` | 774 (derived — see below) | **818/818** |
| ON | `build-spl1-on` | 848 (derived) | **892/892** |

`0 warning C` in both build logs. `892 − 818 = 74`, the same JUCE-only
constant as before the merge, which is the cross-check that the merge added no
JUCE-side tests of its own.

**The two `main` figures are derived, not rebuilt, and that is a real gap.**
Wave 1's own contribution is 44 ctest entries, measured pre-merge as
`791 − 747` on a tree whose only difference was this branch. `818 − 44 = 774`
and `892 − 44 = 848`. I tried to rebuild `main` properly from a `git archive`
export and **MSVC refuses to configure a build tree under `%TEMP%`** — "The
CXX compiler identification is unknown", the same family of problem
`memory/mutation-testing-needs-the-exe-deleted-first.md` records about
`MSB8029`. A verifier with a second checkout re-measures it; the last one did.

**Guard counts, read from each guard's own line:**

| guard | `main` `d071269` | `HEAD` |
|---|---|---|
| `core_has_no_framework_deps` | 164 at `b1e14a9` | **181** |
| `filter_design_has_no_polynomial_form` | 186 at `b1e14a9` | **200** |
| `core_makes_no_class_1_claim` | 11 at `b1e14a9` | **28** |
| `app_measure_has_no_framework_deps` | **86** (measured on main's own tree) | **87** |
| `no_server_library_outside_api` | (L-API's, arrived in the merge) | 428 |

`app_measure_has_no_framework_deps` is the one I could measure on both sides
without building: main's GLOBS expands to 86 files, `HEAD`'s to 87, and the +1
is `SplCriteria.h`. The three `core` guards moved again because round 1's fixes
split three files (below).

### The three confirmed defects

1. **`Dose.h` claimed a negative that nothing had measured.** "No numeric
   acceptance in this lane can distinguish either pair" — FALSE. `D2a` bounds
   each regulator's exact duration formula at `1e-9 %`, and the typed
   `9.9657843` misses it by **1600x** (worst `1.600e-06 %` at 130 dBA, red at
   **50 of 51** rows; 85 dBA survives only because its exponent is zero).
   `16.6096404` fails by **2485x**. The computed constants clear the same bound
   by `1.0e4x`. Reproduced here, digit for digit with the verifier's numbers.

   Where it came from: SPL-R7 argues against `D2b`'s printed-row bounds and
   then assumes `D2a` uses a *float* tolerance. Shipped, `D2a` is an absolute
   `1e-9 %` — four orders tighter. So the rule stands on **two** legs, accuracy
   and bitwise exactness, and D1f's bitwise check is the weaker one.

   Fixed in the header, the test, the plan's SPL-R7 note (dated) and its D1f
   row. **The claim is now arithmetic in the suite**, not prose: D1f computes
   the worst deviation for computed and typed constants over D2a's own grid for
   both regulators. New memory:
   `an-unmeasured-negative-claim-is-the-one-no-suite-exercises.md` — the second
   time in three days a correction retiring an unmeasured claim shipped a new
   one, and both were negatives.

2. **The NIOSH row's label asserted FAST; the primary says SLOW.** Verified
   myself in the archived PDF: cl. 1.3.3, printed p. 4 (PDF p. 22) is
   **normative** — "If a sound level meter is used, the meter response shall be
   set at SLOW" — and ch. 4, printed p. 25 (PDF p. 52) repeats it. So
   `L_ASmax`. That is exactly the substituted-convention error `SplCriteria.h`
   exists to prevent, shipped inside the fixture written to prevent it.

   The root cause is fixed too: the detector had been one character inside a
   label string, where nothing could assert it. `SplCriterion::timeWeighting`
   is data now, with its own citation, and E1 asserts it. A new SECTION pins
   that the two peak rows carry an EMPTY detector because a peak has no
   exponential time weighting — a different fact from the OSHA row's empty
   `weighting`, which means the CFR named none. Record §7a amended (A6, dated).

   And the two allow-lists that had already drifted are now one,
   `kQuantityTokens`, read by both the header predicate and E2's scan — the
   test's copy carried `l_afmax` and omitted `l_asmax`, so this very correction
   would have made a view file printing the corrected label read as an
   offender.

3. **B1's gate was hollow.** `sumSquares()` returning
   `count_ * pow(10, (leqDb() − offset)/10)` — the inversion the header says
   callers should not have to do — left B1 **green**, because what B1 checked
   was a round trip any log-derived value satisfies. B1 now compares
   `sumSquares()` **bitwise** against the same sum accumulated independently in
   the test. Written with the mutant still in place: 4 assertions red, green
   after revert. 4 of the 6 rows discriminate — the exact-power-of-two
   amplitudes cannot — and the file says so, because trimming the list to the
   clean amplitudes would hollow it again.

### The eight lesser findings

All fixed; the interesting one is **finding 4**, the per-row resolution rule.
`table11ResolutionSeconds` keys on the **Hours** cell while the record's prose
says "the row's smallest printed unit", and they differ on rows 97 and 100.
**Resolved in favour of the code, with the justification written down and
asserted** (new `D2b3`): Table 1-1 prints in two FORMATS, and an en dash means
"zero of this unit" only in the minutes-and-seconds one — rows 97 and 100 are
exactly 1800 s and 900 s, while 80 dBA prints `25 24 –` over an exact value
carrying 54.3 seconds. The record's looser prose would give `r = 60 s` at
100 dBA, a `6.6667 %` bound, which contradicts the record's **own** printed
`0.1111 %` for that row and would make D2e's rejection of `q = 10` impossible
there. D2d keeps a separate helper, `table11PrintedUnitSeconds`, because it
asks a different question — which unit the value was rounded TO.

The others: D2d retitled to name its three-row set (one erratum, two
truncations); D3b's fifth forbidden token `EU` restored, case-sensitively on
the stripped text because lowercase "eu" is a substring of ordinary English;
`stripLineComments`' string-literal blind spot documented as fail-safe;
`test_level_histogram.cpp`'s dangling "the scan below" now HAS a scan, reading
`Levels.h` and `SplConfig.h` through `RTA_REPO_ROOT`; `exposureLevelDb` returns
`std::optional` and is absent for `seconds <= 0` instead of returning the bare
Leq; and A2's distribution-adapter portability written down (no assertion
depends on it — the bound is a theorem and both shapes that reach it are
deterministic).

### Three files split, and the complete over-budget list

Round 1's fixes grew three files past the 400-line hard cap, so three subjects
moved out. None of this is new scope:

| new file | what moved | was |
|---|---|---|
| `core/tests/test_dose_constants.cpp` | D1f, once it became a measurement over both regulators' full grids | 457 in `test_dose.cpp` |
| `core/tests/test_level_histogram_span.cpp` | A7 + A8, the DERIVED span — and where finding 8's scan lives | 418 |
| `core/tests/test_dose_table12.cpp` | D2e + D2f; Table 1-2 is a different table asking a different question | 431 |

All three are registered in `core/tests/CMakeLists.txt` and all three are in
the class-1 honesty guard by name.

**Finding 9 was right and the PR's deviation 9 was incomplete.** The complete
list of files over their plan budget, every one under CLAUDE.md's 400 hard cap:

| file | plan cap | now |
|---|---|---|
| `app/tests/test_spl_criteria.cpp` | ≤200 | **387** |
| `core/tests/test_dose.cpp` | ≤340 | **378** |
| `core/tests/test_dose_tables.cpp` | ≤340 | **351** |
| `core/tests/test_alarm.cpp` | ≤240 | **350** |
| `core/tests/test_level_histogram.cpp` | ≤300 | **331** |
| `app/src/measure/SplCriteria.h` | ≤120 | **177** |
| `core/include/rta/meter/Leq.h` | ≤130 | **134** |

Seven, not two. The longest file this wave touches is 387 lines.

### One error of my own, caught by the suite

D2b3's first draft asserted the 80 dBA exact duration against a typed
`91434.300640` and went red against the true `91434.30059336829`. It
cross-checks by MULTIPLYING (`60·480·2^(5/3)`) now rather than dividing — a
different route to the same closed form. This project's verification standard
catching the person applying it, in the commit that exists to fix two other
instances of the same mistake.

### CI came back, and found a real defect in this wave within minutes

The account's Actions billing block is lifted, so PR #20 got the first
three-OS matrix this wave has ever had — which is what rule 3 of
`docs/GIT-WORKFLOW.md` wants. It immediately paid for itself.

**B1's bitwise round trip was non-portable.** GCC and MSVC green, clang red at
`test_window_energy.cpp:113`. The test recomputed `Leq::leqDb()`'s arithmetic
in ONE expression where the implementation uses two statements, and Apple
clang defaults to `-ffp-contract=on`: `10.0 * log10(m) + offset` contracts to
a single `fma`, one rounding instead of two. Same mathematics, different
expression SHAPE, last-bit disagreement — and a bitwise check is exactly what
notices. Only the non-zero-offset rows could fail, because `db + 0.0` is exact
either way.

Fixed **structurally, not by widening**: every product and logarithm the
implementation names before adding is now named in the test too. The
verifier's un-log mutant still reddens the rewritten gate (4 assertions at
`:137`). New memory:
`a-bitwise-check-must-copy-the-expression-not-the-arithmetic.md`, which also
audits the wave's other bitwise checks and says why each is safe (B2's offset
is 0.0 and `fma(10, log10, 0.0)` rounds once to the same value; `percent() ==
100.0` has no addition to contract into; A6's merge is integer arithmetic).

Confirmed fixed by CI at `5337d46`: macOS went from 5 failures to **4**, and
the four that remain are the pre-existing set.

**Two of those four were L6a Wave 0's own with the SAME root cause. Handed
over, and now RESOLVED upstream — by the other branch of the choice.** The
diagnosis handed over was that `test_spl_seam.cpp` asserted
`levelDbFs(0.5) == 0.0` bitwise while `Levels.h:46` computes
`10.0 * std::log10(power) + kFullScaleSineOffsetDb` in one expression, which
contracts to an `fma`. `ci/macos-fixes` (PR #22, merged as `20f3c65` and in
this branch's merge) took the **second** option rather than the first: the
implementation is unchanged and the TEST's bitwise claim is retired for a
derived `1e-15` bound, on the grounds that the equality was never an identity
— it was a coincidence of the product being rounded BEFORE the add that lands
it on `-kFullScaleSineOffsetDb`. Their analysis is sharper than the hand-over:
on arm64 FMA is in the **baseline ISA**, not merely a clang default.

It also set **`-ffp-contract=off` repo-wide** (`CMakeLists.txt:91`), which
means B1's one-expression form would now pass too. **B1 stays structural
anyway**, and their own sentence is the reason: an assertion whose truth is a
compiler flag records the flag, not the arithmetic. The two lessons are
cross-referenced in `memory/`.

The other two, `B0c` (AllocationProbe, an allocation the optimiser removed)
and `D7` (the golden `/snapshot` body), were never this lane's and are fixed
there too. **macOS is green on `main` at `20f3c65`.**

The remaining single-toolchain risk is now smaller but not zero: every bitwise
assertion in the wave has GCC and MSVC evidence, and macOS evidence for all of
them except whatever the four pre-existing failures mask. The note at
`test_dose_constants.cpp` stands: a disagreeing toolchain is a named libm on a
named OS in the PR thread, not a widened tolerance.

---

## Tallies (as built, before round 1)

| config | build dir | at `b1e14a9` (baseline) | at `1357948` |
|---|---|---|---|
| OFF | `build-spl1` | **747/747** (measured here on the untouched tree) | **790/790** |
| ON | `build-spl1-on` | 821/821 (inherited — see the note) | **864/864** |

`0 warning C` in both build logs, both configurations.

```
cmake -S . -B build-spl1 -G "Visual Studio 18 2026" -A x64 -DRTA_BUILD_APP=OFF
cmake --build build-spl1 --config Release --parallel
ctest --test-dir build-spl1 -C Release                 -> 790/790, 0 failed

cmake -S . -B build-spl1-on -G "Visual Studio 18 2026" -A x64 -DRTA_BUILD_APP=ON \
      -DRTA_JUCE_PATH="D:/DEV CAVE EP3/PROJECT005-AZ-handsfree/external/JUCE"
cmake --build build-spl1-on --config Release --parallel
ctest --test-dir build-spl1-on -C Release              -> 864/864, 0 failed
```

**The ON baseline is inherited and cross-checked, not rebuilt** — flagged
honestly because it is the one number on this page that is not a fresh
measurement. 821/821 is what PR #17's verifier measured at `c7845d4`, which is
in `main`. The cross-check is arithmetic and it is exact: `864 − 790 = 74` and
`821 − 747 = 74`. Wave 1 adds nothing to `ui/tests`, `app/tests_juce` or
`platform/tests_juce`, so the JUCE-only count is a constant, and it is the same
constant on both sides. A verifier rebuilding `main` from clean should get 821.

## Guard counts — read from the guard's own line, never predicted

| guard | at `b1e14a9` | at `1357948` | why it moved |
|---|---|---|---|
| `core_has_no_framework_deps` | 164 | **178** | +14: 3 meter headers, 3 meter sources, 5 test files, 3 fixture/support headers |
| `filter_design_has_no_polynomial_form` | 186 | **197** | same files, its own extension set |
| `core_makes_no_class_1_claim` | 11 | **25** | +6 automatic (`meter/*.h`, `meter/*.cpp` globs) and **+8 by name** — SPL-R9's inward hole, below |
| `app_measure_has_no_framework_deps` | 81 | **82** | +1, `SplCriteria.h`. (81 = 76 explicit GLOBS entries + 5 `app/tests/*.h`) |

All four shown **RED by a probe and green again after revert**, on this branch:

| guard | probe | result |
|---|---|---|
| `core_makes_no_class_1_claim` | `"Class 1"` appended to `test_dose_tables.cpp` — one of the newly named files | Failed, then Passed at 25 |
| `core_has_no_framework_deps` | `#include <juce_core/juce_core.h>` appended to `core/include/rta/meter/Dose.h` | Failed, then Passed at 178 |
| `filter_design_has_no_polynomial_form` | `tf2sos` appended to `core/src/meter/Dose.cpp` | Failed, then Passed at 197 |
| `measure_has_no_framework_deps` | `#include <juce_gui_basics/...>` appended to `app/src/measure/SplCriteria.h` | Failed, then Passed at 82 |

The last one is the load-bearing one: it is what proves the new GLOBS entry is
real rather than a line in a list.

## What each commit is

| commit | task | what |
|---|---|---|
| `9a36c24` | **W1-A** | `meter::LevelHistogram` — 2000 bins of 0.1 dB + two out-of-span counters, Ln over bin CENTRES, absence with a reason instead of clamping, and a `w/2` bound that is a theorem |
| `257eb20` | **W1-B** | `Leq::sumSquares()`, and record §3's windowed recompute asserted BITWISE over 3600 steps — plus the two cases that show WHY the running subtraction was rejected |
| `ad0a361` | **W1-C** | `headroomDb` closed form, `AlarmLatch` with no hysteresis / debounce / margin, `update` taking a `WindowResult` with the bare-double overload `= delete` |
| `31274a7` | **W1-D** | `meter::Dose` — one formula, `q` computed per preset, two accumulators; the two-part table fixture over 222 transcribed primary rows |
| `1497667` | **W1-E** | `SplCriteria.h` — the three 140s as three named criteria; plus `SplConfig::dose`, which Wave 0 explicitly deferred to this task |
| `1357948` | SPL-R9 | the honesty guard now covers the files that quote the standards |

## Mutations run, every one red

| # | mutation | file | red |
|---|---|---|---|
| M1 | a percentile outside the span returns `baseDb` instead of absent | `LevelHistogram.cpp` | A5 + A7, 14 assertions |
| M2 | drop the `+ 0.5` — bin edge instead of bin centre | `LevelHistogram.h` | A3 + A4, 82 assertions |
| M3 | hard-code `baseDb = -20.0` (what an earlier plan revision shipped) | `LevelHistogram.h` | A7 + A8, 8 assertions |
| M4 | `sumSquares()` returns the MEAN square | `Leq.cpp` | B1, 26 assertions |
| M5 | an excluded block is counted but its energy still summed | `Block.cpp` | B2c — "0 dB out" where the fixture wants > 10 |
| M6 | a 0.005 dB hysteresis on the clear side | `Alarm.cpp` | C4a's **one-ULP** case only; the 0.01 dB case stayed GREEN |
| M7 | a lost window returns `kLevelFloorDb` instead of absent | `Alarm.cpp` | C3, 10 assertions |
| M8 | the NIOSH preset's `q` typed as `10.0` | `test_dose_tables.cpp` | D2b + D2e, 74 assertions — including at 100 dBA, bound 0.111 %, gap 1.179 % |
| M9 | fold below-threshold time into the dose | `Dose.cpp` | D1e + D2g, 6 assertions |

**M6's asymmetry is the point, and it is why both amplitudes ship.** A ±3 dB
fixture would have passed with a 1 dB hysteresis quietly in place. The plan
predicted exactly this split and it held.

## Provenance of the transcribed tables

`core/tests/DoseTableFixtures.h` carries 222 rows of primary-source data with
its provenance in the header comment. Summarised:

- **NIOSH Table 1-1 (50 single-level rows) and Table 1-2 (121 rows)** — DHHS
  (NIOSH) 98-126, printed pages 2 and 3 = PDF pages 20 and 21. Read 2026-09-18
  from `web.archive.org/web/2020/https://www.cdc.gov/niosh/docs/98-126/pdfs/98-126.pdf`.
  126 pages, **born digital** (`/Author NIOSH`, `/Creator Adobe InDesign CC
  2014`, `/Producer Adobe PDF Library 11.0`), so this is the document's own text
  layer and not an image extraction — which settles a doubt station 1 raised and
  then retracted. **Every `cdc.gov` path for the PDF now returns 404** and the
  DOI redirects to a landing page with no text; the archive copy is the only
  reachable primary. Ten rows cross-check against the independently verified
  spot values in `docs/research/2026-09-16-l6a-spl-pro-station1-research.md`
  §A4.2 — all ten agree.
- **OSHA Table G-16a (51 rows)** — 29 CFR 1910.95 Appendix A, read 2026-09-18
  from `law.cornell.edu/cfr/text/29/1910.95`. `osha.gov` returned HTTP 403 and
  `ecfr.gov` bot-blocked. Three rows cross-check against research §A4.1 — all
  three agree.

Durations are stored **exactly as printed**, including the CFR's inconsistent
significant figures (`32` and `16` with no decimal, `27.9` and `3.0` with one,
`0.125` with three while its neighbours `0.14` and `0.11` have two). That is
load-bearing: each row's acceptance bound is one unit in **that row's** last
printed place, so normalising the strings would change the bound.

## Findings — things that were wrong, or wrong in the plan

### 1. D2d's rounding convention had to be round-half-UP, not banker's

The plan's D2d asserts the set of Table 1-1 rows that truncate rather than round
is exactly `{124, 127}`. Recomputed with round-half-to-even — which is what
Python's `round()` gives, and what a careless C++ implementation gives — the set
is `{99, 109, 124, 127}`. **109 dBA is an exact half**: `480/2^8` min is
`1.875 min = 112.5 s` precisely, and the document prints `1 min 53 sec`. Under
half-up that is correct rounding; under banker's it looks like a fourth
truncation. The shipped fixture uses `floor(x + 0.5)` and 109 has its own
SECTION saying why, so the next reader cannot re-derive the wrong set.

### 2. `headroomDb` loses precision as the window fills, by `T/(T−t)`

`budget − spent` is a subtraction of nearly equal numbers once the window is
nearly full, so the relative error is amplified by `T/(T−t)` — a factor of 1000
at `t = 0.999·T`. C1's first bound was a flat "five roundings" `2.41e-15 dB`
and the measured worst over 252 `(T, t, L_lim)` triples is **`6.25e-13 dB`, 260
times larger**. The shipped bound is per-triple and derived from the
amplification factor itself; the worst triple uses **32.4 %** of its own bound.
Eleven orders under the 0.1 dB the display shows, so nothing is done about it —
but it is now in `Alarm.h`, because a reader who assumed the identity was exact
to the last bit near the end of a window would have been wrong.

### 3. B1's first tolerance was the wrong SHAPE, not the wrong size

An absolute `1e-9` on `sumSquares` went red at `a = 0.1` over 2 s:
`960.00002861256689` summed against a closed form of `960.00002861022972`, a
relative `2.4e-12`. The six B1 signals span energies from 960 down to `1.2e-5`,
so no absolute bound can be right for all of them. Shipped bound is the derived
`N·2^-53` relative one.

### 4. W1-E's E2 grep fires on EQ and FIR vocabulary

"Peak" is an overloaded word here. The grep the plan specifies reports **five**
offenders over `app/src/export` and `app/src/view`, and not one is a sound
pressure: `"peaking"` (twice) is an EQ filter TYPE, and `"peak_0dbfs"`,
`"peak_gain_db"` and `"coefficient_peak"` are FIR normalisation and coefficient
quantities. E2 ships as an SPL-peak check with four **named** exemptions, each
carrying its reason, and **every exemption must still be found** or the case
goes red — so a renamed literal cannot leave a hole. Measured now: 46 files, 202
string literals, 5 mentioning peak, 4 exempt, 0 unqualified.

### 5. Four "this code deliberately lacks X" greps went red against our own headers

C4b, D3a and D3b all failed on first run — because `Alarm.h` has to say
"hysteresis" to record why there is none, and `Dose.h` has to say "OSHA" to cite
App. A I(2). A check that forbids a decision from being documented trains the
next author to delete the documentation. All three now scan **code with line
comments stripped** (`core/tests/support/SourceScan.h`) **and separately REQUIRE
the word in the prose**, so neither half can be satisfied by deleting the other.
D3b's own vacuity sentinel also went red first. New memory file:
`memory/a-naming-grep-that-bans-a-word-bans-its-own-justification.md`.

### 6. SPL-R7's stated reason is refuted in the fixture, and the rule survives anyway

The literal `9.9657843` clears every dose bound in this lane by five to eight
orders, so "the literal fails a dose bound" is false and `test_dose.cpp` D1f
says so. What the computed `3/log10(2)` actually buys is that `10^(3/q)` is
**exactly 2.0 bitwise** — measured on MSVC 14.51 ucrt, for `Q ∈ {3,4,5,6}`, in
both directions — which turns D1b and D1c from tolerances into exact
comparisons. **If a CI toolchain makes any of those bitwise comparisons fail,
that is a finding to report (a named libm on a named OS), not a tolerance to
widen.**

### 7. A `cp`/`mv` mutation backup regresses the mtime, and "rebuild everything" does not fix it

Reverting two mutated `.cpp` files with `mv -f <file>.mutbak <file>` left them
**older** than their own object files, so an incremental rebuild relinked the
mutated objects: **4 test cases red on a tree where `git status --short` and
`git diff --stat HEAD` were both empty.** `memory/mutation-testing-needs-the-exe-deleted-first.md`
already names the "green for the wrong build" failure; it now also names this
door into it, and the fix (`touch` every reverted file, or `--clean-first`).

## Deviations from the plan, all named

1. **`core/tests/test_window_energy.cpp` is a new file**, not the extension of
   `test_block.cpp` and `test_leq.cpp` the plan asks for. Appending would have
   left them at **450** and **412** lines against CLAUDE.md's 400-line hard cap.
   The seam is clean: the new file is record §3, those two are record §2 and the
   2026-08-27 meter track. `blockAtLevel`/`mask` moved to
   `core/tests/BlockFixtures.h` so both build blocks from ONE closed form.
2. **`core/tests/DoseTableFixtures.h` holds the transcribed rows.** 222 data
   rows plus logic cannot fit one 400-line file. Precedent:
   `CrossoverBandFixture.h`, `DelayFilterFixtures.h`.
3. **`LevelHistogram`'s constructor has NO default base.** The plan's API sketch
   defaults it to `-20.0`, which is the exact value SPL-R8 identifies as the
   trap. Nothing constructs one yet, so the cost is zero.
4. **`LnResult { optional<double> db; LnAbsence absence; }` replaces
   `percentileDb()` + `lastAbsence()`.** A "last absence" member would be
   mutable state written from a `const` method on a class the analysis thread
   publishes — a data race for the sake of one enum.
5. **`Dose::projectedPercent()` and `Dose::twaDb()` return `std::optional`**
   where the plan sketches `double`. "Nothing elapsed, so nothing can be
   projected" and "the dose is zero, so the logarithm is −inf" are absences, and
   `0.0` for either reads as a measurement.
6. **`AlarmLatch::update` takes a `WindowResult`**, per C5, not the
   `(double windowedDb, double limitDb)` of the API sketch — and the
   bare-double overload is `= delete`, which makes C5 a compile error rather
   than a convention.
7. **W1-E touched `app/`**, which the station-4 brief's SCOPE line said not to.
   The plan's own W1-E row places `SplCriteria.h` in `app/src/measure/` and its
   test in `app/tests/`, and record §7a/§11 require it: `core` may not name a
   regulator. Both are framework-free and build with `RTA_BUILD_APP=OFF`, so
   nothing moved out of the OFF matrix. `SplConfig::dose` is the same call —
   Wave 0 left an explicit comment deferring that field to W1-D.
8. **SPL-R9's inward half is closed in this wave, not in Task G.** The
   unguarded files are the ones this wave creates.
9. **`test_alarm.cpp` is 350 lines and `test_dose.cpp` 378** against the plan's
   ≤240 and ≤340. Both under the 400 hard cap. The overrun is the
   negative-proof machinery in findings 4 and 5.

## Owner decisions this wave does NOT make

Nothing new was added to `docs/HUMAN-QA-QUEUE.md`; two existing items are now
load-bearing in shipped code and are recorded where the code is:

1. **Record §13 Q4 — NIOSH's two exchange constants.** 98-126 Table 1-1 needs
   `q = 3/log10(2)`; Table 1-2's own printed footnote is `q = 10` exactly, and
   its last row (32,500,000 % → 140.1 dBA) proves it. The gap reaches **4.2549 %
   of dose at 140 dB(A)**. `kNioshRelDose` ships the value that reproduces
   Table 1-1, because Table 1-1 is the artefact an inspector reads, and the
   preset's own comment says so. **Flipping it is one line.**
2. **Record §13 Q4, second half — tables or formulas?** The default taken is
   **formulas**: the 99 dBA erratum is excluded by name and the bound is not
   widened. Flipping it inverts D2c — the fixture would assert 1139 s and the
   formula becomes the thing under tolerance.

## What a human can run, and what they should see

Wave 1 is `core/` pure math with no UI, so there is nothing to look at — but
everything is runnable and the numbers are the deliverable.

Both configurations, whole suites:

```bash
cmake -S . -B build-spl1 -G "Visual Studio 18 2026" -A x64 -DRTA_BUILD_APP=OFF && cmake --build build-spl1 --config Release --parallel && ctest --test-dir build-spl1 -C Release
```

Expect `100% tests passed, 0 tests failed out of 790`.

Just this wave's cases, with every measured margin printed beside its bound
(the `WARN` lines are deliberate — they put the margins in the log rather than
only on a failure):

```bash
build-spl1/core/tests/Release/rta_core_tests.exe "[levelhistogram],[alarm],[dose]" 2>&1 | grep -A2 warning
```

Expect, among others:

```
A2 worst observed Ln residual: 0.050000000000011369 dB, against the w/2 bound
  0.050000000000000003 dB, at mostly flat with spikes n=1.000000
C1 worst |headroom - L_lim| = 6.2527760746888816e-13 dB over 252 (T, t, L_lim)
  triples; worst fraction of its own derived bound = 0.32420398109355347
D2b Table 1-1: 49 rows within their own printed resolution, worst using
  75.7813 % of its own bound; 99 dBA excluded as a named erratum
D2b2 Table G-16a: all 51 rows hold; tightest at 125 dBA using 50 % of its own bound
D2f Table 1-2: 119 of 121 rows within 0.049733479708180539 dB of
  10log10(D/100)+85; 50,000 % and 26,000,000 % excluded by name
```

**A2's worst residual EXCEEDS the w/2 bound by 1.1e-14** and that is the
interesting number on the page: the theorem's bound is *reached*, not merely
respected, which is why the comparison carries `1e-12` of round-off slack and
says why.

The full per-row table for both regulators, 101 lines:

```bash
build-spl1/core/tests/Release/rta_core_tests.exe "D2b*" -s 2>&1 | grep "dBA printed"
```

## Wave 2 is next, and what it needs from here

`docs/plans/2026-09-17-L6a-spl-pro-impl-plan.md` Wave 2 — `SplHistory` (the
declared-span ring, record §4), alarms wired with the proxy window (§6), the
`#key=value` log with rotation and a tolerant reader (§10), then settings, the
`Spl` pane model and the ON specimen (§11).

Wave 1 hands it: `LevelHistogram` (feed it `Detector::levelDb`, base from
`SplConfig::histogramBaseDb()`), `combineBlocks` → `WindowResult` → `AlarmLatch`
(which will not accept anything else), `headroomDb` for the number the alarm
publishes beside its state, and `SplConfig::dose[0..1]` already populated with
the two presets.

Three things Wave 2 must not undo:

- **The histogram base is derived, never typed.** A7 is the fixture; the
  hard-coded `-20.0` makes every uncalibrated session's Ln permanently
  `BelowSpan`.
- **The peak label is `kSampledPeakLabel`, not "Peak".** E2's scan counts rise
  when Wave 2 lands its view files, and that is when the check starts working.
- **`AlarmLatch` has no margin.** An operator's amber is a setting applied *to*
  `headroomDb`, not a constant added to the latch.

---

# 2026-09-18 — **L6a station 4, WAVE 0 BUILT and MERGED** as PR #17 at `b1e14a9`. Branch `l6a/wave0-spl-publish`.

*Heading corrected 2026-09-18 by the Wave 1 builder: this section was written while the PR was still open, and everything below it still reads as if it were. The account of what was built, what two verifier rounds found and every measured number is unchanged and still the record; only "NOT merged" was false, and Wave 1 branched from the merge commit.*

# 2026-09-18 — **L-API (Remote API) CLOSED OUT. PR #18 merged at `91367a8`. Lane report: [`docs/reports/008-remote-api.md`](reports/008-remote-api.md).**

**Read this section first.** The whole of lane L-API is BUILT and on
`origin/main`: stations 1+2 as PR #11 (`a39a02e`), station 3 as PR #14
(`a02fb29`), station 4 Wave 1 (tasks A–G) as **PR #16 at `7b4773f`** and
Wave 2 (tasks H–K) as **PR #18 at `91367a8`**. Eleven tasks, ten of them proven
in `RTA_BUILD_APP=OFF` — the only configuration CI runs — including the whole
request path over a real loopback socket. Report 008 carries what shipped, the
twenty-one record amendments, what each verifier refuted, and what is open.

`docs/plans/MASTER-EXECUTION-PLAN.md` now has **"Status snapshot —
2026-09-18"**: the **L-API** row reads **BUILT 2026-09-18, merged (PRs #11 #14
#16 #18)**. The next lane, by that plan's own opening order, is
**L6a (SPL-pro) Waves 1–4**.

## THE ONE THING THAT CHANGED SINCE EVERY PR BODY IN THIS LANE

**GitHub Actions is LIVE again — billing resolved — and the three-OS matrix is
RED on macOS.** Every PR body and handoff entry in this lane says Actions is
billing-blocked and the numbers are therefore local-only. That stopped being
true at **2026-09-17T17:31Z**, when a push to `main` ran the matrix and passed.
Measured with `gh run list`: every run since has executed. So **PRs #16, #17,
#18 and #19 merged with a live, visibly failing matrix rather than with none**
— a worse position than the PR bodies describe, and nobody looked.

**Hệ quả quy trình:** cổng CI của `docs/GIT-WORKFLOW.md` **luật 3 giờ kiểm
được**, nên nó không còn là "không thể đạt" mà là "đang không đạt" — hai câu
khác nhau, và câu thứ hai buộc phiên phải làm gì đó.

> **CẬP NHẬT — ĐÃ XANH.** `ci/macos-fixes` **merge thành PR #22 tại `20f3c65`**,
> và `main` giờ xanh **cả ba OS**. Nên "đang không đạt" ở trên đúng trong đúng
> hai ngày; giờ cổng luật 3 vừa kiểm được vừa **đạt**. Bốn test đỏ được sửa
> **tại NGUYÊN NHÂN**, không phải bằng cách nới assertion:
>
> - **`-ffp-contract=off` ngoài MSVC** (root `CMakeLists.txt`) lo `D7` và hai
>   case `test_spl_seam.cpp`. Clang mặc định `-ffp-contract=on`, nên
>   `a * b + c` thành **một** `fma` — một lần rounding thay vì hai — ở mọi nơi
>   ISA có sẵn lệnh đó. **Baseline x86-64 KHÔNG có FMA**, nên gcc và MSVC vốn
>   đã khớp từng bit (và đó cũng là điều loại libm ra khỏi danh sách nghi vấn);
>   **chỉ Apple arm64, nơi FMA nằm trong baseline, mới contract.** Đo được: 35
>   trong 2049 giá trị `spectrum.spectrumDb`, mỗi cái lệch đúng **±1 ULP
>   float32**, cộng ba closed-form dB identity đọc ra `2^-53 dB` thay vì 0.
> - **`B0c` là nguyên nhân KHÁC**: một allocation mà clang được phép loại bỏ.
>
> Nên `D7` **vẫn là byte lock** trên cả 198045 byte, và giờ trên **ba** OS —
> nhiều hơn điều nó từng chứng minh. Comment của flag chỉ đúng `D7` làm canary
> nếu flag bị mất. Hai memory mới:
> `memory/a-bitwise-identity-can-belong-to-the-isa-not-the-arithmetic.md` và
> `memory/an-allocation-the-optimiser-removed-reads-as-zero-bytes.md`.
>
> **Và một chỗ report 008 sai, đã ghi vào §8 của chính nó:** ba phương án nó
> liệt kê đều hỏi *test nên nhượng bộ cái gì*. Câu trả lời đúng là phương án
> thứ tư không ai liệt kê — **làm cho hai nền tảng tính ra cùng một số**. Dấu
> hiệu để nhận ra lần sau: lệch **±1 ULP tập trung ở multiply-add, trên MỘT
> kiến trúc, hai cái còn lại khớp nhau** là dấu vết FP-contraction, không phải
> vấn đề tolerance.

At `main` `d071269`:

```
rta_core (ubuntu-latest)   100% tests passed, 0 tests failed out of 774
rta_core (windows-latest)  100% tests passed out of 774
rta_core (macos-latest)     99% tests passed, 4 tests failed out of 774
```

**Bản đầu của mục này viết "giống nhau qua bốn run liên tiếp". SAI, và lịch sử
thật thì nặng hơn chứ không nhẹ hơn.** Đo từng run, job macOS:

| run | cây | macOS |
|---|---|---|
| 35260003002 | PR #16 (`remote-api/wave1-serialise`) | **1 đỏ / 700** — chỉ `D7` |
| 35303640976 | PR #17 (`l6a/wave0-spl-publish`) | 4 đỏ / 747 |
| 35305764296 | PR #18 (`remote-api/wave2-server`) | 4 đỏ / 774 |
| 35305862353 | `main` sau PR #18 | 4 đỏ / 774 |
| 35306020025 | PR #19 (`fix/cmake-comment-mojibake`) | 4 đỏ / 774 |
| 35306075307 | `main` `d071269` | 4 đỏ / 774 |

Tức bộ **bốn** test đỏ xuất hiện ở năm run, và ở mức 774 thì bốn run; run sớm
nhất chỉ có **một** đỏ trên 700 vì ba test của L6a Wave 0 chưa tồn tại. Chỗ
đáng kể: **`D7` đỏ trên macOS ở MỌI run CI kể từ run đầu tiên chứa nó.** Nó
chưa bao giờ xanh trên nền tảng đó. Một phép so byte trên float do DSP tính ra
đã phụ thuộc máy ngay từ commit sinh ra nó, và lane này merge hai lần đè lên
nó trong khi PR body của chính nó nói "không có CI để đọc".

Ubuntu and windows are the **first confirmation of OFF 774 by anything other
than this machine**. The four macOS failures:

| test | file | whose |
|---|---|---|
| `D7 REGRESSION LOCK: the golden /snapshot body has not drifted` | `app/tests/test_api_serialise.cpp:193` | **L-API** |
| `E1 the two conventions differ by kFullScaleSineOffsetDb and nothing else` | `app/tests/test_spl_seam.cpp:85` | L6a Wave 0 |
| `E3 with a calibration offset the metric reads 94 dB and the band still reads 0 dBFS` | `app/tests/test_spl_seam.cpp:164` | L6a Wave 0 |
| `B0c AllocationProbe resets on construction so one case cannot read another's bytes` | `app/tests/test_average_group.cpp:376,389` | L6a Wave 0 |

**L-API's one is diagnosed and it is a test-portability defect, not a
wire-format defect.** `D7` byte-compares a 198 KB `/snapshot` body against the
committed golden; the first divergence, at character 5084, is `-49.341915`
emitted against `-49.34192` committed — **adjacent float32 values, about one
ULP apart**. Both are correct shortest-round-trip decimals of **two different
floats**, so the DSP's own number differs in the last bit between MSVC/x64 and
Apple clang/arm64. `F1`–`F7`, the third-party-parser checks, **pass on all
three OSes**, so the document is well-formed and correctly typed everywhere.
Report 008 §8 has the three options and argues for regenerating the golden from
an exactly-representable fixture. **This closeout did not fix it** — it is a
code change and this was a docs-only pass.

## Số đo — **VERIFIER-MEASURED**, lượt dựng lại độc lập tại `d071269` đã XONG

```
INDEPENDENT REBUILD AT d071269  (= cây merge của PR #18, cộng fix comment PR #19)
  RTA_BUILD_APP=OFF                                            -> 774/774, 0 failed
  RTA_BUILD_APP=ON                                             -> 848/848, 0 failed
  forced fallback (-DRTA_FORCE_ATOMIC_SHARED_PTR_FALLBACK=ON)   -> 774/774, 0 failed
  "warning C" trong mọi build log                              -> 0
  guard xanh                                                   -> 13/13 (ON) / 11/11 (OFF)
  rtatool_snapshot                                             -> 8 PNG, exit 0
  git diff origin/main --stat -- platform/ core/src core/include ui/  -> RỖNG
```

Số file quét khi xanh: `no_server_library_outside_api` **409** (cả hai config),
`no_json_parser_in_shipped_code` **333** (2 witness),
`measure_has_no_framework_deps` **86**, `no_std_atomic_over_shared_ptr` **409**,
`core_has_no_framework_deps` **164**.

Đây **không còn là số của builder**. `d071269` là `91367a8` cộng đúng một dòng
comment của PR #19 (`git diff --stat` giữa hai cái là một dòng
`app/tests/CMakeLists.txt`). Cấu hình **forced fallback** đáng giá hơn ở lane
này so với phần lớn lane khác: đó là nhánh `AtomicSharedPtr` **có lấy lock**, và
lane này thêm một người thứ **ba** vào publish slot. 774/774 ở đó nói một
`latest()` mỗi request cũng ổn trên nhánh lock.

Verifier cũng đã dựng lại độc lập hai mốc trước và xác nhận: Wave 1 tại
`e2fc4b3` (OFF 698 / ON 766 / fallback 698) và Wave 2 tại `4a65c2d`
(**725 / 793 / 725**, ba số 0 warning). **Một mốc duy nhất chưa ai dựng lại:**
sau-fix của Wave 1 (`f95436f`: 700/768/700) — nó bị kẹp giữa hai cây đã đo nên
không có gì tựa lên nó.

Con số 774/848 vượt dự đoán 772/846 của verifier đúng **2**, và 2 đó có giải
trình: hai case limiter thêm cho residual OPTIONS landed *sau* khi lấy số
725/793 — `727 + (747−700) = 774` và `793 + 2 + (821−768) = 848`. Lượt dựng lại
độc lập sau đó đọc đúng 774 và 848 trên `d071269`.

**Một PNG không làm đúng điều tham số của nó nói.** `main-live.png` ra
**39853 byte ở 1280×800** và **bỏ qua kích thước được yêu cầu**; bảy cái còn
lại tôn trọng nó (`preview-phase.png` 45851 byte ở đúng 1100×760). Đó là
`MainComponent` tự khẳng định kích thước của nó, không phải lỗi snapshot — biết
trước để đừng đọc một sai lệch kích thước thành một render hỏng.

**Một khoảng trống CI không thể lấp, và chính lập luận `API-R15` của lane này
làm nó thành vấn đề:** hai guard RT-hazard của audio callback —
`audioio_callback_has_no_rt_hazards` và `audioio_scoped_no_denormals_is_first`
— chỉ được register ở cấu hình **ON**, mà **CI chỉ chạy OFF**. Nên phép grep
khẳng định audio callback vẫn là `ScopedNoDenormals` rồi đúng hai call **không
bao giờ chạy trên một máy CI nào**. Lane này không chạm vào hàm đó và
`git diff origin/main --stat -- platform/` **rỗng**, nên tính chất ấy hôm nay
đúng do cấu tạo. Nhưng toàn bộ sức nặng của `API-R15` là "một control được
chứng minh trên zero máy thì chưa được chứng minh", và theo đúng tiêu chuẩn đó
hai guard này đang ở vị trí server từng ở trước khi plan được sửa. **Không phải
việc của L-API để dời** — ghi ra đây để đừng phải phát hiện lại.

---

## Rule 12 vế 2 — người có thể tự chạy cái gì, và trông đợi THẤY gì

Mọi lệnh dưới đây viết cho **PowerShell 7** trong terminal của chủ nhân: **một
lệnh một block**, không `&&`, không prompt, không output dán trong fence — nếu
không thì nút Run không hiện (CLAUDE.md luật 13). Chạy từ gốc checkout.
`[verified]` = đã chạy thật trong phiên closeout này; `[not run here]` = chưa
chạy (phiên này docs-only, không có build dir).

### 0. ĐỌC TRƯỚC: mở `rtatool.exe` lên thì thấy được gì của L-API?

**Không gì cả, và đó là mặc định đang làm đúng việc của nó.**

- `api.enabled` ship **`false`** (`app/src/api/ApiSettings.h:27`). Ở bản dựng
  ship: không bind, không thread nào start, **không có gì quan sát được thay
  đổi** với một operator không hỏi tới API.
- **Không có preferences store nào trong `app/`** (record §15 `API-R5`), nên
  `ApiSettings` là một struct thuần dựng bằng tay ở composition root
  (`app/src/MainComponent.cpp:120-121`). **Các tên `api.enabled`, `api.port`…
  là TÊN TRONG TÀI LIỆU, không phải key người dùng đặt được** — hai chuỗi
  `api.*` duy nhất trong shipped code là thông điệp từ chối ở
  `app/src/api/ApiPolicy.cpp:160` và `:166`.
- Nên **bật API = sửa source rồi dựng lại**, không phải tick một ô. Xem mục 3.

Nói cách khác: bằng chứng của L-API là **ctest qua socket loopback thật**,
không phải một pane mở lên nhìn. Đừng để ai đọc thành "operator bật API trong
preferences".

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
out of 774`.

```bash
ctest --test-dir build -C Release --output-on-failure
```

`[not run here]` Configure ON (cần JUCE 9.0.1):

```bash
cmake -S . -B build-on -G "Visual Studio 18 2026" -A x64 -DRTA_BUILD_APP=ON -DRTA_JUCE_PATH="D:\DEV CAVE EP3\PROJECT005-AZ-handsfree\external\JUCE"
```

`[not run here]` Build ON:

```bash
cmake --build build-on --config Release --parallel
```

`[not run here]` Chạy test ON. **Sẽ thấy:** `100% tests passed, 0 tests failed
out of 848`.

```bash
ctest --test-dir build-on -C Release --output-on-failure
```

### 2. Lọc test của lane này — và cái bẫy phải nói trước

**`ctest -R "api"` KHÔNG chạy một test API nào.** Nó khớp đúng **một** test, và
đó là *guard* `no_server_library_outside_api` — cái tên tình cờ kết thúc bằng
`api`.

`[verified: 1]` — `ctest -R` là regex **phân biệt hoa thường** trên **tên test
của ctest**; `catch_discover_tests` gọi **không** `TEST_PREFIX`
(`app/tests/CMakeLists.txt:284`), nên tên ctest chính là chuỗi `TEST_CASE` thô;
và **không một trong 837 tên `TEST_CASE` của repo này chứa `api` chữ thường**
(0/837 tên `TEST_CASE`, 1/13 tên `add_test`). Bỏ qua hoa thường thì được sáu
tên, mà chỉ hai thuộc lane này — một trong hai là chính con regenerator bị ẩn —
còn ba nằm ở `test_readouts.cpp`, `core/tests/test_detector.cpp`,
`ui/tests/test_grid_panel.cpp`, thuộc **ba executable khác nhau**.

`[not run here]` Nên nếu muốn xem nó khớp gì, chạy đúng lệnh này và **sẽ thấy
`Total Tests: 1`** cùng tên guard:

```bash
ctest --test-dir build -C Release -R "api" -N
```

`[not run here]` Lệnh **thật sự** chạy nửa thuần của lane (tag `[api]`).
**Sẽ thấy `49 test cases` pass**:

```bash
build/app/tests/Release/rtatool_analysis_tests.exe "[api]"
```

`[verified: 49]` — 49 trong 76 `TEST_CASE` của `app/tests/test_api_*.cpp` mang
tag `[api]` (json 6, policy 10, limits 8, serialise 10, spatial 8, schema 7).

**26 case socket không mang tag NÀO CẢ** (cộng con regenerator ẩn là 27 case
không có `[api]`): `test_api_server.cpp` (14), `test_api_server_bind.cpp` (3),
`test_api_server_refusals.cpp` (9) — tức **toàn bộ các case chạy qua socket
thật, chính là thứ `API-R15` sinh ra để chạy được trên CI**. Không tag filter
nào chạm tới chúng, và **`-f <specfile>` của Catch2 cũng không dùng được**: **9
trong 26 tên có dấu phẩy**, mà dấu phẩy là ký tự phân cách test-spec của Catch2.

Nên phải gọi chúng bằng **tên**, qua một alternation neo `^` của `ctest -R`.
`[verified: 26]` — pattern dưới đây khớp **đúng 26 tên đó trong toàn bộ 837 tên
`TEST_CASE` của repo**, không thừa không thiếu (kiểm bằng cách so pattern với
mọi tên đã trích từ source), và lượt dựng lại độc lập đã chạy nó ở cấu hình
OFF: **26 ran, 26 passed, 1.24 s**.

`[not run here]` Đếm trước cho chắc — **sẽ thấy `Total Tests: 26`**:

```bash
ctest --test-dir build -C Release -N -R "^(I1 |I1b |I2 |I2b |I3 |I4 |I5 |I6 |I7 |I8 |I9 |I10 |I11 |the rate limit is a hard bound|the fixed-port bind branch|allowLanBind exists|a bind address that is not a loopback|a forged Host does not spend|a refused method and a refused token|the limiter still bounds|OPTIONS spends no quota|HEAD is NOT exempt|two Host fields are 400|OPTIONS answers on every route|the route table names eight|HEAD answers on every route)"
```

`[not run here]` Rồi chạy thật — **sẽ thấy `100% tests passed, 0 tests failed
out of 26`**:

```bash
ctest --test-dir build -C Release --output-on-failure -R "^(I1 |I1b |I2 |I2b |I3 |I4 |I5 |I6 |I7 |I8 |I9 |I10 |I11 |the rate limit is a hard bound|the fixed-port bind branch|allowLanBind exists|a bind address that is not a loopback|a forged Host does not spend|a refused method and a refused token|the limiter still bounds|OPTIONS spends no quota|HEAD is NOT exempt|two Host fields are 400|OPTIONS answers on every route|the route table names eight|HEAD answers on every route)"
```

Lưu ý `I1 ` và `I1b ` là **hai** nhánh: `I1 ` có khoảng trắng nên không khớp
`I1b…`. Bỏ một trong hai là mất một case và `-N` sẽ nói ngay.

`[not run here]` Hoặc đơn giản hơn, chạy **cả binary** — **sẽ thấy** toàn bộ
suite `app/tests` pass:

```bash
build/app/tests/Release/rtatool_analysis_tests.exe
```

**Đây là một khoảng trống nên đóng, và nó rẻ:** thêm `"[api][server]"` vào 26
`TEST_CASE` đó là xong, và cái alternation dài ở trên biến mất.
`test_names_are_ascii` canh bộ ký tự của tên test, nhưng **không guard nào canh
việc test của một lane có mang tag của lane đó**. Report 008 §7 ghi nó.

### 3. Bật API lên, và kiểm tay bằng `curl` — **CHƯA AI CHẠY**

**`[not run here — needs a running GUI]`** cho cả mục này. Đây là acceptance
"manual check" của Task J và nó **đói một GUI đang chạy với `enabled = true`**;
không phiên nào đã dựng một cái. Đọc là **chưa verify**, không phải "xong".
Cái *đã* được chứng minh là toàn bộ đường request qua socket thật ở OFF, trên
ba OS, cộng **cả hai** nhánh bind.

Bước 1 — thêm **đúng một dòng** vào `app/src/MainComponent.cpp`, ngay sau dòng
`120` (`rta::api::ApiSettings apiSettings;`) và trước dòng dựng `apiServer_`:

```
apiSettings.enabled = true;
```

Bước 2 — dựng lại ON (mục 1) rồi chạy app:

```bash
build-on/app/rtatool_artefacts/Release/rtatool.exe
```

Bước 3 — trong một terminal khác, bốn lệnh, mỗi lệnh một block.

**(a) Đường bình thường.** `Host` khớp allowlist. **Sẽ thấy** một body JSON có
`"schemaVersion":1`, một `"sequence"` tăng dần giữa hai lần gọi, và
`"available"` gồm **sáu** tên (`transfer`, `mtw`, `bands`, `spectrum`,
`average`, `positions` — sáu là độ dài danh sách NÀY, không phải số endpoint,
vốn là tám). Nếu app vừa mở và chưa publish snapshot nào thì **503**:

```bash
curl -s -H "Host: 127.0.0.1:4736" http://127.0.0.1:4736/api/v1/status
```

**(b) `Host` giả trên một path KHÔNG tồn tại → 403, không phải 404.** Đây là
control giá trị nhất của cả API và **thứ tự** chính là nội dung của nó: check
`Host` chạy **trước routing**. **Sẽ thấy đúng `403`**:

```bash
curl -s -o NUL -w "%{http_code}" -H "Host: attacker.example:4736" http://127.0.0.1:4736/api/v1/nope
```

**(c) `OPTIONS` → 204 kèm `Allow`.** Không đọc snapshot, nên nó trả lời giống
nhau cả trước lần publish đầu (chỗ mà `GET` đúng đắn trả 503). **Sẽ thấy**
`HTTP/1.1 204 No Content` và `Allow: GET, HEAD, OPTIONS`, body rỗng:

```bash
curl -s -i -X OPTIONS -H "Host: 127.0.0.1:4736" http://127.0.0.1:4736/api/v1/status
```

**(d) `If-None-Match` với ETag vừa nhận → 304, không body.** ETag là
`Snapshot::sequence` **kèm dấu ngoặc kép**. Lấy ETag từ (a) bằng `-i` trước, rồi
thay `"12345"` bên dưới bằng đúng giá trị đó. **Sẽ thấy `304`** nếu chưa có
publish mới, `200` nếu đã có:

```bash
curl -s -o NUL -w "%{http_code}" -H "Host: 127.0.0.1:4736" -H "If-None-Match: \"12345\"" http://127.0.0.1:4736/api/v1/status
```

Bước 4 — **bỏ lại dòng đã thêm ở bước 1.** Một `enabled = true` lọt vào commit
là một listener mạng không ai xin.

### 4. Ba guard, và tại sao chúng là thứ giữ ranh giới

`[not run here]` Cả ba, một lệnh:

```bash
ctest --test-dir build -C Release -R "no_server_library_outside_api|no_json_parser_in_shipped_code|measure_has_no_framework_deps" --output-on-failure
```

**Sẽ thấy** `100% tests passed, 0 tests failed out of 3`. Số file quét khi xanh:
**409**, **333** (2 witness), **86**.

Khác biệt giữa hai guard mới là bài học, không phải chi tiết.
`no_server_library_outside_api` **có** `ALLOW`
(`app/src/api/ApiServer.cpp`) nên có **hai sentinel**: file được ALLOW phải nằm
**trong** tập quét (nếu không, một `DIRS` sai chính tả in OK khi chỉ canh bốn
thư mục trong năm), **và** file đó phải **vẫn còn chứa** thứ đang bị guard (nếu
không, ngoại lệ sống lâu hơn lý do của nó). `no_json_parser_in_shipped_code`
**không có `ALLOW` nào** — shipped code không bao giờ được include một parser —
nên sentinel thứ nhất không có bản tương ứng, và một **witness** thay chỗ: phải
có ít nhất một file dưới `app/tests` include parser, không thì script
`FATAL_ERROR`. Nó quét `app/src`, **không phải `app`**, nên `app/tests` ở ngoài
tầm **do cấu tạo** — đó chính là lý do witness tồn tại.

`[not run here]` Đếm cả bộ guard, để thấy 11 (OFF) hay 13 (ON):

```bash
ctest --test-dir build -C Release -N -R "has_no|no_server|no_json|no_std_atomic|makes_no|test_names_are_ascii|is_not_bypassed|is_first"
```

### 4b. Snapshot offscreen — cách DUY NHẤT để nhìn GUI

L-API **không thêm pixel nào**, nhưng `main-live.png` giờ dựng và huỷ một
`MainComponent` **có sở hữu một `ApiServer` đang tắt**, nên nó là bằng chứng
duy nhất rằng Task J không làm hỏng khởi tạo/huỷ của app.

`[not run here]` Dựng target:

```bash
cmake --build build-on --config Release --target rtatool_snapshot --parallel
```

`[not run here]` Render. **Sẽ thấy** `wrote … 8 files` và `exit=0`:

```bash
build-on/app/rtatool_snapshot_artefacts/Release/rtatool_snapshot.exe shots 1100 760
```

**Bẫy về kích thước, đã đo:** `main-live.png` ra **39853 byte ở 1280×800** và
**bỏ qua 1100 760**; bảy PNG còn lại tôn trọng tham số (`preview-phase.png`
45851 byte ở 1100×760). `MainComponent` tự khẳng định kích thước của nó. Đừng
đọc sai lệch đó thành render hỏng. `shots/` bị gitignore.

Gọi exe qua `cmd //c` nếu chạy từ Git Bash; gọi trực tiếp trả 127 (CLAUDE.md
"Seeing the GUI").

### 5. Golden `/snapshot` — và cái cổng không filter nào cấp được

`[not run here]` Regenerate **cần biến môi trường**, không chỉ tag:

```bash
$env:RTA_API_GOLDEN_WRITE = "1"
```

```bash
build/app/tests/Release/rtatool_analysis_tests.exe "regenerate the API golden"
```

```bash
Remove-Item Env:\RTA_API_GOLDEN_WRITE
```

**Sẽ thấy** golden 198045 byte và `git diff` **RỖNG** nếu format không đổi.
Không có biến đó thì case `SKIP` kèm thông điệp, và `D9` assert đúng điều ấy —
chạy **dưới chính filter `[api]` từng làm hỏng chuyện**. Lý do cổng là biến môi
trường chứ không phải tag: thứ bị tấn công CHÍNH LÀ bộ khớp tag (report 008
§4.5).

### 6. Provenance của hai thư viện vendored

`[not run here]` Băm lại file đã commit, đừng tin con số trong doc:

```bash
Get-FileHash -Algorithm SHA256 external/cpp-httplib/httplib.h
```

**Sẽ thấy** `1F99E51881C4C9D0649B27C611442C2F4D9BCFEC5A22A14D5FCD1F8106F730B4`
(22875 dòng, tag `v0.56.0`, MIT).

```bash
Get-FileHash -Algorithm SHA256 external/nlohmann/json.hpp
```

**Sẽ thấy** `AAF127C04CB31C406E5B04A63F1AE89369FCCDE6D8FA7CDDA1ED4F32DFC5DE63`
(25526 dòng, tag `v3.12.0`, MIT, **test-only**).

**Bẫy đã trả học phí:** một bản `httplib.h` sẵn trên máy khai đúng
`CPPHTTPLIB_VERSION "0.56.0"` nhưng là **22885 dòng / `a6e65d30…`**. Nó
**không** được dùng. *Một version string không phải một danh tính.*

### 7. CI — giờ đọc được, và phải đọc

`[verified]` Trạng thái matrix ba OS:

```bash
gh run list --limit 6
```

`[verified]` Chi tiết một run, kể cả job nào đỏ:

```bash
gh run view 35306075307
```

`[verified]` Test nào đỏ trên macOS:

```bash
gh run view 35306075307 --log-failed
```

---

## Rule 12 vế 3 — HANDOFF cho lane kế: **L6a (SPL-pro), Waves 1–4**

Lane kế **không do closeout này chọn** — nó là thứ "Suggested opening order"
của `docs/plans/MASTER-EXECUTION-PLAN.md` đã ghi: L6a. Stations 1+2+3 của nó
XONG, **station 4 Wave 0 BUILT và ĐÃ MERGE (PR #17 tại `b1e14a9`)**, và
**Waves 1–4 là việc kế tiếp**. **L8** (research) read-only, bắn lúc nào cũng
được; **L9** cuối; **L5b** và **L4d** vẫn chặn vì hai khoản mua.

**Đọc trước, theo thứ tự:**

1. `docs/GIT-WORKFLOW.md` — luật hiện hành. **Luật 3 (CI là cổng merge) giờ
   kiểm được và đang KHÔNG đạt** — xem mục đầu file này.
2. `docs/plans/MASTER-EXECUTION-PLAN.md` — "Status snapshot — 2026-09-18",
   hàng **L6a**, và cột PARALLEL-SAFE.
3. Mục **L6a Wave 0** ngay dưới section này: bảng per-commit, hai vòng verifier,
   mọi residual đo được, và mục "Next phase: Wave 1" của chính nó (thứ tự build
   **W1-A ∥ W1-C ∥ W1-D → W1-B → W1-E**, tất cả core, tất cả OFF, **không
   golden vector nào được thêm vào lane này**).
4. `docs/reports/008-remote-api.md` — **Wave 4b là một CLIENT của bề mặt
   L-API**. §2 là hợp đồng transport đã đóng băng; §7 nói cái gì còn thiếu ở đó.
5. `docs/HUMAN-QA-QUEUE.md` — mục `[!]` đầu tiên (Actions: **đã mở lại, và
   matrix đỏ**) và mục `test_weighting.cpp` chờ duyệt: **cả hai nằm đúng trên
   đường của L6a**.

**Ba việc L-API bàn giao trực tiếp cho L6a:**

1. **Ba trong bốn test đỏ trên macOS là của L6a Wave 0**, không phải của
   L-API: `test_spl_seam.cpp` E1 và E3, và `test_average_group.cpp` B0c. Hai
   cái ở `test_spl_seam.cpp` trông giống **cùng một hạng lỗi** với `D7` của
   L-API — một tolerance hoặc một float identity đúng trên MSVC/x64 và không
   đúng trên Apple clang/arm64. Đọc
   `memory/two-builds-disagreeing-is-not-evidence-one-is-wrong.md` **trước khi**
   giả định bên nào sai. **Cả bốn đang được sửa chung trên `ci/macos-fixes`**,
   nên việc của Wave 1 là *review* nhánh đó chứ không phải mở lại từ đầu — và
   review nó theo luật 1: một job macOS xanh là **cần**, không **đủ**. Nới một
   tolerance cho tới khi hết đỏ là cách một lock thôi khoá
   (`memory/a-threshold-read-off-a-grid-is-that-grids-floor.md`).
2. **Gate 1 của G7 (viewer) ĐÃ ĐẠT; gate 2 thì CHƯA CHẠY.** Transport tồn
   tại, đã đóng băng, một port một `Host` check một rate limit. Gate 2 là
   record `2026-09-16-remote-api.md` §12 constraint 4: **một trang được serve
   TỪ `127.0.0.1` fetch `127.0.0.1` có được miễn prompt Local Network Access
   của Chrome hay không**. Nó suy ra được từ mô hình same-address-space của
   LNA nhưng **không tìm thấy phát biểu verbatim** (station-1 UNVERIFIED mục
   7). Một buổi chiều với Chrome 142+. **Đó là test của L6a, không phải của
   L-API**, và Wave 4b là chỗ nó cắn.
3. **`"spl"` trong `available`: cổng đã mở, danh sách chưa theo.**
   `app/src/api/ApiSerialise.cpp:74-75` vẫn ghi `"spl" joins it the day the Meters
   track puts SPL in the Snapshot and not a day earlier`, và `:77` vẫn phát
   literal sáu tên. Ngày đó là **PR #17**, merge **trước** PR #18 của chính
   lane L-API. Trên dây không có gì sai — không endpoint nào serialise SPL, nên
   thêm `"spl"` là quảng cáo một representation không ai trả — nhưng **cái
   trigger được viết trong comment thì đã nổ**. Việc còn lại: một comment, một
   string literal, và **một quyết định** — `"spl"` nghĩa là một field trên
   `/snapshot`, hay một endpoint riêng? Nếu là endpoint riêng thì nó là task
   thứ chín và `available` lên bảy.

**Còn chờ người, không chờ agent:**

- **§14 q.1: port cố định hay ephemeral.** Số **4736** đã chốt (4737 là IANA
  `ipdr-sp`). Hình dạng chưa. **Giá đổi giờ đã biết và nhỏ**: code ship cố
  định, **cả hai nhánh bind đều có test**, nên đổi là một hằng trong
  `ApiSettings.h` cộng một chỗ hẹn. Một câu là chốt được.
- **Xin Smaart API SDK hay không.** Miễn phí, không NDA theo terms công bố, và
  là đường **duy nhất** tới mảnh prior art trạm 1 không đọc được: đối thủ
  encode **coherence** trên dây ra sao. REW không dạy được gì — REW là
  swept-sine một kênh, API của nó không có coherence ở đâu cả. Terms cấm phát
  tán lại, nên **không bao giờ được trích nội dung nó vào repo này**. Mất vài
  ngày.
- **GitHub Actions: giờ chạy, và đỏ.** Có hold merge theo luật 3 hay không là
  lời của chủ nhân — luật nói có, bốn merge gần nhất nói không — và giờ đó là
  một lựa chọn thật chứ không phải một thứ bị chặn.

**Món nợ doc mà L7 để lại và vẫn chưa trả:** ALIGN-R1's `1e-12` chưa được
amend trong §5 của `docs/dsp/2026-09-06-l7-alignment-wizard.md` và trong Wave 0
plan. Chi tiết ở `docs/reports/007-solvers.md` §5 mục 6.

---

---

> *Ghi chú thêm 2026-09-18 lúc closeout: mục dưới đây viết khi PR #18 còn mở.
> **Nó đã merge tại `91367a8`.** Và câu "GitHub Actions vẫn bị chặn billing"
> trong đó là **SAI** kể từ 2026-09-17T17:31Z — xem mục closeout ở trên. Ngoài
> ra, khiếm khuyết mojibake mà mục này ghi "cần một commit riêng" **đã được
> trả**: PR #19 tại `d071269`. Phần còn lại giữ nguyên làm hồ sơ của wave.*

> *Ghi chú thêm 2026-09-18, lúc merge `origin/main` vào `docs/l-api-closeout`:
> mục dưới đây viết khi PR #22 còn mở. **Nó đã MERGE tại `20f3c65`, và
> `main` giờ XANH cả ba OS.** Chỉ dòng này là mới; phần còn lại giữ nguyên làm
> hồ sơ của vòng sửa.*

# 2026-09-18 (sau merge PR #22) — **Vòng verify: test probe PHỤ THUỘC THỨ TỰ — nhánh `ci/probe-order-independence`, PR mở, CHƯA merge.**

PR #22 merge tại `20f3c65`. Verifier tìm hai defect **sau** khi CI 3/3 xanh, và
cả hai đều là loại "xanh nhưng không chứng minh gì".

**Defect 1 — `app/tests/test_allocation_probe.cpp` phụ thuộc thứ tự chạy.**

    $ rtatool_analysis_tests "[allocationprobe]" --order decl
    test_allocation_probe.cpp(139): FAILED:
      CHECK( rta::test::allocationBytes() == 0 )
    with expansion:
      8192 (0x2000) == 0

Byte counter là **program-global** và `~AllocationProbe` **cố ý** không xoá nó
(`AllocationProbe.h` khai báo số đọc còn giá trị sau khi guard ra khỏi scope —
đó là thứ cho `measureGroupPublishBytes` trả về một phép đo lấy bên trong).
Nên trong một lần chạy **MỘT PROCESS**, tổng của case trước vẫn nằm đó, và
assertion này chạy TRƯỚC khi có probe nào được dựng — nó đang đọc 8192 byte của
anchor case. **Có từ trước** `d071269` (dư `17735 == 0` khi chưa có anchor),
nhưng PR #22 viết tiền đề SAI "Catch2 runs every file in this binary in one
process" vào file mới **hai lần**.

Tiền đề đó sai, và chính chỗ sai là vấn đề: `catch_discover_tests` đăng ký
**một ctest test cho mỗi Catch2 case** và gọi lại binary một lần cho từng case
với tên case làm filter — nên dưới `ctest` mỗi case có **process riêng** và
counter global không thể truyền qua case. **ctest đang CHE sự phụ thuộc thứ tự,
không phải chứng minh là không có.**

Sửa: zero counter ở đầu mỗi case (destructor không đụng tới), sửa hai comment
cho đúng, và đăng ký hai ctest entry chạy tag theo kiểu một process:
`allocation_probe_one_process_order_decl` và `..._lex`. **CẢ HAI thứ tự**, vì
chúng bọc lộ dư theo hai chiều ngược nhau — đo được: bỏ reset → `decl` ĐỎ
(`8192 == 0`), `lex` **XANH**. Một mình `lex` sẽ không bắt được.
`--order rand` **cố ý không** đăng ký: Catch2 gieo lại seed mỗi lần chạy.

**Defect 2 — comment CMake quy công sai cho MSVC.** Nó nói baseline SSE2 là lý
do MSVC khớp gcc. Sai lý do cho một kết luận đúng: `/fp:precise` **implies
`fp_contract(off)` từ Visual Studio 2022 trở đi, ở BẤT KỲ `/arch:`** — tài liệu
Microsoft nói thế, nên đó là **cam kết**, không phải tai nạn. MSVC trước VS2022
ĐƯỢC PHÉP contract. GCC trên x86-64 mới là nửa mà ISA là toàn bộ lý do. Ghi lại
trong `memory/a-bitwise-identity-can-belong-to-the-isa-not-the-arithmetic.md`:
**khi hai cấu hình khớp nhau, "vì sao" là câu hỏi cho TỪNG cấu hình.**

## Số đo, tại `HEAD_SHA`

| | |
|---|---|
| OFF `-DRTA_BUILD_APP=OFF` | **777/777**, 0 `warning C` |
| ON (`-DRTA_JUCE_PATH=...PROJECT005.../external/JUCE`) | **851/851**, 0 `warning C` |
| CI ba OS | CI_LINE |
| một process, `[allocationprobe]`, `--order decl` / `lex` / `rand --rng-seed 1,7,104324450` | **77 assertion / 3 case, xanh cả năm lần** |
| toàn bộ binary, một process, `--order decl` | **39178 assertion / 378 case, xanh** |

`777`/`851` = `775`/`849` của PR #22 + hai ctest entry mới. Không xoá gì.

## Tech-debt đã file (`docs/HUMAN-QA-QUEUE.md`, mục "Tech-debt từ CI macOS fix")

- **`-ffp-contract=off` gần như không có gì gác.** Chỉ D7 phát hiện nếu nó bị
  xoá, và D7 chỉ đỏ ở **macos-latest trên CI**. Người phát triển trên Windows
  xoá flag và không thấy gì. Hai lựa chọn để đóng đều có giá, ghi trong queue,
  **cần một câu của chủ nhân**.
- **JUCE chưa bao giờ biên dịch dưới `-ffp-contract=off`.** CI chỉ chạy
  `RTA_BUILD_APP=OFF`; cấu hình ON duy nhất được đo là MSVC, nơi `if(NOT MSVC)`
  khiến flag không tồn tại. clang/gcc + JUCE + ON là tổ hợp **zero lần chạy**.

Còn nguyên từ PR #22: gap over-aligned `operator new` của probe (nhánh
`ci/probe-aligned-new` đã mở ở worktree khác).

Dọn: worktree scratch của verifier `rta-vfy-pr22` dưới `%TEMP%\claude\` đã xoá
(`git worktree remove --force`, không còn trong `git worktree list`).

---

# 2026-09-18 — **CI: bốn test đỏ RIÊNG trên macos-latest đã xong — nhánh `ci/macos-fixes`, PR #22 mở, CHƯA merge.**

GitHub Actions chạy lại sau khi hết billing block. Lần chạy ba-OS đầu tiên
([35306075307](https://github.com/toanaz-ops/rta-tool/actions/runs/35306075307),
`main` tại `d071269`): ubuntu và windows **774/774**, macos-latest **4 đỏ**.
Không commit nào gây ra chúng — đó là hai platform property chưa từng bị chạm.

| test đỏ | root cause, một câu |
|---|---|
| `B0c AllocationProbe resets on construction…` (`0 >= 8192`) | libc++ vào heap qua `__builtin_operator_new`, mà clang được phép **elide** cặp new/delete có pointer không escape — ở `-O3` cái `std::vector<double>(1024)` local bị xoá hẳn, probe đếm đúng zero byte của một allocation chưa từng xảy ra |
| `D7 REGRESSION LOCK: the golden /snapshot body has not drifted` | Apple clang trên arm64 **contract** `a*b + c` thành một `fma` (FMA nằm trong baseline ISA), làm lệch bit cuối của `std::norm` trên `complex<float>`, các butterfly FFT và `acc += alpha * (psd - acc)` trong `SpectrumEngine` → 35 trong 2049 giá trị `spectrum.spectrumDb` lệch đúng ±1 float32 ULP |
| `E1 the two conventions differ by kFullScaleSineOffsetDb…` | cùng contraction đó: `levelDbFs(0.5) == 0.0` bitwise là **trùng hợp của HAI lần rounding**, không phải identity — chính việc round `10*log10(0.5)` về double TRƯỚC phép cộng mới đưa nó về đúng `-kFullScaleSineOffsetDb`; một `fma` giữ product ở full width nên tổng đọc `2^-53` = 1.11e-16 dB |
| `E3 with a calibration offset…` | cùng assertion, cùng giá trị, ở case kia |

**Cách khoanh vùng mà không có máy Mac:** ubuntu-latest và windows-latest khớp
**cả 198045 byte** của golden. Hai compiler khác nhau, hai libm khác nhau, bit
giống hệt. Baseline ISA của x86-64 không có FMA nên không bên nào contract —
điều đó loại libm khỏi danh sách nghi vấn và chỉ còn đúng một thứ macOS không
chia sẻ. **Hai trên ba khớp nhau là bằng chứng về thứ chúng chia sẻ**, và nó đã
nằm sẵn trong log.

## Đã làm, ba sửa cho hai defect

1. **`-ffp-contract=off`** (non-MSVC, root `CMakeLists.txt`, comment dẫn số run).
   KHÔNG phải regime numeric mới: MSVC dưới `/fp:precise` trên baseline SSE2 chưa
   bao giờ contract, nên mọi golden vector và mọi bitwise identity trong repo này
   vốn đã được viết và verify dưới no-contraction. Flag chỉ nói ra điều đó thay
   vì dựa vào việc một instruction không tồn tại. Giá phải trả là throughput
   trong inner loop, không phải latency — audio callback chỉ copy vào ring
   buffer. D7 chính là canary nếu flag này bị mất.
2. **`app/tests/test_allocation_probe.cpp`** (mới): B0b + B0c tách khỏi
   `test_average_group.cpp` (đang 397/400 dòng). B0c lấy element count từ một
   `volatile` và cho một element escape qua `volatile` sink, nên subject của
   phép đo sống sót qua optimiser. **Thêm một anchor case** gọi trực tiếp
   `::operator new(n)` — không phải new-expression, không phải builtin, không
   optimiser nào được xoá — vì suite cũ không phân biệt được "probe bị mù" với
   "allocation bị xoá": mọi case khác assert count bằng zero hoặc một bound, và
   cả hai loại đều pass trong cả hai trường hợp.
3. **`app/tests/test_spl_seam.cpp`**: bitwise claim chuyển sang `p = 1.0`, nơi
   `log10` đúng bằng `+0.0` nên không có lần rounding nào, và `10*0.0 + k` lẫn
   `fma(10.0, 0.0, k)` đều đúng bằng `k` trên mọi platform. Ở `p = 0.5` dùng
   bound **được dẫn ra** trong comment (1 ulp libm error của `log10(0.5)` nhân
   10, cộng tối đa `2^-52` cho việc round product, phép cộng cuối exact theo
   Sterbenz: 7.8e-16 dB) và INFO in residual. Sửa cả assertion LẪN flag là cố ý:
   một assertion mà tính đúng của nó là một compiler flag thì nó ghi lại flag,
   không ghi lại arithmetic (PR #5 bỏ `std::isinf` ở Nyquist vì đúng lý do này).

Không test nào bị skip, tag out hay quarantine.

## Số đo, tại ``9a48e08``

| | |
|---|---|
| OFF `cmake -S . -B build-mac -G "Visual Studio 18 2026" -A x64 -DRTA_BUILD_APP=OFF` | **775/775**, 0 `warning C` |
| ON `-DRTA_BUILD_APP=ON -DRTA_JUCE_PATH=...PROJECT005.../external/JUCE` | **849/849**, 0 `warning C` |
| CI, ba OS ở head này | **775/775 cả ba** — run [35311058336](https://github.com/toanaz-ops/rta-tool/actions/runs/35311058336): ubuntu 3m23s, macos 2m4s, windows 5m49s. Build warning ubuntu 16 / macos 3 / windows 0, **giống hệt** baseline run 35306075307 → nhánh này không thêm warning nào |

Baseline tại `d071269` là 774 OFF / 848 ON. `+1` là anchor case mới; không xoá gì.

## Mutation, exe xoá trước và TU force mỗi lần (build-mac, MSVC 14.51, Release)

| mutation | kết quả |
|---|---|
| bỏ `resetAllocationProbe()` khỏi constructor | **ĐỎ** — `second < first` → `8359 < 8231` tại `:205`. **ĐÃ SỬA 2026-09-18 (xem mục `ci/probe-order-independence`):** dòng này từng ghi thêm "anchor `counted == bytes` → `16551 == 8192`" — **sai quy kết**. Đo lại trên cây cuối: anchor **XANH**, vì nó tự reset counter ở đầu case. `16551` là số của một lần chạy trên cây TRUNG GIAN, khi anchor chưa tự reset và chạy SAU reset case, nên `counted` của nó cọng thêm 8359 dư của case trước |
| `setAllocationCounting` store `false` vô điều kiện | **ĐỎ** — 4 assertion, có `0 == 8192` và `0 >= 8192`, tái hiện đúng triệu chứng macOS |
| `kFullScaleSineOffsetDb` → `3.0102999566398000` (header, rebuild dependents) | **ĐỎ** tại `test_spl_seam.cpp:117`, `:143`, `:149`, `:216`, residual −1.19904e-14 dB |
| restore + rebuild sạch | **XANH** 775/775, working tree khớp commit |

Mutation thứ ba để **XANH** `levelDbFs(1.0) == kFullScaleSineOffsetDb`, vì hai
vế dịch cùng nhau — comment giờ nói đúng điều đó thay vì nhận là nó bắt được
việc sửa constant. Giá trị của constant được pin bằng literal ở `:149`.

## Còn chờ người quyết

- **`-ffp-contract=off` là policy lâu dài của project, hay là biện pháp giữ tới
  khi D7 lock đổi hình?** PR lấy default giữ property mạnh, nói rõ giá, và để
  đường quay lại đúng một dòng.
- ~~**Gap over-aligned của probe: ghi lại, chưa đóng.**~~ **ĐÃ ĐÓNG** bằng
  PR #23 (`ci/probe-aligned-new`, mục đầu file). Nguyên văn lúc viết:
  `operator new(size_t, align_val_t)` vẫn chưa được replace — định nghĩa portable
  cần `_aligned_malloc` trên MSVC và `std::aligned_alloc` ở nơi khác.
  `rta::dsp::RingBuffer` là type như vậy (`alignas(64)`). Không measured window
  nào chạm tới, vì mọi caller arm probe SAU construction.
- ~~**PR #22 chưa merge.**~~ **ĐÃ MERGE** 2026-09-18 tại `20f3c65` (owner chủ
  động, sau vòng verify). Hai điểm verifier tìm ra sau đó đi ở nhánh
  `ci/probe-order-independence` (PR #25) — xem mục ngay trên mục này.

Memory mới: `memory/a-bitwise-identity-can-belong-to-the-isa-not-the-arithmetic.md`,
`memory/an-allocation-the-optimiser-removed-reads-as-zero-bytes.md`.

---

# 2026-09-18 — **L-API station 4, WAVE 2 (tasks H–K) XONG — nhánh `remote-api/wave2-server`, PR mở, CHƯA merge. Lane L-API: BUILT.**

Bốn task, năm commit (Task I tách thêm một commit test đóng một lỗ plan không nêu).
TDD từng cái, red dán trước green. **Chỉ Task J là ON**; cả server và toàn bộ
đường request chạy trong `RTA_BUILD_APP=OFF` — cấu hình duy nhất CI dùng.

| | |
|---|---|
| baseline OFF tại `7b4773f` (main) | **700/700**, 0 `warning C`, guard quét **75** file |
| OFF sau vòng sửa trạm 5 | **725/725** (+25), 0 `warning C`, guard quét **80** file |
| baseline ON tại `7b4773f` | **768/768**, 0 `warning C` |
| ON sau vòng sửa trạm 5 | **793/793** (+25: 24 case OFF-target + 1 guard), 0 `warning C` |
| forced-fallback OFF | **725/725**, 0 `warning C` |
| `no_server_library_outside_api` | xanh **393 file** ở CẢ HAI cấu hình, đỏ **6 lần** |
| `git diff main --stat -- platform/ core/src core/include ui/` | **rỗng** |
| `rtatool_snapshot` | 8 PNG, exit 0 — `main-live.png` dựng và huỷ `MainComponent` (giờ sở hữu một `ApiServer` tắt) sạch |

**SAU KHI MERGE `origin/main` (`b1e14a9`, PR #17 = L6a Wave 0)** -- do lai tren
cay da merge, khong phai cong so:

| | |
|---|---|
| OFF | **774/774**, 0 `warning C`, guard quet **86** file |
| ON | **848/848**, 0 `warning C` |
| `no_server_library_outside_api` | xanh **409 file**, ca hai cau hinh |
| `git diff origin/main --stat -- platform/ core/src core/include ui/` | **rong** (lane nay van khong cham tang nao duoi `app/` ngoai `core/tests/`) |

Ba conflict, ca ba la **hop** (union) va duoc KHANG DINH bang dem, khong bang doc:

- `app/tests/CMakeLists.txt` GLOBS: base **73**, nhanh nay **77** (+4), main
  **78** (+5) -> hop phai la **82**, do duoc **82**. Ca hai nhom nguon (L-API
  va L6a Wave 0) con nguyen, moi nhom van co comment phan cach cua no.
- `memory/MEMORY.md`: base **36** dong, moi ben **37** -> hop **38**, do duoc
  **38**; khong mat dong nao, va ban dai hon cua bai hoc "verifier voi Bash"
  (main them mot cau) la ban duoc giu.
- `docs/HANDOFF.md`: **ca hai** muc dau file song -- L-API Wave 2 roi L6a
  Wave 0 -- cong ghi chu merge cua main ve PR #16, dat tren header Wave 1 da
  de-stale cua nhanh nay.

**MOT KHIEM KHUYET CUA MAIN, KHONG SUA O DAY:** `app/tests/CMakeLists.txt` cua
`origin/main` co **mojibake** -- mot dau section-sign bi encode UTF-8 **hai
lan**, tuc bon byte \xc3 \x82 \xc2 \xa7 o cho dang le la mot. Dung cai CLAUDE.md
luat 6 sinh ra de chong. Giu **nguyen byte** khi giai conflict: day la dong cua
lane khac, sua lang le trong mot conflict resolution la cach lam mat dau vet.
**Can mot commit rieng** -- ghi cho dieu phoi vien.

**MOT HOP TAC DA PHIEN DANG BIET:** `C:\Users\id_az\AppData\Local\Temp\claude\` la thu muc **DUNG
CHUNG** giua cac phien, khong phai scratchpad rieng. Phien nay da doc mot
`m-on.log` **cu tu 16/9** cua phien khac va tuong la log cua minh. Da xac minh
lai bang cach grep duong dan worktree trong chinh log -- **luon lam the, hoac
dat log trong scratchpad theo phien.**

Commit: `8318f87` H (vendor cpp-httplib) · `5afb42e` I (`ApiServer`) ·
`02b5cd0` J (composition root) · `2705b9e` K (guard server-library) ·
`ef8d93a` I-bổ-sung (nhánh bind cổng CỐ ĐỊNH + `allowLanBind` từ chối).

## Vòng verify trạm 5 trên PR #18 — BA DEFECT, đã sửa, record thêm `R18`/`R19`/`R20`

Cả ba **đo được qua socket**, không ai đọc code mà thấy. Commit sửa:
`d3be858`.

1. **`R19` — rate limiter chạy TRƯỚC các refusal, tức một `Host` giả tiêu
   quota của client thật.** Đo: limit 3, ba request `Host` giả rồi một request
   thật → **429**. Một kẻ không đọc được một byte nào của API này, từ ngoài
   allowlist, không token, vẫn khoá được API của chủ nhà giữa show. Thứ tự
   mới: **400 (Host trùng) → 403 (Host) → 405 → 401 → 413 → 429 → route**.
   Lý lẽ "limiter trước công việc" của §4 KHÔNG đòi nó đứng đầu: bound là
   bound trên số `latest()` **LOAD**, và mọi refusal ở trên không chạm
   publish slot. **Đánh đổi phải nói ra:** limiter không còn bound lưu lượng
   *vào*, chỉ bound lưu lượng *được phục vụ*.
2. **`R18` — `Allow` quảng cáo OPTIONS mà không ai phục vụ nó.**
   `methodIsAllowed` cho phép OPTIONS, 405 ghi nó vào `Allow`, nhưng
   `installRoutes` chỉ đăng ký `Get` → OPTIONS qua allowlist, không khớp route,
   trả **404 không có `Allow`**. Giờ **phục vụ**: `204` + `Allow`, **không đọc
   snapshot** (OPTIONS mô tả RESOURCE, phải trả lời giống nhau trước publish
   đầu tiên — chỗ mà GET trả 503), `204` chứ không `200` vì không có
   representation nào để trả. HEAD không cần đăng ký (httplib đẩy GET và HEAD
   vào cùng `get_handlers_`) và giờ có test trên cả tám route.
   `Allow` giờ là **một** hằng — hai chỗ viết danh sách method là hai danh
   sách có thể lệch nhau, và defect này chính là hình dạng của nó.
3. **`R20` — hai field `Host` → 400.** `get_header_value("Host")` chỉ đọc field
   ĐẦU, nên `Host` đúng rồi `Host` giả **qua được allowlist** trong khi proxy /
   cache / log phía sau có thể đọc cái kia. Chối theo **SỐ LƯỢNG** (RFC 9112
   §3.2), không theo "khác nhau thì chối": luật là một field line.
4. **Trích dẫn dòng sai.** `httplib.h:14436` là "Send 101 Switching Protocols";
   comment "fall through to 404" là **`:14487`**. Đo lại luôn cả các trích dẫn
   khác: loop handler `:14413` → **`:14414`**; `:2189`, `:5481`, `:13881`,
   `:14407`, `:14408`, `:14437` đúng.

**Hai quan sát đã gấp vào, không phải defect:**

- **"API thread không bao giờ block analysis thread" là cách nói SAI.** Nói
  đúng: nó **không giữ lock xuyên qua serialisation** — phần đắt tiền chạy sau
  khi đã có bản copy. Nhưng `AtomicSharedPtr` **không lock-free trên MSVC**
  (đo trong class comment của nó), nên bản thân atomic load VẪN có thể tranh
  chấp với publish. Bound là **số học** (≤ `maxRequestsPerSecond` load/giây),
  không phải cấu trúc — và đó là lý do limiter là control an toàn thực, và vì
  sao nó đứng ngay TRƯỚC load chứ không ở đâu sau đó. Đã sửa trong
  `ApiServer.h`.
- **"Bẫy khoảng trắng cuối" trong `ApiPolicy.cpp` chỉ đúng Ở MỨC HÀM.** httplib
  đã trim OWS trước khi `hostIsAllowed` thấy giá trị, nên không client HTTP nào
  gửi được nó tại đây. Vẫn nên chặt — `hostIsAllowed` là hàm thuần, caller sau
  có thể không phải header parser — nhưng đừng gọi nó là phòng ngự mức dây.

**Một deviation nữa cần ghi:** acceptance của plan đòi
`grep -n "httplib" app/src/api/ApiServer.h` **rỗng**. Không thể đúng: class
comment của file đó **của ý** gọi tên cpp-httplib nhiều lần để giải thích pimpl.
Thay bằng grep **neo vào include directive**, đúng thứ guard thật sự quét:
`grep -nE '^[ 	]*#[ 	]*include.*httplib' app/src/api/ApiServer.h` — rỗng.

---

**Sáu điều một phiên sau phải biết:**

1. **Header vendored đúng bản, và bẫy 22885-dòng là thật.** Lấy từ tag
   `v0.56.0`, đo trên chính bytes đã commit: sha256
   `1f99e51881c4c9d0649b27c611442c2f4d9bcfec5a22a14d5fcd1f8106f730b4`, 22875
   dòng. `git show HEAD:external/cpp-httplib/httplib.h | sha256sum` ra cùng
   hash — `.gitattributes` `eol=lf` không đổi gì vì file đã LF sẵn. Bản copy có
   sẵn trên máy khai `CPPHTTPLIB_VERSION "0.56.0"` nhưng là 22885 dòng /
   `a6e65d30…`: **một version string không phải một danh tính.**

2. **`Host` check so với cổng ĐÃ BIND, không phải `settings.port` — plan viết
   sai chỗ này.** Với bind ephemeral, `settings.port == 0`; header `Host` mang
   cổng client thật sự nối tới, nên so với 0 sẽ từ chối **mọi** request. So
   với `boundPort`, bằng `settings.port` bất cứ khi nào nó khác 0 — cấu hình
   ship (cổng cố định 4736) không đổi gì.

3. **Mọi case qua dây đều bind ephemeral — tức nhánh `bind_to_port` mà bản
   SHIP dùng thì không ai test.** Đã bịt bằng `test_api_server_bind.cpp`: bind
   ephemeral để hỏi một cổng đang rỗi, huỷ, rồi bind **cố định** chính số đó.
   Nhánh production đã từng là nhánh duy nhất không được chứng minh.

4. **I11 đo được, không phải khẳng định — và hai sự thật máy móc về httplib
   đi kèm.** `GET` mang `Upgrade: websocket` + `Connection: Upgrade` +
   `Sec-WebSocket-Key` trả **200 với body JSON bình thường** trên path có
   thật, **404** trên path không có; không 405, không 101, không
   `Sec-WebSocket-Accept`. Đúng như R16a dự đoán. Hai ghi chú: comment trong
   `httplib.h:14487` nói "fall through to 404" là **sai** — nó rớt xuống routing,
   nên path có thật ra 200; và `pre_routing_handler_` chạy **HAI lần** cho
   một request upgrade (`:14408` rồi `Server::routing` `:13881`), tức một
   request như vậy tiêu **hai** suất rate-limiter. Cả hai đã ghi trong
   `external/cpp-httplib/PROVENANCE.md`.

5. **413 được chặn Ở HAI tầng và phải thế.** Tầng một đọc `Content-Length`
   trong pre-routing và từ chối **trước khi đọc body** — đó mới là ý của §9
   control 3. Tầng hai là `set_payload_max_length`, cho body **chunked** không
   khai độ dài, chỉ biết được trong lúc đọc. Bỏ tầng một là mở đường cho
   một GET khai 2 GB.

6. **Test client là raw socket, và đó không phải sở thích.** `httplib::Client`
   sẽ là **file thứ hai** include `<httplib.h>`, mà guard chỉ cho đúng một.
   `RawHttpClient.h` gửi string cố định, đọc đến khi peer đóng — không parse,
   không keep-alive, không timeout. Nó **có** ghi nhận `reset`: ở case 413
   server đóng khi body chưa đọc hết nên TCP trả RST; bytes đã nhận vẫn
   được giữ và là thứ assertion đọc.

**Quyết định đang chờ người (không chặn build):**

- **§14 q.1, cố định hay ephemeral.** Số **4736** đã chốt (4737 là IANA
  `ipdr-sp`). Hình dạng thì chưa: cổng cố định dễ tìm nhưng có thể đụng; cổng
  ephemeral ghi ra file không bao giờ đụng nhưng cần một rendezvous. Code ship
  hôm nay **cố định**, và cả hai nhánh bind giờ đều có test.
- **§14 q.3, `/traces` + `/session`.** Vẫn là "chưa". Nếu đổi thành "có" thì là
  thêm một task và một đường publish thứ hai.
- **`api.enabled` vẫn `false` khi ship.** Kiểm tra tay (plan Task J) đói một
  GUI đang chạy, **phiên này không chạy** — đọc là "chưa verify", không phải
  "xong". Đường request thì đã chứng minh qua socket thật trong OFF.

---

---

# 2026-09-18 — **L6a station 4, WAVE 0 BUILT.** Branch `l6a/wave0-spl-publish`, PR open, NOT merged.

Worktree `.claude\worktrees\agent-ac416b1321ee2ec32`, branched from `main` at
`00276cb` (where PR #15, the station-3 plan, merged). Eight commits, `24f6f14..HEAD` (see the table below). GitHub Actions is still billing-blocked at the account
level, so **every number below was measured on this machine and pasted**; a
verifier is expected to re-measure from a clean rebuild.

## Round 3 (2026-09-18, after PR #17's SECOND verifier) -- READ THIS FIRST

Round 2's tallies were confirmed (745/818/745), the merge union verified exact,
items 1 and 3 confirmed. **Two real gaps and three minors**, all fixed. Round
2's section below is kept because its defect accounts are still the record of
what was wrong.

| config | build dir | at `c7845d4` |
|---|---|---|
| OFF | `build-spl` | **747/747** |
| ON | `build-spl-on` | **821/821** |
| forced fallback | `build-spl-fb` | **747/747** |

`0 warning C` in all three.

### Gap 1 -- the ROUTED publish branch had no test at all

`buildPublishedSnapshot` has **two** branches that attach the SPL block, and
all three of round 2's SECTIONs used an **empty** `RoutingPlan`. Deleting the
routed branch's `snapshot->spl = std::move(splView)` therefore left the whole
suite green -- while a **routed session, which is what a dual-FFT measurement
rig actually runs during a show**, would have published no SPL at all.
Silently, because every consumer is already required to tolerate the block
being absent.

Fixed: a case with two routes on one reference and real paired frames, so the
routed branch genuinely runs `publishAverageGroup` and `mergeRoutePositions`.
`positions.size() == plan.routes.size()` is asserted **first**, as proof of
which branch ran, before the block's values. Plus a SECTION for a routed
session with nothing logging. Mutation: delete that line ->
`test_spl_publish.cpp:658  REQUIRE( snapshot->spl.has_value() )` red.

### Gap 2 -- my own mutation (e) claim was half wrong

The verifier was right. There are **two** distinct mutations here and I had
conflated them:

| mutation | site | reds |
|---|---|---|
| **(e1)** the ternary returns a placeholder on the nothing-is-logging path | `buildPublishedSnapshot` | `:545` **only** |
| **(e2)** the early return returns a placeholder | `buildSplBlockView` | `:560` (and C1's `:78`, `:86`) |

`:560` is the "config but no completed block" branch, which returns `nullopt`
from **`buildSplBlockView`'s own** early return -- so a mutation confined to
`buildPublishedSnapshot` cannot reach it, exactly as the verifier said. Both
mutations are now run and pasted separately, and the PR body is corrected.

### Gap 3 -- the array bound is a gate now, and I corrected my own overclaim

`AnalysisThread::kMaxSplMetricWindows = SplConfig::kMaxMetrics` was a
**convention**: a literal 16 there with `kMaxMetrics` raised to 24 compiles and
every test stays green, while the eight metrics past the array's end lose their
windows and publish as ABSENT.

Two guards, both measured red under that mutation:

- **compile**, `AnalysisThread.cpp`'s `static_assert`, in the TU that declares
  the array: `error C2338: static assertion failed: 'the per-metric window
  array must be sized by SplConfig::kMaxMetrics ...'`
- **run time**, `test_spl_drain.cpp` **D5** (ON), which sizes its buffer from
  `kMaxSplMetricWindows` itself. With **both** `static_assert`s also deleted:
  `REQUIRE( filled == session.config()->metrics.size() )` with
  `kMaxSplMetricWindows = 16, SplConfig::kMaxMetrics = 24, metrics = 24,
  filled = 16`.

**And a self-correction worth reading, because it is the same mistake the
verifier had just caught me making.** I first wrote that the new OFF-build case
("every metric the config can express gets a PRESENT reading") catches this
drift. It does not and cannot: `AnalysisThread.h` includes JUCE, so that file
cannot name the constant, and it sizes its buffer from `kMaxMetrics` instead.
**Measured** under the same mutation it reads "metrics = 24, windows filled =
24" and stays **green**. Both that case's comment and the `static_assert`'s
comment now say so, with the measurement, because assuming there is a third
guard would be the next person's mistake. What the OFF case *does* cover is the
all-present property, which is what makes `buildSplBlockView`'s no-fallback
rule safe to ship at all -- absence is the right answer for an unfilled row and
the wrong answer for a configured metric.

The general lesson, and it cost two rounds: **a guard credited with catching
something it never touches is worse than no guard**, because it stops anyone
looking for the real one. Both times the giveaway was the same -- the claim was
made from where the constant is *declared* rather than from where the buffer is
*sized*.

### Minors

`core/include/rta/meter/Block.h` said "two static_asserts" where there are
three (`sizeof`, `alignof`, `offsetof`). Corrected. The PR body's mutation line
numbers are corrected in the round-3 reply: (a) `:454,463,464` 7 assertions,
(c) `:498,499`.

### Still open for the owner, unchanged

- **Q11** -- which flags exclude a block. Default shipped, five fixtures.
- **`UnderRange`** -- reserved, no criterion anywhere, cl. 5.12 paywalled.
- **Truncate vs refuse** on an over-long metric list -- truncation shipped, the
  flip is one line.

---

## Round 2 (2026-09-18, after PR #17's verifier) -- READ THIS FIRST

The verifier reproduced 689/762/689, found mutations (a)-(d) red, B0b
reachable and item-1's numbers exact, and judged the exact-form tolerance
replacements stronger than the plan's. It also found **three defects**, all
now fixed on this branch, and the branch is **merged up to `origin/main`
`7b4773f`** (PR #16, L-API Wave 1).

**The tallies below this section are the PRE-MERGE ones and are superseded.**
After the merge, measured on the merged tree:

| config | build dir | at `c53ca0e` | note |
|---|---|---|---|
| OFF | `build-spl` | **745/745** | includes L-API Wave 1's own 46 |
| ON | `build-spl-on` | **818/818** | |
| forced fallback | `build-spl-fb` | **745/745** | `-DRTA_FORCE_ATOMIC_SHARED_PTR_FALLBACK=ON` |

`0 warning C` in both. Guards: `core_has_no_framework_deps` 164,
`core_makes_no_class_1_claim` 11, `no_std_atomic_over_shared_ptr` 401,
`no_json_parser` 329 (1 test witness), `test_names_are_ascii` 133,
`platform_types` 8, `measure_has_no_framework_deps` **81** -- main's 76 plus
this lane's 5, which is the arithmetic that proves the GLOBS union kept both
sides rather than silently shrinking guard coverage.

### Defect 1 -- the metric cap, and the 18.8 dB lie above it

`SplConfig::metrics` was an unbounded vector validated nowhere, while the
publish path's per-metric window storage is a fixed array of 16. At 17
metrics `fillMetricWindows` returned 0 (all-or-nothing), which
`buildSplBlockView` read as "no per-metric windows supplied" and therefore as
permission to use one shared window for every metric. **Reproduced exactly**
before fixing, on my own fixture, matching the verifier's figures: the
C-weighted metric published **`-28.1735 dB`** -- the A chain's number --
where its own is **`-9.33053 dB`**. **18.843 dB wrong, under a C label.** That
is `e35f121`'s defect re-opened one index above the array bound, and
`AnalysisThread.h` even documented the truncation it did not implement.

Closed in **three layers, each sufficient alone**, because the failure was
silent and 18.8 dB wide:

- **(a)** `SplConfig::kMaxMetrics` -- ONE constant, declared with the data it
  bounds. `AnalysisThread::kMaxSplMetricWindows` is now `= SplConfig::
  kMaxMetrics` rather than a second 16; **two independent numbers is how the
  hole opened**. `SplSession::start` TRUNCATES the list to it.
- **(b)** `fillMetricWindows` fills `min(out.size(), metrics.size())`, so an
  undersized buffer gives a SHORT answer instead of a wrong one.
- **(c)** `buildSplBlockView` no longer falls back per metric. Empty
  `metricWindows` still means "single weighting, read the shared window";
  non-empty-and-short now yields an EMPTY span for the uncovered rows, which
  `combineBlocks` turns into an absent Leq. The reading **floors instead of
  lying**.

**Truncation, not refusal, and it is named.** Refusing the session would
silence SPL logging outright on a misconfiguration and lose a show's
evidence, which is worse than logging the first sixteen -- and that is only
acceptable **because the count is reported**: `SplSession::refusedMetrics()`,
carried through `SplPublishInput::refusedMetrics` to
`SplBlockView::refusedMetrics`, so an operator who configured eighteen
readouts and got sixteen can see the two. A caller that would rather refuse
reads `SplConfig::refusedMetricCount()` first. **If the owner prefers
refusal, that is a one-line flip and this is where it is recorded.**

Mutations, each red then reverted: **(a)** drop the truncation -> red at both
the session and the publish level; **(b)** restore all-or-nothing -> red;
**(c)** restore the per-metric fallback -> red; **(a)+(c)** together -> the
original defect, and the red line now reads
`-28.17347908020019531 is within 0.0001 of -9.33052539825439453` with
`published metric: LCeq_last = -28.1735 dB`. The mislabelling assertion was
deliberately moved ABOVE the size assertion so a regression prints the dB
error rather than `17 == 16`.

### Defect 2 -- the publish path's absence branch was untested

Publishing `SplBlockView{}` instead of `std::nullopt` inside
`buildPublishedSnapshot` left 689/689 **green**. Every case in
`test_spl_publish.cpp` called `buildSplBlockView` DIRECTLY, and nothing called
`buildPublishedSnapshot` with an `spl` argument -- so the line deciding
whether `Snapshot::spl` exists at all, and both branches that copy the base
Snapshot to attach it, were reachable from no test. Three SECTIONs now go
through the real function with a real `Analyser`: nothing logging -> no spl
block **but a real snapshot otherwise**; a config with no completed block ->
still none; logging -> `blockIndex 89`, `blockSamples 48000`,
`droppedSamples 12000`, the `Gap` flag, the 85.0 dB metric and the surviving
base-snapshot fields. Mutation **(e)**: red on both absence branches.

### Defect 3 -- the record now carries what Wave 0 measured

`docs/dsp/2026-09-16-spl-pro-l6a.md` gains a dated **§15** with five
amendments, **every paragraph above left standing**, and inline pointers at
§2's payload table and §3's "retire a block" paragraph so a reader acting on
either is sent there rather than silently contradicted. A1 the block's new
field and flag with measured offsets; A2 the membership rule
(`CalibrationInvalid` alone excludes, `Overload` includes, with the bias
argument and the statement that IEC 61672-1 cl. 3.28 and ISO 1996-2 cl. 10.3
are **paywalled and unread** so no standard basis is claimed for the four
inclusions); A3 the raw-hop overload finding cited to **Smaart LE v9.1 p. 78
and engineering grounds, explicitly NOT IEC 61672-1**, whose clauses
5.11/5.12/5.17/5.18 have not been read by anyone here; A4 the three falsified
tolerances; A5 the metric cap.

### Still open after round 2

- **`UnderRange` is RESERVED, not implemented** -- the flag exists and is
  counted, nothing sets it, cl. 5.12 is paywalled. Recorded in §15 A2.
- **Q11** (which flags exclude) remains an owner question; the default is
  shipped with five fixtures behind it.
- Everything in the pre-merge "take to the orchestrator" list below still
  stands except item 5, which is now fixed.

---

## Baselines and tallies, per config

| config | build dir | main (`00276cb`) | at HEAD | delta |
|---|---|---|---|---|
| OFF (`-DRTA_BUILD_APP=OFF`) | `build-spl` | **649/649** | **689/689** | +40 (12 core, 28 app) |
| ON (`+RTA_BUILD_APP=ON`, JUCE 9.0.1) | `build-spl-on` | **717/717** | **762/762** | +45 (the 40 above plus 5 `routing_live`) |
| forced fallback (OFF `+RTA_FORCE_ATOMIC_SHARED_PTR_FALLBACK=ON`) | `build-spl-fb` | — | **689/689** | — |

`0 warning C` in all three. The main OFF baseline was measured on the untouched
tree before W0-A; the main ON baseline is the arithmetic `762 − 45 = 717`, which
is the figure the L7 close-out already recorded, independently reached.

Guard scanned counts, each read from its own `OK (N files scanned)` line:

| guard | main | now | by |
|---|---|---|---|
| `core_has_no_framework_deps` | 161 | **164** | `+3` — `Block.h`, `Block.cpp`, `test_block.cpp` |
| `core_makes_no_class_1_claim` | 9 | **11** | `+2` — `Block.h`, `Block.cpp` (its globs cover `meter/*`) |
| `no_std_atomic_over_shared_ptr` | 371 | **387** | every new file under `core/` and `app/` |
| `measure_has_no_framework_deps` | 67 | **73** | `+1` W0-B0 (automatic, trailing `*.h` glob), `+3` W0-B, `+0` W0-C/E, `+2` W0-D — **exactly the plan's per-wave prediction** |
| `test_names_are_ascii` | — | 127 files | |

**The 161 figure is a correction to watch.** An early reading said 162; that
reading already had the uncommitted `test_block.cpp` on disk. Counted from
`git ls-tree 00276cb`, the tracked set matching the guard's globs is **161**, so
the rise is +3 and not +2. Do not re-report the +2.

## sizeof(Block), printed

`static_assert`ed in the header (so a disagreeing compiler fails the BUILD) and
printed by W0-A A1's `INFO`:

```
offsetof blockIndex     = 0
offsetof blockSamples   = 8
offsetof droppedSamples = 12
offsetof sumSquares     = 16
offsetof maxFastDb      = 24
offsetof maxSlowDb      = 28
offsetof peakDb         = 32
offsetof flags          = 36
sizeof(Block)           = 40      alignof = 8
```

`droppedSamples` occupies exactly the four bytes the `double`'s alignment was
already wasting, so **record §4's ring-size table does not move** while a gap
becomes reconstructible from the log text alone. Measured on MSVC 14.51 x64
only — GCC and Clang are what CI would add, and CI is blocked.

## What is done, per task

| task | commit | what |
|---|---|---|
| W0-A | `24f6f14` | `meter::Block`, `BlockAccumulator`, `combineBlocks`, `calibrationOffsetDb`, `excludesFromWindow` (core, OFF). 12 TEST_CASEs, 105 assertions |
| W0-B0 | `6489e8b` | `app/tests/AllocationProbe.{h,cpp}` — the ONE global `operator new` in the binary; `test_average_group.cpp` rewired to the scope guard |
| W0-B | `566c666` | `SplConfig`, `SplMeter` (app, OFF). 7 TEST_CASEs |
| W0-C | `81afef7` | `Snapshot::spl`, `SplPublishInput`, `buildSplBlockView` (app, OFF). 7 TEST_CASEs, 204 assertions |
| W0-E | `e30a5c0` | the 3.0103 dB seam, closed form (app, OFF). 4 TEST_CASEs. **Carries the §13 Q1 scope default** |
| W0-D + G2 | `ad4046b` | the drain feed (app, **ON**) + `SplSession` (OFF) + the Table 2 → Table 3 fix |
| the weighting fix | `e35f121` | one chain per DISTINCT weighting, `metricWindows`, and the master plan / weighting-doc prose |
| docs | this commit | this HANDOFF section |

## The design finding that changed the plan

**The overload run cannot live in `BlockAccumulator`.** Every sample that
reaches the accumulator has already been through a weighting filter that
changed its value, and **measured**, the C-weighted copy of three samples at
`rta::dsp::kFullScaleThreshold` does not reach that threshold at all — W0-B B3
was RED against the first implementation for exactly that reason. Overload is a
fact about what the converter delivered, so the latch now reads the **RAW** hop
in `SplMeter`, which is where SPL-R4 already put it; core takes the resulting
flag through `setFlag`, and holds no threshold for `Overload`, `UnderRange` or
`CalibrationInvalid`.

## Three plan tolerances falsified by measurement

Each widened **with its reason in the test**, and each with the EXACT form of
the same identity asserted beside it on an exactly representable fixture:

| row | plan said | measured | now |
|---|---|---|---|
| W0-A A2, `a = 1/√2` | 8.882e-16, assert tightly | **1.955e-12** | in the 1e-9 group with its residual printed; only `a = 1.0` is exact, and that one is asserted BITWISE (`sumSquares == 48000` exactly) |
| W0-B B5 | 1e-9 on a full-scale sine | **−8.27e-08 dB** | float-derived 1e-6, plus an alternating ±1 fixture where `blockLevelDb == 94.0` bitwise |
| W0-E E2/E3 | 1e-9 | **−8.27e-08 dB** | same, plus `levelDbFs(1.0) − meter(0.0) == kFullScaleSineOffsetDb` bitwise |

The cause is **float32 sample quantisation, not summation**: half a float ULP
near amplitude 1.0 is 2.98e-08 relative, giving about 2.6e-07 dB. A sine cannot
reach 1e-9 through a `float` buffer on any toolchain, so this is a plan
arithmetic error rather than a machine difference. Other measured figures worth
keeping: W0-A A2 residuals `a=0.1 −9.06e-13`, `a=0.3 −2.88e-12`, `a=0.5 0`;
W0-B B1 digital residual `−5.09e-08 dB` against `responseDb(1 kHz, A) =
0.00435887` (`analyticDb = 0`, gap 0.00435881, inside the record's 0.05);
W0-B B2 `t = 50 ms` residual `1.752e-07`, `t = 5τ` residual `−6.89e-09`;
B2b step `−4.8190745912`, decay `−1.7371779276`, gap `−3.0818966636`;
T12 after the probe move, unchanged: `bytes(1) 1359, bytes(4) 1575,
bytes(8) 1863, delta 288` against its own bound of 4352.

The plan's `5τ` deficit figure (`−0.0286 dB`) is also wrong — the closed form
is `−0.02936` from silence and `−0.029067` from the fixture's own 20 dB-down
floor. The test asserts the formula, which is why it did not matter.

## Two file-list deviations, both the plan's own escape hatch

1. **`app/src/measure/SplSession.{h,cpp}`** (NEW, JUCE-free). W0-D lists
   `AnalysisThread.{h,cpp}` only, with the escape hatch "if it breaches, the
   seam is drain-versus-feed and the feed moves out". It breached: inlining the
   feed put `AnalysisThread.cpp` at **480** lines against a 400 cap. The
   session moved here rather than into `AnalysisPublish.cpp` because this is
   STATE with a lifetime while that file is free functions. The win is bigger
   than the budget: the block clock, the gap arithmetic and the window are now
   proven with `RTA_BUILD_APP=OFF` on all three CI operating systems
   (`app/tests/test_spl_session.cpp`).
2. **`app/src/measure/AnalysisThreadSpl.cpp`** (NEW) — the same class, a second
   translation unit, the `AlignmentWizard.cpp`/`AlignmentWizardSignals.cpp`
   shape. It carries the thread handover and the published counters.
   `AnalysisThread.cpp` lands at **381**.

Also named: **`SplConfig` does not carry `std::array<DoseSettings, 2> dose`
yet.** `DoseSettings` ships in W1-D, which is Wave 1; inventing the type here
would put it in the wrong lane. Nothing in Wave 0 reads a dose.

## Mutations run, every one RED then reverted

W0-A: average per-block dB instead of summing energy → A4 read `L+5.0` against
`L+7.4036` (and A4b `70.0` against `77.0329`); delete the `CalibrationInvalid`
filter → A5, A9, A13; make `Overload` exclude → A10–A12, A13; `Gap` bit with
`droppedSamples == 0` → A8.
W0-B0: a second `operator new` in a header nothing includes → B0b read
`count = 2` **while the binary still linked**, which is the whole reason that
scan exists.
W0-B: `rta::dsp::hasOverload` per hop with no carried state → B3; sample the
detector at the block boundary instead of max-holding → B2 read `−25.95 dB`
against `−10.75` predicted.
W0-C: return a default-constructed `SplBlockView` instead of `nullopt` → C1.
W0-E: apply `kFullScaleSineOffsetDb` inside the meter path as well → E2 read
`+3.0103`, E3 read `97.0103`.
W0-D: move the tap BELOW the cap check → D1b, and channel 9 (route position 8)
went **silent with no Gap**, which is the one outcome this lane may never ship;
remove the `drainRole` feed → D2; `Gap` bit without the count → D3 (ON) and
three assertions of the OFF session test.

The weighting fix's own mutation (ignore `metricWindows`) made the C metric read
70.0 against 90.0.

Every mutation deleted the test exe first AND touched the TU holding the
assertion (`memory/mutation-testing-needs-the-exe-deleted-first.md`). **One
incident worth carrying forward:** reverting that last mutation with
`git checkout -- <file>` also reverted the *uncommitted fix underneath it*,
because HEAD predated both — exactly
`memory/a-verifier-with-bash-can-git-checkout-your-uncommitted-fix.md`, done to
myself rather than by a verifier. It cost one rebuild. **Commit the fix before
mutating it.** The run finished with a full rebuild of all three configs from a
clean `git status` —
689/689, 762/762 and 689/689 (forced fallback), `git diff HEAD` empty.

## Guards, red once each in the shape that trips them

- `core_has_no_framework_deps` — `#include <juce_core/juce_core.h>` in `Block.h`
  → FATAL_ERROR naming the file. Reverted.
- `measure_has_no_framework_deps` — the same include in `SplSession.h` → red,
  naming `SplSession.h`. Reverted.
- `core_makes_no_class_1_claim` — a `Class 1` claim in `Block.h` → red, and its
  message now reads **"full Table 3 tolerance envelope"**, which is the point of
  G2. Reverted.
- `audioio_callback_has_no_rt_hazards`,
  `audioio_scoped_no_denormals_is_first`, `output_render_has_no_rt_hazards`,
  `check_callback_shape` — not reached, still green.
  `git diff 00276cb --stat -- platform/ ui/` is **empty**, and
  `test_spl_drain.cpp` D4 asserts the same thing structurally on every build.

## What the human can try, and how

Nothing in Wave 0 is reachable from the running app — there is no SPL pane yet
(that is W2-D) and no composition-root call to `enableSplLogging` (the API is
declared, the caller is Wave 2). What can be run:

Build and run the OFF suite, which is where the whole wave's proof lives:

```bash
cmake -S . -B build-spl -G "Visual Studio 18 2026" -A x64 -DRTA_BUILD_APP=OFF
```

Then (one command per block, so the Run button appears):

```bash
cmake --build build-spl --config Release --parallel
```

```bash
ctest --test-dir build-spl -C Release --output-on-failure
```

Expect **689/689**. To see the block layout printed, and the measured residuals
this handoff quotes:

```bash
build-spl/core/tests/Release/rta_core_tests.exe "[block]" -s
```

```bash
build-spl/app/tests/Release/rtatool_analysis_tests.exe "[splmeter]" -s
```

For the ON half (the drain tap, D1b's route past the cap):

```bash
ctest --test-dir build-spl-on -C Release -R spl_drain --output-on-failure
```

Expect 5/5. These are PowerShell-safe: one command each, no `&&`, no `$`.

## Take to the orchestrator

1. **Three plan tolerances are wrong** (the table above). They are arithmetic
   errors in the plan, not machine differences, so the plan wants amending
   rather than the tests re-widening on another toolchain.
2. **Record §2's `peakDb` and the overload criterion are different streams.**
   The record says `peakDb` is C-weighted and sampled; the overload run must be
   RAW. Both are now true in the code, and the record says only the first. This
   wants one sentence in §2 and in SPL-R4.
3. **`UnderRange` has no criterion anywhere.** `BlockFlag::UnderRange` exists
   and `combineBlocks` counts it, but nothing sets it: IEC 61672-1 cl. 5.12 is
   paywalled and unread, so core deliberately holds no threshold and the flag
   is a caller's to set through `setFlag`. Wave 2 will have to decide, or the
   flag should be documented as reserved.
4. **`SplSession`'s Wave-0 window is a rolling vector, not the record's ring.**
   Capped at the longest metric's `windowBlocks` and at 3600; it `std::rotate`s
   once per block (144 KB/s at the 1 s default). W2-A replaces it with the ring
   sized once from `logSpanSeconds`. If W2-A slips, this is the thing that is
   not the record's §4.
5. **A defect found while writing THIS handoff, and fixed rather than
   reported** (commit `e35f121`). `SplMeter` runs ONE weighting per instance,
   but `SplSession` built a single A-weighted meter per channel and read every
   metric's window from it -- so a config naming `LCeq` would have been
   published **A-weighted numbers under a C-weighted label**, with
   `SplConfig::metrics` carrying a `weighting` field the code ignored. Nothing
   was red, because no fixture had two weightings in it. The session now builds
   one chain per distinct weighting and `SplPublishInput::metricWindows`
   carries that into the publish. Two counters needed care in the other
   direction: `droppedSamplesTotal` reads ONE chain rather than summing (the
   same loss rides all of them, and summing would make a reconstructed
   timestamp LATE), and `blockCount` reports the minimum across chains.
   **The lesson for the orchestrator is the shape of it**: the config could
   express something the code could not serve, and only writing the handoff
   surfaced it.
6. **Q11 (which `BlockFlag`s exclude) is still an owner question**, and the
   default is now shipped in code with five fixtures behind it:
   `CalibrationInvalid` alone.
7. The merge conflict the orchestrator should expect: `app/tests/CMakeLists.txt`
   `GLOBS`, against `remote-api/wave1-serialise`. This lane's additions are one
   contiguous run (`SplConfig.h`, `SplMeter.h`, `SplMeter.cpp`, `SplSession.h`,
   `SplSession.cpp`) inserted immediately before the trailing `*.h`/`*.hpp`
   patterns, so the resolution is "keep both runs".

## Next phase: Wave 1

Build order from the plan: **W1-A** (`LevelHistogram`) ∥ **W1-C** (`Alarm`) ∥
**W1-D** (`Dose`, two files), then **W1-B** (which extends
`core/tests/test_block.cpp`, so it follows W0-A and is already unblocked), then
**W1-E**. All five are core, all OFF, all closed-form — **no golden vector may
be added to this lane**. W1-A7/A8 are the two fixtures that pin
`histogramBaseDb()`, whose arithmetic W0-B already asserts at the default's own
site (`test_spl_meter.cpp`'s last TEST_CASE): bin 1200 of 2000 uncalibrated,
bin 1600 for 140 dB(A) at a +100 dB offset.

Known pitfalls carried forward: W1-D's D1b/D1c are BITWISE on purpose and were
measured to be so on two toolchains — a CI toolchain that refutes it is a
finding for the PR body, never a tolerance to widen. W1-D2c's 99 dBA row is a
named, excluded erratum and the bound is not widened for it.

---

> *Note added 2026-09-18 while merging `origin/main` into `l6a/wave0-spl-publish`: the PR this section calls open is **PR #16, and it MERGED at `7b4773f`**. The narrative below is left as L-API's own account of the wave; only this line is new.*

# 2026-09-17 — **L-API station 4, WAVE 1 (tasks A–G) XONG — nhánh `remote-api/wave1-serialise`, đã merge thành PR #16 tại `7b4773f`.** *(Wave 2 đã XONG 2026-09-18 — xem mục phía trên; dòng "việc kế tiếp" cũ đã bị xóa vì nó không còn đúng.)*

Bảy task, bảy commit, TDD từng cái (red dán trước green). **Toàn bộ OFF** —
không có JUCE ở đâu trong wave này, nên cả bảy chạy trên ba OS của CI.

| | |
|---|---|
| baseline OFF tại `a02fb29` | **649/649**, 0 `warning C`, guard quét **67** file |
| OFF tại `73148a9` | **698/698** (+49), 0 `warning C`, guard quét **75** file |
| OFF sau fix verifier PR #16 | **700/700**, 0 `warning C` (thêm `D6b` cap mặc định, `D9` cổng golden) |
| baseline ON tại `a02fb29` | **717/717**, 0 `warning C` |
| ON tại `73148a9` | **766/766** (+49), 0 `warning C` |
| forced-fallback OFF | **698/698**, 0 `warning C` |
| `git diff main --stat -- platform/ core/src core/include ui/` | **rỗng** |

Commit: `ecbb83a` A (ApiJson) · `3f4f370` B (ApiPolicy/ApiSettings) ·
`c1bd509` C (RateLimiter + ETag/304) · `97fb86b` D (serialiser cố định +
golden) · `6c2901b` E (serialiser spatial + union) · `73148a9` F+G
(nlohmann/json test-only + guard + readouts).

**Bốn điều một phiên sau phải biết:**

1. **F3 trong plan SAI và đã sửa — cần orchestrator xác nhận.** Plan bảo
   assert `static_cast<float>(v)` round-trip về chính nó trên mọi numeric
   leaf. **Assertion đó không thể đúng với code đúng** và đỏ ở 6/8 endpoint:
   shortest-round-trip decimal của `25.118864f` là `"25.118864"`, đọc lại
   thành double là 25.118864 chẵn, còn `(double)(float)25.118864` là
   25.118864059448242 — lệch 6e-8 **do cấu tạo**, vì Task A phát ra decimal
   ngắn nhất chứ không phải giá trị double chính xác của float. Plan còn sai
   lần hai: `effectiveAverages` và `frequencyHz` là double thật trên
   `Snapshot`, không phải float32. F3 ship ra assert cái ĐÚNG: finite khắp
   nơi, và — với các key mà nguồn thật sự là `float` (liệt kê tên tường minh)
   — narrow về float32, phát lại decimal ngắn nhất, phải trùng byte với token
   trên dây. Chứng minh có răng bằng mutation: widen float32 → double trước
   khi in (đúng lỗi của OSM, `server.cpp:386-390`) làm **F3 đỏ một mình**.
2. **Guard `no_json_parser_in_shipped_code` ĐÃ DỰNG ở wave này**, sớm hơn
   plan (plan xếp nó vào Task K). Lý do: README và `PROVENANCE.md` đều
   **tuyên bố** guard đó enforce test-only-ness, và một tuyên bố trong doc mà
   không có gì đứng sau là đúng thứ dự án này từ chối. Green: 320 file quét,
   1 witness. **Bốn red đã dán**: offender ở `app/src`, ở `core/`, ở
   `platform/`, và witness bị gỡ include. Red thứ tư kiêm luôn bằng chứng
   witness chạy trên source đã strip comment — một dòng `//` không được tính.
   **Guard `no_server_library_outside_api` thì CHƯA dựng** — nó cần
   `ApiServer.cpp` tồn tại làm `ALLOW`, tức là wave 2.
3. **`stringValue`, không phải `quoted`.** Tên `quoted` trong `ApiJson.h` va
   với `std::quoted`: ADL tìm thấy nó khi đối số là `std::string` và thắng
   overload resolution. Đo được, không đoán — build đầu đỏ với C2678 trên
   `std::_Quote_out`. Đừng đổi lại.
4. **Golden regenerate cần BIẾN MÔI TRƯỜNG `RTA_API_GOLDEN_WRITE=1`**, không
   chỉ tag `[.]`:

   ```
   RTA_API_GOLDEN_WRITE=1 rtatool_analysis_tests.exe "regenerate the API golden"
   ```

   **Bản đầu của mục này SAI và verifier PR #16 bắt được.** Nó viết rằng `[.]`
   khiến "không filter wildcard nào chạm tới". `[.]` chỉ ẩn case khỏi lần chạy
   MẶC ĐỊNH, không ẩn khỏi lần chạy có filter. Đo trên đúng binary đó: thay
   golden bằng sentinel 8 byte rồi chạy `rtatool_analysis_tests.exe "[api]"` →
   nó **chạy regenerator, ghi lại golden thành 198045 byte**, và lần chạy thứ
   hai y hệt thì **PASS**. Một format regression thật sẽ tự báo một lần rồi tự
   xoá bằng chứng — regression lock tự mở khoá, tệ hơn không có lock, vì màu
   xanh ở lần hai trông như bằng chứng.

   Tag không sửa được lỗi này vì chỗ bị tấn công CHÍNH LÀ bộ khớp tag. Nên cổng
   phải là thứ filter không cấp được: một biến môi trường. Case cũng đã bỏ tag
   `[api]` (tag của chính lane là thứ chạm tới nó), và `SKIP` kèm thông điệp khi
   cổng chưa mở. Test `D9` assert cổng đóng, và nó chạy DƯỚI đúng filter `[api]`
   từng gây lỗi. Nó ghi ở chế độ `std::ios::binary` để golden giữ LF trên
   Windows (D8 assert điều đó).

**Cho wave 2, theo thứ tự plan: H (vendor cpp-httplib) → I (`ApiServer`) →
J (composition root, ON) → K (guard server-library + chín red).** Ba bẫy đã
biết, đều từ vòng verify PR #14 và chưa bị chạm tới ở wave này: dùng
`bind_to_any_port` chứ không `bind_to_port` (cái sau trả `bool` và `Server`
không có `port()`); `ApiServer.h` **phải** là pimpl với destructor out-of-line,
nếu không một bản dựng ĐÚNG sẽ làm guard K đỏ; và WebSocket upgrade **là một
`GET`** nên method allowlist không chặn — thứ chặn là không handler nào đăng ký.

**GitHub Actions vẫn bị chặn ở mức tài khoản (billing)**, nên mọi con số ở
trên là đo tại chỗ trên máy này (MSVC 14.51, Visual Studio 18 2026, Release),
không phải từ CI. Verifier phải đo lại.

---

# 2026-09-17 — **L6a station 3 plan written: `docs/plans/2026-09-17-L6a-spl-pro-impl-plan.md`; station 4 next.**

Nhánh `l6a/station-3-plan` từ `main` (`af8a9d0`, nơi PR #12 đã merge), **docs-only,
không một dòng code**, nên không có tally nào ở đây và đó là đúng. GitHub Actions
vẫn bị chặn ở mức tài khoản (billing).

- **Năm wave**: Wave 0 SPL publish path (block + sample-count clock → `Snapshot::spl`,
  seam 3.0103 dB đóng bằng closed form), Wave 1 core pure math (histogram Ln 2000+2,
  dose hai accumulator, headroom, alarm latch), Wave 2 app (ring theo declared span,
  alarms, log `#key=value`, pane), Wave 3 calibration flow, Wave 4a report / 4b viewer.
- **Ba scope default đã lấy thay cho ba câu `[!]`** — Q1 (quy đổi một lần tại meter
  seam, không động vào bands), Q2 (**dựng** calibration flow → Wave 3), Q8 (viewer
  **ship, cuối cùng, có gate** → Wave 4b). Chi phí nếu chủ nhân lật nằm trong bảng
  đầu plan; Wave 3 và Wave 4b bị cắt là **xoá nguyên tác vụ**, không viết lại cái khác.
- **Mười hai reconciliation `SPL-R1..R12`** trong plan, orchestrator sửa record trước.
  Nặng nhất: **SPL-R1** — `rta::dsp::RingBuffer` chỉ có MỘT `readIndex_`, nên yêu cầu
  "SPL meter ngồi trên drain riêng của measurement channel" (§C4) **không dựng được
  như viết**; feed đi theo scratch buffer, và đường routed bị reference gate — block
  mang cờ `Gap` để log **nói ra** là nó đứng, thay vì nói dối.
- **Một việc chỉ chủ nhân làm được**: phép thử Chrome Local Network Access (Wave 4b
  Gate 2) — thủ tục và bốn thứ cần báo lại nằm trong plan.
- **Task G2** nhặn nốt §13 Q10: `core/tests/check_no_conformance_claim.cmake:18,61`
  còn ghi "Table 2" — đúng là **Table 3**. Đó là code, thuộc trạm 4, không đụng ở PR này.

**Bản 2 (2026-09-18) — đã vá **mười bốn** lỗi từ vòng verify đối kháng trên PR #15**
(`bf74e52`). Verdict: **SOUND-WITH-FIXES, station 4 GO, wave đầu là W0-A**; SPL-R1
bị thử bác bỏ và **đứng vững**. Bảy lỗi nặng — cái nào cũng **đỏ ngay lần
chạy đầu** hoặc **ship một con số sai mà không ai biết**:

1. **W0-A A4** — fixture "hai nửa ở `L` và `L+10`" phải là `10log10(5.5) = 7.4036`,
   không phải `10log10(5.05) = 7.0329` (đó là case **±10 dB**, lệch 0.371 dB).
   Đây đúng là cái bẫy `core/tests/test_leq.cpp:41-47` đã ghi sẵn. Thêm A4b.
2. **Dung sai 1e-12 nằm DƯỚI sàn làm tròn** của một tổng 48 000 mẫu (đo được
   2.636e-12 / 2.874e-12). Lấy **1e-9** kèm lý do của `test_leq.cpp:69-74`.
3. **`histogramBaseDb = -20.0`** làm **mọi Ln của mọi phiên chưa calibrate** vĩnh
   viễn là `BelowSpan` — vì Q1 default để SPL chưa calibrate ở dBFS. Base giờ
   **dẫn xuất** từ `referenceOffsetDb`; thêm fixture W1-A7/A8.
   (`memory/a-default-must-be-run-through-the-gate-it-feeds.md` đúng y nguyên.)
4. **`Gap` không tái dựng được từ log**, và `t_iso` sai vĩnh viễn sau cú drop đầu
   tiên. Thêm `droppedSamples` **vào đúng 4 byte padding** — `sizeof(Block)` vẫn
   **40**, bảng ring §4 không đổi — và thêm cột đó vào CSV.
5. **Counting allocator là global `operator new`**, không copy được hai lần vào một
   binary → task mới **W0-B0** tách `app/tests/AllocationProbe.{h,cpp}`.
6. **Điểm tap sai**: `locateBuffer_.feedHop` (`:269`) nằm **DƯỚI** chốt
   `kMaxTransferFunctions` (`:256`), nên route ≥ 8 sẽ **im lặng, không cả `Gap`**.
   Tap chuyển lên giữa `:254` và `:256`; thêm test D1b cho route 8.
   (`memory/a-cap-checked-on-the-drain-path-is-unchecked-on-the-publish-path.md`.)
7. **Cờ nào loại block khỏi `combineBlocks`** chưa ai nói → **chỉ
   `CalibrationInvalid`** (có clause ISO 1996-2 cl. 5.2), `Overload` **KHÔNG** (loại
   nó là xoá khoảnh khắc to nhất của show khỏi số liệu pháp lý). Năm fixture
   A9–A13, và một câu cho chủ nhân (Open item 7).

Bảy lỗi còn lại sửa tại chỗ. Đáng ghi: **SPL-R7 bị bác lý do** — con số `4.7e-8`
không tồn tại, `9.9657843` là làm tròn **lên** chứ không phải cắt, và **không một
acceptance số học nào trong lane phân biệt được hai hằng số** — luật sống nhưng
chỉ dựa vào D1f (bitwise). Guard count sửa: `core_makes_no_class_1_claim` **+13**
(8 file meter + 5 file test phải thêm tên — SPL-R9 mở lỗ hổng ở `core/tests` đúng
lúc nó vươn sang `app/`), `core_has_no_framework_deps` **+13**, W0-C **+0**,
W2-D **+1**. Port viewer **4736**, không phải 4737 (API-R14). Và **static-asset
mount KHÔNG nằm trong L-API v1** — Gate 1 của Wave 4b là **hai** việc, không phải
một. **Chưa bắt đầu bất kỳ việc gì của Wave 4b.**

**Bản 3 (2026-09-18) — vòng verify thứ hai: bảy fix nặng đều ĐƯỢC XÁC NHẬN BẰNG
ĐO** (`sizeof(Block) == 40` **biên thật**, đọc từng dòng chỗ tap, tính lại A4/A4b,
và lý lẽ loại-trừ cờ được **phán là đúng**). **Station 4 GO, wave đầu vẫn là
W0-A.** Thêm bốn mục nhỏ, tất cả đã vá, **mọi con số đều đo trên máy này chứ
không suy luận**:

- **N1** — hàng guard delta ghi "sum to 19" trong khi chính danh sách của nó cộng
  ra **18**, và một trong số đó **tự động** (glob) nên số phải thêm tay là **17**.
  Giờ in đủ ba con số kèm quy ước của từng con, và gọi tên trường hợp Wave 4b
  (**18 tay / 19 tổng**). Đây đúng là hình dạng của defect 7 tái xuất **bên trong
  bản vá cho defect 7**.
- **N2** — D1b/D1c đã bị nới ra 1e-12 dựa trên một **phủ định không đo**. Đo lại:
  `pow(10, log10(2)) == 2.0` **bitwise** trên cả g++ 16.1.0 (MinGW-W64 ucrt) lẫn
  MSVC ucrt, và `D == 100.0` bitwise với `Q ∈ {3,4,5,6}` cả hai chiều.
  **Trả lại "exactly"**, kèm NOTE: nếu một toolchain CI bác bỏ thì đó là **một
  phát hiện phải báo** (tên libm, tên OS, trong PR body), **không phải cớ để nới
  dung sai**.
- **N3** (**có sẵn từ bản đầu, vòng 1 bỏ sót**) — W0-B B2 trích công thức
  `1 − e^{−t/τ}` rồi in `−1.75 dB`, mà `−1.7372` là `10·log10(e^{−0.4})` — **số hạng
  suy giảm, tức phần bù của chính công thức đứng cạnh nó**. Đúng phải là
  `10·log10(1 − e^{−0.4}) = −4.8190745912`, lệch **3.0819 dB**; và dung sai 0.5 dB
  "dẫn xuất từ" con số sai ấy sẽ làm **một bản choài đúng đỏ khoảng 4.3 dB**.
  B2 viết lại quanh công thức đúng, dung sai **dẫn từ `float`** (đo: ULP float32 ở
  100 dB là `7.62939453e-06` → lấy `1e-4 dB`), và τ **đọc từ
  `Detector::riseTimeConstant`** chứ không gõ tay — vì 125 ms vẫn là **UNVERIFIED**
  trong ledger §14. Thêm **B2b** chốt cả hai số hạng theo tên.
- **N4** — một câu trong SPL-R8: offset được cộng vào **cả giá trị lẫn base**, nên
  nó **triệt tiêu** — thứ thực sự vào `add()` là `levelDb` chưa offset, và bin 1600
  của A8 vẫn đúng.

**Vẫn chưa bắt đầu bất kỳ việc gì của Wave 4b.** Docs-only, không tally, Actions
vẫn bị chặn billing.

---

**2026-09-17 — L-API station 3 plan written: `docs/plans/2026-09-17-remote-api-impl-plan.md`; station 4 next.** Mười task (A–J), **chín** chạy hết trong `RTA_BUILD_APP=OFF` không cần JUCE (vòng verify PR #14 chỉ ra server dùng `std::thread` + `bind_to_port`/`listen_after_bind` là thuần std, nên `Host` check được chứng minh trên CI ba OS chứ không chỉ trên máy này); chỉ Task I (wiring composition root) là ON. Mười tám reconciliation (`API-R1..R17` + `R16a`) đã được ghi thành **§15 amendment** trong `docs/dsp/2026-09-16-remote-api.md`. Hai vòng verify đối kháng trên PR #14: vòng 2 cho **station 4 GO (task A–H)** và bắt thêm năm lỗi cơ học — `bind_to_port` trả `bool` và `Server` không có `port()` (phải dùng `bind_to_any_port`); `httplib::Server` để by-value sẽ kéo include vào `ApiServer.h` và làm guard mới **ĐỎ trên một bản dựng đúng**, nên phải pimpl; guard JSON thiếu red ngoài `app/src`; và WebSocket upgrade **là một `GET`** nên method allowlist không chặn — thứ chặn là không có handler nào đăng ký. Năm câu §14 đều đã chốt default có tên — port đổi **4737 → 4736** vì 4737 là IANA `ipdr-sp`. Docs-only, chưa build gì.

---

# 2026-09-16 — **L6a (SPL-pro) trạm 1+2 XONG — nhánh `l6a/stations-1-2`, docs-only, PR mở, CHƯA merge. Trạm 3 (impl plan) là việc kế tiếp.**

Hai tài liệu mới, không có một dòng code nào:
`docs/research/2026-09-16-l6a-spl-pro-station1-research.md` (Part 0 bảy đính
chính, Part A các tiêu chuẩn kèm số clause + URL + ngày đọc, Part B mã nguồn
chín dự án đọc tại SHA ghim, Part C các seam của repo, Part D web viewer,
Part E bảng decision → evidence) và `docs/dsp/2026-09-16-spl-pro-l6a.md`
(§0–§14: mười một quyết định, §12 "không quyết gì", §13 mười câu hỏi cho chủ
nhân, §14 ledger VERIFIED/UNVERIFIED).

**Ba điều một phiên sau phải biết trước khi đọc code:**

1. **Dependency "Meters track" chỉ thoả ở `core/`.** `grep -rn
   "rta::meter\|rta/meter"` toàn repo trả **16 dòng, tất cả dưới `core/`**;
   `WeightingType` xuất hiện đúng ba file, cũng `core/`. `measure::Snapshot`
   không mang broadband level, không weighting, không detector. Nên **wave đầu
   của L6a là dựng SPL publish path**, không phải logging. Đây đúng là tình
   huống L5c gặp ("`app/` chưa từng gọi dual-FFT engine của L2") lặp lại. Trạm 1
   của lane L-API tìm ra **cùng một chỗ hổng, độc lập** (record remote-api §12.1)
   — hai pass song song không trao đổi mà trùng kết luận.
2. **Web viewer (G7) là CLIENT của L-API**, không mở listener thứ hai:
   `docs/dsp/2026-09-16-remote-api.md` §12 (PR #11, **đã merge tại `a39a02e`**) chốt
   việc đó. Transport là cpp-httplib (MIT) theo record kia — bản nháp đầu của
   §9 lập luận từ `juce::StreamingSocket`, đã bị thay thế. Viewer là thứ **cuối
   cùng** L6a xây, không phải thứ đầu tiên.
3. **`docs/dsp/2026-08-27-weighting-and-meters.md` ghi sai số bảng** và đã được
   đính chính tại chỗ trong commit này: tolerance của weighting nằm ở
   **Table 3**, không phải Table 2 (Table 2 là directional response).
   `core/tests/check_no_conformance_claim.cmake:18,61` còn mang số sai trong
   comment và trong thông báo lỗi — đó là code, để lane sau chạm vào (§13 Q10).

**Không có tally nào ở đây và đó là đúng:** PR này docs-only, không đụng
`core/`, `app/`, `platform/`, `ui/` hay CMake, nên không build lại và không có
con số test nào để dán. GitHub Actions vẫn bị chặn ở mức tài khoản (billing).

**Ba câu hỏi chặn phạm vi của trạm 3**, đầy đủ mười câu ở
`docs/HUMAN-QA-QUEUE.md` mục "Từ lane L6a (2026-09-16)": seam 3.0103 dB giữa
`Levels.h` và `meter::Leq` (Q1), có dựng calibration flow trong L6a không (Q2),
và web viewer có ship trong lane này không (Q8).
# 2026-09-16 — Remote API trạm 1+2 đã viết (DOCS-ONLY, không đụng code)

Lane **L-API** (remote read-only API) — trạm 1 nghiên cứu và trạm 2 record đã
xong, nhánh `remote-api/stations-1-2` từ `6d9a53d`, PR docs-only. **Trạm 3
(impl plan) là việc kế tiếp.**

> **Cập nhật 2026-09-17 bởi lane L6a:** mục này ghi "CHƯA merge" khi viết.
> **PR #11 đã merge vào `origin/main` tại `a39a02e`** (2026-09-17T15:35:44Z).
> Record của nó giờ là quyết định đã đáp, không còn là đề xuất — L6a §9 trích
> theo nghĩa đó.

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

## Vòng fix thứ ba (2026-09-17): merge `main` rồi soi lại citation

Verifier vòng 2 bác ba chỗ. Đã sửa hết, **sau khi merge `origin/main`
(`e213202`, PR #10 + #13 đã vào)** nên mọi số dòng dưới đây đọc ở cây ĐÃ MERGE,
không phải ở `6d9a53d` mà bản trước pin:

1. **Chỗ thứ tư của `AudioIo.cpp:117-148`** mà vòng trước sót: bullet "the
   audio callback is literally two calls after `ScopedNoDenormals`" trong
   `docs/research/2026-09-16-remote-api-station1-research.md` (Part C,
   `:609` sau merge). `grep -rn "117-148"` toàn nhánh giờ chỉ còn hit trong
   chính `HANDOFF.md` này — tức các câu KỂ LẠI việc sửa, không còn citation
   nào.
2. **Citation trôi vì `main` đổi file.** PR #10 thêm 5 dòng vào
   `check_no_framework_deps.cmake`, nên regex **`:49` → `:54`**; và
   `measure_has_no_framework_deps` trong `app/tests/CMakeLists.txt`
   **`:274` → `:280`** (`-DGLOBS=` ở `:282`). Mỗi citation dễ trôi giờ kèm
   **"(at main `e213202`)"** và một **grep handle** (`content MATCHES`,
   `add_test(NAME …)`) để lần sau không phải tin con số.
3. **§11 item 13 (`docs/dsp/2026-09-16-remote-api.md:720`) KHÔNG đặc tả
   formatter mới nữa.** Ba hàm đã có thật trong
   repo: `app/src/view/Readouts.h` — `formatHz` (`:72`), `formatTrim` (`:79`),
   `formatAgreement` (`:87`) — đã bị `app/tests/test_readouts.cpp:100-115` ghim,
   đã nằm trong glob `measure_has_no_framework_deps`, tức đã framework-free và
   đã test được ở `RTA_BUILD_APP=OFF`. Item 13 giờ **tái dùng** đúng ba hàm đó.
   Lưu ý chuỗi trả về **mang đơn vị**: `formatTrim(8.5859375) == "8.6 dB"`,
   `formatHz(1000.4) == "1000 Hz"`, `formatAgreement(0.9731445) == "0.97"`.
4. **§11 item 10 (`:660`) ghi thêm sentinel thứ hai** của
   `check_no_std_atomic_shared_ptr.cmake`
   (`:145`, `ALLOW_CODE MATCHES SENTINEL_PATTERN`): file được ALLOW phải CÒN
   chứa thứ đang bị guard. Item 10 copy cả hai, nếu không bản sao yếu hơn bản
   gốc nó trích.

Một mục **KHÔNG thuộc PR này**: `docs/HUMAN-QA-QUEUE.md` §"Tech-debt phát hiện
lúc closeout L7" nói `check_no_framework_deps.cmake` chưa quét `tests/*.h` —
PR #13 (`aafc5f5`) đã thêm glob đó, nên mục ấy cùng citation `:37-43` của nó
đã lạc hậu ở `main`. Nội dung của `main`, phiên sau dọn.

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
>   `remote-api/stations-1-2`. Đây đúng là mảnh L6b scope out mà
>   `MASTER-EXECUTION-PLAN.md` từng ghi là "chưa có lane"; hàng **L-API** đã
>   được thêm vào plan TRÊN NHÁNH CỦA PR ĐÓ, nên nhánh này chưa thấy nó.
>   **Cập nhật 2026-09-17: PR #11 ĐÃ MERGE tại `a39a02e`** (bản gốc của dòng
>   này ghi `828c223`, OPEN). Hàng L-API và record của nó đã có trên `main`.
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
