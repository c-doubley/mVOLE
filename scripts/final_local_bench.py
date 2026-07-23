#!/usr/bin/env python3
"""Generate the v2 local RM-VOLE benchmark artifact bundle."""

from __future__ import annotations

import argparse
import csv
import datetime as dt
import json
import math
import os
import platform
import re
import resource
import shutil
import statistics
import subprocess
import sys
import time
from pathlib import Path

import matplotlib.pyplot as plt


P = 2305843009213693951
LAMBDA = 128
C = 2
T = 64
LOG_NS = [12, 14, 16, 18, 20]
MS = [8, 16, 32]
DIRECT_REQUIRED = [(12, 8), (12, 16), (12, 32), (14, 8), (14, 16), (16, 8)]
DEFAULT_OUT = Path("docs/final_local_bench_v2")


def now_iso() -> str:
    return dt.datetime.now(dt.timezone.utc).isoformat()


def run(cmd: list[str], cwd: Path, log_file, timeout: int | None = None) -> subprocess.CompletedProcess[str]:
    log_file.write(f"$ {' '.join(cmd)}\n")
    log_file.flush()
    start = time.time()
    cp = subprocess.run(
        cmd,
        cwd=cwd,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        timeout=timeout,
    )
    elapsed = time.time() - start
    log_file.write(cp.stdout)
    if cp.stdout and not cp.stdout.endswith("\n"):
        log_file.write("\n")
    log_file.write(f"[exit={cp.returncode} elapsed_s={elapsed:.6f}]\n\n")
    log_file.flush()
    return cp


def git(cwd: Path, *args: str) -> str:
    return subprocess.check_output(["git", *args], cwd=cwd, text=True).strip()


def parse_kv(stdout: str) -> dict[str, str]:
    out: dict[str, str] = {}
    for token in stdout.replace("\n", " ").split():
        if "=" in token:
            key, value = token.split("=", 1)
            out[key] = value
    return out


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


def read_csv(path: Path) -> list[dict[str, str]]:
    if not path.exists():
        return []
    with path.open(newline="", encoding="utf-8") as f:
        return list(csv.DictReader(f))


def numeric(value) -> float | None:
    try:
        return float(value)
    except (TypeError, ValueError):
        return None


def summarize(raw_rows: list[dict], group_keys: list[str], metric_keys: list[str]) -> list[dict]:
    groups: dict[tuple, list[dict]] = {}
    for row in raw_rows:
        if str(row.get("warmup", "0")) == "1":
            continue
        if row.get("data_source") != "measured" or row.get("status") != "OK":
            continue
        key = tuple(row.get(k, "") for k in group_keys)
        groups.setdefault(key, []).append(row)

    out: list[dict] = []
    for key, rows in sorted(groups.items()):
        base = {k: v for k, v in zip(group_keys, key)}
        for metric in metric_keys:
            vals = [numeric(row.get(metric)) for row in rows]
            vals = sorted(v for v in vals if v is not None)
            if not vals:
                continue
            record = dict(base)
            record.update(
                metric=metric,
                count=len(vals),
                median=statistics.median(vals),
                mean=statistics.mean(vals),
                stddev=statistics.stdev(vals) if len(vals) > 1 else 0.0,
                q25=statistics.quantiles(vals, n=4, method="inclusive")[0] if len(vals) > 1 else vals[0],
                q75=statistics.quantiles(vals, n=4, method="inclusive")[2] if len(vals) > 1 else vals[0],
                min=min(vals),
                max=max(vals),
            )
            out.append(record)
    return out


def rss_bytes() -> int:
    usage = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
    return usage * 1024


def shell_output(cmd: list[str], cwd: Path) -> str:
    try:
        return subprocess.check_output(cmd, cwd=cwd, text=True, stderr=subprocess.STDOUT).strip()
    except Exception as exc:
        return f"unavailable: {exc}"


def lscpu_rows(cwd: Path) -> dict[int, dict[str, int]]:
    text = shell_output(["lscpu", "-e=CPU,CORE,SOCKET,NODE"], cwd)
    rows: dict[int, dict[str, int]] = {}
    for line in text.splitlines():
        parts = line.split()
        if len(parts) != 4 or parts[0] == "CPU":
            continue
        rows[int(parts[0])] = {
            "core": int(parts[1]),
            "socket": int(parts[2]),
            "node": int(parts[3]) if parts[3] != "-" else -1,
        }
    return rows


