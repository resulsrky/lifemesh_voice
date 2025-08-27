#include "NoiseSuppressorSpeex.hpp"

#ifdef LIFEMESH_HAVE_SPEEXDSP
#include <speex/speex_preprocess.h>

bool NoiseSuppressorSpeex::init(int sampleRate, int frameSamples, bool enableAgc, int noiseSuppressDb){
    fs_ = sampleRate; frame_ = frameSamples; agc_ = enableAgc; nsDb_ = noiseSuppressDb;

    st_ = speex_preprocess_state_init(frame_, fs_);
    if (!st_) return false;

    int i = 1;
    speex_preprocess_ctl((SpeexPreprocessState*)st_, SPEEX_PREPROCESS_SET_DENOISE, &i);

    i = nsDb_;
    speex_preprocess_ctl((SpeexPreprocessState*)st_, SPEEX_PREPROCESS_SET_NOISE_SUPPRESS, &i);

    i = 0; // kendi VAD’imiz var
    speex_preprocess_ctl((SpeexPreprocessState*)st_, SPEEX_PREPROCESS_SET_VAD, &i);

    i = agc_ ? 1 : 0;
    speex_preprocess_ctl((SpeexPreprocessState*)st_, SPEEX_PREPROCESS_SET_AGC, &i);

    int agcLevel = 20000; // hedef ~telefon seviyesi
    speex_preprocess_ctl((SpeexPreprocessState*)st_, SPEEX_PREPROCESS_SET_AGC_TARGET, &agcLevel);

    i = 0;
    speex_preprocess_ctl((SpeexPreprocessState*)st_, SPEEX_PREPROCESS_SET_DEREVERB, &i);

    return true;
}

void NoiseSuppressorSpeex::process(int16_t* pcm, int nSamples){
    if (!st_ || nSamples != frame_) return;
    speex_preprocess_run((SpeexPreprocessState*)st_, pcm);
}

void NoiseSuppressorSpeex::shutdown(){
    if (st_) { speex_preprocess_state_destroy((SpeexPreprocessState*)st_); st_ = nullptr; }
}
#else
// stub
#endif
