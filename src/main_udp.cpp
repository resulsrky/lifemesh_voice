#include "VoiceEngine.hpp"
#include "UdpTransport.hpp"
#include <iostream>
#include <thread>
#include <chrono>

int main(int argc, char** argv){
    if (argc < 4) {
        std::cerr << "Kullanim: " << argv[0] << " <localPort> <remoteIp> <remotePort> [echo] [bypass]\n";
        std::cerr << "  echo   : local paketi kendine de cal (tek surecte duyarsin)\n";
        std::cerr << "  bypass : VAD'i baypas et, surekli gonder (debug)\n";
        return 1;
    }
    bool echo = (argc >= 5);
    bool bypass = (argc >= 6);

    uint16_t localPort = (uint16_t)std::stoi(argv[1]);
    std::string remoteIp = argv[2];
    uint16_t remotePort = (uint16_t)std::stoi(argv[3]);

    UdpTransport tr(localPort, remoteIp, remotePort);
    if (!tr.start()) { std::cerr<<"UDP start failed\n"; return 1; }

    VoiceEngine ve;
    VoiceParams vp; // opusDtx=false (debug); istersen true yap
    if (!ve.init(vp, &tr, 42)) { std::cerr<<"Voice init failed\n"; return 1; }
    ve.setLocalEcho(echo);
    ve.setBypassVad(bypass);

    std::cout << "PTT/VAD + NS/AGC + Opus. echo="<<echo<<" bypass="<<bypass<<"\n";
    while (true) {
        ve.pollOnce();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}
