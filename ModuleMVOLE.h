#pragma once

#include "Coeff128.h"
#include "Walsh.h"
#include "cryptoTools/Common/Matrix.h"
#include "cryptoTools/Crypto/PRNG.h"
#include <iomanip>
#include <iostream>
#include <omp.h>
#include <string>
#include <vector>

namespace osuCrypto
{
    struct ModuleMVOLEParams
    {
        u64 n = 8;
        u64 N = 1ull << 8;
        u64 t = 4;
        u64 m = 4;
    };

    template<typename F, typename Ctx>
    struct ModuleMVOLEP0Output
    {
        // x is the base-ring value in R_p after the WHT/evaluation map.
        typename Ctx::template Vec<F> x;

        // Z0 represents an R_{p^m} share as m rows of N base-field coordinates.
        Matrix<F> Z0;
    };

    template<typename F, typename Ctx>
    struct ModuleMVOLEP1Output
    {
        // Delta is an F_{p^m} scalar represented as m coordinates over F_p.
        typename Ctx::template Vec<F> Delta;

        // Z1 represents an R_{p^m} share as m rows of N base-field coordinates.
        Matrix<F> Z1;
    };

    template<typename F, typename Ctx>
    struct ModuleMVOLEResult
    {
        ModuleMVOLEP0Output<F, Ctx> p0;
        ModuleMVOLEP1Output<F, Ctx> p1;
    };

    template<typename F, typename Ctx>
    struct ModuleMVOLEBaseState
    {
        typename Ctx::template Vec<F> rho;
        typename Ctx::template Vec<F> rhoWht;
        typename Ctx::template Vec<F> s;
        typename Ctx::template Vec<F> e;
        typename Ctx::template Vec<F> b;
    };

    template<typename F, typename Ctx>
    struct ModuleMVOLERowMask
    {
        typename Ctx::template Vec<F> V;
        typename Ctx::template Vec<F> W0;
        typename Ctx::template Vec<F> U;
        typename Ctx::template Vec<F> W1;
    };

    template<typename F, typename Ctx>
    struct ModuleMVOLEGenState
    {
        ModuleMVOLEBaseState<F, Ctx> base;

        // Coordinate representation psi(Delta) of Delta in F_{p^m}.
        typename Ctx::template Vec<F> Delta;
        std::vector<ModuleMVOLERowMask<F, Ctx>> rowMasks;
    };

    template<typename F, typename Ctx>
    void moduleMvoleRingMul(
        const typename Ctx::template Vec<F>& lhs,
        const typename Ctx::template Vec<F>& rhs,
        typename Ctx::template Vec<F>& out,
        Ctx& ctx)
    {
        auto N = static_cast<u64>(lhs.size());
        ctx.resize(out, N);
        ctx.zero(out.begin(), out.end());

        for (u64 i = 0; i < N; ++i)
        {
            for (u64 j = 0; j < N; ++j)
            {
                F prod;
                F sum;
                ctx.mul(prod, lhs[i], rhs[j]);
                ctx.plus(sum, out[i ^ j], prod);
                out[i ^ j] = sum;
            }
        }
    }

    template<typename F, typename Ctx>
    void moduleMvoleRingMulSparseRhs(
        const typename Ctx::template Vec<F>& lhs,
        const typename Ctx::template Vec<F>& rhs,
        typename Ctx::template Vec<F>& out,
        Ctx& ctx)
    {
        auto N = static_cast<u64>(lhs.size());
        F zero{};
        ctx.resize(out, N);
        ctx.zero(out.begin(), out.end());

        for (u64 j = 0; j < N; ++j)
        {
            if (ctx.eq(rhs[j], zero))
                continue;

            for (u64 i = 0; i < N; ++i)
            {
                F prod;
                F sum;
                ctx.mul(prod, lhs[i], rhs[j]);
                ctx.plus(sum, out[i ^ j], prod);
                out[i ^ j] = sum;
            }
        }
    }

    template<typename F, typename Ctx>
    void moduleMvoleSampleSparse(
        typename Ctx::template Vec<F>& out,
        u64 N,
        u64 weight,
        PRNG& prng,
        Ctx& ctx)
    {
        ctx.resize(out, N);
        ctx.zero(out.begin(), out.end());

        std::vector<u8> used(N, 0);
        for (u64 count = 0; count < weight; ++count)
        {
            u64 idx = prng.get<u64>() % N;
            while (used[idx])
                idx = prng.get<u64>() % N;
            used[idx] = 1;

            F zero{};
            do
            {
                ctx.fromBlock(out[idx], prng.get());
            } while (ctx.eq(out[idx], zero));
        }
    }

