#pragma once

#include "ModuleMVOLE.h"
#include "RmvoleNetBench.h"
#include "libOTe/Tools/ExConvCode/ExConvCode.h"
#include "libOTe/Vole/Silent/SilentVoleReceiver.h"
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace osuCrypto
{
    struct EncoderOnlyBenchRow
    {
        std::string method;
        std::string encoder;
        u64 logN = 0;
        u64 N = 0;
        u64 m = 0;
        u64 trialIndex = 0;
        u64 warmup = 0;
        u64 requestedN = 0;
        u64 generatedN = 0;
        u64 codeSize = 0;
        double baseFieldSeconds = 0;
        double coordinateSeconds = 0;
        double pointwiseSeconds = 0;
        double materializationSeconds = 0;
        double totalSeconds = 0;
        double nsPerRmVoleCoordinate = 0;
        double nsPerBaseFieldMatrixEntry = 0;
        std::string notes;
    };

    inline void encoderOnlyPrintRow(const EncoderOnlyBenchRow& row)
    {
        std::cout << std::fixed << std::setprecision(6)
                  << "ENCODER_ONLY_BENCH"
                  << " method=" << row.method
                  << " encoder=" << row.encoder
                  << " logN=" << row.logN
                  << " N=" << row.N
                  << " m=" << row.m
                  << " trial_index=" << row.trialIndex
                  << " warmup=" << row.warmup
                  << " requested_N=" << row.requestedN
                  << " generated_N=" << row.generatedN
                  << " code_size=" << row.codeSize
                  << " base_field_wht_s=" << row.baseFieldSeconds
                  << " coordinate_wht_m_s=" << row.coordinateSeconds
                  << " pointwise_ops_s=" << row.pointwiseSeconds
                  << " output_materialization_s=" << row.materializationSeconds
                  << " total_s=" << row.totalSeconds
                  << " ns_per_rm_vole_coordinate=" << row.nsPerRmVoleCoordinate
                  << " ns_per_base_field_matrix_entry=" << row.nsPerBaseFieldMatrixEntry
                  << " notes=" << row.notes
                  << " PASS" << std::endl;
    }

    template<u64 M>
    EncoderOnlyBenchRow encoderOnlyRunWht(u64 logN, u64 trialIndex, bool warmup)
    {
        CoeffCtxIntegerPrime_64 scalarCtx;
        PRNG prng(sysRandomSeed());
        ModuleMVOLEParams params;
        params.n = logN;
        params.N = 1ull << logN;
        params.t = 64;
        params.m = M;

        typename CoeffCtxIntegerPrime_64::template Vec<u64> base;
        scalarCtx.resize(base, params.N);
        std::vector<typename CoeffCtxIntegerPrime_64::template Vec<u64>> rows(M);
        for (u64 j = 0; j < params.N; ++j)
            scalarCtx.fromBlock(base[j], prng.get<block>());
        for (auto& row : rows)
        {
            scalarCtx.resize(row, params.N);
            for (u64 j = 0; j < params.N; ++j)
                scalarCtx.fromBlock(row[j], prng.get<block>());
        }

        Matrix<u64> materialized(M, params.N);
        auto totalStart = omp_get_wtime();
        auto start = omp_get_wtime();
        wht<u64, CoeffCtxIntegerPrime_64>(base, static_cast<int>(params.N));
        auto baseSeconds = omp_get_wtime() - start;

        start = omp_get_wtime();
        for (auto& row : rows)
            wht<u64, CoeffCtxIntegerPrime_64>(row, static_cast<int>(params.N));
        auto coordinateSeconds = omp_get_wtime() - start;

        start = omp_get_wtime();
        for (u64 h = 0; h < M; ++h)
        {
            for (u64 j = 0; j < params.N; ++j)
            {
                u64 prod{};
                scalarCtx.mul(prod, rows[h][j], base[j]);
                scalarCtx.plus(rows[h][j], rows[h][j], prod);
            }
        }
        auto pointwiseSeconds = omp_get_wtime() - start;

        start = omp_get_wtime();
        for (u64 h = 0; h < M; ++h)
            for (u64 j = 0; j < params.N; ++j)
                materialized(h, j) = rows[h][j];
        auto materializationSeconds = omp_get_wtime() - start;
        auto totalSeconds = omp_get_wtime() - totalStart;

        EncoderOnlyBenchRow out;
        out.method = "WHT_ENCODER";
        out.encoder = "WHT";
        out.logN = logN;
        out.N = params.N;
        out.m = M;
        out.trialIndex = trialIndex;
        out.warmup = warmup ? 1 : 0;
        out.requestedN = params.N;
        out.generatedN = params.N;
        out.codeSize = params.N;
        out.baseFieldSeconds = baseSeconds;
        out.coordinateSeconds = coordinateSeconds;
        out.pointwiseSeconds = pointwiseSeconds;
        out.materializationSeconds = materializationSeconds;
        out.totalSeconds = totalSeconds;
        out.nsPerRmVoleCoordinate = params.N ? totalSeconds * 1e9 / static_cast<double>(params.N) : 0;
        out.nsPerBaseFieldMatrixEntry = params.N && M ? totalSeconds * 1e9 / static_cast<double>(params.N * M) : 0;
        out.notes = "encoder_only; no_PPRF_setup; no_base_correlations; base_field_WHT_plus_m_coordinate_WHTs";
        return out;
    }

    template<u64 M>
    EncoderOnlyBenchRow encoderOnlyRunExConv(u64 logN, u64 trialIndex, bool warmup)
    {
        using ExtElem = RmvolePprfPrimeVector<M>;
        using ExtCtx = CoeffCtxPrimeArray64<M>;
        using VecF = typename ExtCtx::template Vec<ExtElem>;
        using VecG = typename ExtCtx::template Vec<u64>;

        ExtCtx vectorCtx;
        CoeffCtxIntegerPrime_64 scalarCtx;
        PRNG prng(sysRandomSeed());
        u64 requestSize = 1ull << logN;

        SilentVoleReceiver<ExtElem, u64, ExtCtx> receiver;
        receiver.mMultType = MultType::ExConv7x24;
        receiver.configure(requestSize, SilentBaseType::Base, 128, vectorCtx);

        u64 scaler{}, expanderWeight{}, accumulatorWeight{};
        double minDist{};
        ExConvConfigure(MultType::ExConv7x24, scaler, expanderWeight, accumulatorWeight, minDist);
        ExConvCode encoder;
        encoder.config(receiver.mRequestSize, receiver.mNoiseVecSize, expanderWeight, accumulatorWeight);

        VecG base;
        VecF ext;
        vectorCtx.resize(base, receiver.mNoiseVecSize);
        vectorCtx.resize(ext, receiver.mNoiseVecSize);
        for (u64 j = 0; j < receiver.mNoiseVecSize; ++j)
        {
            scalarCtx.fromBlock(base[j], prng.get<block>());
            for (u64 h = 0; h < M; ++h)
                scalarCtx.fromBlock(ext[j][h], prng.get<block>());
        }

        Matrix<u64> materialized(M, receiver.mRequestSize);
        auto totalStart = omp_get_wtime();
        auto start = omp_get_wtime();
        encoder.dualEncode<u64, CoeffCtxIntegerPrime_64>(base.begin(), scalarCtx);
        auto baseSeconds = omp_get_wtime() - start;

        start = omp_get_wtime();
        encoder.dualEncode<ExtElem, ExtCtx>(ext.begin(), vectorCtx);
        auto coordinateSeconds = omp_get_wtime() - start;

        start = omp_get_wtime();
        auto pointwiseSeconds = omp_get_wtime() - start;

        start = omp_get_wtime();
        for (u64 h = 0; h < M; ++h)
            for (u64 j = 0; j < receiver.mRequestSize; ++j)
                materialized(h, j) = ext[j][h];
        auto materializationSeconds = omp_get_wtime() - start;
        auto totalSeconds = omp_get_wtime() - totalStart;

        EncoderOnlyBenchRow out;
        out.method = "EXCONV7X24_ENCODER";
        out.encoder = "ExConv7x24";
        out.logN = logN;
        out.N = requestSize;
        out.m = M;
        out.trialIndex = trialIndex;
        out.warmup = warmup ? 1 : 0;
        out.requestedN = requestSize;
        out.generatedN = receiver.mRequestSize;
        out.codeSize = receiver.mNoiseVecSize;
        out.baseFieldSeconds = baseSeconds;
        out.coordinateSeconds = coordinateSeconds;
        out.pointwiseSeconds = pointwiseSeconds;
        out.materializationSeconds = materializationSeconds;
        out.totalSeconds = totalSeconds;
        out.nsPerRmVoleCoordinate = requestSize ? totalSeconds * 1e9 / static_cast<double>(requestSize) : 0;
        out.nsPerBaseFieldMatrixEntry = requestSize && M ? totalSeconds * 1e9 / static_cast<double>(requestSize * M) : 0;
        out.notes = "encoder_only; no_PPRF_setup; no_base_correlations; silent_sVOLE_ExConv7x24_parameters";
        return out;
    }

    template<u64 M>
    int encoderOnlyBenchTyped(const std::string& which, u64 logN, u64 reps)
    {
        if (which == "wht" || which == "all")
        {
            encoderOnlyPrintRow(encoderOnlyRunWht<M>(logN, 0, true));
            for (u64 trial = 0; trial < reps; ++trial)
                encoderOnlyPrintRow(encoderOnlyRunWht<M>(logN, trial, false));
        }
        if (which == "exconv7x24" || which == "exconv" || which == "all")
        {
            encoderOnlyPrintRow(encoderOnlyRunExConv<M>(logN, 0, true));
            for (u64 trial = 0; trial < reps; ++trial)
                encoderOnlyPrintRow(encoderOnlyRunExConv<M>(logN, trial, false));
        }
        return 0;
    }

    inline int EncoderOnlyBench(int argc, char** argv)
    {
        if (argc != 6)
        {
            std::cerr << "Usage: ./build/main --ENCODER_ONLY_BENCH <wht|exconv7x24|all> <logN> <m> <measured_reps>" << std::endl;
            return 1;
        }
        auto which = std::string(argv[2]);
        auto logN = static_cast<u64>(std::stoull(argv[3]));
        auto m = static_cast<u64>(std::stoull(argv[4]));
        auto reps = static_cast<u64>(std::stoull(argv[5]));
        if (!reps)
            throw std::runtime_error("ENCODER_ONLY_BENCH requires measured_reps > 0.");

        switch (m)
        {
        case 8: return encoderOnlyBenchTyped<8>(which, logN, reps);
        case 16: return encoderOnlyBenchTyped<16>(which, logN, reps);
        case 32: return encoderOnlyBenchTyped<32>(which, logN, reps);
        default: throw std::runtime_error("ENCODER_ONLY_BENCH supports m = 8, 16, 32.");
        }
    }
}
