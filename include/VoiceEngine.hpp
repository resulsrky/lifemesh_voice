#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>
#include <functional>
#include <queue>
#include <mutex>
#include <optional>

#include "NoiseSuppressorSpeex.hpp"

// -------- Paket başlığı --------
#pragma pack(push,1)
struct MeshVoiceHeader {
    uint8_t  version = 1;
    uint8_t  codec   = 1;      // 1=Opus
    uint8_t  flags   = 0;      // bit0=PTT, bit1=DTN, bit2=FEC
    uint8_t  hop     = 0;
    uint16_t seq     = 0;
    uint32_t convId  = 0;
    uint32_t tsMs    = 0;
    uint16_t payLen  = 0;
};
#pragma pack(pop)

// -------- Transport arayüzü --------
class ITransport {
public:
    using RxHandler = std::function<void(const uint8_t*, size_t)>;
    virtual bool send(const uint8_t* data, size_t len) = 0;
    virtual void onReceive(RxHandler h) = 0;
    virtual ~ITransport() = default;
};

// -------- Parametreler --------
struct VoiceParams {
    int sampleRate   = 16000;
    int frameMs      = 20;
    int bitrateBps   = 12000;
    bool opusFec     = true;
    bool opusDtx     = false;   // debug için kapalı (istersen tekrar true)
    int expectedLoss = 15;
};

// -------- Audio I/O --------
class AudioIO {
public:
    bool startCapture(int sampleRate, int channels);
    bool startPlayback(int sampleRate, int channels);
    bool readFrame(std::vector<int16_t>& outPcm);
    bool writeFrame(const std::vector<int16_t>& pcm);
    void stop();
    int  frameSamples(int sampleRate, int frameMs) const {
        return sampleRate * frameMs / 1000;
    }
};

// -------- Basit VAD --------
class SimpleVAD {
public:
    void configure(float thRms = 300.0f, int hangMs = 150);
    bool isSpeech(const int16_t* pcm, int n, int sampleRate);
private:
    int hangSamples_ = 0, remain_ = 0; float thr_=300.f;
};

// -------- Opus codec --------
class OpusCodec {
public:
    bool initEnc(int sampleRate, int bitrateBps, bool fec, bool dtx, int expectedLoss);
    bool initDec(int sampleRate);
    size_t encode(const int16_t* pcm, int samples, uint8_t* out, size_t outMax);
    size_t decode(const uint8_t* in, size_t inLen, int16_t* pcmOut, size_t maxSamples);
    ~OpusCodec();
private:
    struct OpusEncoder* enc_ = nullptr;
    struct OpusDecoder* dec_ = nullptr;
};

// -------- Jitter buffer --------
struct EncodedFrame {
    uint16_t seq;
    std::vector<uint8_t> payload;
};

class JitterBuffer {
public:
    explicit JitterBuffer(uint16_t targetFrames = 3);
    void push(uint16_t seq, std::vector<uint8_t> frame);
    std::optional<EncodedFrame> popReady();
private:
    uint16_t target_;
    uint16_t baseSeq_{0};
    bool baseSet_{false};
    std::vector<std::optional<std::vector<uint8_t>>> window_;
    std::mutex m_;
};

// -------- VoiceEngine --------
class VoiceEngine {
public:
    bool init(const VoiceParams& vp, ITransport* tr, uint32_t convId);
    void setPtt(bool down);
    void setLocalEcho(bool on) { localEcho_ = on; }
    void setBypassVad(bool on) { bypassVad_ = on; }
    void pollOnce();
    void shutdown();
private:
    VoiceParams vp_;
    ITransport* tr_ = nullptr;
    uint32_t convId_ = 0;
    uint16_t seq_ = 0;

    AudioIO audio_;
    SimpleVAD vad_;
    OpusCodec codec_;
    JitterBuffer jb_{3};
    NoiseSuppressorSpeex ns_;   // NS/AGC

    bool localEcho_ = false;
    bool bypassVad_ = false;

    void onRx(const uint8_t* data, size_t len);
};
