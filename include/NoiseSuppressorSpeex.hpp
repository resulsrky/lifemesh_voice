#pragma once
#include <cstdint>
#include <vector>
#include <speex/speex_preprocess.h>

class NoiseSuppressorSpeex {
public:
    NoiseSuppressorSpeex();
    ~NoiseSuppressorSpeex();

    bool init(int sampleRate, int frameSize, bool enableAgc, int agcTarget);
    void process(int16_t* pcm, int frameSize);

private:
    SpeexPreprocessState* st_ = nullptr;
    int sampleRate_ = 16000;
    int frameSize_ = 320;
};
