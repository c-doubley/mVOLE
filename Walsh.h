

#pragma once

#include "libOTe/config.h"


#include <string.h>
#include <stdint.h>
#include "cryptoTools/Common/Defines.h"
#include "cryptoTools/Common/MatrixView.h"
#include "libOTe/Triple/Foleage/FoleageUtils.h"
template<typename F,typename Ctx>
void wht(typename Ctx::template Vec<F>& coeffs, int len) {
    Ctx mctx;
    for(int step = 1; 2*step <= len; step <<= 1){
        for(int i = 0; i < len; i += 2*step){
            for(int j = 0; j < step; ++j){
                F u = coeffs[i+j];
                F v = coeffs[i+j+step];
                mctx.plus(coeffs[i+j], u, v);
                mctx.minus(coeffs[i+j+step], u,v);
            }
        }
    }
}


template<typename F,typename Ctx>
void prime_wht(std::vector<F>& coeffs, int len) {
    Ctx mctx;
    for(int step = 1; 2*step <= len; step <<= 1){
        for(int i = 0; i < len; i += 2*step){
            for(int j = 0; j < step; ++j){
                F u = coeffs[i+j];
                F v = coeffs[i+j+step];
                mctx.plus(coeffs[i+j], u, v);
                mctx.minus(coeffs[i+j+step], u,v);
            }
        }
    }
}




