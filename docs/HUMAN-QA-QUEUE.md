# Hàng đợi câu hỏi cần con người

*Mỗi mục là một thứ agent KHÔNG tự quyết được. Trả lời xong thì đánh dấu và ghi
ngày + phiên nào nhận, rồi chuyển nội dung vào record/HANDOFF — file này là hàng
đợi, không phải nơi lưu quyết định.*

**Quy ước:** `[ ]` chờ · `[x]` đã trả lời · `[!]` đang CHẶN việc

---

## Chặn việc ngay bây giờ

- [ ] **`[!]` Skill nhắn tin cho chủ nhân không tồn tại trên máy này.**
  Chủ nhân bảo dùng "skill Ask Seph (suy ra từ skill ask-LUNA)" để nhắn Telegram.
  Kiểm: `~/.claude/skills/` không có mục nào tên `ask-seph`, `ask-luna`, hay
  chứa `telegram`; `SearchSkills` trả rỗng. Agent **không tự dựng** một đường
  gửi ra dịch vụ ngoài.
  **Cần:** tên/đường dẫn skill đúng, hoặc xác nhận cứ dùng file này thay thế.

## Mua tiêu chuẩn — hai lane đứng yên vì chúng

- [ ] **ISO 2969:2015 / SMPTE ST 202:2010** — bảng dung sai X-curve. Chặn **L5b**.
- [ ] **IEC 60268-16** — STI. Chặn **L4d**.
- [ ] **ISO 18233:2006** — tiêu chuẩn đúng cho quy tắc độ dài capture. Record chỉ
  *nêu tên* và không trích một dòng nào vì chưa ai mua. **Đừng trích khi chưa có
  bản thật** — lane này đã trả học phí một lần với AES-2id (bẫy #16).

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

- [ ] **Merge L4b vào `main`?** Đếm commit bằng `git rev-list --count main..HEAD`, đừng chép số. Branch
  `claude_desk/handoff-continuation-9045fc`. Đo trên cây đã commit:
  `359/359`, 0 warning /W4, `RTA_BUILD_APP=OFF`. Merge phải chạy ở phiên đang
  giữ checkout `main`, sau khi chủ nhân nói "merge" **trong chính phiên đó** —
  tin nhắn giữa hai phiên không phải lời duyệt.
## Quyết định sản phẩm

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
