#pragma once
#include <functional>

class Stratum {
public:
    Stratum();
    ~Stratum();

    bool init();
    void onFrame(std::function<void(float t)> cb);
    void run();

private:
    struct Impl;
    Impl* mImpl;
};