    template<typename F, typename Ctx>
    ModuleMVOLEBaseState<F, Ctx> moduleMvoleGenBase(
        const ModuleMVOLEParams& params,
        PRNG& prng,
        Ctx ctx = {})
    {
        using VecF = typename Ctx::template Vec<F>;

        ModuleMVOLEBaseState<F, Ctx> state;
        VecF rhoS;

        ctx.resize(state.rho, params.N);
        for (u64 j = 0; j < params.N; ++j)
            ctx.fromBlock(state.rho[j], prng.get());
        ctx.resize(state.rhoWht, params.N);
        ctx.copy(state.rho.begin(), state.rho.end(), state.rhoWht.begin());
        wht<F, Ctx>(state.rhoWht, static_cast<int>(params.N));

        moduleMvoleSampleSparse<F, Ctx>(state.s, params.N, params.t, prng, ctx);
        moduleMvoleSampleSparse<F, Ctx>(state.e, params.N, params.t, prng, ctx);

        moduleMvoleRingMulSparseRhs<F, Ctx>(state.rho, state.s, rhoS, ctx);
        ctx.resize(state.b, params.N);
        for (u64 j = 0; j < params.N; ++j)
            ctx.plus(state.b[j], rhoS[j], state.e[j]);

        return state;
    }

    template<typename F, typename Ctx>
    void moduleMvoleGenDelta(
        typename Ctx::template Vec<F>& Delta,
        const ModuleMVOLEParams& params,
        PRNG& prng,
        Ctx ctx = {})
    {
        // Sample the coordinate vector psi(Delta). The prototype does not
        // implement general F_{p^m} arithmetic; multiplication by x in R_p
        // is coordinate-wise scaling by base-field ring coordinates.
        ctx.resize(Delta, params.m);
        for (u64 h = 0; h < params.m; ++h)
            ctx.fromBlock(Delta[h], prng.get());
    }

    template<typename F, typename Ctx>
    void moduleMvoleGenRowMasks(
        std::vector<ModuleMVOLERowMask<F, Ctx>>& rowMasks,
        const ModuleMVOLEParams& params,
        const ModuleMVOLEBaseState<F, Ctx>& base,
        const typename Ctx::template Vec<F>& Delta,
        PRNG& prng,
        Ctx ctx = {})
    {
        rowMasks.resize(params.m);

        using VecF = typename Ctx::template Vec<F>;
        VecF deltaS, deltaE;
        ctx.resize(deltaS, params.N);
        ctx.resize(deltaE, params.N);

        for (u64 h = 0; h < params.m; ++h)
        {
            auto& row = rowMasks[h];
            ctx.resize(row.V, params.N);
            ctx.resize(row.W0, params.N);
            ctx.resize(row.U, params.N);
            ctx.resize(row.W1, params.N);

            for (u64 j = 0; j < params.N; ++j)
            {
                ctx.mul(deltaS[j], Delta[h], base.s[j]);
                ctx.mul(deltaE[j], Delta[h], base.e[j]);

                ctx.fromBlock(row.V[j], prng.get());
                ctx.fromBlock(row.U[j], prng.get());
                ctx.minus(row.W0[j], deltaS[j], row.V[j]);
                ctx.minus(row.W1[j], deltaE[j], row.U[j]);
            }
        }
    }

    template<typename F, typename Ctx>
    void moduleMvoleGenDirect(
        ModuleMVOLEGenState<F, Ctx>& gen,
        const ModuleMVOLEParams& params,
        PRNG& prng,
        Ctx ctx = {})
    {
        gen.base = moduleMvoleGenBase<F, Ctx>(params, prng, ctx);
        moduleMvoleGenDelta<F, Ctx>(gen.Delta, params, prng, ctx);
        moduleMvoleGenRowMasks<F, Ctx>(gen.rowMasks, params, gen.base, gen.Delta, prng, ctx);
    }

