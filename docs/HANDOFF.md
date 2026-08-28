# HANDOFF — cho orchestrator kế tiếp

*2026-08-28, phiên orchestrator kế nhiệm. Đọc file này + docs/reports/002 +
docs/plans/MASTER-EXECUTION-PLAN.md trước khi đọc bất kỳ code nào.*

## Baseline đo được (dán từ lệnh, không từ trí nhớ)

Đo lại ngày 2026-08-28 trên worktree `claude_desk/orchestrator-ke-nhiem-04b17b`.
**Mọi con số trong handoff trước đều đã lệch** — đừng chép lại con số từ tài
liệu, hãy chạy lại lệnh.

```
git log --oneline | wc -l           -> 50 trên main, +23 trên nhánh này chờ merge
ctest --test-dir build -C Release   -> 226/226, 0 fail   (đầu ngày: 149/149)
clean rebuild /W4                   -> EXIT=0, 0 warning
rtatool_snapshot shots 1100 760     -> exit 0, 6 PNG
build-asio (RTA_ENABLE_ASIO=ON)     -> EXIT=0, 0 warning, ASIOAudioIODeviceType@juce có trong binary
```

## Việc DANG DỞ — làm đầu tiên

**Phase 1 còn đúng HAI bước, và cả hai cần giác quan người:** M2 (nói vào mic
thật, xem bar nhảy) và M7 (cáp loopback vật lý). Phiếu điền sẵn ở
`docs/reports/T12-hardware-run.md`, bản dựng có ASIO sẵn ở `build-asio/`.

Năm bước kia **đã được tự động hoá** và không còn cần ai cắm dây:

- Lý do thật khiến M1-M7 bị đẩy sang chạy tay không phải phần cứng, mà là
  **`AudioIo` chưa từng có target test nào** — `rta_platform_tests` chỉ link
  `rta::platform_types`, tức nửa không biết JUCE. Cánh cửa nằm sẵn trong thiết
  kế: `AudioIo` kế thừa **public** `juce::AudioIODeviceCallback`, nên "rút dây
  giữa chừng" ở tầng code chỉ là một lời gọi `audioDeviceError()`.
- Target mới ở `platform/tests_juce/` với một `juce::AudioIODevice` giả, không
  mở thiết bị nào. Nó bắt được một **lỗi thật ngay lần chạy đầu**:
  `audioDeviceError()` xoá `running_` và ghi fault nhưng **không hạ cờ hoạt động
  của capture bus**, trong khi cả hai đường chết thiết bị còn lại đều hạ. Callback
  bắn sau khi thiết bị chết sẽ vẫn được nhận và ghi vào ring của một thiết bị
  không còn tồn tại. **Phiên chạy tay sẽ không bao giờ bắt được lỗi này** — M5
  bảo người kiểm tra xác nhận có dòng fault, app còn sống, không tự kết nối lại;
  cả ba đều đúng, còn cờ `isActive` thì không hiện trên màn hình.
- M1 và M3 trước đó **không quan sát được** và sẽ bị ghi nhận dối: M1 đòi một
  lời giải thích chưa từng được vẽ, M3 đòi `framesAnalysed` mà không widget nào
  hiển thị. Đã vá ở `9f6dce1`, đã qua phản biện đối kháng.

**M2 và M7 không chặn gì cả.** Chúng thử `platform/AudioIo` với phần cứng thật;
không lane nào khác đụng đường đó. Cứ mở lane, chủ chạy tay lúc nào rảnh.

## Quyết định CHỜ NGƯỜI — không tự quyết

- **T12 hardware M1-M7**: cần chủ cắm interface thật.
- Mua/nguồn IEC 60268-16 (STI) và bảng Table 2 đầy đủ (mọi claim Class-1).
- Tên sản phẩm chính thức (đang là "RTA Tool" working name).

## Nợ kỹ thuật đã biết

- **Guard phụ thuộc framework có điểm mù.** Biến thể danh-sách-tường-minh mà
  `measure_has_no_framework_deps` dùng mở rộng bằng `file(GLOB_RECURSE)`; glob
  một đường dẫn không tồn tại trả về rỗng chứ không lỗi, và chỉ khi **toàn bộ**
  rỗng mới FATAL. Đo trực tiếp: một đường dẫn thật + một đường dẫn sai vẫn ra
  `OK (1 files scanned)`, exit 0. Nghĩa là gõ sai tên file thì test không đỏ —
  nó **âm thầm ngừng bảo vệ** file đó mà vẫn báo xanh. Hiện chỉ chứng minh được
  độ phủ bằng **số đếm** test in ra (thêm `Readouts.h` làm nó nhảy 8 → 9).
  Chi tiết: `memory/core-must-not-include-frameworks.md`.
- Logic cổng của `DevicePanel` (đang chạy hay không, fault thắng thông báo) mới
  chỉ được kiểm bằng đọc code; chỉ hàm thuần trong `Readouts.h` có test.

## Bẫy đã trả học phí (đừng trả lần hai)

1. **Thông báo hoàn tất của agent con nổi lên SESSION ROOT, không tới
   coordinator đã spawn nó** — coordinator phải poll file, cấm chờ tin nhắn.
