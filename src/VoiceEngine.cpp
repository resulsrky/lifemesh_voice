#include "VoiceEngine.hpp"
#include <portaudio.h>
#include <opus/opus.h>
#include <cstring>
#include <chrono>
#include <cmath>
#include <thread>
#include <algorithm>

// ---------- AudioIO ----------
namespace {
struct PaState {
    PaStream* in = nullptr;
    PaStream* out = nullptr;
    int sampleRate = 16000;
    int channels = 1;
    int frameSamples = 320; // 20ms @16k
    int inIndex = -1;       // tercih edilen input cihaz index (PortAudio)
    int outIndex = -1;      // tercih edilen output cihaz index
};
PaState g;
}

void AudioIO::setPreferredDevices(int inIndex, int outIndex){
    g.inIndex = inIndex;
    g.outIndex = outIndex;
}

bool AudioIO::startCapture(int sampleRate, int channels) {
    Pa_Initialize();
    g.sampleRate = sampleRate; g.channels = channels;
    g.frameSamples = frameSamples(sampleRate, 20);

    PaStreamParameters inParams{};
    inParams.device = (g.inIndex >= 0 ? g.inIndex : Pa_GetDefaultInputDevice());
    inParams.channelCount = channels;
    inParams.sampleFormat = paInt16;
    inParams.suggestedLatency = 0.05; // 50 ms daha toleranslı

    if (Pa_OpenStream(&g.in, &inParams, nullptr, sampleRate, g.frameSamples,
                      paNoFlag, nullptr, nullptr) != paNoError) return false;
    return Pa_StartStream(g.in) == paNoError;
}

bool AudioIO::startPlayback(int sampleRate, int channels) {
    PaStreamParameters outParams{};
    outParams.device = (g.outIndex >= 0 ? g.outIndex : Pa_GetDefaultOutputDevice());
    outParams.channelCount = channels;
    outParams.sampleFormat = paInt16;
    outParams.suggestedLatency = 0.05;

    if (Pa_OpenStream(&g.out, nullptr, &outParams, sampleRate, g.frameSamples,
                      paNoFlag, nullptr, nullptr) != paNoError) return false;
    return Pa_StartStream(g.out) == paNoError;
}

bool AudioIO::readFrame(std::vector<int16_t>& outPcm) {
    outPcm.resize(g.frameSamples);
    PaError pe = Pa_ReadStream(g.in, outPcm.data(), g.frameSamples);
    if (pe == paNoError || pe == paInputOverflowed) return true;
    return false;
}

bool AudioIO::writeFrame(const std::vector<int16_t>& pcm) {
    if (!g.out) return false;
    return Pa_WriteStream(g.out, pcm.data(), pcm.size()) == paNoError;
}

void AudioIO::stop() {
    if (g.in) { Pa_StopStream(g.in); Pa_CloseStream(g.in); g.in=nullptr; }
    if (g.out){ Pa_StopStream(g.out);Pa_CloseStream(g.out);g.out=nullptr; }
    Pa_Terminate();
}

// ---------- SimpleVAD ----------
void SimpleVAD::configure(float thRms, int hangMs) {
    thr_=thRms; hangSamples_=hangMs*16;
}
bool SimpleVAD::isSpeech(const int16_t* pcm, int n, int) {
    double s=0; for (int i=0;i<n;i++){ s += double(pcm[i])*pcm[i]; }
    float rms = std::sqrt(s / std::max(1,n));
    if (rms > thr_) { remain_ = hangSamples_; return true; }
    if (remain_>0) { remain_ -= n; return true; }
    return false;
}

