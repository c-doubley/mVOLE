
#include "coproto/Socket/AsioSocket.h"
#include "coproto/Socket/Socket.h"
#include "libOTe/config.h"
#include "Coeff128.h"
#include "libOTe/Triple/Foleage/fft/FoleageFft.h"
#include "Walsh.h"
#include <fstream>
#include <ctime>
#include <omp.h>
#include "libOTe/Tools/CoeffCtx.h"
#include <bitset>
#include "Prime_test.h"
#include "Prime_OLE.h"
#include "ModuleMVOLE.h"
#include "ModulePprfTest.h"
#include "ModuleVectorPprfTest.h"
#include "ModuleMvolePprfIntegration.h"
#include "ModuleMvoleCoordPprfIntegration.h"
#include "SubfieldVoleNetBench.h"
//#define trial 2
//#define num_thread 1
using namespace osuCrypto;

void printUsage() {
    printf("Usage: ./main [OPTIONS] n\n");
    printf("Options:\n");
    printf("  --QA_Syndrome\tTests syndrome encoding of QA code.\n");
    printf("  --EA_Syndrome\tTests syndrome encoding of EA code.\n");
    printf("  --EC_Syndrome\tTests syndrome encoding of EC code.\n");
    printf("  --QA_VOLE\tTests VOLE based on QA code.\n");
    printf("  --EA_VOLE\tTests VOLE based on EA code.\n");
    printf("  --EC_VOLE\tTests VOLE based on EC code..\n");
    printf("  --OLE\tTests OLE based on QA code.\n");
    printf("  --MODULE_MVOLE\tTests module-valued Matrix-VOLE prototype.\n");
    printf("  --MODULE_MVOLE_BENCH\tBenchmarks module-valued Matrix-VOLE prototype.\n");
    printf("  --MODULE_PPRF_TEST\tTests vector-valued ModuleMVOLE PPRF adapter.\n");
    printf("  --MODULE_VECTOR_PPRF_TEST\tTests shared-path vector-valued PPRF prototype.\n");
    printf("  --MODULE_MVOLE_PPRF\tTests RM-VOLE setup using shared-path vector-valued PPRF prototype.\n");
    printf("  --MODULE_MVOLE_COORD_PPRF\tBenchmarks RM-VOLE setup using coordinate-wise scalar PPRF prototype.\n");
    printf("  --SUBFIELD_VOLE_NET_BENCH\tBenchmarks libOTe generic subfield VOLE over loopback coproto sockets.\n");
}

int main(int argc, char **argv)
{
    if (argc <3) {
        printUsage();
    }
    else
    {
        if (strcmp(argv[1], "--SUBFIELD_VOLE_NET_BENCH") ==0) {
            return SubfieldVoleNetBench(argc, argv);
        }

        u64 num_var=std::atoi(argv[2]);
        u64 n=ipow(2, num_var);
        if (strcmp(argv[1], "--QA_Syndrome") == 0) {
            QA_prime_encode_test<u64, CoeffCtxIntegerPrime_64>(n);
            QA_prime_encode_test<u32, CoeffCtxIntegerPrime_32>(n);
        } else if (strcmp(argv[1], "--EA_Syndrome") == 0) {
                EA_prime_encode_test<u64, CoeffCtxIntegerPrime_64>(n, 5, 21);
                EA_prime_encode_test<u32, CoeffCtxIntegerPrime_32>(n, 5, 21);
        } else if (strcmp(argv[1], "--EC_Syndrome")==0) {
                EC_prime_encode_test<u64, CoeffCtxIntegerPrime_64>(n,2,7,24,true);
                EC_prime_encode_test<u32, CoeffCtxIntegerPrime_32>(n,2,7,24,true);
        } else if (strcmp(argv[1], "--QA_VOLE") == 0) {
            VOLE_prime_QASD<u64, CoeffCtxIntegerPrime_64>(n);
            VOLE_prime_QASD<u32, CoeffCtxIntegerPrime_32>(n);
        } else if (strcmp(argv[1], "--EC_VOLE") == 0) {
                Vole_prime_LPN<u64, u64, CoeffCtxIntegerPrime_64>(n, osuCrypto::MultType::ExConv7x24, false, false, false);
                Vole_prime_LPN<u32, u32, CoeffCtxIntegerPrime_32>(n, osuCrypto::MultType::ExConv7x24, false, false, false);
        } else if (strcmp(argv[1], "--EA_VOLE") ==0) {
            Vole_prime_LPN<u64, u64, CoeffCtxIntegerPrime_64>(n, osuCrypto::MultType::ExAcc21, false, false, false);
            Vole_prime_LPN<u32, u32, CoeffCtxIntegerPrime_32>(n, osuCrypto::MultType::ExAcc21, false, false, false);
        } else if (strcmp(argv[1], "--OLE") ==0) {
            Prime_OLE<u64, CoeffCtxIntegerPrime_64>(num_var, 6, 2);
            Prime_OLE<u64, CoeffCtxIntegerPrime_64>(num_var, 5, 3);
        } else if (strcmp(argv[1], "--MODULE_MVOLE") ==0) {
            return ModuleMVOLE_Test(num_var);
        } else if (strcmp(argv[1], "--MODULE_MVOLE_BENCH") ==0) {
            return ModuleMVOLE_Bench(num_var);
        } else if (strcmp(argv[1], "--MODULE_PPRF_TEST") ==0) {
            return ModulePprf_Test(num_var);
        } else if (strcmp(argv[1], "--MODULE_VECTOR_PPRF_TEST") ==0) {
            return ModuleVectorPprf_Test(num_var);
        } else if (strcmp(argv[1], "--MODULE_MVOLE_PPRF") ==0) {
            return ModuleMVOLE_Pprf(num_var);
        } else if (strcmp(argv[1], "--MODULE_MVOLE_COORD_PPRF") ==0) {
            return ModuleMVOLE_CoordPprf(num_var);
        }
        else
        {
            printUsage();
        }
    }
}
