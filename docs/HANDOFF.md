# HANDOFF — cho phiên tiếp theo

*2026-08-28, cuối phiên orchestrator kế nhiệm. Đọc file này + `docs/reports/002`
+ `docs/plans/MASTER-EXECUTION-PLAN.md` trước khi đọc bất kỳ dòng code nào.*

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
baseline phía trên — không lặp lại ở đây. **Lane tiếp theo, theo thứ tự**
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
