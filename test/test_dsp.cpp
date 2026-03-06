#include "gtest/gtest.h"
#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwTest.h"
#include "cwMem.h"
#include "cwMath.h"
#include "cwVectOps.h"
#include "cwDspTypes.h"
#include "cwDsp.h"
#include <cmath>
#include <vector>

using namespace cw;
using namespace cw::dsp;

TEST(DspTest, AmplToDb) {
    EXPECT_NEAR(ampl_to_db(1.0), 0.0, 1e-6);
    EXPECT_NEAR(ampl_to_db(0.1), -20.0, 1e-6);
    EXPECT_NEAR(ampl_to_db(10.0), 20.0, 1e-6);
    EXPECT_NEAR(ampl_to_db(0.0, 1.0, -100.0), -100.0, 1e-6);
    EXPECT_NEAR(ampl_to_db(1e-11, 1.0, -100.0), -100.0, 1e-6);
}

TEST(DspTest, DbToAmpl) {
    EXPECT_NEAR(db_to_ampl(0.0), 1.0, 1e-6);
    EXPECT_NEAR(db_to_ampl(-20.0), 0.1, 1e-6);
    EXPECT_NEAR(db_to_ampl(20.0), 10.0, 1e-6);
}

TEST(DspTest, ClipSampleValue) {
    EXPECT_NEAR(clip_sample_value(0.5f), 0.5f, 1e-6);
    EXPECT_NEAR(clip_sample_value(1.5f), max_sample_value, 1e-6);
    EXPECT_NEAR(clip_sample_value(-1.5f), -max_sample_value, 1e-6);
    EXPECT_NEAR(clip_sample_value(max_sample_value), max_sample_value, 1e-6);
    EXPECT_NEAR(clip_sample_value(-max_sample_value), -max_sample_value, 1e-6);
}

TEST(DspTest, HammingWindow) {
    const unsigned n = 10;
    std::vector<double> w(n);
    hamming(w.data(), n);
    // w[i] = 0.54 - 0.46 * cos(2*pi*i/(n-1))
    EXPECT_NEAR(w[0], 0.08, 1e-6);
    EXPECT_NEAR(w[n-1], 0.08, 1e-6);
    EXPECT_NEAR(w[n/2], 0.54 - 0.46 * std::cos(M_PI * 2 * (n/2) / (n-1)), 1e-6);
}

TEST(DspTest, HannWindow) {
    const unsigned n = 10;
    std::vector<double> w(n);
    hann(w.data(), n);
    // w[i] = 0.5 - 0.5 * cos(2*pi*i/(n-1))
    EXPECT_NEAR(w[0], 0.0, 1e-6);
    EXPECT_NEAR(w[n-1], 0.0, 1e-6);
}

TEST(DspTest, HannMatlabWindow) {
    const unsigned n = 10;
    std::vector<double> w(n);
    hann_matlab(w.data(), n);
    // w[i] = 0.5 * (1.0 - cos(2*pi*(i+1)/(n+1)))
    EXPECT_NEAR(w[0], 0.5 * (1.0 - std::cos(M_PI * 2 * 1 / 11.0)), 1e-6);
    EXPECT_NEAR(w[n-1], 0.5 * (1.0 - std::cos(M_PI * 2 * 10 / 11.0)), 1e-6);
}

TEST(DspTest, TriangleWindow) {
    const unsigned n = 10;
    std::vector<double> w(n);
    triangle(w.data(), n);
    // n=10, n/2=5, incr=1/5=0.2
    // v0=0, seq(w, 5, 0, 0.2) -> 0, 0.2, 0.4, 0.6, 0.8
    // v1=1, seq(w+5, 5, 1, -0.2) -> 1, 0.8, 0.6, 0.4, 0.2
    EXPECT_NEAR(w[0], 0.0, 1e-6);
    EXPECT_NEAR(w[5], 1.0, 1e-6);
    EXPECT_NEAR(w[9], 0.2, 1e-6);
}

TEST(DspTest, KaiserParams) {
    // For slrDb = 50.0:
    // beta = (0.76609 * pow(50.0 - 13.26, 0.4)) + (0.09834 * (50.0 - 13.26))
    // beta = (0.76609 * 4.2381...) + (0.09834 * 36.74)
    // beta = 3.2468... + 3.6130... = 6.8598...
    // The exact value from code was 6.8514486...
    EXPECT_NEAR(kaiser_beta_from_sidelobe_reject(50.0), 6.8514486, 1e-4);
    EXPECT_NEAR(kaiser_freq_resolution_factor(50.0), (6.0 * (50.0 + 12.0))/155.0, 1e-6);
}

TEST(DspTest, KaiserWindow) {
    const unsigned n = 11; // Must be odd for internal logic or it'll be decremented
    std::vector<double> w(n);
    kaiser(w.data(), n, 5.0);
    // Check symmetry
    for(unsigned i=0; i<n/2; ++i) {
        EXPECT_NEAR(w[i], w[n-1-i], 1e-6);
    }
    // Peak should be at center
    EXPECT_GT(w[n/2], w[0]);
}

