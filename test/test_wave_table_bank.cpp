#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwTest.h"
#include "cwMem.h"
#include "cwMath.h"
#include "cwVectOps.h"
#include "cwFile.h"
#include "cwText.h"
#include "cwObject.h"
#include "cwFileSys.h"
#include "cwAudioFile.h"
#include "cwDspTypes.h"
#include "cwDsp.h"
#include "cwAudioTransforms.h"
#include "cwMidi.h"
#include "cwWaveTableBank.h"
#include <gtest/gtest.h>
#include <vector>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

using namespace cw;

class WaveTableBankTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_dir = "test_wtb_dir";
        filesys::makeDir(test_dir.c_str());
        
        audio_fn = test_dir + "/test_audio.wav";
        cfg_fn = test_dir + "/test_instr.cfg";
        
        createDummyAudio(audio_fn.c_str());
        createDummyCfg(cfg_fn.c_str(), audio_fn.c_str());
    }

    void TearDown() override {
        // filesys::rmDir(test_dir.c_str()); 
    }

    std::string test_dir;
    std::string audio_fn;
    std::string cfg_fn;

    void createDummyAudio(const char* fn) {
        audiofile::handle_t h;
        unsigned chCnt = 1;
        unsigned frmCnt = 10000;
        double srate = 44100.0;
        
        audiofile::create(h, fn, srate, 16, chCnt);
        
        float* data_v = mem::alloc<float>(frmCnt);
        for(unsigned i=0; i<frmCnt; ++i) {
            data_v[i] = std::sin(2.0 * M_PI * 261.63 * i / srate);
        }
        float* data[1] = { data_v };
        audiofile::writeFloat(h, frmCnt, chCnt, data);
        audiofile::close(h);
        mem::release(data_v);
    }

    void createDummyCfg(const char* cfg_fn, const char* audio_fn) {
        // We can write the config as a string to the file
        // The project's object format is similar to JSON but often without quotes around keys
        FILE* f = fopen(cfg_fn, "w");
        fprintf(f, "{\n");
        fprintf(f, "  instr : \"test_instr\"\n");
        fprintf(f, "  pitchL : [\n");
        fprintf(f, "    {\n");
        fprintf(f, "       midi_pitch : 60\n");
        fprintf(f, "       srate : 44100.0\n");
        fprintf(f, "       est_hz_mean : 261.63\n");
        fprintf(f, "       audio_fname : \"%s\"\n", audio_fn);
        fprintf(f, "       velL : [\n");
        fprintf(f, "         {\n");
        fprintf(f, "            vel : 32\n");
        fprintf(f, "            bsi : 0\n");
        fprintf(f, "            chL : [\n");
        fprintf(f, "               [\n");
        fprintf(f, "                 { wtbi : 1000 wtei : 2000 rms : 0.5 est_hz : 261.63 }\n");
        fprintf(f, "               ]\n");
        fprintf(f, "            ]\n");
        fprintf(f, "         },\n");
        fprintf(f, "         {\n");
        fprintf(f, "            vel : 96\n");
        fprintf(f, "            bsi : 0\n");
        fprintf(f, "            chL : [\n");
        fprintf(f, "               [\n");
        fprintf(f, "                 { wtbi : 1000 wtei : 2000 rms : 0.5 est_hz : 261.63 }\n");
        fprintf(f, "               ]\n");
        fprintf(f, "            ]\n");
        fprintf(f, "         }\n");
        fprintf(f, "       ]\n");
        fprintf(f, "    }\n");
        fprintf(f, "  ]\n");
        fprintf(f, "}\n");
        fclose(f);
    }
};

TEST_F(WaveTableBankTest, Lifecycle) {
    wt_bank::handle_t h;
    rc_t rc = wt_bank::create(h, 2);
    EXPECT_EQ(rc, kOkRC);
    EXPECT_TRUE(h.isValid());
    
    rc = wt_bank::destroy(h);
    EXPECT_EQ(rc, kOkRC);
    EXPECT_FALSE(h.isValid());
}

TEST_F(WaveTableBankTest, LoadAndQuery) {
    wt_bank::handle_t h;
    rc_t rc = wt_bank::create(h, 2, cfg_fn.c_str(), 1);
    ASSERT_EQ(rc, kOkRC);
    
    EXPECT_EQ(wt_bank::instr_count(h), 1u);
    unsigned idx = wt_bank::instr_index(h, "test_instr");
    EXPECT_EQ(idx, 0u);
    
    unsigned velA[10];
    unsigned velCnt_Ref = 0;
    rc = wt_bank::instr_pitch_velocities(h, idx, 60, velA, 10, velCnt_Ref);
    EXPECT_EQ(rc, kOkRC);
    EXPECT_EQ(velCnt_Ref, 2u);
    EXPECT_EQ(velA[0], 32u);
    EXPECT_EQ(velA[1], 96u);
    
    const wt_bank::multi_ch_wt_seq_t* mcs_32 = nullptr;
    rc = wt_bank::get_wave_table(h, idx, 60, 32, mcs_32);
    EXPECT_EQ(rc, kOkRC);
    ASSERT_NE(mcs_32, nullptr);
    
    const wt_bank::multi_ch_wt_seq_t* mcs_96 = nullptr;
    rc = wt_bank::get_wave_table(h, idx, 60, 96, mcs_96);
    EXPECT_EQ(rc, kOkRC);
    ASSERT_NE(mcs_96, nullptr);
    
    // Test interpolation/mapping
    // vel 64 should be mapped to mcs_32
    const wt_bank::multi_ch_wt_seq_t* mcs_64 = nullptr;
    rc = wt_bank::get_wave_table(h, idx, 60, 64, mcs_64);
    EXPECT_EQ(rc, kOkRC);
    EXPECT_EQ(mcs_64, mcs_32);
    
    // vel 65 should be mapped to mcs_96
    const wt_bank::multi_ch_wt_seq_t* mcs_65 = nullptr;
    rc = wt_bank::get_wave_table(h, idx, 60, 65, mcs_65);
    EXPECT_EQ(rc, kOkRC);
    EXPECT_EQ(mcs_65, mcs_96);
    
    wt_bank::destroy(h);
}

TEST_F(WaveTableBankTest, InvalidQueries) {
    wt_bank::handle_t h;
    wt_bank::create(h, 2, cfg_fn.c_str(), 1);
    
    const wt_bank::multi_ch_wt_seq_t* mcs = nullptr;
    // Invalid instrument index
    EXPECT_NE(wt_bank::get_wave_table(h, 99, 60, 32, mcs), kOkRC);
    // Invalid pitch
    EXPECT_NE(wt_bank::get_wave_table(h, 0, 99, 32, mcs), kOkRC);
    // Unmapped velocity (below first velocity)
    EXPECT_NE(wt_bank::get_wave_table(h, 0, 60, 31, mcs), kOkRC);
    // Unmapped velocity (above last velocity)
    EXPECT_NE(wt_bank::get_wave_table(h, 0, 60, 97, mcs), kOkRC);
    // Invalid velocity (out of range)
    EXPECT_NE(wt_bank::get_wave_table(h, 0, 60, 128, mcs), kOkRC);
    
    wt_bank::destroy(h);
}

TEST_F(WaveTableBankTest, Report) {
    wt_bank::handle_t h;
    wt_bank::create(h, 2, cfg_fn.c_str(), 1);
    
    // Just ensure it doesn't crash
    wt_bank::report(h);
    
    wt_bank::destroy(h);
}
