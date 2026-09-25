#if __ANDROID__
#include "./yubyaudioio.h"
#include <cstdlib>
#include <cstring>
#include <cmath> 
#include <chrono>
#include <dlfcn.h>
#include <pthread.h>
#include <time.h> 
#include <unistd.h> 
#include <sys/system_properties.h>
#include <android/log.h>
#include "./asio.h" 
#include "./asioformatconverter.h"

struct HostTime {
public:
    static HostTime *create() {
        HostTime *h = (HostTime *)malloc(sizeof(HostTime));
        if (!h) return NULL; else memset(h, 0, sizeof(HostTime));
        h->startup = true;
        return h;
    }

    unsigned long long int getFromPosition(double sampleTime, unsigned int numFrames) {
        framePos = sampleTime + numFrames;
        return getFromPosition(sampleTime);
    }

    unsigned long long int getFromFrames(unsigned int numFrames) {
        unsigned long long int r = getFromPosition(framePos);
        framePos += numFrames;
        return r;
    }
private:
    double times[2048], sX[32], sXX[32], sXY[32], sY[32], framePos;
    unsigned int point;
    bool startup;

    unsigned long long int getFromPosition(double sampleTime) {
        double h = static_cast<double>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
        unsigned int group = point >> 4;
        times[point * 4 + 0] = sampleTime;
        times[point * 4 + 1] = sampleTime * sampleTime;
        times[point * 4 + 2] = sampleTime * h;
        times[point * 4 + 3] = h;
        if (++point >= 512) { startup = false; point = 0; }

        unsigned int g = group * 64;
        double sumX = times[g], sumXX = times[g + 1], sumXY = times[g + 2], sumY = times[g + 3], *t = times + g + 4;
        for (unsigned int n = 1; n < 16; n++) {
            sumX += *t++;
            sumXX += *t++;
            sumXY += *t++;
            sumY += *t++;
        }
        sX[group] = sumX; sXX[group] = sumXX; sXY[group] = sumXY; sY[group] = sumY;

        sumX = sX[0]; sumXX = sXX[0]; sumXY = sXY[0]; sumY = sY[0];
        for (unsigned int n = 1; n < 32; n++) {
            sumX += sX[n];
            sumXX += sXX[n];
            sumXY += sXY[n];
            sumY += sY[n];
        }
        double count = startup ? point : 512.0, denominator = count * sumXX - sumX * sumX, slope = denominator == 0.0 ? 0.0 : (count * sumXY - sumX * sumY) / denominator, intercept = (sumY - slope * sumX) / count;
        return std::chrono::microseconds(std::llround(slope * sampleTime + intercept)).count();
    }
};

#define MAXFRAMES 4096

#define AAUDIO_OK 0
#define AAUDIO_UNSPECIFIED 0
#define AAUDIO_DIRECTION_OUTPUT 0
#define AAUDIO_DIRECTION_INPUT 1
#define AAUDIO_FORMAT_PCM_I16 1
#define AAUDIO_FORMAT_PCM_FLOAT 2
#define AAUDIO_FORMAT_PCM_I24_PACKED 3 
#define AAUDIO_FORMAT_PCM_I32 4        
#define AAUDIO_SHARING_MODE_EXCLUSIVE 0
#define AAUDIO_SHARING_MODE_SHARED 1
#define AAUDIO_PERFORMANCE_MODE_LOW_LATENCY 12
#define AAUDIO_CALLBACK_RESULT_CONTINUE 0
#define AAUDIO_USAGE_MEDIA 1
#define AAUDIO_CONTENT_TYPE_MUSIC 2
#define AAUDIO_INPUT_PRESET_UNPROCESSED 9
#define AAUDIO_STREAM_STATE_STOPPING 9
#define AAUDIO_STREAM_STATE_DISCONNECTED 13
#define AAUDIO_ERROR_DISCONNECTED -899
#define AAUDIO_ERROR_TIMEOUT -892

static int (* APermissionManager_checkPermission)(const char *permission, pid_t pid, uid_t uid, int *outResult) = nullptr;

typedef struct AAudioStreamStruct AAudioStream;
typedef struct AAudioStreamBuilderStruct AAudioStreamBuilder;
typedef int (* AAudioStream_dataCallback)(AAudioStream *stream, void *userData, void *audioData, int numFrames);
typedef void (* AAudioStream_errorCallback)(AAudioStream *stream, void *userData, int error);

