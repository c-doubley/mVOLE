#include "SilentWalshVoleSender.h"
#include "SilentWalshVoleReceiver.h"
#include "Walsh.h"
//#include "Vole_Tests.h"
#include "libOTe/Vole/Noisy/NoisyVoleSender.h"
#include "libOTe/Vole/Noisy/NoisyVoleReceiver.h"
#include "libOTe/Vole/Silent/SilentVoleSender.h"
#include "libOTe/Vole/Silent/SilentVoleReceiver.h"
#include "cryptoTools/Network/Session.h"
#include "cryptoTools/Network/IOService.h"
#include "cryptoTools/Common/BitVector.h"
#include "cryptoTools/Common/Timer.h"
#include "cryptoTools/Common/Range.h"
#include "cryptoTools/Common/TestCollection.h"
//#include "Common.h"
#include "coproto/Socket/BufferingSocket.h"
#include <omp.h>
#include "libOTe/Tools/EACode/EACode.h"
#include <iomanip>
#include "libOTe_Tests/ExConvCode_Tests.h"
#include "libOTe/Tools/ExConvCode/ExConvChecker.h"
#include "Coeff128.h"
using namespace oc;

#include <libOTe/config.h>

#include <ctime>
using namespace osuCrypto;


template<typename F,typename Ctx>
double VOLE_QASD(u64 n)
{
    using VecF = typename Ctx::template Vec<F>;
    Ctx ctx;
   //block seed = CCBlock;
    PRNG prng(sysRandomSeed());
    SilentWalshVoleSender<F,F,Ctx> send;
    SilentWalshVoleReceiver<F,F,Ctx> recv;
    auto chls = cp::LocalAsyncSocket::makePair();
    VecF s(n), e(n);
    VecF s1(n), e1(n), s2(n), e2(n);
    VecF a(n);
    VecF addshare1(n), addshare2(n);
    VecF Vol(n);
    F d;
    ctx.fromBlock(d, prng.get());
    for(u64 i=0;i<n;i++)
    {
        ctx.fromBlock(a[i],prng.get());
    }
    wht<F, Ctx>(a,n);
    std::thread t0([&]() 
  {
      cp::sync_wait(recv.silentReceive(s, s1, prng, chls[0]));
      cp::sync_wait(recv.silentReceive(e, e1, prng, chls[0]));
      for(u64 i=0;i<n;i++)
      {
         ctx.mul(Vol[i], a[i], s[i]);
         ctx.plus(Vol[i], Vol[i], e[i]);
         ctx.mul(addshare1[i], a[i], s1[i]);
         ctx.plus(addshare1[i], addshare1[i], e1[i]);
      }
  });
  std::thread t1([&]() 
  {
      cp::sync_wait(send.silentSend(d, s2, prng, chls[1]));
      cp::sync_wait(send.silentSend(d, e2, prng, chls[1]));
      for(u64 i=0;i<n;i++)
      {
      ctx.mul(addshare2[i], a[i], s2[i]);
      ctx.plus(addshare2[i], addshare2[i], e2[i]);
      }
  });
    double start=omp_get_wtime();
    t0.join();
    t1.join();
    double end=omp_get_wtime();
    double total_time=double(end - start);
    std::cout<<"The total time of QA-SD consume "<<double(end - start)<<" seconds"<<std::endl;
    //verify the relation that d*Vol+addshare2=addshare1
    for (u64 i = 0; i < n; ++i)
    {
        F exp;
        ctx.mul(exp, d, Vol[i]);
        ctx.plus(exp, exp, addshare2[i]);

        if (!ctx.eq(addshare1[i] ,exp))
        {
            throw RTE_LOC;
        }
    }
    return total_time;
        
    
}


