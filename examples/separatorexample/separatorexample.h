#include "yuby.h"
#include <atomic>
#include <filesystem>

#define MinimumAcceptableSpeed 20 // must be higher than 1

struct Demucs: public yuby::StemSeparator {
public:
    static std::atomic<int> speed;
    
    static void initialize(const std::filesystem::path &modelPath);
    static Demucs *GetSeparatorForYubyPlayer();
    
    ~Demucs();
    int process(unsigned int resetWithInputSamplerate, short int *input, unsigned int numFrames, short int *output, unsigned int outputChannelOffset);
    
private:
    void *instance;
};

#ifndef AudioShakeClientId
#define AudioShakeClientId "your audioshake client id"
#endif
#ifndef AudioShakeClientSecret
#define AudioShakeClientSecret "your audioshake client secret"
#endif

class AudioShakeSeparator;

struct AudioShake: public yuby::StemSeparator {
public:
    static std::atomic<int> speed;
    
    static void initialize(const std::filesystem::path &modelPath);
    static AudioShake *GetSeparatorForYubyPlayer();
    
    ~AudioShake();
    int process(unsigned int resetWithInputSamplerate, short int *input, unsigned int numFrames, short int *output, unsigned int outputChannelOffset);
    
private:
    AudioShakeSeparator *separator;
    AudioShake(AudioShakeSeparator *s);
};
