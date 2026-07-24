#include "Coeff128.h"
#include "coproto/Socket/LocalAsyncSock.h"
#include "libOTe/Vole/Silent/SilentVoleReceiver.h"
#include "libOTe/Vole/Silent/SilentVoleSender.h"

#include <array>
#include <ostream>

using namespace osuCrypto;

struct ToyExt
{
    std::array<u64, 2> v;
};

static_assert(std::is_trivial_v<ToyExt>);

bool operator==(const ToyExt& lhs, const ToyExt& rhs)
{
    return lhs.v == rhs.v;
}

bool operator!=(const ToyExt& lhs, const ToyExt& rhs)
{
    return !(lhs == rhs);
}

std::ostream& operator<<(std::ostream& os, const ToyExt& value)
{
    return os << "{" << value.v[0] << "," << value.v[1] << "}";
}

struct ToyCtx : CoeffCtxIntegerPrime_64
{
    using Base = CoeffCtxIntegerPrime_64;

    template<typename T>
    using Vec = AlignedUnVector<T>;

    using Base::byteSize;
    using Base::copy;
    using Base::deserialize;
    using Base::isField;
    using Base::mulConst;
    using Base::one;
    using Base::resize;
    using Base::restrictPtr;
    using Base::serialize;
    using Base::zero;

    void plus(ToyExt& ret, const ToyExt& lhs, const ToyExt& rhs)
    {
        plus(ret.v[0], lhs.v[0], rhs.v[0]);
        plus(ret.v[1], lhs.v[1], rhs.v[1]);
    }

    void plus(u64& ret, const u64& lhs, const u64& rhs)
    {
        auto sum = lhs + rhs;
        ret = sum >= Base::PR ? sum - Base::PR : sum;
    }

    void minus(ToyExt& ret, const ToyExt& lhs, const ToyExt& rhs)
    {
        minus(ret.v[0], lhs.v[0], rhs.v[0]);
        minus(ret.v[1], lhs.v[1], rhs.v[1]);
    }

    void minus(u64& ret, const u64& lhs, const u64& rhs)
    {
        ret = lhs >= rhs ? lhs - rhs : lhs + Base::PR - rhs;
    }

    void mul(ToyExt& ret, const ToyExt& lhs, const u64& rhs)
    {
        mul(ret.v[0], lhs.v[0], rhs);
        mul(ret.v[1], lhs.v[1], rhs);
    }

    void mul(u64& ret, const u64& lhs, const u64& rhs)
    {
        ret = static_cast<u64>((static_cast<unsigned __int128>(lhs) * rhs) % Base::PR);
    }

    bool eq(const ToyExt& lhs, const ToyExt& rhs)
    {
        return eq(lhs.v[0], rhs.v[0]) && eq(lhs.v[1], rhs.v[1]);
    }

    bool eq(const u64& lhs, const u64& rhs)
    {
        return lhs == rhs;
    }

    void fromBlock(ToyExt& ret, const block& b)
    {
        std::array<block, 2> buffer;
        mAesFixedKey.ecbEncCounterMode(b, buffer);
        fromBlock(ret.v[0], buffer[0]);
        fromBlock(ret.v[1], buffer[1]);
    }

    void fromBlock(u64& ret, const block& b)
    {
        std::memcpy(&ret, &b, sizeof(ret));
        ret %= Base::PR;
    }

    void powerOfTwo(ToyExt& ret, u64 power)
    {
        ret = ToyExt{{0, 0}};
        auto coord = power / 64;
        if (coord < 2)
            ret.v[coord] = static_cast<u64>((static_cast<unsigned __int128>(1) << (power % 64)) % Base::PR);
    }

    void powerOfTwo(u64& ret, u64 power)
    {
        ret = static_cast<u64>((static_cast<unsigned __int128>(1) << (power % 64)) % Base::PR);
    }
};

int main()
{
    using F = ToyExt;
    using G = u64;
    using Ctx = ToyCtx;
    using VecF = typename Ctx::template Vec<F>;
    using VecG = typename Ctx::template Vec<G>;

    constexpr u64 n = 4096;
    Ctx ctx;
    PRNG recvPrng(block(9, 10));
    PRNG sendPrng(block(11, 12));
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
        F lhs{};
        ctx.minus(lhs, a[i], b[i]);
        for (u64 j = 0; j < 2; ++j)
        {
            u64 rhs{};
            ctx.mul(rhs, delta.v[j], c[i]);
            if (!ctx.eq(lhs.v[j], rhs))
                return 2;
        }
    }
    return 0;
}
