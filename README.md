# Prime Field PCG
The PCG implementation for VOLE and OLE based on Walsh-Hadamard transform over prime field.

## Installing Our libOTe
Our code is developed based on [libOTe](https://github.com/osu-crypto/libOTe). To install our modified libOTe, run the following command
```
cd libOTe
python build.py --all --install --sudo --boost --sodium --openssl -D FETCH_SODIUM=ON -D SODIUM_MONTGOMERY=FALSE
```
If one successfully installed the official libOTe, please follow the same procedure to install our modified libOTe and overwrite the official version.

If installation fails repeatedly, please remove the existing package and reinstall from a fresh download.

### Instructions for Installing Our libOTe on Ubuntu 24.04
First, install the necessary dependencies.
```
sudo apt-get update -y
sudo apt-get install -y unzip g++ cmake libtool libtool-bin build-essential libboost-dev libgmp-dev libssl-dev git
```

Second, install our libOTe.
```
cd libOTe
python3 build.py --all --boost --sodium
```

## Running benchmarks
Benchmarks: Ensure our modified version libOTe is installed before cmake.
```
cmake .
make
./main --QA_Syndrome n Tests syndrome encoding of QA code of length 2^n
./main --EA_Syndrome n Tests syndrome encoding of EA code of length 2^n
./main --EC_Syndrome n Tests syndrome encoding of EC code of length 2^n
./main --QA_VOLE n Tests VOLE boased on QA code of length 2^n
./main --EA_VOLE n Tests VOLE boased on EA code of length 2^n
./main --EC_VOLE n Tests VOLE boased on EC code of length 2^n
./main --OLE n Tests OLE boased on QA code of length 2^n
```

## ⚠️ Important Warning
Please manually delete it if a previous version of libOTe is installed in your computer. Pay attention to the file [KeccakP-1600-times4-SIMD256.o](libOTe/thirdparty/KyberOT/keccak4x/KeccakP-1600-times4-SIMD256.o) for libOTe.
