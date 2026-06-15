# WHTPCG Reproduction Note

Date: 2026-06-10

## Environment

- workspace: `/home/cyy/pcg-experiments`
- hostname: `cyy`
- CPU cores: `64`
- commit: `9cc48600c0b8d95222b1eb14b92bd4470f78f9eb`
- compiler: `g++ 11.4.0`
- CMake: `3.27.9`

The artifact is checked out directly at `/home/cyy/pcg-experiments`, not under a nested `whtpcg/` directory. No git submodules are registered; `libOTe/` is vendored in the repository.

## README Build Instructions

The README says to install the modified vendored libOTe before configuring the top-level artifact:

```bash
cd libOTe
python3 build.py --all --boost --sodium
```

For benchmarks, it then lists:

```bash
cmake .
make
./main --QA_Syndrome n
./main --EA_Syndrome n
./main --EC_Syndrome n
./main --QA_VOLE n
./main --EA_VOLE n
./main --EC_VOLE n
./main --OLE n
```

The top-level `CMakeLists.txt` requires libOTe components `sodium`, `boost`, `openssl`, `simplestot`, `kos`, `silentot`, `softspoken_ot`, and `silent_vole`.

## Dependencies

Already available:

```text
cmake 3.27.9
g++ 11.4.0
Boost 1.86.0 under /usr/local
```

Installed during reproduction:

```bash
apt-get update -y
apt-get install -y libssl-dev
```

Reason: top-level CMake requires the libOTe `openssl` component, but `libssl-dev` was initially missing. Without it, OpenSSL-enabled libOTe configuration failed with:

```text
Could NOT find OpenSSL, try to set the path to OpenSSL root folder in the
system variable OPENSSL_ROOT_DIR (missing: OPENSSL_CRYPTO_LIBRARY
OPENSSL_INCLUDE_DIR)
```

## Build Commands Used

The initial README-style `--boost` build began downloading Boost 1.86 slowly, so it was stopped and rerun against the already installed Boost 1.86.0.

```bash
cd /home/cyy/pcg-experiments/libOTe
python3 build.py --all --sodium --install=/home/cyy/pcg-experiments/local \
  -DCMAKE_BUILD_TYPE=Release -DENABLE_BOOST=ON -DFETCH_BOOST=OFF
```

Top-level CMake then rejected this libOTe because `openssl` was missing:

```text
Found incompatible libOTe at /home/cyy/pcg-experiments/local/lib/cmake/libOTe.
Missing components: openssl
```

After installing `libssl-dev`, libOTe was rebuilt with OpenSSL:

```bash
cd /home/cyy/pcg-experiments/libOTe
python3 build.py --all --sodium --openssl --install=/home/cyy/pcg-experiments/local \
  -DCMAKE_BUILD_TYPE=Release -DENABLE_BOOST=ON -DFETCH_BOOST=OFF
```

Top-level Release build:

```bash
cd /home/cyy/pcg-experiments
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH=/home/cyy/pcg-experiments/local
cmake --build build --parallel 64
```

## Build Result

Build succeeded. Generated top-level binary:

```text
/home/cyy/pcg-experiments/build/main
```

The local libOTe install is under:

```text
/home/cyy/pcg-experiments/local
```

## Tests and Small Correctness Runs

Top-level CTest has no registered tests:

```bash
ctest --test-dir build --output-on-failure
```

Output:

```text
Test project /home/cyy/pcg-experiments/build
No tests were found!!!
```

Small QA syndrome run:

```bash
./build/main --QA_Syndrome 1
```

Output:

```text
The time of QA Syndrome encoding consume 3.91528e-06 seconds
The time of QA Syndrome encoding consume 1.78814e-07 seconds
```

Small QA-SD VOLE run with `n=1` failed:

```bash
./build/main --QA_VOLE 1
```

Output:

```text
terminate called recursively
terminate called after throwing an instance of 'std::runtime_error'
```

Small QA-SD VOLE run with `n=6` failed because the hardcoded 64 PPRF partitions give a per-partition domain of 1:

```bash
./build/main --QA_VOLE 6
```

Output:

```text
terminate called after throwing an instance of 'std::runtime_error'
terminate called recursively
  what():  Pprf domain must be even. /home/cyy/pcg-experiments/local/include/libOTe/Tools/Pprf/RegularPprf.h:72
```

Smallest QA-SD VOLE run found to pass:

```bash
./build/main --QA_VOLE 7
```

Output:

```text
The total time of VOLE based on QA-SD code consume 0.0141795 seconds
The total time of VOLE based on QA-SD code consume 0.0135015 seconds
```

Relevant libOTe unit tests:

```bash
./libOTe/out/build/linux/frontend/frontend_libOTe -u 94
./libOTe/out/build/linux/frontend/frontend_libOTe -u 137
```

Outputs:

```text
94 - Tools_Pprf_expandOne_test                 Passed   13ms
All Passed (1)
```

```text
137 - Vole_Noisy_test                          Passed   34ms
All Passed (1)
```

## Located Relevant Code

- WHT implementation: `Walsh.h`, functions `wht<F, Ctx>()` and `prime_wht<F, Ctx>()`
- QA-SD VOLE benchmark/correctness wrapper: `Prime_test.h`, function `VOLE_prime_QASD<F, Ctx>()`
- QA-SD VOLE sender/receiver implementation: `SilentWalshVoleSender.h`, `SilentWalshVoleReceiver.h`
- Benchmark/test entry point: `main.cpp`, options `--QA_Syndrome`, `--QA_VOLE`, `--EA_VOLE`, `--EC_VOLE`, `--OLE`
- PPRF implementation from libOTe: `libOTe/libOTe/Tools/Pprf/RegularPprf.h`
- libOTe benchmark entry point: `libOTe/frontend/benchmark.h`
- libOTe test framework/registry: `libOTe/libOTe_Tests/UnitTests.cpp`

## Notes

No source files were modified during this reproduction phase. Generated build/dependency directories and the documentation note were added under the workspace.
