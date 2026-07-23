#!/usr/bin/env bash
set -euo pipefail
cmake -S . -B build-final -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/home/cyy/pcg-experiments/local
cmake --build build-final -j2
python3 scripts/final_local_bench.py --reps 10 --taskset --p0-core 0 --p1-core 1
