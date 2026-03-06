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
#include "cwDspTransforms.h"
#include <vector>
#include <cmath>
#include <algorithm>
#include <cstdio>

using namespace cw;
using namespace cw::dsp;

class DspTransformsTest : public ::testing::Test {
protected:
    void SetUp() override {
    }

    void TearDown() override {
    }
};

TEST_F(DspTransformsTest, Compressor) {
    srate_t srate = 44100.0f;
    unsigned procSmpCnt = 64;
    coeff_t inGain = 1.0f;
    ftime_t rmsWndMaxMs = 100.0;
    ftime_t rmsWndMs = 10.0;
    coeff_t threshDb = -10.0f;
    coeff_t ratio = 4.0f;
    ftime_t atkMs = 10.0;
    ftime_t rlsMs = 100.0;
    coeff_t outGain = 1.0f;
    bool bypassFl = false;

    compressor::obj_t* p = nullptr;
    EXPECT_EQ(compressor::create(p, srate, procSmpCnt, inGain, rmsWndMaxMs, rmsWndMs, threshDb, ratio, atkMs, rlsMs, outGain, bypassFl), kOkRC);
    ASSERT_NE(p, nullptr);

    std::vector<sample_t> x(procSmpCnt, 0.1f); // -20dB (below -10dB threshold)
    std::vector<sample_t> y(procSmpCnt);

    EXPECT_EQ(compressor::exec(p, x.data(), y.data(), procSmpCnt, true), kOkRC);
    // When below threshold, output should be roughly input * inGain * outGain
    for(unsigned i=0; i<procSmpCnt; ++i) {
        EXPECT_NEAR(y[i], x[i], 0.01f);
    }

    // Now use a signal above threshold
    std::fill(x.begin(), x.end(), 0.9f); // ~ -0.9dB
    EXPECT_EQ(compressor::exec(p, x.data(), y.data(), procSmpCnt, true), kOkRC);
    
    // Gain should eventually decrease
    EXPECT_LT(p->gain, 1.0f);

    // Test setters
    compressor::set_attack_ms(p, 20.0);
    compressor::set_release_ms(p, 200.0);
    compressor::set_thresh_db(p, -20.0f);
    compressor::set_rms_wnd_ms(p, 20.0);

    EXPECT_EQ(compressor::destroy(p), kOkRC);
    EXPECT_EQ(p, nullptr);
}

TEST_F(DspTransformsTest, Limiter) {
    srate_t srate = 44100.0f;
    unsigned procSmpCnt = 64;
    coeff_t thresh = 0.5f;
    coeff_t igain = 1.0f;
    coeff_t ogain = 0.5f; // Use smaller ogain to see limiting effect
    bool bypassFl = false;

    limiter::obj_t* p = nullptr;
    EXPECT_EQ(limiter::create(p, srate, procSmpCnt, thresh, igain, ogain, bypassFl), kOkRC);
    ASSERT_NE(p, nullptr);

    std::vector<sample_t> x(procSmpCnt, 0.9f);
    std::vector<sample_t> y(procSmpCnt);

    EXPECT_EQ(limiter::exec(p, x.data(), y.data(), procSmpCnt), kOkRC);
    // With ogain=0.5, T = 0.5 * 0.5 = 0.25.
    // y = 0.999 * (0.25 + (1.0 - 0.25) * (0.9 - 0.5) / (1.0 - 0.5))
    // y = 0.999 * (0.25 + 0.75 * 0.4 / 0.5) = 0.999 * (0.25 + 0.6) = 0.999 * 0.85 = 0.84915
    for(unsigned i=0; i<procSmpCnt; ++i) {
        EXPECT_LT(std::abs(y[i]), 0.9f); 
    }

    EXPECT_EQ(limiter::destroy(p), kOkRC);
    EXPECT_EQ(p, nullptr);
}

