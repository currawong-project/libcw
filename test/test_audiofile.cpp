#include <gtest/gtest.h>
#include <vector>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwMem.h"
#include "cwFileSys.h"
#include "cwObject.h" // Must be before cwAudioFile.h
#include "cwAudioFile.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

using namespace cw;
using namespace cw::audiofile;

class AudioFileTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_dir = "test_audio_dir";
        filesys::makeDir(test_dir);
    }

    void TearDown() override {
        // filesys::rmDir(test_dir); 
    }

    const char* test_dir;

    std::string getPath(const char* filename) {
        return std::string(test_dir) + "/" + filename;
    }

    void createSignal(unsigned chCnt, unsigned frmCnt, float** buf) {
        for (unsigned c = 0; c < chCnt; ++c) {
            for (unsigned f = 0; f < frmCnt; ++f) {
                buf[c][f] = std::sin(2.0 * M_PI * 440.0 * f / 44100.0 + c * M_PI / 2.0);
            }
        }
    }
};

TEST_F(AudioFileTest, CreateAndOpenWav16_Simple) {
    handle_t h;
    rc_t rc;
    std::string fn = getPath("test16_simple.wav");
    unsigned chCnt = 1;
    unsigned frmCnt = 10;

    rc = create(h, fn.c_str(), 44100.0, 16, chCnt);
    ASSERT_EQ(rc, kOkRC);

    float data_v[10];
    for(int i=0; i<10; ++i) data_v[i] = (float)i / 10.0f;
    float* data[1] = { data_v };

    rc = writeFloat(h, 10, 1, data);
    ASSERT_EQ(rc, kOkRC);
    close(h);

    rc = open(h, fn.c_str(), nullptr);
    ASSERT_EQ(rc, kOkRC);
    
    float read_v[10];
    float* read_ptr[1] = { read_v };
    rc = readFloat(h, 10, 0, 1, read_ptr, nullptr);
    ASSERT_EQ(rc, kOkRC);

    for(int i=0; i<10; ++i) {
        EXPECT_NEAR(read_v[i], data_v[i], 1.0/32768.0) << "at index " << i;
    }
    close(h);
}

TEST_F(AudioFileTest, CreateAndOpenAiff16Int) {
    handle_t h;
    rc_t rc;
    std::string fn = getPath("test16int.aif");
    unsigned chCnt = 1;
    unsigned frmCnt = 10;

    rc = create(h, fn.c_str(), 44100.0, 16, chCnt);
    ASSERT_EQ(rc, kOkRC);

    int data_v[10];
    for(int i=0; i<10; ++i) data_v[i] = i * 1000;
    int* data[1] = { data_v };

    rc = writeInt(h, 10, 1, data);
    ASSERT_EQ(rc, kOkRC);
    close(h);

    rc = open(h, fn.c_str(), nullptr);
    ASSERT_EQ(rc, kOkRC);
    
    int read_v[10];
    int* read_ptr[1] = { read_v };
    rc = readInt(h, 10, 0, 1, read_ptr, nullptr);
    ASSERT_EQ(rc, kOkRC);

    for(int i=0; i<10; ++i) {
        // Since we wrote 16-bit, the values read back into 32-bit int should be sign-extended.
        // writeInt(16-bit) takes int and casts to short.
        EXPECT_EQ((short)read_v[i], (short)data_v[i]) << "at index " << i;
    }
    close(h);
}

TEST_F(AudioFileTest, CreateAndOpenWav32Int) {
    handle_t h;
    rc_t rc;
    std::string fn = getPath("test32int.wav");
    unsigned chCnt = 1;
    unsigned frmCnt = 10;

    rc = create(h, fn.c_str(), 44100.0, 32, chCnt);
    ASSERT_EQ(rc, kOkRC);

    int data_v[10];
    for(int i=0; i<10; ++i) data_v[i] = i * 1000000;
    int* data[1] = { data_v };

    rc = writeInt(h, 10, 1, data);
    ASSERT_EQ(rc, kOkRC);
    close(h);

    rc = open(h, fn.c_str(), nullptr);
    ASSERT_EQ(rc, kOkRC);
    
    int read_v[10];
    int* read_ptr[1] = { read_v };
    rc = readInt(h, 10, 0, 1, read_ptr, nullptr);
    ASSERT_EQ(rc, kOkRC);

    for(int i=0; i<10; ++i) {
        EXPECT_EQ(read_v[i], data_v[i]) << "at index " << i;
    }
    close(h);
}

