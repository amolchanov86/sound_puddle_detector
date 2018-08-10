//
// Created by artem on 8/10/18.
//
#include <iostream>       // std::cout
#include <thread>         // std::thread
#include <chrono>
#include <cstdio>
#include <string>
#include <mutex>
#include <condition_variable>

#include "micread_thread.hpp"


int main()
{
    MicReadAlsa mic_reader(true);
    mic_reader.start();
    std::this_thread::sleep_for (std::chrono::seconds(2));
    mic_reader.finish();

    return 0;
}