#pragma once
#include <atomic>
#include <thread>
#include <netinet/in.h>
#include <cstdint>

class RttEchoServer {
public:
    explicit RttEchoServer(uint16_t port);
    ~RttEchoServer();
    bool start();
    void stop();
private:
    int fd_=-1;
    sockaddr_in local_{};
    std::atomic<bool> running_{false};
    std::thread th_;
    void loop();
};
