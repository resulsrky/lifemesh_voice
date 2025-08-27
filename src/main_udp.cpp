#include "VoiceEngine.hpp"
#include "UdpTransport.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <cstring>
#include <portaudio.h>

static void listDevices() {
    Pa_Initialize();
    int n = Pa_GetDeviceCount();
    if (n < 0) { std::cerr << "PortAudio device count error\n"; return; }
    std::cout << "=== PortAudio Devices ===\n";
    for (int i=0;i<n;i++) {
        const PaDeviceInfo* d = Pa_GetDeviceInfo(i);
        const PaHostApiInfo* h = Pa_GetHostApiInfo(d->hostApi);
        std::cout << "["<<i<<"] " << (d->name?d->name:"(null)")
                  << "  API=" << (h?h->name:"?")
                  << "  in="<<d->maxInputChannels<<" out="<<d->maxOutputChannels << "\n";
    }
    std::cout << "=========================\n";
    Pa_Terminate();
}

int main(int argc, char** argv){
    if (argc < 4) {
        std::cerr << "Kullanim: " << argv[0]
                  << " <localPort> <remoteIp> <remotePort> [echo] [bypass] [--list] [--in N] [--out M]\n";
        return 1;
    }
    // zorunlu argümanlar
    uint16_t localPort = (uint16_t)std::stoi(argv[1]);
    std::string remoteIp = argv[2];
    uint16_t remotePort = (uint16_t)std::stoi(argv[3]);

    // opsiyonlar
    bool echo = false, bypass = false, listOnly = false;
    int inIdx = -1, outIdx = -1;
    for (int i=4;i<argc;i++){
        if (std::strcmp(argv[i],"echo")==0) echo = true;
        else if (std::strcmp(argv[i],"bypass")==0) bypass = true;
        else if (std::strcmp(argv[i],"--list")==0) listOnly = true;
        else if (std::strcmp(argv[i],"--in")==0 && i+1<argc) inIdx = std::stoi(argv[++i]);
        else if (std::strcmp(argv[i],"--out")==0 && i+1<argc) outIdx = std::stoi(argv[++i]);
    }
    if (listOnly) { listDevices(); return 0; }

    UdpTransport tr(localPort, remoteIp, remotePort);
    if (!tr.start()) { std::cerr<<"UDP start failed\n"; return 1; }

    VoiceEngine ve;
    if (inIdx>=0 || outIdx>=0) ve.setDevices(inIdx, outIdx);
    VoiceParams vp; // opusDtx=false (debug)
    if (!ve.init(vp, &tr, 42)) { std::cerr<<"Voice init failed\n"; return 1; }
    ve.setLocalEcho(echo);
    ve.setBypassVad(bypass);

    std::cout << "PTT/VAD + NS/AGC + Opus. echo="<<echo<<" bypass="<<bypass
              << "  (in="<<inIdx<<", out="<<outIdx<<")\n";

    uint64_t lastTx=0, lastRx=0;
    auto t0 = std::chrono::steady_clock::now();

    while (true) {
        ve.pollOnce();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));

        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - t0).count() >= 1000) {
            uint64_t tx = ve.txFrames();
            uint64_t rx = ve.rxFrames();
            std::cout << "[stats] TX="<<tx<<" (+"<<(tx-lastTx)<<")  RX="<<rx<<" (+"<<(rx-lastRx)<<")\n";
            lastTx = tx; lastRx = rx; t0 = now;
        }
    }
}
