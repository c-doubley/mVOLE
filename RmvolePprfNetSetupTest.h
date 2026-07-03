#pragma once

#include "Coeff128.h"
#include "ModuleVectorPprfTest.h"
#include "coproto/Socket/LocalAsyncSock.h"
#include "cryptoTools/Common/BitVector.h"
#include "cryptoTools/Crypto/PRNG.h"
#include "libOTe/Base/BaseOT.h"
#include "libOTe/Tools/Pprf/RegularPprf.h"
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <omp.h>

namespace osuCrypto
{
    struct RmvolePprfNetSetupParams
    {
        u64 logN = 0;
        u64 N = 0;
        u64 t = 0;
        u64 m = 0;
        u64 blockSize = 0;
    };

    struct RmvolePprfNetSetupResult
    {
        bool ok = false;
        double setupSeconds = 0;
        double expandSeconds = 0;
        double verifySeconds = 0;
        u64 senderBytes = 0;
        u64 receiverBytes = 0;
        u64 totalBytes = 0;
        u64 scalarPprfCount = 0;
        u64 expandedLeavesPerCoordinate = 0;
        u64 totalScalarExpandedLeaves = 0;
        double totalScalarExpandedLeavesOverMN = 0;
        std::string notes;
    };

    inline u64 rmvolePprfPow2(u64 logN)
    {
        if (logN >= 63)
            throw std::runtime_error("RMVOLE_PPRF_NET_SETUP requires logN < 63.");
        return 1ull << logN;
    }

    inline RmvolePprfNetSetupParams rmvolePprfMakeParams(u64 logN, u64 t, u64 m)
    {
        RmvolePprfNetSetupParams params;
        params.logN = logN;
        params.N = rmvolePprfPow2(logN);
        params.t = t;
        params.m = m;

        if (!t)
            throw std::runtime_error("RMVOLE_PPRF_NET_SETUP requires t > 0.");
        if (!m)
            throw std::runtime_error("RMVOLE_PPRF_NET_SETUP requires m > 0.");
        if (params.N % t)
            throw std::runtime_error("RMVOLE_PPRF_NET_SETUP requires t to divide N.");

        params.blockSize = params.N / t;
        if (!moduleVectorPprfIsPowerOfTwo(params.blockSize))
            throw std::runtime_error("RMVOLE_PPRF_NET_SETUP requires N/t to be a power of two.");
        if (params.blockSize < 2 || (params.blockSize & 1))
            throw std::runtime_error("RMVOLE_PPRF_NET_SETUP requires an even blockSize >= 2 for RegularPprf.");

        return params;
    }

    inline BitVector rmvolePprfChoiceBitsFromOffsets(const std::vector<u64>& offsets, u64 depth)
    {
        BitVector choices(offsets.size() * depth);
        for (u64 i = 0; i < static_cast<u64>(offsets.size()); ++i)
        {
            for (u64 j = 0; j < depth; ++j)
                choices[i * depth + j] = (offsets[i] >> j) & 1;
        }
        return choices;
    }

    template<typename F, typename Ctx>
    F rmvolePprfNeg(const F& value, Ctx& ctx)
    {
        F zero{};
        F v = value;
        if (ctx.eq(v, zero))
            return zero;
        return static_cast<F>(ctx.prime() - v);
    }

    template<typename F, typename Ctx>
    bool rmvolePprfSampleNonzero(F& out, PRNG& prng, Ctx& ctx)
    {
        F zero{};
        for (u64 tries = 0; tries < 32; ++tries)
        {
            ctx.fromBlock(out, prng.get<block>());
            if (!ctx.eq(out, zero))
                return true;
        }

        out = static_cast<F>(1);
        return false;
    }

