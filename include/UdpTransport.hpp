#pragma once
#include "VoiceEngine.hpp"
#include <atomic>
#include <thread>
#include <string>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

class UdpTransport : public ITransport {
public:
    UdpTransport(uint16_t localPort, const std::string& remoteIp, uint16_t remotePort);

    bool start();
    void stop();

    bool send(const uint8_t* data, size_t len) override;
    void onReceive(RxHandler h) override { rx_ = std::move(h); }

    bool setRemote(const std::string& ip, uint16_t port);

    ~UdpTransport() override { stop(); }
private:
    int fd_ = -1;
    std::atomic<bool> running_{false};
    std::thread rxThread_;
    RxHandler rx_;

    ::sockaddr_in remote_{};
    uint16_t localPort_;

    bool openSocket(uint16_t localPort);
    void rxLoop();
};
