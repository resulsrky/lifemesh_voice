#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>
#include <functional>
#include <queue>
#include <mutex>
#include <optional>

#include "NoiseSuppressorSpeex.hpp"

#pragma pack(push,1)
struct MeshVoiceHeader {
    uint8_t  version = 1;
    uint8_t  codec   = 1;
    uint8_t  flags   = 0;
    uint8_t  hop     = 0;
    uint16_t seq     = 0;
    uint32_t convId  = 0;
    uint32_t tsMs    = 0;
    uint16_t payLen  = 0;
};
#pragma pack(pop)

class ITransport {
public:
    using RxHandler = std::function<void(const uint8_t*, size_t)>;
    virtual bool send(const uint8_t* data, size_t len) = 0;
    virtual void onReceive(RxHandler h) = 0;
    virtual ~ITransport() = default;
};

struct VoiceParams {
    int sampleRate   = 16000;
    int frameMs      = 20;
    int bitrateBps   = 12000;
    bool opusFec     = true;
    bool opusDtx     = false;  // debug için kapalı
    int expectedLoss = 15;
};

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
    // EKLE: tercih edilen cihaz indekslerini (PortAudio index) ayarla
    void setPreferredDevices(int inIndex, int outIndex);
};

class SimpleVAD {
public:
    void configure(float thRms = 300.0f, int hangMs = 150);
    bool isSpeech(const int16_t* pcm, int n, int sampleRate);
private:
    int hangSamples_ = 0, remain_ = 0; float thr_=300.f;
};

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

class VoiceEngine {
public:
    // Cihaz tercihlerini init'ten ÖNCE ayarla (PortAudio index; -1 = varsayılan)
    void setDevices(int inIndex, int outIndex);
    bool init(const VoiceParams& vp, ITransport* tr, uint32_t convId);
    void setPtt(bool down);
    void setLocalEcho(bool on) { localEcho_ = on; }
    void setBypassVad(bool on) { bypassVad_ = on; }
    void pollOnce();
    void shutdown();

    // Sayaç getter'ları
    uint64_t txFrames() const { return txFrames_; }
    uint64_t rxFrames() const { return rxFrames_; }

private:
    VoiceParams vp_;
    ITransport* tr_ = nullptr;
    uint32_t convId_ = 0;
    uint16_t seq_ = 0;

    AudioIO audio_;
    SimpleVAD vad_;
    OpusCodec codec_;
    JitterBuffer jb_{3};
    NoiseSuppressorSpeex ns_;

    bool localEcho_ = false;
    bool bypassVad_ = false;

    uint64_t txFrames_ = 0;
    uint64_t rxFrames_ = 0;

    void onRx(const uint8_t* data, size_t len);
};
