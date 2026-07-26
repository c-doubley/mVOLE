#!/usr/bin/env python3
import argparse
import csv
import json
import os
import platform
import shutil
import statistics
import subprocess
import time
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "docs" / "final_opt_comparison_v1"
BUILD = ROOT / "build-final-opt"
EXE = BUILD / "main"
OLD = ROOT / "docs" / "split_opt_network_v1"
T = 64
LOG_NS = [12, 14, 16, 18, 20]
MS = [8, 16, 32]
ENCODER_LOG_NS = [12, 14, 16, 18, 20]
LOCAL_METHODS = [
    {"method": "RM_VECTOR_OPT", "kind": "rm", "setup": "coeff-ot", "backend": "vector"},
    {"method": "SILENT_SVOLE", "kind": "silent"},
]
NETWORK_PROFILES = [
    {"profile": "FAST", "rate": "10gbit", "loopback_rate": "10gbit", "delay": "100us", "target_bps": 10_000_000_000, "target_rtt_ms": 0.2, "rtt_max_ms": 0.5},
    {"profile": "WAN100", "rate": "100mbit", "loopback_rate": "140mbit", "delay": "20ms", "target_bps": 100_000_000, "target_rtt_ms": 40.0},
    {"profile": "WAN10", "rate": "10mbit", "loopback_rate": "12mbit", "delay": "20ms", "target_bps": 10_000_000, "target_rtt_ms": 40.0},
]
NETWORK_GRID = {
    "FAST": {"logN": [14, 16, 18, 20], "m": [8, 16, 32], "trials": 10},
    "WAN100": {"logN": [14, 18, 20], "m": [8, 16, 32], "trials": 5},
    "WAN10": {"logN": [14, 18, 20], "m": [8, 16], "trials": 5},
}
NETWORK_METHODS = [
    {"method": "RM_VECTOR_OPT", "kind": "rm", "setup": "coeff-ot", "backend": "vector"},
    {"method": "SILENT_SVOLE", "kind": "silent"},
    {"method": "RM_VECTOR_GENERIC_SPLIT", "kind": "rm", "setup": "split-input-vole", "backend": "vector"},
]


def now():
    return datetime.now(timezone.utc).isoformat()


def run(cmd, cwd=ROOT, log=None, timeout=None):
    cmd = [str(c) for c in cmd]
    if log:
        log.write("$ " + " ".join(cmd) + "\n")
        log.flush()
    start = time.time()
    cp = subprocess.run(cmd, cwd=cwd, text=True, stdout=subprocess.PIPE,
                        stderr=subprocess.STDOUT, timeout=timeout)
    if log:
        log.write(cp.stdout)
        log.write(f"[exit={cp.returncode} elapsed_s={time.time()-start:.6f}]\n\n")
        log.flush()
    return cp


def parse_kv_line(line):
    out = {}
    parts = line.strip().split()
    if not parts:
        return out
    out["tag"] = parts[0]
    out["status_token"] = parts[-1] if parts[-1] in ("PASS", "FAIL") else ""
    note_parts = []
    for part in parts[1:]:
        if note_parts:
            if part not in ("PASS", "FAIL"):
                note_parts.append(part)
            continue
        if part.startswith("notes="):
            note_parts.append(part.split("=", 1)[1].rstrip(","))
            continue
        if "=" in part:
            k, v = part.split("=", 1)
            out[k] = v.rstrip(",")
    if note_parts:
        out["notes"] = " ".join(note_parts)
    return out