int (* AAudio_createStreamBuilder)(AAudioStreamBuilder **builder) = nullptr;
int (* AAudioStreamBuilder_openStream)(AAudioStreamBuilder *builder, AAudioStream **stream) = nullptr;
int (* AAudioStreamBuilder_delete)(AAudioStreamBuilder *builder) = nullptr;
void (* AAudioStreamBuilder_setDataCallback)(AAudioStreamBuilder *builder, AAudioStream_dataCallback callback, void *userData) = nullptr;
void (* AAudioStreamBuilder_setErrorCallback)(AAudioStreamBuilder *builder, AAudioStream_errorCallback callback, void *userData) = nullptr;
void (* AAudioStreamBuilder_setDirection)(AAudioStreamBuilder *builder, int direction) = nullptr;
void (* AAudioStreamBuilder_setSampleRate)(AAudioStreamBuilder *builder, int sampleRate) = nullptr;
void (* AAudioStreamBuilder_setPerformanceMode)(AAudioStreamBuilder *builder, int mode) = nullptr;
void (* AAudioStreamBuilder_setChannelCount)(AAudioStreamBuilder *builder, int channelCount) = nullptr;
void (* AAudioStreamBuilder_setFormat)(AAudioStreamBuilder *builder, int format) = nullptr;
void (* AAudioStreamBuilder_setSharingMode)(AAudioStreamBuilder *builder, int sharingMode) = nullptr;
void (* AAudioStreamBuilder_setBufferCapacityInFrames)(AAudioStreamBuilder *builder, int numFrames) = nullptr;
void (* AAudioStreamBuilder_setFramesPerDataCallback)(AAudioStreamBuilder *builder, int numFrames) = nullptr;
void (* AAudioStreamBuilder_setDeviceId)(AAudioStreamBuilder *builder, int deviceId) = nullptr;
void (* AAudioStreamBuilder_setInputPreset)(AAudioStreamBuilder *builder, int inputPreset) = nullptr; // API 28
void (* AAudioStreamBuilder_setUsage)(AAudioStreamBuilder *builder, int usage) = nullptr; // API 28
void (* AAudioStreamBuilder_setContentType)(AAudioStreamBuilder *builder, int contentType) = nullptr; // API 28
int (* AAudioStream_requestStart)(AAudioStream *stream) = nullptr;
int (* AAudioStream_requestStop)(AAudioStream *stream) = nullptr;
int (* AAudioStream_requestPause)(AAudioStream *stream) = nullptr;
int (* AAudioStream_requestFlush)(AAudioStream *stream) = nullptr;
int (* AAudioStream_getXRunCount)(AAudioStream *stream) = nullptr;
int (* AAudioStream_getFramesPerBurst)(AAudioStream *stream) = nullptr;
int (* AAudioStream_getSampleRate)(AAudioStream *stream) = nullptr;
int (* AAudioStream_getChannelCount)(AAudioStream *stream) = nullptr;
int (* AAudioStream_getFormat)(AAudioStream *stream) = nullptr;
int (* AAudioStream_getDeviceId)(AAudioStream *stream) = nullptr;
int (* AAudioStream_getBufferSizeInFrames)(AAudioStream *stream) = nullptr;
int (* AAudioStream_getBufferCapacityInFrames)(AAudioStream *stream) = nullptr;
int64_t (* AAudioStream_getFramesRead)(AAudioStream *stream) = nullptr;
int64_t (* AAudioStream_getFramesWritten)(AAudioStream *stream) = nullptr;
int (* AAudioStream_getState)(AAudioStream *stream) = nullptr;
int (* AAudioStream_close)(AAudioStream *stream) = nullptr;
int (* AAudioStream_release)(AAudioStream *stream) = nullptr; // API 30
int (* AAudioStream_setBufferSizeInFrames)(AAudioStream *stream, int numFrames) = nullptr;
int (* AAudioStream_read)(AAudioStream *stream, void *buffer, int numFrames, int64_t timeoutNanoseconds) = nullptr;
int (* AAudioStream_getTimestamp)(AAudioStream *stream, clockid_t clockid, int64_t *framePosition, int64_t *timeNanoseconds) = nullptr;
int (* AAudioStream_waitForStateChange)(AAudioStream *stream, int inputState, int *nextState, int64_t timeoutNanoseconds) = nullptr;
const char *(* AAudio_convertResultToText)(int returnCode) = nullptr;

static YubyAudioIOCallback mainCallback = nullptr;
static void *mainClientdata = nullptr;
static volatile int mainState = 0;
static int androidSdkVersion = 0; // 0 = unknown

static void *permissionThread(void *) {
    int last = -1;
    while (mainState == 2) {
        int permission = 0;
        if (APermissionManager_checkPermission("android.permission.RECORD_AUDIO", getpid(), getuid(), &permission) != 0) permission = -1;
        else permission = (permission == 0) ? 1 : 0;
        if ((permission != -1) && (permission != last)) {
            last = permission;
            bool granted = permission == 1;
            mainCallback(mainClientdata, YubyAudioIOEvent::InputPermissionChanged, &granted);
        }
        for (int n = 0; n < 10; n++) if (mainState != 2) break; else usleep(100000);
    }
    mainState = 4;
    pthread_detach(pthread_self());
    pthread_exit(NULL);
}