    template<typename F, typename Ctx>
    RmvolePprfNetSetupResult rmvolePprfRunLocalTyped(
        const RmvolePprfNetSetupParams& params,
        Ctx ctx = {})
    {
        using VecF = typename Ctx::template Vec<F>;

        RmvolePprfNetSetupResult result;
        result.scalarPprfCount = params.m;
        result.expandedLeavesPerCoordinate = params.N;
        result.totalScalarExpandedLeaves = params.m * params.N;
        result.totalScalarExpandedLeavesOverMN =
            (params.m && params.N) ? static_cast<double>(result.totalScalarExpandedLeaves) / static_cast<double>(params.m * params.N) : 0;
        result.notes = "local_async_socket; DefaultBaseOT per scalar RegularPprf; s_only; centralized_offsets_delta_s";

        PRNG prng(sysRandomSeed());
        std::vector<F> delta(params.m);
        std::vector<F> sparseS(params.t);
        std::vector<u64> offsets(params.t);

        for (u64 h = 0; h < params.m; ++h)
            rmvolePprfSampleNonzero(delta[h], prng, ctx);

        for (u64 i = 0; i < params.t; ++i)
        {
            rmvolePprfSampleNonzero(sparseS[i], prng, ctx);
            offsets[i] = prng.get<u64>() % params.blockSize;
        }

        auto sockets = cp::LocalAsyncSocket::makePair();
        result.ok = true;

        for (u64 h = 0; h < params.m; ++h)
        {
            RegularPprfSender<F, F, Ctx> sender;
            RegularPprfReceiver<F, F, Ctx> receiver;
            VecF beta, senderOut, receiverOut;

            auto setupStart = omp_get_wtime();
            sender.configure(params.blockSize, params.t);
            receiver.configure(params.blockSize, params.t);

            auto choices = rmvolePprfChoiceBitsFromOffsets(offsets, receiver.mDepth);
            receiver.setChoiceBits(choices);

            ctx.resize(beta, params.t);
            for (u64 i = 0; i < params.t; ++i)
                ctx.mul(beta[i], delta[h], sparseS[i]);

            std::vector<std::array<block, 2>> senderBaseOts(sender.baseOtCount());
            std::vector<block> receiverBaseOts(receiver.baseOtCount());

#ifdef LIBOTE_HAS_BASE_OT
            DefaultBaseOT senderBaseOt;
            DefaultBaseOT receiverBaseOt;
            PRNG senderPrng(prng.get<block>() ^ block(h, 0xB451));
            PRNG receiverPrng(prng.get<block>() ^ block(h, 0xB452));

            auto sendBase = senderBaseOt.send(senderBaseOts, senderPrng, sockets[0]);
            auto recvBase = receiverBaseOt.receive(choices, receiverBaseOts, receiverPrng, sockets[1]);
            macoro::sync_wait(macoro::when_all_ready(std::move(sendBase), std::move(recvBase)));
#else
            throw std::runtime_error("RMVOLE_PPRF_NET_SETUP requires libOTe base OT support.");
#endif

            sender.setBase(senderBaseOts);
            receiver.setBase(receiverBaseOts);
            result.setupSeconds += omp_get_wtime() - setupStart;

            ctx.resize(senderOut, params.N);
            ctx.resize(receiverOut, params.N);

            auto expandStart = omp_get_wtime();
            auto sendPprf = sender.expand(
                sockets[0],
                beta,
                prng.get<block>() ^ block(h, 0x5EED),
                senderOut,
                PprfOutputFormat::ByTreeIndex,
                true,
                1,
                ctx);
            auto recvPprf = receiver.expand(
                sockets[1],
                receiverOut,
                PprfOutputFormat::ByTreeIndex,
                true,
                1,
                ctx);
            macoro::sync_wait(macoro::when_all_ready(std::move(sendPprf), std::move(recvPprf)));
            result.expandSeconds += omp_get_wtime() - expandStart;

            auto verifyStart = omp_get_wtime();
            for (u64 i = 0; i < params.t && result.ok; ++i)
            {
                for (u64 leaf = 0; leaf < params.blockSize; ++leaf)
                {
                    auto idx = i * params.blockSize + leaf;
                    F reconstructed;
                    ctx.plus(reconstructed, rmvolePprfNeg(senderOut[idx], ctx), receiverOut[idx]);

                    F expected{};
                    if (leaf == offsets[i])
                        ctx.copy(expected, beta[i]);

                    if (!ctx.eq(reconstructed, expected))
                    {
                        result.ok = false;
                        std::cout << "RMVOLE_PPRF_NET_SETUP FAIL"
                                  << " h=" << h
                                  << " block=" << i
                                  << " leaf=" << leaf
                                  << " offset=" << offsets[i]
                                  << " reconstructed=" << ctx.str(reconstructed)
                                  << " expected=" << ctx.str(expected)
                                  << std::endl;
                        break;
                    }
                }
            }
            result.verifySeconds += omp_get_wtime() - verifyStart;

            if (!result.ok)
                break;
        }

        result.senderBytes = sockets[0].bytesSent();
        result.receiverBytes = sockets[1].bytesSent();
        result.totalBytes = result.senderBytes + result.receiverBytes;

        return result;
    }

