# HANDOFF — cho orchestrator kế tiếp

*2026-08-28, cuối phiên orchestrator Phase-1. Đọc file này + docs/reports/002 +
docs/plans/MASTER-EXECUTION-PLAN.md trước khi đọc bất kỳ code nào.*

## Baseline đo được (dán từ lệnh, không từ trí nhớ)

```
git log --oneline | wc -l          -> 46 commits, main, đã push đủ
ctest --test-dir build -C Release  -> 119/119 (shared build, TRƯỚC generator)
warnings /W4                       -> 0
snapshot exit code                 -> 0 (heap bug đã diệt tận gốc)
```

## Việc DANG DỞ — làm đầu tiên

**Generator track chưa commit, đang nằm trên đĩa** (`git status` sẽ thấy
core/gen/*, test_generator_*, tools/gen_generator.py, golden/generator.txt).
Một coordinator agent có thể vẫn đang chạy vòng tích hợp trong `build-gen/`.
Trình tự:

1. Nếu báo cáo gom cuối của coordinator đã về root — đối chiếu nó với đĩa.
2. Nếu không có báo cáo: TỰ tích hợp từ đĩa — configure/build `build-gen`
   (hoặc build chung), full ctest, golden tái sinh 2 lần byte-identical,
   guard no_polynomial_form xanh, rồi commit trọn track một commit.
3. Sau commit: xoá build-gen/ và build-meters/, cập nhật FEAT + report 002
   mục Outstanding.

## Quyết định CHỜ NGƯỜI — không tự quyết

- **T12 hardware M1-M7** (plan AudioIo §5.8): cần chủ cắm interface ASIO thật.
- Mua/nguồn IEC 60268-16 (STI) và bảng Table 2 đầy đủ (mọi claim Class-1).
- Tên sản phẩm chính thức (đang là "RTA Tool" working name).

## Bẫy đã trả học phí (đừng trả lần hai)

1. **Thông báo hoàn tất của agent con nổi lên SESSION ROOT, không tới
   coordinator đã spawn nó** — coordinator phải poll file, cấm chờ tin nhắn.
2. **Relay giữa agent: chỉ sự kiện định tuyến** — orchestrator phiên này bị
   bác hai lần vì mô tả code chưa mở. Mọi thuộc tính kỹ thuật do bên nhận
   đọc từ đĩa.
3. Hai file CMake của core là điểm tranh chấp; một commit tích hợp tuần tự.
4. `cmd //c` để chạy exe từ Git Bash (gọi thẳng = exit 127); app đang mở giữ
   lock .exe — taskkill trước khi build lại.
5. Heredoc bash chứa văn bản dài/đặc biệt hay vỡ ngầm; dùng Write tool cho
   file, python-viết-python với escape là thảm hoạ đã tái diễn.
6. Guard cắn chéo track là TÍNH NĂNG — conform (đường ZPK, vòng lặp tay),
   đừng đục lỗ.
7. Literal trong tài liệu plan là gợi ý, không phải oracle: test khẳng định
   CÔNG THỨC. Bốn literal sai đã bị bắt trong phiên này.

## Sau khi Phase 1 đóng

Mở lane theo MASTER-EXECUTION-PLAN (topology đã chốt: MỘT orchestrator,
chuỗi nối tiếp, không song song trên repo này): đợt 1 = L2 (dual-FFT) song
song L5 (tuning visuals). Session rời chỉ dành cho L-web (repo web — prompt
sẵn trong docs/marketing/WEB-HANDOFF.md) và L8 research (read-only).
