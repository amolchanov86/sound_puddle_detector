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

#include <stdlib.h>
#include <signal.h>

#include "micread_thread.hpp"


bool manual_start=false;
bool record=true;
bool record_only=true;
bool record_csv=true;
MicReadAlsa mic_reader(manual_start, record, record_only, record_csv);

bool run_main_thread;

void signal_handler(int signal)
{
  printf("Caugh CTRL-C. Stopping the threads ...\n");
  mic_reader.finish();
  run_main_thread = false;
}

int main()
{
    run_main_thread = true;

    // Handling ctrl-c
    struct sigaction sigIntHandler;
    sigIntHandler.sa_handler = signal_handler;
    sigemptyset(&sigIntHandler.sa_mask);
    sigIntHandler.sa_flags = 0;
    sigaction(SIGINT, &sigIntHandler, NULL);

    //first par: wait until user call start()
    //second par: record true/false
    //third par: record only, i.e. if record thread should clear the buffer
    mic_reader.start();

    int iterations = 300;

    for(int i=0; i<iterations && run_main_thread; i++){
        std::cout<<"Main thread running:"<<run_main_thread<<std::endl;
        //10ms in my case was sort of optimal, i.e. it spits one frame per step
        std::this_thread::sleep_for (std::chrono::milliseconds(10));
        if(record && !record_only) {
            std::cout << mic_reader.getData();
        }
        std::cout << std::endl;
        std::cout << "Freq: " << mic_reader.estReadFreq() << std::endl << std::flush;
    }

    // Testing pause functionality
//    if(run_main_thread) {
//        printf("Trying to pause thread ...\n");
//        mic_reader.pause();
//        std::this_thread::sleep_for (std::chrono::milliseconds(5000));
//        mic_reader.start();

//        // Testing resuming
//        printf("Trying to resume thread ...\n");
//        for(int i=0; i<iterations && run_main_thread; i++){
//            //10ms in my case was sort of optimal
//            std::this_thread::sleep_for (std::chrono::milliseconds(10));
//            std::cout << mic_reader.getData();
//            std::cout << "Freq: " << mic_reader.estReadFreq() << " FPS (rate):" << mic_reader.estFPS() << std::endl << std::flush;
//        }
//    }

    // One does not have to call finish() since destructor will do the same job
    //mic_reader.finish();
    return 0;
}
