#include "NoiseSuppressorSpeex.hpp"
#include <iostream>

NoiseSuppressorSpeex::NoiseSuppressorSpeex() {}
NoiseSuppressorSpeex::~NoiseSuppressorSpeex() {
    if (st_) {
        speex_preprocess_state_destroy(st_);
        st_ = nullptr;
    }
}

bool NoiseSuppressorSpeex::init(int sampleRate, int frameSize, bool enableAgc, int agcTarget) {
    sampleRate_ = sampleRate;
    frameSize_ = frameSize;

    st_ = speex_preprocess_state_init(frameSize_, sampleRate_);
    if (!st_) {
        std::cerr << "Speex preprocessor init failed\n";
        return false;
    }

    int i = 1;

    // Noise suppression (hafif, sesi boğmayacak şekilde)
    speex_preprocess_ctl(st_, SPEEX_PREPROCESS_SET_DENOISE, &i);
    int noiseSuppressDb = -15;  // daha az agresif (önceden -30’du)
    speex_preprocess_ctl(st_, SPEEX_PREPROCESS_SET_NOISE_SUPPRESS, &noiseSuppressDb);

    // VAD'i kapatıyoruz, çünkü kendi VAD zincirimiz var
    i = 0;
    speex_preprocess_ctl(st_, SPEEX_PREPROCESS_SET_VAD, &i);

    // AGC ayarları
    if (enableAgc) {
        i = 1;
        speex_preprocess_ctl(st_, SPEEX_PREPROCESS_SET_AGC, &i);
        int target = agcTarget > 0 ? agcTarget : 16000; // default 16k amplitude
        speex_preprocess_ctl(st_, SPEEX_PREPROCESS_SET_AGC_TARGET, &target);
    }

    // Reverb removal opsiyonel (kapalı bırakıyoruz)
    i = 0;
    speex_preprocess_ctl(st_, SPEEX_PREPROCESS_SET_DEREVERB, &i);

    return true;
}

void NoiseSuppressorSpeex::process(int16_t* pcm, int frameSize) {
    if (!st_) return;
    int vad = speex_preprocess_run(st_, pcm);
    (void)vad; // kendi VAD zincirimiz var, bunu kullanmıyoruz
}
