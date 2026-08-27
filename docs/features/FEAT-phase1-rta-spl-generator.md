# FEAT-phase1-rta-spl-generator — RTA, SPL meter và bộ phát tín hiệu

- status: active
- phases: P1 ⏳ (90% — còn generator commit + hardware M1-M7)
- branches: main
- next: (1) generator coordinator gom vòng tích hợp xong thì commit track
  từ đĩa; (2) T12: cắm interface ASIO thật, chạy M1-M7 (plan AudioIo §5.8);
  (3) đóng FEAT theo task-closeout path C. Chi tiết: docs/reports/002 +
  docs/HANDOFF.md. Shared build 119/119, zero warnings; app chạy thật với
  SYNTHETIC mode; heap bug snapshot đã diệt gốc.

Module đo được đầu tiên. Chọn làm trước Dual-FFT vì dễ kiểm chứng nhất: phát
pink noise, đo bằng mic, so số với Smaart hoặc máy đo SPL cầm tay. Xây xong thì
FFT engine + octave banding + generator đã sẵn sàng cho mọi module sau.

## Phạm vi

| Module | Chuẩn tham chiếu |
|---|---|
| `dsp/Fft` — FFT đầu vào thực, twiddle tính sẵn, đặt sau interface | — |
| `dsp/SpectrumEngine` — overlap-add, trung bình Welch (power + exponential) | — |
| `dsp/OctaveBands` — 1/1 → 1/48 octave, hai chế độ: cộng bin và filterbank thật | IEC 61260 |
| `dsp/Weighting` — A / C / Z, pole-zero analog → bilinear | IEC 61672-1 |
| `meter/LevelMeter` — Fast/Slow/Impulse, peak, true-peak, RMS | IEC 61672-1 |
| `meter/Leq` — Leq, LAeq, LCeq, LZeq, Lmax/Lmin, L10/L50/L90 | IEC 61672-1 |
| `gen/SignalGenerator` — pink, white, sine, dual sine, sweep Farina, MLS | — |
| `cal/CalibrationCurve` — đọc `.frd`/`.txt`/`.cal`, nội suy log-tần số | — |
| `cal/SplCalibration` — tone 94/114 dB → offset dBSPL | IEC 60942 |
| `platform/AudioIo` — ASIO đa kênh, gán vai trò Measurement/Reference cho từng kênh | — |

## Tiêu chí nghiệm thu — mỗi dòng là một test tự động

| Kiểm tra | Đạt khi |
|---|---|
| Sine 1 kHz @ −20 dBFS qua cửa sổ Hann | đỉnh tại 1000 Hz ±0.5 Hz, biên độ −20 dBFS ±0.1 dB |
| Pink noise qua băng 1/3 octave | mọi băng bằng nhau trong ±0.5 dB |
| A-weighting tại 31.5 Hz … 8 kHz | trong dung sai Class 1, IEC 61672-1 |
| Detector Fast/Slow, burst 1 kHz | thời gian lên/xuống đúng đặc tả IEC |
| Leq của file WAV chuẩn | khớp tham chiếu Python trong ±0.1 dB |
| Độ dốc pink noise của generator | −3.01 dB/octave ±0.2 dB |
| Sweep Farina → deconvolution | khôi phục xung đơn vị, SNR > 60 dB |

Cộng một phép thử loopback vật lý: cắm output về input trên cùng interface.

## Việc chặn

`tools/gen_golden.py` cần **numpy và scipy**, máy này chưa có (Python 3.14.6
trần). Phải tạo venv trước khi bắt đầu sinh golden vector.