template<typename F, typename G, typename Ctx>
double Vole_LPN(u64 n, MultType type, bool debug, bool doFakeBase, bool mal)
{
    using VecF = typename Ctx::template Vec<F>;
    using VecG = typename Ctx::template Vec<G>;
    Ctx ctx;

   // block seed = CCBlock;
    PRNG prng(sysRandomSeed());

    auto chls = cp::LocalAsyncSocket::makePair();

    SilentVoleReceiver<F, G, Ctx> recv;
    SilentVoleSender<F, G, Ctx> send;
    recv.mMultType = type;
    send.mMultType = type;
    recv.mDebug = debug;
    send.mDebug = debug;
    if (mal)
    {
        recv.mMalType = SilentSecType::Malicious;
        send.mMalType = SilentSecType::Malicious;
    }

    VecF a(n), b(n);
    VecG c(n);
    F d = prng.get();

 std::thread t0([&]() 
  {
      cp::sync_wait( recv.silentReceive(c, a, prng, chls[0]));
  });
     
  std::thread t1([&]() 
  {
      cp::sync_wait( send.silentSend(d, b, prng, chls[1]));
  });
    double start=omp_get_wtime();
    t0.join();
    t1.join();
    double end=omp_get_wtime();
    double total_time=double(end - start);
    std::cout<<"The total time of LPN consume "<<double(end - start)<<" seconds"<<std::endl;
    for (u64 i = 0; i < n; ++i)
    {
        // a = b + c * d
        F exp;
        ctx.mul(exp, d, c[i]);
        ctx.plus(exp, exp, b[i]);

        if (!ctx.eq(a[i],exp))
        {
            throw RTE_LOC;
        }
    }
    return total_time;
}


template<typename F, typename Ctx>
double EACode_encode_test(u64 k, u64 R, u64 bw)
    {

        u64 n = k * R;
        EACode code;
        code.config(k, n, bw);

        //auto A = code.getA();
        //auto B = code.getB();
        //auto G = B * A;
        std::vector<F> m0(k), m1(k), c(n), c0(n), c1(n), a1(n);
        std::vector<u8> c2(n), m2(k);

        //if (v)
        //{
        //    std::cout << "B\n" << B << std::endl << std::endl;
        //    std::cout << "A'\n" << code.getAPar() << std::endl << std::endl;
        //    std::cout << "A\n" << A << std::endl << std::endl;
        //    std::cout << "G\n" << G << std::endl;

        //}


        PRNG prng(sysRandomSeed());
        prng.get(c0.data(), c0.size());

        auto a0 = c0;
         Ctx ctx;
        code.accumulate<F, Ctx>(a0, {});
        auto sum = c0[0];

        u64 i = 0;
        detail::ExpanderModd expanderCoeff(code.mSeed, code.mCodeSize);
        auto main = k / 8 * 8;
        for (; i < main; i += 8)
        {
            for (u64 j = 0; j < code.mExpanderWeight; ++j)
            {
                for (u64 p = 0; p < 8; ++p)
                {
                    auto idx = expanderCoeff.get();
                    ctx.plus(m0[i+p], m0[i + p], a0[idx]);
                }
            }
        }

        for (; i < k; ++i)
        {
            for (u64 j = 0; j < code.mExpanderWeight; ++j)
            {
                auto idx = expanderCoeff.get();
                ctx.plus(m0[i], m0[i] , a0[idx]);
            }
        }


        double start=omp_get_wtime();
        code.dualEncode<F, Ctx>(c0, m1, {});
        double end=omp_get_wtime();
    double total_time=double(end - start);
    std::cout<<"The EA encoding consumes "<<double(end - start)<<" seconds"<<std::endl;
    return total_time;
    }

