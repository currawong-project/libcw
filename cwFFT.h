#ifndef cwFFT_H
#define cwFFT_H

#define FFTW_MEASURE (1)
#define FFTW_BACKWARD (2)
/*
typedef struct fftw_plan_str
{
  int foo;
} fftw_plan;
*/
typedef int fftw_plan;
typedef std::complex<double> fftw_complex;
  
void*      fftw_malloc(unsigned );
void       fftw_free(void*);
fftw_plan  fftw_plan_dft_r2c_1d(int bufN, double* buf, fftw_complex* cplxV, int flags );
fftw_plan  fftw_plan_dft_c2r_1d(int bufN, fftw_complex* cplxV, double* outV, int flags );          
void       fftw_destroy_plan(fftw_plan);
void       fftw_execute( fftw_plan );

/*
typedef struct fftwf_plan_str
{
  int foo;
} fftwf_plan;
*/
typedef int fftwf_plan;

typedef std::complex<float>  fftwf_complex;

void*       fftwf_malloc(unsigned );
void        fftwf_free(void*);
fftwf_plan  fftwf_plan_dft_r2c_1d(int bufN, float* buf, fftwf_complex* cplxV, int flags );
fftwf_plan  fftwf_plan_dft_c2r_1d(int bufN, fftwf_complex* cplxV, float* outV, int flags );          
void        fftwf_destroy_plan(fftwf_plan);
void        fftwf_execute( fftwf_plan );
  

#endif
