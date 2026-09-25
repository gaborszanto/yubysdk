#include <jni.h>
#include <android/log.h>
#include <cmath>
#include <cstring>
#include <string>
#include <thread>
#include <unistd.h>
#include <yuby.h>
#include "yubyaudioio.h"

// this demo has an audio player and a reverb effect
static void *player = nullptr;
static void *reverb = nullptr;
static bool microphone = false;
static bool audioInputAvailable = false;

static void OnYubyAudioIOEvent(void *, YubyAudioIOEvent event, void *) {
    if (event == YubyAudioIOEvent::InputPermissionChanged) __android_log_write(ANDROID_LOG_INFO, "YubyDemo", "Microphone permission changed");
}

static bool AudioProcessingCallback(void *, float **inputBuffers, float **outputBuffers, unsigned int numberOfFrames, unsigned int sampleRate, unsigned long long) {
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
    audioCreated |= yuby::Process(player, audioCreated ? outputBuffers[0] : nullptr, nullptr, outputBuffers[0], sampleRate, numberOfFrames);
    // the reverb processes the player's output, or if the player is paused, it may still output some "tail"
    audioCreated |= yuby::Process(reverb, audioCreated ? outputBuffers[0] : nullptr, nullptr, outputBuffers[0], sampleRate, numberOfFrames);
    return audioCreated;
}

// analyzes an audio file
static void analyzerDemo(const std::string &mp3path) {
    // create a decoder and open the file
    void *decoder = yuby::Create(yuby::ObjectType::Decoder);
    yuby::Open(decoder, mp3path.c_str(), yuby::OpenFlag::MeasureSilence | yuby::OpenFlag::SkipThumbnailImage);
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
        if (!yuby::Process(decoder, nullptr, nullptr, (float *)pcmbuf, 0, framesPerPacket)) break; // decoder error
        int f = (int)yuby::Get(decoder, yuby::FramesCreated);
        if (f == 0) { usleep(100000); continue; } // buffering, sleep for 100 ms
        else if (f < 0) break; // end of file
        else success = true;
        yuby::shortinttofloat(pcmbuf, floatbuf, f);
        if (!yuby::Process(analyzer, floatbuf, nullptr, nullptr, 0, f)) { success = false; break; } // analyzer error
    }

    // we're done with processing the audio
    yuby::Delete(decoder);
    free(floatbuf);
    free(pcmbuf);

    // get some useful information
    if (success) {
        __android_log_print(ANDROID_LOG_INFO, "YubyDemo", "bpm: %f", yuby::Get(analyzer, yuby::BeatsPerMinute));
        __android_log_print(ANDROID_LOG_INFO, "YubyDemo", "loudness: %f dB", yuby::Get(analyzer, yuby::LoudPartsAverageDecibel));
    }

    yuby::Delete(analyzer);
}

extern "C" JNIEXPORT void JNICALL Java_com_example_yubydemo_MainActivity_nativeStart(JNIEnv *env, jobject, jstring audioPath, jboolean enableInput) {
    if (player != nullptr) return;
    const char *chars = env->GetStringUTFChars(audioPath, nullptr);
    std::string path(chars);
    env->ReleaseStringUTFChars(audioPath, chars);

    // mandatory step when your app launches
    yuby::YubyInit("your license key");
    // subscribe for audio related notifications
    YubyAudioIO::initialize(OnYubyAudioIOEvent, nullptr);
    // create the player and the effect before starting audio I/O
    player = yuby::Create(yuby::ObjectType::Player);
    reverb = yuby::Create(yuby::ObjectType::Reverb);
    // start audio I/O
    audioInputAvailable = enableInput;
    YubyAudioIO::start(AudioProcessingCallback, nullptr, 2, true, audioInputAvailable, true);
    // open the audio file
    yuby::Open(player, path.c_str());
    std::thread(analyzerDemo, path).detach();
}

extern "C" JNIEXPORT void JNICALL Java_com_example_yubydemo_MainActivity_nativeSetInputAvailable(JNIEnv *, jobject, jboolean available) {
    if ((audioInputAvailable == available) || (player == nullptr)) return;
    audioInputAvailable = available;
    if (!available) microphone = false;
    YubyAudioIO::start(AudioProcessingCallback, nullptr, 2, true, audioInputAvailable, true);
}

extern "C" JNIEXPORT jboolean JNICALL Java_com_example_yubydemo_MainActivity_nativeTogglePlayPause(JNIEnv *, jobject) {
    if (player == nullptr) return JNI_FALSE;
    yuby::Set(player, yuby::TogglePlayPause, 0);
    return yuby::Get(player, yuby::Play) != 0.0;
}

extern "C" JNIEXPORT jboolean JNICALL Java_com_example_yubydemo_MainActivity_nativeToggleReverb(JNIEnv *, jobject) {
    if (reverb == nullptr) return JNI_FALSE;
    const bool isOn = yuby::Get(reverb, yuby::OnOff) != 0.0;
    yuby::Set(reverb, yuby::OnOff, isOn ? 0 : 1);
    return !isOn;
}

extern "C" JNIEXPORT jboolean JNICALL Java_com_example_yubydemo_MainActivity_nativeToggleMicrophone(JNIEnv *, jobject) {
    if (!audioInputAvailable) return JNI_FALSE;
    microphone = !microphone;
    return microphone;
}

extern "C" JNIEXPORT void JNICALL Java_com_example_yubydemo_MainActivity_nativeStop(JNIEnv *, jobject) {
    if (player == nullptr) return;
    microphone = false;
    YubyAudioIO::shutdown();
    YubyDeleteObjects(player, reverb);
    player = nullptr;
    reverb = nullptr;
    audioInputAvailable = false;
}
