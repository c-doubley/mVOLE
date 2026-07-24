#include "Coeff128.h"
#include "coproto/Socket/LocalAsyncSock.h"
#include "libOTe/Vole/Silent/SilentVoleReceiver.h"
#include "libOTe/Vole/Silent/SilentVoleSender.h"

using namespace osuCrypto;

int main()
{
    using F = u64;
    using G = u64;
    using Ctx = CoeffCtxIntegerPrime_64;
    using VecF = typename Ctx::template Vec<F>;
    using VecG = typename Ctx::template Vec<G>;

    constexpr u64 n = 4096;
    Ctx ctx;
    PRNG recvPrng(block(1, 2));
    PRNG sendPrng(block(3, 4));
    F delta{};
    ctx.fromBlock(delta, sendPrng.get<block>());

    VecG c;
    VecF a;
    VecF b;
    ctx.resize(c, n);
    ctx.resize(a, n);
    ctx.resize(b, n);
    for (u64 i = 0; i < n; ++i)
        ctx.fromBlock(c[i], recvPrng.get<block>());

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
        F lhs{}, rhs{};
        ctx.minus(lhs, a[i], b[i]);
        ctx.mul(rhs, delta, c[i]);
        if (!ctx.eq(lhs, rhs))
            return 2;
    }
    return 0;
}