    template<typename F, typename Ctx>
    void moduleMvoleExpandP0(
        ModuleMVOLEP0Output<F, Ctx>& p0,
        const ModuleMVOLEParams& params,
        const ModuleMVOLEGenState<F, Ctx>& gen,
        Ctx ctx = {})
    {
        using VecF = typename Ctx::template Vec<F>;
        VecF row0, rowMask;

        ctx.resize(p0.x, params.N);
        ctx.copy(gen.base.b.begin(), gen.base.b.end(), p0.x.begin());
        wht<F, Ctx>(p0.x, static_cast<int>(params.N));

        p0.Z0.resize(params.m, params.N);
        ctx.resize(row0, params.N);
        ctx.resize(rowMask, params.N);

        for (u64 h = 0; h < params.m; ++h)
        {
            ctx.copy(gen.rowMasks[h].V.begin(), gen.rowMasks[h].V.end(), row0.begin());
            ctx.copy(gen.rowMasks[h].U.begin(), gen.rowMasks[h].U.end(), rowMask.begin());
            wht<F, Ctx>(row0, static_cast<int>(params.N));
            wht<F, Ctx>(rowMask, static_cast<int>(params.N));
            for (u64 j = 0; j < params.N; ++j)
            {
                F prod;
                ctx.mul(prod, gen.base.rhoWht[j], row0[j]);
                ctx.plus(row0[j], prod, rowMask[j]);
            }

            for (u64 j = 0; j < params.N; ++j)
                p0.Z0(h, j) = row0[j];
        }
    }

    template<typename F, typename Ctx>
    void moduleMvoleExpandP1(
        ModuleMVOLEP1Output<F, Ctx>& p1,
        const ModuleMVOLEParams& params,
        const ModuleMVOLEGenState<F, Ctx>& gen,
        Ctx ctx = {})
    {
        using VecF = typename Ctx::template Vec<F>;
        VecF row1, rowMask;

        ctx.resize(p1.Delta, params.m);
        ctx.copy(gen.Delta.begin(), gen.Delta.end(), p1.Delta.begin());
        p1.Z1.resize(params.m, params.N);
        ctx.resize(row1, params.N);
        ctx.resize(rowMask, params.N);

        for (u64 h = 0; h < params.m; ++h)
        {
            ctx.copy(gen.rowMasks[h].W0.begin(), gen.rowMasks[h].W0.end(), row1.begin());
            ctx.copy(gen.rowMasks[h].W1.begin(), gen.rowMasks[h].W1.end(), rowMask.begin());
            wht<F, Ctx>(row1, static_cast<int>(params.N));
            wht<F, Ctx>(rowMask, static_cast<int>(params.N));
            for (u64 j = 0; j < params.N; ++j)
            {
                F prod;
                ctx.mul(prod, gen.base.rhoWht[j], row1[j]);
                ctx.plus(row1[j], prod, rowMask[j]);
            }

            for (u64 j = 0; j < params.N; ++j)
                p1.Z1(h, j) = row1[j];
        }
    }

    template<typename F, typename Ctx>
    ModuleMVOLEResult<F, Ctx> moduleMvoleGenerateDirect(
        const ModuleMVOLEParams& params,
        PRNG& prng,
        Ctx ctx = {})
    {
        ModuleMVOLEGenState<F, Ctx> gen;
        ModuleMVOLEResult<F, Ctx> result;

        moduleMvoleGenDirect<F, Ctx>(gen, params, prng, ctx);
        moduleMvoleExpandP0<F, Ctx>(result.p0, params, gen, ctx);
        moduleMvoleExpandP1<F, Ctx>(result.p1, params, gen, ctx);

        return result;
    }

    template<typename F, typename Ctx>
    bool moduleMvoleVerify(
        const ModuleMVOLEResult<F, Ctx>& result,
        const ModuleMVOLEParams& params,
        Ctx ctx = {},
        std::ostream* err = nullptr)
    {
        // Verifies the coordinate image of Z0 + Z1 = Delta * x:
        //   Psi(Z0 + Z1)[h,j] == psi(Delta)[h] * phi(x)[j].
        for (u64 h = 0; h < params.m; ++h)
        {
            for (u64 j = 0; j < params.N; ++j)
            {
                F lhs;
                F rhs;
                ctx.plus(lhs, result.p0.Z0(h, j), result.p1.Z1(h, j));
                ctx.mul(rhs, result.p1.Delta[h], result.p0.x[j]);
                if (!ctx.eq(lhs, rhs))
                {
                    if (err)
                    {
                        *err << "MODULE_MVOLE FAIL h=" << h
                             << " j=" << j
                             << " lhs=" << ctx.str(lhs)
                             << " rhs=" << ctx.str(rhs)
                             << std::endl;
                    }
                    return false;
                }
            }
        }
        return true;
    }

