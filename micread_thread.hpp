// The class for easy reading from the microphone input
//

#ifndef MIC_READ_THREAD_MICREAD_THREAD_HPP
#define MIC_READ_THREAD_MICREAD_THREAD_HPP

class MicReadAlsa
{
    std::mutex mtx_;
    std::condition_variable cv_;

    bool run_fl_;
    bool ready_fl_;
    std::string name_;
    std::thread th_;

    void run();

public:
    MicReadAlsa(bool delayed_start=false);
    ~MicReadAlsa();

    void start();
    void pause();
    void finish();
    bool isRunning() const {return run_fl_;}

};

#endif //MIC_READ_THREAD_MICREAD_THREAD_HPP
