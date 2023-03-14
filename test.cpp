
#include "Timer.h"
#include <cstdio>

void foo() {
    printf("> call per second!\n");
}

void bar() {
    printf("< call every two seconds\n");
}

int main() {
    using namespace std::chrono_literals;
    POC::TimerManager::getInstance()->addTimer(new POC::Timer(1000ms, 0ms, foo));
    POC::TimerManager::getInstance()->addTimer(new POC::Timer(2000ms, 0ms, bar));
    while (true) {;}
    return 0;
}

