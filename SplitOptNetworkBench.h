#pragma once

#include "Coeff128.h"
#include "RmvolePprfNetSetupTest.h"
#include "RmvoleNetBench.h"
#include "coproto/Socket/AsioSocket.h"
#include "libOTe/TwoChooseOne/Kos/KosOtExtReceiver.h"
#include "libOTe/TwoChooseOne/Kos/KosOtExtSender.h"
#include "libOTe/Vole/Noisy/NoisyVoleReceiver.h"
#include "libOTe/Vole/Noisy/NoisyVoleSender.h"
#include "libOTe/Vole/Silent/SilentVoleReceiver.h"
#include "libOTe/Vole/Silent/SilentVoleSender.h"
#include <chrono>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace osuCrypto
{
    enum class SplitOptBackend
    {
        GenericNoisy,
        SilentR2C,
        CoeffOt
    };

    inline SplitOptBackend splitOptParseBackend(const std::string& value)
    {
        if (value == "generic" || value == "generic-noisy" || value == "SPLIT_GENERIC_NOISY")
            return SplitOptBackend::GenericNoisy;
        if (value == "silent-r2c" || value == "SPLIT_SILENT_R2C")
            return SplitOptBackend::SilentR2C;
        if (value == "coeff-ot" || value == "SPLIT_COEFF_OT")
            return SplitOptBackend::CoeffOt;
        throw std::runtime_error("split backend must be generic-noisy, silent-r2c, or coeff-ot.");
    }

    inline std::string splitOptBackendName(SplitOptBackend backend)
    {
        switch (backend)
        {
        case SplitOptBackend::GenericNoisy: return "SPLIT_GENERIC_NOISY";
        case SplitOptBackend::SilentR2C: return "SPLIT_SILENT_R2C";
        case SplitOptBackend::CoeffOt: return "SPLIT_COEFF_OT";
        }
        return "UNKNOWN";
    }

    struct SplitOptRow
    {
        std::string backend;
        std::string role;
        std::string mode;
        std::string network;
        std::string address;
        u64 L = 128;
        u64 t = 64;
        u64 m = 0;
        u64 reps = 1;
        u64 requestedLength = 128;
        u64 actualGeneratedLength = 128;
        u64 ctxBitSizeExtElem = 0;
        u64 ctxByteSizeExtElem = 0;
        u64 baseOtCount = 0;
        u64 baseOtProtocolInvocations = 0;
        u64 extendedOtCount = 0;
        u64 payloadElementCount = 0;
        u64 payloadBytes = 0;
        u64 correctionBytes = 0;
        u64 baseCorrelationBytes = 0;
        u64 extensionBytes = 0;
        u64 framingBytes = 0;
        u64 verificationOpeningBytes = 0;
        u64 bytesSentByRole = 0;
        u64 bytesReceivedByRole = 0;
        u64 cleanProtocolBytesByRole = 0;
        double baseSetupSeconds = 0;
        double extensionSeconds = 0;
        double correctionSeconds = 0;
        double totalSeconds = 0;
        double totalCpuWorkSeconds = 0;
        double verifySeconds = 0;
        bool ok = true;
        std::string notes;
    };

    template<u64 M>
    using SplitOptExtElem = RmvolePprfPrimeVector<M>;

    template<u64 M>
    void splitOptSampleDelta(SplitOptExtElem<M>& delta, PRNG& prng, CoeffCtxIntegerPrime_64& scalarCtx)
    {
        for (u64 h = 0; h < M; ++h)
            scalarCtx.fromBlock(delta[h], prng.get<block>());
    }

    template<u64 M>
    void splitOptMulScalar(
        SplitOptExtElem<M>& out,
        const SplitOptExtElem<M>& delta,
        u64 q,
        CoeffCtxIntegerPrime_64& scalarCtx)
    {
        for (u64 h = 0; h < M; ++h)
            scalarCtx.mul(out[h], delta[h], q);
    }

    template<u64 M>
    bool splitOptVerify(
        const std::vector<u64>& q,
        const std::vector<SplitOptExtElem<M>>& A,
        const std::vector<SplitOptExtElem<M>>& B,
        const SplitOptExtElem<M>& delta,
        CoeffCtxIntegerPrime_64& scalarCtx)
    {
        if (q.size() != A.size() || q.size() != B.size())
            return false;
        for (u64 i = 0; i < q.size(); ++i)
        {
            SplitOptExtElem<M> product{};
            splitOptMulScalar<M>(product, delta, q[i], scalarCtx);
            for (u64 h = 0; h < M; ++h)
            {
                u64 lhs;
                scalarCtx.minus(lhs, A[i][h], B[i][h]);
                if (!scalarCtx.eq(lhs, product[h]))
                    return false;
            }
        }
        return true;
    }

    inline void splitOptXorBytes(u8* dst, const u8* src, u64 size)
    {
        for (u64 i = 0; i < size; ++i)
            dst[i] ^= src[i];
    }

    template<typename T>
    T splitOptPadValue(block seed, u64 otIndex, u64 branch)
    {
        T out{};
        auto tweak = block(otIndex, branch);
        PRNG prng(seed ^ tweak);
        prng.get(reinterpret_cast<u8*>(&out), sizeof(T));
        return out;
    }

    template<u64 M>
    SplitOptRow splitOptRunTcpRoleTyped(
        bool server,
        const std::string& host,
        const std::string& port,
        SplitOptBackend backend,
        u64 reps,
        RmvoleNetBenchMode mode)
    {
        using ExtElem = SplitOptExtElem<M>;
        using ExtCtx = CoeffCtxPrimeArray64<M>;
        using VecF = typename ExtCtx::template Vec<ExtElem>;
        using VecG = typename ExtCtx::template Vec<u64>;
        constexpr u64 L = 128;
        constexpr u64 BitCount = 61;
        constexpr u64 OtCount = L * BitCount;

        auto address = host + ":" + port;
        auto socket = cp::asioConnect(address, server);
        PRNG prng(sysRandomSeed());
        ExtCtx vectorCtx;
        CoeffCtxIntegerPrime_64 scalarCtx;
        std::vector<double> baseTimes, extensionTimes, correctionTimes, totalTimes, verifyTimes;

        SplitOptRow row;
        row.backend = splitOptBackendName(backend);
        row.role = server ? "p0-server" : "p1-client";
        row.mode = rmvoleNetBenchModeName(mode);
        row.network = rmvoleNetBenchTcpNetworkLabel(host);
        row.address = address;
        row.m = M;
        row.reps = reps;
        row.ctxBitSizeExtElem = vectorCtx.template bitSize<ExtElem>();
        row.ctxByteSizeExtElem = vectorCtx.template byteSize<ExtElem>();
        row.payloadElementCount = L;

        for (u64 rep = 0; rep < reps && row.ok; ++rep)
        {
            std::vector<u64> q(L);
            ExtElem delta{};
            std::vector<ExtElem> A(L), B(L);

            if (server)
            {
                for (auto& qi : q)
                {
                    scalarCtx.fromBlock(qi, prng.get<block>());
                    if (qi == 0)
                        qi = 1;
                }
            }
            else
            {
                splitOptSampleDelta<M>(delta, prng, scalarCtx);
            }

            auto totalStart = omp_get_wtime();
            if (backend == SplitOptBackend::GenericNoisy)
            {
                VecG c;
                VecF a, b;
                vectorCtx.resize(c, L);
                vectorCtx.resize(a, L);
                vectorCtx.resize(b, L);
                if (server)
                    std::copy(q.begin(), q.end(), c.begin());

                NoisyVoleReceiver<ExtElem, u64, ExtCtx> receiver;
                NoisyVoleSender<ExtElem, u64, ExtCtx> sender;
                DefaultBaseOT baseOt;
                row.baseOtCount = row.ctxBitSizeExtElem;
                row.baseOtProtocolInvocations = 1;
                row.payloadBytes = L * row.ctxBitSizeExtElem * row.ctxByteSizeExtElem;
                auto start = omp_get_wtime();
                if (server)
                    macoro::sync_wait(receiver.receive(c, a, prng, baseOt, socket, vectorCtx));
                else
                    macoro::sync_wait(sender.send(delta, b, prng, baseOt, socket, vectorCtx));
                extensionTimes.push_back(omp_get_wtime() - start);
                if (server)
                    std::copy(a.begin(), a.end(), A.begin());
                else
                    std::copy(b.begin(), b.end(), B.begin());
            }
            else if (backend == SplitOptBackend::SilentR2C)
            {
                VecG c;
                VecF a, b;
                vectorCtx.resize(c, L);
                vectorCtx.resize(a, L);
                vectorCtx.resize(b, L);
                SilentVoleReceiver<ExtElem, u64, ExtCtx> receiver;
                SilentVoleSender<ExtElem, u64, ExtCtx> sender;
                receiver.mMultType = MultType::ExConv7x24;
                sender.mMultType = MultType::ExConv7x24;
                receiver.configure(L, SilentBaseType::Base, 128, vectorCtx);
                sender.configure(L, SilentBaseType::Base, 128, vectorCtx);
                row.actualGeneratedLength = receiver.mRequestSize;
                row.baseOtCount = receiver.silentBaseOtCount();
                row.baseOtProtocolInvocations = 1;
                row.payloadElementCount = row.actualGeneratedLength;

                auto before = socket.bytesSent();
                auto start = omp_get_wtime();
                if (server)
                    macoro::sync_wait(receiver.genSilentBaseOts(prng, socket));
                else
                    macoro::sync_wait(sender.genSilentBaseOts(prng, socket, delta));
                baseTimes.push_back(omp_get_wtime() - start);
                auto after = socket.bytesSent();
                row.baseCorrelationBytes += after >= before ? after - before : 0;

                before = socket.bytesSent();
                start = omp_get_wtime();
                if (server)
                    macoro::sync_wait(receiver.silentReceive(c, a, prng, socket));
                else
                    macoro::sync_wait(sender.silentSend(delta, b, prng, socket));
                extensionTimes.push_back(omp_get_wtime() - start);
                after = socket.bytesSent();
                row.extensionBytes += after >= before ? after - before : 0;

                start = omp_get_wtime();
                if (server)
                {
                    std::vector<u64> d(L);
                    for (u64 i = 0; i < L; ++i)
                        scalarCtx.minus(d[i], q[i], c[i]);
                    before = socket.bytesSent();
                    macoro::sync_wait(socket.send(coproto::copy(d)));
                    macoro::sync_wait(socket.flush());
                    after = socket.bytesSent();
                    row.correctionBytes += after >= before ? after - before : 0;
                    std::copy(a.begin(), a.end(), A.begin());
                }
                else
                {
                    std::vector<u64> d(L);
                    before = socket.bytesReceived();
                    macoro::sync_wait(socket.recv(d));
                    after = socket.bytesReceived();
                    row.correctionBytes += after >= before ? after - before : 0;
                    for (u64 i = 0; i < L; ++i)
                    {
                        ExtElem dDelta{};
                        splitOptMulScalar<M>(dDelta, delta, d[i], scalarCtx);
                        for (u64 h = 0; h < M; ++h)
                            scalarCtx.minus(B[i][h], b[i][h], dDelta[h]);
                    }
                }
                correctionTimes.push_back(omp_get_wtime() - start);
                row.payloadBytes = row.extensionBytes;
            }
            else
            {
                row.baseOtCount = gOtExtBaseOtCount;
                row.baseOtProtocolInvocations = 1;
                row.extendedOtCount = OtCount;
                row.payloadElementCount = 2 * OtCount;

                if (server)
                {
                    BitVector choices(OtCount);
                    for (u64 i = 0; i < L; ++i)
                        for (u64 k = 0; k < BitCount; ++k)
                            choices[i * BitCount + k] = static_cast<u8>((q[i] >> k) & 1);
                    AlignedUnVector<block> seeds(OtCount);
                    KosOtExtReceiver receiver;
                    receiver.mIsMalicious = false;
                    auto before = socket.bytesReceived() + socket.bytesSent();
                    auto start = omp_get_wtime();
                    macoro::sync_wait(receiver.genBaseOts(prng, socket));
                    auto after = socket.bytesReceived() + socket.bytesSent();
                    row.baseCorrelationBytes += after >= before ? after - before : 0;
                    baseTimes.push_back(omp_get_wtime() - start);

                    before = socket.bytesReceived() + socket.bytesSent();
                    start = omp_get_wtime();
                    macoro::sync_wait(receiver.receiveChosen(choices, seeds, prng, socket));
                    std::vector<std::array<ExtElem, 2>> encrypted(OtCount);
                    macoro::sync_wait(socket.recv(encrypted));
                    after = socket.bytesReceived() + socket.bytesSent();
                    row.payloadBytes += after >= before ? after - before : 0;
                    extensionTimes.push_back(omp_get_wtime() - start);

                    for (u64 i = 0; i < L; ++i)
                    {
                        A[i] = ExtElem{};
                        for (u64 k = 0; k < BitCount; ++k)
                        {
                            auto ot = i * BitCount + k;
                            auto branch = static_cast<u64>((q[i] >> k) & 1);
                            ExtElem y = encrypted[ot][branch];
                            auto pad = splitOptPadValue<ExtElem>(seeds[ot], ot, branch);
                            splitOptXorBytes(reinterpret_cast<u8*>(&y), reinterpret_cast<const u8*>(&pad), sizeof(ExtElem));
                            for (u64 h = 0; h < M; ++h)
                                scalarCtx.plus(A[i][h], A[i][h], y[h] % CoeffCtxIntegerPrime_64::PR);
                        }
                    }
                }
                else
                {
                    std::vector<std::array<block, 2>> seedPairs(OtCount);
                    std::vector<std::array<ExtElem, 2>> encrypted(OtCount);
                    for (u64 i = 0; i < L; ++i)
                    {
                        B[i] = ExtElem{};
                        for (u64 k = 0; k < BitCount; ++k)
                        {
                            auto ot = i * BitCount + k;
                            seedPairs[ot][0] = prng.get<block>();
                            seedPairs[ot][1] = prng.get<block>();
                            ExtElem r{};
                            splitOptSampleDelta<M>(r, prng, scalarCtx);
                            ExtElem shiftedDelta{};
                            u64 pow2 = (static_cast<u64>(1) << k) % CoeffCtxIntegerPrime_64::PR;
                            splitOptMulScalar<M>(shiftedDelta, delta, pow2, scalarCtx);
                            ExtElem m0 = r;
                            ExtElem m1{};
                            for (u64 h = 0; h < M; ++h)
                            {
                                scalarCtx.plus(m1[h], r[h], shiftedDelta[h]);
                                scalarCtx.plus(B[i][h], B[i][h], r[h]);
                            }
                            auto pad0 = splitOptPadValue<ExtElem>(seedPairs[ot][0], ot, 0);
                            auto pad1 = splitOptPadValue<ExtElem>(seedPairs[ot][1], ot, 1);
                            splitOptXorBytes(reinterpret_cast<u8*>(&m0), reinterpret_cast<const u8*>(&pad0), sizeof(ExtElem));
                            splitOptXorBytes(reinterpret_cast<u8*>(&m1), reinterpret_cast<const u8*>(&pad1), sizeof(ExtElem));
                            encrypted[ot] = {m0, m1};
                        }
                    }

                    KosOtExtSender sender;
                    sender.mIsMalicious = false;
                    auto before = socket.bytesReceived() + socket.bytesSent();
                    auto start = omp_get_wtime();
                    macoro::sync_wait(sender.genBaseOts(prng, socket));
                    auto after = socket.bytesReceived() + socket.bytesSent();
                    row.baseCorrelationBytes += after >= before ? after - before : 0;
                    baseTimes.push_back(omp_get_wtime() - start);

                    before = socket.bytesReceived() + socket.bytesSent();
                    start = omp_get_wtime();
                    macoro::sync_wait(sender.sendChosen(seedPairs, prng, socket));
                    macoro::sync_wait(socket.send(coproto::copy(encrypted)));
                    macoro::sync_wait(socket.flush());
                    after = socket.bytesReceived() + socket.bytesSent();
                    row.payloadBytes += after >= before ? after - before : 0;
                    extensionTimes.push_back(omp_get_wtime() - start);
                }
            }

            totalTimes.push_back(omp_get_wtime() - totalStart);

            if (mode == RmvoleNetBenchMode::Verify)
            {
                auto verifyStart = omp_get_wtime();
                if (server)
                {
                    macoro::sync_wait(socket.recv(delta));
                    macoro::sync_wait(socket.recv(B));
                    row.verificationOpeningBytes += sizeof(delta) + B.size() * sizeof(ExtElem);
                    row.ok = splitOptVerify<M>(q, A, B, delta, scalarCtx);
                }
                else
                {
                    macoro::sync_wait(socket.send(coproto::copy(delta)));
                    macoro::sync_wait(socket.send(coproto::copy(B)));
                    macoro::sync_wait(socket.flush());
                    row.verificationOpeningBytes += sizeof(delta) + B.size() * sizeof(ExtElem);
                }
                verifyTimes.push_back(omp_get_wtime() - verifyStart);
            }
        }

        macoro::sync_wait(socket.flush());
        row.baseSetupSeconds = rmvoleNetBenchMedian(baseTimes);
        row.extensionSeconds = rmvoleNetBenchMedian(extensionTimes);
        row.correctionSeconds = rmvoleNetBenchMedian(correctionTimes);
        row.totalSeconds = rmvoleNetBenchMedian(totalTimes);
        row.totalCpuWorkSeconds = row.totalSeconds;
        row.verifySeconds = rmvoleNetBenchMedian(verifyTimes);
        row.bytesSentByRole = socket.bytesSent();
        row.bytesReceivedByRole = socket.bytesReceived();
        auto excluded = !server ? row.verificationOpeningBytes : 0;
        row.cleanProtocolBytesByRole = row.bytesSentByRole >= excluded ? row.bytesSentByRole - excluded : 0;
        if (row.cleanProtocolBytesByRole > row.baseCorrelationBytes + row.payloadBytes + row.correctionBytes)
            row.framingBytes = row.cleanProtocolBytesByRole - row.baseCorrelationBytes - row.payloadBytes - row.correctionBytes;
        row.notes = "tcp_two_process; p=2^61-1; lambda=128; L=2t=128; A-B=q*Delta";
        return row;
    }

    inline SplitOptRow splitOptRunTcpRole(
        bool server,
        const std::string& host,
        const std::string& port,
        SplitOptBackend backend,
        u64 m,
        u64 reps,
        RmvoleNetBenchMode mode)
    {
        switch (m)
        {
        case 8: return splitOptRunTcpRoleTyped<8>(server, host, port, backend, reps, mode);
        case 16: return splitOptRunTcpRoleTyped<16>(server, host, port, backend, reps, mode);
        case 32: return splitOptRunTcpRoleTyped<32>(server, host, port, backend, reps, mode);
        default: throw std::runtime_error("SPLIT_OPT_BENCH supports m = 8, 16, 32.");
        }
    }

    inline void splitOptPrintRow(const SplitOptRow& row)
    {
        std::cout << "SPLIT_BACKEND_TCP"
                  << " backend=" << row.backend
                  << " role=" << row.role
                  << " mode=" << row.mode
                  << " network=" << row.network
                  << " address=" << row.address
                  << " L=" << row.L
                  << " t=" << row.t
                  << " m=" << row.m
                  << " reps=" << row.reps
                  << " requested_length=" << row.requestedLength
                  << " actual_generated_length=" << row.actualGeneratedLength
                  << " ctx_bit_size_ext_elem=" << row.ctxBitSizeExtElem
                  << " ctx_byte_size_ext_elem=" << row.ctxByteSizeExtElem
                  << " base_ot_count=" << row.baseOtCount
                  << " base_ot_protocol_invocations=" << row.baseOtProtocolInvocations
                  << " extended_ot_count=" << row.extendedOtCount
                  << " payload_element_count=" << row.payloadElementCount
                  << " payload_bytes=" << row.payloadBytes
                  << " correction_bytes=" << row.correctionBytes
                  << " base_correlation_bytes=" << row.baseCorrelationBytes
                  << " extension_bytes=" << row.extensionBytes
                  << " framing_bytes=" << row.framingBytes
                  << " verification_opening_bytes=" << row.verificationOpeningBytes
                  << " bytes_sent_by_role=" << row.bytesSentByRole
                  << " bytes_received_by_role=" << row.bytesReceivedByRole
                  << " clean_protocol_bytes_by_role=" << row.cleanProtocolBytesByRole
                  << " base_setup_s=" << std::setprecision(12) << row.baseSetupSeconds
                  << " extension_s=" << row.extensionSeconds
                  << " correction_s=" << row.correctionSeconds
                  << " total_s=" << row.totalSeconds
                  << " total_cpu_work_s=" << row.totalCpuWorkSeconds
                  << " verify_s=" << row.verifySeconds
                  << " notes=" << row.notes
                  << " " << (row.ok ? "PASS" : "FAIL")
                  << std::endl;
    }

    inline int SplitOptNetworkBench(int argc, char** argv)
    {
        if (argc != 9 || (std::string(argv[2]) != "server" && std::string(argv[2]) != "client"))
        {
            std::cerr << "Usage: ./build/main --SPLIT_OPT_BENCH server|client <host> <port> <generic-noisy|silent-r2c|coeff-ot> <m> <reps> <verify|bench>" << std::endl;
            return 1;
        }
        auto server = std::string(argv[2]) == "server";
        auto backend = splitOptParseBackend(argv[5]);
        auto m = static_cast<u64>(std::stoull(argv[6]));
        auto reps = static_cast<u64>(std::stoull(argv[7]));
        auto mode = rmvoleNetBenchParseMode(argv[8]);
        auto row = splitOptRunTcpRole(server, argv[3], argv[4], backend, m, reps, mode);
        splitOptPrintRow(row);
        return row.ok ? 0 : 1;
    }
}