    template<typename F, typename Ctx>
    bool runModuleMvoleCorrectness(const ModuleMVOLEParams& params, const std::string& label)
    {
        Ctx ctx;
        PRNG prng(sysRandomSeed());
        auto result = moduleMvoleGenerateDirect<F, Ctx>(params, prng, ctx);
        auto ok = moduleMvoleVerify<F, Ctx>(result, params, ctx, &std::cout);

        std::cout << "MODULE_MVOLE " << label
                  << " N=" << params.N
                  << " t=" << params.t
                  << " m=" << params.m
                  << " " << (ok ? "PASS" : "FAIL")
                  << std::endl;
        return ok;
    }

    struct ModuleMVOLEBenchResult
    {
        double genBaseSeconds = 0;
        double directMaskSeconds = 0;
        double genSeconds = 0;
        double expandP0Seconds = 0;
        double expandP1Seconds = 0;
        double xWhtSeconds = 0;
        double z0RowWhtSeconds = 0;
        double z1RowWhtSeconds = 0;
        double verifySeconds = 0;
        double microWhtOneSeconds = 0;
        double microWhtMSeconds = 0;
        bool ok = false;
    };

    inline double moduleMvoleTotalNoVerifySeconds(const ModuleMVOLEBenchResult& result)
    {
        return result.genSeconds
            + result.expandP0Seconds
            + result.expandP1Seconds;
    }

    inline double moduleMvoleTotalSeconds(const ModuleMVOLEBenchResult& result)
    {
        return moduleMvoleTotalNoVerifySeconds(result)
            + result.verifySeconds;
    }

    template<typename F, typename Ctx>
    void moduleMvoleExpandP0Timed(
        ModuleMVOLEP0Output<F, Ctx>& p0,
        const ModuleMVOLEParams& params,
        const ModuleMVOLEGenState<F, Ctx>& gen,
        ModuleMVOLEBenchResult& timings,
        Ctx ctx = {})
    {
        using VecF = typename Ctx::template Vec<F>;
        VecF row0, rowMask;

        ctx.resize(p0.x, params.N);
        ctx.copy(gen.base.b.begin(), gen.base.b.end(), p0.x.begin());

        auto start = omp_get_wtime();
        wht<F, Ctx>(p0.x, static_cast<int>(params.N));
        auto end = omp_get_wtime();
        timings.xWhtSeconds += end - start;

        p0.Z0.resize(params.m, params.N);
        ctx.resize(row0, params.N);
        ctx.resize(rowMask, params.N);

        for (u64 h = 0; h < params.m; ++h)
        {
            ctx.copy(gen.rowMasks[h].V.begin(), gen.rowMasks[h].V.end(), row0.begin());
            ctx.copy(gen.rowMasks[h].U.begin(), gen.rowMasks[h].U.end(), rowMask.begin());

            start = omp_get_wtime();
            wht<F, Ctx>(row0, static_cast<int>(params.N));
            wht<F, Ctx>(rowMask, static_cast<int>(params.N));
            end = omp_get_wtime();
            timings.z0RowWhtSeconds += end - start;

            for (u64 j = 0; j < params.N; ++j)
            {
                F prod;
                ctx.mul(prod, gen.base.rhoWht[j], row0[j]);
                ctx.plus(row0[j], prod, rowMask[j]);
            }

            for (u64 j = 0; j < params.N; ++j)
                p0.Z0(h, j) = row0[j];
        }
    }

    template<typename F, typename Ctx>
    void moduleMvoleExpandP1Timed(
        ModuleMVOLEP1Output<F, Ctx>& p1,
        const ModuleMVOLEParams& params,
        const ModuleMVOLEGenState<F, Ctx>& gen,
        ModuleMVOLEBenchResult& timings,
        Ctx ctx = {})
    {
        using VecF = typename Ctx::template Vec<F>;
        VecF row1, rowMask;

        ctx.resize(p1.Delta, params.m);
        ctx.copy(gen.Delta.begin(), gen.Delta.end(), p1.Delta.begin());
        p1.Z1.resize(params.m, params.N);
        ctx.resize(row1, params.N);
        ctx.resize(rowMask, params.N);

        for (u64 h = 0; h < params.m; ++h)
        {
            ctx.copy(gen.rowMasks[h].W0.begin(), gen.rowMasks[h].W0.end(), row1.begin());
            ctx.copy(gen.rowMasks[h].W1.begin(), gen.rowMasks[h].W1.end(), rowMask.begin());

            auto start = omp_get_wtime();
            wht<F, Ctx>(row1, static_cast<int>(params.N));
            wht<F, Ctx>(rowMask, static_cast<int>(params.N));
            auto end = omp_get_wtime();
            timings.z1RowWhtSeconds += end - start;

            for (u64 j = 0; j < params.N; ++j)
            {
                F prod;
                ctx.mul(prod, gen.base.rhoWht[j], row1[j]);
                ctx.plus(row1[j], prod, rowMask[j]);
            }

            for (u64 j = 0; j < params.N; ++j)
                p1.Z1(h, j) = row1[j];
        }
    }

