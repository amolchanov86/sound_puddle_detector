// Standart stuff

// My libs
#include "micread_thread.hpp"

MicReadAlsa::MicReadAlsa(bool delayed_start, std::string device, int buffer_frames, unsigned int rate, snd_pcm_format_t format, std::string name):
    run_fl_(false),
    ready_fl_(true),
    name_(name),
    buffer_frames_(buffer_frames),
    rate_(rate),
    format_(format),
    device_(device),
    buffer_(nullptr)
{
    if(openDevice() < 0){
        fprintf(stderr,"%s: ERROR: Failed to open device %s. Please openDevice() manually and start() the thread ...\n",
        name_.c_str(),
        device_.c_str());

        ready_fl_ = false;
    }
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
    if(buffer_ != nullptr) {
        delete[] buffer_;
    }
}

void MicReadAlsa::run() {
    int err; //Reporting ALSA errors
    buffer_ = new char[buffer_frames_ * snd_pcm_format_width(format_) / 8 * 2];

    printf("%s: Thread ready ...\n", name_.c_str());

    while(ready_fl_)
    {
        // Pause functionality
        if(!run_fl_){
            int pause_err = snd_pcm_pause(capture_handle_, 1);
            if(pause_err){
                printf("%s: ERROR: Failed to pause device %s: %s \n",
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
        // Test without ALSA
        //printf("%s: Running ...\n", name_.c_str());
        //std::this_thread::sleep_for (std::chrono::seconds(1));

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
                auto time = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()
                );
                long timestamp = time.count(); //count milliseconds
                frame_stamped.timestamp = timestamp;

                //Copying data from the temporary buffer
                for (int i = 0; i < buffer_frames_; i++)
                {
                    frame_stamped.frame.push_back(buffer_[i]);
                }

                //Push data to the main data buffer
                data.push_back(frame_stamped);

                data_mtx_.unlock();
            }
        }

    }

    // Cleaning, closing
    delete[] buffer_;
    buffer_ = nullptr;

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
        printf("%s: ERROR: Failed to resume device %s: %s\n",
               name_.c_str(),
               device_.c_str(),
               snd_strerror(err));
    }
    cv_.notify_all();
}

void MicReadAlsa::pause() {
    std::unique_lock<std::mutex> lck(mtx_);
    run_fl_ = false;
}

void MicReadAlsa::finish() {
    run_fl_ = false;
    ready_fl_ = false;
    cv_.notify_all();
    printf("%s : Waiting for the thread to finish ...\n", name_.c_str());
    th_.join();
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

    if ((err = snd_pcm_hw_params_set_channels (capture_handle_, hw_params_, 2)) < 0) {
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


std::ostream& operator<<(std::ostream& os, const std::vector<char>& data){
    for(int i=0; i<data.size(); i++) {
        std::cout<<(int)data[i]<<" ";
    }
    return os;
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

