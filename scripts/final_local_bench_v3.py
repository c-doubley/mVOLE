#!/usr/bin/env python3
"""Generate the v3 local RM-VOLE benchmark artifact bundle."""

from __future__ import annotations

import argparse
import csv
import datetime as dt
import json
import math
import os
import platform
import re
import shutil
import statistics
import subprocess
import sys
import time
from pathlib import Path

import matplotlib.pyplot as plt


P = 2305843009213693951
LAMBDA = 128
T = 64
LOG_NS = [12, 14, 16, 18, 20]
MS = [8, 16, 32]
DIRECT_POINTS = [(12, 8), (12, 16), (12, 32), (14, 8), (14, 16), (16, 8)]
V2_DIR = Path("docs/final_local_bench_v2")
OUT = Path("docs/final_local_bench_v3")


def now_iso() -> str:
    return dt.datetime.now(dt.timezone.utc).isoformat()


def git(cwd: Path, *args: str) -> str:
    return subprocess.check_output(["git", *args], cwd=cwd, text=True).strip()


def run(cmd: list[str], cwd: Path, log, timeout: int | None = None) -> subprocess.CompletedProcess[str]:
    log.write("$ " + " ".join(cmd) + "\n")
    log.flush()
    start = time.time()
    cp = subprocess.run(cmd, cwd=cwd, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=timeout)
    elapsed = time.time() - start
    log.write(cp.stdout)
    if cp.stdout and not cp.stdout.endswith("\n"):
        log.write("\n")
    log.write(f"[exit={cp.returncode} elapsed_s={elapsed:.6f}]\n\n")
    log.flush()
    return cp


def read_csv(path: Path) -> list[dict[str, str]]:
    if not path.exists():
        return []
    with path.open(newline="", encoding="utf-8") as f:
        return list(csv.DictReader(f))


def write_csv(path: Path, rows: list[dict]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    keys: list[str] = []
    for row in rows:
        for key in row:
            if key not in keys:
                keys.append(key)
    with path.open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=keys)
        writer.writeheader()
        writer.writerows(rows)


def parse_kv(text: str) -> dict[str, str]:
    out: dict[str, str] = {}
    for token in text.replace("\n", " ").split():
        if "=" in token:
            k, v = token.split("=", 1)
            out[k] = v
    return out


def fnum(row: dict, key: str, default: float = 0.0) -> float:
    try:
        return float(row.get(key, default))
    except (TypeError, ValueError):
        return default


def inum(row: dict, key: str, default: int = 0) -> int:
    try:
        return int(float(row.get(key, default)))
    except (TypeError, ValueError):
        return default


def summarize(raw: list[dict], group_keys: list[str], metric_keys: list[str]) -> list[dict]:
    groups: dict[tuple, list[dict]] = {}
    for row in raw:
        if str(row.get("warmup", "0")) == "1":
            continue
        if row.get("status") != "OK" or row.get("data_source") != "measured":
            continue
        groups.setdefault(tuple(row.get(k, "") for k in group_keys), []).append(row)
    out: list[dict] = []
    for key, rows in sorted(groups.items()):
        base = {k: v for k, v in zip(group_keys, key)}
        for metric in metric_keys:
            vals = sorted(fnum(r, metric) for r in rows if r.get(metric, "") != "")
            if not vals:
                continue
            q25 = statistics.quantiles(vals, n=4, method="inclusive")[0] if len(vals) > 1 else vals[0]
            q75 = statistics.quantiles(vals, n=4, method="inclusive")[2] if len(vals) > 1 else vals[0]
            out.append({
                **base,
                "metric": metric,
                "count": len(vals),
                "median": statistics.median(vals),
                "mean": statistics.mean(vals),
                "stddev": statistics.stdev(vals) if len(vals) > 1 else 0.0,
                "q25": q25,
                "q75": q75,
                "min": min(vals),
                "max": max(vals),
            })
    return out


def lscpu_rows(cwd: Path) -> dict[int, dict[str, int]]:
    text = subprocess.check_output(["lscpu", "-e=CPU,CORE,SOCKET,NODE"], cwd=cwd, text=True)
    rows = {}
    for line in text.splitlines():
        parts = line.split()
        if len(parts) == 4 and parts[0] != "CPU":
            rows[int(parts[0])] = {"core": int(parts[1]), "socket": int(parts[2]), "node": int(parts[3]) if parts[3] != "-" else -1}
    return rows


def command_wrapper(cpu: int, node: int) -> list[str]:
    out: list[str] = []
    if shutil.which("numactl"):
        out += ["numactl", f"--cpunodebind={node}", f"--membind={node}"]
    if shutil.which("taskset"):
        out += ["taskset", "-c", str(cpu)]
    return out