TEST_F(AudioFileTest, FloatFormat) {
    handle_t h;
    rc_t rc;
    std::string fn = getPath("test_float.wav");
    double srate = 44100.0;
    unsigned bits = 0; // 0 means float
    unsigned chCnt = 2;
    unsigned frmCnt = 100;

    rc = create(h, fn.c_str(), srate, bits, chCnt);
    ASSERT_EQ(rc, kOkRC);

    float* data[2];
    data[0] = mem::alloc<float>(frmCnt);
    data[1] = mem::alloc<float>(frmCnt);
    for(unsigned i=0; i<frmCnt; ++i) {
        data[0][i] = (float)i/frmCnt;
        data[1][i] = -(float)i/frmCnt;
    }

    rc = writeFloat(h, frmCnt, chCnt, data);
    ASSERT_EQ(rc, kOkRC);
    close(h);

    info_t info;
    rc = open(h, fn.c_str(), &info);
    ASSERT_EQ(rc, kOkRC);
    EXPECT_EQ(info.bits, 32); // float is 32 bits

    float* read_data[2];
    read_data[0] = mem::alloc<float>(frmCnt);
    read_data[1] = mem::alloc<float>(frmCnt);
    rc = readFloat(h, frmCnt, 0, chCnt, read_data, nullptr);
    ASSERT_EQ(rc, kOkRC);

    for (unsigned c = 0; c < chCnt; ++c) {
        for (unsigned f = 0; f < frmCnt; ++f) {
            EXPECT_FLOAT_EQ(read_data[c][f], data[c][f]);
        }
    }

    close(h);
    mem::release(data[0]);
    mem::release(data[1]);
    mem::release(read_data[0]);
    mem::release(read_data[1]);
}

TEST_F(AudioFileTest, CreateAndOpenWav8) {
    handle_t h;
    std::string fn = getPath("test8.wav");
    create(h, fn.c_str(), 44100.0, 8, 1);
    
    int data_v[10];
    for(int i=0; i<10; ++i) {
        data_v[i] = (i - 5) * 20; 
    }
    int* data[1] = { data_v };
    writeInt(h, 10, 1, data);
    close(h);

    open(h, fn.c_str(), nullptr);
    int read_v[10];
    int* read_ptr[1] = { read_v };
    readInt(h, 10, 0, 1, read_ptr, nullptr);
    
    for(int i=0; i<10; ++i) {
        EXPECT_EQ(read_v[i], data_v[i]) << "at index " << i;
    }
    close(h);
}

TEST_F(AudioFileTest, CreateAndOpenAiff8) {
    handle_t h;
    std::string fn = getPath("test8.aif");
    create(h, fn.c_str(), 44100.0, 8, 1);
    
    int data_v[10];
    for(int i=0; i<10; ++i) data_v[i] = (i - 5) * 20;
    int* data[1] = { data_v };
    writeInt(h, 10, 1, data);
    close(h);

    open(h, fn.c_str(), nullptr);
    int read_v[10];
    int* read_ptr[1] = { read_v };
    readInt(h, 10, 0, 1, read_ptr, nullptr);
    
    for(int i=0; i<10; ++i) {
        EXPECT_EQ(read_v[i], data_v[i]) << "at index " << i;
    }
    close(h);
}

TEST_F(AudioFileTest, ReadSum) {
    handle_t h;
    std::string fn = getPath("test_sum.wav");
    create(h, fn.c_str(), 44100.0, 16, 1);
    
    int data_v[10];
    for(int i=0; i<10; ++i) data_v[i] = 1000;
    int* data[1] = { data_v };
    writeInt(h, 10, 1, data);
    close(h);

    open(h, fn.c_str(), nullptr);
    int read_v[10];
    for(int i=0; i<10; ++i) read_v[i] = 500;
    int* read_ptr[1] = { read_v };
    
    // readSumInt should add file values to existing buffer values
    readSumInt(h, 10, 0, 1, read_ptr, nullptr);
    
    for(int i=0; i<10; ++i) {
        EXPECT_EQ(read_v[i], 1500) << "at index " << i;
    }
    close(h);
}

TEST_F(AudioFileTest, ReportAndPrint) {
    std::string fn = getPath("test_report.wav");
    float* data_v = mem::alloc<float>(10);
    for(int i=0; i<10; ++i) data_v[i] = (float)i/10.0f;
    float* data[1] = { data_v };
    writeFileFloat(fn.c_str(), 44100.0, 0, 10, 1, data);

    info_t info;
    getInfo(fn.c_str(), &info);
    
    // Test these functions don't crash. We don't easily check their output here.
    log::handle_t logH = log::globalHandle(); 
    
    printInfo(&info, logH);
    reportFn(fn.c_str(), logH, 0, 5);
    reportInfo(fn.c_str());

    mem::release(data_v);
}

