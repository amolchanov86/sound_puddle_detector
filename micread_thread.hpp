/*
 * Class encapsulating reading from a microphone using ALSA.
 * Author: Artem Molchanov (08/2018)
 * Email: a.molchanov@swerve.ai
 *
 *
 * A minimal example:
 * int main()
{
    MicReadAlsa mic_reader(true); //The first parameter (flag) means that we will wait until user calls start() function
    mic_reader.start();

    int iterations = 10;

    for(int i=0; i<iterations; i++){
        //10ms in my case was sort of optimal, i.e. it spits one frame per step
        std::this_thread::sleep_for (std::chrono::milliseconds(10));
        std::cout << mic_reader.getData();
        std::cout << std::flush;
    }
    return 0;
}
 */

#ifndef MIC_READ_THREAD_MICREAD_THREAD_HPP
#define MIC_READ_THREAD_MICREAD_THREAD_HPP

#include <iostream>
#include <cstdio>
#include <string>
#include <inttypes.h>
#include <vector>

// Thread handling
#include <chrono>
#include <thread>
#include <mutex>
#include <condition_variable>

#include <alsa/asoundlib.h>


#define MICREAD_DEF_FRAME_SIZE 512 //Smaller buffers resulted in the same millisecond time stamp
#define MICREAD_DEF_RATE 44100 //44100
//#define MICREAD_DEF_BPS 16 //8
//#define MICREAD_DEF_DEVICE "hw:0,0"
#define MICREAD_DEF_DEVICE "default"
#define MICREAD_DEF_NAME "MicRead"
#define MICREAD_DEF_REC_FILENAME "rec_mic"


//---SND_PCM_FORMAT options:
//SND_PCM_FORMAT_U8:
//SND_PCM_FORMAT_S16_LE:
//SND_PCM_FORMAT_S32_LE:
//SND_PCM_FORMAT_S24_LE:
//SND_PCM_FORMAT_S24_3LE:

struct micDataStamped
{
    int64_t timestamp; //milliseconds time stamp
    std::vector<int16_t> frame; //mic data itself
};

class MicReadAlsa
{
public:
    MicReadAlsa(bool manual_start=false,
                bool record=true,
                bool record_only=true,
                std::string filename_base=MICREAD_DEF_REC_FILENAME,
                std::string device=MICREAD_DEF_DEVICE,
                int buffer_frames=MICREAD_DEF_FRAME_SIZE,
                unsigned int rate=MICREAD_DEF_RATE,
                int channels=1,
                snd_pcm_format_t format=SND_PCM_FORMAT_S16_LE,
                std::string name=MICREAD_DEF_NAME);
    /// \param manual_start  if you don't want automatic start set to True and use start() later
    /// \param record_only  if set True the recording thread will clear the buffer automatically
    /// \param channels ONLY 1 CHANNEL SUPPORTED. Parameter left for future extensions

    ~MicReadAlsa();


    //--- Thread handling
    void start(); //Starts the reading thread (if delayed_start is not set in the constructor the threads starts automatically)
    void pause(); //Pauses the thread. Use start() to restart it
    void finish(); //Closes the thread completely
    bool isRunning() const {return run_fl_;} //checks if the thread is still running

    //--- Data handling
    // The function locks the mutex, moves the data and clears the main buffer
    // If record_only is set then the record thread calls this function
    std::vector<micDataStamped> getData();
    // The function locks the mutex, copies the data
    std::vector<micDataStamped> copyData();//
    double getFreq() const {return freq_;} //Frequency of data reading

    std::vector<micDataStamped> data; //Direct data access - unsafe. Better use getData() !!!

    //--- Device handling
    //If constructor fails to open the device, use this function manually
    int openDevice(std::string device=MICREAD_DEF_DEVICE,
                   int buffer_frames=MICREAD_DEF_FRAME_SIZE,
                   unsigned int rate=MICREAD_DEF_RATE);

protected:
    std::mutex mtx_; //thread pause mutex
    std::condition_variable cv_;
    std::mutex data_mtx_;

    bool run_fl_; //pause flag
    bool ready_fl_; //thread alive flag (not exited)
    std::string name_; //object name (for messaging)
    std::thread th_; //reading thread
    std::thread th_rec_;//recording thread

    int buffer_frames_; //128 default
    unsigned int rate_; //44100 default

    // ALSA stuff
    int8_t *buffer_; //temporary data buffer for ALSA
    snd_pcm_t *capture_handle_;
    std::string device_; //Devices are in the format: "hw:X,Y", where X - card #, Y - device #. Both are int
    snd_pcm_hw_params_t *hw_params_;
    snd_pcm_format_t format_;

    // Wav stuff
    int channels_;
    int bits_per_sample_;

    // Thread stuff
    void run(); //Thread functions
    void record_thread();
    double freq_;
    bool record_only_;
    bool record_;

    bool openFiles();//Opens files that we are recording into
    std::string filename_base_;//We will modify this base to record csv and wav files

};

template <typename T>
std::ostream& operator<<(std::ostream& os, const std::vector<T>& data){
    for(int i=0; i<data.size(); i++) {
        std::cout<<(int)data[i]<<" ";
    }
    return os;
}

std::ostream& operator<<(std::ostream& os, const std::vector<micDataStamped>& data);
std::ostream& operator<<(std::ostream& os, const micDataStamped& data);


#endif //MIC_READ_THREAD_MICREAD_THREAD_HPP
