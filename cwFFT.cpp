#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwFFT.h"

void*      fftw_malloc(unsigned ) { return nullptr; }
void       fftw_free(void*){}
fftw_plan  fftw_plan_dft_r2c_1d(int bufN, double* buf, fftw_complex* cplxV, int type ){ return 0; }
fftw_plan  fftw_plan_dft_c2r_1d(int bufN, fftw_complex* cplxV, double* outV, int flags ) { return 0; }
void       fftw_destroy_plan(fftw_plan) {}
void       fftw_execute( fftw_plan) {}

void*       fftwf_malloc(unsigned ){ return nullptr; }
void        fftwf_free(void*) {}
fftwf_plan  fftwf_plan_dft_r2c_1d(int bufN, float* buf, fftwf_complex* cplxV, int type ) { return 0; }
fftwf_plan  fftwf_plan_dft_c2r_1d(int bufN, fftwf_complex* cplxV, float* outV, int flags ) { return 0; }
void        fftwf_destroy_plan(fftwf_plan){}
void        fftwf_execute( fftwf_plan){}
