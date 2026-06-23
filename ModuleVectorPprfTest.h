#pragma once

#include "Coeff128.h"
#include "cryptoTools/Common/Matrix.h"
#include "cryptoTools/Crypto/AES.h"
#include "cryptoTools/Crypto/PRNG.h"
#include <iomanip>
#include <iostream>
#include <omp.h>
#include <stdexcept>
#include <string>
#include <vector>

namespace osuCrypto
{
    struct ModuleVectorPprfParams
    {
        u64 N = 1024;
        u64 t = 8;
        u64 m = 8;
    };

    template<typename F, typename Ctx>
    struct ModuleVectorPprfPoint
    {
        u64 alpha = 0;
        block rootSeed = ZeroBlock;

        // beta is one F_{p^m} payload represented by m coordinates over F_p.
        typename Ctx::template Vec<F> beta;
    };

    struct ModuleVectorPprfTiming
    {
        double setupSeconds = 0;
        double expandSeconds = 0;
        double verifySeconds = 0;
        bool ok = false;
    };

    inline bool moduleVectorPprfIsPowerOfTwo(u64 n)
    {
        return n && ((n & (n - 1)) == 0);
    }

    template<typename F, typename Ctx>
    void moduleVectorPprfZero(Matrix<F>& mtx, u64 rows, u64 cols, Ctx&)
    {
        mtx.resize(rows, cols);
        for (u64 h = 0; h < rows; ++h)
            for (u64 j = 0; j < cols; ++j)
                mtx(h, j) = F{};
    }

    template<typename F, typename Ctx>
    void moduleVectorPprfAdd(Matrix<F>& mtx, u64 row, u64 col, const F& value, Ctx& ctx)
    {
        F sum;
        ctx.plus(sum, mtx(row, col), value);
        mtx(row, col) = sum;
    }

    template<typename F, typename Ctx>
    F moduleVectorPprfNeg(const F& value, Ctx& ctx)
    {
        F zero{};
        F v = value;
        if (ctx.eq(v, zero))
            return zero;
        return static_cast<F>(ctx.prime() - v);
    }

    inline void moduleVectorPprfExpandSharedTree(
        block rootSeed,
        u64 N,
        std::vector<block>& leaves)
    {
        AES aes(block(0x4d56505246545245ull, 0x455850414e445345ull));

        // Local prototype expansion: keep the complete frontier in one buffer
        // and expand it in reverse so child writes do not clobber live parents.
        leaves.resize(N);
        leaves[0] = rootSeed;

        for (u64 level = 0, width = 1; width < N; ++level, width *= 2)
        {
            for (u64 i = width; i-- > 0;)
            {
                auto leftTweak = block(level, 2 * i);
                auto rightTweak = block(level, 2 * i + 1);
                auto parent = leaves[i];
                leaves[2 * i] = aes.hashBlock(parent ^ leftTweak);
                leaves[2 * i + 1] = aes.hashBlock(parent ^ rightTweak);
            }
        }
    }

    template<typename F, typename Ctx>
    void moduleVectorPprfSample(
        const ModuleVectorPprfParams& params,
        std::vector<ModuleVectorPprfPoint<F, Ctx>>& points,
        Matrix<F>& expected,
        PRNG& prng,
        Ctx& ctx)
    {
        points.resize(params.t);
        moduleVectorPprfZero<F, Ctx>(expected, params.m, params.N, ctx);

        for (u64 i = 0; i < params.t; ++i)
        {
            auto& point = points[i];
            point.alpha = prng.get<u64>() % params.N;
            point.rootSeed = prng.get<block>();
            ctx.resize(point.beta, params.m);

            for (u64 h = 0; h < params.m; ++h)
            {
                ctx.fromBlock(point.beta[h], prng.get<block>() ^ block(i, h));
                moduleVectorPprfAdd<F, Ctx>(expected, h, point.alpha, point.beta[h], ctx);
            }
        }
    }

    template<typename F, typename Ctx>
    void moduleVectorPprfExpand(
        const ModuleVectorPprfParams& params,
        const std::vector<ModuleVectorPprfPoint<F, Ctx>>& points,
        Matrix<F>& y0,
        Matrix<F>& y1,
        Ctx& ctx)
    {
        AES aes(block(0x4d565052464c4541ull, 0x4656414c55455331ull));
        std::vector<block> leaves;

        moduleVectorPprfZero<F, Ctx>(y0, params.m, params.N, ctx);
        moduleVectorPprfZero<F, Ctx>(y1, params.m, params.N, ctx);

        for (u64 i = 0; i < static_cast<u64>(points.size()); ++i)
        {
            const auto& point = points[i];
            moduleVectorPprfExpandSharedTree(point.rootSeed, params.N, leaves);

            for (u64 j = 0; j < params.N; ++j)
            {
                for (u64 h = 0; h < params.m; ++h)
                {
                    F value;
                    ctx.fromBlock(value, aes.hashBlock(leaves[j] ^ block(i ^ h, h)));

                    moduleVectorPprfAdd<F, Ctx>(y0, h, j, value, ctx);
                    auto neg = moduleVectorPprfNeg<F, Ctx>(value, ctx);
                    moduleVectorPprfAdd<F, Ctx>(y1, h, j, neg, ctx);
                }
            }

            for (u64 h = 0; h < params.m; ++h)
                moduleVectorPprfAdd<F, Ctx>(y1, h, point.alpha, point.beta[h], ctx);
        }
    }

