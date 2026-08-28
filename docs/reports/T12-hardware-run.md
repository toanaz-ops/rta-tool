# T12 — phiếu chạy tay M1–M7 (cần chủ máy + interface thật)

*Soạn 2026-08-27 bởi orchestrator kế nhiệm, trên worktree
`.claude/worktrees/orchestrator-ke-nhiem-04b17b`, commit nền `c573d47` cộng bản
vá quan sát M1/M3 chưa commit.*

Đây là bảy bước cuối cùng của Phase 1. Chúng nằm ngoài CI **không phải vì tuỳ
chọn**, mà vì không máy nào tự cắm được sợi dây — xem
`docs/plans/2026-08-27-audioio-rta-impl-plan.md` §5.8.

Luật của dự án: không viết "đạt" nếu chưa dán được thứ quan sát thấy. Cột
**Thấy gì thật** để trống cho tới khi chạy.

---

## 0. Chuẩn bị

Bản dựng dành riêng cho phiên chạy tay, đã bật ASIO:

| | |
|---|---|
| Thư mục build | `build-asio/` (tách khỏi `build/` để không đụng bản CI) |
| Cờ | `RTA_BUILD_APP=ON`, `RTA_ENABLE_ASIO=ON`, `RTA_BUILD_TESTS=OFF` |
| SDK ASIO | `D:/DEV CAVE EP3/PROJECT005-AZ-handsfree/external/asiosdk` (đã có sẵn, không tải gì) |
| Kết quả | `EXIT=0`, 0 cảnh báo, `"RTA Tool.exe"` 10.181.120 byte |
| Bằng chứng ASIO thật sự vào binary | symbol `ASIOAudioIODeviceType@juce` có mặt trong exe |

Bấm `Esc` để xoá dòng lệnh đang dở trước khi chạy, rồi:

```bash
& "D:\DEV CAVE EP3\PRJ010-RTA-TOOL\.claude\worktrees\orchestrator-ke-nhiem-04b17b\build-asio\app\rtatool_artefacts\Release\RTA Tool.exe"
```

Cửa sổ mở ra là màn hình đo thật. Công tắc `SYNTHETIC` ở trên cùng chạy toàn
chuỗi mà không cần phần cứng — **tắt nó** trước khi làm M2 trở đi. `F1` mở bản
mẫu design system.

Ba chỗ cần nhìn trong lúc chạy:

- Panel **DEVICE** — bốn combo `TYPE` / `DEVICE` / `RATE` / `BUFFER`, nút
  `START`, bộ đếm `DROPS`, và **dòng chữ nhỏ ngay dưới hàng START**: đó là chỗ
  fault và lời giải thích hiện ra.
- Dòng **readout** phía trên đồ thị: `100 Hz    -28.7 dB    36 FRAMES`.
- Bảng **CHANNEL / ROLE** bên dưới panel DEVICE.

---

## 1. Bảy bước

| # | Làm gì | Kỳ vọng thấy gì | Thấy gì thật | Đạt? |
|---|---|---|---|---|
| **M1** | Chưa cắm interface. Mở combo `TYPE`, chọn `ASIO`, **rồi bấm `START`, rồi mới nhìn**. | Có **hai** kết quả đều hợp lệ, tuỳ máy có driver ASIO nào đăng ký sẵn không — xem §2 bên dưới. Đừng kết luận ngay lúc vừa chọn: xem ô cảnh báo §2.1. | | |
| **M2** | Cắm interface. Chọn nó ở `DEVICE`. Trong bảng CHANNEL/ROLE: kênh 1 = `MEAS`, kênh 2 = `REF`. Nói vào mic. | Các cột bar nhảy. Readout hiện một dải tần hợp lý và `DROPS` vẫn là `0`. | | |
| **M3** | Đang chạy, đổi `RATE` 48000 → 96000. | Trục tần số vẫn trung thực (tone 1 kHz vẫn đọc `1000 Hz`); **`… FRAMES` trên readout tụt về gần 0 rồi đếm lên lại**; và không có cụm năng lượng cao tần vô lý ở snapshot đầu tiên sau khi đổi. Cụm đó chính là thứ mà ring cũ nối sai qua mốc đổi sample rate sẽ tạo ra. | | |
| **M4** | Đang chạy, đổi `BUFFER`. | Thiết bị nạp lại, tiếng chạy tiếp, app **không đơ**. (Vụ treo ASIO ngày xưa đến từ logic reset nằm trong callback đã ngừng bắn.) | | |
| **M5** | Rút interface giữa chừng. | Một dòng fault đỏ hiện ở panel DEVICE, app còn sống và bấm được, và **không có gì tự kết nối lại**. Cắm lại, bấm `START`, phục hồi. | | |
| **M6** | Đang có tiếng, bấm `STOP` từ panel. | Không crash, không assertion. (Vài tổ hợp platform/JUCE vẫn bắn callback sau `closeAudioDevice()`; biến atomic hợp lệ là thứ hấp thụ điều đó.) | | |
| **M7** | Loopback: output interface → input interface, phát pink noise từ trình phát bất kỳ. | Đồ thị 1/3 octave phẳng trong vài dB, theo đúng đáp tuyến của chính interface. (Bản tự kiểm tra do generator của ta điều khiển là hạng mục sau; đây chỉ là phép thử khói bằng mắt.) | | |

---