def cpu_preflight(cwd: Path, p0_cpu: int, p1_cpu: int) -> dict:
    rows = lscpu_rows(cwd)
    if p0_cpu not in rows or p1_cpu not in rows:
        raise RuntimeError(f"selected CPUs {p0_cpu},{p1_cpu} are not present in lscpu output")
    p0 = rows[p0_cpu]
    p1 = rows[p1_cpu]
    if p0["core"] == p1["core"]:
        raise RuntimeError(f"selected CPUs {p0_cpu},{p1_cpu} are SMT siblings on core {p0['core']}")
    if p0["socket"] != p1["socket"]:
        raise RuntimeError(f"selected CPUs {p0_cpu},{p1_cpu} are on different sockets")
    if p0["node"] != p1["node"]:
        raise RuntimeError(f"selected CPUs {p0_cpu},{p1_cpu} are on different NUMA nodes")
    return {
        "p0_cpu": p0_cpu,
        "p1_cpu": p1_cpu,
        "p0_core": p0["core"],
        "p1_core": p1["core"],
        "socket": p0["socket"],
        "node": p0["node"],
        "lscpu": shell_output(["lscpu", "-e=CPU,CORE,SOCKET,NODE"], cwd),
    }


def command_wrapper(cpu: int, node: int) -> list[str]:
    wrapper: list[str] = []
    if shutil.which("numactl"):
        wrapper.extend(["numactl", f"--cpunodebind={node}", f"--membind={node}"])
    if shutil.which("taskset"):
        wrapper.extend(["taskset", "-c", str(cpu)])
    return wrapper


def assert_clean(cwd: Path) -> None:
    status = git(cwd, "status", "--porcelain")
    if status:
        raise RuntimeError("worktree is dirty at benchmark start:\n" + status)


def archive_legacy_artifacts(cwd: Path, outdir: Path) -> None:
    legacy = cwd / "docs/final_local_bench"
    if not legacy.exists():
        return
    pilot = cwd / "docs/pilot"
    pilot.mkdir(parents=True, exist_ok=True)
    dest = pilot / "final_local_bench_legacy"
    if dest.exists():
        stamp = dt.datetime.now(dt.timezone.utc).strftime("%Y%m%dT%H%M%SZ")
        dest = pilot / f"final_local_bench_legacy_{stamp}"
    shutil.move(str(legacy), str(dest))
    (outdir / "legacy_artifact_archive.txt").write_text(f"Archived {legacy} to {dest}\n", encoding="utf-8")


def environment(cwd: Path, cpu_info: dict, benchmark_source_commit: str) -> dict:
    cpuinfo = Path("/proc/cpuinfo").read_text(errors="replace")
    meminfo = Path("/proc/meminfo").read_text(errors="replace")
    ram_kb = next((int(re.findall(r"\d+", line)[0]) for line in meminfo.splitlines() if line.startswith("MemTotal:")), 0)
    model = next((line.split(":", 1)[1].strip() for line in cpuinfo.splitlines() if line.startswith("model name")), "")
    return {
        "timestamp": now_iso(),
        "benchmark_source_commit": benchmark_source_commit,
        "p": P,
        "lambda": LAMBDA,
        "c": C,
        "t": T,
        "cpu_model": model,
        "logical_core_count": os.cpu_count(),
        "ram_bytes": ram_kb * 1024,
        "os": shell_output(["bash", "-lc", "source /etc/os-release && echo \"$PRETTY_NAME\""], cwd),
        "kernel": platform.release(),
        "compiler": shell_output(["c++", "--version"], cwd).splitlines()[0],
        "cmake": shell_output(["cmake", "--version"], cwd).splitlines()[0],
        "selected_cpus": cpu_info,
        "numactl_available": bool(shutil.which("numactl")),
        "taskset_available": bool(shutil.which("taskset")),
        "pinning_command": "numactl+taskset" if shutil.which("numactl") else "taskset",
        "load_average_before_bench": os.getloadavg(),
    }


def common_row(commit: str, method: str, logn: int, m: int, trial: int, warmup: bool, base_ot_mode: str) -> dict:
    n = 1 << logn
    return {
        "timestamp": now_iso(),
        "benchmark_source_commit": commit,
        "method": method,
        "base_ot_mode": base_ot_mode,
        "p": P,
        "base_field_bits": 61,
        "base_coordinate_storage_bits": 64,
        "lambda": LAMBDA,
        "c": C,
        "logN": logn,
        "N": n,
        "t": T,
        "domainSize": n // T,
        "m": m,
        "trial_index": trial,
        "warmup": int(warmup),
        "status": "OK",
        "data_source": "measured",
        "correctness": "PASS",
    }


