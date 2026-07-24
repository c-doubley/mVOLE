#include "RmvolePprfNetSetupTest.h"
#include "libOTe/Tools/ExConvCode/ExConvCode.h"

#include <iostream>

using namespace osuCrypto;

int main()
{
    constexpr u64 M = 8;
    constexpr u64 requestSize = 32;
    constexpr u64 codeSize = 64;
    using ExtElem = RmvolePprfPrimeVector<M>;
    using ExtCtx = CoeffCtxPrimeArray64<M>;

    ExtCtx vectorCtx;
    CoeffCtxIntegerPrime_64 scalarCtx;
    PRNG prng(block(13, 14));
    ExConvCode encoder;
    encoder.config(requestSize, codeSize, 7, 24);

    typename ExtCtx::template Vec<ExtElem> moduleInput;
    vectorCtx.resize(moduleInput, codeSize);
    for (u64 i = 0; i < codeSize; ++i)
        vectorCtx.fromBlock(moduleInput[i], prng.get<block>());

    std::array<typename ExtCtx::template Vec<u64>, M> scalarInputs;
    for (u64 j = 0; j < M; ++j)
    {
        vectorCtx.resize(scalarInputs[j], codeSize);
        for (u64 i = 0; i < codeSize; ++i)
            scalarInputs[j][i] = moduleInput[i][j];
    }

    encoder.dualEncode<ExtElem, ExtCtx>(moduleInput.begin(), vectorCtx);
    for (u64 j = 0; j < M; ++j)
        encoder.dualEncode<u64, ExtCtx>(scalarInputs[j].begin(), vectorCtx);

    bool ok = true;
    for (u64 i = 0; i < requestSize; ++i)
    {
        for (u64 j = 0; j < M; ++j)
        {
            if (!scalarCtx.eq(moduleInput[i][j], scalarInputs[j][i]))
                ok = false;
        }
    }

    std::cout << "test,M,message_size,code_size,expander_weight,accumulator_weight,ok\n"
              << "ExConv_module_vs_coordinate," << M << ','
              << requestSize << ',' << codeSize << ",7,24,"
              << (ok ? 1 : 0) << "\n";
    return ok ? 0 : 2;
}
