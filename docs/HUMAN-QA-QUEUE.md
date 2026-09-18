# Hàng đợi câu hỏi cần con người

*Mỗi mục là một thứ agent KHÔNG tự quyết được. Trả lời xong thì đánh dấu và ghi
ngày + phiên nào nhận, rồi chuyển nội dung vào record/HANDOFF — file này là hàng
đợi, không phải nơi lưu quyết định.*

**Quy ước:** `[ ]` chờ · `[x]` đã trả lời · `[!]` đang CHẶN việc

---

## Chặn việc ngay bây giờ

- [x] **ĐÃ MỞ LẠI 2026-09-18.** GitHub Actions chạy bình thường: run
  [35306075307](https://github.com/toanaz-ops/rta-tool/actions/runs/35306075307)
  (ba OS, `main` tại `d071269`) và
  [35311592135](https://github.com/toanaz-ops/rta-tool/actions/runs/35311592135)
  (PR #22, **777/777 cả ba**). Gate CI của `docs/GIT-WORKFLOW.md` **kiểm được
  lại** — và lần chạy đầu tiên sau khi mở đã bắt 4 test đỏ riêng macOS mà 8
  ngày merge-trên-bằng-chứng-local không thấy (chi tiết: `docs/HANDOFF.md`
  mục 2026-09-18, PR #22). Mô tả gốc giữ nguyên bên dưới làm hồ sơ.

  ~~**GitHub Actions bị chặn ở mức tài khoản (2026-09-16).**~~ Mọi run từ sau
  merge PR #5 chết sau 3 s: "The job was not started because recent account
  payments have failed or your spending limit needs to be increased" → Settings
  → Billing & plans. Repo private gói free có 2000 phút/tháng; macOS tính ×10,
  phiên 2026-09-15 chạy ~12 run ba OS. Cho tới khi mở lại, gate CI của
  `docs/GIT-WORKFLOW.md` không kiểm được; PR merge trên bằng chứng local hai
  cấu hình và ghi rõ trong PR body.
- [ ] **Duyệt thay đổi assertion `core/tests/test_weighting.cpp` (PR #5, hoãn
  2026-09-16).** Cũ: `isinf(|H(Nyquist)| dB)`. Mới: zero DC khẳng định trên hệ
  số `b0 − b1 + b2 = 0` (đồng nhất chính xác, Sterbenz) + `> 200 dB` tại
  Nyquist với biên lập luận từ `d²`. Verifier xác nhận lập luận đúng và mutation
  `kDigital·2.0000001` đỏ; nhưng tại đúng một giá trị nó YẾU hơn `isinf`. Đã
  merge nguyên trạng theo lệnh; chủ nhân xem lại khi rảnh.

- [x] **Skill nhắn tin cho chủ nhân → DÙNG CHÍNH FILE NÀY, quyết theo uỷ quyền
  2026-08-30.** Kiểm lại: `~/.claude/skills/` không có `ask-seph`, `ask-luna`
  hay mục nào chứa `telegram`.

  Quyết định: **không dựng đường gửi ra dịch vụ ngoài**, và file này là kênh.
  Lý do không phải "agent không làm được" mà là **agent không nên làm**: một
  đường gửi ra ngoài do agent tự dựng là một kênh chủ nhân không cấu hình, không
  thấy, và không tắt được — cùng loại ranh giới với push lên `origin` ở trên, và
  ở đó ranh giới đã do con người bước qua chứ không do agent suy ra.

  File này lại có tính chất mà một tin nhắn không có: **nó nằm trong repo, đi
  cùng lịch sử, và phiên sau đọc được**. Ba tuần nữa không ai tìm lại được một
  tin Telegram, nhưng `git log` thì còn. Nếu chủ nhân muốn kênh đẩy, đưa tên
  skill đúng và mục này mở lại.

## Mua tiêu chuẩn — hai lane đứng yên vì chúng

*Ba mục dưới đây cần TIỀN, nên agent không quyết được. Nhưng agent ĐÃ quyết
phần quyết được: cái nào thật sự chặn việc, và cái nào không.*

- [ ] **ISO 2969:2015 / SMPTE ST 202:2010** — bảng dung sai X-curve. **THẬT SỰ
  CHẶN L5b**, và khác hẳn ISO 3382-1: hình dạng đường cong là công khai, **bảng
  DUNG SAI thì không**. Một dung sai đoán là một tuyên bố Class sai — không có
  đường vòng bằng nguồn mở, và cố đi vòng chính là bẫy #16.
- [ ] **IEC 60268-16** — STI. **THẬT SỰ CHẶN L4d**, cùng lý do: STI là một thủ
  tục tính có trọng số và bảng cụ thể, không phải một định nghĩa suy ra được.
- [x] **ISO 18233:2006 → KHÔNG chặn gì, quyết 2026-08-30.** Nó là chuẩn đúng cho
  quy tắc độ dài capture, nhưng L4a/L4b **đã dẫn xuất quy tắc đó bằng đo** (chặn
  hai phía: ≥1.5×RT60 từ dưới, truncation gỡ chặn trên). Chuẩn sẽ **xác nhận
  hoặc sửa** một con số đã có, không mở khoá một việc đang đứng. Luật giữ
  nguyên: **nêu tên được, trích thì không, khi chưa cầm bản thật.** Mua khi
  tiện, đừng xếp nó cạnh hai mục trên như thể cùng mức khẩn.

## Từ lane L6b (2026-09-06, record `docs/dsp/2026-09-06-multichannel-l6b.md`)

- [x] **Ngưỡng "capture xấu" theo phần băng tần coherence tin được → GIỮ
  REPORT-ONLY, 2026-09-06.** Chủ nhân chốt: v1 ship với hai refusal cứng
  (overload, gate-8) và phần băng tin-được chỉ *báo*, KHÔNG bịa một ngưỡng số
  không nguồn nào công bố (memory `a-threshold-read-off-a-grid-is-that-grids-floor`).
  Cơ chế mềm kiểu SysTune (down-weight block lệch running average) chuyển sang
  `docs/UPGRADE-BACKLOG.md` cho version sau. Nguồn phân tích: record §8.
- [x] **Remote API — hai quyết định, 2026-09-06.** (a) **Bind localhost mặc
  định** (REW model, record §10); LAN-bind + password là opt-in về sau. (b)
  **Read-only trước** — API phơi `SnapshotSource`, không ghi routing; write
  routing chuyển sang `docs/UPGRADE-BACKLOG.md` (ghi routing lúc show là gần-
  không-đảo-được, cần auth trước). Cả hai không chặn L6b; lane remote khi mở
  đọc hai quyết định này.
- [x] **Đường output generator (auto solo/mute, G20) → GỘP VÀO L7, 2026-09-06.**
  Chủ nhân chốt: không lane riêng. Vì mọi solver L7 cũng cần phát tín hiệu, đường
  output lock-free được thiết kế MỘT LẦN trong trạm 1 của L7 (prerequisite),
  phục vụ cả solver excitation lẫn G20 auto solo/mute. Nó đổi hợp đồng audio
  callback nên trạm 1 phải mở bằng research đường output. Ghi ở master plan hàng
  L7. Nguồn: record §8.

## Từ lane Remote API (2026-09-16)

*Trạm 1+2 đã xong, docs-only: nghiên cứu
`docs/research/2026-09-16-remote-api-station1-research.md`, record
`docs/dsp/2026-09-16-remote-api.md`. **Không câu nào dưới đây chặn trạm 3 viết
impl plan** — chúng quyết bề mặt v1, không quyết kiến trúc.*

*Cập nhật 2026-09-17: **trạm 4 wave 1 (task A–G) đã ship** trên nhánh
`remote-api/wave1-serialise` (chưa merge). `port = 4736` giờ là một hằng trong
`app/src/api/ApiSettings.h`, và `ApiSettings{}` là đối tượng mọi test của wave
này chạy qua. **Câu hỏi cố-định-hay-ephemeral vẫn mở và vẫn rẻ** — đổi sang
ephemeral là sửa một hằng cộng một chỗ hẹn, không phải sửa schema đã ship. Nhưng
nó nên được trả lời **trước wave 2**, vì `ApiServer` là chỗ `bind_to_any_port`
so với `bind_to_port` được quyết.*

- [ ] **Số port mặc định — chọn cố định hay ephemeral?** **Con số đã chốt:
  4736.** Câu hỏi còn lại chỉ là *hình dạng*: **port cố định** thì client dò
  được nhưng có thể đụng port máy khác, còn **ephemeral port ghi ra một file
  cho client đọc** thì không bao giờ đụng nhưng phải có chỗ hẹn. Một câu là
  chốt được. Nguồn: record §8, §14 q.1, và **§15 R14**.

  **Đính chính bản trước của mục này** (nếu chủ nhân đã đọc nó): bản cũ đề xuất
  **4737** với lý do "chưa thấy tool nào chiếm". Câu đó đúng với các tool **âm
  thanh** đã khảo sát, và **phép kiểm duy nhất bắt được lỗi này thì chưa ai
  chạy**: IANA Service Name and Transport Protocol Port Number Registry có
  `ipdr-sp,4737,tcp` và `ipdr-sp,4737,udp` (IPDR/SP, đăng ký 2005-08). **4734,
  4735** (chính là của REW) **và 4736 thì KHÔNG có trong registry.** User Ports
  không độc quyền nên 4737 vẫn chạy được — nhưng một default có tên mà dựa trên
  một phép kiểm chưa ai chạy đúng là thứ phương pháp của dự án này sinh ra để
  chặn. Trạm 3 đổi default sang **4736**; câu hỏi cố-định-hay-ephemeral thì y
  nguyên, chỉ tiền đề được sửa.

- [ ] **`api.allowLanBind` có ship ở v1 dạng setting TẮT sẵn, hay KHÔNG tồn
  tại?** `docs/UPGRADE-BACKLOG.md` hoãn cái *tính năng* LAN bind, nhưng không
  nói cái *setting* có hiện ra hay không. Hai bên đều bảo vệ được: setting có
  mà từ chối thì **thành thật về roadmap**; setting không có thì **không ai bật
  nhầm được**, kể cả theo một post trên forum. Nguồn: record §8, §14 q.2.

- [ ] **`/traces` và `/session` có nằm trong v1 không?** Hai endpoint này cần
  một **publish mới trên message thread** mà hiện không có gì khác trong app
  cần: `TraceLibrary` do `MainComponent` sở hữu, mutable, xoá cả copy lẫn move,
  có `revision()` nhưng **không có atomic publish** — đọc thẳng từ API thread
  là race với mọi `rename`/`setVisible`/`soloOnly`. Đo live thì không cần gì cả
  vì `Snapshot` đã publish sẵn. Nếu chủ nhân chốt "chưa", **v1 còn TÁM endpoint
  và không phải xây publish path nào** — `/status`, `/snapshot`, `/transfer`,
  `/mtw`, `/bands`, `/spectrum`, `/average`, `/positions`. (Bản trước của mục
  này viết "sáu"; **sáu là độ dài danh sách `available` trong `/status`**, mà
  danh sách đó không kể `/status` và `/snapshot`. Xem §15 **R12**.) Nguồn:
  record §5, §6, §14 q.3.

- [ ] **Token: ship setting rỗng, hay sinh token ngay lần bật đầu tiên?** Token
  sinh sẵn để operator copy ra khỏi panel preferences thì an toàn hơn hẳn, và
  cũng là thêm một thứ để mất giữa show. Trên loopback đã có `Host`-header
  allowlist thì phần lợi biên là nhỏ; nhưng **ngày nào có LAN bind thì token là
  bắt buộc** dù chọn đường nào hôm nay. Ghi rõ: token phải là **Bearer header
  hoặc tham số tường minh, TUYỆT ĐỐI không phải cookie** — lý do là **ambient
  authority / CSRF**, KHÔNG phải DNS rebinding. Cookie được browser tự đính vào
  **mọi** request tới `127.0.0.1:<port>`, bất kể trang nào phát ra nó: chỉ cần
  operator mở một trang web bất kỳ giữa show là trang đó đã authenticated với
  listener này, không cần trò DNS nào. Bearer header thì không ambient — chỉ
  caller đã biết secret mới gắn được.

  **Đính chính bản trước của mục này** (nếu chủ nhân đã đọc nó): bản cũ viết
  "rebinding là same-origin thật nên nó **sẽ** mang cookie của origin đó theo"
  — **sai, và ngược với nguồn được trích**. Cookie jar key theo **host name**;
  rebinding chỉ đổi cái name đó *resolve* ra IP nào, chứ không đổi name. Nên
  request bị rebind mang cookie của `attacker.example`, KHÔNG mang cookie app
  đặt cho `127.0.0.1` — đúng như GitHub Security blog 3/4/2025 nói ("cannot
  contain cookies"). Chặn rebinding là việc của **`Host`-header allowlist**,
  không phải của format token. Câu hỏi vẫn y nguyên, chỉ tiền đề được sửa: lợi
  biên của token trên loopback là nhỏ **vì đã có `Host` check**, không phải vì
  cookie hay Bearer gì. Nguồn: record §8, §9, §14 q.4.

- [ ] **Có xin **Smaart API SDK** không?** Free, theo terms công bố thì không
  NDA. Đây là đường duy nhất tới **một mảnh prior art trạm 1 không đọc được**:
  đối thủ encode **coherence** trên dây ra sao. REW không dạy được gì về
  coherence vì REW là swept-sine một kênh, **API của nó không có coherence ở
  đâu cả**. Terms cấm phát tán lại SDK, nên **không bao giờ được trích nội dung
  nó vào docs của repo này** — chỉ dùng để biết. Vòng xin mất vài ngày, nên nếu
  muốn thì xin **trước** trạm 3, đừng đợi tới lúc freeze schema. Nguồn: record
  §14 q.5, ledger UNVERIFIED mục 1.

## Từ lane L7 (2026-09-06, phiên orchestrator trạm 1+2)

- [x] **Hai tiền đề chưa xây → GỘP vào sub-lane phụ thuộc, 2026-09-06.** Chủ nhân
  chốt: G24 (min/excess-phase) xây trong **L7-EQ**; relative-polarity ρ xây trong
  **L7-ALIGN** (dùng để hoà giải hai tín hiệu polarity mâu thuẫn: `DelayFinder`
  `inverted` vs L4a `Polarity`). Không xây lane riêng. Master plan `:120` vẫn liệt
  G24 dưới L4c — closeout cập nhật một dòng. Nguồn: record `docs/dsp/2026-09-06-l7-auto-eq.md` §5, `docs/dsp/2026-09-06-l7-alignment-wizard.md` §7-8.
- [x] **Lane split → Wave 0 shared-foundation trước, 2026-09-06.** MinimumPhase +
  FilterSpec + BiquadDesign/Response xây một lần làm nền, rồi OUT+FIR → DELAY+EQ →
  ALIGN. Chi tiết ở `docs/HANDOFF.md` mục đầu.
- [x] **Solo mặc định khi phát output → CÓ setting, mặc định option 1, 2026-09-06.**
  Sequencer/auto-step: strict single-output solo. Manual toggle: additive. Operator
  đổi được. Trả lời chung cho OUT §13.2 và DELAY §14.2.
- [x] **Order-4 mâu thuẫn (ALIGN §13.1) → settled by independent probe 2026-09-15.**
  Chủ nhân KHÔNG phải trả lời gì. Ô grid độc lập đã chạy:
  [`docs/research/2026-09-15-l7-align-order4-probe.md`](research/2026-09-15-l7-align-order4-probe.md),
  script `tools/probe_align_order4.py`, khoá CI `core/tests/test_align_order4_identity.cpp`
  (ctest 551 → 556, đã làm đỏ một lần rồi xanh lại).
  Kết luận: identity `N·90°` ĐÚNG — chính xác tới máy, analog lẫn digital, BW và
  LR, mọi bậc 1–8, mọi tần số. L4a đọc sai dấu ở bậc 4 vì fixture là **hai box
  band-pass** đấu chéo nhau (sub band-pass dưới + main band-pass trên), không phải
  cặp crossover matched-cutoff: offset hết hằng số (62.9° spread ở bậc 4), đỉnh
  cross-correlation rời khỏi lag 0 sang một lobe ngược dấu, trong khi **giá trị
  tại lag 0 (bậc 4) vẫn dương đúng như identity**. Luật tái hiện được là ρ
  **un-whitened** của L4a quyết định 6b (`2026-08-30-sweep-ir-l4a.md:1181` — số dòng trên `main`) —
  luật này CHƯA build trong core. Correlator ĐANG ship là PHAT
  (`DelayFinder.cpp:34`) và nó **KHÔNG** tái hiện: trên đúng cặp đó nó đọc ngược
  lại — đúng ở bậc 4, sai ở bậc 8. Hai correlator cãi nhau trên một cặp loa không
  đổi → ruling cấm đọc dấu topology từ **bất kỳ** đỉnh correlation nào, whitened
  hay không. Convention KHÔNG phải thủ phạm và
  không thể là: conj hoá làm đổi dấu offset, mà −0° = 0° và −180° = 180°, nên
  không flip nào chạm tới được bậc CHẴN. **Wave 3 (ALIGN) hết chặn.**
- [ ] **Judgement L7 chờ duyệt (default đã chọn, không chặn)**: `G_cap +6dB`/`Q_max`
  (EQ §12.2), N cap (§12.3), NotMinimumPhase→V2 (§12.4), −120dB floor cho `|H|` đo
  (§12.5), plausibility window Locate (DELAY §14.1), tracker on-by-default (§14.3),
  64-output hardware check (OUT §13.1).

  **Cập nhật 2026-09-16 (closeout L7): lane đã BUILT, nên mỗi default giờ có
  MỘT địa chỉ trong code. Chủ nhân đổi ý thì đổi ở đúng chỗ dưới đây, không phải
  đi tìm.** Mục này vẫn `[ ]`: default đã chọn và đã ghi tên, chưa ai duyệt.

  | default | giá trị đã ship | ở đâu |
  |---|---|---|
  | `G_cap` (EQ §12.2) | `gCapDb = 6.0` | `core/include/rta/eq/EqGainSolve.h:28`, tự nhận là "a labelled judgement" |
  | `Q_max` (EQ §12.2) | `qMaxBoost = 10.0`, `qMaxCut = 20.0` | `EqGainSolve.h:29-30`; đây là **fallback** khi không có `roomT60Sec` — có T60 thì `qMaxBoost` buộc theo nó (`EqAllocator.h:39-40`) |
  | N cap (EQ §12.3) | `maxFilters = 6` | `EqGainSolve.h:27` |
  | NotMinimumPhase → V2 (EQ §12.4) | `DipVerdict::NotMinimumPhase` loại candidate khỏi PLACEMENT, không bao giờ "cứ boost thử" | `core/include/rta/eq/DipClassifier.h:17`, `EqAllocator.h:46` |
  | −120 dB floor (EQ §12.5) | `kMinPhaseFloorDb = -120.0f`, cùng số với `TransferSnapshot::kMagnitudeFloorDb` | `core/include/rta/dsp/MinimumPhase.h:14` |
  | plausibility window Locate (DELAY §14.1) | **toàn dải linear** mặc định (`minLag`/`maxLag` = ±`numeric_limits`), và comment nói rõ vì sao "0.5 của đỉnh" KHÔNG được mời làm knob | `core/include/rta/dsp/DelayPolicy.h:67-71` |
  | tracker on-by-default (DELAY §14.3) | `rta::dsp::ResidualDelayTracker` đã ship ở `core/include/rta/dsp/ResidualTracker.h`, **nhưng chưa có toggle UI nào** — nên "on by default" hiện là một câu về thiết kế, chưa phải một hành vi chạy được | `ResidualTracker.{h,cpp}` |
  | 64-output hardware check (OUT §13.1) | `kRequestedOutputChannels` đã nâng 2 → `rta::platform::kMaxChannels`, không over-read | Wave 1 OUT, `881862b..16ec825` |
  | strict solo khi phát (OUT §13.2 / DELAY §14.2) | **ĐÃ TRẢ LỜI** — xem mục "Solo mặc định" ở trên; sequencer/auto-step strict solo, manual toggle additive | `app/src/measure/OutputPolicy.h`, `AlignmentWizard` dùng nó làm default của sequence |

## Tech-debt phát hiện lúc closeout L7 (2026-09-16)

- [x] **ĐÃ ĐÓNG 2026-09-16 — `aafc5f5` (`ci(guard): scan test headers, not just
  test .cpp`), merged qua PR #13 tại `8818ad2`.** Glob `tests/*.h` giờ ở
  `core/tests/check_no_framework_deps.cmake:46` và `tests/*.hpp` ở `:47`; bốn
  file dưới đây đã nằm trong tầm quét. Mô tả gốc giữ nguyên bên dưới làm hồ sơ.

  ~~**`check_no_framework_deps.cmake` KHÔNG quét `tests/*.h` — bốn test header
  chưa bao giờ bị guard nhìn tới.**~~ `core/tests/check_no_framework_deps.cmake:37-43`
  glob `include/*.h`, `include/*.hpp`, `src/*.h`, `src/*.cpp` và `tests/*.cpp`,
  **thiếu `tests/*.h`**. Bốn file ngoài tầm quét:
  `core/tests/CrossoverBandFixture.h`, `core/tests/DelayFilterFixtures.h`,
  `core/tests/support/Golden.h`, `core/tests/guard_fixtures/hex_escape_comment.h`.
  Một `#include <juce…>` trong bất kỳ file nào trong số đó vẫn biên dịch vào
  `rta_core_tests` trong khi guard in `OK` — tức guard đang nói một câu rộng hơn
  điều nó kiểm. **Hôm nay không file nào vi phạm**; lỗ nằm ở phép quét, không
  nằm ở cây, nên đây là tech-debt chứ không phải defect sống.

  ~~**Fix một dòng đang chạy ở nhánh `ci/guard-scan-test-headers`**~~ — **ĐÃ
  LÀM và ĐÃ MERGE.** Điều kiện duyệt ("thêm glob rồi **làm nó ĐỎ một lần**",
  `memory/a-prescribed-mutation-is-not-proof-the-check-catches-it.md`) đã được
  thoả trong PR #13: JUCE include nhét vào `core/tests/support/Golden.h` và
  `app/tests/EqSessionFixture.h`, cả hai guard đỏ với đúng tên file, revert rồi
  xanh lại. Số file quét tăng theo đúng lỗ hổng: `core_has_no_framework_deps`
  157 → **161** (+4 core test header), `measure_has_no_framework_deps`
  64 → **67** (+3 app/tests header), `platform_types_has_no_framework_deps`
  giữ nguyên 8 (`platform/types` không có `tests/`). **Lỗ còn lại, chưa đóng:**
  `platform/tests` nằm ngoài mọi framework guard — ghi ở
  `memory/core-must-not-include-frameworks.md`, hôm nay JUCE-free.

## Tech-debt từ CI macOS fix (2026-09-18, PR #22, nhánh `ci/macos-fixes`)

Hai lỗ được ĐẶT TÊN trong PR #22 thay vì đóng, vì đóng chúng nằm ngoài việc
"làm bốn test đỏ macOS xanh lại". Cả hai đều là loại mục rữa im lặng.

- [ ] **`-ffp-contract=off` gần như KHÔNG CÓ GÌ gác.** Flag ở
  `CMakeLists.txt` (block "The floating-point contract") là thứ giữ cho ba OS
  ra **cùng bit**. Thứ duy nhất phát hiện nếu nó bị xoá là **D7 regression
  lock** (`app/tests/test_api_serialise.cpp`), và D7 chỉ đỏ ở nơi contraction
  thật sự xảy ra — tức **chỉ macos-latest trên CI**. Hệ quả: người phát triển
  trên Windows xoá flag và **không thấy gì**; local ON/OFF đều xanh; ubuntu
  xanh (baseline x86-64 không có FMA để fuse). Flag không được gác trên 2/3
  platform và trên mọi máy local.
  **Cần một câu của chủ nhân**, vì cả hai lối đều có giá: (a) một ctest
  đọc `CMAKE_CXX_FLAGS`/`COMPILE_OPTIONS` và đỏ nếu thiếu flag — rẻ, nhưng là
  guard kiểm *chuỗi ký tự* chứ không kiểm *hành vi*, đúng loại thứ
  `memory/a-prescribed-mutation-is-not-proof-the-check-catches-it.md` cảnh báo;
  (b) một test tính `10.0*std::log10(0.5) + 3.0102999566398120` và đòi bitwise
  0.0 — kiểm hành vi thật, nhưng **chỉ đỏ trên arm64**, nên vẫn là guard một
  platform, chỉ là rẻ hơn D7. Không tự chọn.

- [ ] **JUCE chưa bao giờ biên dịch dưới `-ffp-contract=off`.** Flag là
  `add_compile_options` ở root nên áp cho MỌI target, JUCE gồm cả nguồn C của
  nó. Nhưng: CI chỉ chạy `RTA_BUILD_APP=OFF`, và cấu hình ON duy nhất được đo
  là trên **MSVC**, nơi `if(NOT MSVC)` khiến flag không tồn tại. Nghĩa là
  **không máy nào từng biên dịch JUCE với flag này** — clang/gcc + JUCE + ON là
  tổ hợp zero lần chạy. Rủi ro thấp (`-ffp-contract` là flag chuẩn, JUCE không
  đòi FMA) nhưng **chưa đo**, và CLAUDE.md không cho gọi cái chưa đo là "ổn".
  Rẻ nhất để đóng: một job CI `RTA_BUILD_APP=ON` trên ubuntu, hoặc một lần
  build ON tay trên macOS/Linux. Ghi ở đây vì nó là **quyết định về phạm vi
  CI**, không phải một dòng code.

## Từ lane L7-ALIGN (2026-09-16, record `docs/dsp/2026-09-06-l7-alignment-wizard.md`)

*Lane đã BUILT và merge (PR #8 `02bd02a`, PR #9 `6d9a53d`); report
`docs/reports/007-solvers.md`. Ba mục dưới là thứ lane CỐ Ý không tự quyết.*

- [ ] **Một cặp sub/main THẬT (record §13.2).** Mọi check ở §10 là synthetic.
  Hai câu chỉ người cầm rack và micro trả lời được: trên một capture phòng
  THẬT, `R` có còn đủ cao để đọc được intercept không; và ±1 octave có phải cửa
  sổ đúng cho một cặp 24 dB/oct thật không. Cùng hạng với "EDT floor" và "MTW
  fill" đang mở.
- [ ] **Nhánh "unknown" của câu hỏi (c) (record §13.3).** Khi operator KHÔNG
  biết processor có đảo hay không, bề mặt G18 nay hiện **hai** đường candidate
  (`targetAmbiguous()` / `alternativeTargetRadians()`) và không có gì chọn giữa
  chúng. Record đọc rằng "hiện cả hai" KHÔNG phải là suy ra topology — operator
  vẫn là người chọn tin đường nào và wizard nói rõ thế. Nhưng đây là chỗ mép
  của ruling "không bao giờ suy topology" gần nhất, nên **chủ nhân nên nhìn
  `shots/preview-phase.png` trước khi nó đi tiếp** (lệnh dựng ở
  `docs/HANDOFF.md` mục "L7 CLOSED OUT", phần 4).
- [ ] **Xác nhận câu chữ của ruling ρ (record §13.4).** Fold-in ρ vào L7-ALIGN
  được **relay bằng lời**, chưa ai ghi thành câu. Closeout viết câu này; chủ
  nhân xác nhận nó nói đúng điều đã định: **ρ ship KHÔNG ngưỡng và KHÔNG
  verdict.** Hai grid độc lập đã chạy và **không đồng ý** — grid A có 2 ô sai
  dấu trên 43200 (sàn ρ > 0.0640) nhưng một ô ĐÚNG dấu nằm ở 0.0520, tức hai
  phân bố CHỒNG nhau; grid B không có ô sai dấu nào trên 2000 nên không đặt
  được sàn nào cả. Đúng thứ
  `memory/a-threshold-read-off-a-grid-is-that-grids-floor.md` cảnh báo. Nên
  `rta::ir::relativePolarity` trả một con số bounded, test E6
  (`core/tests/test_relative_polarity_guard.cpp`) giữ sự VẮNG MẶT đó bằng cấu
  trúc, và ρ chỉ là một **nhân chứng** hiện bên cạnh `findPolarity` kèm lý do —
  không bao giờ là phán quyết, và không bao giờ là dấu topology.

## Chờ một câu của chủ nhân, không phải một quyết định khó

- [x] **`[!]` Merge lane L4a vào `main` — ĐÃ MERGE 2026-08-30**, commit
  `61daf1a`, chạy ở phiên REVIEW (checkout `main`) sau khi chủ nhân nói "merge"
  trong chính phiên đó. Đo lại trên cây ĐÃ MERGE, build dir độc lập của phiên
  review (`build-verify/` clean rebuild, `build-verify-app/`):
  `345/345` (OFF, 0 warning) và `385/385` (ON). Hai cấu hình phủ hai tập target
  khác nhau, không cộng gộp. Ghi chú quy trình giữ lại cho lane sau: số commit
  đo bằng `git rev-list --count`, đừng đọc con số chép sẵn — bản đầu của mục này
  ghi "8 commit", con số thật lúc merge là 10.

## Từ lane L4b (2026-08-30, phiên EP06)

- [x] **Merge L4b vào `main` → ĐÃ MERGE**, `e77e0e1`, 2026-08-30, chạy ở phiên
  REVIEW sau khi chủ nhân nói "merge" trong chính phiên đó. Đo độc lập sau
  merge trên build dir phiên review chưa từng bị phiên build chạm:
  **359/359** (OFF) và **399/399** (ON — lần đo đầu tiên của cấu hình này với
  L4b; 385→399 khớp đúng chênh lệch 14 case của 345→359). Sau đó đã push:
  `29b464e..7122070`.

  **Đợt EDT ensemble sau đó → ĐÃ MERGE 2026-09-05**, merge `--no-ff` tại
  `a2cbd02`, chủ nhân ra lệnh "merge hết code mới về master" trong chính phiên
  đó. Nó mang `rta::ir::decayTimesAcross` (`DecayEnsemble`) + bốn quyết định
  uỷ quyền, từ `claude_desk/handoff-continuation-9045fc`. Chi tiết đo ở mục đầu
  HANDOFF. `main` chưa push (`rev-list --count origin/main..main` > 0) — push là
  lệnh riêng của con người, phiên này không tự suy ra.
- [x] **Tên sản phẩm chính thức → `AZ Soundtech RTA`.** Chủ nhân chốt
  2026-08-30, phiên EP06 DOER, nguyên văn: *"Tên sản phẩm chính thức =
  AZ Soundtech RTA"*. Target CMake vẫn là `rtatool` — đó là định danh
  build, không phải tên sản phẩm, và đổi nó là một lượt refactor riêng
  không ai yêu cầu.
- [x] **Push lên `origin` → ĐÃ PUSH 2026-08-30**, `29b464e..7122070`, chạy ở
  phiên REVIEW sau khi chủ nhân ra lệnh **trong chính phiên đó**. Đo sau khi
  push: `git rev-list --count origin/main..main` → **0**.

  Hai ghi chú quy trình đáng giữ, vì cả hai là luật của phiên này được đem ra
  dùng thật:
  1. Chủ nhân gõ **"merge master"** — ngắn hơn câu hỏi và không khớp hẳn từ
     vựng. Phiên review **hỏi lại trước khi chạy**, đúng luật rút ra từ vụ `b:`
     sáng cùng ngày. Luật ra đời từ một lần ghi sai, và lần dùng đầu tiên của
     nó là để tránh một hành động **không đảo được**.
  2. Phiên DOER trước đó đã **cố ý loại push khỏi phạm vi uỷ quyền "agent tự
     quyết các câu hỏi còn lại"**, vì push rời khỏi máy và không đảo sạch được.
     Việc chủ nhân sau đó tự ra lệnh push **không làm cho việc loại trừ đó sai**
     — nó cho thấy đúng con đường: ranh giới đó do con người bước qua, không
     phải do agent tự suy ra là mình được phép.

## Uỷ quyền: agent tự quyết sau khi tham khảo nguồn ngoài (2026-08-30)

*Chủ nhân: "Các câu hỏi còn lại, agent tham khảo các sản phẩm khác để có thêm 1
nguồn thông tin rồi cho phép tự quyết." Bốn mục dưới đây quyết theo uỷ quyền đó.
Mỗi mục ghi **nguồn ngoài đã tra**, kể cả khi nguồn đó nói ngược lại — một
khảo sát chỉ trích những gì ủng hộ mình thì không phải khảo sát.*

- [x] **Hai hằng số gate polarity `100 Hz` / `8 kHz` → GIỮ NGUYÊN, nhãn
  *observed*.** Chúng đo được 0 câu sai qua `butter`/`cheby1`/`ellip`/`bessel`
  bậc 2–16; giá đã biết và đã ghi (`bessel` bậc 6/8 bị từ chối oan, nhất quán,
  có số trong refusal reason).

  **Nguồn ngoài, và nó chỉ hướng NGƯỢC LẠI:** kỹ thuật *band-limited polarity
  detection* (US 2006/0062399) làm điều ngược hẳn — **low-pass bậc hai tại
  400 Hz** để ép đáp ứng vào vùng cùng pha với đấu dây, thay vì đòi một băng
  rộng. Ghi lại vì nó thật, và **không đổi quyết định**: nó giải một bài khác
  (ép tín hiệu vào vùng dễ đọc) trong khi cổng của ta trả lời *"phép đo này có
  đủ tư cách kết luận không"*. Nếu lane sau muốn thử hướng low-pass, đây là
  con trỏ — nhưng nó là một tính năng khác, không phải một hằng số khác.

- [x] **Chuỗi chữ cho `Unknown` → chốt câu chữ, kèm hướng.** Nguyên tắc: mỗi
  refusal phải **gửi người vận hành đi đâu đó**, không chỉ nói "không biết".
  - `BandTooLow` (đo được low edge > 100 Hz — một horn): *"Băng đo không xuống
    đủ thấp để xác định cực tính tuyệt đối (low edge X Hz). So sánh tương đối
    với một phép đo khác của chính hệ này."*
  - `BandTooHigh` (high edge < 8 kHz — một sub): *"Băng đo không lên đủ cao
    (high edge X Hz). Đo lại full-range, hoặc so tương đối trước/sau thay đổi."*
  - `SweepBandInsufficient`: câu chữ phải **đổ lỗi cho sweep, không cho loa** —
    *"Sweep này chỉ đáng tin từ X đến Y Hz, không đủ để kết luận."*
  Mỗi chuỗi **phải mang con số đo được**; một refusal không có số là một refusal
  người vận hành không kiểm được. L4c thi công, không thiết kế lại.

- [x] **Cap 3 pane → GIỮ.** Record L5c §6 đã chốt 1–3 pane dọc và **tự xếp nó
  vào loại "taste"** (§423), tức đã biết nó là lựa chọn chứ không phải suy ra.
  Nguồn ngoài: Smaart cho đổi loại đồ thị trong mỗi pane qua dropdown nhưng
  không tài liệu nào nêu một cap khuyến nghị; SysTune cố định **hai** pane —
  và §299 đã ghi rằng hai pane không đủ chỗ cho RTA + transfer + một cái nữa.
  **Không có bằng chứng ngoài để đổi, và có lý do nội tại để giữ.** Đổi cap là
  đổi cả model workspace đã persist xuống đĩa; không làm vì không có lý do.

- [x] **Unwrap có hiện trace pha đã lưu không → CÓ, và unwrap LẠI tại chỗ vẽ,
  không tin unwrap đã lưu.** L5c §5 đã unwrap dọc trục bin lúc dựng cache rồi
  wrap lại lúc vẽ — nên một stored trace **đã mang đủ thông tin** để unwrap lại
  dưới đúng cài đặt hiện hành.

  Lý do không tái dùng unwrap cũ: unwrap là **thao tác hiển thị** (§4), và một
  trace lưu hôm qua đã unwrap dưới coherence gate và độ phân giải của hôm qua.
  Vẽ nó cạnh một trace live unwrap hôm nay là đặt hai quyết định khác nhau lên
  một trục và mời người vận hành đọc hiệu số của chúng như hiệu số của hệ
  thống. Unwrap lại cả hai bằng cùng một luật thì hiệu số mới là của hệ thống.

- [x] **Công bố giới hạn multi-way ra tài liệu người dùng → CÓ, CÔNG BỐ.**
  Một thùng 2-way có driver đảo cực theo thiết kế là ill-posed cho **mọi**
  absolute polarity checker.

  **Nguồn ngoài xác nhận nó là giới hạn thật, không phải khiếm khuyết của ta:**
  văn liệu và diễn đàn kỹ thuật ghi *"cực tính đôi khi rất mơ hồ, đặc biệt khi
  dùng loa multi-way"*, rằng cực tính phụ thuộc độ dốc crossover, và rằng
  impulse response của một loa một-đường đọc rõ dấu trong khi cấu hình nhiều
  driver thì không.

  **Quyết định công bố, và lý do là một lý do sản phẩm chứ không phải kỹ
  thuật:** đối thủ ship tính năng này và không nói ra giới hạn. Nói ra biến một
  giới hạn chung của ngành thành một điểm phân biệt — công cụ này **từ chối
  thay vì đoán**, và tài liệu giải thích khi nào nó từ chối. Người vận hành mất
  niềm tin vì một con số sai tự tin, không vì một lời từ chối có lý do.

---

## Từ lane L6a (2026-09-16, record `docs/dsp/2026-09-16-spl-pro-l6a.md`)

*Mười câu. Q1, Q2 và Q8 định phạm vi trạm 3; bảy câu còn lại không chặn việc.*

> **Cập nhật 2026-09-17 — trạm 3 đã viết xong plan**
> (`docs/plans/2026-09-17-L6a-spl-pro-impl-plan.md`). Plan **không chờ** ba câu
> `[!]`: mỗi câu được lấy **đúng đề xuất của record** làm một *scope default*,
> có ghi tên, ghi § và ghi **giá nếu chủ nhân lật**. Ba ô dưới vẫn để `[ ]` —
> một default không phải một câu trả lời. Bảy câu còn lại cũng đã được lấy
> default theo đề xuất của record (bảng thứ hai đầu plan) để các wave dựng được.
>
> **Bản 2 (2026-09-18):** vòng verify đối kháng trên PR #15 cho **SOUND-WITH-FIXES,
> station 4 GO**, và **mười bốn lỗi** đã được vá vào plan. Ba default dưới
> **không đổi**. Thêm **Q11** ở cuối mục này — một câu mới mà vòng verify làm
> lộ ra, có default và có năm fixture, nhưng đáng một câu của chủ nhân vì nó
> đổi **mọi con số Leq công bố trong một show to**.

- [ ] **`[!]` Q1 — seam 3.0103 dB: readout SPL theo convention nào?**
  `app/src/measure/Levels.h` định nghĩa dB sao cho một **sine full-scale đọc
  đúng `0.0 dBFS`** (cộng `kFullScaleSineOffsetDb = 3.0102999566398120`), và
  comment của chính nó gọi đó là "the ONE definition of dB the rest of the app
  reads through". `core/include/rta/meter/Leq.h` định nghĩa Leq theo IEC —
  `10*log10(mean(p^2))`, mean-square referenced — nên cùng sine đó đọc
  **`−3.0103`**. Cả hai đều đúng, chúng trả lời hai câu hỏi khác nhau (một RMS
  level và một peak-equivalent reference).

  Nhưng đặt broadband Leq cạnh RTA bands mà không quy đổi thì một tone 1 kHz
  đọc **lệch 3.0103 dB giữa hai readout**, và người vận hành đọc ra đó là lỗi
  thiết bị. Sau khi calibrate thì không sao — offset nuốt hằng số — chỗ đau
  đúng là **dBFS chưa calibrate**, tức là cái operator nhìn thấy trước khi họ
  calibrate.

  **Đề xuất: quy đổi đúng một lần tại meter seam, ghi nhãn cả hai, và KHÔNG
  động vào bands.** Chuyển bands sang mean-square sẽ đổi **mọi con số dBFS đang
  có trên màn hình và trong mọi trace đã lưu** — đó là một quyết định phá vỡ
  dữ liệu cũ, không phải một lựa chọn thẩm mỹ. Cần chủ nhân xác nhận.

  > **Default đã lấy — plan §"Defaults this plan TAKES", thi hành ở Wave 0 task
  > W0-E; lật nếu sai.** Plan lấy đúng đề xuất trên: quy đổi một lần tại meter
  > seam, ghi nhãn cả hai, **không động vào bands**. W0-E là test closed-form:
  > cùng một sine full-scale đọc **cùng một số dB** qua cả hai đường sau khi
  > offset được áp (`levelDbFs(0.5) == 0.0` và, với
  > `referenceOffsetDb = kFullScaleSineOffsetDb`, metric SPL cũng đọc `0.0`).
  > **Giá nếu lật:** `kFullScaleSineOffsetDb` (`Levels.h:22`) về `0.0`; mọi số
  > dBFS trên màn và trong **mọi `Trace` đã lưu** dịch 3.0103 dB; golden và tám
  > PNG của `rtatool_snapshot` đều đổi; và `kSchemaVersion` (`SessionCodec.h:30`)
  > phải lên **4** với một khoá `levelConvention` — nếu không, session cũ đọc
  > bằng build mới sai 3 dB mà không ai biết. Tức là **một schema bump cộng một
  > lần regenerate golden, không phải sửa một hằng số**. Đây là câu đáng trả lời
  > **trước khi trạm 4 commit W0-E**.

- [ ] **`[!]` Q2 — dựng calibration flow ngay trong L6a? (mở rộng scope)**
  Hàng L6a của master plan không liệt kê nó. Nhưng hiện tại repo có
  `Trace::calibrationOffsetDb` mà **không có gì set nó một cách trung thực**:
  không có routine nào đo calibrator rồi tính offset. Không có flow thì log
  SPL là log dBFS, và report không thể mang cặp đọc đầu/cuối mà **ISO 1996-2
  cl. 5.2 đòi** (calibrator class 1 IEC 60942, kiểm ở đầu và cuối mỗi phép đo,
  lệch hai lần liên tiếp **≤ 0,5 dB**, vượt thì **huỷ toàn bộ kết quả kể từ lần
  kiểm đạt trước đó**). Đây là số published duy nhất tìm được cho drift, và nó
  đến từ một clause normative.

  Đáng nói thêm, vì nó là điểm khác biệt chứ không phải parity: **Smaart SPL và
  10EaZy đều KHÔNG in cặp calibration trước/sau vào report** (danh sách field
  của Smaart là đầy đủ và không có mục calibration nào). Cirrus AuditStore thì
  có lưu. **Đề xuất: dựng.** Cần chủ nhân duyệt vì nó là scope addition.

  > **Default đã lấy — plan §"Defaults this plan TAKES", thi hành ở **Wave 3**;
  > lật nếu sai.** Plan lấy đúng đề xuất trên: **dựng**, và dựng thành một wave
  > riêng đứng một mình.
  > **Giá nếu lật: ba task biến mất, không task nào phải viết lại.** Wave 0–2
  > nhận calibration offset **như dữ liệu** (`SplConfig::referenceOffsetDb`)
  > chính là để phép cắt này là một phép xoá. Mất gì:
  > `flags.calibrationInvalid` không bao giờ được set, và report mục 3 in
  > *"calibration check not performed"* thay cho cặp đọc đầu/cuối cùng clause
  > ISO 1996-2 cl. 5.2. Một dòng `meter::calibrationOffsetDb` vẫn ở lại `core/`
  > vì test seam W0-E dùng nó.

- [ ] **Q3 — ship bao nhiêu Ln, và có cho người dùng đặt phần trăm không?**
  Larson Davis 831/LxT phơi **sáu** slot với phần trăm settable
  (`NUM_LNS = 6`, `m_fLnPercents[]`); report của Smaart in **L10/L50/L90**;
  NoiseCapture chỉ implement đúng ba cái đó. **Đề xuất: sáu slot, default
  L1/L5/L10/L50/L90/L95.**

- [ ] **Q4 — NIOSH ship theo convention nào? (98-126 mâu thuẫn với chính nó,
  giữa hai bảng trong CÙNG một chương)** **Bảng 1-1** (trang 2) và công thức in
  ngay trên nó ở §1.1.1 (trang 1) — `T (min) = 480 / 2^((L−85)/3)`, kèm đúng
  chữ *"where 3 = the exchange rate"* — cần `q = 3/log10(2) = 9.9657843`. Còn
  **Bảng 1-2** (trang 3) và footnote in dưới nó, `*TWA = 10 × Log(D/100) + 85`,
  cần `q = 10` chẵn. Dòng cuối Bảng 1-2 là bằng chứng không cần diễn giải:
  **32,500,000 % → 140.1 dBA**, và `10·log10(325000) + 85 = 140.1188`.

  Lệch bao nhiêu là một closed form, không phải một con số:
  `D(q=10)/D(q=3/log10 2) = 10^(−0.00034333·ΔL)`, tức `q = 10` đọc **thấp**
  1.1788 % ở 100 dBA, 2.3437 % ở 115, 3.4949 % ở 130, và **4.2549 % ở
  140 dBA** — đúng đỉnh dải đo 80–140 dBA mà chính §1.3.3 của NIOSH quy định.

  > **Sửa bản trước, ghi lại để khỏi ai tin lại con số cũ.** Bản đầu của record
  > viết "bảng chọi công thức, lệch tới 1.18 %". Cả hai vế đều sai, cùng một
  > gốc: nó lấy **bảng tóm tắt sáu dòng của CDC bulletin 2016** (8 h/85 …
  > 15 min/100) và tưởng đó là bảng của NIOSH. 1.18 % chính là giá trị ở
  > 100 dBA — endpoint của bảng tóm tắt, thấp hơn endpoint thật 40 dB. Bảng
  > thật là **Table 1-1, 51 dòng, bước 1 dB, 80 dBA → `130–140 <1 sec`**. Lý do
  > trạm 1 không đọc bản gốc ("chỉ có bản scan ảnh") cũng đã bị bác: bản
  > born-digital có text layer nằm ở
  > `web.archive.org/web/2020/https://www.cdc.gov/niosh/docs/98-126/pdfs/98-126.pdf`,
  > đã đọc và trích 2026-09-17.

  **Đề xuất: ship giá trị tái tạo được Bảng 1-1** (`q = 9.9657843`), vì đó là
  bảng một inspector cầm đọc *và* nó khớp với công thức in cùng trang; đồng
  thời in TWA theo đúng công thức NIOSH và ghi rõ hai bảng hàm ý hai hằng số
  exchange khác nhau.

  **Hỏi luôn một câu đi kèm, vì quyết cùng lúc thì rẻ:** 98-126 có **ba lỗi số
  học** (record §7) — Bảng 1-1 dòng 99 dBA in `18 min 59 sec` (công thức cho
  18 min 53.93 sec); Bảng 1-2 dòng `50,000 % → 102.0` (đúng ra 111.99, lỗi đảo
  chữ số, hai dòng kề là 111.5 và 112.8) và `26,000,000 % → 139.0` (139.15).
  App tái tạo **bảng đã in kể cả lỗi**, hay tái tạo **công thức**? Hai sản phẩm
  khác nhau. (Đề xuất: theo công thức; test chặn từng dòng bảng bằng cận
  `100·r/T_exact` suy ra từ độ phân giải in — cận đó đúng ở cả 50 dòng **trừ
  đúng dòng 99 dBA**, nên nó vừa dung sai làm tròn vừa **bắt được lỗi in**.)

  **OSHA KHÔNG đối xứng như bản trước viết.** Bản trước bảo OSHA "tự nhất
  quán, chỉ làm tròn" còn NIOSH "sai về bản chất" — không phải. Dose của OSHA
  tính theo **Table G-16a** (Appendix A I(1)(i), bắt buộc), không phải
  Table G-16; và G-16a **cũng làm tròn**: ở 81 dBA giá trị đúng là
  **27.857618 h**, bảng in **27.9**. Cả hai cơ quan đều in một công thức đúng
  và một bảng làm tròn theo nó.

- [ ] **Q5 — alarm window là sliding hay consecutive-fixed?** Cả hai đều suy ra
  được từ block của §3. Sliding **nghiêm ngặt hơn** (max của nó ≥ max của
  fixed). Thực tế thị trường dùng một quantity dài rồi so, chứ không so
  instantaneous: VLAREM đăng ký `LAeq,60min` và **deem** là đạt nếu
  `LAeq,15min ≤ 102 dB(A)`; Pop Code cl. 4.12 theo dõi `LAeq` 1 phút để cảnh
  báo sớm cho limit 15 phút. **Đề xuất: sliding, hiện cả regulated window lẫn
  proxy window.**

- [ ] **Q6 — log có cần tamper-evident không?** 10EaZy ghi **checksum** xuống
  cuối file log và ship riêng một app **Log File Validator**; Cirrus giữ một
  bản secure song song. Record §9 mục 9 đề xuất **hash** in trong report.
  **Chữ ký số thì cần một khoá, và một khoá cần một câu chuyện về nơi nó
  sống** — ngoài scope cho tới khi chủ nhân hỏi tới.

- [ ] **Q7 — retention và rotation.** VLAREM đòi dữ liệu đã đăng ký giữ **ít
  nhất một tháng**; tóm tắt cấp bang của Thuỵ Sĩ nói **sáu tháng** (nguồn liên
  bang 502, nên UNVERIFIED). Default log span là bao nhiêu (§4), segment size
  bao nhiêu, và app có bao giờ tự xoá không?

- [ ] **`[!]` Q8 — web viewer có ship trong L6a không?** Nó là thứ cuối cùng
  trong build order (§9), phụ thuộc PR #11 land, và phụ thuộc một phép thử
  **chưa ai chạy**: một trang phục vụ *từ* `127.0.0.1` fetch `127.0.0.1` có
  được miễn prompt **Local Network Access** của Chrome không. Suy ra được từ mô
  hình same-address-space của LNA nhưng **không tìm thấy phát biểu nguyên văn**
  (ledger UNVERIFIED của chính PR #11, mục 7). Một buổi chiều thử với Chrome
  142+ là xong. Đây là thứ **sạch nhất để cắt** nếu lane quá to.

  > **Default đã lấy — plan §"Defaults this plan TAKES", thi hành ở **Wave 4a /
  > 4b**; lật nếu sai.** Plan lấy đúng build order của record §9: viewer
  > **ship, nhưng cuối cùng và có gate**. Và nó **tách làm đôi**: **4a =
  > report** (payload đóng băng, không socket, **không bị gate**), **4b =
  > viewer** (cùng renderer, payload fetch qua L-API). Lý do tách: thứ một
  > venue thực sự cầm đọc là **report**, không phải cái socket — cắt cái sau
  > không được phép cắt luôn cái trước.
  > **Giá nếu lật: Wave 4b biến mất nguyên, cộng đúng một câu** — ràng buộc
  > rounding §12 constraint 2 được ghi là **untested for the viewer** và câu đó
  > vào report, đúng như §9 đã cho phép sẵn.
  > **Hai gate, cả hai nằm ngoài tay lane này:** (1) L-API trạm 4 phải land
  > `ApiServer` + route static asset; (2) **phép thử Chrome LNA — việc chỉ chủ
  > nhân làm được.** Thủ tục từng bước và **bốn thứ cần báo lại** (phiên bản
  > Chrome; có prompt hay không và nó viết đúng chữ gì; status của
  > `GET /api/v1/spl` trong tab Network; dòng console nào nhắc local/private
  > network) nằm ở **Wave 4b Gate 2** trong plan.

- [ ] **Q9 — có mua ISO 1996-2:2017 không?** Clause **13 "Information to be
  recorded and reported"** đúng là clause báo cáo cho một logging meter, và
  **thân bài bị tường phí**; hai clause còn lại lane này sẽ xây theo là
  **10.2.2 (L_N,T)** và **10.3 (incomplete or corrupted data)**. Khác ISO 2969
  và IEC 60268-16: nó **KHÔNG chặn** lane — danh sách nội dung report ở §9 dựng
  từ market practice và đã ghi rõ là *không* claim conformant với clause 13 —
  nhưng mua thì biến một danh sách lắp ghép thành một danh sách có citation.
  Đừng xếp nó cạnh hai mục "mua tiêu chuẩn" ở trên như thể cùng mức khẩn.

- [ ] **Q10 — ai sửa số bảng sai trong guard?** Record weighting đã được đính
  chính trong chính PR này (Table 2 → **Table 3**). Nhưng
  `core/tests/check_no_conformance_claim.cmake:18,61` còn mang số sai ở comment
  và ở message lỗi — **đó là code**, nên PR docs-only này không đụng. Ai nhặt?

  > **Đã có người nhặt (2026-09-17):** plan trạm 3 của L6a nhận nó làm
  > **Task G2**. Vẫn là code, nên nó thuộc **trạm 4**, không đụng ở PR
  > docs-only này. Để ngỏ tới khi trạm 4 chạy.

- [ ] **Q11 — cờ nào loại một block khỏi `combineBlocks`?** (vòng verify PR #15,
  defect 14). Record §2 liệt kê `overload | underRange | dropped |
  calibrationInvalid` và **không nói cái nào loại**. Một quyết định, và nó đổi
  **mọi con số Leq công bố trong một show to**.

  **Default đã lấy trong plan (Wave 0, task W0-A A9–A13): chỉ
  `CalibrationInvalid` loại.** Lý do, từng cờ một:
  - `CalibrationInvalid` **LOẠI** — dB của nó tham chiếu một offset mà chính
    **ISO 1996-2 cl. 5.2** nói phải huỷ. Đây là cờ duy nhất có một clause
    normative đứng sau.
  - `Overload` **KHÔNG loại** — sóng đã clip mang **ÍT** năng lượng hơn tín
    hiệu làm nó clip, nên bỏ block đó là **xoá khoảnh khắc to nhất của show**
    khỏi con số pháp lý, lệch đúng hướng có lợi cho người vận hành. Cùng một
    kiểu sai mà §2 đã bác khi nó từ chối lấy mẫu tức thời.
  - `UnderRange` **KHÔNG loại** — loại đáy thì Leq lệch **lên**.
  - `Dropped` / `Gap` **KHÔNG loại** — năng lượng chúng mang là thật trên số
    mẫu thực sự có, và `blockSamples` là mẫu số trung thực cho nó. Thứ mất là
    **thời gian**, và đó là việc của cột `droppedSamples`.

  Cả năm cờ đều được **đếm và in ra** dù có loại hay không (§9 mục 8).

  **Vì sao vẫn hỏi:** hai tài liệu chi phối — **IEC 61672-1 cl. 3.28** (định
  nghĩa validity) và **ISO 1996-2 cl. 10.3** ("incomplete or corrupted data")
  — đều **tường phí và chưa đọc**, nên dự án **không claim** cơ sở tiêu chuẩn
  cho bất kỳ hướng nào và nói thẳng điều đó. Một câu của chủ nhân là đủ.

---

## Từ phiên EP06 (2026-08-30)

- [ ] **`[!]` ISO 3382-1 — mua, hay dựng từ nguồn mở? CHỦ NHÂN HOÃN CÓ CHỦ Ý.**
  Định nghĩa T20/T30/EDT/C50/C80/D50 nằm trong đó. Hỏi 2026-08-30, chủ nhân trả
  lời **"ISO thì cần bàn thêm sau"** — đây là một *hoãn tường minh*, không phải
  câu chưa được hỏi và không phải câu đã trả lời. Đừng đóng nó bằng suy luận.

  **Luật tạm thời L4b chạy dưới, trong lúc chờ:** dựng từ nguồn mở (Schroeder
  1965, Lundeby 1995, python-acoustics 0.2.6, pyrato, REW), provenance ghi là
  ***literature***, và **docs không được viết "theo ISO 3382-1" ở bất kỳ đâu**.

  Luật này chọn được vì nó **đảo được theo cả hai hướng**: mua chuẩn về thì chỉ
  thêm citation clause vào chỗ đã có số; quyết định không mua thì không phải gỡ
  gì cả. Một lane viết "theo ISO 3382-1" trước khi cầm chuẩn thì hướng ngược lại
  mới là hướng đắt — đó là bẫy #16 (AES-2id) ở dạng khác.


## Đã trả lời — phiên EP06 (2026-08-30)

- [x] **EDT ship thế nào → theo kết quả phân tích của hai phiên.** Chủ nhân
  chốt 2026-08-30, nguyên văn: *"EDT ship theo két quả agent phân tích"*.
  Nghĩa là: **không cổng**, và độ tin cậy đến từ **spread giữa nhiều capture**,
  không phải từ một điểm số tính trên một lần đọc.

  Cơ sở đo được, ghi ở record §4f và trong header `DecayEnsemble.h`: hai ứng
  viên điểm-số-một-lần-đọc **đều bị bác bằng đo** — rms residual của chính phép
  fit (REW ship nó dưới tên *model fit error*) và curvature `100·|T30/T20−1|`
  cho **|tương quan| 0.02–0.16** với sai số thật trên 300 realisation. Một điểm
  số không tương quan với sai số thì **tệ hơn không có**: nó trấn an thay vì
  báo tin. Thứ có tác dụng là thêm capture — IQR giảm theo `1/√N`, đo được
  118.2 / 86.2 / 62.8 / 41.2 / 27.9 % ở N = 1/2/4/8/16.

  Đã thi công: `rta::ir::decayTimesAcross()` trả median + IQR + số capture, và
  **từ chối gọi hai capture là một spread**.

## Đã trả lời

- [x] **G21 tầng 1 — ship absolute polarity hay không (B1 vs B2).**
  → **B1**: ship, kèm gate, docs nói thật envelope. 2026-08-30, trong phiên thi
  công. Lý do quyết: mọi đối thủ ship G21 **không gate**, nên gate nào cũng giảm
  nói dối so với thị trường.
- [x] **Biến gác: margin hay hộp hai band edge.**
  → **Hộp hai band edge.** 2026-08-30. Margin từ chối 74–88% box tốt.
- [x] **G21 tầng 3 — workflow "đo main → đo sub tương đối".**
  → **Duyệt sửa**: chuyển sang G17 như câu hỏi PHA. 2026-08-30.
- [x] **Ngôn ngữ báo cáo.** → Technical terms giữ nguyên tiếng Anh, không dịch
  sang tiếng Việt. 2026-08-30.
- [x] **Lane kế sau L4a.** → **L4b** (ETC, Schroeder, Lundeby, EDT/T20/T30,
  C50/C80/D50, toàn bộ trong `core/`). Chủ nhân chốt 2026-08-30, phiên EP06,
  trả lời trực tiếp trong phiên thi công. `MASTER-EXECUTION-PLAN.md` mục 4 của
  "Suggested opening order" đã bỏ nhãn pending theo đó.
