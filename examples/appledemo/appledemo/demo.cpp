#include "demo-to-swift.h"
#include "../../../io/yubyaudioio.h"
#include <yuby/yuby.h>
#include <string>
#include <dispatch/dispatch.h>

// everything in this is optional
static void OnYubyAudioIOEvent(void *clientdata, YubyAudioIOEvent event, void *arg) {
    switch (event) {
        case YubyAudioIOEvent::DevicesChanged:
            // the list of available audio devices changed, arg has the list
            break;
        case YubyAudioIOEvent::MapChannels:
            // a channel mapping for the current audio device is needed, arg is the mapping
            break;
        case YubyAudioIOEvent::InputPermissionChanged:
            // the application's audio input permission changed, arg is permitted/not permitted
            break;
        case YubyAudioIOEvent::Interruption:
            // a phone call or similar interrupted the audio I/O, arg is interruption began/ended
            break;
    }
}

// this demo has an audio player and a reverb effect
static void *player = nullptr;
static void *reverb = nullptr;
static bool microphone = false;

// this function is called in a high performance audio thread, the actual audio processing happens here
static bool AudioProcessingCallback(void *clientdata, float **inputBuffers, float **outputBuffers, unsigned int numberOfFrames, unsigned int samplerate, unsigned long long int hostTime) {
    // check if the player has an event for us
    switch ((unsigned int)yuby::Get(player, yuby::OpenState)) {
        case yuby::OpenState::Opening: break;
        case yuby::OpenState::OpenFailed: break;
        case yuby::OpenState::Opened: break;
        case yuby::OpenState::Closed: break;
    }
    bool audioCreated = false;
    // feed back the microphone to the output
    if (microphone && inputBuffers && inputBuffers[0]) {
        audioCreated = true;
        if (inputBuffers[0] != outputBuffers[0]) memcpy(outputBuffers[0], inputBuffers[0], numberOfFrames * 2 * sizeof(float));
    }
    // the player outputs audio here:
    audioCreated |= yuby::Process(player, audioCreated ? outputBuffers[0] : nullptr, nullptr, outputBuffers[0], samplerate, numberOfFrames);
    // the reverb processes the player's output, or if the player is paused, it may still output some "tail"
    audioCreated |= yuby::Process(reverb, audioCreated ? outputBuffers[0] : nullptr, nullptr, outputBuffers[0], samplerate, numberOfFrames);
    return audioCreated;
}

// forward declaration
void analyzerDemo(const char *mp3path);

// called when the app launches
void demoStart(const char *mp3path) {
    // mandatory step when your app launches
    yuby::YubyInit("your license key");
    // subscribe for audio related notifications
    YubyAudioIO::initialize(OnYubyAudioIOEvent, nullptr);
    // create the player and the effect before starting audio I/O
    player = yuby::Create(yuby::ObjectType::Player);
    reverb = yuby::Create(yuby::ObjectType::Reverb);
    // start audio I/O
    YubyAudioIO::start(AudioProcessingCallback, nullptr, 2, true, true, true);
    // open the audio file
    yuby::Open(player, mp3path);
    // run the analyzer demo in the background
    std::string path(mp3path);
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{ analyzerDemo(path.c_str()); });
}

// called when the app closes
void demoEnd(void) {
    YubyAudioIO::shutdown();
    YubyDeleteObjects(player, reverb);
}

bool togglePlayPause(void) {
    yuby::Set(player, yuby::Parameter::TogglePlayPause, 0);
    return yuby::Get(player, yuby::Parameter::Play);
}

bool toggleReverb(void) {
    bool isOn = yuby::Get(reverb, yuby::Parameter::OnOff);
    yuby::Set(reverb, yuby::Parameter::OnOff, isOn ? 0 : 1);
    return !isOn;
}

bool toggleMicrophone(void) {
    microphone = !microphone;
    return microphone;
}

// analyzes an audio file
void analyzerDemo(const char *mp3path) {
    // create a decoder and open the file
    void *decoder = yuby::Create(yuby::ObjectType::Decoder);
    yuby::Open(decoder, mp3path, yuby::OpenFlag::MeasureSilence | yuby::OpenFlag::SkipThumbnailImage);
    if (yuby::GetString(decoder, yuby::ErrorMessage)) {
        yuby::Delete(decoder);
        return;
    }
    
    // get some essential information
    double durationSeconds = ceil(yuby::Get(decoder, yuby::DurationSeconds));
    double samplerate = yuby::Get(decoder, yuby::SamplerateHz);
    unsigned int framesPerPacket = (int)yuby::Get(decoder, yuby::FramesPerPacket);
    
    // create the analyzer
    void *analyzer = yuby::Create(yuby::ObjectType::Analyzer);
    YubySetParameters(analyzer, { yuby::SamplerateHz, samplerate }, { yuby::DurationSeconds, durationSeconds });
    
    // create some buffers
    float *floatbuf = (float *)malloc(framesPerPacket * 2 * sizeof(float) * 2);
    short int *pcmbuf = (short int *)malloc(framesPerPacket * 2 * sizeof(short int) * 2);
    
    // process audio
    bool success = false;
    while (true) {
        if (!yuby::Process(decoder, NULL, NULL, (float *)pcmbuf, 0, framesPerPacket)) break; // decoder error
        int f = (int)yuby::Get(decoder, yuby::FramesCreated);
        if (f == 0) { usleep(100000); continue; } // buffering, sleep for 100 ms
        else if (f < 0) break; // end of file
        else success = true;
        yuby::shortinttofloat(pcmbuf, floatbuf, f);
        if (!yuby::Process(analyzer, floatbuf, NULL, NULL, 0, f)) { success = false; break; } // analyzer error
    }
    
    // we're done with processing the audio
    yuby::Delete(decoder);
    free(floatbuf);
    free(pcmbuf);
    
    // get some useful information
    if (success) {
        printf("bpm: %f\n", yuby::Get(analyzer, yuby::BeatsPerMinute));
        printf("loudness: %f db\n", yuby::Get(analyzer, yuby::LoudPartsAverageDecibel));
    }
    
    yuby::Delete(analyzer);
}
