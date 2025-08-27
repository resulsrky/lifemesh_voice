#include "VoiceEngine.hpp"
#include <random>
#include <deque>
#include <chrono>
#include <thread>
#include <iostream>

class DummyTransport : public ITransport {
public:
    bool send(const uint8_t* data, size_t len) override {
        if (rand01() < 0.10) return true; // %10 kayıp
        int dly = randMs(0,120);          // 0–120 ms jitter
        queue_.push_back({std::chrono::steady_clock::now() + std::chrono::milliseconds(dly),
                          std::vector<uint8_t>(data, data+len)});
        return true;
    }
    void onReceive(RxHandler h) override { rx_ = std::move(h); }

    void pump(){
        auto now = std::chrono::steady_clock::now();
        while (!queue_.empty() && queue_.front().t <= now) {
            auto pkt = std::move(queue_.front().buf); queue_.pop_front();
            if (rx_) rx_(pkt.data(), pkt.size());
        }
    }
private:
    struct Item{ std::chrono::steady_clock::time_point t; std::vector<uint8_t> buf; };
    std::deque<Item> queue_;
    RxHandler rx_;

    static double rand01(){ static std::mt19937 r{std::random_device{}()}; static std::uniform_real_distribution<>u(0,1); return u(r); }
    static int randMs(int a,int b){ static std::mt19937 r{std::random_device{}()}; std::uniform_int_distribution<>u(a,b); return u(r); }
};

int main(){
    DummyTransport tr;
    VoiceEngine ve;
    VoiceParams vp; // 16k/20ms/12kbps FEC+DTX
    if (!ve.init(vp, &tr, /*convId*/ 42)) { std::cerr<<"init failed\n"; return 1; }

    std::cout << "Konuş ve %10 kayıp + jitter altında loopback dinle.\n";
    for (;;) {
        ve.pollOnce();
        tr.pump();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return 0;
}
