#include <gtest/gtest.h>
#include "cwMidi.h"
#include <cmath>

using namespace cw::midi;

TEST(MidiTest, UtilityFunctions) {
    // removeCh
    EXPECT_EQ(removeCh(0x91), 0x90);
    EXPECT_EQ(removeCh(0x8F), 0x80);
    EXPECT_EQ(removeCh(0xF0), 0xF0);

    // isStatus
    EXPECT_TRUE(isStatus(0x80));
    EXPECT_TRUE(isStatus(0x90));
    EXPECT_TRUE(isStatus(0xF0));
    EXPECT_TRUE(isStatus(0xFF));
    EXPECT_FALSE(isStatus(0x7F));
    EXPECT_FALSE(isStatus(0x00));

    // isChStatus
    EXPECT_TRUE(isChStatus(0x80));
    EXPECT_TRUE(isChStatus(0x91));
    EXPECT_TRUE(isChStatus(0xEF));
    EXPECT_FALSE(isChStatus(0xF0));
    EXPECT_FALSE(isChStatus(0x7F));

    // isCtlStatus
    EXPECT_TRUE(isCtlStatus(0xB0));
    EXPECT_TRUE(isCtlStatus(0xB5));
    EXPECT_FALSE(isCtlStatus(0x90));

    // isNoteOnStatus
    EXPECT_TRUE(isNoteOnStatus(0x90));
    EXPECT_TRUE(isNoteOnStatus(0x9F));
    EXPECT_FALSE(isNoteOnStatus(0x80));

    // isNoteOn
    EXPECT_TRUE(isNoteOn((uint8_t)0x90, (uint8_t)64));
    EXPECT_FALSE(isNoteOn((uint8_t)0x90, (uint8_t)0)); // velocity 0 is note off
    EXPECT_FALSE(isNoteOn((uint8_t)0x80, (uint8_t)64));

    // isNoteOff
    EXPECT_TRUE(isNoteOff((uint8_t)0x80, (uint8_t)64));
    EXPECT_TRUE(isNoteOff((uint8_t)0x8F, (uint8_t)0));
    EXPECT_TRUE(isNoteOff((uint8_t)0x90, (uint8_t)0)); // velocity 0 note on is note off
    EXPECT_FALSE(isNoteOff((uint8_t)0x90, (uint8_t)64));

    // isCtl
    EXPECT_TRUE(isCtl((uint8_t)0xB0));
    EXPECT_TRUE(isCtl((uint8_t)0xBF));
    EXPECT_FALSE(isCtl((uint8_t)0x90));

    // Pedals
    EXPECT_TRUE(isPedal((uint8_t)0xB0, (uint8_t)kSustainCtlMdId));
    EXPECT_TRUE(isPedal((uint8_t)0xB0, (uint8_t)kLegatoCtlMdId));
    EXPECT_FALSE(isPedal((uint8_t)0x90, (uint8_t)kSustainCtlMdId));
    EXPECT_FALSE(isPedal((uint8_t)0xB0, (uint8_t)10));

    EXPECT_TRUE(isPedalDown((uint8_t)64));
    EXPECT_TRUE(isPedalDown((uint8_t)127));
    EXPECT_FALSE(isPedalDown((uint8_t)63));
    EXPECT_FALSE(isPedalDown((uint8_t)0));

    EXPECT_TRUE(isPedalUp((uint8_t)0));
    EXPECT_TRUE(isPedalUp((uint8_t)63));
    EXPECT_FALSE(isPedalUp((uint8_t)64));

    EXPECT_TRUE(isSustainPedal((uint8_t)0xB0, (uint8_t)kSustainCtlMdId));
    EXPECT_FALSE(isSustainPedal((uint8_t)0xB0, (uint8_t)kSoftPedalCtlMdId));

    EXPECT_TRUE(isAllNotesOff((uint8_t)0xB0, (uint8_t)kAllNotesOffMdId));
    EXPECT_TRUE(isResetAllCtls((uint8_t)0xB0, (uint8_t)kResetAllCtlsMdId));
}

TEST(MidiTest, Labels) {
    EXPECT_STREQ(statusToLabel(0x80), "nof");
    EXPECT_STREQ(statusToLabel(0x91), "non");
    EXPECT_STREQ(statusToLabel(0xB0), "ctl");
    EXPECT_STREQ(statusToLabel(0xF0), "sex");
    EXPECT_STREQ(statusToLabel(0xF8), "clk");
    EXPECT_EQ(statusToLabel(0x7F), nullptr);

    EXPECT_STREQ(metaStatusToLabel(kTrkNameMdId), "name");
    EXPECT_STREQ(metaStatusToLabel(kTempoMdId), "tempo");
    EXPECT_STREQ(metaStatusToLabel(kEndOfTrkMdId), "eot");

    EXPECT_STREQ(pedalLabel(kSustainCtlMdId), "sustn");
    EXPECT_STREQ(pedalLabel(kSoftPedalCtlMdId), "soft");
}

