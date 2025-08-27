#include "RttProbe.hpp"
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <unistd.h>
#include <cstring>
#include <chrono>

RttProbe::RttProbe(const std::string& ip, uint16_t port,
                   const std::string& localIp, uint16_t localPort) {
    std::memset(&remote_,0,sizeof(remote_));
    remote_.sin_family = AF_INET;
    remote_.sin_port = htons(port);
    inet_pton(AF_INET, ip.c_str(), &remote_.sin_addr);

    std::memset(&local_,0,sizeof(local_));
    local_.sin_family = AF_INET;
    local_.sin_port = htons(localPort);
    inet_pton(AF_INET, localIp.c_str(), &local_.sin_addr);
}

RttProbe::~RttProbe(){ stop(); }

bool RttProbe::start(){
    if (running_) return true;
    fd_ = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd_ < 0) return false;
    int yes=1; setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
#ifdef SO_REUSEPORT
    setsockopt(fd_, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof(yes));
#endif
    if (bind(fd_, (sockaddr*)&local_, sizeof(local_)) < 0) { close(fd_); fd_=-1; return false; }
    running_ = true;
    th_ = std::thread(&RttProbe::loop, this);
    return true;
}

void RttProbe::stop(){
    if (!running_) return;
    running_ = false;
    if (th_.joinable()) th_.join();
    if (fd_!=-1){ close(fd_); fd_=-1; }
}

uint64_t RttProbe::nowNanos(){
    using namespace std::chrono;
    return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
}

void RttProbe::loop(){
    constexpr long NANOS_PER_MS = 1'000'000;
    uint8_t buf[PACKET_SIZE]{};
    while (running_){
        // PING
        std::memset(buf,0,sizeof(buf));
        uint8_t* p = buf;
        auto put32=[&](uint32_t v){ uint32_t b=htonl(v); std::memcpy(p,&b,4); p+=4; };
        auto put16=[&](uint16_t v){ uint16_t b=htons(v); std::memcpy(p,&b,2); p+=2; };
        auto put8 =[&](uint8_t v){ *p++ = v; };
        auto put64=[&](uint64_t v){
            uint32_t hi = htonl((uint32_t)(v>>32));
            uint32_t lo = htonl((uint32_t)(v & 0xffffffffULL));
            std::memcpy(p,&hi,4); p+=4;
            std::memcpy(p,&lo,4); p+=4;
        };

        ++seq_;
        uint64_t t0 = nowNanos();
        p=buf;
        put32(MAGIC);
        put8(VERSION);
        put8(MSG_PING);
        put16(0);
        put32(0);
        put32(seq_);
        put64(t0);
        sendto(fd_, buf, PACKET_SIZE, 0, (sockaddr*)&remote_, sizeof(remote_));

        // 200 ms ECHO bekle
        fd_set rfds; FD_ZERO(&rfds); FD_SET(fd_, &rfds);
        timeval tv{0, 200*1000};
        int r = select(fd_+1, &rfds, nullptr, nullptr, &tv);
        if (r > 0 && FD_ISSET(fd_, &rfds)){
            sockaddr_in src{}; socklen_t sl=sizeof(src);
            int n = recvfrom(fd_, buf, PACKET_SIZE, 0, (sockaddr*)&src, &sl);
            if (n == (int)PACKET_SIZE){
                const uint8_t* q = buf;
                auto get32=[&](){ uint32_t v; std::memcpy(&v,q,4); q+=4; return ntohl(v); };
                auto get16=[&](){ uint16_t v; std::memcpy(&v,q,2); q+=2; return ntohs(v); };
                auto get8 =[&](){ return *q++; };
                auto get64=[&](){
                    uint32_t hi,lo; std::memcpy(&hi,q,4); q+=4; std::memcpy(&lo,q,4); q+=4;
                    hi = ntohl(hi); lo = ntohl(lo);
                    return (uint64_t(hi)<<32) | lo;
                };
                uint32_t magic = get32(); if (magic!=MAGIC) goto sleep_and_continue;
                (void)get8(); uint8_t type = get8(); (void)get16();
                (void)get32(); uint32_t seq = get32();
                uint64_t ts  = get64();
                if (type==MSG_ECHO && seq==seq_){
                    double rtt_ms = double(nowNanos() - ts) / double(NANOS_PER_MS);
                    double prev = rtt_ewma_ms_.load();
                    if (prev < 0) rtt_ewma_ms_.store(rtt_ms);
                    else rtt_ewma_ms_.store(0.2*rtt_ms + 0.8*prev);
                }
            }
        }
sleep_and_continue:
        for (int i=0;i<100 && running_;++i) usleep(10'000); // 1s toplam
    }
}