// ---------- OpusCodec ----------
bool OpusCodec::initEnc(int sampleRate, int bitrateBps, bool fec, bool dtx, int loss) {
    int err=0;
    enc_ = opus_encoder_create(sampleRate, 1, OPUS_APPLICATION_VOIP, &err);
    if (err!=OPUS_OK) return false;
    opus_encoder_ctl(enc_, OPUS_SET_BITRATE(bitrateBps));
    opus_encoder_ctl(enc_, OPUS_SET_COMPLEXITY(5));
    opus_encoder_ctl(enc_, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
    opus_encoder_ctl(enc_, OPUS_SET_INBAND_FEC(fec?1:0));
    opus_encoder_ctl(enc_, OPUS_SET_DTX(dtx?1:0));
    opus_encoder_ctl(enc_, OPUS_SET_PACKET_LOSS_PERC(loss));
    return true;
}
bool OpusCodec::initDec(int sampleRate) {
    int err=0;
    dec_ = opus_decoder_create(sampleRate, 1, &err);
    return err==OPUS_OK;
}
size_t OpusCodec::encode(const int16_t* pcm, int samples, uint8_t* out, size_t outMax) {
    int n = opus_encode(enc_, pcm, samples, out, (opus_int32)outMax);
    return n>0 ? (size_t)n : 0;
}
size_t OpusCodec::decode(const uint8_t* in, size_t inLen, int16_t* pcmOut, size_t maxSamples) {
    int n = opus_decode(dec_, in, (opus_int32)inLen, pcmOut, (int)maxSamples, 0);
    return n>0 ? (size_t)n : 0;
}
OpusCodec::~OpusCodec(){
    if(enc_) opus_encoder_destroy(enc_);
    if(dec_) opus_decoder_destroy(dec_);
}

// ---------- JitterBuffer ----------
JitterBuffer::JitterBuffer(uint16_t targetFrames):target_(targetFrames){
    window_.resize(64);
}
static inline int16_t diffSeq(uint16_t a, uint16_t b){ return (int16_t)(a-b); }
void JitterBuffer::push(uint16_t seq, std::vector<uint8_t> frame){
    std::lock_guard<std::mutex> lk(m_);
    if (!baseSet_) { baseSeq_=seq; baseSet_=true; }
    int16_t d = diffSeq(seq, baseSeq_);
    if (d<0 || d >= (int16_t)window_.size()) return;
    window_[d] = std::move(frame);
}
std::optional<EncodedFrame> JitterBuffer::popReady(){
    std::lock_guard<std::mutex> lk(m_);
    if (!baseSet_) return std::nullopt;
    if (window_[0].has_value()){
        EncodedFrame f{ baseSeq_, std::move(window_[0].value()) };
        for (size_t i=1;i<window_.size();++i) window_[i-1]=std::move(window_[i]);
        window_.back().reset();
        baseSeq_++;
        return f;
    }
    return std::nullopt;
}

// ---------- VoiceEngine ----------
static uint32_t nowMs(){
    using namespace std::chrono;
    return (uint32_t)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

void VoiceEngine::setDevices(int inIndex, int outIndex){
    audio_.setPreferredDevices(inIndex, outIndex);
}

bool VoiceEngine::init(const VoiceParams& vp, ITransport* tr, uint32_t convId){
    vp_=vp; tr_=tr; convId_=convId;
    if (!audio_.startCapture(vp.sampleRate,1)) return false;
    if (!audio_.startPlayback(vp.sampleRate,1)) return false;
    if (!codec_.initEnc(vp.sampleRate, vp.bitrateBps, vp.opusFec, vp.opusDtx, vp.expectedLoss)) return false;
    if (!codec_.initDec(vp.sampleRate)) return false;

    int frameSamples = audio_.frameSamples(vp_.sampleRate, vp_.frameMs);
    ns_.init(vp_.sampleRate, frameSamples, /*AGC*/true, /*NS dB*/-15);

    vad_.configure(300.f, 150);
    tr_->onReceive([this](const uint8_t* d, size_t l){ onRx(d,l); });
    return true;
}

void VoiceEngine::setPtt(bool){}

void VoiceEngine::onRx(const uint8_t* data, size_t len){
    if (len < sizeof(MeshVoiceHeader)) return;
    MeshVoiceHeader hdr{};
    std::memcpy(&hdr, data, sizeof(hdr));
    if (hdr.payLen == 0 || len < sizeof(hdr)+hdr.payLen) return;
    const uint8_t* enc = data + sizeof(hdr);
    std::vector<uint8_t> frame(enc, enc + hdr.payLen);
    jb_.push(hdr.seq, std::move(frame));
}

void VoiceEngine::pollOnce(){
    // ---- TX
    std::vector<int16_t> pcm;
    if (audio_.readFrame(pcm)) {
        ns_.process(pcm.data(), (int)pcm.size());
        bool speech = bypassVad_ ? true : vad_.isSpeech(pcm.data(), (int)pcm.size(), vp_.sampleRate);
        if (speech) {
            uint8_t encBuf[400];
            size_t encLen = codec_.encode(pcm.data(), (int)pcm.size(), encBuf, sizeof(encBuf));
            if (encLen>0) {
                MeshVoiceHeader hdr{};
                hdr.flags = 0b00000001; // PTT
                hdr.seq = ++seq_;
                hdr.convId = convId_;
                hdr.tsMs = nowMs();
                hdr.payLen = (uint16_t)encLen;

                std::vector<uint8_t> pkt(sizeof(hdr) + encLen);
                std::memcpy(pkt.data(), &hdr, sizeof(hdr));
                std::memcpy(pkt.data()+sizeof(hdr), encBuf, encLen);

                if (localEcho_) { onRx(pkt.data(), pkt.size()); }
                tr_->send(pkt.data(), pkt.size());
                txFrames_++;
            }
        }
    }

    // ---- RX
    auto ready = jb_.popReady();
    if (ready.has_value()) {
        std::vector<int16_t> outPcm( audio_.frameSamples(vp_.sampleRate, vp_.frameMs) );
        size_t ns = codec_.decode(ready->payload.data(), ready->payload.size(),
                                  outPcm.data(), outPcm.size());
        if (ns>0) {
            outPcm.resize(ns);
            audio_.writeFrame(outPcm);
            rxFrames_++;
        } else {
            std::vector<int16_t> zeros(outPcm.size(), 0);
            audio_.writeFrame(zeros);
        }
    } else {
        // underrun azalt: paket yoksa sessizlik bas
        std::vector<int16_t> zeros( audio_.frameSamples(vp_.sampleRate, vp_.frameMs), 0 );
        audio_.writeFrame(zeros);
    }
}

void VoiceEngine::shutdown(){ audio_.stop(); }
