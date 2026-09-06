# HANDOFF — cho phiên tiếp theo

*2026-08-28, cuối phiên orchestrator kế nhiệm. Đọc file này + `docs/reports/002`
+ `docs/plans/MASTER-EXECUTION-PLAN.md` trước khi đọc bất kỳ dòng code nào.*

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

## Còn mở (không đổi so với mục merge L6b bên dưới)
- **Chưa commit, chưa push.** Commit là lệnh của chủ nhân; push càng vậy.
- Ba câu hỏi chủ nhân trong `HUMAN-QA-QUEUE` (mục L6b) vẫn treo.
- Lane lớn kế tiếp theo master plan: **L7 (Solvers)** — cần một lượt research
  trạm 1 (chưa có decision record).

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
