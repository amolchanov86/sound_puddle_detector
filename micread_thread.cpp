#include "micread_thread.hpp"

#include <fstream>
#include <iostream>
#include <climits>

MicReadAlsa::MicReadAlsa(bool manual_start,
                         bool record,
                         bool record_only,
                         bool record_csv,
                         float record_freq,
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
    record_(record),
    record_csv_(record_csv),
    chunks_read_(0),
    chunks_recorded_(0),
    rec_freq_estimate_(0.)
{
    setRecFreq(record_freq);

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
        //Creating a timestamp
        auto time = std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::system_clock::now().time_since_epoch()
                    );
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
                micDataStamped chunk_stamped;
                chunk_stamped.id = chunks_read_;
                chunk_stamped.flags.recorded = !record_;
                chunks_read_ += 1;

                long timestamp = time.count(); //count milliseconds
                chunk_stamped.timestamp = timestamp;

                //Only for a single channel
                int i_incr = bits_per_sample_ / 8;
                int buffer_bytes = buffer_frames_ * channels_* bits_per_sample_ / 8;
                for (int i = 0; i < buffer_bytes; i+=i_incr)
                {
                    // swap_endian(buffer_ + i, bits_per_sample_ / 8);
                    auto val_ptr = (int16_t *) (buffer_ + i);
                    chunk_stamped.frames.push_back(*val_ptr);
                }

                //Calculating freq
                freq_ = (double) 1.0 / (double)(time - time_prev).count() * 1000000.0;
                fps_est_= (double) buffer_frames_ / (double)(time - time_prev).count() * 1000000.0;
//                std::cout << "Read freq: " << estReadFreq() << " FPS:" << estFPS() << std::endl;
                time_prev = time;

                //Push data to the main data buffer
                data.push_back(chunk_stamped);

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
    printf("%s: Chunks read %ld ...\n",  name_.c_str(), getChunksRead());
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
    printf("%s: Waiting for the reading thread to finish ...\n", name_.c_str());
    th_.join();
    if(record_){
        printf("%s: Waiting for the recording thread to finish ...\n", name_.c_str());
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
    std::vector<micDataStamped> data_temp;
    data_mtx_.lock();
    while(!data.empty() && data.front().flags.recorded) {
        data_temp.push_back(std::move(data.front())); //Not not sure if move actually makes difference
        data.pop_front();
    }
    data_mtx_.unlock();
    return data_temp; //Theoretically should return by rval since C11 to avoid copying
}

std::vector<micDataStamped> MicReadAlsa::copyUnrecordedData(){
    std::vector<micDataStamped> data_temp;
    data_mtx_.lock();
    for(auto it = data.begin(); it != data.end(); it++)
    {
        if(!(it->flags.recorded))
        {
            data_temp.push_back(*it); //hopefully default copy constructor will do the job
            it->flags.recorded = 1;
        }
    }
    data_mtx_.unlock();
    return data_temp; //Theoretically should return by rval since C11 to avoid copying
}

// Try not to use this function either since it will interfere with the recording mechanism
std::deque<micDataStamped> MicReadAlsa::moveData(){
    data_mtx_.lock();
    std::deque<micDataStamped> data_temp = std::move(data); //After moving the data vector should be empty
    data_mtx_.unlock();
    return data_temp; //Theoretically should return by rval since C11 to avoid copying
}

// DON'T USE THIS FUNCTION: I left it specifically to point out / demonstrate unsafe behavior
std::deque<micDataStamped> MicReadAlsa::copyData(){
    data_mtx_.lock();
    std::deque<micDataStamped> data_temp = data; //Just copying data
    data_mtx_.unlock();
    return data_temp; //Theoretically should return by rval since C11 to avoid copying
}


std::ostream& operator<<(std::ostream& os, const micDataStamped& data){
    os << "Timestamp: " << data.timestamp << std::endl;
    os << "Data: " << data.frames << std::endl;
    return os;
}

std::ostream& operator<<(std::ostream& os, const std::vector<micDataStamped>& data){
    if(data.empty()){
        os << "Empty !!!" << std::endl;
        return os;
    }
    for(int i=0; i<data.size(); i++) {
        os << "Frame " << i << ":" << std::endl;
        os << "\t Id: " << data[i].id << std::endl;
        os << "\t Timestamp: " << data[i].timestamp << std::endl;
        os << "\t Data: " << data[i].frames << std::endl;
    }
    return os;
}


//-----------------------------------------------------------------
//RECORDING RELATED STUFF


template <typename Word>
std::ostream& write_word(std::ostream& outs, Word value)
{
    union
    {
        Word u;
        int8_t u8[sizeof(Word)];
    } source;

    source.u = value;

    for (int i=0; i<sizeof(Word); i++)
        outs.put(source.u8[i]);
    return outs;
}

template <typename Word>
std::ostream& write_word_swap_endian( std::ostream& outs, Word value, unsigned size = sizeof( Word ) )
{
    for (; size; --size, value >>= 8)
        outs.put( static_cast <int8_t> (value & 0xFF) );
    return outs;
}


void MicReadAlsa::record_thread()
{
    //-----------------------------------------------------------------
    // Opening CSV
    std::ofstream csv_file;
    csv_file.open(filename_base_ + ".csv");
    if(record_csv_) {
        csv_file << "id" << "," <<
                    "timestamp" << "," <<
                    "flag" << "," <<
                    "frames" << std::endl;
    }

    //-----------------------------------------------------------------
    // PREPARING WAV HEADER
    std::ofstream f( filename_base_ + ".wav", std::ios::binary );

    int total_bitrate = rate_ * bits_per_sample_ * channels_; //sample rate
    int data_block_size = (int) channels_ * bits_per_sample_ / 8;

    f << "RIFF----WAVEfmt ";     // (chunk size to be filled in later)
    write_word_swap_endian( f,               16, 4 );  // no extension data
    write_word_swap_endian( f,                1, 2 );  // PCM - integer samples
    write_word_swap_endian( f,        channels_, 2 );  // one channel (mono file)
    write_word_swap_endian( f,            rate_, 4 );  // samples per second (Hz)
    write_word_swap_endian( f,    total_bitrate, 4 );  // (Sample Rate * BitsPerSample * Channels) / 8 == 176400 for 2 channels with 16 bits per sample
    write_word_swap_endian( f,  data_block_size, 2 );  // data block size (size of all integer sample, one for each channel, in bytes)
    write_word_swap_endian( f, bits_per_sample_, 2 );  // number of bits per sample (use a multiple of 8)

    // Write the data chunk header
    size_t data_chunk_pos = f.tellp();
    f << "data----";  // (chunk size to be filled in later)

    //Time to measure freq
    auto rec_time_prev = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::system_clock::now().time_since_epoch()
                );

    printf("%s: Recording Thread ready ...\n", name_.c_str());
    //-----------------------------------------------------------------
    // RECORDING WAV and CSV
    while(ready_fl_)
    {
        // Pausing together with the reading thread
        if(!run_fl_){
            printf("%s: Record Thread is paused ...\n",  name_.c_str());
            std::unique_lock<std::mutex> lck(mtx_);
            cv_.wait(lck);
        }

        //Creating a timestamp
        auto rec_time = std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::system_clock::now().time_since_epoch()
                    );

        // Sleeping
        std::this_thread::sleep_for (std::chrono::milliseconds(rec_delay_));
        // Checking data
        auto data = copyUnrecordedData();
        // If data empty - let's wait more
        if(data.empty()) continue;

        int chunks_recorded_cur = 0;

        for (auto iter=data.begin(); iter != data.end(); iter++)
        {
            chunks_recorded_ ++;
            chunks_recorded_cur ++;

            // CSV nonframe information
            if(record_csv_) {
                csv_file << std::to_string(iter->id) << "," <<
                            std::to_string(iter->timestamp) << "," <<
                            std::to_string(iter->flags.all) << ",";
            }

            // WAV and CSV frames writing
            for(int i=0; i<iter->frames.size(); i++)
            {
                //Recording data frame (only 1 channel)
                write_word_swap_endian(f, iter->frames[i], (int)bits_per_sample_/8);
                // write_word(f, iter->frame[i]);

                if(record_csv_) { //Space separation for easy splitting
                    csv_file << " " << std::to_string(iter->frames[i]);
                }

            }

            // CSV ENDLINE
            if(record_csv_) {
                csv_file<<std::endl;
            }

        }

        //Calculating freq
        rec_freq_estimate_ = (double) 1.0 / (rec_time - rec_time_prev).count() * 1000000;

        std::cout << "Rec freq: " << estRecFreq() << " Chunks recorded:" << chunks_recorded_cur << std::endl;
        rec_time_prev = rec_time;


    }
    //-----------------------------------------------------------------
    // --- CLOSING CSV
    csv_file.close();

    //-----------------------------------------------------------------
    // --- CLOSING WAV RECODRING
    // We'll need the final file size to fix the chunk sizes above
    size_t file_length = f.tellp();

    // Fix the data chunk header to contain the data size
    f.seekp( data_chunk_pos + 4 );
    write_word_swap_endian( f, file_length - data_chunk_pos + 8 );

    // Fix the file header to contain the proper RIFF chunk size, which is (file size - 8) bytes
    f.seekp( 0 + 4 );
    write_word_swap_endian( f, file_length - 8, 4 );

    printf("%s: Chunks recorded %ld ...\n",  name_.c_str(), getChunksRecorded());
}

long MicReadAlsa::getChunksRecorded() const
{
    return chunks_recorded_;
}

long MicReadAlsa::getChunksRead() const
{
    return chunks_read_;
}
