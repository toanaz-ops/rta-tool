<!-- Fable strategic review 2026-10-21, model claude-fable-5-1 (read-only), dispatched by DEV MASTER on owner request. -->

# PRJ010 — Fable review & phương án (2026-10-21)

> **Ghi chú khi đưa vào repo (2026-10-21, phiên điều phối lane):** bản review viết ở `ea539fc`. Mục tuần 1 #4 (FFT twiddle contiguous + hoist conjugate) đã mở thành PR #82. Các câu hỏi ở mục 6 vẫn chờ chủ nhân; không có quyết định nào ở đây được coi là đã chốt.

*Read-only, model `claude-fable-5-1`. Không build, không chạy gì. Đường dẫn tương đối tính từ `D:\DEV CAVE EP3\PRJ010-RTA-TOOL`. Lưu ý trước khi đọc: checkout chính (`main` local) đang đứng ở `0a8f236` (`.git/logs/refs/heads/main:104`, fast-forward 2026-10-19) trong khi `origin/main` = `ea539fc` (PR #81, `D:\DEV CAVE EP3\shared\handoff\handoff-20261021-rta-h6-audit.md:19`). Tài liệu mới nhất (entry H6 + audit) chỉ có trong worktree `.claude/worktrees/agent-a773b5ddc11ea0575/docs/` — các trích dẫn "WT:" bên dưới trỏ vào đó.*

---

## 1. Hiện trạng đo được

**Phase/lane đã đóng.** Chuỗi tuần tự của plan review #2 (H1 session-UX → H2 routing-64 → H3 EQ-UI → H4 SWEEP → H5 ALIGN-UI → H6 RT60-UI) đã xong toàn bộ; PR #70–#81 merge, CI 6/6 xanh ở mọi head, ctest ON 1528/1528, orphan_check exit 0 (`shared/handoff/handoff-20261021-rta-h6-audit.md:24-42`; WT:`docs/HANDOFF.md:8-56`). Vòng audit Fable 2026-10-21: HIGH (#78) và 5 MEDIUM (#76, #78, #80, #81) đã sửa; còn DEFER perf FFT scalar + lưới `crossoverBandFit` (WT:`docs/HANDOFF.md:20-23`; `D:\DEV CAVE EP3\shared\audits\prj010-fable-audit-20261021.md:59-67`).

**Phần trăm theo bảng 19 unit của repo** (đếm unit trọng số bằng nhau, không phải % công sức — `docs/plans/MASTER-EXECUTION-PLAN.md:42-44`):

| chỉ số | giá trị | nguồn |
|---|---|---|
| (a) engine built | 15/19 = 78.9 % | WT:`docs/HANDOFF.md:37` |
| (b) operator-reachable (gồm partial) | 14/19 = 73.7 % | WT:`docs/HANDOFF.md:38`; WT:`docs/plans/MASTER-EXECUTION-PLAN.md:133` |
| (b-strict) BUILT không partial | 10/19 = 52.6 % | WT:`docs/HANDOFF.md:39` |

Năm hàng ngoài BUILT: 5 L4c IR gate/min-phase/offline WAV (NOT STARTED, chưa có plan), 6 THD/STI (BLOCKED, IEC 60268-16), 8 targets/X-curve (BLOCKED, ISO 2969/ST 202), 12 L-API (CORE ONLY — `enabled=false` hard-code tại `app/src/MainComponent.cpp:212`), 19 productization (NOT STARTED) (`docs/plans/MASTER-EXECUTION-PLAN.md:26-40`). Bốn hàng "BUILT (partial)" được headline đếm là BUILT: 1 (không có generator control ngoài pink noise cố định của LOCATE/VERIFY), 9 (multi-pane chỉ qua OPEN), 11 (TF slot 1..7 không với tới; `CaptureSequencer` không caller), 13 (không chọn được output) (`MASTER-EXECUTION-PLAN.md:22,30,32,34,139-145`).

**Quy mô.** `core/src` 50 file .cpp, `app/src` 107 file .cpp (đếm header SPDX trong WT), 76 file `docs/**/*.md`. `docs/HANDOFF.md` đã 328 KB (Read từ chối mở nguyên file) — bản thân nó là một rủi ro vận hành (mục 3).

**Orphan còn lại (theo định nghĩa rule 5 của `CLAUDE.md:244-262`).**
- `app/src/measure/CaptureSequencer.{h,cpp}`: chỉ tự tham chiếu + 2 dòng chú thích ở `EqVerify.h`, `OutputPolicy.h` (grep WT `app/src`); hoãn có chủ ý từ plan review #2 (`docs/HANDOFF.md:172-174`).
- `RoutingMatrix::setActiveTransferFunction`: zero caller (`MASTER-EXECUTION-PLAN.md:140-142`).
- Dead/test-only core API (`Window::coherentGain…`, `mtwAlpha`, `RingBuffer::read`, `AtomicSharedPtr::exchange`) và nhánh config một giá trị (`Estimator::Hv`, `TransferAveraging::Exponential`) — audit xếp "owner taste", chưa quyết (`shared/audits/prj010-fable-audit-20261021.md:72-76`).
- `AlignmentWizard` và cả chuỗi IR/decay đã hết orphan sau H5/H6 (`MASTER-EXECUTION-PLAN.md:25,38`).

**Cái gì được chứng minh bằng mic/phần cứng thật, cái gì chỉ synthetic.** Kết luận thẳng: **chưa có một phép đo nào của sản phẩm được quan sát trên phần cứng thật.**
- Phiếu T12 (M1–M7, Phase 1): cột "Thấy gì thật" và "Đạt?" vẫn trống (`docs/reports/T12-hardware-run.md:13-14,52-60`); M2 (nói vào mic) và M7 (loopback vật lý) "still pending a human" (`MASTER-EXECUTION-PLAN.md:22`; `docs/reports/002-phase1-audio-chain.md:92-95`).
- Bản build ASIO `build-asio/` mà T12 nói "exe đã được giữ lại" (`T12-hardware-run.md:4-6`) **không còn tồn tại** (Glob `build-asio/app/rtatool_artefacts/Release/*.exe` rỗng). CI không bao giờ build ASIO: `RTA_ENABLE_ASIO` OFF (`CMakeLists.txt:21`), không workflow nào nhắc tới (grep `.github/workflows`).
- Hàng B (VERIFY), C (SWEEP), ALIGN, D (RT60) trên phần cứng: tất cả mở, "later" (WT:`docs/HUMAN-QA-QUEUE.md:1202-1216`; WT:`docs/HANDOFF.md:85`). SYNTHETIC từ chối VERIFY theo thiết kế (`docs/HANDOFF.md:62`); SYNTHETIC chỉ render MỘT output nên không chứng minh được solo kênh 1+ của ALIGN (WT:`docs/HANDOFF.md`, entry H4, pitfall "SYNTHETIC render MỘT output channel"); SYNTHETIC không có phòng nên RT60 **từ chối mọi band**, phòng comb chỉ vào qua test hook (WT:`docs/HUMAN-QA-QUEUE.md:1183-1192`).
- Cái đã chứng minh được không cần phần cứng, và chứng minh tốt: DSP core đối chiếu closed-form/chuẩn/golden (`CLAUDE.md:26-37`), audit xác nhận PSD/dual-FFT/PHAT/ESS/weighting/callback đúng quy ước (`shared/audits/…:107-116`), fake `juce::AudioIODevice` ở `platform/tests_juce/` bắt được lỗi `audioDeviceError` thật (`docs/reports/002-phase1-audio-chain.md:102-107`).

## 2. Mục tiêu dự án và độ lệch

**Mục tiêu đã ghi.** "Build the best RTA analyser there is: every feature superior to what is on the market" (chủ nhân, 2026-08-27) với bảng "ai dẫn / better nghĩa là gì / đo thế nào" (`docs/specs/2026-08-26-rta-tool-design.md:21-38`); **feature floor = hợp của Smaart v9 Suite ∪ RiTA** (`:40-43`); swept-sine là mode hạng nhất kiểu REW/ARTA (`:45-50`); và luật tự ràng: "superiority is a claim, and this project does not make claims it has not measured" (`:52-56`). Ledger parity G1–G26 (`docs/dsp/2026-08-28-competitive-parity.md:17-56`).

**Độ lệch với thực tế.**
1. *Đối với floor Smaart∪RiTA:* đã có G1, G2, G6 (một phần), G7, G8, G9, G10, G11, G14, G15, G17, G18, G20 (một phần), G24 (kernel). **Chưa có:** G3 THD, G4 STI (chuẩn), G5 X-curve (chuẩn), G12 reference-free TF, G13 AES-75, G16 environment comp, G19 network audio, G21 polarity checker (core có `core/src/ir/Polarity.cpp`, không UI), G22 offline WAV, G23 cepstrum/wavelet, **G25 IR gating** — thứ ledger tự gọi là "table stakes in every surveyed tool" (`competitive-parity.md:55`) — và G26 plugin API. Hàng 5 L4c (gating/ETC/min-phase view) là hàng duy nhất chưa BUILT mà không bị chặn bởi tiền và không có plan (`MASTER-EXECUTION-PLAN.md:26`).
2. *Đối với "superior, measured":* bảng superiority yêu cầu "runs at a show" (multichannel ASIO) và "dropout counters under load" (`design.md:35-36`); cả hai chưa có số đo nào ngoài CI. Mọi "better" hiện là tuyên bố, chưa có con số đặt cạnh incumbent — chính xác điều `design.md:55` cấm. Trang marketing có "superiority section" status-tagged (`docs/marketing/product-page-content.md:38,59`) — cần giữ tag planned, không được hiểu là đã đo.
3. *Đối với "dùng được tại show":* không có generator control (tín hiệu / mức / kênh ra) (`MASTER-EXECUTION-PLAN.md:145`), không installer/manual (hàng 19). Một kỹ sư PA hôm nay không thể dùng app ngoài SYNTHETIC mà không build từ source.

Tóm lại: **engine ~79 %, thao tác được ~74 %, nhưng bằng chứng thực địa 0 %.** Năng suất 4 lane H3–H6 rất tốt (12 PR trong 20–21/10) nhưng tất cả xếp chồng lên một tầng chưa được ai cắm mic kiểm.

## 3. Rủi ro lớn nhất

1. **Bề mặt chưa đo tăng theo từng lane.** H3–H6 cộng thêm VERIFY, SWEEP, ALIGN, RT60 — bốn luồng phát-thu-deconvolve chỉ chứng minh qua loopback `OutputEngine → SyntheticInput` (WT:`docs/HANDOFF.md` entry H4, PR #67). Cái SYNTHETIC không thể cho thấy được ghi rõ: phản xạ, drift, băng thực của driver, phòng hai dốc, nhiễu nền (WT:`HUMAN-QA-QUEUE.md:1214-1215`). Chi phí đổi hướng tăng theo số lane xây thêm trước khi đo.
2. **Không còn đường dẫn phần cứng.** Exe ASIO đã mất, CI không build ASIO, phiếu T12 chưa điền (mục 1). Ngay cả khi chủ nhân rảnh, hôm nay chưa có gì để cắm.
3. **Trần 400 dòng của `MainComponent.h` và hot-file tuần tự.** Plan H6 ghi `MainComponent.h` 360 dòng, mỗi pane mới "~5 lines" (WT:`docs/plans/2026-10-20-rt60-ui-lane-plan.md:153`); handoff 09-29 đã cảnh báo "đang sát trần 400" (`docs/HANDOFF.md:67`). Năm NEW-LANE đều chạm file này; không tách trước thì lane nào cũng chờ lane kia (`docs/HANDOFF.md:114`).
4. **Nợ perf nằm đúng đường nóng.** FFT scalar radix-2 với twiddle stride là CPU chủ đạo khi 8 analyser x 7 band MTW (`shared/audits/…:59-61`); rebuild analyser đã hạ còn 107–112 ms/slot nhưng ring headroom ở 192 kHz chỉ 341 ms (`shared/handoff/…:42`). Exit hang dưới tải "cause unproven" (`shared/handoff/…:62`). Loại lỗi chỉ hiện ở show, lại chưa có show nào.
5. **Phân mảnh trạng thái.** Checkout chính lệch 11 PR so với origin; HANDOFF 328 KB; năm worktree `agent-*` mỗi cái một bản HANDOFF (grep `^# 2026-10-21` cho 6 hit). Audit đã ghi "HANDOFF three weeks behind" (`shared/audits/…:87-88`); đã sửa trong worktree nhưng người đọc checkout chính vẫn thấy entry 09-29 (`docs/HANDOFF.md:8`).
6. **Hai hàng bị chặn bởi tiền** (IEC 60268-16, ISO 2969/ST 202; `docs/HUMAN-QA-QUEUE.md:105-110`) — trần cứng cho floor Smaart Suite.

## 4. Phương án hướng đi (H7+)

| | A. Hardware-QA-first | B. NEW-LANE batch (operator path) | C. L4c IR toolkit (G25/G22/G21/G24-view) | D. Productization + perf |
|---|---|---|---|---|
| Được | Biến 0 % bằng chứng thực địa thành số; bắt lỗi loại "chỉ ở show" trước khi xây thêm | Lật 4 hàng partial + hàng 12 → (b-strict) 15/19; mở khoá generator level control — điều kiện an toàn để đo phần cứng (hiện pink noise mức cố định, `MASTER-EXECUTION-PLAN.md:145`) | Đóng khoảng trống parity "table stakes" so với REW/ARTA; không cần chuẩn mua | Installer/manual/i18n (hàng 19); FFT twiddle contiguous/radix-4 (golden `core/tests/test_fft.cpp` khoá) |
| Mất | Agent gần như đứng chờ chủ nhân; không tăng % bảng | Vẫn không có bằng chứng thực địa; đụng `MainComponent.h` trần 400 → cần tách trước | L: research station 1-3 + hai wave; càng xa tầng chưa đo | Không đổi năng lực đo; perf chỉ có ý nghĩa khi có tải thật |
| Công sức | Agent S (dựng lại ASIO build, gộp phiếu QA) / Owner 2–3 h với gear | M (5 lane nhỏ + 1 refactor tách header) | L | M |
| Không đảo ngược | Không | Không (chỉ app/) | Không, trừ nếu kéo theo MTW storage v4 (schema — không đảo ngược, `docs/HANDOFF.md:186-188`) | Installer/licence text là public — khó rút |
| Phụ thuộc | Interface + mic + loa + chủ nhân có mặt | Không | Không | Không |

**KHUYẾN NGHỊ: A + B chạy song song, B mở đầu bằng "generator/output control + tách `MainComponent.h`" (gọi là H7).** Lý do: (i) A là việc duy nhất agent không thay được, và mọi lane sau đó đều rẻ hơn nếu biết H3–H6 đúng trên mic thật; (ii) phần đầu của B (level/kênh ra) là tiền đề thực tế cho A — chủ nhân không nên đặt loa vào pink noise mức cố định; (iii) tách header trước là điều kiện để 4 lane còn lại của B không xếp hàng. C và D xếp sau khi A có kết quả: nếu A lộ lỗi ở chuỗi sweep/IR, C đổi scope; nếu A sạch, C là lane kế tiếp tự nhiên và D đi cùng. Từ chối phương án "cứ làm tiếp C/D rồi đo sau": đó là lặp lại hình L6a (xây 4 wave không ai gọi, `memory/a-component-with-no-production-caller-is-not-shipped.md`) ở quy mô sản phẩm.

## 5. Kế hoạch 2 tuần

**Tuần 1 — agent làm ngay (không chờ ai):**
1. Đồng bộ checkout chính lên `origin/main` `ea539fc` (cần chủ nhân gật `git pull`, rule 5) và thu HANDOFF: giữ 3 entry mới nhất, phần còn lại sang `docs/handoff-archive/` (docs-only, đảo ngược được).
2. H7 station 1-3: research + plan "generator/output control" (tín hiệu pink/sweep/sine, mức dBFS, kênh ra, mutual exclusion với LOCATE/VERIFY/SWEEP/ALIGN qua accessor `captureBusy()/outputBusy()` sẵn có từ PR #69), kèm task 0 = tách `MainComponent.h` theo seam pane (plan phải có cột "called from" + "operator path", `CLAUDE.md:244-268`).
3. Dựng lại `build-asio/` (`RTA_ENABLE_ASIO=ON`, SDK tại `D:\DEV CAVE EP3\PROJECT005-AZ-handsfree\external\asiosdk`, `T12-hardware-run.md:20-28`) từ head mới; gộp T12 M2/M7 + hàng B/C/ALIGN/D thành MỘT phiếu `docs/reports/hardware-qa-2026-10.md` với cột "kỳ vọng thấy gì" có số (D ≈ khoảng cách/343·fs; RT60 so REW cùng vị trí; `HUMAN-QA-QUEUE.md:1060-1216`).
4. Perf DEFER của audit: twiddle contiguous + hoist conjugate trong `core/src/dsp/Fft.cpp` (golden khoá; S–M; agent-safe theo audit `:61`).

**Tuần 2 — agent:** build H7 wave A/B theo vòng verifier (no HIGH/MEDIUM); kế tiếp TF slot picker + L-API enable switch nếu header đã tách. **Chờ chủ nhân / phần cứng:** một phiên 2–3 giờ với interface, loa, mic, dây loopback: T12 M2–M7 → hàng C (SWEEP) → hàng B (VERIFY) → ALIGN → hàng D (RT60, so với REW). Agent nhận phiếu đã điền, triage thành lane sửa (nếu có) trước khi mở C.

**Không làm trong 2 tuần:** MTW storage v4, L4c, mua chuẩn, bất kỳ câu "superior" nào ra trang marketing.

## 6. Câu hỏi chủ nhân cần chốt

1. **Khi nào có phiên phần cứng?** Khuyến nghị: trong 2 tuần, trước mọi lane đo mới; một phiên đủ cho cả 5 hàng. Nếu không thể, nói rõ để agent chuyển sang C với nhãn "unmeasured" đậm hơn.
2. **H7 = generator/output control + tách `MainComponent.h` đi trước các NEW-LANE khác?** Khuyến nghị: có — tiền đề của cả A lẫn 4 lane còn lại.
3. **Dead/test-only core API và nhánh config một giá trị (audit, owner taste):** xoá, dán nhãn test-only, hay lộ ra operator path? Khuyến nghị: dán nhãn `ForTest`/ghi test-only ngay (S, đảo ngược được); chỉ lộ Hv/exponential nếu phiên phần cứng cho thấy cần.
4. **Mua IEC 60268-16 và ISO 2969/ST 202** (mở hàng 6, 8)? Khuyến nghị: hoãn tới sau phiên phần cứng; tiền không đổi được cái đang thiếu nhất là bằng chứng thực địa.
5. **MTW storage schema v4** (không đảo ngược): giữ UNSCHEDULED? Khuyến nghị: giữ, cho tới khi B xong và A sạch.
6. **Cho phép `git pull` checkout chính và tách/archive `docs/HANDOFF.md`?** Khuyến nghị: có, docs-only, một PR riêng.
7. **Trang marketing:** giữ mọi mục "superior" ở tag planned cho tới khi có số so với incumbent (`design.md:52-56`)? Khuyến nghị: có — đây là luật dự án tự đặt.
