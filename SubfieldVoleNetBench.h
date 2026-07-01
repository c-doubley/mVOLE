#pragma once

#include "libOTe/Base/BaseOT.h"
#include "libOTe/Tools/CoeffCtx.h"
#include "libOTe/Vole/Noisy/NoisyVoleReceiver.h"
#include "libOTe/Vole/Noisy/NoisyVoleSender.h"
#include "coproto/Socket/LocalAsyncSock.h"
#include "cryptoTools/Crypto/PRNG.h"
#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace osuCrypto
{
    struct SubfieldVoleNetBenchRow
    {
        std::string method = "libOTe-NoisyVole";
        std::string network = "loopback-LAN";
        u64 N = 0;
        u64 log2N = 0;
        u64 m = 0;
        std::string fieldType = "array<u8,m> over scalar u8";
        u64 securityBits = 128;
        double totalSeconds = 0;
        u64 senderBytes = 0;
        u64 receiverBytes = 0;
        u64 totalBytes = 0;
        u64 entries = 0;
        double entriesPerSecond = 0;
        double rankOnePerSecond = 0;
        bool ok = false;
        std::string notes;
    };

    inline double subfieldVoleMedian(std::vector<double> values)
    {
        if (values.empty())
            return 0;

        std::sort(values.begin(), values.end());
        auto mid = values.size() / 2;
        if (values.size() % 2)
            return values[mid];
        return (values[mid - 1] + values[mid]) / 2;
    }

    inline u64 subfieldVoleMedianU64(std::vector<u64> values)
    {
        if (values.empty())
            return 0;

        std::sort(values.begin(), values.end());
        return values[values.size() / 2];
    }

    inline std::string subfieldVoleCsvEscape(const std::string& value)
    {
        if (value.find_first_of(",\"\n") == std::string::npos)
            return value;

        std::string out = "\"";
        for (auto ch : value)
        {
            if (ch == '"')
                out += "\"\"";
            else
                out += ch;
        }
        out += "\"";
        return out;
    }

    inline void subfieldVoleAppendCsv(const SubfieldVoleNetBenchRow& row)
    {
        const auto path = std::filesystem::path("docs/subfield_vole_network_benchmark.csv");
        auto needsHeader = !std::filesystem::exists(path) || std::filesystem::file_size(path) == 0;
        std::ofstream out(path, std::ios::app);
        if (!out)
            throw std::runtime_error("failed to open docs/subfield_vole_network_benchmark.csv");

        if (needsHeader)
        {
            out << "method,network,N,log2N,m,field_type,security_bits,total_s,"
                << "comm_bytes_sent_by_sender,comm_bytes_sent_by_receiver,total_comm_bytes,"
                << "entries,entries_per_s,rank_one_per_s,ok,notes\n";
        }

        out << subfieldVoleCsvEscape(row.method) << ','
            << subfieldVoleCsvEscape(row.network) << ','
            << row.N << ','
            << row.log2N << ','
            << row.m << ','
            << subfieldVoleCsvEscape(row.fieldType) << ','
            << row.securityBits << ','
            << std::setprecision(12) << row.totalSeconds << ','
            << row.senderBytes << ','
            << row.receiverBytes << ','
            << row.totalBytes << ','
            << row.entries << ','
            << std::setprecision(12) << row.entriesPerSecond << ','
            << std::setprecision(12) << row.rankOnePerSecond << ','
            << (row.ok ? 1 : 0) << ','
            << subfieldVoleCsvEscape(row.notes) << '\n';
    }

    inline void subfieldVoleWriteNotes()
    {
        std::ofstream out("docs/subfield_vole_network_experiment_notes.md");
        if (!out)
            throw std::runtime_error("failed to open docs/subfield_vole_network_experiment_notes.md");

        out << "# Subfield VOLE Network Benchmark Notes\n\n"
            << "This Phase 6B harness benchmarks libOTe generic noisy subfield VOLE as an external networked baseline. "
            << "It does not modify or measure RM-VOLE network runtime.\n\n"
            << "## Implementation\n\n"
            << "- Harness: `SubfieldVoleNetBench.h`\n"
            << "- CLI: `./build/main --SUBFIELD_VOLE_NET_BENCH <log2N>`\n"
            << "- libOTe classes: `NoisyVoleSender<F,G,Ctx>` and `NoisyVoleReceiver<F,G,Ctx>`\n"
            << "- Base OT path: libOTe `DefaultBaseOT`\n"
            << "- Socket path: `coproto::LocalAsyncSocket::makePair()`\n"
            << "- Field instantiation: `F = std::array<u8, m>`, `G = u8`, `Ctx = CoeffCtxArray<u8, m>`\n\n"
            << "## Relation\n\n"
            << "libOTe noisy VOLE outputs receiver values `(a,c)` and sender values `(b,Delta)` such that:\n\n"
            << "```text\n"
            << "a = b + c * Delta\n"
            << "```\n\n"
            << "For the evaluated relation, the harness uses:\n\n"
            << "```text\n"
            << "xhat = c\n"
            << "Z1 = a\n"
            << "Z0 = -b\n"
            << "Z0 + Z1 = c * Delta\n"
            << "```\n\n"
            << "The harness verifies this relation after every trial.\n\n"
            << "## Network And Bytes\n\n"
            << "The benchmark is single-process loopback-LAN: both roles run in one process over real asynchronous "
            << "`coproto` sockets. Communication is measured from socket counters, not estimated: "
            << "`Socket::bytesSent()` on the sender and receiver endpoints after the protocol finishes. "
            << "The measurement includes the libOTe `DefaultBaseOT` messages used by the noisy VOLE call and "
            << "the noisy VOLE payload on the same socket abstraction.\n\n"
            << "The code is structured with separate sender and receiver role calls around one socket pair, so a later "
            << "Phase 6C split can replace `LocalAsyncSocket::makePair()` with `asioConnect()` listen/connect roles.\n\n"
            << "## Supported Parameters And Limitations\n\n"
            << "The noisy generic API supports compile-time coordinate dimensions in this harness for `m = 8, 16, 32, 64`. "
            << "However, noisy VOLE communication grows with the binary decomposition size of `Delta`; for "
            << "`std::array<u8,m>`, the dominant payload is about `8 * m^2 * N` bytes. The harness therefore "
            << "marks settings above a 256 MiB estimated noisy payload as unsupported by the benchmark guard.\n\n"
            << "This is a generic subfield-VOLE baseline, not an exact same-field comparison to RM-VOLE over odd-prime "
            << "`F_p`/`F_{p^m}`. The chosen `u8` coordinate-array context is useful for exercising the libOTe generic "
            << "`F != G` subfield API and real socket communication, but it should be described in the paper as a "
            << "networked generic subfield VOLE baseline only.\n\n"
            << "Silent subfield VOLE remains pending in this harness. The audited `SilentVole<F,G,Ctx>` API is the likely "
            << "next target, but it needs careful parameter and setup treatment before reporting.\n\n"
            << "## Paper Use\n\n"
            << "Use these numbers as an external networked baseline showing the cost of a real libOTe subfield VOLE channel. "
            << "Do not compare them as RM-VOLE LAN runtime. Current RM-VOLE PPRF measurements remain local prototype timings "
            << "until vector-valued PPRF setup is implemented as a real sender/receiver protocol.\n";
    }

    template<u64 M>
    SubfieldVoleNetBenchRow subfieldVoleRunNoisyTrial(u64 log2N)
    {
        using F = std::array<u8, M>;
        using G = u8;
        using Ctx = CoeffCtxArray<G, M>;
        using VecF = typename Ctx::template Vec<F>;
        using VecG = typename Ctx::template Vec<G>;

        auto N = 1ull << log2N;
        Ctx ctx;
        PRNG prng(sysRandomSeed());
        NoisyVoleReceiver<F, G, Ctx> receiver;
        NoisyVoleSender<F, G, Ctx> sender;
        DefaultBaseOT receiverBaseOt;
        DefaultBaseOT senderBaseOt;
        VecG c(N);
        VecF a(N), b(N);
        F delta{};

        prng.get(c.data(), c.size());
        ctx.fromBlock(delta, prng.get<block>());

        auto sockets = cp::LocalAsyncSocket::makePair();

        auto start = std::chrono::steady_clock::now();
        auto recvTask = receiver.receive(c, a, prng, receiverBaseOt, sockets[0], ctx);
        auto sendTask = sender.send(delta, b, prng, senderBaseOt, sockets[1], ctx);
        auto result = macoro::sync_wait(macoro::when_all_ready(std::move(recvTask), std::move(sendTask)));
        std::get<0>(result).result();
        std::get<1>(result).result();
        macoro::sync_wait(macoro::when_all_ready(sockets[0].flush(), sockets[1].flush()));
        auto end = std::chrono::steady_clock::now();

        SubfieldVoleNetBenchRow row;
        row.N = N;
        row.log2N = log2N;
        row.m = M;
        row.senderBytes = sockets[1].bytesSent();
        row.receiverBytes = sockets[0].bytesSent();
        row.totalBytes = row.senderBytes + row.receiverBytes;
        row.entries = M * N;
        row.totalSeconds = std::chrono::duration<double>(end - start).count();
        row.entriesPerSecond = row.totalSeconds > 0 ? static_cast<double>(row.entries) / row.totalSeconds : 0;
        row.rankOnePerSecond = row.totalSeconds > 0 ? 1.0 / row.totalSeconds : 0;
        row.notes = "median_of_3_trials; measured_full_noisy_vole_with_DefaultBaseOT";

        row.ok = true;
        for (u64 i = 0; i < N; ++i)
        {
            F z0{}, lhs{}, rhs{};
            ctx.minus(z0, z0, b[i]);
            ctx.plus(lhs, z0, a[i]);
            ctx.mul(rhs, delta, c[i]);
            if (!ctx.eq(lhs, rhs))
            {
                row.ok = false;
                row.notes = "verification_failed";
                break;
            }
        }

        return row;
    }

    template<u64 M>
    SubfieldVoleNetBenchRow subfieldVoleRunNoisyMedian(u64 log2N)
    {
        constexpr u64 trials = 3;
        std::vector<double> seconds;
        std::vector<u64> senderBytes;
        std::vector<u64> receiverBytes;
        std::vector<u64> totalBytes;
        SubfieldVoleNetBenchRow last;

        for (u64 i = 0; i < trials; ++i)
        {
            auto row = subfieldVoleRunNoisyTrial<M>(log2N);
            last = row;
            if (!row.ok)
                return row;

            seconds.push_back(row.totalSeconds);
            senderBytes.push_back(row.senderBytes);
            receiverBytes.push_back(row.receiverBytes);
            totalBytes.push_back(row.totalBytes);
        }

        last.totalSeconds = subfieldVoleMedian(seconds);
        last.senderBytes = subfieldVoleMedianU64(senderBytes);
        last.receiverBytes = subfieldVoleMedianU64(receiverBytes);
        last.totalBytes = subfieldVoleMedianU64(totalBytes);
        last.entriesPerSecond = last.totalSeconds > 0 ? static_cast<double>(last.entries) / last.totalSeconds : 0;
        last.rankOnePerSecond = last.totalSeconds > 0 ? 1.0 / last.totalSeconds : 0;
        last.ok = true;
        return last;
    }

    inline u64 subfieldVoleEstimatedNoisyPayload(u64 N, u64 m)
    {
        return 8 * m * m * N;
    }

    template<u64 M>
    SubfieldVoleNetBenchRow subfieldVoleRunOrSkip(u64 log2N)
    {
        constexpr u64 maxPayloadBytes = 256ull * 1024 * 1024;
        auto N = 1ull << log2N;
        auto estimatedPayload = subfieldVoleEstimatedNoisyPayload(N, M);

        if (estimatedPayload > maxPayloadBytes)
        {
            SubfieldVoleNetBenchRow row;
            row.N = N;
            row.log2N = log2N;
            row.m = M;
            row.entries = M * N;
            row.ok = false;
            row.notes = "unsupported_by_harness_payload_guard; estimated_noisy_payload_bytes="
                + std::to_string(estimatedPayload);
            return row;
        }

        return subfieldVoleRunNoisyMedian<M>(log2N);
    }

    inline void subfieldVolePrintRow(const SubfieldVoleNetBenchRow& row)
    {
        std::cout << std::fixed << std::setprecision(6)
                  << "SUBFIELD_VOLE_NET_BENCH"
                  << " method=" << row.method
                  << " network=" << row.network
                  << " N=" << row.N
                  << " log2N=" << row.log2N
                  << " m=" << row.m
                  << " field_type=" << row.fieldType
                  << " security_bits=" << row.securityBits
                  << " total_s=" << row.totalSeconds
                  << " comm_bytes_sent_by_sender=" << row.senderBytes
                  << " comm_bytes_sent_by_receiver=" << row.receiverBytes
                  << " total_comm_bytes=" << row.totalBytes
                  << " entries=" << row.entries
                  << " entries_per_s=" << row.entriesPerSecond
                  << " rank_one_per_s=" << row.rankOnePerSecond
                  << " " << (row.ok ? "PASS" : "UNSUPPORTED")
                  << " notes=" << row.notes
                  << std::endl;
    }

    inline int SubfieldVoleNetBench(u64 log2N)
    {
        subfieldVoleWriteNotes();

        std::vector<SubfieldVoleNetBenchRow> rows;
        rows.push_back(subfieldVoleRunOrSkip<8>(log2N));
        rows.push_back(subfieldVoleRunOrSkip<16>(log2N));
        rows.push_back(subfieldVoleRunOrSkip<32>(log2N));
        rows.push_back(subfieldVoleRunOrSkip<64>(log2N));

        bool anyOk = false;
        for (const auto& row : rows)
        {
            subfieldVolePrintRow(row);
            subfieldVoleAppendCsv(row);
            anyOk = anyOk || row.ok;
        }

        return anyOk ? 0 : 1;
    }
}
