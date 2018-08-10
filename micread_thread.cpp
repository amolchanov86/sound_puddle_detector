// thread example
#include <iostream>       // std::cout
#include <thread>         // std::thread
#include <chrono>
#include <cstdio>
#include <string>
#include <mutex>
#include <condition_variable>

#include "micread_thread.hpp"

void MicReadAlsa::run() {
    printf("%s: Thread ready ...\n", name_.c_str());
    while(ready_fl_)
    {
        if(!run_fl_){
            printf("%s: Thread is paused ...\n",  name_.c_str());
            // First, waiting until we give permission to start running
            // See explanation of using mutex together with condvar
            // https://github.com/angrave/SystemProgramming/wiki/Synchronization,-Part-5:-Condition-Variables
            std::unique_lock<std::mutex> lck(mtx_);
            cv_.wait(lck);
        }
        printf("%s: Running ...\n", name_.c_str());
        std::this_thread::sleep_for (std::chrono::seconds(1));
    }
    printf("%s: Thread func finished ...\n", name_.c_str());
}


MicReadAlsa::MicReadAlsa(bool delayed_start):
    run_fl_(false),
    ready_fl_(true),
    name_("MicReader")
{
    th_ = std::thread(&MicReadAlsa::run, this);

    if(!delayed_start) {
        start();
    }
}

MicReadAlsa::~MicReadAlsa() {
    if(ready_fl_){
        ready_fl_ = false;
        // Waiting for the thread to finish
        finish();
    }
}


void MicReadAlsa::start() {
    std::unique_lock<std::mutex> lck(mtx_);
    ready_fl_ = true;
    run_fl_ = true;
    cv_.notify_all();
}

void MicReadAlsa::pause() {
    run_fl_ = false;
}

void MicReadAlsa::finish() {
    run_fl_ = false;
    ready_fl_ = false;
    cv_.notify_all();
    printf("%s : Waiting for the thread to finish ...\n", name_.c_str());
    th_.join();
}