unsigned int YubyAudioIO::preferredBufferSizeMs = 12;
void YubyAudioIO::setDevice(const char *) {}

void YubyAudioIO::initialize(YubyAudioIOCallback callback, void *clientdata) {
    if (mainState == 0) {
        mainState = 1;

        char sdkVersion[PROP_VALUE_MAX] = {0}; // make the Android version check optional, continue if can not be read
        if (__system_property_get("ro.build.version.sdk", sdkVersion) != 0) androidSdkVersion = atoi(sdkVersion);
        if (androidSdkVersion && (androidSdkVersion < __ANDROID_API_O_MR1__)) { __android_log_write(ANDROID_LOG_ERROR, "YubyAudioIO", "Android version below 27"); return; }
        void *aaudioLib = dlopen("libaaudio.so", RTLD_NOW);
        if (aaudioLib == nullptr) { __android_log_write(ANDROID_LOG_ERROR, "YubyAudioIO", "libaaudio.so cannot be loaded"); return; }
#define GETFUNCTION(func) func = (decltype(func))dlsym(aaudioLib, #func); if (func == nullptr) { __android_log_write(ANDROID_LOG_ERROR, "YubyAudioIO", "aaudio function not loaded: " #func); return; }
#define GETOPTIONALFUNCTION(func) func = (decltype(func))dlsym(aaudioLib, #func); 
#define GETFUNCTIONWITHALIAS(func, alias) func = (decltype(func))dlsym(aaudioLib, #func); if (func == nullptr) func = (decltype(func))dlsym(aaudioLib, alias); if (func == nullptr) { __android_log_write(ANDROID_LOG_ERROR, "YubyAudioIO", "aaudio function not loaded: " #func " or " alias); return; }
        GETFUNCTION(AAudioStreamBuilder_openStream)
        GETFUNCTION(AAudioStreamBuilder_delete)
        GETFUNCTION(AAudioStreamBuilder_setDataCallback)
        GETFUNCTION(AAudioStreamBuilder_setErrorCallback)
        GETFUNCTION(AAudioStreamBuilder_setDirection)
        GETFUNCTION(AAudioStreamBuilder_setSampleRate)
        GETFUNCTION(AAudioStreamBuilder_setPerformanceMode)
        GETFUNCTIONWITHALIAS(AAudioStreamBuilder_setChannelCount, "AAudioStreamBuilder_setSamplesPerFrame")
        GETFUNCTION(AAudioStreamBuilder_setFormat)
        GETFUNCTION(AAudioStreamBuilder_setSharingMode)
        GETFUNCTION(AAudioStreamBuilder_setBufferCapacityInFrames)
        GETFUNCTION(AAudioStreamBuilder_setFramesPerDataCallback)
        GETFUNCTION(AAudioStreamBuilder_setDeviceId)
        GETOPTIONALFUNCTION(AAudioStreamBuilder_setInputPreset)
        GETOPTIONALFUNCTION(AAudioStreamBuilder_setUsage)
        GETOPTIONALFUNCTION(AAudioStreamBuilder_setContentType)
        GETFUNCTION(AAudioStream_requestStart)
        GETFUNCTION(AAudioStream_requestStop)
        GETFUNCTION(AAudioStream_requestPause)
        GETFUNCTION(AAudioStream_requestFlush)
        GETFUNCTION(AAudioStream_getXRunCount)
        GETFUNCTION(AAudioStream_getFramesPerBurst)
        GETFUNCTION(AAudioStream_getSampleRate)
        GETFUNCTIONWITHALIAS(AAudioStream_getChannelCount, "AAudioStream_getSamplesPerFrame")
        GETFUNCTION(AAudioStream_getFormat)
        GETFUNCTION(AAudioStream_getDeviceId)
        GETFUNCTION(AAudioStream_getBufferSizeInFrames)
        GETFUNCTION(AAudioStream_getBufferCapacityInFrames)
        GETFUNCTION(AAudioStream_getFramesRead)
        GETFUNCTION(AAudioStream_getFramesWritten)
        GETFUNCTION(AAudioStream_getState)
        GETFUNCTION(AAudioStream_close)
        GETOPTIONALFUNCTION(AAudioStream_release)
        GETFUNCTION(AAudioStream_setBufferSizeInFrames)
        GETFUNCTION(AAudioStream_read)
        GETFUNCTION(AAudioStream_getTimestamp)
        GETFUNCTION(AAudioStream_waitForStateChange)
        GETFUNCTION(AAudio_convertResultToText)
        GETFUNCTION(AAudio_createStreamBuilder) // must be the last GETFUNCTION!
#undef GETOPTIONALFUNCTION
#undef GETFUNCTIONWITHALIAS
#undef GETFUNCTION

        void *androidLib = dlopen("libandroid.so", RTLD_NOW);
        if (androidLib != nullptr) APermissionManager_checkPermission = (decltype(APermissionManager_checkPermission))dlsym(androidLib, "APermissionManager_checkPermission");
    } else shutdown();

    mainCallback = callback;
    mainClientdata = clientdata;

    if (!mainCallback) return; else if (APermissionManager_checkPermission != nullptr) {
        mainState = 2;
        pthread_t thread;
        if (pthread_create(&thread, NULL, permissionThread, NULL) != 0) mainState = 1;
    } else mainState = 1;

    static const AudioDeviceInfo androidDeviceInfo = { "Audio devices are automatically handled by Android.", 0, 0 };
    static const AudioDeviceList androidDeviceList = { 1, (AudioDeviceInfo *)&androidDeviceInfo };
    callback(clientdata, YubyAudioIOEvent::DevicesChanged, (void *)&androidDeviceList);
}