    template<typename F, typename Ctx>
    double moduleMvoleMicrobenchWhtOne(
        const ModuleMVOLEParams& params,
        PRNG& prng,
        Ctx ctx = {})
    {
        typename Ctx::template Vec<F> vec;
        ctx.resize(vec, params.N);
        for (u64 j = 0; j < params.N; ++j)
            ctx.fromBlock(vec[j], prng.get());

        auto start = omp_get_wtime();
        wht<F, Ctx>(vec, static_cast<int>(params.N));
        auto end = omp_get_wtime();
        return end - start;
    }

    template<typename F, typename Ctx>
    double moduleMvoleMicrobenchWhtM(
        const ModuleMVOLEParams& params,
        PRNG& prng,
        Ctx ctx = {})
    {
        typename Ctx::template Vec<F> vec;
        ctx.resize(vec, params.N);

        auto start = omp_get_wtime();
        for (u64 h = 0; h < params.m; ++h)
        {
            for (u64 j = 0; j < params.N; ++j)
                ctx.fromBlock(vec[j], prng.get());
            wht<F, Ctx>(vec, static_cast<int>(params.N));
        }
        auto end = omp_get_wtime();
        return end - start;
    }

    template<typename F, typename Ctx>
    ModuleMVOLEBenchResult moduleMvoleBenchmarkOnce(
        const ModuleMVOLEParams& params,
        Ctx ctx = {})
    {
        PRNG prng(sysRandomSeed());
        ModuleMVOLEGenState<F, Ctx> gen;
        ModuleMVOLEResult<F, Ctx> result;
        ModuleMVOLEBenchResult timings;

        auto start = omp_get_wtime();
        gen.base = moduleMvoleGenBase<F, Ctx>(params, prng, ctx);
        auto end = omp_get_wtime();
        timings.genBaseSeconds = end - start;

        start = omp_get_wtime();
        moduleMvoleGenDelta<F, Ctx>(gen.Delta, params, prng, ctx);
        moduleMvoleGenRowMasks<F, Ctx>(gen.rowMasks, params, gen.base, gen.Delta, prng, ctx);
        end = omp_get_wtime();
        timings.directMaskSeconds = end - start;
        timings.genSeconds = timings.genBaseSeconds + timings.directMaskSeconds;

        timings.microWhtOneSeconds = moduleMvoleMicrobenchWhtOne<F, Ctx>(params, prng, ctx);
        timings.microWhtMSeconds = moduleMvoleMicrobenchWhtM<F, Ctx>(params, prng, ctx);

        start = omp_get_wtime();
        moduleMvoleExpandP0Timed<F, Ctx>(result.p0, params, gen, timings, ctx);
        end = omp_get_wtime();
        timings.expandP0Seconds = end - start;

        start = omp_get_wtime();
        moduleMvoleExpandP1Timed<F, Ctx>(result.p1, params, gen, timings, ctx);
        end = omp_get_wtime();
        timings.expandP1Seconds = end - start;

        start = omp_get_wtime();
        timings.ok = moduleMvoleVerify<F, Ctx>(result, params, ctx, &std::cout);
        end = omp_get_wtime();
        timings.verifySeconds = end - start;

        return timings;
    }

