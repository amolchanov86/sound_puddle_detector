// The class for easy reading from the microphone input
//

#ifndef MIC_READ_THREAD_MICREAD_THREAD_HPP
#define MIC_READ_THREAD_MICREAD_THREAD_HPP

#include <iostream>
#include <cstdio>
#include <string>

// Thread handling
#include <chrono>
#include <thread>
#include <mutex>
#include <condition_variable>

// ALSA
#include <alsa/asoundlib.h>

class MicReadAlsa
{
    std::mutex mtx_;
    std::condition_variable cv_;

    bool run_fl_;
    bool ready_fl_;
    std::string name_;
    std::thread th_;

    int buffer_frames_; //128 default
    unsigned int rate_; //44100 default

    // ALSA stuff
    char *buffer_;
    snd_pcm_t *capture_handle_;
    std::string device_;
    snd_pcm_hw_params_t *hw_params_;
    snd_pcm_format_t format_;

    void run();

public:
    MicReadAlsa(
        bool delayed_start=false,
        std::string device="hw:0,0",
        int buffer_frames=128,
        unsigned int rate=44100);
    ~MicReadAlsa();

    int openDevice();
    void start();
    void pause();
    void finish();
    bool isRunning() const {return run_fl_;}

};

#endif //MIC_READ_THREAD_MICREAD_THREAD_HPP