void YubyAudioIO::shutdown() {
    if (mainState == 0) return;
    stop();
    if (mainState == 2) {
        mainState = 3;
        while (mainState != 4) usleep(20000);
    }
    mainState = 1;
    mainCallback = nullptr;
    mainClientdata = nullptr;
}

static pthread_mutex_t audioMutex = PTHREAD_MUTEX_INITIALIZER; 
static AAudioStream *outputStream = nullptr, *inputStream = nullptr;
static audioProcessingCallback audioCallback = nullptr;
static void *audioClientdata = nullptr, *deviceInputRaw = nullptr;
static float *inputBufs[MAX_MAPPABLE_CHANNELS], *outputBufs[MAX_MAPPABLE_CHANNELS];
static int outputChannelMap[MAX_MAPPABLE_CHANNELS], inputChannelMap[MAX_MAPPABLE_CHANNELS];
static float *deviceInput = nullptr, *deviceOutput = nullptr, *clientInput = nullptr, *clientOutput = nullptr, *channelBuffers = nullptr;
static struct HostTime *timeHandler = nullptr;
static unsigned int requestedNumChannels = 2, deviceInputChannels = 0, deviceOutputChannels = 0, audioSamplerate = 0;
static int framesPerBurst = 0, bufferSizeFrames = 0, maxBufferSizeFrames = 0, previousXRuns = 0, drainCallbacks = 0, cushionCallbacks = 0;
static ASIOSampleType inputSampleType = ASIOSTFloat32LSB, outputSampleType = ASIOSTFloat32LSB;
static int inputBytesPerSample = 4, outputBytesPerSample = 4;
static volatile int restartState = 0;
static bool audioInterleaved = true, audioInputEnabled = false, audioOutputEnabled = true;
static volatile bool channelMapActive = false;

