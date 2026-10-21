# SPDX-License-Identifier: AGPL-3.0-or-later
# Wall-clock benchmarks. Not registered with ctest; see bench_fft.cpp.
add_executable(rta_bench_fft bench_fft.cpp)
target_link_libraries(rta_bench_fft PRIVATE rta::core)
