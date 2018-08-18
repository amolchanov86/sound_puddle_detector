#include "micread_thread.hpp"

#include <fstream>
#include <iostream>
#include <climits>

MicReadAlsa::MicReadAlsa(bool manual_start,
                         bool record,
                         bool record_only,
                         std::string filename_base,
                         std::string device,
                         int buffer_frames,
                         unsigned int rate,
                         int channels,
                         snd_pcm_format_t format,
                         std::string name):
    run_fl_(false),
    ready_fl_(true),
    name_(name),
    buffer_frames_(buffer_frames),
    rate_(rate),
    format_(format),
    device_(device),
    buffer_(nullptr),
    freq_(0.0),
    filename_base_(filename_base),
    channels_(channels),
    record_only_(record_only),
    record_(record)
{

    bits_per_sample_ = snd_pcm_format_width(format_);
    if(openDevice() < 0){
        fprintf(stderr,"%s: ERROR: Failed to open device %s. Please openDevice() manually and start() the thread ...\n",
        name_.c_str(),
        device_.c_str());

        ready_fl_ = false;
    }
    th_ = std::thread(&MicReadAlsa::run, this);
    if(record) {
        th_rec_ = std::thread(&MicReadAlsa::record_thread, this);
    }

    if(!manual_start) {
        start();
    }

}

MicReadAlsa::~MicReadAlsa() {
    if(ready_fl_){
        ready_fl_ = false;
        // Waiting for the thread to finish
        finish();
    }
    if(buffer_ != nullptr){
        delete[] buffer_;
        buffer_ = nullptr;
    }
}


//template <typename T>
//T swap_endian(T u, size=sizeof(T))
//{
//    static_assert (CHAR_BIT == 8, "CHAR_BIT != 8");

//    union
//    {
//        T u;
//        unsigned char u8[size];
//    } source, dest;

//    source.u = u;

//    for (size_t k = 0; k < size; k++)
//        dest.u8[k] = source.u8[size - k - 1];

//    return dest.u;
//}

int8_t* swap_endian(int8_t* value, int size)
{
  int8_t buf;
  for (int i=0; i<size/2; i++) {
      buf = value[i];
      value[i] = value[size-i-1];
      value[size-i-1] = buf;
  }
}

void MicReadAlsa::run() {
    int err; //Reporting ALSA errors

    buffer_ = new int8_t[buffer_frames_ * snd_pcm_format_width(format_) * channels_/ 8];

    //Time to measure freq
    auto time_prev = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    );
    long int frames_since_last = 0;

    printf("%s: Reading Thread ready ...\n", name_.c_str());

    while(ready_fl_)
    {
        // Pause functionality
        if(!run_fl_){
            int pause_err = snd_pcm_pause(capture_handle_, 1);
            if(pause_err){
                fprintf(stderr, "%s: WARNING: Failed to pause device %s: %s \n",
                       name_.c_str(),
                       device_.c_str(),
                       snd_strerror(pause_err));
            }
            printf("%s: Thread is paused ...\n",  name_.c_str());
            // First, waiting until we give permission to start running
            // See explanation of using mutex together with condvar
            // https://github.com/angrave/SystemProgramming/wiki/Synchronization,-Part-5:-Condition-Variables
            std::unique_lock<std::mutex> lck(mtx_);
            cv_.wait(lck);
        }

        // ALSA
        if ((err = snd_pcm_readi(capture_handle_, buffer_, buffer_frames_)) != buffer_frames_)
        {
            fprintf(stderr, "%s: ERROR: Read from audio interface failed (%s)\n",
                    name_.c_str(),
                    snd_strerror(err));
        }
        // Copy data to my buffer
        else {
            if(data_mtx_.try_lock())
            {
                micDataStamped frame_stamped;

                //Creating a timestamp
                auto time = std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::system_clock::now().time_since_epoch()
                );
                long timestamp = time.count(); //count milliseconds
                frame_stamped.timestamp = timestamp;

                //Copying data from the temporary buffer
//                int bytes_per_sample = bits_per_sample_ / 8;
//                for (int i = 0; i < buffer_frames_ * channels_ * bytes_per_sample; i++)
//                {
//                    frame_stamped.frame.push_back(buffer_[i]);
//                }

                //A bit more general version, but still for a single channel
                int i_incr = channels_* bits_per_sample_ / 8;
                for (int i = 0; i < buffer_frames_ * i_incr; i+=i_incr)
                {
//                    swap_endian(buffer_ + i, bits_per_sample_ / 8);
                    auto val_ptr = (uint16_t *) (buffer_ + i);
                    frame_stamped.frame.push_back(*val_ptr);
                }

                //Calculating freq
                freq_ = (double)buffer_frames_ / (double)(time - time_prev).count() * 1000000.0;
                time_prev = time;

                //Push data to the main data buffer
                data.push_back(frame_stamped);

                data_mtx_.unlock();
            }
        }

    }

    // Cleaning, closing
    delete[] buffer_;
    buffer_ = nullptr;

    snd_pcm_drain(capture_handle_);
    snd_pcm_close (capture_handle_);
    fprintf(stdout, "%s: Audio interface closed\n", name_.c_str());

    printf("%s: Thread func finished ...\n", name_.c_str());
}

