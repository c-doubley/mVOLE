#!/usr/bin/env bash
set -euo pipefail
python3 scripts/final_local_bench.py --reps 10 --p0-cpu 0 --p1-cpu 2
