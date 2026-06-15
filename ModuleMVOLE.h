#pragma once

#include "Coeff128.h"
#include "Walsh.h"
#include "cryptoTools/Common/Matrix.h"
#include "cryptoTools/Crypto/PRNG.h"
#include <iostream>
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
        typename Ctx::template Vec<F> x;
        Matrix<F> Z0;
    };

    template<typename F, typename Ctx>
    struct ModuleMVOLEP1Output
    {
        typename Ctx::template Vec<F> Delta;
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

        moduleMvoleSampleSparse<F, Ctx>(state.s, params.N, params.t, prng, ctx);
        moduleMvoleSampleSparse<F, Ctx>(state.e, params.N, params.t, prng, ctx);

        moduleMvoleRingMul<F, Ctx>(state.rho, state.s, rhoS, ctx);
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
        VecF rhoV, row0;

        ctx.resize(p0.x, params.N);
        ctx.copy(gen.base.b.begin(), gen.base.b.end(), p0.x.begin());
        wht<F, Ctx>(p0.x, static_cast<int>(params.N));

        p0.Z0.resize(params.m, params.N);

        for (u64 h = 0; h < params.m; ++h)
        {
            ctx.resize(row0, params.N);
            moduleMvoleRingMul<F, Ctx>(gen.base.rho, gen.rowMasks[h].V, rhoV, ctx);

            for (u64 j = 0; j < params.N; ++j)
                ctx.plus(row0[j], rhoV[j], gen.rowMasks[h].U[j]);

            wht<F, Ctx>(row0, static_cast<int>(params.N));

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
        VecF rhoW0, row1;

        ctx.resize(p1.Delta, params.m);
        ctx.copy(gen.Delta.begin(), gen.Delta.end(), p1.Delta.begin());
        p1.Z1.resize(params.m, params.N);

        for (u64 h = 0; h < params.m; ++h)
        {
            ctx.resize(row1, params.N);
            moduleMvoleRingMul<F, Ctx>(gen.base.rho, gen.rowMasks[h].W0, rhoW0, ctx);

            for (u64 j = 0; j < params.N; ++j)
                ctx.plus(row1[j], rhoW0[j], gen.rowMasks[h].W1[j]);

            wht<F, Ctx>(row1, static_cast<int>(params.N));

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

    inline int ModuleMVOLE_Test(u64 presetOrNumVar)
    {
        auto params = moduleMvolePreset(presetOrNumVar);
        auto ok64 = runModuleMvoleCorrectness<u64, CoeffCtxIntegerPrime_64>(params, "Fp64");
        auto ok32 = runModuleMvoleCorrectness<u32, CoeffCtxIntegerPrime_32>(params, "Fp32");
        return (ok64 && ok32) ? 0 : 1;
    }
}