void MicReadAlsa::start() {
    std::unique_lock<std::mutex> lck(mtx_);
    ready_fl_ = true;
    run_fl_ = true;
    int err = snd_pcm_pause(capture_handle_, 0);
    if(err){
        fprintf(stderr, "%s: WARNING: Failed to resume device %s: %s\n",
               name_.c_str(),
               device_.c_str(),
               snd_strerror(err));
    }
    cv_.notify_all();
}

void MicReadAlsa::pause() {
    if(ready_fl_) {
        std::unique_lock<std::mutex> lck(mtx_);
        run_fl_ = false;
    }
}

void MicReadAlsa::finish() {
    cv_.notify_all();
    ready_fl_ = false;
//    run_fl_ = false;
    printf("%s : Waiting for the reading thread to finish ...\n", name_.c_str());
    th_.join();
    if(record_){
        printf("%s : Waiting for the recording thread to finish ...\n", name_.c_str());
        th_rec_.join();
    }
}

int MicReadAlsa::openDevice(std::string device,
                            int buffer_frames,
                            unsigned int rate) {

    bool restart = false;
    if (run_fl_) {
        pause();
        restart = true;
    }

    int i;
    int err;

    device_ = device;
    rate_ = rate;
    buffer_frames_ = buffer_frames;

    if ((err = snd_pcm_open (&capture_handle_, device_.c_str(), SND_PCM_STREAM_CAPTURE, 0)) < 0) {
        fprintf (stderr, "%s: ERROR: cannot open audio device %s (%s)\n",
                 name_.c_str(),
                 device_.c_str(),
                 snd_strerror (err));
        return -1;
    }
    fprintf(stdout, "%s: Audio interface opened\n", name_.c_str());


    if ((err = snd_pcm_hw_params_malloc (&hw_params_)) < 0) {
        fprintf (stderr, "%s: ERROR: cannot allocate hardware parameter structure (%s)\n",
                 name_.c_str(),
                 snd_strerror (err));
        return -2;
    }
    fprintf(stdout, "%s: hw_params allocated\n", name_.c_str());


    if ((err = snd_pcm_hw_params_any (capture_handle_, hw_params_)) < 0) {
        fprintf (stderr, "%s: ERROR: Cannot initialize hardware parameter structure (%s)\n",
                 name_.c_str(),
                 snd_strerror (err));
        return -3;
    }
    fprintf(stdout, "%s: hw_params initialized\n", name_.c_str());


    if ((err = snd_pcm_hw_params_set_access (capture_handle_, hw_params_, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0) {
        fprintf (stderr, "%s: ERROR: cannot set access type (%s)\n",
                 name_.c_str(),
                 snd_strerror (err));
        return -4;
    }
    fprintf(stdout, "%s: hw_params access setted\n", name_.c_str());


    if ((err = snd_pcm_hw_params_set_format (capture_handle_, hw_params_, format_)) < 0) {
        fprintf (stderr, "%s: ERROR: Cannot set sample format (%s)\n",
                 name_.c_str(),
                 snd_strerror (err));
        return -5;
    }
    fprintf(stdout, "%s: hw_params format setted\n", name_.c_str());


    if ((err = snd_pcm_hw_params_set_rate_near (capture_handle_, hw_params_, &rate_, 0)) < 0) {
        fprintf (stderr, "%s: ERROR: Cannot set sample rate (%s)\n",
                 name_.c_str(),
                 snd_strerror (err));
        return -6;
    }
    fprintf(stdout, "%s: hw_params rate setted\n", name_.c_str());

    if ((err = snd_pcm_hw_params_set_channels (capture_handle_, hw_params_, channels_)) < 0) {
        fprintf (stderr, "%s: ERROR: cannot set channel count (%s)\n",
                 name_.c_str(),
                 snd_strerror (err));
        return -7;
    }
    fprintf(stdout, "%s: hw_params channels setted\n", name_.c_str());


    if ((err = snd_pcm_hw_params (capture_handle_, hw_params_)) < 0) {
        fprintf (stderr, "%s: ERROR: Cannot set parameters (%s)\n",
                 name_.c_str(),
                 snd_strerror (err));
        return -8;
    }
    fprintf(stdout, "%s: hw_params setted\n", name_.c_str());


    snd_pcm_hw_params_free (hw_params_);
    fprintf(stdout, "%s: hw_params freed\n", name_.c_str());


    if ((err = snd_pcm_prepare (capture_handle_)) < 0) {
        fprintf (stderr, "%s: ERROR: Cannot prepare audio interface for use (%s)\n",
                 name_.c_str(),
                 snd_strerror (err));
        return -9;
    }
    fprintf(stdout, "%s: Audio interface prepared\n", name_.c_str());

    if(restart){
        start();
    }
    return 0;
}

std::vector<micDataStamped> MicReadAlsa::getData(){
    data_mtx_.lock();
    std::vector<micDataStamped> data_temp = std::move(data); //After moving the data vector should be empty
    data_mtx_.unlock();
    return data_temp; //Theoretically should return by rval since C11 to avoid copying
}

std::vector<micDataStamped> MicReadAlsa::copyData(){
    data_mtx_.lock();
    std::vector<micDataStamped> data_temp = data; //Just copying data
    data_mtx_.unlock();
    return data_temp; //Theoretically should return by rval since C11 to avoid copying
}


std::ostream& operator<<(std::ostream& os, const micDataStamped& data){
    os << "Timestamp: " << data.timestamp << std::endl;
    os << "Data: " << data.frame << std::endl;
    return os;
}

std::ostream& operator<<(std::ostream& os, const std::vector<micDataStamped>& data){
    if(data.empty()){
        os << "Empty !!!" << std::endl;
        return os;
    }
    for(int i=0; i<data.size(); i++) {
        os << "Frame " << i << ":" << std::endl;
        os << "\t Timestamp: " << data[i].timestamp << std::endl;
        os << "\t Data: " << data[i].frame << std::endl;
    }
    return os;
}


//-----------------------------------------------------------------
//RECORDING RELATED STUFF

namespace little_endian_io
{
  template <typename Word>
  std::ostream& write_word( std::ostream& outs, Word value, unsigned size = sizeof( Word ) )
  {
    for (; size; --size, value >>= 8)
      outs.put( static_cast <char> (value & 0xFF) );
    return outs;
  }
}
using namespace little_endian_io;


void MicReadAlsa::record_thread()
{
    //-----------------------------------------------------------------
    // PREPARING WAV HEADER
    std::ofstream f( filename_base_ + ".wav", std::ios::binary );

    int total_bitrate = rate_ * bits_per_sample_ * channels_; //sample rate
    int data_block_size = (int) channels_ * bits_per_sample_ / 8;

    f << "RIFF----WAVEfmt ";     // (chunk size to be filled in later)
    write_word( f,               16, 4 );  // no extension data
    write_word( f,                1, 2 );  // PCM - integer samples
    write_word( f,        channels_, 2 );  // one channel (mono file)
    write_word( f,            rate_, 4 );  // samples per second (Hz)
    write_word( f,    total_bitrate, 4 );  // (Sample Rate * BitsPerSample * Channels) / 8 == 176400 for 2 channels with 16 bits per sample
    write_word( f,  data_block_size, 2 );  // data block size (size of all integer sample, one for each channel, in bytes)
    write_word( f, bits_per_sample_, 2 );  // number of bits per sample (use a multiple of 8)

    // Write the data chunk header
    size_t data_chunk_pos = f.tellp();
    f << "data----";  // (chunk size to be filled in later)

    printf("%s: Recording Thread ready ...\n", name_.c_str());
    //-----------------------------------------------------------------
    // RECORDING WAV
    while(ready_fl_)
    {
        // Pausing together with the reading thread
        if(!run_fl_){
            printf("%s: Record Thread is paused ...\n",  name_.c_str());
            std::unique_lock<std::mutex> lck(mtx_);
            cv_.wait(lck);
        }
        // Sleeping
        std::this_thread::sleep_for (std::chrono::milliseconds(10));
        // Checking data
        auto data = (record_only_) ? getData() : copyData();
        // If data empty - let's wait more
        if(data.empty()) continue;

        for (auto iter=data.begin(); iter != data.end(); iter++)
        {
            for(int i=0; i<iter->frame.size(); i+=2)
            {
                //Recording data frame (only 1 channel)
                write_word( f, iter->frame[i], (int) bits_per_sample_ / 8 );
            }
        }
    }

    //-----------------------------------------------------------------
    // --- CLOSING WAV RECODRING
    // We'll need the final file size to fix the chunk sizes above
    size_t file_length = f.tellp();

    // Fix the data chunk header to contain the data size
    f.seekp( data_chunk_pos + 4 );
    write_word( f, file_length - data_chunk_pos + 8 );

    // Fix the file header to contain the proper RIFF chunk size, which is (file size - 8) bytes
    f.seekp( 0 + 4 );
    write_word( f, file_length - 8, 4 );
}