    template<typename F, typename Ctx>
    bool moduleVectorPprfVerify(
        const ModuleVectorPprfParams& params,
        const Matrix<F>& y0,
        const Matrix<F>& y1,
        const Matrix<F>& expected,
        Ctx& ctx,
        std::ostream* err = nullptr)
    {
        for (u64 h = 0; h < params.m; ++h)
        {
            for (u64 j = 0; j < params.N; ++j)
            {
                F actual;
                ctx.plus(actual, y0(h, j), y1(h, j));
                F exp = expected(h, j);
                if (!ctx.eq(actual, exp))
                {
                    if (err)
                    {
                        *err << "MODULE_VECTOR_PPRF_TEST FAIL h=" << h
                             << " j=" << j
                             << " actual=" << ctx.str(actual)
                             << " expected=" << ctx.str(exp)
                             << std::endl;
                    }
                    return false;
                }
            }
        }
        return true;
    }

    template<typename F, typename Ctx>
    ModuleVectorPprfTiming moduleVectorPprfRunOnce(
        const ModuleVectorPprfParams& params,
        Ctx ctx = {})
    {
        if (!moduleVectorPprfIsPowerOfTwo(params.N))
            throw std::runtime_error("MODULE_VECTOR_PPRF_TEST requires N to be a power of two.");

        PRNG prng(sysRandomSeed());
        std::vector<ModuleVectorPprfPoint<F, Ctx>> points;
        Matrix<F> expected, y0, y1;
        ModuleVectorPprfTiming timing;

        auto start = omp_get_wtime();
        moduleVectorPprfSample<F, Ctx>(params, points, expected, prng, ctx);
        auto end = omp_get_wtime();
        timing.setupSeconds = end - start;

        start = omp_get_wtime();
        moduleVectorPprfExpand<F, Ctx>(params, points, y0, y1, ctx);
        end = omp_get_wtime();
        timing.expandSeconds = end - start;

        start = omp_get_wtime();
        timing.ok = moduleVectorPprfVerify<F, Ctx>(params, y0, y1, expected, ctx, &std::cout);
        end = omp_get_wtime();
        timing.verifySeconds = end - start;

        return timing;
    }

    template<typename F, typename Ctx>
    bool moduleVectorPprfRun(const ModuleVectorPprfParams& params, const std::string& label)
    {
        auto timing = moduleVectorPprfRunOnce<F, Ctx>(params);
        auto total = timing.setupSeconds + timing.expandSeconds + timing.verifySeconds;
        auto outputElems = params.m * params.N;
        auto throughput = total > 0 ? static_cast<double>(outputElems) / total : 0.0;

        std::cout << std::fixed << std::setprecision(6)
                  << "MODULE_VECTOR_PPRF_TEST " << label
                  << " N=" << params.N
                  << " t=" << params.t
                  << " m=" << params.m
                  << " output_elems=" << outputElems
                  << " setup_s=" << timing.setupSeconds
                  << " expand_s=" << timing.expandSeconds
                  << " verify_s=" << timing.verifySeconds
                  << " total_s=" << total
                  << " throughput_elems_per_s=" << throughput
                  << " shared_path=1"
                  << " " << (timing.ok ? "PASS" : "FAIL")
                  << std::endl;

        return timing.ok;
    }

    inline ModuleVectorPprfParams moduleVectorPprfPreset(u64 preset)
    {
        ModuleVectorPprfParams params;
        if (preset == 1)
        {
            params.N = 1024;
            params.t = 8;
            params.m = 8;
        }
        else if (preset == 2)
        {
            params.N = 4096;
            params.t = 16;
            params.m = 16;
        }
        else if (preset == 3)
        {
            params.N = 16384;
            params.t = 32;
            params.m = 16;
        }
        else
        {
            params.N = 1024;
            params.t = 8;
            params.m = 8;
        }
        return params;
    }

    inline int ModuleVectorPprf_Test(u64 preset)
    {
        auto params = moduleVectorPprfPreset(preset);
        auto ok64 = moduleVectorPprfRun<u64, CoeffCtxIntegerPrime_64>(params, "Fp64");
        return ok64 ? 0 : 1;
    }
}
