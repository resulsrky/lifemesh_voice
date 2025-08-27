#include "VoiceEngine.hpp"
#include "UdpTransport.hpp"
#include <iostream>
#include <thread>
#include <chrono>

int main(int argc, char** argv){
    if (argc != 4) {
        std::cerr << "Kullanim: " << argv[0] << " <localPort> <remoteIp> <remotePort>\n";
        return 1;
    }
    uint16_t localPort = (uint16_t)std::stoi(argv[1]);
    std::string remoteIp = argv[2];
    uint16_t remotePort = (uint16_t)std::stoi(argv[3]);

    UdpTransport tr(localPort, remoteIp, remotePort);
    if (!tr.start()) { std::cerr<<"UDP start failed\n"; return 1; }

    VoiceEngine ve;
    VoiceParams vp; // 16k/20ms/12kbps, FEC+DTX açık
    if (!ve.init(vp, &tr, 42)) { std::cerr<<"Voice init failed\n"; return 1; }

    std::cout << "Konuş: UDP unicast üzerinde PTT/VAD + NS/AGC + Opus.\n";
    while (true) {
        ve.pollOnce();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}
