# FEAT-phase0-foundations — móng cho một engine đo kiểm chứng được

- status: done
- phases: P0 ✅
- branches: main
- next: đã đóng 2026-08-26. Việc tiếp theo nằm ở [FEAT-phase1-rta-spl-generator](FEAT-phase1-rta-spl-generator.md).

Dựng bốn lớp mà toàn bộ dự án tựa lên: `core` (DSP thuần, không framework),
`platform` (I/O thiết bị, còn rỗng), `ui/az_ui` (design system SODIUM RACK đóng
gói thành JUCE module) và `app` (chỉ lắp ráp).

## Quyết định chịu lực

`core/` **không bao giờ** được include JUCE, Qt hay bất kỳ API thiết bị âm thanh
nào. Nhờ đó mọi khẳng định về DSP đều chứng minh được trên CI, không cần sound
card, không cần mic — điều mà Open Sound Meter và OpenOptimize đều không làm
được, và là lý do độ chính xác của chúng không kiểm chứng độc lập được.

Luật này do test `core_has_no_framework_deps` cưỡng chế, không phải do comment.
Đã kiểm chứng bằng cách chèn `#include <juce_core/juce_core.h>` vào
`core/src/dsp/Window.cpp`: guard exit 1 và gọi đúng tên file; gỡ ra thì xanh lại.

## Đã có trong core

- `Window` — cửa sổ cosine dạng **periodic**, kèm hệ số hiệu chỉnh biên độ (ACF),
  năng lượng (ECF) và ENBW. Kiểm bằng đẳng thức đóng (`sum(w) = a0·N`,
  `ENBW = (a0² + ½Σaₖ²)/a0²`) chứ không phải bằng số mà code in ra, đối chiếu
  thêm với bảng Harris (1978).
- `RingBuffer` — SPSC lock-free có `peek`/`discard` tách rời, để khối FFT chồng
  lấn hoạt động được (FFT 4096 với overlap 75% chỉ tiêu thụ 1024 mẫu mỗi bước
  nhưng phải nhìn lại 3072 mẫu cũ). Kiểm bằng stress test hai thread 1 triệu mẫu.

## Số nền — phải giữ nguyên hoặc tốt lên

```
cmake --build build --config Release --parallel   # errors+warnings: 0  (/W4 /permissive-)
ctest --test-dir build -C Release                 # 13/13 pass
```

File dài nhất: `ui/az_ui/theme/AzLookAndFeel_Buttons.cpp`, 233 dòng (trần 400).

## Bẫy đã gặp

1. **JUCE cần `LANGUAGES C CXX`.** Chỉ khai `CXX` thì configure chết với thông
   báo không liên quan gì tới C.
2. **Xoá `build/CMakeCache.txt` mà giữ `build/_deps` là hỏng.** Subbuild của
   Catch2 còn nhớ generator cũ; phải xoá cả `build/`.
3. **`az_ui` suýt không portable.** Bản port đầu `#include <BinaryData.h>` và gọi
   thẳng `BinaryData::SairaCondensedSemiBold_ttf` — những symbol do một target
   `juce_add_binary_data` cụ thể sinh ra. Đã đảo chiều: module khai báo cần mặt
   chữ gì, app đưa bytes vào qua `az::ui::setTypefaces()`.
