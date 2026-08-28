# HANDOFF — cho orchestrator kế tiếp

*2026-08-28, phiên orchestrator kế nhiệm. Đọc file này + docs/reports/002 +
docs/plans/MASTER-EXECUTION-PLAN.md trước khi đọc bất kỳ code nào.*

## Baseline đo được (dán từ lệnh, không từ trí nhớ)

Đo lại ngày 2026-08-28 trên worktree `claude_desk/orchestrator-ke-nhiem-04b17b`.
**Mọi con số trong handoff trước đều đã lệch** — đừng chép lại con số từ tài
liệu, hãy chạy lại lệnh.

```
git log --oneline | wc -l           -> 50 trên main, +2 trên nhánh này chờ merge
ctest --test-dir build -C Release   -> 159/159, 0 fail   (trước bản vá: 149/149)
clean rebuild /W4                   -> EXIT=0, 0 warning
rtatool_snapshot shots 1100 760     -> exit 0, 6 PNG
build-asio (RTA_ENABLE_ASIO=ON)     -> EXIT=0, 0 warning, ASIOAudioIODeviceType@juce có trong binary
```

## Việc DANG DỞ — làm đầu tiên

**Chỉ còn một việc trong Phase 1, và nó cần tay người, không phải thêm agent:**
T12 hardware pass M1-M7. Phiếu điền sẵn ở `docs/reports/T12-hardware-run.md`,
bản dựng có ASIO đã sẵn ở `build-asio/`.

Mọi thứ máy làm được cho T12 đã xong và đã commit:

- Nửa tự động (`/W4` sweep + ctest) đã đo, xanh.
- Hai trong bảy bước **trước đây không quan sát được** và sẽ bị ghi nhận dối:
  M1 đòi một lời giải thích chưa từng được vẽ, M3 đòi `framesAnalysed` mà không
  widget nào hiển thị. Đã vá ở `9f6dce1`, đã qua phản biện đối kháng.
- Sau khi ngài chạy xong M1-M7: đóng FEAT theo `task-closeout` path C, rồi mở
  đợt 1 **L2 (dual-FFT) ∥ L5 (tuning visuals)**, mỗi lane một worktree +
  build-dir riêng.

**T12 không chặn L2 và L5.** Nó thử `platform/AudioIo` với phần cứng thật; cả
hai lane kia không đụng đường đó. Nếu chủ chưa rảnh cắm dây, cứ mở lane.

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

## Sau khi Phase 1 đóng

Mở lane theo MASTER-EXECUTION-PLAN (topology đã chốt: MỘT orchestrator, chuỗi
nối tiếp, không song song trên repo này): đợt 1 = L2 (dual-FFT) song song L5
(tuning visuals). Session rời chỉ dành cho L-web (repo web — prompt sẵn trong
`docs/marketing/WEB-HANDOFF.md`) và L8 research (read-only).

Lưu ý cho L2: **chưa có decision record cho dual-FFT.** Lane đó phải bắt đầu từ
trạm 1 (research), không nhảy thẳng vào code. L5 thì đã có
`docs/specs/2026-08-28-interactive-tuning-visuals.md`.
