#pragma once
#include <cstdint>

// Eğer SpeexDSP bulunursa gerçek impl; aksi halde stub
#ifdef LIFEMESH_HAVE_SPEEXDSP

class NoiseSuppressorSpeex {
public:
    bool init(int sampleRate, int frameSamples, bool enableAgc = true, int noiseSuppressDb = -15);
    void process(int16_t* pcm, int nSamples); // in-place
    void shutdown();
    ~NoiseSuppressorSpeex(){ shutdown(); }
private:
    void* st_ = nullptr; // SpeexPreprocessState*
    int   fs_ = 16000;
    int   frame_ = 320;
    bool  agc_ = true;
    int   nsDb_ = -15;
};

#else

// Stub: kitaplık yoksa derleme devam eder, işlev yok
class NoiseSuppressorSpeex {
public:
    bool init(int, int, bool=true, int=-15) { return false; }
    void process(int16_t*, int) {}
    void shutdown() {}
};

#endif