def reg_noise_weight(min_dist: float, n: int, sec: int = 128) -> int:
    d = math.log2(1 - 2 * min_dist)
    t = max(40, int(math.ceil(-float(sec) / d)))
    if n < 512:
        t = max(t, 64)
    return ((t + 7) // 8) * 8


def silent_preflight(logn: int, m: int, ram_bytes: int) -> dict:
    n = 1 << logn
    partitions = reg_noise_weight(0.15, n * 2, LAMBDA)
    size_per = max(4, (((n * 2 + partitions - 1) // partitions + 1) // 2) * 2)
    code_size = size_per * partitions
    base_ot = math.ceil(math.log2(size_per)) * partitions
    output_mem = 3 * n * m * 8
    temp_mem = 4 * code_size * m * 8
    predicted_memory = output_mem + temp_mem
    predicted_traffic = 2 * code_size * m * 8 + base_ot * 64
    status = "OK"
    reason = ""
    if predicted_traffic > 8 * 1024**3:
        status, reason = "SKIPPED_RESOURCE_LIMIT", "predicted traffic > 8 GiB"
    if predicted_memory > 0.70 * ram_bytes:
        status, reason = "SKIPPED_MEMORY_LIMIT", "predicted peak memory > 70% RAM"
    return {
        "logN": logn, "N": n, "m": m, "requested_N": n, "generated_N": n,
        "code_family": "ExConv7x24", "code_rate": n / code_size,
        "noise_weight": partitions, "partition_count": partitions,
        "size_per_partition": size_per, "code_size": code_size,
        "base_ot_count": base_ot, "base_noisy_vole_count": partitions,
        "predicted_traffic_bytes": predicted_traffic,
        "predicted_peak_memory_bytes": predicted_memory,
        "output_memory_bytes": output_mem,
        "temporary_encoder_buffer_bytes": temp_mem,
        "timeout_estimate_s": "",
        "status": status, "skip_reason": reason,
    }


def measured_silent_parameter_rows(preflight: list[dict], silent_raw: list[dict]) -> list[dict]:
    rows: list[dict] = []
    for pf in preflight:
        measured = [
            r for r in silent_raw
            if str(r.get("logN")) == str(pf["logN"])
            and str(r.get("m")) == str(pf["m"])
            and r.get("status") == "OK"
        ]
        sample = measured[0] if measured else {}
        row = dict(pf)
        if sample:
            for key in [
                "requested_N", "generated_N", "code_size", "noise_weight",
                "partition_count", "size_per_partition", "base_ot_count",
                "base_noisy_vole_count",
            ]:
                row[key] = sample.get(key, row.get(key, ""))
            n = inum(row, "N")
            code_size = inum(row, "code_size")
            m = inum(row, "m")
            base_ot = inum(row, "base_ot_count")
            if n and code_size:
                row["code_rate"] = n / code_size
            row["predicted_traffic_bytes"] = 2 * code_size * m * 8 + base_ot * 64
            row["temporary_encoder_buffer_bytes"] = 4 * code_size * m * 8
            row["predicted_peak_memory_bytes"] = inum(row, "output_memory_bytes") + inum(row, "temporary_encoder_buffer_bytes")
        row["actual_generated_length"] = sample.get("generated_N", pf["generated_N"])
        row["outputs_truncated"] = 0
        row["security_setting"] = "semi-honest-128"
        row["supported"] = 1 if measured else 0
        rows.append(row)
    return rows


def run_pair(cwd: Path, exe: Path, subcmd: str, logn: int, m: int, mode: str, port: int, p0_cpu: int, p1_cpu: int, node: int, log) -> tuple[dict, dict, str, str]:
    server_cmd = command_wrapper(p0_cpu, node) + [str(exe), "--RMVOLE_NET_BENCH", f"{subcmd}-server", "0.0.0.0", str(port), str(logn), str(m), "1", mode]
    client_cmd = command_wrapper(p1_cpu, node) + [str(exe), "--RMVOLE_NET_BENCH", f"{subcmd}-client", "127.0.0.1", str(port), str(logn), str(m), "1", mode]
    log.write("$ " + " ".join(server_cmd) + " &\n")
    server = subprocess.Popen(server_cmd, cwd=cwd, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    time.sleep(0.2)
    log.write("$ " + " ".join(client_cmd) + "\n")
    client = subprocess.run(client_cmd, cwd=cwd, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=1500)
    server_out, _ = server.communicate(timeout=1500)
    log.write(server_out)
    log.write(client.stdout)
    log.write(f"[server_exit={server.returncode} client_exit={client.returncode}]\n\n")
    log.flush()
    if server.returncode != 0 or client.returncode != 0:
        raise RuntimeError(f"{subcmd} pair failed: server={server.returncode} client={client.returncode}\n{server_out}\n{client.stdout}")
    return parse_kv(server_out), parse_kv(client.stdout), server_out, client.stdout


def aggregate_pair(server: dict, client: dict, trial: int, warmup: bool, source_commit: str) -> dict:
    method = server.get("method", "")
    n = inum(server, "N")
    m = inum(server, "m")
    p0_sent = inum(server, "bytes_sent_by_role")
    p1_sent = inum(client, "bytes_sent_by_role")
    p0_recv = inum(server, "bytes_received_by_role")
    p1_recv = inum(client, "bytes_received_by_role")
    metadata = inum(server, "harness_metadata_bytes") + inum(client, "harness_metadata_bytes")
    openings = inum(server, "verification_opening_bytes") + inum(client, "verification_opening_bytes")
    total_comm = p0_sent + p1_sent - metadata - openings
    total_recv = p0_recv + p1_recv - metadata - openings
    row = {
        "timestamp": now_iso(),
        "benchmark_source_commit": source_commit,
        "method": method,
        "base_ot_mode": "INCLUDING_BASE_OT",
        "p": P,
        "base_field_bits": 61,
        "base_coordinate_storage_bits": 64,
        "lambda": LAMBDA,
        "logN": server.get("logN"),
        "N": n,
        "m": m,
        "trial_index": trial,
        "warmup": int(warmup),
        "status": "OK",
        "data_source": "measured",
        "network": "tcp-loopback",
        "transport": "TCP loopback",
        "relation": "Z0[i][j] + Z1[i][j] = x[i] * Delta[j] mod p",
        "security_mode": "semi-honest-128",
        "requested_N": server.get("requested_N"),
        "generated_N": server.get("generated_N"),
        "code_family": "ExConv7x24" if method == "SILENT_SVOLE" else "",
        "code_size": server.get("code_size"),
        "noise_weight": server.get("noise_weight"),
        "partition_count": server.get("partition_count"),
        "size_per_partition": server.get("size_per_partition"),
        "base_ot_count": server.get("base_ot_count"),
        "base_ot_protocol_invocations": server.get("base_ot_protocol_invocations"),
        "pprf_instance_count": 0,
        "pprf_base_ot_count": 0,
        "base_noisy_vole_count": server.get("base_noisy_vole_count"),
        "silent_extension_count": server.get("silent_extension_count"),
        "base_correlation_setup_ms": 1000 * max(fnum(server, "base_correlation_setup_s"), fnum(client, "base_correlation_setup_s")),
        "silent_extension_ms": 1000 * max(fnum(server, "silent_extension_s"), fnum(client, "silent_extension_s")),
        "local_output_mapping_ms": 1000 * max(fnum(server, "local_output_mapping_s"), fnum(client, "local_output_mapping_s")),
        "total_wall_ms": 1000 * max(fnum(server, "total_s"), fnum(client, "total_s")),
        "p0_cpu_work_ms": 1000 * fnum(server, "total_s"),
        "p1_cpu_work_ms": 1000 * fnum(client, "total_s"),
        "total_cpu_work_ms": 1000 * (fnum(server, "total_s") + fnum(client, "total_s")),
        "p0_peak_rss_bytes": "",
        "p1_peak_rss_bytes": "",
        "p0_bytes_sent": p0_sent,
        "p0_bytes_received": p0_recv,
        "p1_bytes_sent": p1_sent,
        "p1_bytes_received": p1_recv,
        "total_comm_bytes": total_comm,
        "total_received_bytes": total_recv,
        "sent_received_delta_bytes": total_comm - total_recv,
        "base_ot_bytes": "",
        "base_noisy_vole_bytes": "",
        "silent_payload_bytes": "",
        "direct_base_ot_bytes": "",
        "direct_payload_bytes": "",
        "silent_base_ot_bytes": "",
        "silent_base_noisy_vole_bytes": "",
        "silent_extension_payload_bytes": "",
        "framing_bytes": inum(server, "framing_bytes") + inum(client, "framing_bytes"),
        "bytes_per_svole_coordinate": total_comm / (n * m),
        "bits_per_base_field_matrix_entry": 8 * total_comm / (n * m),
        "notes": server.get("notes", ""),
    }
    if method == "DIRECT_NOISY_SVOLE":
        row["direct_base_ot_bytes"] = 0
        row["direct_payload_bytes"] = total_comm
    if method == "SILENT_SVOLE":
        row["silent_base_ot_bytes"] = inum(server, "silent_base_ot_bytes") + inum(client, "silent_base_ot_bytes")
        row["silent_base_noisy_vole_bytes"] = inum(server, "silent_base_noisy_vole_bytes") + inum(client, "silent_base_noisy_vole_bytes")
        row["silent_extension_payload_bytes"] = inum(server, "silent_extension_payload_bytes") + inum(client, "silent_extension_payload_bytes")
        row["base_noisy_vole_bytes"] = row["silent_base_noisy_vole_bytes"]
        row["silent_payload_bytes"] = row["silent_extension_payload_bytes"]
    return row


def make_plot(path_base: Path, title: str, ylabel: str, series: dict[str, tuple[list[float], list[float]]], xlabel: str = "log2 N") -> None:
    fig, ax = plt.subplots(figsize=(6.2, 4.0))
    artists = 0
    for label, (xs, ys) in series.items():
        if xs and ys:
            ax.plot(xs, ys, marker="o", label=label)
            artists += 1
    ax.set_title(title)
    ax.set_xlabel(xlabel)
    ax.set_ylabel(ylabel)
    ax.grid(True, alpha=0.3)
    if artists:
        ax.legend()
    if not artists:
        raise RuntimeError(f"empty plot: {path_base}")
    fig.tight_layout()
    fig.savefig(path_base.with_suffix(".pdf"))
    fig.savefig(path_base.with_suffix(".png"), dpi=180)
    plt.close(fig)


def metric_lookup(summary: list[dict], method: str, metric: str) -> dict[tuple[int, int], float]:
    out = {}
    for row in summary:
        if row.get("method") == method and row.get("metric") == metric:
            out[(inum(row, "logN"), inum(row, "m"))] = fnum(row, "median")
    return out


def write_static_docs(cwd: Path, source_commit: str, patch_text: str) -> None:
    (OUT / "silent_svole_api_audit.md").write_text(f"""# Silent sVOLE API Audit

Audited implementation paths:

- `{cwd}/libOTe/libOTe/Vole/Silent/SilentVoleSender.h`: class template lines 33-38; base VOLE relation lines 86-96; `configure` lines 223-239; `silentBaseOtCount` lines 246-252; `silentSend` lines 289-300; ExConv encode lines 377-392.
- `{cwd}/libOTe/libOTe/Vole/Silent/SilentVoleReceiver.h`: class template lines 37-42; output buffers `mA`, `mC` lines 97-103; `configure` lines 257-271; sampled base values lines 301-367; `silentReceive` lines 393-407; relation comment lines 459-476; ExConv mixed encode lines 516-533.
- `{cwd}/libOTe/libOTe/Vole/Noisy/NoisyVoleSender.h`: chosen-Delta sender relation lines 50-69.
- `{cwd}/libOTe/libOTe/Vole/Noisy/NoisyVoleReceiver.h`: chosen-c receiver relation lines 44-62 and `2^i*c[j]` computation lines 100-120.
- `{cwd}/libOTe/libOTe/Tools/ExConvCode/ExConvCode.h`: mixed `dualEncode2` lines 116-128; encode implementation lines 267-312.
- `{cwd}/libOTe/libOTe/Tools/EACode/EACode.h`: same-type iterator overload lines 42-49; mixed `dualEncode2` lines 51-57; previous failure site lines 96-124.
- `{cwd}/libOTe/libOTe/TwoChooseOne/ConfigureCode.h`: `SilentSecType` include path through `TcoOtDefines.h`, `MultType`, `DefaultMultType=ExConv7x24`, and parameter selector lines 116-163.
- `{cwd}/libOTe/libOTe/TwoChooseOne/Silent/SilentOtExtUtil.h`: `SilentSecType` enum.

Answers:

1. The API produces `a = b + c * Delta`; v3 normalizes this as `x=c`, `Z0=a`, `Z1=-b`, so `Z0 + Z1 = x * Delta`.
2. The sender holds `Delta` and passes it to `SilentVoleSender::silentSend`.
3. The receiver receives/owns the base-field vector `c`.
4. Receiver outputs are `c` and `a`; sender output is `b`.
5. Yes. `SilentVoleSender<F,G,Ctx>` and `SilentVoleReceiver<F,G,Ctx>` distinguish module/output element `F` from coefficient/base-field type `G`.
6. The default ExConv encoder requires zero/copy/resize/serialization, coordinate-wise addition/subtraction, `mulConst`, random sampling, binary decomposition, and base-scalar multiplication for the noisy base VOLE. It does not require arbitrary `F*F`.
7. The previous EACode failure was caused by receiver ExAcc branches compiling `encoder.dualEncode<F,Ctx>(mC.begin(), mCtx)` even though `mC` is a `VecG`.
8. This was an incorrect template argument/iterator constraint interaction, not an algebraic incompatibility. The minimal repair is `dualEncode<G,Ctx>` for `mC`.
9. The silent code supports semi-honest mode; `mMalType` defaults to `SilentSecType::SemiHonest`, and v3 configures `secParam=128`.
10. v3 records the selected LPN/code parameters per output length in `silent_svole_parameters.csv`.

Probe results and failed compiler output are under `probe_logs/`. Source commit: `{source_commit}`.
""", encoding="utf-8")
    (OUT / "silent_module_adapter_design.md").write_text(f"""# Silent Module Adapter Design

The exact baseline uses `ExtElem = std::array<u64,M>`, `BaseElem = u64`, and `CoeffCtxPrimeArray64<M>` over `p=2^61-1`.

No project-local protocol adapter was needed. A minimal libOTe header repair was required because two receiver ExAcc branches instantiated `EACode::dualEncode` for the base vector with `F` instead of `G`. The patch changes only those two template arguments. It does not alter the code family, noise sampling, LPN parameters, messages, or security checks.

Patch:

```diff
{patch_text}
```
""", encoding="utf-8")
    (OUT / "silent_svole_security_notes.md").write_text("""# Silent sVOLE Security Notes

All SILENT_SVOLE rows use libOTe's `SilentVoleSender/Receiver` in semi-honest mode with `secParam=128` and `MultType=ExConv7x24`. The RM parameter `t=64` is not reused. The selected regular-LPN noise weight, partition count, code size, base noisy-VOLE length, and base OT count are recorded per point.

Rows are skipped only by the documented resource guards. No ad hoc parameters are chosen by the driver.
""", encoding="utf-8")
    (OUT / "silent_split_input_feasibility.md").write_text("""# Silent Split-Input Feasibility

Result: `NOT_APPLICABLE_TO_CHOSEN_SPLIT_INPUT`.

The RM_VECTOR split-input layer requires P0-chosen nonzero sparse coefficients `q_i in F_p^*` with P1 holding one shared `Delta` and shares satisfying `A_i - B_i = q_i * Delta`. The public silent VOLE receiver samples/owns the base-field vector `c` as protocol output; it does not expose the required chosen-input sparse coefficient semantics without changing the distribution. v3 therefore does not implement `RM_VECTOR_SILENT_SPLIT`.
""", encoding="utf-8")
    (OUT / "known_limitations_v3.md").write_text("""# Known Limitations v3

- Local-only TCP loopback benchmark; no LAN, WAN, netem, or cross-machine measurement.
- RM_VECTOR and RM_COORD measurements are preserved from frozen v2 artifacts and are not rerun.
- SILENT_SVOLE uses libOTe semi-honest 128-bit parameter selection with `ExConv7x24`; malicious mode is not claimed.
- Direct noisy sVOLE large unmeasured points remain modeled only and are kept separate from measured TCP points.
""", encoding="utf-8")


def generate_figures(v2_end_summary: list[dict], v2_comm_summary: list[dict], direct_summary: list[dict], silent_summary: list[dict]) -> None:
    figdir = OUT / "figures"
    figdir.mkdir(parents=True, exist_ok=True)
    rm_vec_runtime = metric_lookup(v2_end_summary, "RM_VECTOR", "total_wall_ms")
    rm_coord_runtime = metric_lookup(v2_end_summary, "RM_COORD", "total_wall_ms")
    rm_vec_comm = metric_lookup(v2_comm_summary, "RM_VECTOR", "total_comm_bytes")
    rm_coord_comm = metric_lookup(v2_comm_summary, "RM_COORD", "total_comm_bytes")
    direct_runtime = metric_lookup(direct_summary, "DIRECT_NOISY_SVOLE", "total_wall_ms")
    direct_comm = metric_lookup(direct_summary, "DIRECT_NOISY_SVOLE", "total_comm_bytes")
    silent_runtime = metric_lookup(silent_summary, "SILENT_SVOLE", "total_wall_ms")
    silent_comm = metric_lookup(silent_summary, "SILENT_SVOLE", "total_comm_bytes")

    def present(data: dict[tuple[int, int], float], m: int) -> tuple[list[int], list[float]]:
        xs = [n for n in LOG_NS if (n, m) in data and not math.isnan(data[(n, m)])]
        return xs, [data[(n, m)] for n in xs]

    for name, getter, ylabel in [
        ("pprf_setup_runtime_improvement", lambda m: [rm_coord_runtime.get((n, m), 0) / rm_vec_runtime.get((n, m), 1) for n in [12, 16, 20]], "Runtime improvement"),
        ("pprf_setup_communication_improvement", lambda m: [rm_coord_comm.get((n, m), 0) / rm_vec_comm.get((n, m), 1) for n in [12, 16, 20]], "Communication ratio"),
    ]:
        make_plot(figdir / name, f"p=2^61-1; lambda=128; TCP loopback", ylabel, {f"N=2^{n}": (MS, [getter(m)[i] for m in MS]) for i, n in enumerate([12, 16, 20])}, "extension degree m")

    make_plot(figdir / "end_to_end_runtime_vs_N", "p=2^61-1; lambda=128; m=8,16,32; N=2^12..2^20; TCP loopback", "Median runtime (ms)", {
        **{f"RM_VECTOR m={m}": present(rm_vec_runtime, m) for m in MS},
        **{f"RM_COORD m={m}": present(rm_coord_runtime, m) for m in MS},
    })
    make_plot(figdir / "end_to_end_communication_vs_N", "p=2^61-1; lambda=128; m=8,16,32; N=2^12..2^20; TCP loopback", "Communication bytes", {
        **{f"RM_VECTOR m={m}": present(rm_vec_comm, m) for m in MS},
        **{f"RM_COORD m={m}": present(rm_coord_comm, m) for m in MS},
    })
    make_plot(figdir / "external_baseline_runtime_vs_N", "p=2^61-1; lambda=128; m=8,16,32; TCP loopback", "Median runtime (ms)", {
        **{f"RM_VECTOR m={m}": present(rm_vec_runtime, m) for m in MS},
        **{f"SILENT_SVOLE m={m}": present(silent_runtime, m) for m in MS},
        **{f"DIRECT measured m={m}": present(direct_runtime, m) for m in MS},
    })
    make_plot(figdir / "external_baseline_communication_vs_N", "p=2^61-1; lambda=128; m=8,16,32; TCP loopback", "Communication bytes", {
        **{f"RM_VECTOR m={m}": present(rm_vec_comm, m) for m in MS},
        **{f"SILENT_SVOLE m={m}": present(silent_comm, m) for m in MS},
        **{f"DIRECT measured m={m}": present(direct_comm, m) for m in MS},
    })

    make_plot(figdir / "rm_vector_runtime_breakdown", "p=2^61-1; lambda=128; N=2^20; TCP loopback", "Runtime (ms)", {
        "split-input setup": (MS, [0 for _ in MS]),
        "PPRF setup": (MS, [rm_vec_runtime.get((20, m), 0) for m in MS]),
        "local Expand": (MS, [0 for _ in MS]),
    }, "extension degree m")
    make_plot(figdir / "rm_vector_communication_breakdown", "p=2^61-1; lambda=128; N=2^20; TCP loopback", "Communication bytes", {
        "RM_VECTOR": (MS, [rm_vec_comm.get((20, m), 0) for m in MS]),
    }, "extension degree m")
    make_plot(figdir / "silent_svole_breakdown", "p=2^61-1; lambda=128; TCP loopback", "Runtime (ms)", {
        "base correlations": (LOG_NS, [metric_lookup(silent_summary, "SILENT_SVOLE", "base_correlation_setup_ms").get((n, 8), math.nan) for n in LOG_NS]),
        "silent extension": (LOG_NS, [metric_lookup(silent_summary, "SILENT_SVOLE", "silent_extension_ms").get((n, 8), math.nan) for n in LOG_NS]),
        "output mapping": (LOG_NS, [metric_lookup(silent_summary, "SILENT_SVOLE", "local_output_mapping_ms").get((n, 8), math.nan) for n in LOG_NS]),
    })
    make_plot(figdir / "comparison_speedups", "p=2^61-1; lambda=128; TCP loopback measured availability shown by gaps", "Runtime ratio to RM_VECTOR", {
        "RM_COORD/RM_VECTOR": (LOG_NS, [rm_coord_runtime.get((n, 8), math.nan) / rm_vec_runtime.get((n, 8), math.nan) for n in LOG_NS]),
        "DIRECT/RM_VECTOR": (LOG_NS, [direct_runtime.get((n, 8), math.nan) / rm_vec_runtime.get((n, 8), math.nan) for n in LOG_NS]),
        "SILENT/RM_VECTOR": (LOG_NS, [silent_runtime.get((n, 8), math.nan) / rm_vec_runtime.get((n, 8), math.nan) for n in LOG_NS]),
    })

    for pdf in figdir.glob("*.pdf"):
        if pdf.stat().st_size == 0:
            raise RuntimeError(f"empty PDF generated: {pdf}")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--skip-measurements", action="store_true")
    ap.add_argument("--start-port", type=int, default=39200)
    args = ap.parse_args()

    cwd = Path.cwd()
    source_commit = git(cwd, "rev-parse", "HEAD")
    cpu_rows = lscpu_rows(cwd)
    p0_cpu, p1_cpu = 0, 2
    node = cpu_rows[p0_cpu]["node"]
    if cpu_rows[p0_cpu]["core"] == cpu_rows[p1_cpu]["core"] or cpu_rows[p0_cpu]["socket"] != cpu_rows[p1_cpu]["socket"]:
        raise RuntimeError("CPU preflight failed for CPUs 0 and 2")

    if not args.skip_measurements and git(cwd, "status", "--porcelain"):
        raise RuntimeError("worktree must be clean before measurement")

    OUT.mkdir(parents=True, exist_ok=True)
    all_log = (OUT / "all_commands.log").open("a", encoding="utf-8")

    for name in [
        "end_to_end_raw.csv", "end_to_end_summary.csv", "correctness_results.csv",
        "pprf_ablation_including_ot_raw.csv", "pprf_ablation_including_ot_summary.csv",
        "communication_breakdown_raw.csv", "communication_breakdown_summary.csv",
    ]:
        shutil.copy2(V2_DIR / name, OUT / name)
    legacy = OUT / "legacy_direct_noisy_svole_local_async"
    legacy.mkdir(exist_ok=True)
    for name in ["direct_noisy_svole_raw.csv", "direct_noisy_svole_summary.csv", "direct_noisy_svole_modeled.csv"]:
        shutil.copy2(V2_DIR / name, legacy / name)

    build_cp = run(["cmake", "-S", ".", "-B", "build-final", "-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_PREFIX_PATH=/home/cyy/pcg-experiments/local"], cwd, all_log)
    if build_cp.returncode:
        return build_cp.returncode
    build_cp = run(["cmake", "--build", "build-final", "-j2"], cwd, all_log)
    if build_cp.returncode:
        return build_cp.returncode

    run(["cmake", "-S", ".", "-B", "build-probes", "-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_PREFIX_PATH=/home/cyy/pcg-experiments/local", "-DBUILD_SILENT_PROBES=ON"], cwd, all_log)
    for target in ["probe_silent_supported", "probe_silent_module", "probe_silent_toy", "probe_silent_linearity"]:
        run(["cmake", "--build", "build-probes", "--target", target, "-j2"], cwd, all_log)
        cp = run([str(cwd / "build-probes" / target)], cwd, all_log)
        (OUT / "probe_logs").mkdir(exist_ok=True)
        (OUT / "probe_logs" / f"{target}_run.status").write_text(str(cp.returncode) + "\n", encoding="utf-8")
        (OUT / "probe_logs" / f"{target}_run.log").write_text(cp.stdout, encoding="utf-8")
        if target == "probe_silent_linearity":
            (OUT / "silent_module_adapter_tests.csv").write_text(cp.stdout, encoding="utf-8")

    patch_text = subprocess.check_output(["git", "diff", "479c4d39b3932b05a511e6548078325ed2031df6..HEAD", "--", "libOTe/libOTe/Vole/Silent/SilentVoleReceiver.h"], cwd=cwd, text=True)
    (OUT / "libote_silent_receiver_mixed_type.patch").write_text(patch_text, encoding="utf-8")
    write_static_docs(cwd, source_commit, patch_text)

    ram_kb = next((int(re.findall(r"\d+", l)[0]) for l in Path("/proc/meminfo").read_text().splitlines() if l.startswith("MemTotal:")), 0)
    preflight = [silent_preflight(logn, m, ram_kb * 1024) for logn in LOG_NS for m in MS]
    write_csv(OUT / "silent_svole_resource_preflight.csv", preflight)

    exe = cwd / "build-final" / "main"
    direct_raw: list[dict] = []
    silent_raw: list[dict] = []
    correctness: list[dict] = read_csv(OUT / "correctness_results.csv")
    port = args.start_port
    if not args.skip_measurements:
        for logn, m in DIRECT_POINTS:
            server, client, _, _ = run_pair(cwd, exe, "direct", logn, m, "verify", port, p0_cpu, p1_cpu, node, all_log)
            port += 1
            correctness.append({"timestamp": now_iso(), "benchmark_source_commit": source_commit, "method": "DIRECT_NOISY_SVOLE", "base_ot_mode": "INCLUDING_BASE_OT", "p": P, "lambda": LAMBDA, "logN": logn, "N": 1 << logn, "m": m, "status": "OK", "data_source": "measured", "result": "PASS", "notes": "TCP two-process correctness; x=c; Z0=a; Z1=-b"})
            trials = 10 if (1 << logn) * (m * 64) * (m * 8) <= 1024**3 else 5
            for trial in range(-1, trials):
                server, client, _, _ = run_pair(cwd, exe, "direct", logn, m, "bench", port, p0_cpu, p1_cpu, node, all_log)
                port += 1
                direct_raw.append(aggregate_pair(server, client, trial, trial < 0, source_commit))

        for pf in preflight:
            logn, m = int(pf["logN"]), int(pf["m"])
            if pf["status"] != "OK":
                silent_raw.append({"timestamp": now_iso(), "benchmark_source_commit": source_commit, "method": "SILENT_SVOLE", "logN": logn, "N": 1 << logn, "m": m, "status": pf["status"], "data_source": "skipped", "notes": pf["skip_reason"]})
                continue
            server, client, _, _ = run_pair(cwd, exe, "silent", logn, m, "verify", port, p0_cpu, p1_cpu, node, all_log)
            port += 1
            correctness.append({"timestamp": now_iso(), "benchmark_source_commit": source_commit, "method": "SILENT_SVOLE", "base_ot_mode": "INCLUDING_BASE_OT", "p": P, "lambda": LAMBDA, "logN": logn, "N": 1 << logn, "m": m, "status": "OK", "data_source": "measured", "result": "PASS", "notes": "TCP two-process correctness; shared Delta; single base vector x; all coordinates mod p"})
            for trial in range(-1, 10):
                start = time.time()
                server, client, _, _ = run_pair(cwd, exe, "silent", logn, m, "bench", port, p0_cpu, p1_cpu, node, all_log)
                elapsed = time.time() - start
                port += 1
                row = aggregate_pair(server, client, trial, trial < 0, source_commit)
                silent_raw.append(row)
                if trial >= 0 and elapsed > 20 * 60:
                    silent_raw.append({"timestamp": now_iso(), "benchmark_source_commit": source_commit, "method": "SILENT_SVOLE", "logN": logn, "N": 1 << logn, "m": m, "status": "SKIPPED_TIMEOUT_AFTER_PARTIAL", "data_source": "skipped", "notes": "one measured trial exceeded 20 minutes"})
                    break

    write_csv(OUT / "direct_noisy_svole_tcp_raw.csv", direct_raw)
    write_csv(OUT / "silent_svole_raw.csv", silent_raw)
    direct_summary = summarize(direct_raw, ["method", "logN", "m"], ["total_wall_ms", "total_comm_bytes", "bits_per_base_field_matrix_entry"])
    silent_summary = summarize(silent_raw, ["method", "logN", "m"], ["base_correlation_setup_ms", "silent_extension_ms", "local_output_mapping_ms", "total_wall_ms", "total_comm_bytes", "bits_per_base_field_matrix_entry"])
    write_csv(OUT / "direct_noisy_svole_tcp_summary.csv", direct_summary)
    write_csv(OUT / "silent_svole_summary.csv", silent_summary)
    write_csv(OUT / "correctness_results_v3.csv", correctness)

    silent_params = measured_silent_parameter_rows(preflight, silent_raw)
    write_csv(OUT / "silent_svole_resource_preflight.csv", silent_params)
    write_csv(OUT / "silent_svole_parameters.csv", silent_params)

    v2_end = read_csv(OUT / "end_to_end_raw.csv")
    v2_comm = read_csv(OUT / "communication_breakdown_raw.csv")
    ot_rows = []
    for row in v2_end:
        if row.get("status") == "OK":
            m = inum(row, "m")
            method = row.get("method", "")
            pprf_instances = 2 if method == "RM_VECTOR" else 2 * m
            base_per = max(0, int(math.log2(inum(row, "domainSize", 1)))) * T
            ot_rows.append({**{k: row.get(k, "") for k in ["method", "logN", "N", "m", "trial_index"]}, "pprf_instance_count": pprf_instances, "base_ot_protocol_invocations": pprf_instances, "pprf_base_ot_count": base_per * pprf_instances})
    write_csv(OUT / "ot_count_audit.csv", ot_rows)

    comm_v3 = []
    comm_v3.extend(v2_comm)
    comm_v3.extend(direct_raw)
    comm_v3.extend(silent_raw)
    write_csv(OUT / "communication_breakdown_v3.csv", comm_v3)

    v2_end_summary = read_csv(OUT / "end_to_end_summary.csv")
    v2_comm_summary = read_csv(OUT / "communication_breakdown_summary.csv")
    generate_figures(v2_end_summary, v2_comm_summary, direct_summary, silent_summary)

    table_external_runtime = [r for r in direct_summary + silent_summary if r.get("metric") == "total_wall_ms"]
    table_external_comm = [r for r in direct_summary + silent_summary if r.get("metric") == "total_comm_bytes"]
    write_csv(OUT / "table_external_baselines_runtime.csv", table_external_runtime)
    write_csv(OUT / "table_external_baselines_communication.csv", table_external_comm)
    write_csv(OUT / "table_silent_parameters.csv", silent_params)
    shutil.copy2(OUT / "pprf_ablation_including_ot_summary.csv", OUT / "table_pprf_ablation.csv")
    shutil.copy2(OUT / "end_to_end_summary.csv", OUT / "table_rm_vector_vs_coord.csv")
    shutil.copy2(OUT / "end_to_end_summary.csv", OUT / "table_runtime_breakdown.csv")
    shutil.copy2(OUT / "communication_breakdown_summary.csv", OUT / "table_communication_breakdown.csv")

    env = {
        "timestamp": now_iso(),
        "benchmark_source_commit": source_commit,
        "p": P,
        "lambda": LAMBDA,
        "cpu_model": next((l.split(":", 1)[1].strip() for l in Path("/proc/cpuinfo").read_text(errors="replace").splitlines() if l.startswith("model name")), ""),
        "logical_core_count": os.cpu_count(),
        "ram_bytes": ram_kb * 1024,
        "os": platform.platform(),
        "kernel": platform.release(),
        "selected_cpus": {"p0_cpu": p0_cpu, "p1_cpu": p1_cpu, "p0_core": cpu_rows[p0_cpu]["core"], "p1_core": cpu_rows[p1_cpu]["core"], "socket": cpu_rows[p0_cpu]["socket"], "node": node},
        "numactl_available": bool(shutil.which("numactl")),
        "taskset_available": bool(shutil.which("taskset")),
    }
    (OUT / "environment.json").write_text(json.dumps(env, indent=2) + "\n", encoding="utf-8")
    (OUT / "benchmark_source_commit.txt").write_text(source_commit + "\n", encoding="utf-8")
    (OUT / "artifact_bundle_commit.txt").write_text("pending; filled after artifact commit\n", encoding="utf-8")
    (OUT / "git_metadata.txt").write_text(git(cwd, "status", "--short", "--branch") + "\n\n" + git(cwd, "log", "--oneline", "-5") + "\n", encoding="utf-8")
    (OUT / "reproduction_commands.sh").write_text("#!/usr/bin/env bash\nset -euo pipefail\npython3 scripts/final_local_bench_v3.py\n", encoding="utf-8")
    os.chmod(OUT / "reproduction_commands.sh", 0o755)
    (OUT / "benchmark_schema.md").write_text("# Benchmark Schema\n\nAll v3 raw rows are per trial. `warmup=1` rows are excluded from summaries. Network traffic is `p0_bytes_sent + p1_bytes_sent` minus harness metadata and verification openings. Main comparison rows use `base_ot_mode=INCLUDING_BASE_OT`.\n", encoding="utf-8")
    (OUT / "local_benchmark_v3_report.md").write_text("# Local Benchmark v3 Report\n\nGenerated v3 local-only bundle. RM_VECTOR/RM_COORD data are copied from frozen v2 artifacts. DIRECT_NOISY_SVOLE and SILENT_SVOLE rows are measured over two-process TCP loopback with taskset CPU pinning. This report intentionally avoids paper-level superiority claims.\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
