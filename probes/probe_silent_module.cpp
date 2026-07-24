#include "RmvolePprfNetSetupTest.h"
#include "coproto/Socket/LocalAsyncSock.h"
#include "libOTe/Vole/Silent/SilentVoleReceiver.h"
#include "libOTe/Vole/Silent/SilentVoleSender.h"

using namespace osuCrypto;

int main()
{
    constexpr u64 M = 8;
    using F = RmvolePprfPrimeVector<M>;
    using G = u64;
    using Ctx = CoeffCtxPrimeArray64<M>;
    using VecF = typename Ctx::template Vec<F>;
    using VecG = typename Ctx::template Vec<G>;

    constexpr u64 n = 4096;
    Ctx ctx;
    CoeffCtxIntegerPrime_64 scalarCtx;
    PRNG recvPrng(block(5, 6));
    PRNG sendPrng(block(7, 8));
    F delta{};
    for (u64 j = 0; j < M; ++j)
        scalarCtx.fromBlock(delta[j], sendPrng.get<block>());

    VecG c;
    VecF a;
    VecF b;
    ctx.resize(c, n);
    ctx.resize(a, n);
    ctx.resize(b, n);
    for (u64 i = 0; i < n; ++i)
        scalarCtx.fromBlock(c[i], recvPrng.get<block>());

    SilentVoleReceiver<F, G, Ctx> receiver;
    SilentVoleSender<F, G, Ctx> sender;
    receiver.configure(n, SilentBaseType::Base, 128, ctx);
    sender.configure(n, SilentBaseType::Base, 128, ctx);

    auto sockets = cp::LocalAsyncSocket::makePair();
    auto tasks = macoro::sync_wait(macoro::when_all_ready(
        receiver.silentReceive(c, a, recvPrng, sockets[0]),
        sender.silentSend(delta, b, sendPrng, sockets[1])));
    std::get<0>(tasks).result();
    std::get<1>(tasks).result();

    for (u64 i = 0; i < n; ++i)
    {
        F lhs{};
        ctx.minus(lhs, a[i], b[i]);
        for (u64 j = 0; j < M; ++j)
        {
            u64 rhs{};
            scalarCtx.mul(rhs, delta[j], c[i]);
            if (!scalarCtx.eq(lhs[j], rhs))
                return 2;
        }
    }
    return 0;
}