TEST(MidiTest, ByteCount) {
    EXPECT_EQ(statusToByteCount(0x80), 2);
    EXPECT_EQ(statusToByteCount(0x91), 2);
    EXPECT_EQ(statusToByteCount(0xC0), 1);
    EXPECT_EQ(statusToByteCount(0xD0), 1);
    EXPECT_EQ(statusToByteCount(0xE0), 2);
    EXPECT_EQ(statusToByteCount(0xF0), kInvalidMidiByte); // SysEx is variable
    EXPECT_EQ(statusToByteCount(0xF8), 0); // Real-time
}

TEST(MidiTest, conversions14Bit) {
    uint8_t d0, d1;
    
    // to14Bits and split14Bits
    EXPECT_EQ(to14Bits(0x7F, 0x7F), 16383);
    EXPECT_EQ(to14Bits(0x40, 0x00), 8192);
    EXPECT_EQ(to14Bits(0x00, 0x00), 0);

    split14Bits(16383, d0, d1);
    EXPECT_EQ(d0, 0x7F);
    EXPECT_EQ(d1, 0x7F);

    split14Bits(8192, d0, d1);
    EXPECT_EQ(d0, 0x40);
    EXPECT_EQ(d1, 0x00);

    // toPbend and splitPbend
    EXPECT_EQ(toPbend(0x40, 0x00), 0);
    EXPECT_EQ(toPbend(0x7F, 0x7F), 8191);
    EXPECT_EQ(toPbend(0x00, 0x00), -8192);

    splitPbend(0, d0, d1);
    EXPECT_EQ(d0, 0x40);
    EXPECT_EQ(d1, 0x00);

    splitPbend(8191, d0, d1);
    EXPECT_EQ(d0, 0x7F);
    EXPECT_EQ(d1, 0x7F);

    splitPbend(-8192, d0, d1);
    EXPECT_EQ(d0, 0x00);
    EXPECT_EQ(d1, 0x00);
}

TEST(MidiTest, SciPitch) {
    char buf[kMidiSciPitchCharCnt];
    
    // midiToSciPitch
    EXPECT_STREQ(midiToSciPitch(60, buf, sizeof(buf)), "C4");
    EXPECT_STREQ(midiToSciPitch(0, buf, sizeof(buf)), "C-1");
    EXPECT_STREQ(midiToSciPitch(12, buf, sizeof(buf)), "C0");
    EXPECT_STREQ(midiToSciPitch(127, buf, sizeof(buf)), "G9");
    EXPECT_STREQ(midiToSciPitch(10, buf, sizeof(buf)), "A#-1");
    EXPECT_STREQ(midiToSciPitch(128, buf, sizeof(buf)), "");

    // sciPitchToMidiPitch
    EXPECT_EQ(sciPitchToMidiPitch('C', 0, 4), 60);
    EXPECT_EQ(sciPitchToMidiPitch('A', 1, -1), 10);
    EXPECT_EQ(sciPitchToMidiPitch('G', 0, 9), 127);
    EXPECT_EQ(sciPitchToMidiPitch('C', 0, -1), 0);
    EXPECT_EQ(sciPitchToMidiPitch('X', 0, 4), kInvalidMidiPitch);

    // sciPitchToMidi
    EXPECT_EQ(sciPitchToMidi("C4"), 60);
    EXPECT_EQ(sciPitchToMidi("c4"), 60);
    EXPECT_EQ(sciPitchToMidi("A#-1"), 10);
    EXPECT_EQ(sciPitchToMidi("Bb3"), 58);
    EXPECT_EQ(sciPitchToMidi("G9"), 127);
    EXPECT_EQ(sciPitchToMidi("C-1"), 0);
    EXPECT_EQ(sciPitchToMidi("C-2"), kInvalidMidiPitch);
    EXPECT_EQ(sciPitchToMidi("G10"), kInvalidMidiPitch);
    EXPECT_EQ(sciPitchToMidi("INVALID"), kInvalidMidiPitch);
    EXPECT_EQ(sciPitchToMidi(""), kInvalidMidiPitch);
    EXPECT_EQ(sciPitchToMidi(nullptr), kInvalidMidiPitch);
}

TEST(MidiTest, HzConversions) {
    // 440Hz is A4 (MIDI 69)
    EXPECT_EQ(hzToMidi(440.0), 69);
    EXPECT_NEAR(midiToHz(69), 440.0, 0.01);

    // C4 (MIDI 60) is approx 261.63Hz
    EXPECT_EQ(hzToMidi(261.63), 60);
    EXPECT_NEAR(midiToHz(60), 261.63, 0.01);

    // Limits
    EXPECT_EQ(hzToMidi(8.175), 0); // C-1
    EXPECT_NEAR(midiToHz(0), 8.175, 0.01);
    
    EXPECT_EQ(hzToMidi(12543.85), 127); // G9
    EXPECT_NEAR(midiToHz(127), 12543.85, 0.1);

    EXPECT_EQ(hzToMidi(0.0), 0);
    EXPECT_EQ(hzToMidi(1000000.0), 127);
}