    inline void rmvolePprfWriteDoc()
    {
        std::filesystem::create_directories("docs");
        std::ofstream out("docs/rmvole_pprf_net_setup_test.md");
        out << "# RM-VOLE PPRF Network Setup Test\n\n"
            << "Phase 6E-1 adds `RmvolePprfNetSetupTest.h` and CLI flag `--RMVOLE_PPRF_NET_SETUP`.\n\n"
            << "## API Used\n\n"
            << "- libOTe API: `RegularPprfSender<F,F,Ctx>` and `RegularPprfReceiver<F,F,Ctx>` from `libOTe/Tools/Pprf/RegularPprf.h`.\n"
            << "- Field instantiation: `F = u64`, `Ctx = CoeffCtxIntegerPrime_64`.\n"
            << "- Network channel: `coproto::LocalAsyncSocket::makePair()`.\n"
            << "- Base OT: `DefaultBaseOT::send()` and `DefaultBaseOT::receive()` are run over the same local async socket pair before each scalar RegularPprf expansion.\n"
            << "- PPRF expansion: sender calls `expand(socket, beta, seed, senderOut, PprfOutputFormat::ByTreeIndex, true, 1, ctx)` and receiver calls `expand(socket, receiverOut, PprfOutputFormat::ByTreeIndex, true, 1, ctx)`.\n\n"
            << "## Relation Tested\n\n"
            << "For `N = 2^logN`, `blockSize = N/t`, and each block `i`, the harness centrally samples an offset `offset_i`, a nonzero scalar `s_i`, and extension-coordinate scalars `Delta_h`. For coordinate `h`, it programs:\n\n"
            << "```text\n"
            << "beta_{i,h} = Delta_h * s_i in F_p\n"
            << "```\n\n"
            << "RegularPprf reconstructs `receiverOut = senderOut + beta` at the selected point and `receiverOut = senderOut` elsewhere. The RM-VOLE setup shares are interpreted as:\n\n"
            << "```text\n"
            << "share0_h[j] = -senderOut_h[j]\n"
            << "share1_h[j] =  receiverOut_h[j]\n"
            << "share0_h[j] + share1_h[j] = beta_{i,h} at j = i*blockSize + offset_i, else 0\n"
            << "```\n\n"
            << "Verification opens the simulated local shares and checks all `m*N` scalar positions.\n\n"
            << "## Caveats\n\n"
            << "- This is a correctness/API harness, not secure input generation. The test centrally samples `Delta`, `s`, offsets, and then installs receiver choice bits.\n"
            << "- Phase 6E-1 runs `m` independent scalar RegularPprf instances for sparse `s` only. Sparse `e` is the next duplicate path, not included here.\n"
            << "- This is not the final optimized shared-path vector PPRF. It repeats base OT and PPRF setup independently per coordinate.\n"
            << "- It uses true coproto sockets and libOTe base OT/PPRF network messages in one process, but it is not a two-process TCP benchmark yet.\n\n"
            << "## Next Step\n\n"
            << "Phase 6E-2 should split the same sender/receiver work over TCP loopback/LAN. After that, implement the shared-path vector-valued PPRF so path/base material is paid once while coordinate payloads remain vector-valued.\n";
    }

    inline int RmvolePprfNetSetupTest(int argc, char** argv)
    {
        if (argc != 6 || std::string(argv[2]) != "local")
        {
            std::cerr << "Usage: ./build/main --RMVOLE_PPRF_NET_SETUP local <logN> <t> <m>" << std::endl;
            return 1;
        }

        auto logN = static_cast<u64>(std::stoull(argv[3]));
        auto t = static_cast<u64>(std::stoull(argv[4]));
        auto m = static_cast<u64>(std::stoull(argv[5]));
        auto params = rmvolePprfMakeParams(logN, t, m);

        rmvolePprfWriteDoc();
        auto result = rmvolePprfRunLocalTyped<u64, CoeffCtxIntegerPrime_64>(params);

        std::cout << std::fixed << std::setprecision(6)
                  << "RMVOLE_PPRF_NET_SETUP"
                  << " mode=local"
                  << " field=Fp64"
                  << " logN=" << params.logN
                  << " N=" << params.N
                  << " t=" << params.t
                  << " m=" << params.m
                  << " blockSize=" << params.blockSize
                  << " scalar_pprf_count=" << result.scalarPprfCount
                  << " expanded_leaves_per_coordinate=" << result.expandedLeavesPerCoordinate
                  << " total_scalar_expanded_leaves=" << result.totalScalarExpandedLeaves
                  << " total_scalar_expanded_leaves_over_mN=" << result.totalScalarExpandedLeavesOverMN
                  << " setup_total_s=" << result.setupSeconds
                  << " expand_total_s=" << result.expandSeconds
                  << " verify_s=" << result.verifySeconds
                  << " sender_socket_bytes_sent=" << result.senderBytes
                  << " receiver_socket_bytes_sent=" << result.receiverBytes
                  << " total_socket_bytes_sent=" << result.totalBytes
                  << " notes=" << result.notes
                  << " " << (result.ok ? "PASS" : "FAIL")
                  << std::endl;

        return result.ok ? 0 : 1;
    }
}