static unsigned long long int getTime(AAudioStream *stream, unsigned int numFrames) {
    if (timeHandler) {
        int64_t framePosition = 0, nanoseconds = 0;
        if (AAudioStream_getTimestamp(stream, CLOCK_MONOTONIC, &framePosition, &nanoseconds) == AAUDIO_OK) return timeHandler->getFromPosition((double)framePosition, numFrames);
        return timeHandler->getFromFrames(numFrames);
    }
    return (unsigned long long int)std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

static void applyChannelMap(float *input, float *output, unsigned int inputChannels, unsigned int outputChannels, int frames, unsigned int channelsToMap, const int *sourceMap, const int *destinationMap, float **sourcePlanes = nullptr, float **destinationPlanes = nullptr) {
    if (destinationPlanes) for (unsigned int ch = 0; ch < outputChannels; ch++) memset(destinationPlanes[ch], 0, (size_t)frames * sizeof(float));
    else memset(output, 0, (size_t)frames * outputChannels * sizeof(float));
    float *from[MAX_MAPPABLE_CHANNELS], *to[MAX_MAPPABLE_CHANNELS];
    unsigned int routeCount = 0, inputStride = sourcePlanes ? 1 : inputChannels, outputStride = destinationPlanes ? 1 : outputChannels;
    if (channelsToMap > MAX_MAPPABLE_CHANNELS) channelsToMap = MAX_MAPPABLE_CHANNELS;
    for (unsigned int ch = 0; ch < channelsToMap; ch++) {
        int source = sourceMap ? sourceMap[ch] : (int)ch, destination = destinationMap ? destinationMap[ch] : (int)ch;
        if ((source < 0) || ((unsigned int)source >= inputChannels) || (destination < 0) || ((unsigned int)destination >= outputChannels)) continue;
        from[routeCount] = sourcePlanes ? sourcePlanes[source] : input + source;
        to[routeCount++] = destinationPlanes ? destinationPlanes[destination] : output + destination;
    }
    if (!routeCount) return;
    for (int n = 0; n < frames; n++) {
        size_t inputOffset = (size_t)n * inputStride, outputOffset = (size_t)n * outputStride;
        for (unsigned int ch = 0; ch < routeCount; ch++) to[ch][outputOffset] = from[ch][inputOffset];
    }
}

static void streamErrorCallback(AAudioStream *stream, void *userData, int error);

static float *deviceInputToFloat(const void *raw, int numFrames) {
    if (inputSampleType == ASIOSTFloat32LSB) return (float *)raw;
    toFloat(raw, deviceInput, numFrames * (int)deviceInputChannels, 1, inputSampleType);
    return deviceInput;
}

static void checkBufferSizes(AAudioStream *stream) {
    if ((framesPerBurst < 1) || (bufferSizeFrames >= maxBufferSizeFrames)) return;
    int xRuns = AAudioStream_getXRunCount(stream);
    if (xRuns <= previousXRuns) return; else previousXRuns = xRuns;
    bufferSizeFrames += framesPerBurst;
    if (bufferSizeFrames > maxBufferSizeFrames) bufferSizeFrames = maxBufferSizeFrames;
    AAudioStream_setBufferSizeInFrames(stream, bufferSizeFrames);
}

static bool inputDrainedAndHasFrames(int numFrames) {
    if (drainCallbacks > 0) {
        int framesRead, total = 0;
        do {
            framesRead = AAudioStream_read(inputStream, deviceInputRaw, numFrames, 0);
            if (framesRead > 0) total += framesRead;
            else if (framesRead == AAUDIO_ERROR_DISCONNECTED) { streamErrorCallback(nullptr, nullptr, AAUDIO_ERROR_DISCONNECTED); return false; }
        } while (framesRead > 0); 
        if (total > 0) drainCallbacks--; 
        return false;
    } else if (cushionCallbacks > 0) { cushionCallbacks--; return false; }
    int64_t read = AAudioStream_getFramesRead(inputStream);
    if (read < 0) { streamErrorCallback(nullptr, nullptr, (int)read); return false; }
    int64_t write = AAudioStream_getFramesWritten(inputStream);
    if (write < 0) { streamErrorCallback(nullptr, nullptr, (int)write); return false; }
    if ((write - read) >= numFrames) return true;
    if (AAudioStream_getState(inputStream) == AAUDIO_STREAM_STATE_DISCONNECTED) streamErrorCallback(nullptr, nullptr, AAUDIO_ERROR_DISCONNECTED);
    return false;
}

static int outputAudioCallback(AAudioStream *stream, void *, void *audioData, int numFrames) {
    if ((numFrames < 1) || (numFrames > MAXFRAMES)) {
        if (numFrames > 0) zero(audioData, numFrames * (int)deviceOutputChannels, outputSampleType);
        return AAUDIO_CALLBACK_RESULT_CONTINUE;
    } else checkBufferSizes(stream);
    int framesRead = ((inputStream != nullptr) && inputDrainedAndHasFrames(numFrames)) ? AAudioStream_read(inputStream, deviceInputRaw, numFrames, 0) : 0;
    bool rendered, mapped = channelMapActive, hasInput = framesRead == numFrames;
    if (framesRead == AAUDIO_ERROR_DISCONNECTED) streamErrorCallback(nullptr, nullptr, AAUDIO_ERROR_DISCONNECTED);
    float *output = (outputSampleType == ASIOSTFloat32LSB) ? (float *)audioData : deviceOutput;
    if (audioInterleaved) {
        if (hasInput) applyChannelMap(deviceInputToFloat(deviceInputRaw, numFrames), clientInput, deviceInputChannels, requestedNumChannels, numFrames, requestedNumChannels, mapped ? inputChannelMap : nullptr, nullptr);
        float *inputs[1] = { clientInput }, *outputs[1] = { clientOutput };
        rendered = audioCallback(audioClientdata, hasInput ? inputs : nullptr, outputs, numFrames, audioSamplerate, getTime(stream, numFrames));
        if (rendered) applyChannelMap(clientOutput, output, requestedNumChannels, deviceOutputChannels, numFrames, requestedNumChannels, nullptr, mapped ? outputChannelMap : nullptr);
    } else {
        if (hasInput) applyChannelMap(deviceInputToFloat(deviceInputRaw, numFrames), nullptr, deviceInputChannels, requestedNumChannels, numFrames, requestedNumChannels, mapped ? inputChannelMap : nullptr, nullptr, nullptr, inputBufs);
        rendered = audioCallback(audioClientdata, hasInput ? inputBufs : nullptr, outputBufs, numFrames, audioSamplerate, getTime(stream, numFrames));
        if (rendered) applyChannelMap(nullptr, output, requestedNumChannels, deviceOutputChannels, numFrames, requestedNumChannels, nullptr, mapped ? outputChannelMap : nullptr, outputBufs, nullptr);
    }
    if (!rendered) zero(audioData, numFrames * (int)deviceOutputChannels, outputSampleType);
    else if (output != audioData) fromFloat(output, audioData, numFrames * (int)deviceOutputChannels, 1, outputSampleType);
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

static int inputOnlyCallback(AAudioStream *stream, void *, void *audioData, int numFrames) {
    if ((numFrames < 1) || (numFrames > MAXFRAMES)) return AAUDIO_CALLBACK_RESULT_CONTINUE;
    if (audioInterleaved) {
        applyChannelMap(deviceInputToFloat(audioData, numFrames), clientInput, deviceInputChannels, requestedNumChannels, numFrames, requestedNumChannels, channelMapActive ? inputChannelMap : nullptr, nullptr);
        float *inputs[1] = { clientInput };
        audioCallback(audioClientdata, inputs, nullptr, numFrames, audioSamplerate, getTime(stream, numFrames));
    } else {
        applyChannelMap(deviceInputToFloat(audioData, numFrames), nullptr, deviceInputChannels, requestedNumChannels, numFrames, requestedNumChannels, channelMapActive ? inputChannelMap : nullptr, nullptr, nullptr, inputBufs);
        audioCallback(audioClientdata, inputBufs, nullptr, numFrames, audioSamplerate, getTime(stream, numFrames));
    }
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

static void startLocked(audioProcessingCallback apc, void *clientdata, unsigned int numberOfChannels, bool interleaved, bool inputEnabled, bool outputEnabled);
static void stopLocked();

static void *restartThread(void *reopenAfterError) {
    pthread_mutex_lock(&audioMutex);
    if (audioCallback != nullptr) {
        if (reopenAfterError) startLocked(audioCallback, audioClientdata, requestedNumChannels, audioInterleaved, audioInputEnabled, audioOutputEnabled); else stopLocked();
    }
    pthread_mutex_unlock(&audioMutex);
    restartState = 0;
    pthread_detach(pthread_self());
    pthread_exit(NULL);
}

static void streamErrorCallback(AAudioStream *stream, void *, int error) {
    if (stream && (stream != outputStream) && (stream != inputStream)) return;
    // Oboe workaround for b/173928197: on Android 11, plugging in a headset reports a timeout instead of a disconnect, so a timeout counts as one on exactly that version.
    if ((error == AAUDIO_ERROR_TIMEOUT) && (androidSdkVersion == __ANDROID_API_R__)) error = AAUDIO_ERROR_DISCONNECTED;
    bool disconnected = error == AAUDIO_ERROR_DISCONNECTED;
    __android_log_write(disconnected ? ANDROID_LOG_WARN : ANDROID_LOG_ERROR, "YubyAudioIO", disconnected ? "audio device disconnected, reopening" : "audio stream error, stopping");
    if (__sync_val_compare_and_swap(&restartState, 0, 1) != 0) return;
    pthread_t thread;
    if (pthread_create(&thread, NULL, restartThread, disconnected ? (void *)1 : 0) != 0) restartState = 0;
}

static void stopAndCloseStream(AAudioStream **streamPointer) {
    AAudioStream *stream = *streamPointer;
    if (stream == nullptr) return; else *streamPointer = nullptr; 
    AAudioStream_requestStop(stream); 
    int nextState = 0;
    AAudioStream_waitForStateChange(stream, AAUDIO_STREAM_STATE_STOPPING, &nextState, 100000000); // 100 ms
    if ((AAudioStream_release != nullptr) && (androidSdkVersion > __ANDROID_API_R__)) AAudioStream_release(stream);
    AAudioStream_close(stream);
}

static bool formatToASIOSampleType(int format, ASIOSampleType *sampleType, int *bytesPerSample) {
    switch (format) {
        case AAUDIO_FORMAT_PCM_I16: *sampleType = ASIOSTInt16LSB; *bytesPerSample = 2; return true;
        case AAUDIO_FORMAT_PCM_FLOAT: *sampleType = ASIOSTFloat32LSB; *bytesPerSample = 4; return true;
        case AAUDIO_FORMAT_PCM_I24_PACKED: *sampleType = ASIOSTInt24LSB; *bytesPerSample = 3; return true; 
        case AAUDIO_FORMAT_PCM_I32: *sampleType = ASIOSTInt32LSB; *bytesPerSample = 4; return true;
    }
    return false;
}

static AAudioStream *openStream(int direction, int sampleRate, int bufferCapacity, AAudioStream_dataCallback dataCallback) {
    AAudioStreamBuilder *builder = nullptr;
    if (AAudio_createStreamBuilder(&builder) != AAUDIO_OK) return nullptr;
    AAudioStreamBuilder_setDirection(builder, direction);
    AAudioStreamBuilder_setChannelCount(builder, AAUDIO_UNSPECIFIED); // the device's native channel count
    AAudioStreamBuilder_setFormat(builder, AAUDIO_UNSPECIFIED); // the device's native format, asioformatconverter.h converts it
    AAudioStreamBuilder_setSampleRate(builder, sampleRate); // AAUDIO_UNSPECIFIED uses the device's native rate (lowest latency)
    AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    AAudioStreamBuilder_setDeviceId(builder, AAUDIO_UNSPECIFIED); 
    if (bufferCapacity > 0) AAudioStreamBuilder_setBufferCapacityInFrames(builder, bufferCapacity);

    if (direction == AAUDIO_DIRECTION_OUTPUT) {
        if (AAudioStreamBuilder_setUsage != nullptr) AAudioStreamBuilder_setUsage(builder, AAUDIO_USAGE_MEDIA);
        if (AAudioStreamBuilder_setContentType != nullptr) AAudioStreamBuilder_setContentType(builder, AAUDIO_CONTENT_TYPE_MUSIC);
    } else if (AAudioStreamBuilder_setInputPreset != nullptr) AAudioStreamBuilder_setInputPreset(builder, AAUDIO_INPUT_PRESET_UNPROCESSED); // no AGC or noise suppression
    if (dataCallback != nullptr) {
        AAudioStreamBuilder_setDataCallback(builder, dataCallback, nullptr);
        AAudioStreamBuilder_setErrorCallback(builder, streamErrorCallback, nullptr);
    }

    AAudioStream *stream = nullptr;
    AAudioStreamBuilder_setSharingMode(builder, AAUDIO_SHARING_MODE_EXCLUSIVE);
    if (AAudioStreamBuilder_openStream(builder, &stream) != AAUDIO_OK) {
        stream = nullptr;
        AAudioStreamBuilder_setSharingMode(builder, AAUDIO_SHARING_MODE_SHARED);
        if (AAudioStreamBuilder_openStream(builder, &stream) != AAUDIO_OK) stream = nullptr;
    }
    AAudioStreamBuilder_delete(builder);

    if (stream == nullptr) {
        __android_log_write(ANDROID_LOG_ERROR, "YubyAudioIO", direction == AAUDIO_DIRECTION_OUTPUT ? "can not open the output stream" : "can not open the input stream");
        return nullptr;
    }

    bool isOutput = direction == AAUDIO_DIRECTION_OUTPUT;
    if (!formatToASIOSampleType(AAudioStream_getFormat(stream), isOutput ? &outputSampleType : &inputSampleType, isOutput ? &outputBytesPerSample : &inputBytesPerSample) || (AAudioStream_getChannelCount(stream) < 1)) {
        __android_log_write(ANDROID_LOG_ERROR, "YubyAudioIO", "the stream format can not be converted to float");
        stopAndCloseStream(&stream);
        return nullptr;
    }
    return stream;
}

static void stopLocked() {
    stopAndCloseStream(&outputStream);
    stopAndCloseStream(&inputStream);
    free(deviceInputRaw); deviceInputRaw = nullptr;
    free(deviceInput); deviceInput = nullptr;
    free(deviceOutput); deviceOutput = nullptr;
    free(clientInput); clientInput = nullptr;
    free(clientOutput); clientOutput = nullptr;
    free(channelBuffers); channelBuffers = nullptr;
    if (timeHandler) free(timeHandler); timeHandler = nullptr;
    audioCallback = nullptr;
    audioClientdata = nullptr;
    framesPerBurst = bufferSizeFrames = maxBufferSizeFrames = previousXRuns = 0;
    drainCallbacks = cushionCallbacks = 0;
    deviceInputChannels = deviceOutputChannels = 0;
    inputSampleType = outputSampleType = ASIOSTFloat32LSB;
    inputBytesPerSample = outputBytesPerSample = 4;
    channelMapActive = false;
}

static void mapChannelsLocked() {
    ChannelMap map;
    for (int n = 0; n < MAX_MAPPABLE_CHANNELS; n++) map.inputMap[n] = map.outputMap[n] = -1;
    map.outputDeviceName = map.inputDeviceName = "Audio devices are automatically handled by Android.";
    map.numOutputChannels = deviceOutputChannels; 
    map.numInputChannels = deviceInputChannels;
    if (mainCallback != nullptr) mainCallback(mainClientdata, YubyAudioIOEvent::MapChannels, &map);

    bool hasMapping = false;
    for (int n = 0; n < MAX_MAPPABLE_CHANNELS; n++) if ((map.inputMap[n] != -1) || (map.outputMap[n] != -1)) { hasMapping = true; break; }
    channelMapActive = false; 
    if (!hasMapping) return;
    memcpy(inputChannelMap, map.inputMap, MAX_MAPPABLE_CHANNELS * 4);
    memcpy(outputChannelMap, map.outputMap, MAX_MAPPABLE_CHANNELS * 4);
    channelMapActive = true;
}

static void startLocked(audioProcessingCallback apc, void *clientdata, unsigned int numberOfChannels, bool interleaved, bool inputEnabled, bool outputEnabled) {
    stopLocked();
    if (!apc || (!inputEnabled && !outputEnabled)) return;
    if (AAudio_createStreamBuilder == nullptr) { __android_log_write(ANDROID_LOG_ERROR, "YubyAudioIO", "AAudio is not available, initialize() did not succeed"); return; }
    if (numberOfChannels < 1) numberOfChannels = 1; else if (numberOfChannels > MAX_MAPPABLE_CHANNELS) numberOfChannels = MAX_MAPPABLE_CHANNELS;
    audioCallback = apc;
    audioClientdata = clientdata;
    requestedNumChannels = numberOfChannels;
    audioInterleaved = interleaved;
    audioInputEnabled = inputEnabled;
    audioOutputEnabled = outputEnabled;

    if (outputEnabled) {
        outputStream = openStream(AAUDIO_DIRECTION_OUTPUT, AAUDIO_UNSPECIFIED, 0, outputAudioCallback);
        if (outputStream == nullptr) { stopLocked(); return; } else audioSamplerate = (unsigned int)AAudioStream_getSampleRate(outputStream);
        deviceOutputChannels = (unsigned int)AAudioStream_getChannelCount(outputStream);
        framesPerBurst = AAudioStream_getFramesPerBurst(outputStream);
        maxBufferSizeFrames = AAudioStream_getBufferCapacityInFrames(outputStream);
        previousXRuns = AAudioStream_getXRunCount(outputStream);
        bufferSizeFrames = framesPerBurst * 2;
        if (bufferSizeFrames > maxBufferSizeFrames) bufferSizeFrames = maxBufferSizeFrames;
        AAudioStream_setBufferSizeInFrames(outputStream, bufferSizeFrames);
        if (outputSampleType != ASIOSTFloat32LSB) {
            deviceOutput = (float *)malloc(MAXFRAMES * deviceOutputChannels * sizeof(float));
            if (deviceOutput == nullptr) { stopLocked(); return; }
        }
    }
    if (inputEnabled) {
        inputStream = openStream(AAUDIO_DIRECTION_INPUT, outputStream ? (int)audioSamplerate : AAUDIO_UNSPECIFIED, outputStream ? AAudioStream_getBufferCapacityInFrames(outputStream) * 2 : 0, outputStream ? nullptr : inputOnlyCallback);
        if (inputStream == nullptr) { stopLocked(); return; }
        deviceInputChannels = (unsigned int)AAudioStream_getChannelCount(inputStream);
        AAudioStream_setBufferSizeInFrames(inputStream, AAudioStream_getBufferCapacityInFrames(inputStream));
        if (outputStream == nullptr) audioSamplerate = (unsigned int)AAudioStream_getSampleRate(inputStream);
        else { drainCallbacks = 20; cushionCallbacks = 2; }
        if (inputSampleType != ASIOSTFloat32LSB) {
            deviceInput = (float *)malloc(MAXFRAMES * deviceInputChannels * sizeof(float));
            if (deviceInput == nullptr) { stopLocked(); return; }
        }
        if (outputEnabled) {
            deviceInputRaw = malloc(MAXFRAMES * deviceInputChannels * inputBytesPerSample);
            if (deviceInputRaw == nullptr) { stopLocked(); return; }
        }
    }

    size_t clientBytes = MAXFRAMES * requestedNumChannels * sizeof(float);
    if (interleaved) {
        if (inputEnabled && ((clientInput = (float *)malloc(clientBytes)) == nullptr)) { stopLocked(); return; }
        if (outputEnabled && ((clientOutput = (float *)malloc(clientBytes)) == nullptr)) { stopLocked(); return; }
    } else {
        channelBuffers = (float *)malloc(clientBytes * 2);
        if (channelBuffers == nullptr) { stopLocked(); return; }
        for (unsigned int n = 0; n < requestedNumChannels; n++) {
            inputBufs[n] = channelBuffers + (size_t)n * MAXFRAMES;
            outputBufs[n] = channelBuffers + (size_t)(requestedNumChannels + n) * MAXFRAMES;
        }
    }

    timeHandler = HostTime::create();
    mapChannelsLocked(); 
    if ((inputStream != nullptr) && (AAudioStream_requestStart(inputStream) != AAUDIO_OK)) { stopLocked(); return; }
    if ((outputStream != nullptr) && (AAudioStream_requestStart(outputStream) != AAUDIO_OK)) { stopLocked(); return; }
}

void YubyAudioIO::start(audioProcessingCallback apc, void *clientdata, unsigned int numberOfChannels, bool interleaved, bool inputEnabled, bool outputEnabled) {
    pthread_mutex_lock(&audioMutex);
    startLocked(apc, clientdata, numberOfChannels, interleaved, inputEnabled, outputEnabled);
    pthread_mutex_unlock(&audioMutex);
}

void YubyAudioIO::stop() {
    pthread_mutex_lock(&audioMutex);
    stopLocked();
    pthread_mutex_unlock(&audioMutex);
}

void YubyAudioIO::mapChannels() {
    pthread_mutex_lock(&audioMutex);
    mapChannelsLocked();
    pthread_mutex_unlock(&audioMutex);
}

#endif
