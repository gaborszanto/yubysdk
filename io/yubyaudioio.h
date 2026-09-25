#ifndef YubyAudioIOHeader
#define YubyAudioIOHeader

#define IOS_PREFERRED_SAMPLERATE 44100
#define IOS_AVAUDIOSESSION_MODE AVAudioSessionModeDefault
#define IOS_PREFER_SPEAKER_OVER_RECEIVER true
#define IOS_ALLOW_BLUETOOTH false
#define IOS_SAVE_BATTERY false
#define IOS_AVAUDIOSESSIONCATEGORYOPTIONS 0 // to enable Bluetooth, use: AVAudioSessionCategoryOptionAllowBluetoothA2DP | AVAudioSessionCategoryOptionMixWithOthers
#define MAX_MAPPABLE_CHANNELS 8

class YubyAudioIO;

struct AudioDeviceInfo {
    const char *name;
    unsigned int numInputChannels, numOutputChannels;
};
struct AudioDeviceList {
    unsigned int length;
    AudioDeviceInfo *devices;
};
struct ChannelMap {
    int outputMap[MAX_MAPPABLE_CHANNELS], inputMap[MAX_MAPPABLE_CHANNELS]; // map[internal channel index] = physical channel index
    const char *outputDeviceName, *inputDeviceName;
    unsigned int numOutputChannels, numInputChannels;
};
enum class YubyAudioIOEvent: int { 
    DevicesChanged, // YubyAudioIOCallback(arg): AudioDeviceList
    MapChannels,    // YubyAudioIOCallback(arg): ChannelMap
    InputPermissionChanged, // YubyAudioIOCallback(arg): bool (granted)
    Interruption    // YubyAudioIOCallback(arg): bool (interruption begin/end)
};
typedef void (*YubyAudioIOCallback) (void *clientdata, YubyAudioIOEvent event, void *arg);

typedef bool (*audioProcessingCallback) (void *clientdata, float **inputBuffers, float **outputBuffers, unsigned int numberOfFrames, unsigned int samplerate, unsigned long long int hostTime);

class YubyAudioIO {
public:
    static unsigned int preferredBufferSizeMs;
    static void initialize(YubyAudioIOCallback callback, void *clientdata = 0);
    static void shutdown();
    static void start(audioProcessingCallback apc, void *clientdata = 0, unsigned int numberOfChannels = 2, bool interleaved = true, bool inputEnabled = false, bool outputEnabled = true);
    static void stop();
    static void mapChannels();
    static void setDevice(const char *deviceName);
#if _WIN32
    static void initializeWithASIO(YubyAudioIOCallback callback, void *clientdata = 0);
    static bool ASIOMain(int *returnCode);
#endif
};

#endif
