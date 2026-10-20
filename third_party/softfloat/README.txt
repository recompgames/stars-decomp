Berkeley SoftFloat by John R. Hauser (BSD-3-Clause, see LICENSE): "3e plus master changes".
Source: https://github.com/ucb-bar/berkeley-softfloat-3, GitHub master tarball at commit
a0c6494cdc11865811dec815d5c0049fba9d82a8, fetched 2026-10-15. It is NOT the 3e release zip:
it is 3e plus post-release changes (including bf16 support). The headers still say
"Release 3e". The 8086-SSE specialization was reviewed and is correct for arm64 clang and
wasm32.
Vendored unmodified: source/ (specialization 8086-SSE only; the other specialization
directories were removed) and platform.h (copied from build/Linux-x86_64-GCC/platform.h).
Build flags: see the Makefile (SOFTFLOAT_FAST_INT64, SOFTFLOAT_ROUND_ODD, INLINE_LEVEL=5).
Used for exact x87 80-bit arithmetic: docs/CONVENTIONS.md, section "Floating point".
