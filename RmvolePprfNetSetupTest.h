#pragma once

#include "Coeff128.h"
#include "ModuleVectorPprfTest.h"
#include "coproto/Socket/AsioSocket.h"
#include "coproto/Socket/LocalAsyncSock.h"
#include "cryptoTools/Common/BitVector.h"
#include "cryptoTools/Crypto/PRNG.h"
#include "libOTe/Base/BaseOT.h"
#include "libOTe/Tools/Pprf/RegularPprf.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
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
        double sSetupSeconds = 0;
        double eSetupSeconds = 0;
        double verifySeconds = 0;
        u64 senderBytes = 0;
        u64 receiverBytes = 0;
        u64 totalBytes = 0;
        u64 scalarPprfCountS = 0;
        u64 scalarPprfCountE = 0;
        u64 totalScalarPprfCount = 0;
        u64 expandedLeavesPerCoordinatePerSparseVector = 0;
        u64 totalScalarExpandedLeaves = 0;
        double totalScalarExpandedLeavesOver2MN = 0;
        std::string notes;
    };

    struct RmvolePprfTcpSetupRow
    {
        std::string method = "RMVOLE-PPRF-RegularPprf";
        std::string role;
        std::string network;
        std::string address;
        u64 logN = 0;
        u64 N = 0;
        u64 t = 0;
        u64 m = 0;
        u64 blockSize = 0;
        u64 reps = 0;
        double medianSSetupSeconds = 0;
        double medianESetupSeconds = 0;
        double medianTotalSetupSeconds = 0;
        double medianVerifySeconds = 0;
        u64 bytesSentByRole = 0;
        u64 bytesReceivedByRole = 0;
        u64 totalLocalSocketBytes = 0;
        u64 scalarPprfCountS = 0;
        u64 scalarPprfCountE = 0;
        u64 totalScalarPprfCount = 0;
        u64 expandedLeavesPerCoordinatePerSparseVector = 0;
        u64 totalScalarExpandedLeaves = 0;
        double totalScalarExpandedLeavesOver2MN = 0;
        bool ok = false;
        std::string notes;
    };

    template<typename F>
    struct RmvolePprfSparseInput
    {
        std::vector<F> values;
        std::vector<u64> offsets;
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

    inline double rmvolePprfMedian(std::vector<double> values)
    {
        if (values.empty())
            return 0;
        std::sort(values.begin(), values.end());
        auto mid = values.size() / 2;
        if (values.size() & 1)
            return values[mid];
        return (values[mid - 1] + values[mid]) / 2;
    }

    inline std::string rmvolePprfCsvEscape(const std::string& value)
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

    inline std::string rmvolePprfTcpNetworkLabel(const std::string& host)
    {
        if (host == "127.0.0.1" || host == "localhost" || host == "0.0.0.0" || host == "::1")
            return "tcp-loopback";
        return "tcp-lan";
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
    void rmvolePprfSampleNonzero(F& out, PRNG& prng, Ctx& ctx)
    {
        F zero{};
        for (u64 tries = 0; tries < 32; ++tries)
        {
            ctx.fromBlock(out, prng.get<block>());
            if (!ctx.eq(out, zero))
                return;
        }

        out = static_cast<F>(1);
    }

    template<typename F, typename Ctx>
    RmvolePprfSparseInput<F> rmvolePprfSampleSparseInput(
        const RmvolePprfNetSetupParams& params,
        PRNG& prng,
        Ctx& ctx)
    {
        RmvolePprfSparseInput<F> input;
        input.values.resize(params.t);
        input.offsets.resize(params.t);

        for (u64 i = 0; i < params.t; ++i)
        {
            rmvolePprfSampleNonzero(input.values[i], prng, ctx);
            input.offsets[i] = prng.get<u64>() % params.blockSize;
        }

        return input;
    }

    template<typename F, typename Ctx>
    typename Ctx::template Vec<F> rmvolePprfBuildBeta(
        const RmvolePprfNetSetupParams& params,
        const std::vector<F>& delta,
        const RmvolePprfSparseInput<F>& input,
        u64 coord,
        Ctx& ctx)
    {
        typename Ctx::template Vec<F> beta;
        ctx.resize(beta, params.t);
        for (u64 i = 0; i < params.t; ++i)
            ctx.mul(beta[i], delta[coord], input.values[i]);
        return beta;
    }

    template<typename F, typename Ctx>
    bool rmvolePprfVerifyScalar(
        const RmvolePprfNetSetupParams& params,
        const RmvolePprfSparseInput<F>& input,
        const typename Ctx::template Vec<F>& beta,
        const typename Ctx::template Vec<F>& senderOut,
        const typename Ctx::template Vec<F>& receiverOut,
        u64 coord,
        const std::string& label,
        Ctx& ctx)
    {
        for (u64 i = 0; i < params.t; ++i)
        {
            for (u64 leaf = 0; leaf < params.blockSize; ++leaf)
            {
                auto idx = i * params.blockSize + leaf;
                F reconstructed;
                ctx.plus(reconstructed, rmvolePprfNeg(senderOut[idx], ctx), receiverOut[idx]);

                F expected{};
                if (leaf == input.offsets[i])
                    ctx.copy(expected, beta[i]);

                if (!ctx.eq(reconstructed, expected))
                {
                    std::cout << "RMVOLE_PPRF_NET_SETUP FAIL"
                              << " vector=" << label
                              << " h=" << coord
                              << " block=" << i
                              << " leaf=" << leaf
                              << " offset=" << input.offsets[i]
                              << " reconstructed=" << ctx.str(reconstructed)
                              << " expected=" << ctx.str(expected)
                              << std::endl;
                    return false;
                }
            }
        }
        return true;
    }

    inline void rmvolePprfFillCounters(
        const RmvolePprfNetSetupParams& params,
        RmvolePprfNetSetupResult& result)
    {
        result.scalarPprfCountS = params.m;
        result.scalarPprfCountE = params.m;
        result.totalScalarPprfCount = 2 * params.m;
        result.expandedLeavesPerCoordinatePerSparseVector = params.N;
        result.totalScalarExpandedLeaves = 2 * params.m * params.N;
        result.totalScalarExpandedLeavesOver2MN =
            (params.m && params.N) ? static_cast<double>(result.totalScalarExpandedLeaves) / static_cast<double>(2 * params.m * params.N) : 0;
    }

    inline void rmvolePprfFillTcpCounters(
        const RmvolePprfNetSetupParams& params,
        RmvolePprfTcpSetupRow& row)
    {
        row.scalarPprfCountS = params.m;
        row.scalarPprfCountE = params.m;
        row.totalScalarPprfCount = 2 * params.m;
        row.expandedLeavesPerCoordinatePerSparseVector = params.N;
        row.totalScalarExpandedLeaves = 2 * params.m * params.N;
        row.totalScalarExpandedLeavesOver2MN =
            (params.m && params.N) ? static_cast<double>(row.totalScalarExpandedLeaves) / static_cast<double>(2 * params.m * params.N) : 0;
    }

    template<typename F, typename Ctx>
    bool rmvolePprfRunLocalSparseVector(
        const RmvolePprfNetSetupParams& params,
        const std::vector<F>& delta,
        const RmvolePprfSparseInput<F>& input,
        const std::string& label,
        cp::Socket& senderSocket,
        cp::Socket& receiverSocket,
        double& setupSeconds,
        double& verifySeconds,
        PRNG& prng,
        Ctx& ctx)
    {
        using VecF = typename Ctx::template Vec<F>;

        for (u64 h = 0; h < params.m; ++h)
        {
            RegularPprfSender<F, F, Ctx> sender;
            RegularPprfReceiver<F, F, Ctx> receiver;
            VecF senderOut, receiverOut;

            auto start = omp_get_wtime();
            sender.configure(params.blockSize, params.t);
            receiver.configure(params.blockSize, params.t);

            auto choices = rmvolePprfChoiceBitsFromOffsets(input.offsets, receiver.mDepth);
            receiver.setChoiceBits(choices);
            auto beta = rmvolePprfBuildBeta<F, Ctx>(params, delta, input, h, ctx);

            std::vector<std::array<block, 2>> senderBaseOts(sender.baseOtCount());
            std::vector<block> receiverBaseOts(receiver.baseOtCount());

#ifdef LIBOTE_HAS_BASE_OT
            DefaultBaseOT senderBaseOt;
            DefaultBaseOT receiverBaseOt;
            PRNG senderPrng(prng.get<block>() ^ block(h, label == "s" ? 0xB451 : 0xE451));
            PRNG receiverPrng(prng.get<block>() ^ block(h, label == "s" ? 0xB452 : 0xE452));

            auto sendBase = senderBaseOt.send(senderBaseOts, senderPrng, senderSocket);
            auto recvBase = receiverBaseOt.receive(choices, receiverBaseOts, receiverPrng, receiverSocket);
            macoro::sync_wait(macoro::when_all_ready(std::move(sendBase), std::move(recvBase)));
#else
            throw std::runtime_error("RMVOLE_PPRF_NET_SETUP requires libOTe base OT support.");
#endif

            sender.setBase(senderBaseOts);
            receiver.setBase(receiverBaseOts);
            ctx.resize(senderOut, params.N);
            ctx.resize(receiverOut, params.N);

            auto sendPprf = sender.expand(
                senderSocket,
                beta,
                prng.get<block>() ^ block(h, label == "s" ? 0x5EED : 0xEED),
                senderOut,
                PprfOutputFormat::ByTreeIndex,
                true,
                1,
                ctx);
            auto recvPprf = receiver.expand(
                receiverSocket,
                receiverOut,
                PprfOutputFormat::ByTreeIndex,
                true,
                1,
                ctx);
            macoro::sync_wait(macoro::when_all_ready(std::move(sendPprf), std::move(recvPprf)));
            setupSeconds += omp_get_wtime() - start;

            auto verifyStart = omp_get_wtime();
            auto ok = rmvolePprfVerifyScalar<F, Ctx>(params, input, beta, senderOut, receiverOut, h, label, ctx);
            verifySeconds += omp_get_wtime() - verifyStart;
            if (!ok)
                return false;
        }

        return true;
    }

    template<typename F, typename Ctx>
    RmvolePprfNetSetupResult rmvolePprfRunLocalTyped(
        const RmvolePprfNetSetupParams& params,
        Ctx ctx = {})
    {
        RmvolePprfNetSetupResult result;
        rmvolePprfFillCounters(params, result);
        result.notes = "local_async_socket; DefaultBaseOT repeated per scalar RegularPprf; s_and_e; centralized_offsets_delta_s_e";

        PRNG prng(sysRandomSeed());
        std::vector<F> delta(params.m);
        for (u64 h = 0; h < params.m; ++h)
            rmvolePprfSampleNonzero(delta[h], prng, ctx);

        auto sparseS = rmvolePprfSampleSparseInput<F, Ctx>(params, prng, ctx);
        auto sparseE = rmvolePprfSampleSparseInput<F, Ctx>(params, prng, ctx);
        auto sockets = cp::LocalAsyncSocket::makePair();

        result.ok = rmvolePprfRunLocalSparseVector<F, Ctx>(
            params, delta, sparseS, "s", sockets[0], sockets[1],
            result.sSetupSeconds, result.verifySeconds, prng, ctx);
        if (result.ok)
        {
            result.ok = rmvolePprfRunLocalSparseVector<F, Ctx>(
                params, delta, sparseE, "e", sockets[0], sockets[1],
                result.eSetupSeconds, result.verifySeconds, prng, ctx);
        }

        result.senderBytes = sockets[0].bytesSent();
        result.receiverBytes = sockets[1].bytesSent();
        result.totalBytes = result.senderBytes + result.receiverBytes;

        return result;
    }

    inline void rmvolePprfAppendTcpCsv(const RmvolePprfTcpSetupRow& row)
    {
        const auto path = std::filesystem::path("docs/rmvole_pprf_tcp_setup.csv");
        auto needsHeader = row.role == "sender-server"
            && (!std::filesystem::exists(path) || std::filesystem::file_size(path) == 0);
        std::ofstream out(path, std::ios::app);
        if (!out)
            throw std::runtime_error("failed to open docs/rmvole_pprf_tcp_setup.csv");

        if (needsHeader)
        {
            out << "method,role,network,address,logN,N,t,m,blockSize,reps,"
                << "median_s_setup_s,median_e_setup_s,median_total_setup_s,median_verify_s,"
                << "bytes_sent_by_role,bytes_received_by_role,total_local_socket_bytes,"
                << "scalar_pprf_count_s,scalar_pprf_count_e,total_scalar_pprf_count,"
                << "expanded_leaves_per_coordinate_per_sparse_vector,total_scalar_expanded_leaves,"
                << "total_scalar_expanded_leaves_over_2mN,ok,notes\n";
        }

        out << rmvolePprfCsvEscape(row.method) << ','
            << rmvolePprfCsvEscape(row.role) << ','
            << rmvolePprfCsvEscape(row.network) << ','
            << rmvolePprfCsvEscape(row.address) << ','
            << row.logN << ','
            << row.N << ','
            << row.t << ','
            << row.m << ','
            << row.blockSize << ','
            << row.reps << ','
            << std::setprecision(12) << row.medianSSetupSeconds << ','
            << std::setprecision(12) << row.medianESetupSeconds << ','
            << std::setprecision(12) << row.medianTotalSetupSeconds << ','
            << std::setprecision(12) << row.medianVerifySeconds << ','
            << row.bytesSentByRole << ','
            << row.bytesReceivedByRole << ','
            << row.totalLocalSocketBytes << ','
            << row.scalarPprfCountS << ','
            << row.scalarPprfCountE << ','
            << row.totalScalarPprfCount << ','
            << row.expandedLeavesPerCoordinatePerSparseVector << ','
            << row.totalScalarExpandedLeaves << ','
            << std::setprecision(12) << row.totalScalarExpandedLeavesOver2MN << ','
            << (row.ok ? 1 : 0) << ','
            << rmvolePprfCsvEscape(row.notes) << '\n';
    }

    template<typename F, typename Ctx>
    void rmvolePprfTcpServerScalar(
        const RmvolePprfNetSetupParams& params,
        cp::Socket& socket,
        PRNG& prng,
        Ctx& ctx)
    {
        using VecF = typename Ctx::template Vec<F>;

        RegularPprfSender<F, F, Ctx> sender;
        sender.configure(params.blockSize, params.t);

        VecF beta, senderOut;
        ctx.resize(beta, params.t);
        macoro::sync_wait(socket.recv(beta));

        std::vector<std::array<block, 2>> senderBaseOts(sender.baseOtCount());
#ifdef LIBOTE_HAS_BASE_OT
        DefaultBaseOT senderBaseOt;
        auto sendBase = senderBaseOt.send(senderBaseOts, prng, socket);
        macoro::sync_wait(std::move(sendBase));
#else
        throw std::runtime_error("RMVOLE_PPRF_NET_SETUP requires libOTe base OT support.");
#endif
        sender.setBase(senderBaseOts);

        ctx.resize(senderOut, params.N);
        auto sendPprf = sender.expand(
            socket,
            beta,
            prng.get<block>(),
            senderOut,
            PprfOutputFormat::ByTreeIndex,
            true,
            1,
            ctx);
        macoro::sync_wait(std::move(sendPprf));

        macoro::sync_wait(socket.send(coproto::copy(senderOut)));
    }

    template<typename F, typename Ctx>
    bool rmvolePprfTcpClientScalar(
        const RmvolePprfNetSetupParams& params,
        const std::vector<F>& delta,
        const RmvolePprfSparseInput<F>& input,
        const std::string& label,
        u64 coord,
        cp::Socket& socket,
        PRNG& prng,
        Ctx& ctx,
        double& verifySeconds)
    {
        using VecF = typename Ctx::template Vec<F>;

        RegularPprfReceiver<F, F, Ctx> receiver;
        receiver.configure(params.blockSize, params.t);
        auto choices = rmvolePprfChoiceBitsFromOffsets(input.offsets, receiver.mDepth);
        receiver.setChoiceBits(choices);

        auto beta = rmvolePprfBuildBeta<F, Ctx>(params, delta, input, coord, ctx);
        macoro::sync_wait(socket.send(coproto::copy(beta)));

        std::vector<block> receiverBaseOts(receiver.baseOtCount());
#ifdef LIBOTE_HAS_BASE_OT
        DefaultBaseOT receiverBaseOt;
        auto recvBase = receiverBaseOt.receive(choices, receiverBaseOts, prng, socket);
        macoro::sync_wait(std::move(recvBase));
#else
        throw std::runtime_error("RMVOLE_PPRF_NET_SETUP requires libOTe base OT support.");
#endif
        receiver.setBase(receiverBaseOts);

        VecF receiverOut, senderOut;
        ctx.resize(receiverOut, params.N);
        ctx.resize(senderOut, params.N);

        auto recvPprf = receiver.expand(
            socket,
            receiverOut,
            PprfOutputFormat::ByTreeIndex,
            true,
            1,
            ctx);
        macoro::sync_wait(std::move(recvPprf));

        macoro::sync_wait(socket.recv(senderOut));

        auto verifyStart = omp_get_wtime();
        auto ok = rmvolePprfVerifyScalar<F, Ctx>(params, input, beta, senderOut, receiverOut, coord, label, ctx);
        verifySeconds += omp_get_wtime() - verifyStart;
        return ok;
    }

    template<typename F, typename Ctx>
    double rmvolePprfTcpServerSparseVector(
        const RmvolePprfNetSetupParams& params,
        cp::Socket& socket,
        PRNG& prng,
        Ctx& ctx)
    {
        auto start = omp_get_wtime();
        for (u64 h = 0; h < params.m; ++h)
            rmvolePprfTcpServerScalar<F, Ctx>(params, socket, prng, ctx);
        return omp_get_wtime() - start;
    }

    template<typename F, typename Ctx>
    bool rmvolePprfTcpClientSparseVector(
        const RmvolePprfNetSetupParams& params,
        const std::vector<F>& delta,
        const RmvolePprfSparseInput<F>& input,
        const std::string& label,
        cp::Socket& socket,
        PRNG& prng,
        Ctx& ctx,
        double& setupSeconds,
        double& verifySeconds)
    {
        auto start = omp_get_wtime();
        for (u64 h = 0; h < params.m; ++h)
        {
            auto ok = rmvolePprfTcpClientScalar<F, Ctx>(
                params, delta, input, label, h, socket, prng, ctx, verifySeconds);
            if (!ok)
            {
                setupSeconds += omp_get_wtime() - start;
                return false;
            }
        }
        setupSeconds += omp_get_wtime() - start;
        return true;
    }

    template<typename F, typename Ctx>
    RmvolePprfTcpSetupRow rmvolePprfRunTcpRoleTyped(
        bool server,
        const std::string& host,
        const std::string& port,
        const RmvolePprfNetSetupParams& params,
        u64 reps,
        Ctx ctx = {})
    {
        auto address = host + ":" + port;
        auto socket = cp::asioConnect(address, server);
        PRNG prng(sysRandomSeed());
        std::vector<double> sTimes, eTimes, totalTimes, verifyTimes;

        RmvolePprfTcpSetupRow row;
        row.role = server ? "sender-server" : "receiver-client";
        row.network = rmvolePprfTcpNetworkLabel(host);
        row.address = address;
        row.logN = params.logN;
        row.N = params.N;
        row.t = params.t;
        row.m = params.m;
        row.blockSize = params.blockSize;
        row.reps = reps;
        rmvolePprfFillTcpCounters(params, row);
        row.notes = server
            ? "tcp_two_process; server_runs_RegularPprfSender; receives_test_beta; sends_sender_output_for_opening; DefaultBaseOT_repeated_per_scalar"
            : "tcp_two_process; client_generates_centralized_test_inputs; verifies_s_and_e; DefaultBaseOT_repeated_per_scalar";
        row.ok = true;

        for (u64 rep = 0; rep < reps; ++rep)
        {
            double sSetup = 0;
            double eSetup = 0;
            double verify = 0;

            if (server)
            {
                sSetup = rmvolePprfTcpServerSparseVector<F, Ctx>(params, socket, prng, ctx);
                eSetup = rmvolePprfTcpServerSparseVector<F, Ctx>(params, socket, prng, ctx);
            }
            else
            {
                std::vector<F> delta(params.m);
                for (u64 h = 0; h < params.m; ++h)
                    rmvolePprfSampleNonzero(delta[h], prng, ctx);
                auto sparseS = rmvolePprfSampleSparseInput<F, Ctx>(params, prng, ctx);
                auto sparseE = rmvolePprfSampleSparseInput<F, Ctx>(params, prng, ctx);

                row.ok = rmvolePprfTcpClientSparseVector<F, Ctx>(
                    params, delta, sparseS, "s", socket, prng, ctx, sSetup, verify);
                if (row.ok)
                {
                    row.ok = rmvolePprfTcpClientSparseVector<F, Ctx>(
                        params, delta, sparseE, "e", socket, prng, ctx, eSetup, verify);
                }
            }

            sTimes.push_back(sSetup);
            eTimes.push_back(eSetup);
            totalTimes.push_back(sSetup + eSetup);
            verifyTimes.push_back(verify);

            if (!row.ok)
                break;
        }

        macoro::sync_wait(socket.flush());

        row.medianSSetupSeconds = rmvolePprfMedian(sTimes);
        row.medianESetupSeconds = rmvolePprfMedian(eTimes);
        row.medianTotalSetupSeconds = rmvolePprfMedian(totalTimes);
        row.medianVerifySeconds = rmvolePprfMedian(verifyTimes);
        row.bytesSentByRole = socket.bytesSent();
        row.bytesReceivedByRole = socket.bytesReceived();
        row.totalLocalSocketBytes = row.bytesSentByRole + row.bytesReceivedByRole;
        return row;
    }

    inline void rmvolePprfPrintTcpRow(const RmvolePprfTcpSetupRow& row)
    {
        std::cout << std::fixed << std::setprecision(6)
                  << "RMVOLE_PPRF_NET_SETUP"
                  << " role=" << row.role
                  << " network=" << row.network
                  << " address=" << row.address
                  << " logN=" << row.logN
                  << " N=" << row.N
                  << " t=" << row.t
                  << " m=" << row.m
                  << " blockSize=" << row.blockSize
                  << " reps=" << row.reps
                  << " s_setup_total_s=" << row.medianSSetupSeconds
                  << " e_setup_total_s=" << row.medianESetupSeconds
                  << " total_setup_s=" << row.medianTotalSetupSeconds
                  << " verify_s=" << row.medianVerifySeconds
                  << " sender_bytes_sent=" << row.bytesSentByRole
                  << " receiver_bytes_sent=" << row.bytesReceivedByRole
                  << " total_bytes=" << row.totalLocalSocketBytes
                  << " scalar_pprf_count_s=" << row.scalarPprfCountS
                  << " scalar_pprf_count_e=" << row.scalarPprfCountE
                  << " total_scalar_pprf_count=" << row.totalScalarPprfCount
                  << " expanded_leaves_per_coordinate_per_sparse_vector=" << row.expandedLeavesPerCoordinatePerSparseVector
                  << " total_scalar_expanded_leaves=" << row.totalScalarExpandedLeaves
                  << " total_scalar_expanded_leaves_over_2mN=" << row.totalScalarExpandedLeavesOver2MN
                  << " notes=" << row.notes
                  << " " << (row.ok ? "PASS" : "FAIL")
                  << std::endl;
    }

    inline void rmvolePprfWriteDoc()
    {
        std::filesystem::create_directories("docs");
        std::ofstream out("docs/rmvole_pprf_net_setup_test.md");
        out << "# RM-VOLE PPRF Network Setup Test\n\n"
            << "Phase 6E adds `RmvolePprfNetSetupTest.h` and CLI flag `--RMVOLE_PPRF_NET_SETUP`.\n\n"
            << "## Modes\n\n"
            << "```bash\n"
            << "./build/main --RMVOLE_PPRF_NET_SETUP local <logN> <t> <m>\n"
            << "./build/main --RMVOLE_PPRF_NET_SETUP server <host_or_0.0.0.0> <port> <logN> <t> <m> <reps>\n"
            << "./build/main --RMVOLE_PPRF_NET_SETUP client <host> <port> <logN> <t> <m> <reps>\n"
            << "```\n\n"
            << "Example TCP loopback run:\n\n"
            << "```bash\n"
            << "./build/main --RMVOLE_PPRF_NET_SETUP server 0.0.0.0 12220 12 8 8 3\n"
            << "./build/main --RMVOLE_PPRF_NET_SETUP client 127.0.0.1 12220 12 8 8 3\n"
            << "```\n\n"
            << "## API Used\n\n"
            << "- libOTe API: `RegularPprfSender<F,F,Ctx>` and `RegularPprfReceiver<F,F,Ctx>` from `libOTe/Tools/Pprf/RegularPprf.h`.\n"
            << "- Field instantiation: `F = u64`, `Ctx = CoeffCtxIntegerPrime_64`.\n"
            << "- Local channel: `coproto::LocalAsyncSocket::makePair()`.\n"
            << "- TCP channel: `coproto::asioConnect(address, server)`, with server running `RegularPprfSender` and client running `RegularPprfReceiver`.\n"
            << "- Base OT: `DefaultBaseOT::send()` and `DefaultBaseOT::receive()` are run before every scalar RegularPprf instance.\n"
            << "- PPRF expansion: sender calls `expand(socket, beta, seed, senderOut, PprfOutputFormat::ByTreeIndex, true, 1, ctx)` and receiver calls `expand(socket, receiverOut, PprfOutputFormat::ByTreeIndex, true, 1, ctx)`.\n\n"
            << "## Relation Tested\n\n"
            << "For `N = 2^logN`, `blockSize = N/t`, and each block `i`, the harness centrally samples sparse offsets and nonzero scalars for both `s` and `e`, plus extension-coordinate scalars `Delta_h`. It programs two sparse vectors:\n\n"
            << "```text\n"
            << "betaS_{i,h} = Delta_h * s_i in F_p\n"
            << "betaE_{i,h} = Delta_h * e_i in F_p\n"
            << "```\n\n"
            << "RegularPprf reconstructs `receiverOut = senderOut + beta` at the selected point and `receiverOut = senderOut` elsewhere. The RM-VOLE setup shares are interpreted as:\n\n"
            << "```text\n"
            << "share0_h[j] = -senderOut_h[j]\n"
            << "share1_h[j] =  receiverOut_h[j]\n"
            << "share0_h[j] + share1_h[j] = betaS or betaE at its support, else 0\n"
            << "```\n\n"
            << "Local mode opens simulated shares in one process and checks all `2*m*N` scalar positions. TCP mode has the client centrally generate correctness-test inputs and send each scalar `beta` vector to the server; after each PPRF, the server sends its output share back to the client so the client can open and verify. This metadata/share opening is only for the correctness harness.\n\n"
            << "## Counters And Bytes\n\n"
            << "`scalar_pprf_count_s = m`, `scalar_pprf_count_e = m`, `total_scalar_pprf_count = 2*m`, `expanded_leaves_per_coordinate_per_sparse_vector = N`, and `total_scalar_expanded_leaves = 2*m*N`. TCP rows are appended to `docs/rmvole_pprf_tcp_setup.csv`.\n\n"
            << "The socket byte counters in TCP mode are local per process and include test metadata (`beta` sent from client to server) and verification opening traffic (`senderOut` sent from server to client), in addition to `DefaultBaseOT` and `RegularPprf` messages. The repeated `DefaultBaseOT` per scalar PPRF is a likely overestimate and a future batching/reuse target.\n\n"
            << "## Caveats\n\n"
            << "- This is a semi-honest correctness/API harness, not secure input generation.\n"
            << "- It uses real coproto sockets and libOTe `DefaultBaseOT`/`RegularPprf` network messages.\n"
            << "- It still uses centralized test input generation.\n"
            << "- It runs `m` independent scalar RegularPprf instances for `s` and another `m` for `e`; it is not the final shared-path vector-valued PPRF.\n"
            << "- It is setup only, not the full RM-VOLE expand path or LAN runtime for the whole construction.\n\n"
            << "## Next Step\n\n"
            << "Batch or reuse base OT material across scalar PPRFs where valid, then replace the coordinate-wise adapter with a shared-path vector-valued PPRF so path/base material is paid once and only coordinate payloads scale with `m`.\n";
    }

    inline void rmvolePprfPrintLocalRow(
        const RmvolePprfNetSetupParams& params,
        const RmvolePprfNetSetupResult& result)
    {
        std::cout << std::fixed << std::setprecision(6)
                  << "RMVOLE_PPRF_NET_SETUP"
                  << " mode=local"
                  << " field=Fp64"
                  << " logN=" << params.logN
                  << " N=" << params.N
                  << " t=" << params.t
                  << " m=" << params.m
                  << " blockSize=" << params.blockSize
                  << " s_setup_total_s=" << result.sSetupSeconds
                  << " e_setup_total_s=" << result.eSetupSeconds
                  << " total_setup_s=" << (result.sSetupSeconds + result.eSetupSeconds)
                  << " verify_s=" << result.verifySeconds
                  << " sender_bytes_sent=" << result.senderBytes
                  << " receiver_bytes_sent=" << result.receiverBytes
                  << " total_bytes=" << result.totalBytes
                  << " scalar_pprf_count_s=" << result.scalarPprfCountS
                  << " scalar_pprf_count_e=" << result.scalarPprfCountE
                  << " total_scalar_pprf_count=" << result.totalScalarPprfCount
                  << " expanded_leaves_per_coordinate_per_sparse_vector=" << result.expandedLeavesPerCoordinatePerSparseVector
                  << " total_scalar_expanded_leaves=" << result.totalScalarExpandedLeaves
                  << " total_scalar_expanded_leaves_over_2mN=" << result.totalScalarExpandedLeavesOver2MN
                  << " notes=" << result.notes
                  << " " << (result.ok ? "PASS" : "FAIL")
                  << std::endl;
    }

    inline int RmvolePprfNetSetupTest(int argc, char** argv)
    {
        if (argc >= 3 && std::string(argv[2]) == "local")
        {
            if (argc != 6)
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
            rmvolePprfPrintLocalRow(params, result);
            return result.ok ? 0 : 1;
        }

        if (argc >= 3 && (std::string(argv[2]) == "server" || std::string(argv[2]) == "client"))
        {
            if (argc != 9)
            {
                std::cerr << "Usage: ./build/main --RMVOLE_PPRF_NET_SETUP server <host_or_0.0.0.0> <port> <logN> <t> <m> <reps>\n"
                          << "       ./build/main --RMVOLE_PPRF_NET_SETUP client <host> <port> <logN> <t> <m> <reps>" << std::endl;
                return 1;
            }

            auto server = std::string(argv[2]) == "server";
            auto host = std::string(argv[3]);
            auto port = std::string(argv[4]);
            auto logN = static_cast<u64>(std::stoull(argv[5]));
            auto t = static_cast<u64>(std::stoull(argv[6]));
            auto m = static_cast<u64>(std::stoull(argv[7]));
            auto reps = static_cast<u64>(std::stoull(argv[8]));
            if (!reps)
                throw std::runtime_error("RMVOLE_PPRF_NET_SETUP TCP mode requires reps > 0.");

            auto params = rmvolePprfMakeParams(logN, t, m);
            rmvolePprfWriteDoc();
            auto row = rmvolePprfRunTcpRoleTyped<u64, CoeffCtxIntegerPrime_64>(
                server, host, port, params, reps);
            rmvolePprfPrintTcpRow(row);
            rmvolePprfAppendTcpCsv(row);
            return row.ok ? 0 : 1;
        }

        std::cerr << "Usage: ./build/main --RMVOLE_PPRF_NET_SETUP local <logN> <t> <m>\n"
                  << "       ./build/main --RMVOLE_PPRF_NET_SETUP server <host_or_0.0.0.0> <port> <logN> <t> <m> <reps>\n"
                  << "       ./build/main --RMVOLE_PPRF_NET_SETUP client <host> <port> <logN> <t> <m> <reps>" << std::endl;
        return 1;
    }
}