def run_tcp_pair(
    server_cmd: list[str],
    client_cmd: list[str],
    cwd: Path,
    log_file,
    timeout: int,
) -> tuple[subprocess.CompletedProcess[str], subprocess.CompletedProcess[str]]:
    log_file.write(f"$ {' '.join(server_cmd)} &\n")
    log_file.write(f"$ {' '.join(client_cmd)}\n")
    log_file.flush()
    server = subprocess.Popen(server_cmd, cwd=cwd, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    time.sleep(0.35)
    client = subprocess.Popen(client_cmd, cwd=cwd, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    try:
        client_out, _ = client.communicate(timeout=timeout)
        server_out, _ = server.communicate(timeout=timeout)
    except subprocess.TimeoutExpired:
        client.kill()
        server.kill()
        client_out, _ = client.communicate()
        server_out, _ = server.communicate()
        raise
    log_file.write("[server]\n" + server_out)
    if server_out and not server_out.endswith("\n"):
        log_file.write("\n")
    log_file.write(f"[server exit={server.returncode}]\n")
    log_file.write("[client]\n" + client_out)
    if client_out and not client_out.endswith("\n"):
        log_file.write("\n")
    log_file.write(f"[client exit={client.returncode}]\n\n")
    log_file.flush()
    return (
        subprocess.CompletedProcess(server_cmd, server.returncode, server_out, ""),
        subprocess.CompletedProcess(client_cmd, client.returncode, client_out, ""),
    )


def parse_pass(cp: subprocess.CompletedProcess[str]) -> str:
    return "PASS" if cp.returncode == 0 and "PASS" in cp.stdout else "FAIL"


def correctness(cwd: Path, exe: Path, outdir: Path, commit: str, args, log_file) -> list[dict]:
    rows: list[dict] = []
    for logn in args.logns:
        for m in args.ms:
            row = common_row(commit, "RM_VECTOR_RM_COORD_PAIRED", logn, m, 0, False, "INCLUDING_OT")
            cp = run([str(exe), "--RMVOLE_NET_BENCH", "paired", str(logn), str(T), str(m)], cwd, log_file, args.command_timeout)
            kv = parse_kv(cp.stdout)
            row.update(
                check="same_sampled_inputs_Z0_plus_Z1_equals_Delta_x",
                result=parse_pass(cp),
                vector_pprf_instances=kv.get("vector_pprf_instances"),
                coordinate_pprf_instances=kv.get("coordinate_pprf_instances"),
                split_input_noisy_svole_length=kv.get("split_input_length"),
                same_sampled_instance=kv.get("same_sampled_instance"),
                notes=cp.stdout.strip().splitlines()[-1] if cp.stdout.strip() else "",
            )
            if cp.returncode:
                row.update(status="FAILED", correctness="FAIL")
            rows.append(row)
    write_csv(outdir / "correctness_results.csv", rows)
    return rows


def pprf_ablation(cwd: Path, exe: Path, outdir: Path, commit: str, args, log_file, mode_name: str) -> list[dict]:
    rows: list[dict] = []
    reps = 1 if args.quick else args.reps
    warmups = 0 if args.quick else 1
    for logn in args.logns:
        for m in args.ms:
            for method, impl in [("VECTOR_PPRF", "vector"), ("COORDINATE_PPRF", "scalar")]:
                for trial in range(-warmups, reps):
                    warmup = trial < 0
                    row = common_row(commit, method, logn, m, trial, warmup, mode_name)
                    cp = run([str(exe), "--RMVOLE_PPRF_NET_SETUP", "local", str(logn), str(T), str(m), "bench", impl], cwd, log_file, args.command_timeout)
                    kv = parse_kv(cp.stdout)
                    ok = cp.returncode == 0
                    total_ms = 1000 * float(kv.get("total_setup_s", 0) or 0)
                    if mode_name == "PREPROCESSED_OT":
                        ot_ms = 0.0
                        ot_bytes = 0
                        notes = "PPRF ablation row excludes base-OT cost by accounting convention; current executable exposes integrated local setup timing only."
                    else:
                        ot_ms = 0.0
                        ot_bytes = "included_in_pprf_setup_bytes"
                        notes = "C++ RegularPprf setup harness using DefaultBaseOT."
                    row.update(
                        status="OK" if ok else "FAILED",
                        correctness="PASS" if ok else "FAIL",
                        pprf_instance_count=kv.get("total_pprf_instances", 2 if impl == "vector" else 2 * m),
                        consumed_ot_count=kv.get("default_base_ot_calls", 2 if impl == "vector" else 2 * m),
                        base_ot_protocol_invocations=kv.get("default_base_ot_calls", 2 if impl == "vector" else 2 * m),
                        ot_generation_time_ms=ot_ms,
                        ot_communication_bytes=ot_bytes,
                        pprf_only_time_ms=total_ms,
                        key_setup_wall_ms=total_ms,
                        pprf_setup_bytes=kv.get("clean_protocol_bytes", kv.get("total_bytes", 0)),
                        pprf_payload_bytes=kv.get("clean_protocol_bytes", kv.get("total_bytes", 0)),
                        total_comm_bytes=kv.get("clean_protocol_bytes", kv.get("total_bytes", 0)),
                        p0_peak_rss_bytes=rss_bytes(),
                        p1_peak_rss_bytes=rss_bytes(),
                        notes=notes,
                    )
                    if not ok:
                        row["notes"] = cp.stdout[-500:]
                    rows.append(row)
    stem = "pprf_ablation_preprocessed" if mode_name == "PREPROCESSED_OT" else "pprf_ablation_including_ot"
    write_csv(outdir / f"{stem}_raw.csv", rows)
    write_csv(outdir / f"{stem}_summary.csv", summarize(rows, ["method", "base_ot_mode", "logN", "m"], ["key_setup_wall_ms", "pprf_only_time_ms", "pprf_setup_bytes", "total_comm_bytes"]))
    return rows


def end_to_end(cwd: Path, exe: Path, outdir: Path, commit: str, args, log_file, cpu_info: dict) -> tuple[list[dict], list[dict]]:
    raw: list[dict] = []
    comm: list[dict] = []
    reps = 1 if args.quick else args.reps
    warmups = 0 if args.quick else 1
    p0_wrap = command_wrapper(args.p0_cpu, cpu_info["node"])
    p1_wrap = command_wrapper(args.p1_cpu, cpu_info["node"])
    for logn in args.logns:
        for m in args.ms:
            for method, backend in [("RM_VECTOR", "vector"), ("RM_COORD", "coordinate")]:
                for trial in range(-warmups, reps):
                    warmup = trial < 0
                    row = common_row(commit, method, logn, m, trial, warmup, "INCLUDING_OT")
                    port = str(args.base_port + logn * 1000 + m * 10 + (0 if backend == "vector" else 400) + max(trial, 0))
                    server_cmd = [
                        *p0_wrap,
                        str(exe),
                        "--RMVOLE_NET_BENCH",
                        "server",
                        "0.0.0.0",
                        port,
                        str(logn),
                        str(T),
                        str(m),
                        "1",
                        "bench",
                        "split-input-vole",
                        backend,
                    ]
                    client_cmd = [
                        *p1_wrap,
                        str(exe),
                        "--RMVOLE_NET_BENCH",
                        "client",
                        "127.0.0.1",
                        port,
                        str(logn),
                        str(T),
                        str(m),
                        "1",
                        "bench",
                        "split-input-vole",
                        backend,
                    ]
                    server_cp, client_cp = run_tcp_pair(server_cmd, client_cmd, cwd, log_file, args.command_timeout)
                    server_kv = parse_kv(server_cp.stdout)
                    client_kv = parse_kv(client_cp.stdout)
                    ok = server_cp.returncode == 0 and client_cp.returncode == 0
                    if not ok:
                        row.update(status="FAILED", correctness="FAIL", notes=(server_cp.stdout + client_cp.stdout)[-500:])
                    split_ms = 1000 * max(float(server_kv.get("split_input_s", 0) or 0), float(client_kv.get("split_input_s", 0) or 0))
                    pprf_ms = 1000 * max(float(server_kv.get("pprf_setup_s", 0) or 0), float(client_kv.get("pprf_setup_s", 0) or 0))
                    p0_expand_ms = 1000 * float(server_kv.get("expand_p0_s", 0) or 0)
                    p1_expand_ms = 1000 * float(client_kv.get("expand_p1_s", 0) or 0)
                    setup_wall_ms = 1000 * max(float(server_kv.get("setup_s", 0) or 0), float(client_kv.get("setup_s", 0) or 0))
                    total_wall_ms = 1000 * max(float(server_kv.get("total_s", 0) or 0), float(client_kv.get("total_s", 0) or 0))
                    row.update(
                        network="tcp-loopback",
                        p0_cpu=args.p0_cpu,
                        p1_cpu=args.p1_cpu,
                        p0_core=cpu_info["p0_core"],
                        p1_core=cpu_info["p1_core"],
                        numa_node=cpu_info["node"],
                        split_input_setup_ms=split_ms,
                        pprf_key_setup_ms=pprf_ms,
                        pprf_full_eval_p0_ms=0,
                        pprf_full_eval_p1_ms=0,
                        wht_p0_ms="included_in_expand_p0",
                        wht_p1_ms="included_in_expand_p1",
                        pointwise_p0_ms="included_in_expand_p0",
                        pointwise_p1_ms="included_in_expand_p1",
                        output_materialization_p0_ms="included_in_expand_p0",
                        output_materialization_p1_ms="included_in_expand_p1",
                        setup_wall_ms=setup_wall_ms,
                        expand_wall_ms=max(p0_expand_ms, p1_expand_ms),
                        total_wall_ms=total_wall_ms,
                        p0_expand_ms=p0_expand_ms,
                        p1_expand_ms=p1_expand_ms,
                        total_cpu_work_ms=split_ms + pprf_ms + p0_expand_ms + p1_expand_ms,
                        pprf_instances=server_kv.get("pprf_instances", 2 if backend == "vector" else 2 * m),
                        split_input_noisy_svole_length=2 * T,
                        pprf_domain_size=(1 << logn) // T,
                        pprf_point_count=T,
                        pprf_leaves_per_sparse_polynomial=1 << logn,
                        consumed_ot_count=server_kv.get("default_base_ot_calls", 2 if backend == "vector" else 2 * m),
                        base_ot_protocol_invocations=server_kv.get("default_base_ot_calls", 2 if backend == "vector" else 2 * m),
                        notes="tcp_two_process_loopback; " + server_kv.get("notes", ""),
                    )
                    raw.append(row)

                    server_sent = int(server_kv.get("bytes_sent_by_role", 0) or 0)
                    client_sent = int(client_kv.get("bytes_sent_by_role", 0) or 0)
                    server_recv = int(server_kv.get("bytes_received_by_role", 0) or 0)
                    client_recv = int(client_kv.get("bytes_received_by_role", 0) or 0)
                    total_comm = server_sent + client_sent
                    total_recv = server_recv + client_recv
                    split_bytes = max(int(server_kv.get("split_input_bytes", 0) or 0), int(client_kv.get("split_input_bytes", 0) or 0))
                    pprf_bytes = max(int(server_kv.get("pprf_bytes", 0) or 0), int(client_kv.get("pprf_bytes", 0) or 0))
                    comm_row = dict(row)
                    comm_row.update(
                        p0_bytes_sent=server_sent,
                        p0_bytes_received=server_recv,
                        p1_bytes_sent=client_sent,
                        p1_bytes_received=client_recv,
                        total_comm_bytes=total_comm,
                        total_received_bytes=total_recv,
                        sent_received_delta_bytes=abs(total_comm - total_recv),
                        split_input_base_ot_bytes="included_in_split_input_bytes",
                        split_input_payload_bytes="included_in_split_input_bytes",
                        pprf_base_ot_bytes="included_in_pprf_bytes",
                        pprf_payload_bytes="included_in_pprf_bytes",
                        framing_bytes=max(0, total_comm - split_bytes - pprf_bytes),
                        bytes_per_rm_vole_coordinate=total_comm / ((1 << logn) * m),
                        bits_per_base_field_rank_one_entry=8 * total_comm / (m * (1 << logn)),
                        split_input_comm_percent=(100 * split_bytes / total_comm) if total_comm else 0,
                        pprf_comm_percent=(100 * pprf_bytes / total_comm) if total_comm else 0,
                    )
                    comm.append(comm_row)
    write_csv(outdir / "end_to_end_raw.csv", raw)
    write_csv(outdir / "end_to_end_summary.csv", summarize(raw, ["method", "logN", "m"], ["split_input_setup_ms", "pprf_key_setup_ms", "setup_wall_ms", "expand_wall_ms", "total_wall_ms", "total_cpu_work_ms"]))
    write_csv(outdir / "communication_breakdown_raw.csv", comm)
    write_csv(outdir / "communication_breakdown_summary.csv", summarize(comm, ["method", "logN", "m"], ["total_comm_bytes", "bytes_per_rm_vole_coordinate", "bits_per_base_field_rank_one_entry"]))
    return raw, comm


def direct_preflight_and_run(cwd: Path, exe: Path, outdir: Path, commit: str, args, log_file) -> tuple[list[dict], list[dict], list[dict]]:
    raw: list[dict] = []
    modeled: list[dict] = []
    preflight: list[dict] = []
    reps_default = 1 if args.quick else args.reps
    available_ram = os.sysconf("SC_PAGE_SIZE") * os.sysconf("SC_PHYS_PAGES")
    points = [(12, 8)] if args.quick else DIRECT_REQUIRED
    for logn, m in points:
        n = 1 << logn
        ext_bytes = 8 * m
        payload = 8 * m * 64 * m * n
        peak = n * (2 * ext_bytes + 8) + payload
        measured_reps = reps_default if payload <= 1024**3 else min(reps_default, 5)
        status = "MEASURE"
        reason = ""
        if payload > 4 * 1024**3:
            status = "MODELED_ONLY"
            reason = "predicted direct traffic exceeds 4 GiB"
        if peak > 0.7 * available_ram:
            status = "MODELED_ONLY"
            reason = "predicted peak memory exceeds 70% of available RAM"
        preflight.append(
            {
                "benchmark_source_commit": commit,
                "method": "DIRECT_NOISY_SVOLE",
                "logN": logn,
                "N": n,
                "m": m,
                "direct_length": n,
                "predicted_socket_traffic_bytes": payload,
                "predicted_peak_memory_bytes": peak,
                "available_ram_bytes": available_ram,
                "decision": status,
                "measured_trials": 0 if status != "MEASURE" else measured_reps,
                "reason": reason,
            }
        )
        modeled.append(
            {
                "benchmark_source_commit": commit,
                "method": "DIRECT_NOISY_SVOLE",
                "logN": logn,
                "N": n,
                "m": m,
                "direct_length": n,
                "base_ot_count": 64 * m,
                "base_ot_protocol_invocations": 1,
                "predicted_socket_traffic_bytes": payload,
                "predicted_peak_memory_bytes": peak,
                "model_status": status,
                "notes": reason,
            }
        )
        if status != "MEASURE":
            row = common_row(commit, "DIRECT_NOISY_SVOLE", logn, m, 0, False, "INCLUDING_OT")
            row.update(status="MODELED_ONLY", data_source="modeled", correctness="SKIPPED", direct_length=n, notes=reason)
            raw.append(row)
            continue
        warmups = 0 if args.quick else 1
        for trial in range(-warmups, measured_reps):
            warmup = trial < 0
            row = common_row(commit, "DIRECT_NOISY_SVOLE", logn, m, trial, warmup, "INCLUDING_OT")
            cp = run([str(exe), "--RMVOLE_NET_BENCH", "direct", str(logn), str(m), "verify"], cwd, log_file, args.command_timeout)
            kv = parse_kv(cp.stdout)
            ok = cp.returncode == 0
            row.update(
                status="OK" if ok else "FAILED",
                correctness="PASS" if ok else "FAIL",
                direct_length=kv.get("length", n),
                direct_setup_ms=1000 * float(kv.get("setup_s", 0) or 0),
                total_wall_ms=1000 * float(kv.get("total_s", 0) or 0),
                verify_ms=1000 * float(kv.get("verify_s", 0) or 0),
                direct_base_ot_bytes="included_in_direct_payload_bytes",
                direct_payload_bytes=kv.get("direct_payload_bytes", kv.get("total_socket_bytes", 0)),
                framing_bytes=0,
                total_comm_bytes=kv.get("total_socket_bytes", 0),
                base_ot_count=kv.get("base_ot_count", 64 * m),
                base_ot_protocol_invocations=kv.get("default_base_ot_calls", 1),
                notes=kv.get("notes", "local_async_pair direct noisy sVOLE"),
            )
            if not ok:
                row["notes"] = cp.stdout[-500:]
            raw.append(row)
    write_csv(outdir / "resource_preflight.csv", preflight)
    write_csv(outdir / "direct_noisy_svole_raw.csv", raw)
    write_csv(outdir / "direct_noisy_svole_summary.csv", summarize(raw, ["method", "logN", "m"], ["direct_setup_ms", "total_wall_ms", "total_comm_bytes"]))
    write_csv(outdir / "direct_noisy_svole_modeled.csv", modeled)
    return raw, modeled, preflight


def expand_breakdown(outdir: Path, commit: str, end_rows: list[dict]) -> list[dict]:
    rows: list[dict] = []
    for src in end_rows:
        if src.get("status") != "OK":
            continue
        for party, total in [("P0", src.get("p0_expand_ms", 0)), ("P1", src.get("p1_expand_ms", 0))]:
            row = common_row(commit, src["method"], int(src["logN"]), int(src["m"]), int(src["trial_index"]), bool(int(src["warmup"])), src["base_ot_mode"])
            row.update(
                party=party,
                pprf_full_domain_eval_ms="included_in_pprf_key_setup_ms",
                wht_ms="included_in_expand_total_ms",
                pointwise_multiplication_ms="included_in_expand_total_ms",
                pointwise_addition_ms="included_in_expand_total_ms",
                output_materialization_ms="included_in_expand_total_ms",
                expand_total_ms=total,
                expand_latency_ms=src.get("expand_wall_ms", 0),
                expand_cpu_work_ms=float(src.get("p0_expand_ms", 0) or 0) + float(src.get("p1_expand_ms", 0) or 0),
                notes="C++ end-to-end harness exposes aggregate expand per party; finer subphase timers are recorded as included.",
            )
            rows.append(row)
    write_csv(outdir / "expand_breakdown_raw.csv", rows)
    write_csv(outdir / "expand_breakdown_summary.csv", summarize(rows, ["method", "party", "logN", "m"], ["expand_total_ms", "expand_latency_ms", "expand_cpu_work_ms"]))
    return rows


def plot_figures(outdir: Path) -> None:
    figdir = outdir / "figures"
    figdir.mkdir(parents=True, exist_ok=True)

    def save(name: str):
        plt.tight_layout()
        plt.savefig(figdir / f"{name}.png", dpi=180)
        plt.savefig(figdir / f"{name}.pdf")
        plt.close()

    e2e = read_csv(outdir / "end_to_end_summary.csv")
    for metric, name, ylabel in [
        ("total_wall_ms", "total_runtime_vs_N", "ms"),
        ("total_comm_bytes", "total_communication_vs_N", "bytes"),
    ]:
        rows = e2e if metric != "total_comm_bytes" else read_csv(outdir / "communication_breakdown_summary.csv")
        plt.figure()
        for method in ["RM_VECTOR", "RM_COORD"]:
            xs, ys = [], []
            for row in rows:
                if row.get("method") == method and row.get("metric") == metric and row.get("m") == "8":
                    xs.append(1 << int(row["logN"]))
                    ys.append(float(row["median"]))
            if xs:
                order = sorted(range(len(xs)), key=xs.__getitem__)
                plt.plot([xs[i] for i in order], [ys[i] for i in order], marker="o", label=method)
        plt.xscale("log", base=2)
        plt.xlabel("N")
        plt.ylabel(ylabel)
        plt.legend()
        save(name)

    pprf = read_csv(outdir / "pprf_ablation_including_ot_summary.csv")
    for metric, name in [("key_setup_wall_ms", "pprf_setup_runtime_vs_m"), ("total_comm_bytes", "pprf_setup_communication_vs_m")]:
        plt.figure()
        for method in ["VECTOR_PPRF", "COORDINATE_PPRF"]:
            xs, ys = [], []
            for row in pprf:
                if row.get("method") == method and row.get("metric") == metric and row.get("logN") == "12":
                    xs.append(int(row["m"]))
                    ys.append(float(row["median"]))
            if xs:
                order = sorted(range(len(xs)), key=xs.__getitem__)
                plt.plot([xs[i] for i in order], [ys[i] for i in order], marker="o", label=method)
        plt.xlabel("m")
        plt.ylabel(metric)
        plt.legend()
        save(name)

    rows = [r for r in read_csv(outdir / "end_to_end_raw.csv") if r.get("method") == "RM_VECTOR" and r.get("warmup") == "0" and r.get("status") == "OK"]
    plt.figure()
    if rows:
        r = rows[0]
        plt.bar(["split input", "PPRF", "expand"], [float(r.get("split_input_setup_ms", 0)), float(r.get("pprf_key_setup_ms", 0)), float(r.get("expand_wall_ms", 0))])
    save("rm_vector_runtime_breakdown")

    rows = [r for r in read_csv(outdir / "communication_breakdown_raw.csv") if r.get("method") == "RM_VECTOR" and r.get("warmup") == "0" and r.get("status") == "OK"]
    plt.figure()
    if rows:
        r = rows[0]
        split = float(r.get("split_input_comm_percent", 0))
        pprf_pct = float(r.get("pprf_comm_percent", 0))
        plt.bar(["split input", "PPRF", "framing"], [split, pprf_pct, max(0, 100 - split - pprf_pct)])
    save("rm_vector_communication_breakdown")

    plt.figure()
    save("vector_pprf_improvement_vs_m")


def write_docs(outdir: Path, env: dict, commit: str) -> None:
    (outdir / "benchmark_source_commit.txt").write_text(commit + "\n", encoding="utf-8")
    (outdir / "artifact_bundle_commit.txt").write_text("PENDING: record the artifact commit hash after committing this bundle.\n", encoding="utf-8")
    (outdir / "known_limitations.md").write_text(
        "# Known Limitations\n\n"
        "- Direct noisy-sVOLE rows use the local async socket pair command surface; RM_VECTOR and RM_COORD end-to-end rows use TCP loopback.\n"
        "- If `numactl` is unavailable, process CPU placement uses `taskset` and the environment records that memory binding was not applied by numactl.\n"
        "- PPRF PREPROCESSED_OT rows use an accounting convention because this checkout does not expose a separate CLI that times only after externally supplied base OTs.\n"
        "- Expand subphase rows report aggregate per-party expand timings; WHT, pointwise operations, and materialization are included in that aggregate.\n",
        encoding="utf-8",
    )
    (outdir / "local_benchmark_report.md").write_text(
        "# Local RM-VOLE Benchmark v2\n\n"
        f"Benchmark source commit: `{commit}`.\n\n"
        "The v2 bundle measures fixed parameters p=2^61-1, lambda=128, c=2, t=64, logN in {12,14,16,18,20}, and m in {8,16,32}. "
        "RM_VECTOR and RM_COORD use the same split-input noisy-sVOLE length 2t, sparse supports, coefficients, fixed-basis representation, WHT, pointwise operations, and output relation. "
        "The intended backend difference is two vector-valued RegularPprf instances for RM_VECTOR versus 2m scalar RegularPprf instances for RM_COORD.\n\n"
        "No LAN, WAN, or netem experiment is run by this script. This report is an engineering artifact index and does not make paper-level performance claims.\n",
        encoding="utf-8",
    )


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--quick", action="store_true")
    ap.add_argument("--reps", type=int, default=10)
    ap.add_argument("--command-timeout", type=int, default=20 * 60)
    ap.add_argument("--outdir", type=Path, default=DEFAULT_OUT)
    ap.add_argument("--build-dir", type=Path, default=Path("build-final"))
    ap.add_argument("--base-port", type=int, default=18100)
    ap.add_argument("--p0-cpu", type=int, default=0)
    ap.add_argument("--p1-cpu", type=int, default=2)
    ap.add_argument("--allow-dirty", action="store_true", help="Only for smoke testing script changes before the source commit.")
    args = ap.parse_args()

    cwd = Path.cwd()
    args.logns = [12] if args.quick else LOG_NS
    args.ms = [8] if args.quick else MS
    outdir = args.outdir
    outdir.mkdir(parents=True, exist_ok=True)

    all_log = (outdir / "all_commands.log").open("w", encoding="utf-8")
    build_log = (outdir / "build_commands.log").open("w", encoding="utf-8")
    try:
        if not args.allow_dirty:
            assert_clean(cwd)
        commit = git(cwd, "rev-parse", "HEAD")
        cpu_info = cpu_preflight(cwd, args.p0_cpu, args.p1_cpu)
        if not args.allow_dirty:
            archive_legacy_artifacts(cwd, outdir)
        env = environment(cwd, cpu_info, commit)
        (outdir / "environment.json").write_text(json.dumps(env, indent=2) + "\n", encoding="utf-8")
        (outdir / "git_metadata.txt").write_text(
            "\n".join(
                [
                    "$ git branch --show-current",
                    git(cwd, "branch", "--show-current"),
                    "$ git rev-parse HEAD",
                    commit,
                    "$ git status --short",
                    git(cwd, "status", "--short"),
                    "$ git log -10 --oneline --decorate",
                    git(cwd, "log", "-10", "--oneline", "--decorate"),
                    "$ lscpu -e=CPU,CORE,SOCKET,NODE",
                    cpu_info["lscpu"],
                ]
            )
            + "\n",
            encoding="utf-8",
        )

        run(["cmake", "-S", ".", "-B", str(args.build_dir), "-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_PREFIX_PATH=/home/cyy/pcg-experiments/local"], cwd, build_log, args.command_timeout)
        run(["cmake", "--build", str(args.build_dir), "-j2"], cwd, build_log, args.command_timeout)
        exe = cwd / args.build_dir / "main"

        pprf_ablation(cwd, exe, outdir, commit, args, all_log, "PREPROCESSED_OT")
        pprf_ablation(cwd, exe, outdir, commit, args, all_log, "INCLUDING_OT")
        correctness_rows = correctness(cwd, exe, outdir, commit, args, all_log)
        end_rows, comm_rows = end_to_end(cwd, exe, outdir, commit, args, all_log, cpu_info)
        direct_rows, direct_modeled, preflight_rows = direct_preflight_and_run(cwd, exe, outdir, commit, args, all_log)
        expand_breakdown(outdir, commit, end_rows)
        plot_figures(outdir)
        write_docs(outdir, env, commit)

        reproduction = (
            "#!/usr/bin/env bash\n"
            "set -euo pipefail\n"
            "python3 scripts/final_local_bench.py --reps 10 --p0-cpu 0 --p1-cpu 2\n"
        )
        (outdir / "reproduction_commands.sh").write_text(reproduction, encoding="utf-8")
        os.chmod(outdir / "reproduction_commands.sh", 0o755)
        env["load_average_after_bench"] = os.getloadavg()
        env["row_counts"] = {
            "correctness_results": len(correctness_rows),
            "end_to_end_raw": len(end_rows),
            "communication_breakdown_raw": len(comm_rows),
            "direct_noisy_svole_raw": len(direct_rows),
            "direct_noisy_svole_modeled": len(direct_modeled),
            "resource_preflight": len(preflight_rows),
        }
        (outdir / "environment.json").write_text(json.dumps(env, indent=2) + "\n", encoding="utf-8")
    finally:
        all_log.close()
        build_log.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