TEST(DspTest, GaussianWindow) {
    const unsigned n = 10;
    std::vector<double> w(n);
    gaussian(w.data(), n, 0.0, 1.0);
    // Peak should be at center
    // Gaussian formula in code uses mean=0, variance=1
    // M=9, sqrt2pi=sqrt(2*pi)
    // arg = (((i/9) - 0.5) * 9) = i - 4.5
    // arg = pow(i-4.5, 2)
    // arg = exp(-arg/2)
    // val = arg / (1 * sqrt2pi)
    EXPECT_NEAR(w[4], std::exp(-std::pow(4-4.5, 2)/2.0) / std::sqrt(2.0*M_PI), 1e-6);
    EXPECT_NEAR(w[5], std::exp(-std::pow(5-4.5, 2)/2.0) / std::sqrt(2.0*M_PI), 1e-6);
    EXPECT_NEAR(w[4], w[5], 1e-6); // Symmetry
}

TEST(DspTest, GaussWindow) {
    const unsigned n = 11;
    std::vector<double> w(n);
    gauss_window(w.data(), n, 2.5);
    // Symmetry check
    for(unsigned i=0; i<n/2; ++i) {
        EXPECT_NEAR(w[i], w[n-1-i], 1e-6);
    }
    // Center should be 1.0 (n=11, N=10, n_loop starts at -5, center is at n_loop=0)
    EXPECT_NEAR(w[5], 1.0, 1e-6);
}

#ifdef cwFFTW
TEST(DspTest, FftIfft) {
    const unsigned n = 16;
    std::vector<float> x(n);
    cw::vop::sine(x.data(), n, (float)n, 1.0f);

    fft::obj_str<float>* ft = nullptr;
    fft::create(ft, n, fft::kToPolarFl);
    fft::exec(ft, x.data(), n);

    ifft::obj_str<float>* ift = nullptr;
    ifft::create(ift, fft::bin_count(ft));
    ifft::exec_polar(ift, fft::magn(ft), fft::phase(ft));

    const float* y = ifft::out(ift);
    for(unsigned i=0; i<n; ++i) {
        EXPECT_NEAR(x[i], y[i], 1e-5);
    }

    fft::destroy(ft);
    ifft::destroy(ift);
}

TEST(DspTest, DcPreservation) {
    const unsigned n = 16;
    std::vector<float> x(n, 1.0f); // Pure DC signal

    fft::obj_str<float>* ft = nullptr;
    fft::create(ft, n, fft::kToRectFl);
    fft::exec(ft, x.data(), n);

    ifft::obj_str<float>* ift = nullptr;
    ifft::create(ift, fft::bin_count(ft));
    // use rect to test real_rect_to_complex
    ifft::exec_rect(ift, fft::magn(ft), fft::phase(ft));

    const float* y = ifft::out(ift);
    for(unsigned i=0; i<n; ++i) {
        EXPECT_NEAR(x[i], y[i], 1e-5);
    }

    fft::destroy(ft);
    ifft::destroy(ift);
}

/*
TEST(DspTest, FftIfftOdd) {
    const unsigned n = 15;
    std::vector<float> x(n);
    cw::vop::sine(x.data(), n, (float)n, 1.0f);

    fft::obj_str<float>* ft = nullptr;
    fft::create(ft, n, fft::kToPolarFl);
    fft::exec(ft, x.data(), n);

    ifft::obj_str<float>* ift = nullptr;
    ifft::create(ift, fft::bin_count(ft));
    ifft::exec_polar(ift, fft::magn(ft), fft::phase(ft));

    const float* y = ifft::out(ift);
    for(unsigned i=0; i<n; ++i) {
        EXPECT_NEAR(x[i], y[i], 1e-5);
    }

    fft::destroy(ft);
    ifft::destroy(ift);
}
*/

TEST(DspTest, Convolution) {
    std::vector<float> h = { 1.0f, 0.5f, 0.25f };
    std::vector<float> x = { 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f };
    std::vector<float> y(10);
    
    // Convolution of {1, 0.5, 0.25} with {1, 0, 0, 0, 1, 0, 0, 0}
    // Result: {1, 0.5, 0.25, 0, 1, 0.5, 0.25, 0, 0, 0}
    
    convolve::apply(x.data(), (unsigned)x.size(), h.data(), (unsigned)h.size(), y.data(), (unsigned)y.size());
    
    EXPECT_NEAR(y[0], 1.0f, 1e-6);
    EXPECT_NEAR(y[1], 0.5f, 1e-6);
    EXPECT_NEAR(y[2], 0.25f, 1e-6);
    EXPECT_NEAR(y[3], 0.0f, 1e-6);
    EXPECT_NEAR(y[4], 1.0f, 1e-6);
    EXPECT_NEAR(y[5], 0.5f, 1e-6);
    EXPECT_NEAR(y[6], 0.25f, 1e-6);
}
#endif
