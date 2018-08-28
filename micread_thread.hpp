/*

Class encapsulating reading from a microphone using ALSA.
Author: Artem Molchanov (08/2018)
Email: a.molchanov@swerve.ai
A minimal example:
int main()
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

---Useful information:
See more about ALSA programming here: https://www.linuxjournal.com/article/6735

--- TODO:
- bug: getData() should only erase data already recorded if recording is happening
- little->big endian should happen before I put data in the micDataStamped container
 */

#ifndef MIC_READ_THREAD_MICREAD_THREAD_HPP
#define MIC_READ_THREAD_MICREAD_THREAD_HPP

#include <iostream>
#include <cstdio>
#include <string>
#include <inttypes.h>
#include <vector>
#include <deque>

// Thread handling
#include <chrono>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <numeric>

#include <alsa/asoundlib.h>

// Buffer size in terms of frames.
// Smaller buffers resulted in the same millisecond time stamp
#define MICREAD_DEF_BUF_SIZE 512
#define MICREAD_DEF_RATE 44100 //44100
//#define MICREAD_DEF_BPS 16 //8
#define MICREAD_DEF_DEVICE "hw:2,0"
//#define MICREAD_DEF_DEVICE "default"
#define MICREAD_DEF_NAME "MicRead"
#define MICREAD_DEF_REC_FILENAME "rec_mic"
#define MICREAD_DEF_REC_FREQ 100

//---SND_PCM_FORMAT options:
//SND_PCM_FORMAT_U8:
//SND_PCM_FORMAT_S16_LE:
//SND_PCM_FORMAT_S32_LE:
//SND_PCM_FORMAT_S24_LE:
//SND_PCM_FORMAT_S24_3LE:

struct micDataStamped
{
    micDataStamped(){
        id = 0;
        timestamp = 0;
        flags.all = 0;
    }
    union {
        uint8_t all; //summary of all flags (i.e. a byte containing them all)
        uint8_t recorded:1;
    } flags;
    long int id; //counter of the chunk
    int64_t timestamp; //milliseconds time stamp
    std::vector<int16_t> frames; //mic data itself
};

class MicReadAlsa
{
public:
    MicReadAlsa(std::chrono::steady_clock::time_point t_start,
                bool manual_start=false,
                bool record=true,
                bool record_only=true,
                bool record_csv=true,
                float record_freq=MICREAD_DEF_REC_FREQ,
                std::string filename_base=MICREAD_DEF_REC_FILENAME,
                std::string device=MICREAD_DEF_DEVICE,
                int buffer_frames_num=MICREAD_DEF_BUF_SIZE,
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
    // The function locks the mutex, moves the recorded data and clears the main buffer
    // If record is true it only copies and clears unrecorded data
    // thus if the recording thread is hanging you may get nothing
    // If record is false it just moves all the data.
    // If record_only is true then the record thread calls this function and clears the data
    std::vector<micDataStamped> getData();
    double estReadFreq() const {return std::accumulate( read_freq_estimates.begin(), read_freq_estimates.end(), 0.0)/read_freq_estimates.size();} //Frequency of data reading
    double estFPS() const {return std::accumulate( read_fps_estimates.begin(), read_fps_estimates.end(), 0.0)/read_fps_estimates.size();} //Frames per Second estimate

    std::deque<micDataStamped> data; //Direct data access - unsafe. Better use getData() !!!

    // Frame counters
    long getChunksRead() const; //num of frames received from the device
    long getChunksRecorded() const; //num of frames recorded from the device

    //--- Device handling
    //If constructor fails to open the device, use this function manually
    int openDevice(std::string device=MICREAD_DEF_DEVICE,
                   int buffer_frames=MICREAD_DEF_BUF_SIZE,
                   unsigned int rate=MICREAD_DEF_RATE);

    // Sets the recording freq through calculating a delay
    void setRecFreq(float rec_freq){
        rec_delay_ = (long) 1./ rec_freq * 1000; //ms
    }

    // Get measured recording freq
    float estRecFreq() const {return std::accumulate( rec_freq_estimates.begin(), rec_freq_estimates.end(), 0.0)/rec_freq_estimates.size();}


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
    double freq_; // estimated frequency of the reading thread
    double rec_freq_estimate_;
    std::deque<float> rec_freq_estimates;
    std::deque<float> read_freq_estimates;
    std::deque<float> read_fps_estimates;
    int max_est_size_;
    double fps_est_;
    bool record_only_;
    bool record_;
    bool record_csv_;

    long chunks_read_; //how many frames we received from the device
    long chunks_recorded_; //how many frames we actually recorded
    std::chrono::steady_clock::time_point t_start_;

    bool openFiles();//Opens files that we are recording into
    std::string filename_base_;//We will modify this base to record csv and wav files

    // This function copies only unrecorded data and marks the data as recorded
    // It is also safe since it lock the thread
    std::vector<micDataStamped> copyUnrecordedData();
    long rec_delay_;

    // The copyData() is inherently unsafe to use: it locks the mutex, copies the data.
    // Beware it does not clean anything.
    // Unsafety reasons:
    // - when you use getData it will clear the buffer. Thus copyData may never see portions of the data.
    // - that is the reason I introduced copyUnrecordedData() for the recording thread. To only erase what we checked out.
    std::deque<micDataStamped> copyData();
    // This function is unsafe for similar reasons
    std::deque<micDataStamped> moveData();
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
