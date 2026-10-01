#include <windows.h>
#include <string>
#include <thread>
#include "../../io/yubyaudioio.h"
#include "../../windows/yuby.h"

#if defined(_DLL)
#define RT "MD"
#else
#define RT "MT"
#endif
#if defined(_DEBUG)
#define RTD "d"
#else
#define RTD ""
#endif
#if _M_ARM64
#define CPU "arm64"
#elif _M_X64
#define CPU "x64"
#endif
#pragma comment(lib, "..\\..\\windows\\yuby_145_" RT RTD "_" CPU ".lib")
#pragma comment(lib, "Ws2_32.lib")

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
    yuby::YubyInit(0, 0, 0); // three zeros allow for time-limited development use --- visit yuby.com to get a license
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
    std::thread([path = std::string(mp3path)] { analyzerDemo(path.c_str()); }).detach();
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
        if (f == 0) { Sleep(100); continue; } // buffering, sleep for 100 ms
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
        OutputDebugStringA(("bpm: " + std::to_string(yuby::Get(analyzer, yuby::BeatsPerMinute)) + "\n").c_str());
        OutputDebugStringA(("loudness: " + std::to_string(yuby::Get(analyzer, yuby::LoudPartsAverageDecibel)) + " db\n").c_str());
    }
    
    yuby::Delete(analyzer);
}

constexpr int PlayButton = 1;
constexpr int MicButton = 2;
constexpr int ReverbButton = 3;

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_CREATE) {
        HINSTANCE instance = reinterpret_cast<LPCREATESTRUCTW>(lParam)->hInstance;
        CreateWindowW(L"BUTTON", L"Play", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 20, 20, 100, 32, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(PlayButton)), instance, nullptr);
        CreateWindowW(L"BUTTON", L"Mic Off", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 130, 20, 100, 32, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(MicButton)), instance, nullptr);
        CreateWindowW(L"BUTTON", L"Reverb Off", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 240, 20, 100, 32, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ReverbButton)), instance, nullptr);
        return 0;
    } else if (message == WM_COMMAND && HIWORD(wParam) == BN_CLICKED) {
        if (LOWORD(wParam) == PlayButton) SetWindowTextW(reinterpret_cast<HWND>(lParam), togglePlayPause() ? L"Pause" : L"Play");
        else if (LOWORD(wParam) == MicButton) SetWindowTextW(reinterpret_cast<HWND>(lParam), toggleMicrophone() ? L"Mic On" : L"Mic Off");
        else if (LOWORD(wParam) == ReverbButton) SetWindowTextW(reinterpret_cast<HWND>(lParam), toggleReverb() ? L"Reverb On" : L"Reverb Off");
        return 0;
    } else if (message == WM_DESTROY) {
        demoEnd();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    constexpr wchar_t className[] = L"windemo";
    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    windowClass.lpszClassName = className;
    if (!RegisterClassW(&windowClass)) return 1;

    HWND window = CreateWindowExW(0, className, L"Yuby Demo", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, nullptr,
        nullptr, instance, nullptr);
    if (!window) return 1;
    demoStart("..\\tropical-breeze.mp3");
    ShowWindow(window, showCommand);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}
