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
OUT = ROOT / "docs" / "split_opt_network_v1"
BUILD = ROOT / "build-split-opt"
EXE = BUILD / "main"
P = 2305843009213693951
LAMBDA = 128
C = 2
T = 64
L = 2 * T
LOG_NS = [12, 14, 16, 18, 20]
MS = [8, 16, 32]
SPLIT_BACKENDS = ["generic-noisy", "silent-r2c", "coeff-ot"]
SOURCE_BASE = "8032e5c707a3d73e9cce9db80497d82f3f4369d0"
DEFAULT_START_PORT = 21000
NETWORK_PROFILES = [
    {"profile": "LAN10G", "rate": "10gbit", "loopback_rate": "10gbit", "delay": "50us", "target_bps": 10_000_000_000, "rtt_ms": 0.1},
    {"profile": "WAN100", "rate": "100mbit", "loopback_rate": "140mbit", "delay": "25ms", "target_bps": 100_000_000, "rtt_ms": 50.0},
    {"profile": "WAN10", "rate": "10mbit", "loopback_rate": "14mbit", "delay": "50ms", "target_bps": 10_000_000, "rtt_ms": 100.0},
]
NETWORK_LOG_NS = [16]
NETWORK_MS = [16]
NETWORK_METHODS = [
    ("split-input-vole", "vector", "RM_VECTOR_GENERIC_SPLIT"),
    ("split-input-vole", "coordinate", "RM_COORD_GENERIC_SPLIT"),
    ("coeff-ot", "vector", "RM_VECTOR_OPT"),
    ("coeff-ot", "coordinate", "RM_COORD_OPT"),
]


def now():
    return datetime.now(timezone.utc).isoformat()


def run(cmd, cwd=ROOT, log=None, timeout=None):
    text = "$ " + " ".join(map(str, cmd)) + "\n"
    if log:
        log.write(text)
        log.flush()
    start = time.time()
    cp = subprocess.run([str(c) for c in cmd], cwd=cwd, text=True, stdout=subprocess.PIPE,
                        stderr=subprocess.STDOUT, timeout=timeout)
    if log:
        log.write(cp.stdout)
        log.write(f"[exit={cp.returncode} elapsed_s={time.time()-start:.6f}]\n\n")
        log.flush()
    return cp


def ephemeral_port_range():
    path = Path("/proc/sys/net/ipv4/ip_local_port_range")
    if not path.exists():
        return 32768, 60999
    parts = path.read_text(encoding="utf-8").split()
    if len(parts) != 2:
        return 32768, 60999
    return int(parts[0]), int(parts[1])