    template<typename F, typename Ctx>
    bool runModuleMvoleBenchmark(const ModuleMVOLEParams& params, const std::string& label)
    {
        auto timings = moduleMvoleBenchmarkOnce<F, Ctx>(params);
        auto totalWithVerify = moduleMvoleTotalSeconds(timings);
        auto totalNoVerify = moduleMvoleTotalNoVerifySeconds(timings);
        auto outputElems = params.m * params.N;
        auto throughputWithVerify = totalWithVerify > 0 ? static_cast<double>(outputElems) / totalWithVerify : 0.0;
        auto throughputNoVerify = totalNoVerify > 0 ? static_cast<double>(outputElems) / totalNoVerify : 0.0;

        std::cout << std::fixed << std::setprecision(6)
                  << "MODULE_MVOLE_BENCH " << label
                  << " N=" << params.N
                  << " t=" << params.t
                  << " m=" << params.m
                  << " output_elems=" << outputElems
                  << " gen_base_s=" << timings.genBaseSeconds
                  << " direct_masks_s=" << timings.directMaskSeconds
                  << " gen_s=" << timings.genSeconds
                  << " expand_p0_s=" << timings.expandP0Seconds
                  << " expand_p1_s=" << timings.expandP1Seconds
                  << " x_wht_s=" << timings.xWhtSeconds
                  << " z0_wht_m_s=" << timings.z0RowWhtSeconds
                  << " z1_wht_m_s=" << timings.z1RowWhtSeconds
                  << " verify_s=" << timings.verifySeconds
                  << " total_no_verify_s=" << totalNoVerify
                  << " total_with_verify_s=" << totalWithVerify
                  << " throughput_no_verify_elems_per_s=" << throughputNoVerify
                  << " throughput_with_verify_elems_per_s=" << throughputWithVerify
                  << " micro_wht_1_s=" << timings.microWhtOneSeconds
                  << " micro_wht_m_s=" << timings.microWhtMSeconds
                  << " " << (timings.ok ? "PASS" : "FAIL")
                  << std::endl;

        return timings.ok;
    }

    inline ModuleMVOLEParams moduleMvolePreset(u64 presetOrNumVar)
    {
        ModuleMVOLEParams params;

        if (presetOrNumVar == 1)
        {
            params.n = 8;
            params.t = 4;
            params.m = 4;
        }
        else if (presetOrNumVar == 2)
        {
            params.n = 10;
            params.t = 8;
            params.m = 8;
        }
        else if (presetOrNumVar == 3)
        {
            params.n = 11;
            params.t = 16;
            params.m = 8;
        }
        else
        {
            params.n = presetOrNumVar;
            params.t = params.n <= 8 ? 4 : 8;
            params.m = params.n <= 8 ? 4 : 8;
        }

        params.N = 1ull << params.n;
        return params;
    }

    inline ModuleMVOLEParams moduleMvoleBenchPreset(u64 preset)
    {
        ModuleMVOLEParams params;

        if (preset == 1)
        {
            params.n = 10;
            params.t = 8;
            params.m = 8;
        }
        else if (preset == 2)
        {
            params.n = 12;
            params.t = 16;
            params.m = 16;
        }
        else if (preset == 3)
        {
            params.n = 14;
            params.t = 32;
            params.m = 16;
        }
        else if (preset == 4)
        {
            params.n = 16;
            params.t = 64;
            params.m = 32;
        }
        else if (preset == 5)
        {
            params.n = 14;
            params.t = 32;
            params.m = 8;
        }
        else if (preset == 6)
        {
            params.n = 14;
            params.t = 32;
            params.m = 32;
        }
        else if (preset == 7)
        {
            params.n = 14;
            params.t = 32;
            params.m = 64;
        }
        else if (preset == 8)
        {
            params.n = 16;
            params.t = 64;
            params.m = 8;
        }
        else if (preset == 9)
        {
            params.n = 16;
            params.t = 64;
            params.m = 16;
        }
        else if (preset == 10)
        {
            params.n = 18;
            params.t = 64;
            params.m = 8;
        }
        else if (preset == 11)
        {
            params.n = 18;
            params.t = 64;
            params.m = 16;
        }
        else
        {
            params.n = preset;
            params.t = params.n <= 10 ? 8 : 16;
            params.m = params.n <= 10 ? 8 : 16;
        }

        params.N = 1ull << params.n;
        return params;
    }

    inline int ModuleMVOLE_Test(u64 presetOrNumVar)
    {
        auto params = moduleMvolePreset(presetOrNumVar);
        auto ok64 = runModuleMvoleCorrectness<u64, CoeffCtxIntegerPrime_64>(params, "Fp64");
        auto ok32 = runModuleMvoleCorrectness<u32, CoeffCtxIntegerPrime_32>(params, "Fp32");
        return (ok64 && ok32) ? 0 : 1;
    }

    inline int ModuleMVOLE_Bench(u64 preset)
    {
        auto params = moduleMvoleBenchPreset(preset);
        auto ok64 = runModuleMvoleBenchmark<u64, CoeffCtxIntegerPrime_64>(params, "Fp64");
        return ok64 ? 0 : 1;
    }
}
