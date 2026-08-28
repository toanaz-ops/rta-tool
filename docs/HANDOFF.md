# HANDOFF — cho phiên tiếp theo

*2026-08-28, cuối phiên orchestrator kế nhiệm. Đọc file này + `docs/reports/002`
+ `docs/plans/MASTER-EXECUTION-PLAN.md` trước khi đọc bất kỳ dòng code nào.*

## Baseline đo được (dán từ lệnh, không từ trí nhớ)

Đo lại cuối phiên, trên đúng cây đã commit:

```
ctest --test-dir build -C Release        -> 226/226, 0 fail      (đầu ngày: 149/149)
clean rebuild /W4                        -> 0 warning
git rev-list --count c573d47..HEAD       -> 24 commit chờ merge vào main
rtatool_snapshot shots 1100 760          -> exit 0, 6 PNG
shots/rta-view.png (md5)                 -> d3e698964c8ffd7bcf2148b154f96e3d
```

**Có BA guard framework khác nhau, đừng nhầm chúng với nhau:**

```
core_has_no_framework_deps            -> OK (53 files scanned)
measure_has_no_framework_deps         -> OK (19 files scanned)   <- app/src/trace + view
platform_types_has_no_framework_deps  -> OK (5 files scanned)
```

Nói "guard đếm bao nhiêu" mà không nói guard nào là một cái bẫy nhỏ của chính nó.

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

**Đường stored-trace được xây nhưng CHƯA NỐI.** Không chỗ nào gắn `TraceLibrary`
vào `RtaView`. Đây là điểm dừng có chủ ý, không phải nợ — nhánh null của
`RtaView` chứng minh được là giống hệt bản cũ, và băm `rta-view.png` khớp xác
nhận điều đó. **L5c là lane nối nó vào.**

**Lane tiếp theo, theo thứ tự:** L2 từ trạm 3 (nó là xương sống — P3, P4b, P6b,
P7 đều chờ nó) song song L5c (nối hiển thị). L5b chờ mua chuẩn.

## Quyết định CHỜ NGƯỜI — không tự quyết

- **Mua ISO 2969:2015 hoặc SMPTE ST 202:2010** — bảng dung sai X-curve cho L5b.
  Hình dạng đường cong tra được công khai; **dung sai thì không**, và một dung
  sai đoán ra là một tuyên bố Class sai.
- **Mua IEC 60268-16** — STI cho P4b. Cùng lý do.
- Tên sản phẩm chính thức (đang là "RTA Tool" working name).

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