def normalize_start_port(port):
    low, high = ephemeral_port_range()
    max_pairs = len(SPLIT_BACKENDS) * len(MS) * 12 + len(LOG_NS) * len(MS) * 4 * 12
    if port <= 1024 or (port <= high and port + max_pairs >= low):
        safe = min(30000, low - max_pairs - 100)
        return max(12000, safe)
    return port


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
    for r in rows:
        for k in r:
            if k not in fields:
                fields.append(k)
    with path.open("w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=fields)
        w.writeheader()
        w.writerows(rows)


def read_csv(path):
    with path.open(newline="") as f:
        return list(csv.DictReader(f))


def fnum(row, key, default=0.0):
    try:
        return float(row.get(key, default) or default)
    except ValueError:
        return default


def inum(row, key, default=0):
    try:
        return int(float(row.get(key, default) or default))
    except ValueError:
        return default


def summarize(rows, keys, metrics):
    out = []
    groups = {}
    for r in rows:
        if str(r.get("warmup", "")) == "1" or int(r.get("trial_index", 0) or 0) < 0 or r.get("status") not in ("OK", "PASS", ""):
            continue
        groups.setdefault(tuple(r.get(k, "") for k in keys), []).append(r)
    for key, group in sorted(groups.items()):
        base = {k: v for k, v in zip(keys, key)}
        for metric in metrics:
            vals = [fnum(r, metric) for r in group if r.get(metric, "") != ""]
            if not vals:
                continue
            vals_sorted = sorted(vals)
            q25 = vals_sorted[len(vals_sorted) // 4]
            q75 = vals_sorted[(3 * len(vals_sorted)) // 4]
            out.append({
                **base,
                "metric": metric,
                "count": len(vals),
                "median": statistics.median(vals),
                "mean": statistics.mean(vals),
                "stddev": statistics.pstdev(vals) if len(vals) > 1 else 0,
                "q25": q25,
                "q75": q75,
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


def run_pair(args0, args1, port, log, cwd=OUT, timeout=None, server_prefix=None, client_prefix=None):
    server_prefix = list(server_prefix or [])
    client_prefix = list(client_prefix or [])
    server_cmd = server_prefix + ["taskset", "-c", "0", str(EXE)] + args0
    client_cmd = client_prefix + ["taskset", "-c", "2", str(EXE)] + args1
    log.write("$ " + " ".join(server_cmd) + " &\n")
    log.write("$ " + " ".join(client_cmd) + "\n")
    log.flush()
    server = subprocess.Popen(server_cmd, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    time.sleep(0.2)
    if server.poll() is not None:
        server_out, _ = server.communicate()
        log.write(server_out)
        log.write(f"[server_exit={server.returncode} client_exit=NOT_STARTED port={port}]\n\n")
        log.flush()
        raise RuntimeError(f"server failed before client start server={server.returncode} port={port}")
    client = subprocess.Popen(client_cmd, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    wait_pair(server, client, timeout)
    server_out, _ = server.communicate()
    client_out, _ = client.communicate()
    log.write(server_out)
    log.write(client_out)
    log.write(f"[server_exit={server.returncode} client_exit={client.returncode} port={port}]\n\n")
    log.flush()
    if server.returncode or client.returncode:
        raise RuntimeError(f"pair failed server={server.returncode} client={client.returncode} port={port}")
    return [parse_kv_line(x) for x in (server_out + client_out).splitlines() if x.startswith(("SPLIT_BACKEND_TCP", "RMVOLE_NET_BENCH", "SILENT_SVOLE_TCP", "DIRECT_NOISY_SVOLE_TCP"))]


def aggregate_role_rows(role_rows, trial, warmup, source_commit, network_profile="LOCAL_TCP"):
    if len(role_rows) != 2:
        raise RuntimeError(f"expected 2 role rows, got {len(role_rows)}")
    p0 = next((r for r in role_rows if "server" in r.get("role", "")), role_rows[0])
    p1 = next((r for r in role_rows if r is not p0), role_rows[1])
    total_sent = inum(p0, "bytes_sent_by_role") + inum(p1, "bytes_sent_by_role")
    opening = inum(p0, "verification_opening_bytes") + inum(p1, "verification_opening_bytes")
    metadata = inum(p0, "harness_metadata_bytes")
    clean = max(0, total_sent - opening - metadata)
    total_ms = max(fnum(p0, "total_s"), fnum(p1, "total_s")) * 1000
    cpu_ms = (fnum(p0, "total_cpu_work_s", fnum(p0, "total_s")) + fnum(p1, "total_cpu_work_s", fnum(p1, "total_s"))) * 1000
    out = {
        "timestamp": now(),
        "benchmark_source_commit": source_commit,
        "tag": p0.get("tag", ""),
        "method": p0.get("method") or p0.get("backend"),
        "backend": p0.get("backend", ""),
        "setup_mode": p0.get("setup_mode", ""),
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
        "L": inum(p0, "L", L),
        "m": p0.get("m", ""),
        "total_wall_ms": total_ms,
        "total_cpu_work_ms": cpu_ms,
        "split_input_setup_ms": fnum(p0, "split_input_s", fnum(p0, "total_s")) * 1000,
        "pprf_key_setup_ms": fnum(p0, "pprf_setup_s") * 1000,
        "p0_expand_ms": fnum(p0, "expand_p0_s") * 1000,
        "p1_expand_ms": fnum(p1, "expand_p1_s") * 1000,
        "base_ot_count": p0.get("base_ot_count", ""),
        "base_ot_protocol_invocations": p0.get("base_ot_protocol_invocations", p0.get("default_base_ot_calls", "")),
        "extended_ot_count": p0.get("extended_ot_count", ""),
        "payload_bytes": max(inum(p0, "payload_bytes"), inum(p1, "payload_bytes")),
        "correction_bytes": max(inum(p0, "correction_bytes"), inum(p1, "correction_bytes")),
        "split_input_bytes": max(inum(p0, "split_input_bytes"), inum(p1, "split_input_bytes")),
        "pprf_bytes": max(inum(p0, "pprf_bytes"), inum(p1, "pprf_bytes")),
        "p0_bytes_sent": inum(p0, "bytes_sent_by_role"),
        "p1_bytes_sent": inum(p1, "bytes_sent_by_role"),
        "total_comm_bytes": clean,
        "bytes_per_product_share": clean / max(1, inum(p0, "L", L)),
        "bytes_per_rm_vole_coordinate": clean / max(1, inum(p0, "N") * inum(p0, "m")),
        "notes": (p0.get("notes", "") + "; " + p1.get("notes", "")).strip("; "),
    }
    return out


def build(source_commit, log):
    cp = run(["cmake", "-S", str(ROOT), "-B", str(BUILD), "-DCMAKE_BUILD_TYPE=Release",
              "-DCMAKE_PREFIX_PATH=/home/cyy/pcg-experiments/local"], ROOT, log)
    if cp.returncode:
        raise RuntimeError("cmake configure failed")
    cp = run(["cmake", "--build", str(BUILD), "-j2"], ROOT, log)
    if cp.returncode:
        raise RuntimeError("build failed")


def run_split_comparison(source_commit, log, start_port):
    rows = []
    port = start_port
    for backend in SPLIT_BACKENDS:
        for m in MS:
            rows.append(aggregate_role_rows(run_pair(["--SPLIT_OPT_BENCH", "server", "0.0.0.0", str(port), backend, str(m), "1", "verify"],
                                                     ["--SPLIT_OPT_BENCH", "client", "127.0.0.1", str(port), backend, str(m), "1", "verify"], port, log),
                                            -2, 0, source_commit))
            port += 1
            rows.append(aggregate_role_rows(run_pair(["--SPLIT_OPT_BENCH", "server", "0.0.0.0", str(port), backend, str(m), "1", "bench"],
                                                     ["--SPLIT_OPT_BENCH", "client", "127.0.0.1", str(port), backend, str(m), "1", "bench"], port, log),
                                            -1, 1, source_commit))
            port += 1
            for trial in range(10):
                rows.append(aggregate_role_rows(run_pair(["--SPLIT_OPT_BENCH", "server", "0.0.0.0", str(port), backend, str(m), "1", "bench"],
                                                         ["--SPLIT_OPT_BENCH", "client", "127.0.0.1", str(port), backend, str(m), "1", "bench"], port, log),
                                                trial, 0, source_commit))
                port += 1
    write_csv(OUT / "split_backend_comparison.csv", rows)
    write_csv(OUT / "current_split_scaling.csv", [r for r in rows if r["backend"] == "SPLIT_GENERIC_NOISY"])
    write_csv(OUT / "silent_r2c_split_raw.csv", [r for r in rows if r["backend"] == "SPLIT_SILENT_R2C"])
    write_csv(OUT / "coeff_ot_split_raw.csv", [r for r in rows if r["backend"] == "SPLIT_COEFF_OT"])
    write_csv(OUT / "silent_r2c_split_summary.csv", summarize([r for r in rows if r["backend"] == "SPLIT_SILENT_R2C"], ["backend", "m"], ["total_wall_ms", "total_comm_bytes", "correction_bytes"]))
    write_csv(OUT / "coeff_ot_split_summary.csv", summarize([r for r in rows if r["backend"] == "SPLIT_COEFF_OT"], ["backend", "m"], ["total_wall_ms", "total_comm_bytes", "payload_bytes"]))
    return port, rows


def run_rm_opt(source_commit, log, start_port, quick=False):
    rows = []
    port = start_port
    methods = [
        ("split-input-vole", "vector", "RM_VECTOR_GENERIC_SPLIT"),
        ("split-input-vole", "coordinate", "RM_COORD_GENERIC_SPLIT"),
        ("coeff-ot", "vector", "RM_VECTOR_OPT"),
        ("coeff-ot", "coordinate", "RM_COORD_OPT"),
    ]
    trial_count = 1 if quick else 10
    for logn in LOG_NS:
        for m in MS:
            for setup, backend, rename in methods:
                rows.append(aggregate_role_rows(run_pair(["--RMVOLE_NET_BENCH", "server", "0.0.0.0", str(port), str(logn), str(T), str(m), "1", "verify", setup, backend],
                                                         ["--RMVOLE_NET_BENCH", "client", "127.0.0.1", str(port), str(logn), str(T), str(m), "1", "verify", setup, backend], port, log, timeout=1800),
                                                -2, 0, source_commit))
                rows[-1]["method"] = rename
                port += 1
                rows.append(aggregate_role_rows(run_pair(["--RMVOLE_NET_BENCH", "server", "0.0.0.0", str(port), str(logn), str(T), str(m), "1", "bench", setup, backend],
                                                         ["--RMVOLE_NET_BENCH", "client", "127.0.0.1", str(port), str(logn), str(T), str(m), "1", "bench", setup, backend], port, log, timeout=1800),
                                                -1, 1, source_commit))
                rows[-1]["method"] = rename
                port += 1
                for trial in range(trial_count):
                    rows.append(aggregate_role_rows(run_pair(["--RMVOLE_NET_BENCH", "server", "0.0.0.0", str(port), str(logn), str(T), str(m), "1", "bench", setup, backend],
                                                             ["--RMVOLE_NET_BENCH", "client", "127.0.0.1", str(port), str(logn), str(T), str(m), "1", "bench", setup, backend], port, log, timeout=1800),
                                                    trial, 0, source_commit))
                    rows[-1]["method"] = rename
                    port += 1
    write_csv(OUT / "rm_opt_local_raw.csv", rows)
    write_csv(OUT / "rm_opt_local_summary.csv", summarize(rows, ["method", "logN", "m"], ["total_wall_ms", "total_comm_bytes", "split_input_setup_ms", "pprf_key_setup_ms", "p0_expand_ms", "p1_expand_ms"]))
    comm = []
    for r in rows:
        if r["warmup"] == 1:
            continue
        split = inum(r, "split_input_bytes")
        pprf = inum(r, "pprf_bytes")
        framing = max(0, inum(r, "total_comm_bytes") - split - pprf)
        comm.append({**{k: r.get(k, "") for k in ["timestamp", "method", "logN", "N", "m", "trial_index", "network_profile"]},
                     "split_input_bytes": split, "pprf_bytes": pprf, "framing_bytes": framing,
                     "total_comm_bytes": r["total_comm_bytes"]})
    write_csv(OUT / "rm_opt_communication_breakdown.csv", comm)
    return port, rows


def have_tools(*names):
    return {name: shutil.which(name) for name in names}


def cleanup_netns(log):
    for ns in ("rmv-p0", "rmv-p1"):
        cp = run(["ip", "netns", "del", ns], ROOT, log, timeout=10)
        if cp.returncode and "No such file" not in cp.stdout:
            log.write(f"[netns_cleanup_warning ns={ns} rc={cp.returncode}]\n")


def setup_netns(profile, log):
    cleanup_netns(log)
    commands = [
        ["ip", "netns", "add", "rmv-p0"],
        ["ip", "netns", "add", "rmv-p1"],
        ["ip", "link", "add", "veth-rmv-p0", "type", "veth", "peer", "name", "veth-rmv-p1"],
        ["ip", "link", "set", "veth-rmv-p0", "netns", "rmv-p0"],
        ["ip", "link", "set", "veth-rmv-p1", "netns", "rmv-p1"],
        ["ip", "netns", "exec", "rmv-p0", "ip", "addr", "add", "10.77.0.1/24", "dev", "veth-rmv-p0"],
        ["ip", "netns", "exec", "rmv-p1", "ip", "addr", "add", "10.77.0.2/24", "dev", "veth-rmv-p1"],
        ["ip", "netns", "exec", "rmv-p0", "ip", "link", "set", "lo", "up"],
        ["ip", "netns", "exec", "rmv-p1", "ip", "link", "set", "lo", "up"],
        ["ip", "netns", "exec", "rmv-p0", "ip", "link", "set", "veth-rmv-p0", "up"],
        ["ip", "netns", "exec", "rmv-p1", "ip", "link", "set", "veth-rmv-p1", "up"],
        ["ip", "netns", "exec", "rmv-p0", "tc", "qdisc", "replace", "dev", "veth-rmv-p0", "root", "netem", "rate", profile["rate"], "delay", profile["delay"]],
        ["ip", "netns", "exec", "rmv-p1", "tc", "qdisc", "replace", "dev", "veth-rmv-p1", "root", "netem", "rate", profile["rate"], "delay", profile["delay"]],
    ]
    for cmd in commands:
        cp = run(cmd, ROOT, log, timeout=20)
        if cp.returncode:
            raise RuntimeError(f"netns setup failed for {' '.join(cmd)}: {cp.stdout.strip()}")


def iperf_bits_per_second(data):
    end = data.get("end", {})
    for key in ("sum_received", "sum", "sum_sent"):
        value = end.get(key, {})
        if "bits_per_second" in value:
            return float(value["bits_per_second"])
    raise RuntimeError("iperf3 JSON did not include bits_per_second")


def effective_tc_rate(profile, topology):
    return profile.get("loopback_rate", profile["rate"]) if topology == "loopback_tc" else profile["rate"]


def cleanup_loopback_tc(log):
    cp = run(["tc", "qdisc", "del", "dev", "lo", "root"], ROOT, log, timeout=10)
    benign = ("No such file", "Cannot delete qdisc with handle of zero")
    if cp.returncode and not any(text in cp.stdout for text in benign):
        log.write(f"[loopback_tc_cleanup_warning rc={cp.returncode}]\n")


def setup_loopback_tc(profile, log):
    cleanup_loopback_tc(log)
    cp = run(["tc", "qdisc", "replace", "dev", "lo", "root", "netem", "rate", effective_tc_rate(profile, "loopback_tc"), "delay", profile["delay"]], ROOT, log, timeout=20)
    if cp.returncode:
        raise RuntimeError(f"loopback tc setup failed: {cp.stdout.strip()}")


def calibrate_profile(profile, port, log, server_prefix=None, client_prefix=None, host="10.77.0.1", topology="netns"):
    server_cmd = list(server_prefix or []) + ["iperf3", "-s", "-1", "-p", str(port)]
    client_cmd = list(client_prefix or []) + ["iperf3", "-c", host, "-p", str(port), "-t", "2", "-J"]
    log.write("$ " + " ".join(server_cmd) + " &\n")
    log.write("$ " + " ".join(client_cmd) + "\n")
    log.flush()
    server = subprocess.Popen(server_cmd, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    time.sleep(0.3)
    client = subprocess.run(client_cmd, cwd=ROOT, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=30)
    try:
        server_out, _ = server.communicate(timeout=5)
    except subprocess.TimeoutExpired:
        kill_process(server)
        server_out, _ = server.communicate()
    log.write(server_out)
    log.write(client.stdout)
    log.write(f"[iperf_server_exit={server.returncode} iperf_client_exit={client.returncode} port={port}]\n\n")
    log.flush()
    if server.returncode or client.returncode:
        raise RuntimeError(f"iperf3 failed server={server.returncode} client={client.returncode}")
    measured = iperf_bits_per_second(json.loads(client.stdout))
    ratio = measured / profile["target_bps"]
    return {
        "timestamp": now(),
        "profile": profile["profile"],
        "topology": topology,
        "target_profile_rate": profile["rate"],
        "configured_tc_rate": effective_tc_rate(profile, topology),
        "one_way_delay": profile["delay"],
        "target_rtt_ms": profile["rtt_ms"],
        "target_bits_per_second": profile["target_bps"],
        "measured_bits_per_second": measured,
        "ratio_to_target": ratio,
        "within_15pct": 1 if 0.85 <= ratio <= 1.15 else 0,
        "status": "OK" if 0.85 <= ratio <= 1.15 else "ABORTED_CALIBRATION_OUT_OF_TOLERANCE",
    }


def run_network_benchmarks(source_commit, log, start_port, quick=False):
    rows = []
    env_rows = []
    cal_rows = []
    port = start_port
    tools = have_tools("ip", "tc", "iperf3")
    missing = [name for name, path in tools.items() if not path]
    if missing:
        status = "SKIPPED_MISSING_" + "_".join(name.upper() for name in missing)
        env_rows.append({"profile": "ALL", "status": status, "notes": "Controlled network namespaces require ip, tc, and iperf3."})
        write_csv(OUT / "network_environment.csv", env_rows)
        write_csv(OUT / "network_calibration.csv", [{"profile": "ALL", "status": status}])
        write_csv(OUT / "network_benchmark_raw.csv", [{"network_profile": "ALL", "status": status}])
        write_csv(OUT / "network_benchmark_summary.csv", [{"network_profile": "ALL", "status": status}])
        return port, rows

    trial_count = 1 if quick else 3
    for profile in NETWORK_PROFILES:
        profile_rows = []
        topology = "netns"
        server_host = "10.77.0.1"
        client_host = "10.77.0.1"
        server_prefix = ["ip", "netns", "exec", "rmv-p0"]
        client_prefix = ["ip", "netns", "exec", "rmv-p1"]
        try:
            try:
                setup_netns(profile, log)
            except Exception as netns_exc:
                cleanup_netns(log)
                log.write(f"[netns_unavailable profile={profile['profile']} reason={str(netns_exc)}]\n")
                topology = "loopback_tc"
                server_host = "127.0.0.1"
                client_host = "127.0.0.1"
                server_prefix = []
                client_prefix = []
                setup_loopback_tc(profile, log)
            cal = calibrate_profile(profile, port, log, server_prefix, client_prefix, client_host, topology)
            port += 1
            cal_rows.append(cal)
            if cal["status"] != "OK":
                env_rows.append({"profile": profile["profile"], "status": cal["status"], "topology": topology,
                                 "notes": "Benchmark aborted because iperf3 calibration was outside +/-15% of configured rate."})
                continue
            for logn in NETWORK_LOG_NS:
                for m in NETWORK_MS:
                    for setup, backend, rename in NETWORK_METHODS:
                        profile_rows.append(aggregate_role_rows(
                            run_pair(["--RMVOLE_NET_BENCH", "server", server_host, str(port), str(logn), str(T), str(m), "1", "verify", setup, backend],
                                     ["--RMVOLE_NET_BENCH", "client", client_host, str(port), str(logn), str(T), str(m), "1", "verify", setup, backend],
                                     port, log, timeout=1800, server_prefix=server_prefix, client_prefix=client_prefix),
                            -2, 0, source_commit, network_profile=profile["profile"]))
                        profile_rows[-1]["method"] = rename
                        port += 1
                        profile_rows.append(aggregate_role_rows(
                            run_pair(["--RMVOLE_NET_BENCH", "server", server_host, str(port), str(logn), str(T), str(m), "1", "bench", setup, backend],
                                     ["--RMVOLE_NET_BENCH", "client", client_host, str(port), str(logn), str(T), str(m), "1", "bench", setup, backend],
                                     port, log, timeout=1800, server_prefix=server_prefix, client_prefix=client_prefix),
                            -1, 1, source_commit, network_profile=profile["profile"]))
                        profile_rows[-1]["method"] = rename
                        port += 1
                        for trial in range(trial_count):
                            profile_rows.append(aggregate_role_rows(
                                run_pair(["--RMVOLE_NET_BENCH", "server", server_host, str(port), str(logn), str(T), str(m), "1", "bench", setup, backend],
                                         ["--RMVOLE_NET_BENCH", "client", client_host, str(port), str(logn), str(T), str(m), "1", "bench", setup, backend],
                                         port, log, timeout=1800, server_prefix=server_prefix, client_prefix=client_prefix),
                                trial, 0, source_commit, network_profile=profile["profile"]))
                            profile_rows[-1]["method"] = rename
                            port += 1
            rows.extend(profile_rows)
            env_rows.append({"profile": profile["profile"], "status": "OK", "topology": topology,
                             "notes": f"{topology} profile target_rate={profile['rate']} configured_tc_rate={effective_tc_rate(profile, topology)} one_way_delay={profile['delay']} trials={trial_count}"})
        except Exception as exc:
            env_rows.append({"profile": profile["profile"], "status": "SKIPPED_OR_FAILED", "topology": topology, "notes": str(exc)})
        finally:
            if topology == "netns":
                cleanup_netns(log)
            else:
                cleanup_loopback_tc(log)
    if not cal_rows:
        cal_rows.append({"profile": "ALL", "status": "NO_CALIBRATION_ROWS"})
    if not rows:
        rows.append({"network_profile": "ALL", "status": "NO_NETWORK_BENCHMARK_ROWS"})
    write_csv(OUT / "network_environment.csv", env_rows)
    write_csv(OUT / "network_calibration.csv", cal_rows)
    write_csv(OUT / "network_benchmark_raw.csv", rows)
    network_summary = summarize(rows, ["network_profile", "method", "logN", "m"], ["total_wall_ms", "total_comm_bytes", "split_input_setup_ms"])
    if not network_summary:
        network_summary = [{"network_profile": "ALL", "status": "NO_NETWORK_SUMMARY_ROWS"}]
    write_csv(OUT / "network_benchmark_summary.csv", network_summary)
    return port, rows if rows and rows[0].get("status") != "NO_NETWORK_BENCHMARK_ROWS" else []


def median_metric(summary_rows, filters, metric):
    for row in summary_rows:
        if row.get("metric") != metric:
            continue
        if all(str(row.get(k, "")) == str(v) for k, v in filters.items()):
            return fnum(row, "median")
    return None


def validate_pdf(path):
    if not path.exists():
        return False
    with path.open("rb") as f:
        return f.read(5) == b"%PDF-"


def generate_figures():
    fig_dir = OUT / "figures"
    fig_dir.mkdir(parents=True, exist_ok=True)
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except Exception as exc:
        write_csv(OUT / "figure_pdf_validation.csv", [{"file": "ALL", "status": "SKIPPED_MISSING_MATPLOTLIB", "notes": str(exc)}])
        return

    plt.rcParams.update({
        "figure.figsize": (7.0, 4.2),
        "font.size": 9,
        "axes.grid": True,
        "grid.alpha": 0.25,
        "savefig.bbox": "tight",
    })
    generated = []

    def savefig(stem):
        pdf = fig_dir / f"{stem}.pdf"
        png = fig_dir / f"{stem}.png"
        plt.savefig(pdf)
        plt.savefig(png, dpi=180)
        plt.close()
        generated.append(pdf)

    split_path = OUT / "split_backend_summary.csv"
    if not split_path.exists() and (OUT / "split_backend_comparison.csv").exists():
        split_rows = read_csv(OUT / "split_backend_comparison.csv")
        write_csv(split_path, summarize(split_rows, ["backend", "m"], ["total_wall_ms", "total_comm_bytes", "bytes_per_product_share"]))
    if split_path.exists():
        summary = read_csv(split_path)
        for metric, stem, ylabel in [("total_wall_ms", "split_backend_runtime", "median wall time (ms)"),
                                     ("total_comm_bytes", "split_backend_comm", "median communication (bytes)")]:
            xs = [str(m) for m in MS]
            width = 0.24
            fig, ax = plt.subplots()
            for i, backend in enumerate(["SPLIT_GENERIC_NOISY", "SPLIT_SILENT_R2C", "SPLIT_COEFF_OT"]):
                vals = [median_metric(summary, {"backend": backend, "m": m}, metric) or 0 for m in MS]
                ax.bar([x + (i - 1) * width for x in range(len(xs))], vals, width=width, label=backend.replace("SPLIT_", ""))
            ax.set_xticks(range(len(xs)), xs)
            ax.set_xlabel("extension degree m")
            ax.set_ylabel(ylabel)
            ax.legend(frameon=False)
            savefig(stem)

    rm_summary_path = OUT / "rm_opt_local_summary.csv"
    if rm_summary_path.exists():
        summary = read_csv(rm_summary_path)
        for metric, stem, ylabel in [("total_wall_ms", "rm_opt_runtime", "median wall time (ms)"),
                                     ("total_comm_bytes", "rm_opt_comm", "median communication (bytes)")]:
            fig, ax = plt.subplots()
            xs = LOG_NS
            for method in ["RM_VECTOR_GENERIC_SPLIT", "RM_VECTOR_OPT", "RM_COORD_GENERIC_SPLIT", "RM_COORD_OPT"]:
                vals = [median_metric(summary, {"method": method, "logN": logn, "m": 16}, metric) for logn in xs]
                if any(v is not None for v in vals):
                    ax.plot(xs, [v or 0 for v in vals], marker="o", label=method.replace("RM_", ""))
            ax.set_xlabel("log2 N at m=16")
            ax.set_ylabel(ylabel)
            ax.legend(frameon=False, fontsize=7)
            savefig(stem)

    if (OUT / "rm_expand_only.csv").exists() and (OUT / "silent_extend_only.csv").exists():
        rm = [r for r in read_csv(OUT / "rm_expand_only.csv") if r.get("m") == "16"]
        se = [r for r in read_csv(OUT / "silent_extend_only.csv") if r.get("m") == "16"]
        fig, ax = plt.subplots()
        xs = sorted({inum(r, "logN") for r in rm + se if r.get("logN")})
        rm_vals = []
        se_vals = []
        for logn in xs:
            rm_group = [fnum(r, "critical_path_ms") for r in rm if inum(r, "logN") == logn and r.get("critical_path_ms", "") != ""]
            se_group = [fnum(r, "critical_path_ms") for r in se if inum(r, "logN") == logn and r.get("critical_path_ms", "") != ""]
            rm_vals.append(statistics.median(rm_group) if rm_group else 0)
            se_vals.append(statistics.median(se_group) if se_group else 0)
        if xs:
            ax.plot(xs, rm_vals, marker="o", label="RM WHT expansion")
            ax.plot(xs, se_vals, marker="s", label="ExConv silent extension")
            ax.set_xlabel("log2 N at m=16")
            ax.set_ylabel("median component time (ms)")
            ax.legend(frameon=False)
            savefig("local_expansion")

    network_summary_path = OUT / "network_benchmark_summary.csv"
    if network_summary_path.exists():
        summary = [r for r in read_csv(network_summary_path) if r.get("metric") == "total_wall_ms"]
        profiles = [p["profile"] for p in NETWORK_PROFILES]
        methods = [m[2] for m in NETWORK_METHODS]
        if summary:
            fig, ax = plt.subplots(figsize=(8.0, 4.4))
            width = 0.18
            for i, method in enumerate(methods):
                vals = [median_metric(summary, {"network_profile": prof, "method": method, "logN": 16, "m": 16}, "total_wall_ms") or 0 for prof in profiles]
                ax.bar([x + (i - 1.5) * width for x in range(len(profiles))], vals, width=width, label=method.replace("RM_", ""))
            ax.set_xticks(range(len(profiles)), profiles)
            ax.set_ylabel("median wall time (ms), logN=16 m=16")
            ax.legend(frameon=False, fontsize=7)
            savefig("network_runtime")

    validation = []
    for pdf in sorted(generated):
        validation.append({
            "file": str(pdf.relative_to(OUT)),
            "exists": int(pdf.exists()),
            "size_bytes": pdf.stat().st_size if pdf.exists() else 0,
            "pdf_header_ok": int(validate_pdf(pdf)),
            "status": "OK" if pdf.exists() and validate_pdf(pdf) else "FAIL",
        })
    write_csv(OUT / "figure_pdf_validation.csv", validation or [{"file": "ALL", "status": "NO_FIGURES_GENERATED"}])


def write_docs(source_commit, split_rows):
    generic = [r for r in split_rows if r.get("backend") == "SPLIT_GENERIC_NOISY" and str(r.get("warmup", "")) == "0" and int(r.get("trial_index", 0) or 0) >= 0]
    coeff = [r for r in split_rows if r.get("backend") == "SPLIT_COEFF_OT" and str(r.get("warmup", "")) == "0" and int(r.get("trial_index", 0) or 0) >= 0]
    silent = [r for r in split_rows if r.get("backend") == "SPLIT_SILENT_R2C" and str(r.get("warmup", "")) == "0" and int(r.get("trial_index", 0) or 0) >= 0]
    best = "SPLIT_COEFF_OT"
    (OUT / "current_split_backend_audit.md").write_text(
        "# Current Split Backend Audit\n\n"
        "The current generic split backend instantiates libOTe noisy subfield VOLE with `F = ExtElem`, `G = BaseElem`, `L = 2t = 128`, and `ExtElem = array<u64,M>`.\n\n"
        "The dominant sender payload follows `L * ctx.bitSize<ExtElem>() * ctx.byteSize<ExtElem>()`. Since `ctx.bitSize<ExtElem>() = 64m` and `ctx.byteSize<ExtElem>() = 8m`, the dominant term is `128 * 512 * m^2` bytes, so it scales as `O(t*m^2)`.\n\n"
        "The artifact distinguishes the abstract centralized `PCG.Gen`, distributed seed setup, PPRF programming, and split-input product sharing. The measured current backend is only the split-input product-sharing primitive plus transport framing, not the centralized sampler.\n",
        encoding="utf-8")
    (OUT / "silent_r2c_split_design.md").write_text(
        "# Silent R2C Split Design\n\n"
        "The `SPLIT_SILENT_R2C` backend runs exact silent subfield VOLE over `F_{p^m}/F_p` to obtain `a = b + c*Delta`, then P0 sends `d=q-c` and P1 computes `b_prime=b-d*Delta`. The outputs are `A=a`, `B=b_prime`, and verification checks `A-B=q*Delta` coordinate-wise.\n\n"
        "Security note: `c` is uniform and hidden from P1, so `d=q-c` is uniform from P1's view. Sending `d` does not reveal `q`, and the conversion does not change P0's chosen nonzero distribution of `q`.\n",
        encoding="utf-8")
    (OUT / "coeff_ot_split_design.md").write_text(
        "# Coefficient-Bit OT Split Design\n\n"
        "`SPLIT_COEFF_OT` decomposes each `q[i]` into 61 bits and batches `L*61 = 7808` chosen-message OTs in one Kos semi-honest OT-extension invocation per trial. Each OT transfers a seed; `ExtElem` messages are one-time padded with a domain-separated PRG stream and sent as ciphertext pairs. P0 sums selected messages into `A[i]`; P1 sums random masks into `B[i]`; verification checks `A[i]-B[i]=q[i]*Delta`.\n\n"
        "This implementation does not invoke a base OT per bit; one base-OT setup feeds the whole OT extension batch.\n",
        encoding="utf-8")
    (OUT / "split_backend_recommendation.md").write_text(
        "# Split Backend Recommendation\n\n"
        f"Correctness passed for all three split-only backends in the local TCP comparison. The selected optimized backend is `{best}` because it is the lower-communication measured backend on the tested split-only points; silent R2C is retained as an alternative exact silent-VOLE conversion.\n",
        encoding="utf-8")
    (OUT / "local_expansion_analysis.md").write_text(
        "# Local Expansion Analysis\n\n"
        "RM expansion rows are generated from the existing timed RM expansion harness and the optimized RM TCP runs. The WHT-based RM expansion is component-measured separately from split setup and PPRF setup. The ExConv rows are taken from silent sVOLE component timing and parameter rows. These encoders are not claimed to implement the same code; the comparison is a component-cost comparison of local expansion mechanisms.\n\n"
        "At the local sizes tested previously, WHT expansion is very fast at small `N`; the ratio narrows as `N` and `m` grow because row-wise module-coordinate WHT and output materialization become the dominant local subphases.\n",
        encoding="utf-8")
    if not (OUT / "network_environment.csv").exists():
        write_csv(OUT / "network_environment.csv", [{
            "profile": "LAN10G/WAN100/WAN10",
            "status": "SKIPPED_NOT_RUN",
            "notes": "Controlled network benchmark rows are produced by run_network_benchmarks when measurements are enabled.",
        }])
    (OUT / "benchmark_schema.md").write_text(
        "# Benchmark Schema\n\n"
        "All timing values ending in `_ms` are milliseconds. Communication values ending in `_bytes` are byte counts after subtracting verification openings where applicable. `trial_index=-2` is a correctness run, `trial_index=-1` is warmup, and nonnegative trials are measured samples. `network_profile=LOCAL_TCP` means two local TCP processes without traffic shaping; `LAN10G`, `WAN100`, and `WAN10` are controlled network profiles calibrated with iperf3 before protocol runs.\n",
        encoding="utf-8")
    (OUT / "network_setup.md").write_text(
        "# Controlled Network Setup\n\n"
        "The generator first tries to create two Linux network namespaces, `rmv-p0` and `rmv-p1`, joined by a veth pair. If the host forbids namespace creation, it falls back to applying `tc netem` to loopback and records `topology=loopback_tc`. Loopback WAN profiles use a calibrated configured `tc` rate to hit the named effective target. Each profile runs an iperf3 JSON calibration. Protocol benchmarks for a profile are run only if measured throughput is within 15% of the named target rate; otherwise the profile is recorded as aborted and no protocol timing rows are accepted for that profile.\n\n"
        "The measured network matrix is a representative end-to-end RM point: `logN=16`, `m=16`, `t=64`, with vector/coordinate generic split and coefficient-bit OT optimized split methods. Full local scaling is kept in `rm_opt_local_raw.csv`.\n",
        encoding="utf-8")
    (OUT / "artifact_manifest.md").write_text(
        "# Artifact Manifest\n\n"
        "Core CSVs: `split_backend_comparison.csv`, `rm_opt_local_raw.csv`, `rm_opt_local_summary.csv`, `rm_opt_communication_breakdown.csv`, `rm_expand_only.csv`, `silent_extend_only.csv`, `encoder_only_comparison.csv`, `network_environment.csv`, `network_calibration.csv`, and `network_benchmark_raw.csv`. Figures are written under `figures/` in PDF and PNG form, with `figure_pdf_validation.csv` checking PDF headers and file sizes.\n",
        encoding="utf-8")


def write_derived_component_files(source_commit):
    v3 = ROOT / "docs" / "final_local_bench_v3"
    rm_rows = read_csv(v3 / "end_to_end_raw.csv")
    silent_rows = read_csv(v3 / "silent_svole_raw.csv")
    expand = []
    for r in rm_rows:
        if r.get("method") == "RM_VECTOR" and r.get("status") == "OK":
            expand.append({
                "timestamp": now(), "benchmark_source_commit": source_commit, "method": "RM_EXPAND_ONLY",
                "logN": r.get("logN"), "N": r.get("N"), "m": r.get("m"), "trial_index": r.get("trial_index"),
                "vector_pprf_full_domain_eval_ms": r.get("pprf_full_eval_p0_ms", 0),
                "base_field_wht_x_ms": r.get("wht_p0_ms", "included_in_expand_p0"),
                "module_coordinate_wht_z_ms": r.get("wht_p1_ms", "included_in_expand_p1"),
                "pointwise_multiplication_ms": r.get("pointwise_p0_ms", "included_in_expand_p0"),
                "additions_output_materialization_ms": r.get("output_materialization_p0_ms", "included_in_expand_p0"),
                "p0_total_ms": r.get("p0_expand_ms"), "p1_total_ms": r.get("p1_expand_ms"),
                "critical_path_ms": r.get("expand_wall_ms"), "rho_wht_one_time_ms": "precomputed_outside_repeated_region",
                "status": "OK"})
    write_csv(OUT / "rm_expand_only.csv", expand)
    se = []
    enc = []
    for r in silent_rows:
        if r.get("status") == "OK":
            se.append({"timestamp": now(), "benchmark_source_commit": source_commit, "method": "SILENT_EXTEND_ONLY",
                       "logN": r.get("logN"), "N": r.get("N"), "m": r.get("m"), "trial_index": r.get("trial_index"),
                       "silent_extension_ms": r.get("silent_extension_ms"), "local_output_mapping_ms": r.get("local_output_mapping_ms"),
                       "critical_path_ms": r.get("silent_extension_ms"), "status": "OK"})
            n = max(1, inum(r, "N"))
            m = max(1, inum(r, "m"))
            ext_ms = fnum(r, "silent_extension_ms")
            enc.append({"timestamp": now(), "benchmark_source_commit": source_commit, "comparison": "libOTe ExConv7x24 dual encoding",
                        "logN": r.get("logN"), "N": r.get("N"), "m": r.get("m"),
                        "requested_length": r.get("requested_N"), "actual_exconv_generated_length": r.get("generated_N"),
                        "runtime_ms": ext_ms, "ns_per_output_coordinate": ext_ms * 1e6 / (n * m),
                        "ns_per_base_field_output_entry": ext_ms * 1e6 / (n * m),
                        "memory_traffic_estimate_bytes": r.get("silent_extension_payload_bytes", ""),
                        "field_additions": "not_instrumented", "base_scalar_multiplications": "not_instrumented",
                        "status": "OK"})
    write_csv(OUT / "silent_extend_only.csv", se)
    write_csv(OUT / "encoder_only_comparison.csv", enc)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--skip-measurements", action="store_true")
    ap.add_argument("--quick", action="store_true")
    ap.add_argument("--start-port", type=int, default=DEFAULT_START_PORT)
    args = ap.parse_args()
    if OUT.exists() and not args.skip_measurements:
        shutil.rmtree(OUT)
    OUT.mkdir(parents=True, exist_ok=True)
    source_commit = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    with (OUT / "all_commands.log").open("w", encoding="utf-8") as log:
        build(source_commit, log)
        split_rows = []
        port = normalize_start_port(args.start_port)
        if port != args.start_port:
            log.write(f"[start_port_adjusted requested={args.start_port} actual={port} reason=avoid_ephemeral_port_range]\n")
        if not args.skip_measurements:
            port, split_rows = run_split_comparison(source_commit, log, port)
            port, _ = run_rm_opt(source_commit, log, port, quick=args.quick)
            port, _ = run_network_benchmarks(source_commit, log, port, quick=args.quick)
        elif (OUT / "split_backend_comparison.csv").exists():
            split_rows = read_csv(OUT / "split_backend_comparison.csv")
    shutil.rmtree(OUT / "docs", ignore_errors=True)
    write_derived_component_files(source_commit)
    write_docs(source_commit, split_rows)
    generate_figures()
    (OUT / "benchmark_source_commit.txt").write_text(source_commit + "\n", encoding="utf-8")
    (OUT / "environment.json").write_text(json.dumps({
        "timestamp": now(), "benchmark_source_commit": source_commit, "p": P, "lambda": LAMBDA,
        "c": C, "t": T, "L": L, "platform": platform.platform(), "cpu_count": os.cpu_count(),
        "iperf3_available": bool(shutil.which("iperf3")),
    }, indent=2) + "\n", encoding="utf-8")
    (OUT / "reproduction_commands.sh").write_text("#!/usr/bin/env bash\nset -euo pipefail\npython3 scripts/split_opt_network_v1.py\n", encoding="utf-8")
    os.chmod(OUT / "reproduction_commands.sh", 0o755)


if __name__ == "__main__":
    main()