TEST_F(AudioFileTest, SeekAndTell) {
    handle_t h;
    std::string fn = getPath("test_seek.wav");
    create(h, fn.c_str(), 44100.0, 16, 1);
    
    int* data[1];
    data[0] = mem::alloc<int>(100);
    for(int i=0; i<100; ++i) data[0][i] = i * 100;
    
    writeInt(h, 100, 1, data);
    close(h);

    open(h, fn.c_str(), nullptr);
    EXPECT_EQ(tell(h), 0);

    seek(h, 50);
    EXPECT_EQ(tell(h), 50);

    int read_val;
    int* read_ptr[1] = { &read_val };
    readInt(h, 1, 0, 1, read_ptr, nullptr);
    EXPECT_EQ(read_val, 50 * 100);
    EXPECT_EQ(tell(h), 51);

    seek(h, 99);
    EXPECT_FALSE(isEOF(h));
    readInt(h, 1, 0, 1, read_ptr, nullptr);
    EXPECT_TRUE(isEOF(h));

    close(h);
    mem::release(data[0]);
}

TEST_F(AudioFileTest, MinMaxMean) {
    std::string fn = getPath("test_mmm.wav");
    float* data_v = mem::alloc<float>(100);
    float* data[1] = { data_v };
    for(int i=0; i<100; ++i) data_v[i] = (float)i/100.0f;
    
    writeFileFloat(fn.c_str(), 44100.0, 0, 100, 1, data);

    float minV, maxV, meanV;
    rc_t rc = minMaxMeanFn(fn.c_str(), 0, &minV, &maxV, &meanV);
    ASSERT_EQ(rc, kOkRC);

    EXPECT_NEAR(minV, 0.0f, 1e-6);
    EXPECT_NEAR(maxV, 0.99f, 1e-6);
    EXPECT_NEAR(meanV, 49.5f/100.0f, 1e-6);

    mem::release(data_v);
}

TEST_F(AudioFileTest, GetInfo) {
    std::string fn = getPath("test_info.wav");
    writeFileFloat(fn.c_str(), 22050.0, 16, 0, 2, nullptr);

    info_t info;
    rc_t rc = getInfo(fn.c_str(), &info);
    ASSERT_EQ(rc, kOkRC);
    EXPECT_EQ(info.chCnt, 2);
    EXPECT_EQ(info.bits, 16);
    EXPECT_DOUBLE_EQ(info.srate, 22050.0);
}

TEST_F(AudioFileTest, SetSrate) {
    std::string fn = getPath("test_srate.wav");
    writeFileFloat(fn.c_str(), 44100.0, 16, 0, 1, nullptr);

    rc_t rc = setSrate(fn.c_str(), 48000);
    ASSERT_EQ(rc, kOkRC);

    info_t info;
    getInfo(fn.c_str(), &info);
    EXPECT_DOUBLE_EQ(info.srate, 48000.0);
}

TEST_F(AudioFileTest, AllocFloatBuf) {
    std::string fn = getPath("test_alloc.wav");
    float* data_v = mem::alloc<float>(10);
    for(int i=0; i<10; ++i) data_v[i] = (float)i;
    float* data[1] = { data_v };
    writeFileFloat(fn.c_str(), 44100.0, 0, 10, 1, data);

    float** chBuf = nullptr;
    unsigned chCnt = 0;
    unsigned frmCnt = 0;
    info_t info;
    rc_t rc = allocFloatBuf(fn.c_str(), chBuf, chCnt, frmCnt, info, 2, 5, 0, 1);
    ASSERT_EQ(rc, kOkRC);
    EXPECT_EQ(chCnt, 1);
    EXPECT_EQ(frmCnt, 5);
    
    for(unsigned i=0; i<5; ++i) {
        EXPECT_FLOAT_EQ(chBuf[0][i], (float)(2+i));
    }

    freeFloatBuf(chBuf, chCnt);
    mem::release(data_v);
}

TEST_F(AudioFileTest, WriteInterleaved) {
    handle_t h;
    std::string fn = getPath("test_interleaved.wav");
    create(h, fn.c_str(), 44100.0, 0, 2);
    
    float data[10] = { 0, 10, 1, 11, 2, 12, 3, 13, 4, 14 };
    rc_t rc = writeFloatInterleaved(h, 5, 2, data);
    ASSERT_EQ(rc, kOkRC);
    close(h);

    float* read_data[2];
    read_data[0] = mem::alloc<float>(5);
    read_data[1] = mem::alloc<float>(5);
    
    getFloat(fn.c_str(), 0, 5, 0, 2, read_data, nullptr, nullptr);
    
    for(int i=0; i<5; ++i) {
        EXPECT_FLOAT_EQ(read_data[0][i], (float)i);
        EXPECT_FLOAT_EQ(read_data[1][i], (float)(i+10));
    }

    mem::release(read_data[0]);
    mem::release(read_data[1]);
}
