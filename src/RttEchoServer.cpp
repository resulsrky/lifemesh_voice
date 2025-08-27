#include "RttEchoServer.hpp"
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cstring>

namespace {
    constexpr uint32_t MAGIC = 0xABCD1357;
    constexpr uint8_t  VERSION=1;
    constexpr uint8_t  PING=1, ECHO=2;
    constexpr size_t   PACKET_SIZE=64;
}

RttEchoServer::RttEchoServer(uint16_t port){
    std::memset(&local_,0,sizeof(local_));
    local_.sin_family=AF_INET;
    local_.sin_port=htons(port);
    local_.sin_addr.s_addr=htonl(INADDR_ANY);
}
RttEchoServer::~RttEchoServer(){ stop(); }

bool RttEchoServer::start(){
    if (running_) return true;
    fd_=::socket(AF_INET,SOCK_DGRAM,0);
    if (fd_<0) return false;
    int yes=1; setsockopt(fd_,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof(yes));
#ifdef SO_REUSEPORT
    setsockopt(fd_,SOL_SOCKET,SO_REUSEPORT,&yes,sizeof(yes));
#endif
    if (bind(fd_,(sockaddr*)&local_,sizeof(local_))<0){ close(fd_); fd_=-1; return false; }
    running_=true;
    th_=std::thread(&RttEchoServer::loop,this);
    return true;
}
void RttEchoServer::stop(){
    if (!running_) return;
    running_=false;
    if (th_.joinable()) th_.join();
    if (fd_!=-1){ close(fd_); fd_=-1; }
}
void RttEchoServer::loop(){
    uint8_t buf[PACKET_SIZE];
    while (running_){
        sockaddr_in src{}; socklen_t sl=sizeof(src);
        int n = recvfrom(fd_,buf,sizeof(buf),0,(sockaddr*)&src,&sl);
        if (n==(int)sizeof(buf)){
            if (*(uint32_t*)buf == htonl(MAGIC)){
                buf[5]=ECHO; // 4(MAGIC)+1(VERSION)
                sendto(fd_,buf,sizeof(buf),0,(sockaddr*)&src,sl);
            }
        }
    }
}