TEST_F(DspTransformsTest, DcFilter) {
    srate_t srate = 44100.0f;
    unsigned procSmpCnt = 64;
    coeff_t gain = 1.0f;
    bool bypassFl = false;

    dc_filter::obj_t* p = nullptr;
    EXPECT_EQ(dc_filter::create(p, srate, procSmpCnt, gain, bypassFl), kOkRC);
    ASSERT_NE(p, nullptr);

    // Signal with DC offset: 0.5 DC + 0.1 sine
    unsigned n = 1024 * 64; // Increase samples to allow filter to settle
    std::vector<sample_t> x(n);
    for(unsigned i=0; i<n; ++i) x[i] = 0.5f + 0.1f * std::sin(2.0f * (float)M_PI * 1000.0f * i / srate);
    std::vector<sample_t> y(n);

    // Process in blocks
    for(unsigned i=0; i<n; i += procSmpCnt) {
        EXPECT_EQ(dc_filter::exec(p, x.data() + i, y.data() + i, procSmpCnt), kOkRC);
    }

    // After some time, the DC should be removed. 
    // The last block should have mean near 0.
    double sum = 0;
    for(unsigned i=n - procSmpCnt; i<n; ++i) sum += y[i];
    EXPECT_NEAR(sum / procSmpCnt, 0.0, 0.05);

    EXPECT_EQ(dc_filter::set(p, 0.5f, true), kOkRC);
    EXPECT_EQ(p->gain, 0.5f);
    EXPECT_TRUE(p->bypassFl);

    EXPECT_EQ(dc_filter::destroy(p), kOkRC);
    EXPECT_EQ(p, nullptr);
}

TEST_F(DspTransformsTest, Recorder) {
    srate_t srate = 44100.0f;
    ftime_t max_secs = 0.1; 
    unsigned chN = 2;

    recorder::obj_t* p = nullptr;
    EXPECT_EQ(recorder::create(p, srate, max_secs, chN), kOkRC);
    ASSERT_NE(p, nullptr);

    unsigned frameN = 100;
    std::vector<sample_t> x(frameN * chN, 0.5f);
    
    // Test interleaved exec
    EXPECT_EQ(recorder::exec(p, x.data(), chN, frameN), kOkRC);
    EXPECT_EQ(p->frameIdx, frameN);

    // Test non-interleaved exec
    std::vector<sample_t> ch0(frameN, 0.1f);
    std::vector<sample_t> ch1(frameN, 0.2f);
    const sample_t* chA[] = { ch0.data(), ch1.data() };
    EXPECT_EQ(recorder::exec(p, chA, chN, frameN), kOkRC);
    EXPECT_EQ(p->frameIdx, 2 * frameN);

    // Test write
    const char* tmp_fn = "test_recorder_output.wav";
    EXPECT_EQ(recorder::write(p, tmp_fn), kOkRC);
    std::remove(tmp_fn);

    EXPECT_EQ(recorder::destroy(p), kOkRC);
    EXPECT_EQ(p, nullptr);
}

TEST_F(DspTransformsTest, AudioMeter) {
    srate_t srate = 44100.0f;
    ftime_t maxWndMs = 100.0;
    ftime_t wndMs = 10.0;
    coeff_t peakThreshDb = -3.0f;

    audio_meter::obj_t* p = nullptr;
    EXPECT_EQ(audio_meter::create(p, srate, maxWndMs, wndMs, peakThreshDb), kOkRC);
    ASSERT_NE(p, nullptr);

    unsigned n = 1000;
    std::vector<sample_t> x(n, 0.5f); // -6.02 dB
    
    EXPECT_EQ(audio_meter::exec(p, x.data(), n), kOkRC);
    
    EXPECT_NEAR(p->outLin, 0.5f, 1e-6);
    EXPECT_NEAR(p->outDb, ampl_to_db(0.5f), 1e-5);
    EXPECT_FALSE(p->peakFl);

    // Now exceed threshold
    std::fill(x.begin(), x.end(), 0.9f); // ~ -0.9 dB
    EXPECT_EQ(audio_meter::exec(p, x.data(), n), kOkRC);
    EXPECT_TRUE(p->peakFl);
    EXPECT_GT(p->peakCnt, 0u);

    // Test clip
    std::fill(x.begin(), x.end(), 1.1f); 
    EXPECT_EQ(audio_meter::exec(p, x.data(), n), kOkRC);
    EXPECT_TRUE(p->clipFl);
    EXPECT_GT(p->clipCnt, 0u);

    audio_meter::reset(p);
    EXPECT_FALSE(p->peakFl);
    EXPECT_FALSE(p->clipFl);
    EXPECT_EQ(p->peakCnt, 0u);
    EXPECT_EQ(p->clipCnt, 0u);

    audio_meter::set_window_ms(p, 20.0);
    // After setting window, we might need to check if internal state updated correctly
    // Depending on implementation, it might change wndSmpN
    EXPECT_NEAR((ftime_t)p->wndSmpN, 20.0 * srate / 1000.0, 1.0);

    EXPECT_EQ(audio_meter::destroy(p), kOkRC);
    EXPECT_EQ(p, nullptr);
}