2. **Relay giữa agent: chỉ sự kiện định tuyến** — mọi thuộc tính kỹ thuật do
   bên nhận đọc từ đĩa, không chép qua lời kể.
3. Hai file CMake của core là điểm tranh chấp; một commit tích hợp tuần tự.
4. `cmd //c` để chạy exe từ Git Bash (gọi thẳng = exit 127); app đang mở giữ
   lock .exe — taskkill trước khi build lại.
5. Heredoc bash chứa văn bản dài/đặc biệt hay vỡ ngầm; dùng Write tool cho
   file, python-viết-python với escape là thảm hoạ đã tái diễn.
6. Guard cắn chéo track là TÍNH NĂNG — conform (đường ZPK, vòng lặp tay),
   đừng đục lỗ.
7. Literal trong tài liệu plan là gợi ý, không phải oracle: test khẳng định
   CÔNG THỨC. Bốn literal sai đã bị bắt.
8. **Venv nằm ở checkout chính, worktree KHÔNG thấy nó.**
   `D:\DEV CAVE EP3\PRJ010-RTA-TOOL\.venv` có numpy 2.5.2 + scipy 1.18.1 và đã
   sinh ra sáu golden vector. Nhưng `.venv` bị gitignore nên `git worktree add`
   không mang theo — trong worktree, `import numpy` thất bại. Đừng tạo venv thứ
   hai, đừng kết luận máy thiếu numpy. Gọi interpreter kia bằng đường dẫn tuyệt
   đối.
9. **`build-*/` mới được thêm vào `.gitignore`** (commit `8409958`). Trước đó
   chỉ `build/` được chặn, trong khi luật đa-lane bắt mỗi lane dùng
   `build-<lane>` riêng. Nếu phiên nào thấy `git status` đầy file object thì
   kiểm tra lại dòng đó còn không.
10. **Con số trong tài liệu mục nát nhanh hơn ta tưởng.** Handoff trước ghi 46
    commits / 149 tests, FEAT ghi 119/119, MASTER-EXECUTION-PLAN ghi 103/103 —
    ba con số khác nhau cho cùng một repo. Đo lại, đừng chép.

    **Và bẫy này đã cắn chính người viết ra nó, trong cùng một ngày.** Sáng
    2026-08-28 tôi sửa cả ba con số về 159/159. Đến chiều, sau tám nhiệm vụ
    L5a, năm file lại ghi 159 trong khi thực tế là 226 — chỉ có review toàn
    nhánh mới bắt được. Nghĩa là "nhớ cập nhật" **không phải một cơ chế**; nó
    là một ý định, và ý định thì không sống sót qua một ngày làm việc.

    Hai điều rút ra, dùng được ngay:

    - **Mỗi tài liệu giữ con số ở ĐÚNG MỘT CHỖ.** Báo cáo 002 từng có 159/159
      ở ba nơi, tức ba cơ hội để mục. Giờ phần thân trỏ về đầu tài liệu. Một
      con số lặp lại ở n chỗ sẽ sai ở n−1 chỗ.
    - **Đợt vá cuối làm mọi mốc trong plan lệch đi.** Vòng sửa nhiệm vụ 3 thêm
      8 test, thế là các mốc 191/196/200 tôi viết cho nhiệm vụ 4-6 đều sai và
      phải nói đè trong từng lệnh phái. Nếu plan có mốc số, hãy coi chúng là
      **dự kiến**, và đo lại trước mỗi nhiệm vụ.

## Sau khi Phase 1 đóng

Mở lane theo MASTER-EXECUTION-PLAN (topology đã chốt: MỘT orchestrator, chuỗi
nối tiếp, không song song trên repo này): đợt 1 = L2 (dual-FFT) song song L5
(tuning visuals). Session rời chỉ dành cho L-web (repo web — prompt sẵn trong
`docs/marketing/WEB-HANDOFF.md`) và L8 research (read-only).

**L2 giờ ĐÃ có decision record**: `docs/dsp/2026-08-28-dual-fft.md`, sáu quyết
định kèm cái giá của phương án bị loại. Trạm 1 (research) và trạm 2 (phân tích)
của L2 đã xong; lane đó bắt đầu từ **trạm 3 — kế hoạch thi công**, không phải từ
đầu. Đọc record trước khi viết dòng code nào: nó chốt H1 làm mặc định *và* giải
thích vì sao Open Sound Meter không dùng H1, chốt coherence dạng bình phương
(nên số của ta sẽ thấp hơn số OSM trên cùng dữ liệu — đừng "sửa"), và chốt
ngưỡng tối thiểu số khung trước khi được phát coherence.

**L5 đã tách làm bốn** (xem `docs/specs/2026-08-28-trace-library-and-session.md`
§"Why this is its own record"). **L5a đã thi công xong trên nhánh này**: mô hình
trace, thư viện, lưu phiên, cổng vẽ lại, tầng ảnh đệm — tám nhiệm vụ, kế hoạch ở
`docs/plans/2026-08-28-L5a-trace-session-impl-plan.md`. L5b/L5c chưa viết record.
L5d (cepstrum, wavelet) đã **chuyển khỏi L5** sang lane DSP vì nó là DSP.
