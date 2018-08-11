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

    int iterations = 10;

    for(int i=0; i<iterations; i++){
        //10ms in my case was sort of optimal
        std::this_thread::sleep_for (std::chrono::milliseconds(10));
        std::cout << mic_reader.getData();
        std::cout << std::flush;
    }

    // Testing pause functionality
    printf("Trying to pause thread ...\n");
    mic_reader.pause();
    std::this_thread::sleep_for (std::chrono::milliseconds(1000));
    mic_reader.start();

    // Testing resuming
    printf("Trying to resume thread ...\n");
    for(int i=0; i<iterations; i++){
        //10ms in my case was sort of optimal
        std::this_thread::sleep_for (std::chrono::milliseconds(10));
        std::cout << mic_reader.getData();
        std::cout << std::flush;
    }


    // One does not have to call finish() since destructor will do the same job
    //mic_reader.finish();
    return 0;
}