template<typename F, typename CoeffCtx>
double QA_encode_test(int n)
{
    using VecF = typename CoeffCtx::template Vec<F>;
    VecF coeff(n),a(n);
    CoeffCtx mctx;
    PRNG prng(sysRandomSeed());
    for(u64 i=0;i<n;i++)
    {
        mctx.fromBlock(a[i],prng.get());
        mctx.fromBlock(coeff[i],prng.get());
    }
    double start=omp_get_wtime();
    wht<F, CoeffCtx>(coeff,n);
    wht<F, CoeffCtx>(a,n);
    for(u64 i=0;i<n;i++)
    {
        mctx.mul(a[i], coeff[i], a[i]);
    }
    double end=omp_get_wtime();
    double total_time=double(end - start);
    std::cout<<"The QA encoding consumes "<<double(end - start)<<" seconds"<<std::endl;
    return total_time;
}




    template<typename F, typename CoeffCtx>
    double EC_encode_test(u64 k, u64 r, u64 bw, u64 aw, bool sys)
    {
        u64 n=r*k;
        ExConvCode code;
        code.config(k, n, bw, aw, sys);

        auto accOffset = sys*k;
        std::vector<F> x1(n), x2(n), x3(n), x4(n);
        PRNG prng(CCBlock);

        for (u64 i = 0; i < x1.size(); ++i)
        {
            x1[i] = x2[i] = x3[i] = prng.get();
        }
        CoeffCtx ctx;
        std::vector<u8> rand(divCeil(aw, 8));
        for (u64 i = 0; i < n; ++i)
        {
            prng.get(rand.data(), rand.size());
            code.accOneGen<F, CoeffCtx, true>(x1.data(), i, n, rand.data(), ctx);

            if (aw == 24)
                code.accOne<F, CoeffCtx, true, 24>(x2.data(), i, n, rand.data(), ctx);

            u64 j = i + 1;

            assert(aw <= 64);
            u64 bits = 0;
            memcpy(&bits, rand.data(), std::min<u64>(rand.size(), 8));
            for (u64 a = 0; a < aw; ++a, ++j)
            {
                if (bits & 1)
                {
                    ctx.plus(x3[j % n], x3[j % n], x3[i]);
                }
                bits >>= 1;
            }
            ctx.plus(x3[j % n], x3[j % n], x3[i]);
            ctx.mulConst(x3[j % n], x3[j % n]);

            j = i + 1;
            for (u64 a = 0; a <= aw; ++a, ++j)
            {
                //auto j = (i + a + 2) % n;

                if (aw == 24 && x1[j%n] != x2[j % n])
                {
                    std::cout << j % n << " " << ctx.str(x1[j % n]) << " " << ctx.str(x2[j % n]) << std::endl;
                    throw RTE_LOC;
                }

                if (x1[j % n] != x3[j % n])
                {
                    std::cout << j % n << " " << ctx.str(x1[j % n]) << " " << ctx.str(x3[j % n]) << std::endl;
                    throw RTE_LOC;
                }
            }
        }

        x4 = x1;
        u64 size = n - accOffset;

        code.accumulateFixed<F, CoeffCtx, 0>(x1.data() + accOffset, size, ctx, code.mSeed);
        if (code.mAccTwice)
            code.accumulateFixed<F, CoeffCtx, 0>(x1.data() + accOffset, size, ctx, ~code.mSeed);
        if (aw == 24)
        {
            code.accumulateFixed<F, CoeffCtx, 24>(x2.data() + accOffset, size, ctx, code.mSeed);

            if (code.mAccTwice)
                code.accumulateFixed<F, CoeffCtx, 24>(x2.data() + accOffset, size, ctx, ~code.mSeed);

            if (x1 != x2)
            {
                for (u64 i = 0; i < x1.size(); ++i)
                {
                    std::cout << i << " " << ctx.str(x1[i]) << " " << ctx.str(x2[i]) << std::endl;
                }
                throw RTE_LOC;
            }
        }

        {
            for (auto r = 0; r < 1 + code.mAccTwice; ++r)
            {
                PRNG coeffGen(r ? ~code.mSeed : code.mSeed);
                u8* mtxCoeffIter = (u8*)coeffGen.mBuffer.data();
                auto mtxCoeffEnd = mtxCoeffIter + coeffGen.mBuffer.size() * sizeof(block) - divCeil(aw, 8);

                auto x = x3.data() + accOffset;
                u64 i = 0;
                while (i < size)
                {
                    auto xi = x + i;

                    if (mtxCoeffIter > mtxCoeffEnd)
                    {
                        // generate more mtx coefficients
                        ExConvCode::refill(coeffGen);
                        mtxCoeffIter = (u8*)coeffGen.mBuffer.data();
                    }

                    // add xi to the next positions
                    auto j = (i + 1) % size;

                    u64 bits = 0;
                    memcpy(&bits, mtxCoeffIter, divCeil(aw, 8));
                    for (u64 a = 0; a < aw; ++a)
                    {

                        if (bits & 1)
                        {
                            auto xj = x + j;
                            ctx.plus(*xj, *xj, *xi);
                        }
                        bits >>= 1;
                        j = (j + 1) % size;
                    }

                    {
                        auto xj = x + j;
                        ctx.plus(*xj, *xj, *xi);
                        ctx.mulConst(*xj, *xj);
                    }
 
                    ++mtxCoeffIter;

                    ++i;
                }
            }
        }

        if (x1 != x3)
        {
            for (u64 i = 0; i < x1.size(); ++i)
            {
                std::cout << i << " " << ctx.str(x1[i]) << " " << ctx.str(x3[i]) << std::endl;
            }
            throw RTE_LOC;
        }


        std::vector<F> y1(k), y2(k);

        if (sys)
        {
            std::copy(x1.data(), x1.data() + k, y1.data());
            y2 = y1;
            code.mExpander.expand<F, CoeffCtx, true>(x1.data() + accOffset, y1.data());
            //using P = std::pair<typename std::vector<F>::const_iterator, typename std::vector<F>::iterator>;
            //auto p = P{ x1.cbegin() + accOffset, y1.begin() };
            //code.mExpander.expandMany<true, CoeffCtx, F>(
            //    std::tuple<P>{ p }
            //);
        }
        else
        {
            code.mExpander.expand<F, CoeffCtx, false>(x1.data() + accOffset, y1.data());
        }

        u64 step, exSize, regCount = 0;;
        if (code.mExpander.mRegular)
        {
            regCount = divCeil(code.mExpander.mExpanderWeight, 2);
            exSize = step = code.mExpander.mCodeSize / regCount;
        }
        else
        {
            step = 0;
            exSize = n;
        }
        detail::ExpanderModd regExp(code.mExpander.mSeed^ block(342342134, 23421341), exSize);
        detail::ExpanderModd fullExp(code.mExpander.mSeed, code.mExpander.mCodeSize);

        u64 i = 0;
        auto main = k / 8 * 8;
        for (; i < main; i += 8)
        {
            
            for (u64 j = 0; j < regCount; ++j)
            {
                for (u64 p = 0; p < 8; ++p)
                {
                    auto idx = regExp.get() + step * j;
                    ctx.plus(y2[i + p], y2[i + p], x1[idx + accOffset]);
                }
            }
            for (u64 j = 0; j < code.mExpander.mExpanderWeight - regCount; ++j)
            {
                for (u64 p = 0; p < 8; ++p)
                {
                    auto idx = fullExp.get();
                    ctx.plus(y2[i + p], y2[i + p], x1[idx + accOffset]);
                }
            }
        }

        for (; i < k; ++i)
        {
            for (u64 j = 0; j < regCount; ++j)
            {
                auto idx = regExp.get() + step * j;
                ctx.plus(y2[i], y2[i], x1[idx + accOffset]);
            }
            for (u64 j = 0; j < code.mExpander.mExpanderWeight - regCount; ++j)
            {
                auto idx = fullExp.get();
                ctx.plus(y2[i], y2[i], x1[idx + accOffset]);
            }
        }

        if (y1 != y2)
            throw RTE_LOC;

    double start=omp_get_wtime();
    code.dualEncode<F, CoeffCtx>(x4.begin(), {});
    double end=omp_get_wtime();
    double total_time=double(end - start);
    std::cout<<"The EC encoding consumes "<<double(end - start)<<" seconds"<<std::endl;
    return total_time;
        x4.resize(k);
        if (x4 != y1)
            throw RTE_LOC;
}