def write_csv(path, rows):
    path.parent.mkdir(parents=True, exist_ok=True)
    rows = list(rows)
    fields = []
    for row in rows:
        for key in row:
            if key not in fields:
                fields.append(key)
    if not fields:
        fields = ["status"]
        rows = [{"status": "NO_ROWS"}]
    with path.open("w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)


def read_csv(path):
    if not path.exists():
        return []
    with path.open(newline="") as f:
        return list(csv.DictReader(f))


def fnum(row, key, default=0.0):
    try:
        return float(row.get(key, default) or default)
    except (TypeError, ValueError):
        return default


def inum(row, key, default=0):
    try:
        return int(float(row.get(key, default) or default))
    except (TypeError, ValueError):
        return default


def median(rows, key):
    vals = [fnum(r, key) for r in rows if r.get(key, "") != "" and not inum(r, "warmup")]
    return statistics.median(vals) if vals else 0.0


def summarize(rows, keys, metrics):
    groups = {}
    for row in rows:
        if inum(row, "warmup") or row.get("status") not in ("OK", "PASS", ""):
            continue
        groups.setdefault(tuple(row.get(k, "") for k in keys), []).append(row)
    out = []
    for key, group in sorted(groups.items()):
        base = {k: v for k, v in zip(keys, key)}
        for metric in metrics:
            vals = [fnum(r, metric) for r in group if r.get(metric, "") != ""]
            if not vals:
                continue
            out.append({
                **base,
                "metric": metric,
                "count": len(vals),
                "median": statistics.median(vals),
                "mean": statistics.mean(vals),
                "stddev": statistics.pstdev(vals) if len(vals) > 1 else 0,
                "min": min(vals),
                "max": max(vals),
            })
    return out


def kill_process(proc):
    if proc.poll() is not None:
        return
    proc.terminate()
    try:
        proc.wait(timeout=2)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait()


def wait_pair(server, client, timeout):
    deadline = None if timeout is None else time.time() + timeout
    while True:
        s_rc = server.poll()
        c_rc = client.poll()
        if s_rc is not None and c_rc is not None:
            return
        if s_rc not in (None, 0) and c_rc is None:
            kill_process(client)
            return
        if c_rc not in (None, 0) and s_rc is None:
            kill_process(server)
            return
        if deadline is not None and time.time() > deadline:
            kill_process(client)
            kill_process(server)
            raise subprocess.TimeoutExpired("server/client pair", timeout)
        time.sleep(0.05)


def run_pair(args0, args1, port, log, timeout=1800, server_prefix=None, client_prefix=None):
    server_cmd = [str(x) for x in (list(server_prefix or []) + ["taskset", "-c", "0", EXE] + args0)]
    client_cmd = [str(x) for x in (list(client_prefix or []) + ["taskset", "-c", "2", EXE] + args1)]
    log.write("$ " + " ".join(server_cmd) + " &\n")
    log.write("$ " + " ".join(client_cmd) + "\n")
    log.flush()
    server = subprocess.Popen(server_cmd, cwd=OUT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    time.sleep(0.2)
    client = subprocess.Popen(client_cmd, cwd=OUT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    wait_pair(server, client, timeout)
    server_out, _ = server.communicate()
    client_out, _ = client.communicate()
    log.write(server_out)
    log.write(client_out)
    log.write(f"[server_exit={server.returncode} client_exit={client.returncode} port={port}]\n\n")
    log.flush()
    if server.returncode or client.returncode:
        raise RuntimeError(f"pair failed server={server.returncode} client={client.returncode} port={port}")
    return [parse_kv_line(x) for x in (server_out + client_out).splitlines()
            if x.startswith(("SPLIT_BACKEND_TCP", "RMVOLE_NET_BENCH", "SILENT_SVOLE_TCP", "DIRECT_NOISY_SVOLE_TCP"))]


def aggregate_pair(role_rows, trial, warmup, source_commit, method, network_profile="LOCAL_TCP"):
    if len(role_rows) != 2:
        raise RuntimeError(f"expected 2 role rows, got {len(role_rows)}")
    p0 = next((r for r in role_rows if "server" in r.get("role", "")), role_rows[0])
    p1 = next((r for r in role_rows if r is not p0), role_rows[1])
    total_sent = inum(p0, "bytes_sent_by_role") + inum(p1, "bytes_sent_by_role")
    opening = inum(p0, "verification_opening_bytes") + inum(p1, "verification_opening_bytes")
    metadata = inum(p0, "harness_metadata_bytes")
    clean = max(0, total_sent - opening - metadata)
    p0_split = fnum(p0, "split_input_s", fnum(p0, "total_s"))
    p1_split = fnum(p1, "split_input_s", fnum(p1, "total_s"))
    p0_pprf = fnum(p0, "pprf_setup_s")
    p1_pprf = fnum(p1, "pprf_setup_s")
    p0_expand = fnum(p0, "expand_p0_s")
    p1_expand = fnum(p1, "expand_p1_s")
    total_wall = max(fnum(p0, "total_s"), fnum(p1, "total_s")) * 1000
    if p0.get("tag") == "SILENT_SVOLE_TCP":
        setup_wall = max(fnum(p0, "base_correlation_setup_s") + fnum(p0, "silent_extension_s"),
                         fnum(p1, "base_correlation_setup_s") + fnum(p1, "silent_extension_s")) * 1000
        split_wall = 0.0
        pprf_wall = 0.0
        p0_expand = fnum(p0, "local_output_mapping_s")
        p1_expand = fnum(p1, "local_output_mapping_s")
    else:
        split_wall = max(p0_split, p1_split) * 1000
        pprf_wall = max(p0_pprf, p1_pprf) * 1000
        setup_wall = split_wall + pprf_wall
    return {
        "timestamp": now(),
        "benchmark_source_commit": source_commit,
        "tag": p0.get("tag", ""),
        "method": method,
        "setup_mode": p0.get("setup_mode", ""),
        "backend": p0.get("backend", ""),
        "network_profile": network_profile,
        "network": p0.get("network", ""),
        "mode": p0.get("mode", ""),
        "trial_index": trial,
        "warmup": 1 if warmup else 0,
        "status": "OK" if p0.get("status_token") != "FAIL" and p1.get("status_token") != "FAIL" else "FAIL",
        "correctness": "PASS" if p0.get("status_token") == "PASS" and p1.get("status_token") == "PASS" else "",
        "logN": p0.get("logN", ""),
        "N": p0.get("N", ""),
        "t": p0.get("t", T),
        "m": p0.get("m", ""),
        "reps": p0.get("reps", ""),
        "p0_split_input_setup_ms": p0_split * 1000,
        "p1_split_input_setup_ms": p1_split * 1000,
        "split_input_phase_wall_ms": split_wall,
        "split_input_phase_wall_definition": "max(P0,P1)_same_phase" if split_wall else "",
        "p0_pprf_setup_ms": p0_pprf * 1000,
        "p1_pprf_setup_ms": p1_pprf * 1000,
        "pprf_key_setup_wall_ms": pprf_wall,
        "p0_expand_ms": p0_expand * 1000,
        "p1_expand_ms": p1_expand * 1000,
        "expand_wall_ms": max(p0_expand, p1_expand) * 1000,
        "setup_wall_ms": setup_wall,
        "total_wall_ms": total_wall,
        "base_correlation_setup_ms": max(fnum(p0, "base_correlation_setup_s"), fnum(p1, "base_correlation_setup_s")) * 1000,
        "silent_extension_ms": max(fnum(p0, "silent_extension_s"), fnum(p1, "silent_extension_s")) * 1000,
        "local_output_mapping_ms": max(fnum(p0, "local_output_mapping_s"), fnum(p1, "local_output_mapping_s")) * 1000,
        "p0_bytes_sent": inum(p0, "bytes_sent_by_role"),
        "p1_bytes_sent": inum(p1, "bytes_sent_by_role"),
        "total_comm_bytes": clean,
        "bytes_per_rm_vole_coordinate": clean / max(1, inum(p0, "N") * inum(p0, "m")),
        "split_input_bytes": max(inum(p0, "split_input_bytes"), inum(p1, "split_input_bytes")),
        "pprf_bytes": max(inum(p0, "pprf_bytes"), inum(p1, "pprf_bytes")),
        "split_input_base_ot_mode": p0.get("split_input_base_ot_mode") or p0.get("base_ot_mode") or p1.get("split_input_base_ot_mode") or p1.get("base_ot_mode", ""),
        "coeff_ot_encrypted_pair_count": max(inum(p0, "coeff_ot_encrypted_pair_count"), inum(p1, "coeff_ot_encrypted_pair_count")),
        "coeff_ot_encrypted_payload_send_calls": inum(p0, "coeff_ot_encrypted_payload_send_calls") + inum(p1, "coeff_ot_encrypted_payload_send_calls"),
        "coeff_ot_encrypted_payload_flush_count": inum(p0, "coeff_ot_encrypted_payload_flush_count") + inum(p1, "coeff_ot_encrypted_payload_flush_count"),
        "coeff_ot_encrypted_payload_bytes": max(inum(p0, "coeff_ot_encrypted_payload_bytes"), inum(p1, "coeff_ot_encrypted_payload_bytes")),
        "coeff_ot_encrypted_payload_bytes_per_send": max(inum(p0, "coeff_ot_encrypted_payload_bytes_per_send"), inum(p1, "coeff_ot_encrypted_payload_bytes_per_send")),
        "coeff_ot_protocol_round_count": max(inum(p0, "coeff_ot_protocol_round_count"), inum(p1, "coeff_ot_protocol_round_count")),
        "notes": (p0.get("notes", "") + "; " + p1.get("notes", "")).strip("; "),
    }


def build(log):
    cp = run(["cmake", "-S", ROOT, "-B", BUILD, "-DCMAKE_BUILD_TYPE=Release",
              "-DCMAKE_PREFIX_PATH=/home/cyy/pcg-experiments/local"], ROOT, log, timeout=120)
    if cp.returncode:
        raise RuntimeError("cmake configure failed")
    cp = run(["cmake", "--build", BUILD, "-j2"], ROOT, log, timeout=3600)
    if cp.returncode:
        raise RuntimeError("build failed")


def run_encoder(source_commit, log):
    rows = []
    for logn in ENCODER_LOG_NS:
        for m in MS:
            cp = run(["taskset", "-c", "0", EXE, "--ENCODER_ONLY_BENCH", "all", logn, m, 10],
                     OUT, log, timeout=1800)
            if cp.returncode:
                raise RuntimeError("encoder-only bench failed")
            for line in cp.stdout.splitlines():
                if line.startswith("ENCODER_ONLY_BENCH"):
                    row = parse_kv_line(line)
                    total_s = fnum(row, "total_s")
                    row.update({
                        "timestamp": now(),
                        "benchmark_source_commit": source_commit,
                        "status": "OK" if row.get("status_token") == "PASS" else "FAIL",
                        "base_field_encoder_ms": fnum(row, "base_field_wht_s") * 1000,
                        "coordinate_encoder_ms": fnum(row, "coordinate_wht_m_s") * 1000,
                        "pointwise_ops_ms": fnum(row, "pointwise_ops_s") * 1000,
                        "output_materialization_ms": fnum(row, "output_materialization_s") * 1000,
                        "total_ms": total_s * 1000,
                    })
                    rows.append(row)
    write_csv(OUT / "encoder_only_comparison_v2.csv", rows)
    return rows


def run_split_coeff(source_commit, log, start_port):
    rows = []
    port = start_port
    for backend in ("coeff-ot", "coeff-ot-precomputed"):
        for m in MS:
            rows.append(aggregate_pair(run_pair(
                ["--SPLIT_OPT_BENCH", "server", "0.0.0.0", port, backend, m, 1, "bench"],
                ["--SPLIT_OPT_BENCH", "client", "127.0.0.1", port, backend, m, 1, "bench"],
                port, log), -1, 1, source_commit, "SPLIT_COEFF_OT" if backend == "coeff-ot" else "SPLIT_COEFF_OT_PRECOMPUTED"))
            port += 1
            for trial in range(10):
                rows.append(aggregate_pair(run_pair(
                    ["--SPLIT_OPT_BENCH", "server", "0.0.0.0", port, backend, m, 1, "bench"],
                    ["--SPLIT_OPT_BENCH", "client", "127.0.0.1", port, backend, m, 1, "bench"],
                    port, log), trial, 0, source_commit, "SPLIT_COEFF_OT" if backend == "coeff-ot" else "SPLIT_COEFF_OT_PRECOMPUTED"))
                port += 1
    return port, rows


def run_local(source_commit, log, start_port, quick=False):
    rows = []
    port = start_port
    trials = 1 if quick else 10
    for logn in LOG_NS:
        for m in MS:
            for method in LOCAL_METHODS:
                if method["kind"] == "rm":
                    base_server = ["--RMVOLE_NET_BENCH", "server", "0.0.0.0", None, logn, T, m, 1, "bench", method["setup"], method["backend"]]
                    base_client = ["--RMVOLE_NET_BENCH", "client", "127.0.0.1", None, logn, T, m, 1, "bench", method["setup"], method["backend"]]
                else:
                    base_server = ["--RMVOLE_NET_BENCH", "silent-server", "0.0.0.0", None, logn, m, 1, "bench"]
                    base_client = ["--RMVOLE_NET_BENCH", "silent-client", "127.0.0.1", None, logn, m, 1, "bench"]
                for trial, warmup in [(-1, 1)] + [(i, 0) for i in range(trials)]:
                    args0 = [str(port) if x is None else str(x) for x in base_server]
                    args1 = [str(port) if x is None else str(x) for x in base_client]
                    rows.append(aggregate_pair(run_pair(args0, args1, port, log), trial, warmup, source_commit, method["method"]))
                    port += 1
    write_csv(OUT / "optimized_vs_silent_local.csv", rows)
    return port, rows


def tc_cleanup(log):
    cp = run(["tc", "qdisc", "del", "dev", "lo", "root"], ROOT, log, timeout=10)
    if cp.returncode and "No such file" not in cp.stdout and "Cannot delete qdisc" not in cp.stdout:
        log.write(f"[tc_cleanup_warning rc={cp.returncode}]\n")


def tc_setup(profile, log):
    tc_cleanup(log)
    cp = run(["tc", "qdisc", "replace", "dev", "lo", "root", "netem", "rate", profile.get("loopback_rate", profile["rate"]), "delay", profile["delay"]],
             ROOT, log, timeout=20)
    if cp.returncode:
        raise RuntimeError(f"tc setup failed: {cp.stdout.strip()}")


def qdisc(log):
    cp = run(["tc", "qdisc", "show", "dev", "lo"], ROOT, log, timeout=10)
    return cp.stdout.strip()


def ping20(profile, log):
    cp = run(["ping", "-c", "20", "-i", "0.05", "127.0.0.1"], ROOT, log, timeout=30)
    vals = []
    for line in cp.stdout.splitlines():
        if "time=" in line:
            try:
                vals.append(float(line.split("time=", 1)[1].split()[0]))
            except ValueError:
                pass
    med = statistics.median(vals) if vals else 0.0
    if profile["profile"] == "FAST":
        ok = med <= profile["rtt_max_ms"]
    else:
        ok = abs(med - profile["target_rtt_ms"]) <= 0.15 * profile["target_rtt_ms"]
    return vals, med, ok


def iperf_bidir(port, log):
    values = []
    for reverse in (False, True):
        server_cmd = ["iperf3", "-s", "-1", "-p", str(port)]
        client_cmd = ["iperf3", "-c", "127.0.0.1", "-p", str(port), "-t", "2", "-J"]
        if reverse:
            client_cmd.append("-R")
        log.write("$ " + " ".join(server_cmd) + " &\n")
        server = subprocess.Popen(server_cmd, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        time.sleep(0.3)
        client = run(client_cmd, ROOT, log, timeout=40)
        try:
            server_out, _ = server.communicate(timeout=5)
        except subprocess.TimeoutExpired:
            kill_process(server)
            server_out, _ = server.communicate()
        log.write(server_out)
        log.write(f"[iperf_server_exit={server.returncode} direction={'reverse' if reverse else 'forward'}]\n\n")
        log.flush()
        if client.returncode or server.returncode:
            raise RuntimeError("iperf3 bidirectional calibration failed")
        data = json.loads(client.stdout)
        end = data.get("end", {})
        values.append(float(end.get("sum_received", {}).get("bits_per_second", 0) or end.get("sum_sent", {}).get("bits_per_second", 0)))
        port += 1
    return values[0], values[1]


def run_network(source_commit, log, start_port, quick=False):
    rows = []
    validation = []
    port = start_port
    if not all(shutil.which(x) for x in ("tc", "ping", "iperf3")):
        validation.append({"profile": "ALL", "status": "SKIPPED_MISSING_NETWORK_TOOLS"})
        write_csv(OUT / "network_validation_v2.csv", validation)
        write_csv(OUT / "network_raw_v2.csv", rows)
        write_csv(OUT / "network_summary_v2.csv", rows)
        return port, rows, validation
    for profile in NETWORK_PROFILES:
        tc_setup(profile, log)
        qdisc_text = qdisc(log)
        pings, median_rtt, rtt_ok = ping20(profile, log)
        iperf_sent, iperf_received = iperf_bidir(port, log)
        port += 1
        throughput_ok = all(abs(x - profile["target_bps"]) <= 0.15 * profile["target_bps"] for x in (iperf_sent, iperf_received))
        status = "OK" if rtt_ok and throughput_ok else "REJECTED_OUT_OF_TOLERANCE"
        validation.append({
            "timestamp": now(),
            "profile": profile["profile"],
            "network_label": "emulated TCP network",
            "topology": "loopback_tc",
            "target_rtt_ms": profile["target_rtt_ms"],
            "median_rtt_ms": median_rtt,
            "rtt_within_15pct": 1 if rtt_ok else 0,
            "target_bits_per_second": profile["target_bps"],
            "iperf_forward_bits_per_second": iperf_sent,
            "iperf_reverse_bits_per_second": iperf_received,
            "throughput_within_15pct": 1 if throughput_ok else 0,
            "tc_qdisc": qdisc_text,
            "ping_samples_ms": " ".join(f"{x:.3f}" for x in pings),
            "status": status,
        })
        if status != "OK":
            continue
        grid = NETWORK_GRID[profile["profile"]]
        trials = 1 if quick else grid["trials"]
        for logn in grid["logN"]:
            for m in grid["m"]:
                for method in NETWORK_METHODS:
                    if method["kind"] == "rm":
                        base_server = ["--RMVOLE_NET_BENCH", "server", "0.0.0.0", None, logn, T, m, 1, "bench", method["setup"], method["backend"]]
                        base_client = ["--RMVOLE_NET_BENCH", "client", "127.0.0.1", None, logn, T, m, 1, "bench", method["setup"], method["backend"]]
                    else:
                        base_server = ["--RMVOLE_NET_BENCH", "silent-server", "0.0.0.0", None, logn, m, 1, "bench"]
                        base_client = ["--RMVOLE_NET_BENCH", "silent-client", "127.0.0.1", None, logn, m, 1, "bench"]
                    for trial, warmup in [(-1, 1)] + [(i, 0) for i in range(trials)]:
                        args0 = [str(port) if x is None else str(x) for x in base_server]
                        args1 = [str(port) if x is None else str(x) for x in base_client]
                        row = aggregate_pair(run_pair(args0, args1, port, log, timeout=3600),
                                             trial, warmup, source_commit, method["method"], profile["profile"])
                        row["network_label"] = "emulated TCP network"
                        rows.append(row)
                        port += 1
    tc_cleanup(log)
    write_csv(OUT / "network_validation_v2.csv", validation)
    write_csv(OUT / "network_raw_v2.csv", rows)
    write_csv(OUT / "network_summary_v2.csv", summarize(rows, ["network_profile", "network_label", "method", "logN", "m"],
                                                         ["total_wall_ms", "total_comm_bytes", "split_input_phase_wall_ms"]))
    write_csv(OUT / "network_profiles_v2.csv", [{
        "profile": p["profile"],
        "network_label": "emulated TCP network",
        "rate": p["rate"],
        "configured_loopback_tc_rate": p.get("loopback_rate", p["rate"]),
        "one_way_delay": p["delay"],
        "target_rtt_ms": p["target_rtt_ms"],
        "target_bits_per_second": p["target_bps"],
    } for p in NETWORK_PROFILES])
    return port, rows, validation


def coeff_interaction(split_rows, local_rows):
    rows = []
    for source, items in (("standalone_split", split_rows), ("integrated_rm", local_rows)):
        for row in items:
            if inum(row, "warmup") or row.get("method") not in ("SPLIT_COEFF_OT", "SPLIT_COEFF_OT_PRECOMPUTED", "RM_VECTOR_OPT", "RM_VECTOR_OPT_PRECOMPUTED_BASE_OT"):
                continue
            pairs = inum(row, "coeff_ot_encrypted_pair_count")
            sends = inum(row, "coeff_ot_encrypted_payload_send_calls")
            rows.append({
                "source": source,
                "method": row.get("method", ""),
                "logN": row.get("logN", ""),
                "N": row.get("N", ""),
                "m": row.get("m", ""),
                "trial_index": row.get("trial_index", ""),
                "base_ot_mode": row.get("split_input_base_ot_mode", ""),
                "encrypted_ot_message_pairs": pairs,
                "socket_send_call_count": sends,
                "flush_count": row.get("coeff_ot_encrypted_payload_flush_count", ""),
                "protocol_round_count": row.get("coeff_ot_protocol_round_count", ""),
                "encrypted_payload_bytes": row.get("coeff_ot_encrypted_payload_bytes", ""),
                "bytes_per_send": row.get("coeff_ot_encrypted_payload_bytes_per_send", ""),
                "batched_payload_check": "PASS" if pairs == 7808 and 0 <= sends <= 2 else "FAIL",
                "notes": "Harness-visible encrypted payload sends; libOTe internal OT-extension sends are not decomposed here.",
            })
    write_csv(OUT / "coeff_ot_interaction_audit.csv", rows)
    return rows


def phase_audit(split_rows, local_rows):
    audit = []
    coeff_split = [r for r in split_rows if r.get("method") == "SPLIT_COEFF_OT" and not inum(r, "warmup")]
    rm = [r for r in local_rows if r.get("method") == "RM_VECTOR_OPT" and not inum(r, "warmup")]
    for m in MS:
        split_m = [r for r in coeff_split if inum(r, "m") == m]
        split_med = median(split_m, "split_input_phase_wall_ms")
        vals_by_n = {}
        for logn in LOG_NS:
            rows = [r for r in rm if inum(r, "m") == m and inum(r, "logN") == logn]
            vals_by_n[logn] = median(rows, "split_input_phase_wall_ms")
            ratio = vals_by_n[logn] / split_med if split_med else 0
            audit.append(f"- m={m}, logN={logn}: integrated split median {vals_by_n[logn]:.3f} ms vs standalone {split_med:.3f} ms, ratio {ratio:.3f}, {'PASS' if split_med and abs(ratio - 1) <= 0.15 else 'FAIL'}.")
        nz = [v for v in vals_by_n.values() if v]
        spread = (max(nz) - min(nz)) / statistics.median(nz) if nz else 0
        audit.append(f"- m={m}: split-input median spread across N is {100*spread:.2f}%, {'PASS' if spread <= 0.15 else 'FAIL'}.")
    text = [
        "# Phase Timing Audit",
        "",
        "Raw rows in this directory preserve separate `p0_split_input_setup_ms` and `p1_split_input_setup_ms` timers. The phase wall time is `max(P0,P1)` only for phases where both roles execute the same concurrent protocol interval; those rows are marked `split_input_phase_wall_definition=max(P0,P1)_same_phase`.",
        "",
        "The optimized RM integrated split phase is compared against the standalone `SPLIT_COEFF_OT` benchmark. The coefficient-OT split input has fixed length `L=2t=128`, so material dependence on `N` is treated as a regression.",
        "",
        "## Checks",
        *audit,
    ]
    (OUT / "phase_timing_audit.md").write_text("\n".join(text) + "\n", encoding="utf-8")


def protocol_tex():
    text = r"""\section{Batched Bit-Decomposition OT Split-Input Multiplication}

This backend is a standard batched bit-decomposition OT multiplication backend for producing additive shares of products \(q_i \Delta\), where \(q_i \in \mathbb{F}_p\) is held by \(P_0\) and \(\Delta \in \mathbb{F}_p^m\) is held by \(P_1\). The benchmark uses \(p=2^{61}-1\), \(L=2t=128\), and \(61L=7808\) one-out-of-two OT messages in one batch.

\paragraph{Protocol.}
For each coefficient \(q_i\) and bit position \(k\), \(P_1\) samples a random vector \(r_{i,k}\in\mathbb{F}_p^m\). It defines OT messages
\[
M^0_{i,k}=r_{i,k},\qquad M^1_{i,k}=r_{i,k}+2^k\Delta.
\]
The messages are one-time padded using seeds delivered by a semi-honest chosen-message OT extension. \(P_0\) uses the bit \(q_{i,k}\) as its OT choice, decrypts \(M^{q_{i,k}}_{i,k}\), and sets
\[
A_i=\sum_k M^{q_{i,k}}_{i,k}.
\]
\(P_1\) sets \(B_i=-\sum_k r_{i,k}\). The shares \(A_i\) and \(B_i\) are then inserted into the split-input regular-PPRF backend.

\paragraph{Correctness.}
For every \(i\),
\[
A_i+B_i
=\sum_k \left(r_{i,k}+q_{i,k}2^k\Delta\right)-\sum_k r_{i,k}
=\left(\sum_k q_{i,k}2^k\right)\Delta
=q_i\Delta.
\]
Thus the reconstructed sparse PPRF payload is the same product that a centralized generator would have programmed.

\paragraph{Semi-Honest OT-Hybrid Security.}
In the OT-hybrid model, \(P_0\) receives exactly one of the two messages for each bit and learns no information about the unchosen branch beyond what follows from its output share. Each chosen message is masked by an OT-derived seed, and the encrypted message pairs are sent as one contiguous payload in the implementation. \(P_1\)'s view consists of its input \(\Delta\), its random masks, and OT sender state; OT privacy hides \(P_0\)'s choice bits. The simulated view for \(P_0\) samples additive shares consistent with \(q_i\Delta\), and the simulated view for \(P_1\) samples the same random masks and OT sender transcript. Therefore, against semi-honest parties, the backend realizes multiplication sharing in the OT-hybrid model.

\paragraph{Complexity.}
For \(L\) coefficients represented with \(\ell=\lceil\log_2 p\rceil=61\) bits and extension dimension \(m\), the backend uses \(L\ell\) OTs and sends \(2L\ell\) encrypted \(\mathbb{F}_p^m\) messages after OT extension setup. Computation is \(O(L\ell m)\) field additions plus OT-extension work, and communication is \(O(L\ell m\log p)\) bits for the encrypted payload, plus the OT-extension/base-OT traffic. In the measured configuration, \(L\ell=7808\) and the encrypted pair payload is serialized as a single large sender payload.
"""
    (OUT / "coeff_ot_split_protocol.tex").write_text(text, encoding="utf-8")


def publication_tables(local_rows, network_rows, encoder_rows):
    rows = []
    for name, items, keys, metrics in [
        ("local_runtime", local_rows, ["method", "logN", "m"], ["total_wall_ms", "total_comm_bytes"]),
        ("network_runtime", network_rows, ["network_profile", "method", "logN", "m"], ["total_wall_ms", "total_comm_bytes"]),
        ("encoder_only", encoder_rows, ["method", "logN", "m"], ["total_ms", "ns_per_rm_vole_coordinate", "ns_per_base_field_matrix_entry"]),
    ]:
        for row in summarize(items, keys, metrics):
            row["table"] = name
            rows.append(row)
    write_csv(OUT / "publication_candidate_tables.csv", rows)


def figures(local_rows, network_rows, encoder_rows):
    fig_dir = OUT / "figures"
    fig_dir.mkdir(parents=True, exist_ok=True)
    validation = []
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except Exception as exc:
        write_csv(OUT / "figure_pdf_validation.csv", [{"file": "ALL", "status": "SKIPPED", "notes": str(exc)}])
        return

    def save(name):
        pdf = fig_dir / f"{name}.pdf"
        png = fig_dir / f"{name}.png"
        plt.tight_layout()
        plt.savefig(pdf)
        plt.savefig(png, dpi=180)
        plt.close()
        validation.append({"file": str(pdf.relative_to(OUT)), "status": "OK" if pdf.read_bytes()[:4] == b"%PDF" else "BAD_PDF"})
        validation.append({"file": str(png.relative_to(OUT)), "status": "OK" if png.stat().st_size > 0 else "EMPTY"})

    summary = summarize(local_rows, ["method", "logN", "m"], ["total_wall_ms"])
    for m in MS:
        plt.figure(figsize=(7, 4))
        for method in ("RM_VECTOR_OPT", "SILENT_SVOLE"):
            ys = []
            for logn in LOG_NS:
                match = [r for r in summary if r.get("method") == method and inum(r, "logN") == logn and inum(r, "m") == m]
                ys.append(fnum(match[0], "median") if match else 0)
            plt.plot(LOG_NS, ys, marker="o", label=method)
        plt.xlabel("logN")
        plt.ylabel("median wall time (ms)")
        plt.title(f"Local TCP runtime, m={m}")
        plt.legend()
        save(f"local_runtime_m{m}")

    enc_summary = summarize(encoder_rows, ["method", "logN", "m"], ["ns_per_base_field_matrix_entry"])
    plt.figure(figsize=(7, 4))
    for method in ("WHT_ENCODER", "EXCONV7X24_ENCODER"):
        ys = []
        for logn in ENCODER_LOG_NS:
            match = [r for r in enc_summary if r.get("method") == method and inum(r, "logN") == logn and inum(r, "m") == 16]
            ys.append(fnum(match[0], "median") if match else 0)
        plt.plot(ENCODER_LOG_NS, ys, marker="o", label=method)
    plt.xlabel("logN")
    plt.ylabel("ns per base-field matrix entry")
    plt.title("Encoder-only comparison, m=16")
    plt.legend()
    save("encoder_only_m16")

    if network_rows:
        net_summary = summarize(network_rows, ["network_profile", "method", "logN", "m"], ["total_wall_ms"])
        plt.figure(figsize=(8, 4))
        labels, vals = [], []
        for profile in NETWORK_GRID:
            for method in ("RM_VECTOR_OPT", "SILENT_SVOLE", "RM_VECTOR_GENERIC_SPLIT"):
                match = [r for r in net_summary if r.get("network_profile") == profile and r.get("method") == method and inum(r, "logN") == 18 and inum(r, "m") == 16]
                if match:
                    labels.append(f"{profile}\n{method}")
                    vals.append(fnum(match[0], "median"))
        plt.bar(range(len(vals)), vals)
        plt.xticks(range(len(vals)), labels, rotation=35, ha="right")
        plt.ylabel("median wall time (ms)")
        plt.title("Emulated TCP network runtime")
        save("network_runtime_log18_m16")
    write_csv(OUT / "figure_pdf_validation.csv", validation)


def write_environment(source_commit):
    data = {
        "timestamp": now(),
        "source_commit": source_commit,
        "artifact_commit_parent": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
        "platform": platform.platform(),
        "python": platform.python_version(),
        "cpu_count": os.cpu_count(),
        "network_label": "emulated TCP network",
    }
    (OUT / "environment.json").write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--quick", action="store_true")
    parser.add_argument("--skip-network", action="store_true")
    parser.add_argument("--start-port", type=int, default=24000)
    args = parser.parse_args()

    OUT.mkdir(parents=True, exist_ok=True)
    source_commit = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    with (OUT / "all_commands.log").open("w", encoding="utf-8") as log:
        build(log)
        write_environment(source_commit)
        encoder_rows = run_encoder(source_commit, log)
        port, split_rows = run_split_coeff(source_commit, log, args.start_port)
        port, local_rows = run_local(source_commit, log, port, quick=args.quick)
        network_rows = []
        if not args.skip_network:
            port, network_rows, _ = run_network(source_commit, log, port, quick=args.quick)
        coeff_interaction(split_rows, local_rows)
        phase_audit(split_rows, local_rows)
        protocol_tex()
        publication_tables(local_rows, network_rows, encoder_rows)
        figures(local_rows, network_rows, encoder_rows)
        if OLD.exists():
            (OUT / "preserved_raw_data_note.md").write_text(
                "Existing split-only and optimized-RM raw data under docs/split_opt_network_v1 were preserved unchanged and treated as prior artifacts.\n",
                encoding="utf-8")


if __name__ == "__main__":
    main()