## 2. M1 có hai kết quả đều hợp lệ — đừng nhầm cái nào là lỗi

M1 kiểm tra một hành vi im lặng của JUCE: khi loại thiết bị được yêu cầu không
có đăng ký, JUCE **giữ nguyên loại hiện tại và không báo gì cả**;
`deviceManager_.initialise()` vẫn trả về chuỗi rỗng, tức là *thành công*. Đó là
lý do `AudioIo::start()` không ghi fault nào — và đó là một quyết định đã ghi
nhận, không phải thiếu sót: `platform/src/AudioIo.cpp:32-39` nói rõ
`currentState()` mới là cách người gọi biết sự thật.

**(a) Máy KHÔNG có driver ASIO nào đăng ký.** Combo `TYPE` tự nhảy về loại đang
thực dùng (`Windows Audio`), và dòng chữ dưới hàng START hiện, màu `warn`:

```
ASIO unavailable - using Windows Audio
```

**(b) Máy CÓ driver ASIO đăng ký nhưng chưa cắm thiết bị** (ASIO4ALL, hoặc
driver của interface cài sẵn). JUCE chuyển loại **thành công** sang `ASIO` với
danh sách thiết bị rỗng. Khi đó combo giữ `ASIO`, **không** có dòng giải thích —
vì không có gì để giải thích — và nếu `initialise()` thất bại thì một dòng
**fault đỏ** hiện ra thay thế, với nội dung do driver cung cấp.

Cả hai đều đạt. Cái **không** đạt là: combo hiện `ASIO` trong khi thiết bị thật
đang chạy `Windows Audio` — tức là UI nói dối về trạng thái. Đó chính là điều
`currentState()` sinh ra để chặn.

Thứ tự ưu tiên trên dòng chữ đó: fault thật luôn thắng lời giải thích. Chỉ khi
`Fault::Kind::None` thì lời giải thích mới được vẽ.

### 2.1 ⚠️ Combo bật lại NGAY khi chọn, nhưng lời giải thích chỉ đến SAU khi bấm START

Đây là chỗ dễ ghi nhầm "trượt" nhất, nên đọc kỹ.

`AudioIo::setDesiredDeviceType()` **không** đụng tới device manager
(`platform/src/AudioIo.cpp:87-89`) — nó chỉ ghi nhớ nguyện vọng, và nguyện vọng
đó được áp dụng ở lần `start()` kế tiếp. Hệ quả nhìn thấy được:

1. Ngài chọn `ASIO` → combo **lập tức nhảy về loại cũ**, và **chưa có dòng chữ
   nào**. Lúc này chưa có gì bị thử, nên chưa có gì để giải thích.
2. Ngài bấm `START` → thiết bị mới thực sự được mở → dòng giải thích (hoặc
   fault) mới hiện ra.

Nếu ngài kết luận ngay ở bước 1, sẽ tưởng tính năng hỏng. Nó không hỏng — nó cố
ý không nói dối: không tuyên bố gì về một lần thử chưa xảy ra.

Cùng lý do đó, khi thiết bị **đang dừng** thì dòng giải thích luôn bị xoá. Điều
này cũng đúng cho M5: rút dây → thiết bị đóng → lời giải thích cũ biến mất và
fault thật chiếm chỗ, không có chuyện hai thông điệp chồng nhau.

---

## 3. Hai thứ vừa phải vá thì M1 và M3 mới quan sát được

Trước bản vá này, hai bước trên **không thể chạy trung thực** — không phải vì
code sai, mà vì không có gì để nhìn:

| | Vấn đề | Đã vá thế nào |
|---|---|---|
| M1 | Combo tự nhảy về loại thật, nhưng **không lời giải thích nào** được vẽ, vì không có fault nào được ghi. Người dùng thấy lựa chọn của mình bị nuốt mà không rõ vì sao. | `DevicePanel` giữ lại loại **được yêu cầu** trước khi `refreshFromState()` ghi đè combo bằng sự thật, rồi so hai bên. Chỉ so khi thiết bị **đang chạy** — nếu chưa bấm START thì chưa có gì được thử, so sẽ ra lệch giả. |
| M3 | `framesAnalysed` chỉ tồn tại trong struct snapshot, **không widget nào hiển thị**. | Nối vào dòng readout sẵn có của đồ thị. Cố ý **không** phụ thuộc việc có band hay không: chuỗi đang chạy mà im lặng — frame đếm lên, chưa có band — đúng là trạng thái M3 sinh ra để người ta kiểm tra. |

Logic format của cả hai nằm ở `app/src/view/Readouts.h`, một header thuần
không biết JUCE, được canh bởi cùng ctest `measure_has_no_framework_deps` đang
canh `PlotGeometry.h`. Mười test trong `app/tests/test_readouts.cpp` ghim hành vi.

---

## 4. Chạy xong thì làm gì

1. Điền cột **Thấy gì thật** và **Đạt?** ở bảng §1.
2. Bước nào trượt → mô tả đúng thứ quan sát được, không diễn giải.
3. Gộp kết luận vào `docs/reports/002-phase1-audio-chain.md` mục *Outstanding*,
   rồi đóng `docs/features/FEAT-phase1-rta-spl-generator.md` theo
   `task-closeout` path C.
4. Phase 1 đóng → mở đợt 1: **L2 (dual-FFT) ∥ L5 (tuning visuals)** theo
   `docs/plans/MASTER-EXECUTION-PLAN.md`.
