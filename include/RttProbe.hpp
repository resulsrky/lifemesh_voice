#pragma once
#include <atomic>
#include <cstdint>
#include <thread>
#include <string>
#include <netinet/in.h>

class RttProbe {
public:
    RttProbe(const std::string& echoServerIp, uint16_t echoPort,
             const std::string& localIp="0.0.0.0", uint16_t localPort=0);
    ~RttProbe();

    bool start();
    void stop();

    double rttMs() const { return rtt_ewma_ms_.load(); }

private:
    static constexpr uint32_t MAGIC = 0xABCD1357;
    static constexpr uint8_t  VERSION = 1;
    static constexpr uint8_t  MSG_PING = 1;
    static constexpr uint8_t  MSG_ECHO = 2;
    static constexpr size_t   PACKET_SIZE = 64;

    int fd_ = -1;
    sockaddr_in remote_{};
    sockaddr_in local_{};

    std::atomic<bool> running_{false};
    std::thread th_;

    std::atomic<double> rtt_ewma_ms_{-1.0};
    double alpha_ = 0.2;
    uint32_t seq_ = 0;

    void loop();
    static uint64_t nowNanos();
};
