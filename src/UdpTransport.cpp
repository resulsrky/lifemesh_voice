#include "UdpTransport.hpp"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <iostream>
#include <chrono>
#include <vector>

static bool set_nonblock(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return false;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

UdpTransport::UdpTransport(uint16_t localPort, const std::string& remoteIp, uint16_t remotePort)
: localPort_(localPort) {
    std::memset(&remote_, 0, sizeof(remote_));
    remote_.sin_family = AF_INET;
    remote_.sin_port   = htons(remotePort);
    inet_pton(AF_INET, remoteIp.c_str(), &remote_.sin_addr);
}

bool UdpTransport::openSocket(uint16_t localPort) {
    fd_ = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd_ < 0) { perror("socket"); return false; }

    int rcv = 1<<20, snd = 1<<20;
    setsockopt(fd_, SOL_SOCKET, SO_RCVBUF, &rcv, sizeof(rcv));
    setsockopt(fd_, SOL_SOCKET, SO_SNDBUF, &snd, sizeof(snd));

    int yes = 1;
    setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    int tos = 46 << 2; // DSCP EF -> TOS
    setsockopt(fd_, IPPROTO_IP, IP_TOS, &tos, sizeof(tos));

    sockaddr_in local{};
    local.sin_family      = AF_INET;
    local.sin_port        = htons(localPort);
    local.sin_addr.s_addr = INADDR_ANY;

    if (bind(fd_, (sockaddr*)&local, sizeof(local)) < 0) {
        perror("bind"); close(fd_); fd_ = -1; return false;
    }

    set_nonblock(fd_);
    return true;
}

bool UdpTransport::start() {
    if (fd_ == -1 && !openSocket(localPort_)) return false;
    running_ = true;
    rxThread_ = std::thread(&UdpTransport::rxLoop, this);
    return true;
}

void UdpTransport::stop() {
    if (!running_) return;
    running_ = false;
    if (rxThread_.joinable()) rxThread_.join();
    if (fd_ != -1) { ::close(fd_); fd_ = -1; }
}

bool UdpTransport::send(const uint8_t* data, size_t len) {
    if (fd_ < 0) return false;
    if (len > 1400) {
        std::cerr << "[UdpTransport] WARNING: oversize UDP packet " << len << " bytes\n";
    }
    ssize_t n = ::sendto(fd_, data, len, 0, (sockaddr*)&remote_, sizeof(remote_));
    return n == (ssize_t)len;
}

bool UdpTransport::setRemote(const std::string& ip, uint16_t port) {
    sockaddr_in r{}; r.sin_family = AF_INET; r.sin_port = htons(port);
    if (inet_pton(AF_INET, ip.c_str(), &r.sin_addr) != 1) return false;
    remote_ = r; return true;
}

void UdpTransport::rxLoop() {
    using namespace std::chrono_literals;
    constexpr size_t MAX = 2048;
    std::vector<uint8_t> buf(MAX);

    while (running_) {
        fd_set rfds; FD_ZERO(&rfds); FD_SET(fd_, &rfds);
        timeval tv{0, 200*1000}; // 200ms
        int r = select(fd_+1, &rfds, nullptr, nullptr, &tv);
        if (r <= 0) continue;

        if (FD_ISSET(fd_, &rfds)) {
            sockaddr_in src{}; socklen_t sl = sizeof(src);
            ssize_t n = recvfrom(fd_, buf.data(), buf.size(), 0, (sockaddr*)&src, &sl);
            if (n > 0) {
                if (rx_) rx_(buf.data(), (size_t)n);
            }
        }
    }
}
