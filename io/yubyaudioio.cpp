#if _WIN32
#include "./yubyaudioio.h"
#include <objbase.h>
#include <initguid.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <iostream>
#include <avrt.h>
#include <functiondiscoverykeys_devpkey.h>
#include <chrono>
#include <cstring>
#include <cwchar>
#include <map>
#include <string>
#include <roapi.h>
#include <windows.security.authorization.appcapabilityaccess.h>
#include <wrl.h>
#define NATIVE_INT64 0
#define IEEE754_64FLOAT 1
#include "./asio.h"
#include "./asioformatconverter.h"
#pragma comment(lib, "avrt.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "windowsapp.lib")

#define ASIOPROCESSSTARTUPTIMEOUTSEC 10
#define ASIOPROCESSSHUTDOWNTIMEOUTSEC 3
#define ASIOPROCESSTERMINATIONTIMEOUTSEC 1

typedef interface IASIO: public IUnknown {
	virtual ASIOBool init(void *sysHandle) = 0;
	virtual void getDriverName(char *name) = 0;	
	virtual long getDriverVersion() = 0;
	virtual void getErrorMessage(char *string) = 0;	
	virtual ASIOError start() = 0;
	virtual ASIOError stop() = 0;
	virtual ASIOError getChannels(long *numInputChannels, long *numOutputChannels) = 0;
	virtual ASIOError getLatencies(long *inputLatency, long *outputLatency) = 0;
	virtual ASIOError getBufferSize(long *minSize, long *maxSize,
		long *preferredSize, long *granularity) = 0;
	virtual ASIOError canSampleRate(ASIOSampleRate sampleRate) = 0;
	virtual ASIOError getSampleRate(ASIOSampleRate *sampleRate) = 0;
	virtual ASIOError setSampleRate(ASIOSampleRate sampleRate) = 0;
	virtual ASIOError getClockSources(ASIOClockSource *clocks, long *numSources) = 0;
	virtual ASIOError setClockSource(long reference) = 0;
	virtual ASIOError getSamplePosition(ASIOSamples *sPos, ASIOTimeStamp *tStamp) = 0;
	virtual ASIOError getChannelInfo(ASIOChannelInfo *info) = 0;
	virtual ASIOError createBuffers(ASIOBufferInfo *bufferInfos, long numChannels,
		long bufferSize, ASIOCallbacks *callbacks) = 0;
	virtual ASIOError disposeBuffers() = 0;
	virtual ASIOError controlPanel() = 0;
	virtual ASIOError future(long selector,void *opt) = 0;
	virtual ASIOError outputReady() = 0;
} IASIO;

static void microSleep(uint64_t microseconds) {
    LARGE_INTEGER frequency, start, now;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&start);
    int64_t waitTicks = (frequency.QuadPart * microseconds) / 1'000'000;
    do { QueryPerformanceCounter(&now); } while ((now.QuadPart - start.QuadPart) < waitTicks);
}

struct HostTime {
public:
    static HostTime *create() {
        HostTime *h = (HostTime *)malloc(sizeof(HostTime));
        if (!h) return NULL; else memset(h, 0, sizeof(HostTime));
        LARGE_INTEGER frequency;
        QueryPerformanceFrequency(&frequency);
        h->ticksToMicroseconds = 1000000.0 / static_cast<double>(frequency.QuadPart);
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
    double times[2048], sX[32], sXX[32], sXY[32], sY[32], ticksToMicroseconds, framePos;
    unsigned int point;
    bool startup;

    unsigned long long int getFromPosition(double sampleTime) {
        LARGE_INTEGER ticks;
        QueryPerformanceCounter(&ticks);
        double h = double(std::llround(ticksToMicroseconds * double(ticks.QuadPart)));
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

struct SharedMemory {
public:
    float inputBuf[MAX_MAPPABLE_CHANNELS * 8192], outputBuf[MAX_MAPPABLE_CHANNELS * 8192];
    int inputMap[MAX_MAPPABLE_CHANNELS], outputMap[MAX_MAPPABLE_CHANNELS];
    unsigned long long int audioTime;
    long numInputs, numOutputs, minBufferSize, maxBufferSize, preferredBufferSize, granularity;
    unsigned int buffersizeFrames, samplerate, preferredBufferSizeMs;

    unsigned int getRequest() { return (unsigned int)InterlockedCompareExchange((volatile LONG *)&requestIndex, 0, 0); }
    unsigned int incRequest() { return (unsigned int)InterlockedIncrement((volatile LONG *)&requestIndex); }
    unsigned int getProvide() { return (unsigned int)InterlockedCompareExchange((volatile LONG *)&provideIndex, 0, 0); }
    void updateProvide() { InterlockedExchange((volatile LONG *)&provideIndex, (LONG)InterlockedCompareExchange((volatile LONG *)&requestIndex, 0, 0)); }
    bool getSilence() { return InterlockedCompareExchange(&silence, 0, 0) != 0; }
    bool getQuit() { return InterlockedCompareExchange(&quit, 0, 0) != 0; }
    bool getStarted() { return InterlockedCompareExchange(&started, 0, 0) != 0; }
    void setQuit(unsigned char value) { InterlockedExchange(&quit, value); }
    void setSilence(bool value) { InterlockedExchange(&silence, value ? 1 : 0); }
    void setStarted() { InterlockedExchange(&started, 1); }

private:
    volatile unsigned int requestIndex, provideIndex;
    volatile LONG silence, quit, started;
};

class ASIOHandler {
public:
    std::wstring name;
    long numInputs, numOutputs;
    bool error;

    ASIOHandler(std::wstring &deviceName, std::wstring &dllPath, std::wstring &clsid, 
        bool inputEnabled_ = false, bool outputEnabled_ = true, unsigned int numberOfChannels = 2, bool interleaved_ = true,
        audioProcessingCallback cb = NULL, void *cd = NULL) :
        name(deviceName), numInputs(0), numOutputs(0), error(true), processHandle(nullptr), sharedHandle(nullptr), audioThreadHandle(nullptr), apc(cb), clientdata(cd),
        shared(nullptr), interleavedInput(nullptr), interleavedOutput(nullptr), audioThreadState(0), requiredNumberOfChannels(numberOfChannels),
        inputEnabled(inputEnabled_), outputEnabled(outputEnabled_), interleaved(interleaved_) {
        if (apc && interleaved) {
            size_t bytes = (size_t)numberOfChannels * 8192 * sizeof(float);
            if (inputEnabled && !(interleavedInput = (float *)malloc(bytes))) return;
            if (outputEnabled && !(interleavedOutput = (float *)malloc(bytes))) return;
        }
        GUID sharedMemoryID;
        wchar_t sharedMemoryIDString[39];
        if (FAILED(CoCreateGuid(&sharedMemoryID)) || !StringFromGUID2(sharedMemoryID, sharedMemoryIDString, 39)) return;
        std::wstring sharedMemoryName = L"Local\\YubyAudioIO.ASIO." + std::wstring(sharedMemoryIDString);
        SetLastError(ERROR_SUCCESS);
        sharedHandle = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(SharedMemory), sharedMemoryName.c_str());
        if (!sharedHandle) return; else if (GetLastError() == ERROR_ALREADY_EXISTS) { CloseHandle(sharedHandle); sharedHandle = nullptr; return; }
        shared = static_cast<SharedMemory *>(MapViewOfFile(sharedHandle, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedMemory)));
        if (!shared) return; else memset(shared, 0, sizeof(SharedMemory));
        for (int n = 0; n < MAX_MAPPABLE_CHANNELS; n++) {
            shared->inputMap[n] = -1;
            shared->outputMap[n] = -1;
        }
        shared->preferredBufferSizeMs = YubyAudioIO::preferredBufferSizeMs;

        wchar_t *executablePathString = nullptr;
        if (_get_wpgmptr(&executablePathString) || !executablePathString) return;
        std::wstring command = L"\"";
        command.append(executablePathString).append(L"\" \"").append(sharedMemoryName).append(L"\" \"").append(dllPath).append(L"\" \"").append(clsid).append(apc ? L"\" \"start\"" : L"\"");
        STARTUPINFOW si = { sizeof(si) };
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION pi = {};
        if (!CreateProcessW(executablePathString, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
            DWORD err = GetLastError();
            LPSTR message = nullptr;
            size_t size = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&message, 0, nullptr);
            OutputDebugStringW(deviceName.c_str()); OutputDebugStringA(" "); OutputDebugStringA(message); OutputDebugStringA("\n\n");
            LocalFree(message);
            return;
        } else processHandle = pi.hProcess;

        if (apc) {
            audioThreadHandle = CreateThread(0, 0, audioThread, this, 0, NULL);
            if (!audioThreadHandle) { CloseHandle(pi.hThread); stopProcess(0, ERROR_OPERATION_ABORTED); return; }
        }

        ULONGLONG startupDeadline = GetTickCount64() + ASIOPROCESSSTARTUPTIMEOUTSEC * 1000;
        while (true) {
            DWORD waitResult = WaitForSingleObject(pi.hProcess, 50);
            if (waitResult == WAIT_OBJECT_0) { // crash or quit
                DWORD exitCode = 0;
                GetExitCodeProcess(pi.hProcess, &exitCode);
                if (!apc && (exitCode == 0)) { error = false; break; } // success, but no start
                std::wstring s = name + L" ASIO agent failed with exit code " + std::to_wstring(exitCode) + L"\n";
                OutputDebugStringW(s.c_str());
                break;
            }
            if (waitResult == WAIT_FAILED) break;
            if (apc && shared->getStarted()) { error = false; break; } // success, agent started
            if (GetTickCount64() >= startupDeadline) break;
        }

        CloseHandle(pi.hThread);
        if (error) stopProcess(0, ERROR_TIMEOUT);
        else {
            numInputs = shared->numInputs;
            numOutputs = shared->numOutputs;
            if (!apc) { CloseHandle(processHandle); processHandle = nullptr; }
        }
    }

    ~ASIOHandler() {
        stopProcess(ASIOPROCESSSHUTDOWNTIMEOUTSEC * 1000, ERROR_TIMEOUT);
        if (shared) UnmapViewOfFile(shared);
        if (sharedHandle) CloseHandle(sharedHandle);
        if (interleavedInput) free(interleavedInput);
        if (interleavedOutput) free(interleavedOutput);
    }

    void applyMap(const ChannelMap &map) {
        if (inputEnabled) memcpy(shared->inputMap, map.inputMap, sizeof(shared->inputMap));
        if (outputEnabled) memcpy(shared->outputMap, map.outputMap, sizeof(shared->outputMap));
    }

    bool isRunning() const { return !error && processHandle && (WaitForSingleObject(processHandle, 0) == WAIT_TIMEOUT); }

private:
    HANDLE processHandle, sharedHandle, audioThreadHandle;
    audioProcessingCallback apc;
    void *clientdata;
    SharedMemory *shared;
    float *interleavedInput, *interleavedOutput;
    volatile LONG audioThreadState;
    unsigned int requiredNumberOfChannels;
    bool inputEnabled, outputEnabled, interleaved;

    void stopProcess(DWORD shutdownTimeoutMs, UINT terminationCode) {
        if (processHandle) {
            if (shared) shared->setQuit(2);
            if (WaitForSingleObject(processHandle, shutdownTimeoutMs) != WAIT_OBJECT_0) {
                TerminateProcess(processHandle, terminationCode);
                WaitForSingleObject(processHandle, ASIOPROCESSTERMINATIONTIMEOUTSEC * 1000);
            }
        }
        if (audioThreadHandle) {
            if (InterlockedCompareExchange(&audioThreadState, 0, 0) != 3) InterlockedExchange(&audioThreadState, 2);
            WaitForSingleObject(audioThreadHandle, INFINITE);
            CloseHandle(audioThreadHandle);
            audioThreadHandle = nullptr;
        }
        if (processHandle) {
            CloseHandle(processHandle);
            processHandle = nullptr;
        }
    }

    static unsigned long WINAPI audioThread(HANDLE param) {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
        DWORD taskIndex = 0;
        HANDLE task = AvSetMmThreadCharacteristicsA("Pro Audio", &taskIndex);
        ((ASIOHandler *)param)->audioThreadFunction();
        if (task) AvRevertMmThreadCharacteristics(task);
        return 0;
    }

    void audioThreadFunction() {
        if (InterlockedCompareExchange(&audioThreadState, 1, 0) != 0) return;
        float *inputBufs[MAX_MAPPABLE_CHANNELS], *outputBufs[MAX_MAPPABLE_CHANNELS];
        for (int n = 0; n < MAX_MAPPABLE_CHANNELS; n++) {
            inputBufs[n] = shared->inputBuf + n * 8192;
            outputBufs[n] = shared->outputBuf + n * 8192;
        }
        while (InterlockedCompareExchange(&audioThreadState, 0, 0) == 1) {
            if (processHandle && (WaitForSingleObject(processHandle, 0) == WAIT_OBJECT_0)) {
                if (shared && !shared->getQuit()) shared->setQuit(1);
                InterlockedExchange(&audioThreadState, 3);
                return;
            }
            if (shared->getRequest() == shared->getProvide()) { microSleep(50); continue; }
            if (!shared->getQuit()) {
                shared->preferredBufferSizeMs = YubyAudioIO::preferredBufferSizeMs;
                if (interleaved) {
                    float *input = inputEnabled ? interleavedInput : NULL, *output = outputEnabled ? interleavedOutput : NULL;
                    if (input) for (unsigned int frame = 0; frame < shared->buffersizeFrames; frame++) {
                        for (unsigned int channel = 0; channel < requiredNumberOfChannels; channel++) input[(size_t)frame * requiredNumberOfChannels + channel] = inputBufs[channel][frame];
                    }
                    bool rendered = apc(clientdata, input ? &input : NULL, output ? &output : NULL, shared->buffersizeFrames, shared->samplerate, shared->audioTime);
                    shared->setSilence(!output || !rendered);
                    if (output && rendered) for (unsigned int frame = 0; frame < shared->buffersizeFrames; frame++) {
                        for (unsigned int channel = 0; channel < requiredNumberOfChannels; channel++) outputBufs[channel][frame] = output[(size_t)frame * requiredNumberOfChannels + channel];
                    }
                } else {
                    bool rendered = apc(clientdata, inputEnabled ? inputBufs : NULL, outputEnabled ? outputBufs : NULL, shared->buffersizeFrames, shared->samplerate, shared->audioTime);
                    shared->setSilence(!outputEnabled || !rendered);
                }
            }
            shared->updateProvide();
        }
        InterlockedExchange(&audioThreadState, 3);
    }
};

class ASIOProcess {
public:
    ASIOProcess() : dll(nullptr), classFactory(nullptr), asio(nullptr), bufferInfos(nullptr), channelTypes(nullptr), hasOutputReady(false), lastPreferredBufferSizeMs(0), sharedHandle(nullptr), shared(NULL), state(initialized), hostTime(HostTime::create()) {
        singleton = this;
        callbacks.bufferSwitch = onBufferSwitch;
        callbacks.bufferSwitchTimeInfo = onBufferSwitchTimeInfo;
        callbacks.sampleRateDidChange = onSampleRateDidChange;
        callbacks.asioMessage = onAsioMessage;
    }

    int main(int argc, wchar_t *argv[]) {
        if (!hostTime) return 3;

        sharedHandle = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, argv[1]);
        if (!sharedHandle) return 1;
        shared = static_cast<SharedMemory *>(MapViewOfFile(sharedHandle, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedMemory)));
        if (!shared) return 2;

        std::wstring dllPath = argv[2], clsid = argv[3];
        int r = initASIODevice(dllPath, clsid);
        if (r != 0) return 1000 + r;

        if (argc == 5) {
            r = startASIODevice();
            if (r != 0) return 2000 + r; else shared->setStarted();
        } else return 0;

        while (!shared->getQuit()) Sleep(50);
        return 0;
    }

    ~ASIOProcess() {
        if (asio) {
            if (state == running) asio->stop();
            if (state >= prepared) asio->disposeBuffers();
            asio->Release();
            asio = nullptr;
        }
        if (bufferInfos) delete[] bufferInfos;
        if (channelTypes) delete[] channelTypes;
        if (classFactory) classFactory->Release();
        if (dll) FreeLibrary(dll);
        if (shared) UnmapViewOfFile(shared);
        if (sharedHandle) CloseHandle(sharedHandle);
        if (hostTime) free(hostTime);
    }

private:
    static ASIOProcess *singleton;
    ASIOCallbacks callbacks;
    HMODULE dll;
    HANDLE sharedHandle;
    SharedMemory *shared;
    IClassFactory *classFactory;
    IASIO *asio;
    ASIOBufferInfo *bufferInfos;
    ASIOSampleType *channelTypes;
    HostTime *hostTime;
    enum { initialized = 1, prepared = 2, running = 3 } state;
    unsigned char lastPreferredBufferSizeMs;
    bool hasOutputReady;

    unsigned int calculateBufferSizeFrames() {
        long s = long((shared->preferredBufferSizeMs * (double)shared->samplerate / 1000.0) + 0.5);
        if (s > shared->maxBufferSize) s = shared->maxBufferSize; else if (s < shared->minBufferSize) s = shared->minBufferSize;
        if (shared->granularity == -1) {
            if ((shared->minBufferSize <= 0) || (shared->maxBufferSize < shared->minBufferSize)) return shared->preferredBufferSize;
            long size = shared->minBufferSize;
            while (size <= shared->maxBufferSize / 2) {
                long next = size * 2;
                if (next >= s) return (s - size < next - s) ? (unsigned int)size : (unsigned int)next;
                size = next;
            }
            return (unsigned int)size;
        }
        if (shared->granularity == 0) shared->granularity = 128;
        s = (s - shared->minBufferSize + shared->granularity / 2) / shared->granularity; // times
        s = shared->minBufferSize + s * shared->granularity;
        return (unsigned int)(s > shared->maxBufferSize ? shared->maxBufferSize : s);
    }

    int initASIODevice(std::wstring &dllPath, std::wstring &clsid) {
        CLSID classId;
        if (CLSIDFromString(clsid.c_str(), &classId) != S_OK) return 1; else dll = LoadLibraryW(dllPath.c_str());
        if (!dll) return 2;
        typedef HRESULT (STDAPICALLTYPE *DllGetClassObjectFunction)(REFCLSID rclsid, REFIID riid, LPVOID *ppv);
        DllGetClassObjectFunction DllGetClassObject = (DllGetClassObjectFunction)GetProcAddress(dll, "DllGetClassObject");
        if (!DllGetClassObject) return 3;
        if (FAILED(DllGetClassObject(classId, IID_IClassFactory, (void**)&classFactory))) classFactory = nullptr;
        if (!classFactory) return 4;
        if (FAILED(classFactory->CreateInstance(nullptr, classId, (void**)&asio))) asio = nullptr;
        const IID IID_IASIO = { 0x232685c6, 0x6548, 0x49d6, { 0xad, 0x24, 0x39, 0x53, 0xf9, 0x3a, 0x72, 0xd1 } };
        if (!asio && FAILED(classFactory->CreateInstance(nullptr, IID_IASIO, (void**)&asio))) asio = nullptr;
        if (!asio) return 5;
        if (asio->init(GetDesktopWindow()) != ASIOTrue) return 6;
        if (asio->getChannels(&shared->numInputs, &shared->numOutputs) != ASE_OK) return 7;
        if ((shared->numInputs < 0) || (shared->numOutputs < 0) || (!shared->numInputs && !shared->numOutputs) || (shared->numInputs > INT_MAX - shared->numOutputs)) return 8;
        hasOutputReady = (asio->outputReady() == 0);
        return 0;
    }

    int startASIODevice() {
        lastPreferredBufferSizeMs = shared->preferredBufferSizeMs;
        shared->samplerate = 44100;
        ASIOSampleRate currentSamplerate;
        asio->setSampleRate(shared->samplerate);
        if (asio->getSampleRate(&currentSamplerate) != ASE_OK) return 1; else shared->samplerate = (unsigned int)currentSamplerate;
        if (asio->getBufferSize(&shared->minBufferSize, &shared->maxBufferSize, &shared->preferredBufferSize, &shared->granularity) != ASE_OK) return 2;
        shared->buffersizeFrames = calculateBufferSizeFrames();
        if (shared->buffersizeFrames > 8192) return 3;

        const int totalNumChannels = shared->numInputs + shared->numOutputs;
        bufferInfos = new ASIOBufferInfo[totalNumChannels];
        channelTypes = new ASIOSampleType[totalNumChannels];
        for (int n = 0; n < totalNumChannels; n++) {
            bufferInfos[n].isInput = (n < shared->numInputs) ? ASIOTrue : ASIOFalse;
            bufferInfos[n].channelNum = n < shared->numInputs ? n : (n - shared->numInputs);
            bufferInfos[n].buffers[0] = bufferInfos[n].buffers[1] = nullptr;
            ASIOChannelInfo channelInfo = {};
            channelInfo.channel = bufferInfos[n].channelNum;
            channelInfo.isInput = bufferInfos[n].isInput;
            if (asio->getChannelInfo(&channelInfo) != ASE_OK) {
                delete[] bufferInfos; bufferInfos = nullptr;
                delete[] channelTypes; channelTypes = nullptr;
                return 4;
            } else channelTypes[n] = channelInfo.type;
        }

        if (asio->createBuffers(bufferInfos, totalNumChannels, shared->buffersizeFrames, &callbacks) != ASE_OK) return 5; else state = prepared;
        if (asio->start() != ASE_OK) return 6; else state = running;
        return 0;
    }

    void process(ASIOTime *timeInfo, long doubleBufferIndex, ASIOBool processNow) {
        struct ProAudioHandle {
            HANDLE handle;
            ~ProAudioHandle() { if (handle) AvRevertMmThreadCharacteristics(handle); }
            void set() {
                SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
                if (handle) return;
                DWORD taskIndex = 0;
                handle = AvSetMmThreadCharacteristicsA("Pro Audio", &taskIndex);
            }
        };
        static thread_local ProAudioHandle proAudio = { NULL };
        if (shared->getQuit()) return;
        if (lastPreferredBufferSizeMs != shared->preferredBufferSizeMs) {
            lastPreferredBufferSizeMs = shared->preferredBufferSizeMs;
            if (calculateBufferSizeFrames() != shared->buffersizeFrames) { invalidate(); return; }
        }
        if (shared->getRequest() == 5) proAudio.set();

        if (timeInfo == NULL) {
            ASIOTime t{};
            if (asio->getSamplePosition(&t.timeInfo.samplePosition, &t.timeInfo.systemTime) == ASE_OK) shared->audioTime = hostTime->getFromPosition(asioSamplesToDouble(t.timeInfo.samplePosition), shared->buffersizeFrames);
            else shared->audioTime = hostTime->getFromFrames(shared->buffersizeFrames);
        } else if (timeInfo->timeInfo.flags & kSamplePositionValid) shared->audioTime = hostTime->getFromPosition(asioSamplesToDouble(timeInfo->timeInfo.samplePosition), shared->buffersizeFrames);
        else shared->audioTime = hostTime->getFromFrames(shared->buffersizeFrames);

        memset(shared->inputBuf, 0, shared->buffersizeFrames * MAX_MAPPABLE_CHANNELS * 4);
        for (int n = 0; n < MAX_MAPPABLE_CHANNELS; n++) {
            int volatile index = shared->inputMap[n];
            if ((index >= 0) && (index < shared->numInputs)) toFloat(bufferInfos[index].buffers[doubleBufferIndex], shared->inputBuf + n * 8192, shared->buffersizeFrames, 1, channelTypes[index]);
        }

        unsigned int requestIndex = shared->incRequest();
        while (shared->getProvide() != requestIndex) { if (shared->getQuit()) return; else microSleep(50); }
        bool silence = shared->getSilence();
        for (int n = 0; n < shared->numOutputs; n++) {
            int index = -1;
            if (!silence) for (int internal = 0; internal < MAX_MAPPABLE_CHANNELS; internal++) if (shared->outputMap[internal] == n) {
                index = internal;
                break;
            }
            if (index >= 0) fromFloat(shared->outputBuf + index * 8192, bufferInfos[n + shared->numInputs].buffers[doubleBufferIndex], shared->buffersizeFrames, 1, channelTypes[n + shared->numInputs]);
            else zero(bufferInfos[n + shared->numInputs].buffers[doubleBufferIndex], shared->buffersizeFrames, channelTypes[n + shared->numInputs]);
        }

        if (hasOutputReady) asio->outputReady();
    }

    void invalidate() { if (!shared->getQuit()) shared->setQuit(1); }
    static inline double asioSamplesToDouble(const ASIOSamples &samples) { return samples.lo + samples.hi * std::pow(2, 32); }
    static void onBufferSwitch(long doubleBufferIndex, ASIOBool processNow) { singleton->process(NULL, doubleBufferIndex, processNow); }
    static ASIOTime *onBufferSwitchTimeInfo(ASIOTime *timeInfo, long doubleBufferIndex, ASIOBool processNow) { singleton->process(timeInfo, doubleBufferIndex, processNow); return {}; }
    static void onSampleRateDidChange(ASIOSampleRate sRate) { singleton->invalidate(); }

    static long onAsioMessage(long selector, long value, void* message, double *opt) {
        if (singleton->shared->getQuit()) return 0;
        switch (selector) {
            case kAsioSelectorSupported:
                switch (value) {
                    case kAsioSelectorSupported:
                    case kAsioEngineVersion:
                    case kAsioResetRequest:
                    case kAsioBufferSizeChange:
                    case kAsioResyncRequest:
                    case kAsioLatenciesChanged:
                    case kAsioOverload: return 1;
                    default: return 0;
                }
                break;
            case kAsioEngineVersion: return 2;
            case kAsioBufferSizeChange:
            case kAsioResyncRequest:
            case kAsioResetRequest: singleton->invalidate(); return 1;
            case kAsioOverload:
            case kAsioLatenciesChanged: return 1;
            default: return 0;
        }
    }
};
ASIOProcess *ASIOProcess::singleton = NULL;

class InputToOutputBridge {
public:
    static InputToOutputBridge *create(int inChannels, int outSamplerate, int capacityFrames) {
        InputToOutputBridge *b = new InputToOutputBridge(inChannels, outSamplerate, capacityFrames);
        if (!b->inputBuffer || !b->prev || !b->resampleBuffer || !b->outputBuffer) { delete b; return NULL; } else return b;
    }

    ~InputToOutputBridge() {
        if (prev) _aligned_free(prev);
        if (inputBuffer) _aligned_free(inputBuffer);
        if (resampleBuffer) _aligned_free(resampleBuffer);
        if (outputBuffer) _aligned_free(outputBuffer);
    }

    void push(float *input, int numFrames, int samplerate) {
        if (!input || (numFrames <= 0)) return;
        if (samplerate != outputSamplerate) { // Resampling if needed.
            numFrames = resample(input, resampleBuffer, float(samplerate) / float(outputSamplerate), numFrames);
            input = resampleBuffer;
		}
        if (numFrames > capacity) { 
            input += (numFrames - capacity) * inputNumberOfChannels; 
            numFrames = capacity; 
        }
        if (numFrames > capacity - available) {
            readPos = (readPos + numFrames - (capacity - available)) % capacity;
            available = capacity - numFrames;
        }
        available += numFrames;
        int spaceLeft = capacity - writePos;
        if (spaceLeft < numFrames) { // End of buffer.
            memcpy(inputBuffer + writePos * inputNumberOfChannels, input, spaceLeft * inputNumberOfChannels * 4);
            input += spaceLeft * inputNumberOfChannels;
            numFrames -= spaceLeft;
            writePos = 0;
        }
        if (input) memcpy(inputBuffer + writePos * inputNumberOfChannels, input, numFrames * inputNumberOfChannels * 4);
        writePos = (writePos + numFrames) % capacity;
    }

    float *pull(int numFrames) {
        int minimumInputFramesShouldBe = afterUnderrun ? (numFrames + (numFrames >> 1)) : numFrames;
        if (available < minimumInputFramesShouldBe) { // Underrun, not enough audio input frames are available.
            afterUnderrun = true;
            memset(outputBuffer, 0, numFrames * 4 * inputNumberOfChannels);
            return outputBuffer;
        }
        afterUnderrun = false;
        available -= numFrames;
        int spaceLeft = capacity - readPos;
        if (spaceLeft >= numFrames) {
            float *r = inputBuffer + readPos * inputNumberOfChannels;
            readPos = (readPos + numFrames) % capacity;
            return r;
        }
        memcpy(outputBuffer, inputBuffer + readPos * inputNumberOfChannels, spaceLeft * inputNumberOfChannels * 4);
        numFrames -= spaceLeft;
        readPos = 0;
        memcpy(outputBuffer + spaceLeft * inputNumberOfChannels, inputBuffer + readPos * inputNumberOfChannels, numFrames * inputNumberOfChannels * 4);
        readPos = (readPos + numFrames) % capacity;
        return outputBuffer;
    }
    
private:
    float *inputBuffer, *resampleBuffer, *outputBuffer, *prev;
    float slopeCount, invSlopeCount;
    int outputSamplerate, capacity, available, inputNumberOfChannels, readPos, writePos;
    bool afterUnderrun;

    InputToOutputBridge(int inChannels, int outSamplerate, int capacityFrames) :
    outputSamplerate(outSamplerate), capacity(capacityFrames), available(0), inputNumberOfChannels(inChannels),
    slopeCount(0), invSlopeCount(1.0f), afterUnderrun(true), writePos(0), readPos(0) {
        inputBuffer = (float*)_aligned_malloc((size_t)inputNumberOfChannels * 4 * capacity, 16);
        prev = (float*)_aligned_malloc(inputNumberOfChannels * 4, 16);
        resampleBuffer = (float*)_aligned_malloc((size_t)inputNumberOfChannels * 4 * capacity, 16);
        outputBuffer = (float *)_aligned_malloc((size_t)inputNumberOfChannels * 4 * capacity, 16);
        if (prev) memset(prev, 0, inputNumberOfChannels * 4);
    }
    
    int resample(float *input, float *output, float rate, int numFrames) {
        int outFrames = 0;
        while (true) {
            while (slopeCount > 1.0f) {
                numFrames--;
                slopeCount -= 1.0f;
                invSlopeCount = 1.0f - slopeCount;
                if (!numFrames) return outFrames;
                memcpy(prev, input, inputNumberOfChannels * 4);
                input += inputNumberOfChannels;
            }
            for (int ch = 0; ch < inputNumberOfChannels; ch++) *output++ = invSlopeCount * prev[ch] + slopeCount * input[ch];
            memcpy(prev, input, inputNumberOfChannels * 4);
            outFrames++;
            slopeCount += rate;
            invSlopeCount = 1.0f - slopeCount;
        }
    }
};

#define WASAPI_CHECK(call) do { \
    HRESULT result = (call); \
    if (FAILED(result)) { \
        char message[256]; \
        _snprintf_s(message, sizeof(message), "WASAPI failure at line %d: returned 0x%08lx\n", __LINE__, (unsigned long)result); \
        OutputDebugStringA(message); \
        return result; \
    } \
} while (false)

struct Endpoint {
public:
    IMMDevice *device;
    IAudioClient3 *client;
    IAudioCaptureClient *capture;
    IAudioRenderClient *render;
    HANDLE event;
    WAVEFORMATEXTENSIBLE format;
    ASIOSampleType sampleType;
    unsigned int frames;
    float *samples, *converted;

    Endpoint() : device(NULL), client(NULL), capture(NULL), render(NULL), event(NULL), sampleType(ASIOSTFloat32LSB), frames(0), samples(NULL), converted(NULL) { memset(&format, 0, sizeof(WAVEFORMATEXTENSIBLE)); }
    
    void reset() {
        if (client) client->Stop();
        if (capture) { capture->Release(); capture = NULL; }
        if (render) { render->Release(); render = NULL; }
        if (client) { client->Release(); client = NULL; }
        if (event) { CloseHandle(event); event = NULL; }
        if (samples) { _aligned_free(samples); samples = NULL; }
        if (converted) { _aligned_free(converted); converted = NULL; }
        frames = 0;
    }

    HRESULT prepare(bool isInput, unsigned int requiredNumberOfChannels, unsigned int callbackFrames = 0) {
        if (!getSampleType(&sampleType)) return AUDCLNT_E_UNSUPPORTED_FORMAT;
        if (!frames || (frames > (unsigned int)INT_MAX / format.Format.nChannels) || (frames > UINT_MAX / requiredNumberOfChannels)) return E_INVALIDARG;
        if (callbackFrames < frames) callbackFrames = frames;
        if (callbackFrames > UINT_MAX / requiredNumberOfChannels) return E_INVALIDARG;
        samples = (float *)_aligned_malloc((size_t)callbackFrames * requiredNumberOfChannels * sizeof(float), 16);
        converted = (float *)_aligned_malloc((size_t)frames * format.Format.nChannels * sizeof(float), 16);
        if (!samples || !converted) return E_OUTOFMEMORY; else event = CreateEvent(NULL, FALSE, FALSE, NULL);
        if (!event) return HRESULT_FROM_WIN32(GetLastError());
        WASAPI_CHECK(client->SetEventHandle(event));
        return isInput ? client->GetService(__uuidof(IAudioCaptureClient), (void **)&capture) : client->GetService(__uuidof(IAudioRenderClient), (void **)&render);
    }

    bool matchingFormats(Endpoint *other) {
        return (sampleType == other->sampleType) && (format.Format.nSamplesPerSec == other->format.Format.nSamplesPerSec) && (format.Format.nChannels == other->format.Format.nChannels) && (channelMask(format) == channelMask(other->format));
    }

    HRESULT getExclusiveMinimumPeriod(const WAVEFORMATEXTENSIBLE *candidate, REFERENCE_TIME *period) {
        if (!device) return S_OK;
        WORD channels = format.Format.nChannels;
        DWORD mask = channelMask(format);
        format = *candidate;
        if (format.Format.wFormatTag != WAVE_FORMAT_EXTENSIBLE) {
            format.SubFormat = format.Format.wFormatTag == WAVE_FORMAT_PCM ? KSDATAFORMAT_SUBTYPE_PCM : KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
            format.Samples.wValidBitsPerSample = format.Format.wBitsPerSample;
        }
        format.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
        format.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
        format.Format.nChannels = channels;
        format.dwChannelMask = mask;
        unsigned int blockAlign = channels * (format.Format.wBitsPerSample / 8u);
        if (!blockAlign || (blockAlign > USHRT_MAX) || (format.Format.nSamplesPerSec > UINT_MAX / blockAlign)) return AUDCLNT_E_UNSUPPORTED_FORMAT;
        format.Format.nBlockAlign = (WORD)blockAlign;
        format.Format.nAvgBytesPerSec = format.Format.nSamplesPerSec * blockAlign;
        if (!client) WASAPI_CHECK(activate());
        HRESULT hr = client->IsFormatSupported(AUDCLNT_SHAREMODE_EXCLUSIVE, &format.Format, NULL);
        if (hr != S_OK) return FAILED(hr) ? hr : AUDCLNT_E_UNSUPPORTED_FORMAT;
        REFERENCE_TIME defaultPeriod, minimumPeriod;
        WASAPI_CHECK(client->GetDevicePeriod(&defaultPeriod, &minimumPeriod));
        if (minimumPeriod > *period) *period = minimumPeriod;
        return S_OK;
    }

    HRESULT exclusiveInitialize(REFERENCE_TIME period, bool *aligned) {
        if (!device) return S_OK; else if (!client) WASAPI_CHECK(activate());
        HRESULT hr = client->Initialize(AUDCLNT_SHAREMODE_EXCLUSIVE, AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_NOPERSIST, period, period, &format.Format, NULL);
        if (FAILED(hr) && (hr != AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED)) return hr;
        WASAPI_CHECK(client->GetBufferSize(&frames));
        if ((hr == AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED) || (frames < 128) || (frames & 7u)) *aligned = false;
        return S_OK;
    }

    HRESULT openShared() {
        if (!client) WASAPI_CHECK(activate());
        WASAPI_CHECK(getMixFormat());
        unsigned int defaultPeriod, fundamentalPeriod, minimumPeriod, maximumPeriod;
        WASAPI_CHECK(client->GetSharedModeEnginePeriod(&format.Format, &defaultPeriod, &fundamentalPeriod, &minimumPeriod, &maximumPeriod));
        WASAPI_CHECK(client->InitializeSharedAudioStream(AUDCLNT_STREAMFLAGS_EVENTCALLBACK, defaultPeriod, &format.Format, NULL));
        WASAPI_CHECK(client->GetBufferSize(&frames));
        return S_OK;
    }

    bool isValid() { return device && SUCCEEDED(activate()) && SUCCEEDED(getDeviceFormat()); }

private:

    static DWORD channelMask(const WAVEFORMATEXTENSIBLE &format) {
        if ((format.Format.wFormatTag == WAVE_FORMAT_EXTENSIBLE) && format.dwChannelMask) return format.dwChannelMask;
        return (format.Format.nChannels == 1) ? SPEAKER_FRONT_CENTER : (format.Format.nChannels == 2) ? SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT : 0;
    }

    HRESULT activate() {
        reset();
        return device->Activate(__uuidof(IAudioClient3), CLSCTX_ALL, NULL, (void **)&client);
    }

    HRESULT getMixFormat() {
        WAVEFORMATEX *format = NULL;
        WASAPI_CHECK(client->GetMixFormat(&format));
        HRESULT hr = copyFormat(format);
        CoTaskMemFree(format);
        return hr;
    }

    HRESULT getDeviceFormat() {
        IPropertyStore *store = NULL;
        PROPVARIANT prop;
        PropVariantInit(&prop);
        HRESULT hr = device->OpenPropertyStore(STGM_READ, &store);
        if (SUCCEEDED(hr)) hr = store->GetValue(PKEY_AudioEngine_DeviceFormat, &prop);
        if (SUCCEEDED(hr)) {
            hr = E_INVALIDARG;
            if ((prop.vt == VT_BLOB) && prop.blob.pBlobData && (prop.blob.cbSize >= sizeof(WAVEFORMATEX))) {
                WAVEFORMATEX header;
                memcpy(&header, prop.blob.pBlobData, sizeof(header));
                if (sizeof(header) + header.cbSize <= prop.blob.cbSize) hr = copyFormat((const WAVEFORMATEX *)prop.blob.pBlobData);
            }
        }
        PropVariantClear(&prop);
        if (store) store->Release();
        return FAILED(hr) ? getMixFormat() : hr;
    }
    
    HRESULT copyFormat(const WAVEFORMATEX *source) {
        memset(&format, 0, sizeof(WAVEFORMATEXTENSIBLE));
        if (source->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
            if (source->cbSize < sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX)) return E_INVALIDARG;
            memcpy(&format, source, sizeof(WAVEFORMATEXTENSIBLE));
            format.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
        } else {
            format.Format = *source;
            format.Format.cbSize = 0;
        }
        return getSampleType() ? S_OK : AUDCLNT_E_UNSUPPORTED_FORMAT;
    }

    bool getSampleType(ASIOSampleType *t = NULL) {
        bool pcm, ieeeFloat;
        unsigned int validBits = format.Format.wBitsPerSample;
        if (!format.Format.nChannels || !format.Format.nSamplesPerSec || (format.Format.nSamplesPerSec > INT_MAX)) return unsupported("invalid channels or sample rate", validBits);
        if (format.Format.wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
            if (format.Format.cbSize < (sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX))) return unsupported("invalid extensible header", validBits);
            pcm = format.SubFormat == KSDATAFORMAT_SUBTYPE_PCM;
            ieeeFloat = format.SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
            if (format.Samples.wValidBitsPerSample) validBits = format.Samples.wValidBitsPerSample;
        } else {
            pcm = format.Format.wFormatTag == WAVE_FORMAT_PCM;
            ieeeFloat = format.Format.wFormatTag == WAVE_FORMAT_IEEE_FLOAT;
        }
        if (validBits != format.Format.wBitsPerSample) return unsupported("valid bits differ from container bits", validBits);
        if (format.Format.nBlockAlign != format.Format.nChannels * (format.Format.wBitsPerSample >> 3)) return unsupported("unexpected block alignment", validBits);
        if (pcm) switch (format.Format.wBitsPerSample) {
            case 16: if (t) *t = ASIOSTInt16LSB; return true;
            case 24: if (t) *t = ASIOSTInt24LSB; return true;
            case 32: if (t) *t = ASIOSTInt32LSB; return true;
            default: return unsupported("unsupported PCM container size", validBits); // 8-bit PCM is unsupported
        }
        if (ieeeFloat) switch (format.Format.wBitsPerSample) {
            case 32: if (t) *t = ASIOSTFloat32LSB; return true;
            case 64: if (t) *t = ASIOSTFloat64LSB; return true;
            default: return unsupported("unsupported floating-point container size", validBits);
        }
        return unsupported("unsupported subformat", validBits);
    }

    bool unsupported(const char *reason, unsigned int validBits) {
        char message[256];
        _snprintf_s(message, sizeof(message), _TRUNCATE, "WASAPI unsupported format (%s): tag=%u, channels=%u, rate=%lu, containerBits=%u, validBits=%u, blockAlign=%u, subformat=0x%08lx\n", reason, format.Format.wFormatTag, format.Format.nChannels, (unsigned long)format.Format.nSamplesPerSec, format.Format.wBitsPerSample, validBits, format.Format.nBlockAlign, (unsigned long)format.SubFormat.Data1);
        OutputDebugStringA(message);
        return false;
    }
};

class WASAPIHandler {
public:
    std::wstring inputDeviceID, outputDeviceID;
    HANDLE thread;
    char *inputDeviceName, *outputDeviceName;
    unsigned int mapInputChannels, mapOutputChannels;
    bool error;

    WASAPIHandler(bool inputEnabled, bool outputEnabled, unsigned int requiredNumberOfChannels, bool interleaved, audioProcessingCallback cb, void *cd, const std::wstring &selectedInputDeviceKey_, const std::wstring &selectedOutputDeviceKey_) :
    error(true), thread(NULL), stopEvent(CreateEvent(NULL, TRUE, FALSE, NULL)), startedEvent(CreateEvent(NULL, TRUE, FALSE, NULL)), ioHandler(NULL), inputBuffer(NULL), planarSamples(NULL), planarBuffers(NULL), planarFrames(0), hostTime(NULL),
    inputEnabled(inputEnabled), outputEnabled(outputEnabled), requiredNumberOfChannels(requiredNumberOfChannels),
    callback(cb), clientdata(cd), pendingFrames(0), pendingOffset(0), exclusive(false), interleaved(interleaved),
    inputDeviceName(NULL), outputDeviceName(NULL), mapInputChannels(0), mapOutputChannels(0), selectedInputDeviceKey(selectedInputDeviceKey_), selectedOutputDeviceKey(selectedOutputDeviceKey_) {
        if (!stopEvent || !startedEvent) return;
        for (int n = 0; n < MAX_MAPPABLE_CHANNELS; n++) inputMap[n] = outputMap[n] = -1;
        thread = CreateThread(NULL, 0, audioThread, this, 0, NULL);
        if (!thread) return; else WaitForSingleObject(startedEvent, INFINITE);
    }

    ~WASAPIHandler() {
        if (thread) {
            SetEvent(stopEvent);
            WaitForSingleObject(thread, INFINITE);
            CloseHandle(thread);
        }
        if (startedEvent) CloseHandle(startedEvent);
        if (stopEvent) CloseHandle(stopEvent);
        if (inputDeviceName) free(inputDeviceName);
        if (outputDeviceName) free(outputDeviceName);
    }

    void applyMap(const ChannelMap &map) {
        memcpy(inputMap, map.inputMap, sizeof(inputMap));
        memcpy(outputMap, map.outputMap, sizeof(outputMap));
    }

private:
    Endpoint input, output;
    HANDLE stopEvent, startedEvent;
    audioProcessingCallback callback;
    InputToOutputBridge *ioHandler;
    HostTime *hostTime;
    void *clientdata;
    float *inputBuffer, *planarSamples, **planarBuffers;
    int inputMap[MAX_MAPPABLE_CHANNELS], outputMap[MAX_MAPPABLE_CHANNELS];
    unsigned int planarFrames, requiredNumberOfChannels, pendingFrames, pendingOffset;
    bool inputEnabled, outputEnabled, exclusive, interleaved;
    std::wstring selectedInputDeviceKey, selectedOutputDeviceKey;

    static char *getDeviceName(IMMDevice *device) {
        IPropertyStore *store = NULL;
        PROPVARIANT prop;
        PropVariantInit(&prop);
        char *name = NULL;
        if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &store)) && SUCCEEDED(store->GetValue(PKEY_Device_FriendlyName, &prop)) && (prop.vt == VT_LPWSTR) && prop.pwszVal) {
            int bytes = WideCharToMultiByte(CP_UTF8, 0, prop.pwszVal, -1, NULL, 0, NULL, NULL);
            if (bytes > 0) name = (char *)malloc(bytes);
            if (name && !WideCharToMultiByte(CP_UTF8, 0, prop.pwszVal, -1, name, bytes, NULL, NULL)) { free(name); name = NULL; }
        }
        PropVariantClear(&prop);
        if (store) store->Release();
        return name;
    }

    HRESULT checkInputAccess(HRESULT hr) {
        if ((hr != E_ACCESSDENIED) && (hr != HRESULT_FROM_WIN32(ERROR_NOT_FOUND))) return hr; else inputEnabled = false;
        input.reset();
        if (input.device) { input.device->Release(); input.device = NULL; }
        delete ioHandler; ioHandler = NULL;
        if (inputBuffer) { _aligned_free(inputBuffer); inputBuffer = NULL; }
        pendingFrames = pendingOffset = 0;
        return outputEnabled ? S_OK : hr;
    }

    void resetStreams() {
        input.reset();
        output.reset();
        delete ioHandler; ioHandler = NULL;
        if (inputBuffer) { _aligned_free(inputBuffer); inputBuffer = NULL; }
        if (planarSamples) { _aligned_free(planarSamples); planarSamples = NULL; }
        if (planarBuffers) { free(planarBuffers); planarBuffers = NULL; }
        pendingFrames = pendingOffset = 0;
    }

    HRESULT startStreams(bool sameDevice) {
        if (inputEnabled) WASAPI_CHECK(checkInputAccess(input.prepare(true, requiredNumberOfChannels, outputEnabled ? output.frames : 0)));
        if (outputEnabled) WASAPI_CHECK(output.prepare(false, requiredNumberOfChannels));
        if (!interleaved) {
            planarFrames = ((outputEnabled ? output.frames : input.frames) + 3u) & ~3u;
            size_t planes = (size_t)requiredNumberOfChannels * (inputEnabled + outputEnabled);
            if ((planes > size_t(-1) / sizeof(float *)) || (planes > size_t(-1) / sizeof(float) / planarFrames)) return E_OUTOFMEMORY;
            planarBuffers = (float **)malloc(planes * sizeof(float *));
            planarSamples = (float *)_aligned_malloc(planes * planarFrames * sizeof(float), 16);
            if (!planarBuffers || !planarSamples) return E_OUTOFMEMORY;
        }
        if (inputEnabled && outputEnabled) {
            if (!exclusive && (!sameDevice || !input.matchingFormats(&output))) {
                int rate = (int)output.format.Format.nSamplesPerSec;
                unsigned long long int resampled = ((unsigned long long int)input.frames * rate + input.format.Format.nSamplesPerSec - 1) / input.format.Format.nSamplesPerSec + 2;
                unsigned long long int capacity = (unsigned long long int)rate + output.frames + resampled;
                if (capacity > INT_MAX / input.format.Format.nChannels / sizeof(float)) return E_INVALIDARG;
                ioHandler = InputToOutputBridge::create(input.format.Format.nChannels, rate, (int)capacity);
                if (!ioHandler) return E_OUTOFMEMORY;
            } else {
                inputBuffer = (float *)_aligned_malloc((size_t)output.frames * input.format.Format.nChannels * sizeof(float), 16);
                if (!inputBuffer) return E_OUTOFMEMORY;
            }
        }
        if (outputEnabled) {
            BYTE *buffer;
            WASAPI_CHECK(output.render->GetBuffer(output.frames, &buffer));
            WASAPI_CHECK(output.render->ReleaseBuffer(output.frames, AUDCLNT_BUFFERFLAGS_SILENT));
        }
        if (inputEnabled) WASAPI_CHECK(checkInputAccess(input.client->Start()));
        if (outputEnabled) WASAPI_CHECK(output.client->Start());
        return S_OK;
    }

    static bool containerID(IMMDevice *device, GUID *id) {
        IPropertyStore *store = NULL;
        PROPVARIANT prop;
        PropVariantInit(&prop);
        bool valid = SUCCEEDED(device->OpenPropertyStore(STGM_READ, &store)) && SUCCEEDED(store->GetValue(PKEY_Device_ContainerId, &prop)) && (prop.vt == VT_CLSID) && prop.puuid && !IsEqualGUID(*prop.puuid, GUID_NULL);
        if (valid) *id = *prop.puuid;
        PropVariantClear(&prop);
        if (store) store->Release();
        return valid;
    }

    static HRESULT getDeviceId(IMMDevice *device, std::wstring *str) {
        if (!str) return E_POINTER; else if (!device) return S_OK;
        LPWSTR id = NULL;
        HRESULT result = device->GetId(&id);
        if (SUCCEEDED(result)) { if (id) *str = id; else result = E_UNEXPECTED; }
        CoTaskMemFree(id);
        return result;
    }

    HRESULT openExclusive(const WAVEFORMATEXTENSIBLE *format) {
        REFERENCE_TIME period = 0;
        WASAPI_CHECK(checkInputAccess(input.getExclusiveMinimumPeriod(format, &period)));
        WASAPI_CHECK(output.getExclusiveMinimumPeriod(format, &period));
        double minimumFrames = ceil((double)period * format->Format.nSamplesPerSec / 10000000.0), preferredFrames = (double)YubyAudioIO::preferredBufferSizeMs * format->Format.nSamplesPerSec / 1000.0;
        if (preferredFrames > INT_MAX - 7) return E_INVALIDARG;
        unsigned int requestedFrames = (unsigned int)ceil(preferredFrames);
        if (requestedFrames < 128) requestedFrames = 128; else requestedFrames = (requestedFrames + 7u) & ~7u;
        for (int attempt = 0; attempt < 4; attempt++) {
            if (minimumFrames > INT_MAX - 7) return E_INVALIDARG;
            if (requestedFrames < minimumFrames) requestedFrames = ((unsigned int)minimumFrames + 7u) & ~7u;
            period = (REFERENCE_TIME)(10000000.0 * requestedFrames / format->Format.nSamplesPerSec + 0.5);
            bool aligned = true;
            WASAPI_CHECK(checkInputAccess(input.exclusiveInitialize(period, &aligned)));
            WASAPI_CHECK(output.exclusiveInitialize(period, &aligned));
            if (aligned && (!inputEnabled || !outputEnabled || (input.frames == output.frames))) {
                char message[128];
                _snprintf_s(message, sizeof(message), _TRUNCATE, "WASAPI exclusive mode initialized with %u frames at %lu Hz\n",
                    outputEnabled ? output.frames : input.frames, (unsigned long)format->Format.nSamplesPerSec);
                OutputDebugStringA(message);
                exclusive = true;
                return S_OK;
            }
            minimumFrames = input.frames > output.frames ? input.frames : output.frames;
            resetStreams();
        }
        return AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED;
    }

    HRESULT initialize() {
        IMMDeviceEnumerator *enumerator = NULL;
        WASAPI_CHECK(CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&enumerator)));
        HRESULT hr = S_OK;
        if (SUCCEEDED(hr) && inputEnabled) {
            HRESULT inputResult = selectedInputDeviceKey.empty() ? enumerator->GetDefaultAudioEndpoint(eCapture, eMultimedia, &input.device) : enumerator->GetDevice(selectedInputDeviceKey.c_str(), &input.device);
            hr = checkInputAccess(inputResult);
        }
        if (SUCCEEDED(hr) && outputEnabled) hr = selectedOutputDeviceKey.empty() ? enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &output.device) : enumerator->GetDevice(selectedOutputDeviceKey.c_str(), &output.device);
        enumerator->Release();
        HRESULT ihr = getDeviceId(input.device, &inputDeviceID), ohr = getDeviceId(output.device, &outputDeviceID);
        WASAPI_CHECK(hr);
        WASAPI_CHECK(ihr);
        WASAPI_CHECK(ohr);
        hostTime = HostTime::create();
        if (!hostTime) return E_OUTOFMEMORY;

        GUID inputID, outputID;
        bool sameDevice = inputEnabled && outputEnabled && containerID(input.device, &inputID) && containerID(output.device, &outputID) && IsEqualGUID(inputID, outputID);
        if (!inputEnabled || !outputEnabled || sameDevice) {
            WAVEFORMATEXTENSIBLE formats[2];
            int count = 0;
            if (output.isValid()) formats[count++] = output.format;
            if (input.isValid()) formats[count++] = input.format;
            if (count == 0) resetStreams(); else for (int n = 0; n < count; n++) {
                hr = openExclusive(&formats[n]);
                if (SUCCEEDED(hr)) hr = startStreams(sameDevice);
                if (SUCCEEDED(hr)) break; else { exclusive = false; resetStreams(); }
                if (!inputEnabled && !outputEnabled) return hr;
            }
        }

        if (!exclusive) {
            if (inputEnabled) WASAPI_CHECK(checkInputAccess(input.openShared()));
            if (outputEnabled) WASAPI_CHECK(output.openShared());
            WASAPI_CHECK(startStreams(sameDevice));
        }
        if (inputEnabled) {
            inputDeviceName = getDeviceName(input.device);
            mapInputChannels = input.format.Format.nChannels;
        }
        if (outputEnabled) {
            outputDeviceName = getDeviceName(output.device);
            mapOutputChannels = output.format.Format.nChannels;
        }
        return S_OK;
    }

    static void applyChannelMap(float *input, float *output, unsigned int inputChannels, unsigned int outputChannels, unsigned int frames, unsigned int channelsToMap, const int *sourceMap, const int *destinationMap, float **sourcePlanes = NULL, float **destinationPlanes = NULL) {
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
        for (unsigned int n = 0; n < frames; n++) {
            size_t inputOffset = (size_t)n * inputStride, outputOffset = (size_t)n * outputStride;
            for (unsigned int ch = 0; ch < routeCount; ch++) to[ch][outputOffset] = from[ch][inputOffset];
        }
    }

    bool processCallback(float *inputData, float *outputData, unsigned int frames, unsigned int samplerate, const int *audioInputMap, const int *audioOutputMap) {
        float *callbackInput = inputData ? input.samples : NULL, *callbackOutput = outputData ? output.samples : NULL;
        if (inputData && interleaved) applyChannelMap(inputData, callbackInput, input.format.Format.nChannels, requiredNumberOfChannels, frames, requiredNumberOfChannels, audioInputMap, NULL);
        bool rendered;
        if (interleaved) rendered = callback(clientdata, callbackInput ? &callbackInput : NULL, callbackOutput ? &callbackOutput : NULL, frames, samplerate, hostTime->getFromFrames(frames));
        else {
            unsigned int planes = requiredNumberOfChannels * (inputEnabled + outputEnabled);
            for (unsigned int ch = 0; ch < planes; ch++) planarBuffers[ch] = planarSamples + ch * planarFrames;
            float **inputs = inputData ? planarBuffers : NULL, **outputs = outputData ? planarBuffers + (inputEnabled ? requiredNumberOfChannels : 0) : NULL;
            if (inputs) applyChannelMap(inputData, NULL, input.format.Format.nChannels, requiredNumberOfChannels, frames, requiredNumberOfChannels, audioInputMap, NULL, NULL, inputs);
            rendered = callback(clientdata, inputs, outputs, frames, samplerate, hostTime->getFromFrames(frames));
            if (rendered && outputs) applyChannelMap(NULL, outputData, requiredNumberOfChannels, output.format.Format.nChannels, frames, requiredNumberOfChannels, NULL, audioOutputMap, outputs, NULL);
        }
        if (rendered && outputData && interleaved) applyChannelMap(callbackOutput, outputData, requiredNumberOfChannels, output.format.Format.nChannels, frames, requiredNumberOfChannels, NULL, audioOutputMap);
        return rendered;
    }

    HRESULT capturePacket(unsigned int *frames) {
        BYTE *buffer = NULL;
        DWORD flags = 0;
        *frames = 0;
        HRESULT hr = input.capture->GetBuffer(&buffer, frames, &flags, NULL, NULL);
        if ((hr == AUDCLNT_S_BUFFER_EMPTY) || (hr == AUDCLNT_E_BUFFER_ERROR)) { *frames = 0; return S_OK; } else WASAPI_CHECK(hr);
        if (*frames > input.frames) {
            input.capture->ReleaseBuffer(*frames);
            return E_UNEXPECTED;
        }
        if (flags & AUDCLNT_BUFFERFLAGS_SILENT) memset(input.converted, 0, (size_t)*frames * input.format.Format.nChannels * sizeof(float)); 
        else toFloat(buffer, input.converted, *frames * input.format.Format.nChannels, 1, input.sampleType);
        return input.capture->ReleaseBuffer(*frames);
    }

    HRESULT captureWait(const int *audioInputMap) {
        if (outputEnabled && !ioHandler && pendingFrames) return S_OK;
        while (WaitForSingleObject(stopEvent, 0) != WAIT_OBJECT_0) {
            unsigned int frames;
            WASAPI_CHECK(capturePacket(&frames));
            if (!frames) return S_OK;
            if (ioHandler) ioHandler->push(input.converted, frames, input.format.Format.nSamplesPerSec);
            else if (outputEnabled) { 
                pendingFrames = frames; 
                pendingOffset = 0; 
                return S_OK; 
            } else processCallback(input.converted, NULL, frames, input.format.Format.nSamplesPerSec, audioInputMap, NULL);
        }
        return S_OK;
    }

    HRESULT process() {
        int audioInputMap[MAX_MAPPABLE_CHANNELS], audioOutputMap[MAX_MAPPABLE_CHANNELS];
        bool loggedFirstCallback = false;
        for (int n = 0; n < MAX_MAPPABLE_CHANNELS; n++) audioInputMap[n] = audioOutputMap[n] = -1;
        while (true) {
            HANDLE events[3] = { stopEvent, outputEnabled ? output.event : (inputEnabled ? input.event : NULL), outputEnabled && inputEnabled ? input.event : NULL };
            DWORD eventCount = events[2] == NULL ? 2 : 3, event = WaitForMultipleObjects(eventCount, events, FALSE, INFINITE);
            if (event == WAIT_OBJECT_0) return S_OK; else if (event == WAIT_FAILED) return HRESULT_FROM_WIN32(GetLastError()); else if (event >= WAIT_OBJECT_0 + eventCount) return E_UNEXPECTED;
            memcpy(audioInputMap, inputMap, sizeof(inputMap));
            memcpy(audioOutputMap, outputMap, sizeof(outputMap));
            if (outputEnabled && (event == WAIT_OBJECT_0 + 1)) {
                unsigned int frames = output.frames;
                if (!exclusive) {
                    unsigned int padding;
                    WASAPI_CHECK(output.client->GetCurrentPadding(&padding));
                    if (padding > frames) return E_UNEXPECTED; else frames -= padding;
                }
                if (!frames) continue;

                float *inputs[] = { NULL };
                if (inputEnabled) {
                    if (ioHandler) {
                        WASAPI_CHECK(captureWait(audioInputMap));
                        inputs[0] = ioHandler->pull(frames);
                    } else {
                        unsigned int copied = 0;
                        while (copied < frames) {
                            if (!pendingFrames) {
                                WASAPI_CHECK(capturePacket(&pendingFrames));
                                pendingOffset = 0;
                                if (!pendingFrames) break;
                            }
                            unsigned int count = pendingFrames < frames - copied ? pendingFrames : frames - copied;
                            memcpy(inputBuffer + (size_t)copied * input.format.Format.nChannels, input.converted + (size_t)pendingOffset * input.format.Format.nChannels, (size_t)count * input.format.Format.nChannels * sizeof(float));
                            copied += count;
                            pendingOffset += count;
                            pendingFrames -= count;
                        }
                        if (copied < frames) memset(inputBuffer + (size_t)copied * input.format.Format.nChannels, 0, (size_t)(frames - copied) * input.format.Format.nChannels * sizeof(float));
                        inputs[0] = inputBuffer;
                    }
                }

                BYTE *buffer;
                WASAPI_CHECK(output.render->GetBuffer(frames, &buffer));
                if (!loggedFirstCallback) { OutputDebugStringA("WASAPI calling the audio processing callback\n"); loggedFirstCallback = true; }
                bool silence = !processCallback(inputs[0], output.converted, frames, output.format.Format.nSamplesPerSec, audioInputMap, audioOutputMap);
                if (!silence) fromFloat(output.converted, buffer, frames * output.format.Format.nChannels, 1, output.sampleType);
                WASAPI_CHECK(output.render->ReleaseBuffer(frames, silence ? AUDCLNT_BUFFERFLAGS_SILENT : 0));
            } else WASAPI_CHECK(captureWait(audioInputMap));
        }
    }

    static DWORD WINAPI audioThread(void *param) {
        WASAPIHandler *handler = (WASAPIHandler *)param;
        HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
        bool initializedCOM = SUCCEEDED(hr);
        DWORD taskIndex = 0;
        HANDLE task = initializedCOM ? AvSetMmThreadCharacteristicsA("Pro Audio", &taskIndex) : NULL;
        if (initializedCOM) hr = handler->initialize();
        handler->error = FAILED(hr);
        SetEvent(handler->startedEvent);
        if (SUCCEEDED(hr)) {
            OutputDebugStringA("WASAPI streams initialized successfully\n");
            hr = handler->process();
        }
        if (FAILED(hr)) {
            char message[96];
            _snprintf_s(message, sizeof(message), _TRUNCATE, "WASAPI error: 0x%08lx\n", (unsigned long)hr);
            OutputDebugStringA(message);
        }
        handler->resetStreams();
        if (handler->input.device) { handler->input.device->Release(); handler->input.device = NULL; }
        if (handler->output.device) { handler->output.device->Release(); handler->output.device = NULL; }
        if (handler->hostTime) free(handler->hostTime);
        if (task) AvRevertMmThreadCharacteristics(task);
        if (initializedCOM) CoUninitialize();
        return 0;
    }
};

class YubyAudioIOHandler : public IMMNotificationClient {
public:
    YubyAudioIOHandler(YubyAudioIOCallback cb, void *cd, bool enableASIO) :
    callback(cb), clientdata(cd), apc(NULL), apcClientdata(NULL), thread(NULL),
    readyEvent(CreateEvent(NULL, TRUE, FALSE, NULL)), runEvent(CreateEvent(NULL, TRUE, FALSE, NULL)), stopEvent(CreateEvent(NULL, TRUE, FALSE, NULL)), wakeEvent(CreateEvent(NULL, FALSE, FALSE, NULL)),
    refs(1), pendingEvents(0), pendingDefaultChanges(0), numberOfChannels(0), asioHandler(NULL), wasapiHandler(NULL),
    interleaved(true), inputEnabled(false), outputEnabled(false), audioStartRequested(false), audioHandlerMutex(SRWLOCK_INIT), asioEnabled(enableASIO) {
        if (!readyEvent || !runEvent || !stopEvent || !wakeEvent) return; else AddRef();
        thread = CreateThread(NULL, 0, notificationThread, this, 0, NULL);
        if (!thread) Release(); else WaitForSingleObject(readyEvent, INFINITE);
    }

    void start() {
        if (runEvent) SetEvent(runEvent);
        notify(NotificationInitialDevices);
    }

    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refs); }
    ULONG STDMETHODCALLTYPE Release() override { ULONG count = InterlockedDecrement(&refs); if (!count) delete this; return count; }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **object) override {
        if (!object) return E_POINTER; else *object = NULL;
        if ((iid != __uuidof(IUnknown)) && (iid != __uuidof(IMMNotificationClient))) return E_NOINTERFACE; else *object = static_cast<IMMNotificationClient *>(this);
        AddRef();
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR id) override { return OnDeviceAdded(id); }
    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR id, DWORD) override { return OnDeviceAdded(id); }
    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR id, const PROPERTYKEY key) override { return (IsEqualPropertyKey(key, PKEY_Device_FriendlyName) || IsEqualPropertyKey(key, PKEY_AudioEngine_DeviceFormat) || IsEqualPropertyKey(key, PKEY_Device_ContainerId)) ? OnDeviceAdded(id) : S_OK; }
    HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR) override { notify(NotificationDevicesChanged); return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR) override {
        if (role != eMultimedia) return S_OK; else InterlockedOr(&pendingDefaultChanges, flow == eCapture ? 1 : 2);
        notify(NotificationDevicesChanged);
        return S_OK;
    }
    void mapChannels() { notify(NotificationMapChannels); }

    void setDevice(const char *deviceName) {
        std::wstring name;
        if (deviceName && *deviceName && strcmp(deviceName, "Windows Default Audio Device")) {
            int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, deviceName, -1, NULL, 0);
            if (!length) return;
            name.resize((size_t)length);
            if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, deviceName, -1, name.data(), length)) return;
            name.resize((size_t)length - 1);
        }
        AcquireSRWLockExclusive(&audioHandlerMutex);
        bool changed = requestedDeviceName != name;
        if (changed) requestedDeviceName = std::move(name);
        ReleaseSRWLockExclusive(&audioHandlerMutex);
        if (changed) notify(NotificationDeviceSelection);
    }

    void start(audioProcessingCallback newAPC, void *newAPCClientdata, unsigned int newNumberOfChannels, bool newInterleaved, bool newInputEnabled, bool newOutputEnabled) {
        if (newNumberOfChannels > MAX_MAPPABLE_CHANNELS) newNumberOfChannels = MAX_MAPPABLE_CHANNELS;
        AcquireSRWLockExclusive(&audioHandlerMutex);
        if (wasapiHandler) delete wasapiHandler; wasapiHandler = NULL;
        if (asioHandler) delete asioHandler; asioHandler = NULL;
        apc = newAPC;
        apcClientdata = newAPCClientdata;
        numberOfChannels = newNumberOfChannels;
        interleaved = newInterleaved;
        inputEnabled = newInputEnabled;
        outputEnabled = newOutputEnabled;
        audioStartRequested = (inputEnabled || outputEnabled) && numberOfChannels && apc;
        if (audioStartRequested) createAudioHandler();
        ReleaseSRWLockExclusive(&audioHandlerMutex);
    }

    void stop() {
        AcquireSRWLockExclusive(&audioHandlerMutex);
        audioStartRequested = false;
        if (wasapiHandler) delete wasapiHandler; wasapiHandler = NULL;
        if (asioHandler) delete asioHandler; asioHandler = NULL;
        ReleaseSRWLockExclusive(&audioHandlerMutex);
    }

    void shutdown() {
        if (stopEvent) SetEvent(stopEvent);
        if (thread) WaitForSingleObject(thread, INFINITE);
        stop();
        Release(); 
    }

private:
    struct AudioDeviceKeys { std::wstring input, output, asio; };
    std::map<std::wstring, AudioDeviceKeys>audioDeviceKeys;
    YubyAudioIOCallback callback;
    audioProcessingCallback apc;
    void *apcClientdata, *clientdata;
    ASIOHandler *asioHandler;
    WASAPIHandler *wasapiHandler;
    SRWLOCK audioHandlerMutex;
    HANDLE thread, readyEvent, runEvent, stopEvent, wakeEvent;
    std::wstring requestedDeviceName, selectedDeviceName;
    LONG refs, pendingEvents, pendingDefaultChanges;
    unsigned int numberOfChannels;
    bool interleaved, inputEnabled, outputEnabled, audioStartRequested, asioEnabled;

    enum NotificationEvent : LONG {
        NotificationDevicesChanged = 1,
        NotificationMapChannels = 2,
        NotificationInputPermission = 4,
        NotificationInitialDevices = 8,
        NotificationDeviceSelection = 16
    };

    bool defaultDeviceChanged(IMMDeviceEnumerator *enumerator, EDataFlow flow, const std::wstring &currentID) {
        IMMDevice *device = NULL;
        LPWSTR id = NULL;
        HRESULT hr = enumerator->GetDefaultAudioEndpoint(flow, eMultimedia, &device);
        if (SUCCEEDED(hr)) hr = device->GetId(&id);
        bool changed = (hr == HRESULT_FROM_WIN32(ERROR_NOT_FOUND)) ? !currentID.empty() : SUCCEEDED(hr) && id && (currentID != id);
        CoTaskMemFree(id);
        if (device) device->Release();
        return changed;
    }

    ~YubyAudioIOHandler() {
        if (thread) CloseHandle(thread);
        if (readyEvent) CloseHandle(readyEvent);
        if (runEvent) CloseHandle(runEvent);
        if (stopEvent) CloseHandle(stopEvent);
        if (wakeEvent) CloseHandle(wakeEvent);
    }

    void notify(LONG events) {
        InterlockedOr(&pendingEvents, events);
        if (wakeEvent) SetEvent(wakeEvent);
    }

    void setDefaultChannelMapIfEmpty(ChannelMap &map) {
        for (unsigned int n = 0; n < MAX_MAPPABLE_CHANNELS; n++) if ((map.inputMap[n] != -1) || (map.outputMap[n] != -1)) return;
        int inputs = int(map.numInputChannels < numberOfChannels ? map.numInputChannels : numberOfChannels);
        int outputs = int(map.numOutputChannels < numberOfChannels ? map.numOutputChannels : numberOfChannels);
        for (int n = 0; n < inputs; n++) map.inputMap[n] = n;
        for (int n = 0; n < outputs; n++) map.outputMap[n] = n;
    }

    static char *copyUTF8(const wchar_t *value) {
        if (!value) return NULL;
        int bytes = WideCharToMultiByte(CP_UTF8, 0, value, -1, NULL, 0, NULL, NULL);
        if (!bytes) return NULL;
        char *result = (char *)malloc(bytes);
        if (!result) return NULL;
        if (!WideCharToMultiByte(CP_UTF8, 0, value, -1, result, bytes, NULL, NULL)) { free(result); return NULL; }
        return result;
    }

    void callDevicesChanged(IMMDeviceEnumerator *enumerator) {
        AudioDeviceList *devices = getAudioDevices(enumerator);
        if (!devices) return; else callback(clientdata, YubyAudioIOEvent::DevicesChanged, devices);
        for (unsigned int n = 0; n < devices->length; n++) if (devices->devices[n].name) free((char *)devices->devices[n].name);
        free(devices->devices);
        free(devices);
        AcquireSRWLockShared(&audioHandlerMutex);
        bool requestedAvailable = requestedDeviceName.empty() || (audioDeviceKeys.find(requestedDeviceName) != audioDeviceKeys.end());
        bool selectedUnavailable = !selectedDeviceName.empty() && (audioDeviceKeys.find(selectedDeviceName) == audioDeviceKeys.end());
        bool updateSelection = audioStartRequested && (selectedUnavailable || ((selectedDeviceName != requestedDeviceName) && requestedAvailable) || (!wasapiHandler && !asioHandler));
        ReleaseSRWLockShared(&audioHandlerMutex);
        if (updateSelection) notify(NotificationDeviceSelection);
    }

    static bool readRegistryString(HKEY key, const wchar_t *valueName, DWORD allowedTypes, std::wstring &value) {
        DWORD bytes = 0;
        if ((RegGetValueW(key, NULL, valueName, allowedTypes, NULL, NULL, &bytes) != ERROR_SUCCESS) || !bytes || (bytes % sizeof(wchar_t)) || (bytes > MAXDWORD - sizeof(wchar_t))) return false;
        std::wstring result(bytes / sizeof(wchar_t) + 1, L'\0');
        bytes += sizeof(wchar_t);
        if ((RegGetValueW(key, NULL, valueName, allowedTypes, NULL, result.data(), &bytes) != ERROR_SUCCESS) || !bytes || (bytes % sizeof(wchar_t))) return false;
        size_t length = bytes / sizeof(wchar_t);
        while (length && !result[length - 1]) length--;
        if (!length) return false;
        for (size_t n = 0; n < length; n++) if (!result[n]) return false;
        result.resize(length);
        value = std::move(result);
        return true;
    }

    static bool getRegistryInfo(const std::wstring &deviceName, std::wstring &outClsid, std::wstring &outDllPath) {
        HKEY deviceKey;
        std::wstring deviceKeyPath = std::wstring(L"SOFTWARE\\ASIO\\") + deviceName;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, deviceKeyPath.c_str(), 0, KEY_READ | KEY_WOW64_64KEY, &deviceKey) != ERROR_SUCCESS) return false;
        std::wstring clsid;
        bool success = readRegistryString(deviceKey, L"CLSID", RRF_RT_REG_SZ, clsid);
        RegCloseKey(deviceKey);
        if (!success) return false;
        HKEY clsidKey;
        std::wstring clsidKeyPath = L"CLSID\\" + clsid + L"\\InprocServer32";
        if (RegOpenKeyExW(HKEY_CLASSES_ROOT, clsidKeyPath.c_str(), 0, KEY_READ | KEY_WOW64_64KEY, &clsidKey) != ERROR_SUCCESS) return false;
        std::wstring dllPath;
        success = readRegistryString(clsidKey, NULL, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ, dllPath);
        RegCloseKey(clsidKey);
        if (!success) return false;
        outClsid = std::move(clsid);
        outDllPath = std::move(dllPath);
        return true;
    }

    static bool readWASAPIAudioDeviceInfo(IPropertyStore *store, const wchar_t *id, EDataFlow flow, PROPVARIANT *prop, AudioDeviceInfo *info, std::map<std::wstring, AudioDeviceKeys> &deviceKeys) {
        if (FAILED(store->GetValue(PKEY_AudioEngine_DeviceFormat, prop)) || (prop->vt != VT_BLOB) || !prop->blob.pBlobData || (prop->blob.cbSize < sizeof(WAVEFORMATEX))) return false;
        WAVEFORMATEX format;
        memcpy(&format, prop->blob.pBlobData, sizeof(format));
        if (!format.nChannels || (sizeof(format) + format.cbSize > prop->blob.cbSize)) return false;
        PropVariantClear(prop);
        PropVariantInit(prop);
        const wchar_t *label = (SUCCEEDED(store->GetValue(PKEY_Device_FriendlyName, prop)) && (prop->vt == VT_LPWSTR) && prop->pwszVal && *prop->pwszVal) ? prop->pwszVal : id;
        char *name = copyUTF8(label);
        if (!name) return false;
        if (flow == eCapture) deviceKeys[label].input = id; else deviceKeys[label].output = id;
        info->numInputChannels = flow == eCapture ? format.nChannels : 0;
        info->numOutputChannels = flow == eRender ? format.nChannels : 0;
        info->name = name;
        return true;
    }

    static unsigned int getDefaultWASAPIChannelCount(IMMDeviceEnumerator *enumerator, EDataFlow flow) {
        if (!enumerator) return 0;
        IMMDevice *device = NULL;
        IPropertyStore *store = NULL;
        PROPVARIANT prop;
        PropVariantInit(&prop);
        unsigned int channels = 0;
        if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(flow, eMultimedia, &device)) && device &&
            SUCCEEDED(device->OpenPropertyStore(STGM_READ, &store)) && store &&
            SUCCEEDED(store->GetValue(PKEY_AudioEngine_DeviceFormat, &prop)) && (prop.vt == VT_BLOB) && prop.blob.pBlobData && (prop.blob.cbSize >= sizeof(WAVEFORMATEX))) {
            WAVEFORMATEX format;
            memcpy(&format, prop.blob.pBlobData, sizeof(format));
            if (format.nChannels && (sizeof(format) + format.cbSize <= prop.blob.cbSize)) channels = format.nChannels;
        }
        PropVariantClear(&prop);
        if (store) store->Release();
        if (device) device->Release();
        return channels;
    }

    AudioDeviceList *getAudioDevices(IMMDeviceEnumerator *enumerator) {
        std::map<std::wstring, AudioDeviceKeys> deviceKeys;
        IMMDeviceCollection *collection = NULL;
        UINT wasapiCount = 0;
        bool hasWASAPIDevices =
            enumerator &&
            SUCCEEDED(enumerator->EnumAudioEndpoints(eAll, DEVICE_STATE_ACTIVE, &collection)) &&
            SUCCEEDED(collection->GetCount(&wasapiCount));
        if (!hasWASAPIDevices) wasapiCount = 0;

        HKEY asioKey = NULL;
        DWORD asioCount = 0, maxDeviceNameLength = 0;
        if (asioEnabled && (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\ASIO", 0, KEY_READ | KEY_WOW64_64KEY, &asioKey) == ERROR_SUCCESS) &&
            ((RegQueryInfoKeyW(asioKey, NULL, NULL, NULL, &asioCount, &maxDeviceNameLength, NULL, NULL, NULL, NULL, NULL, NULL) != ERROR_SUCCESS) ||
            (maxDeviceNameLength == MAXDWORD))) {
            RegCloseKey(asioKey);
            asioKey = NULL;
            asioCount = 0;
        }

        size_t totalCount = (size_t)wasapiCount + asioCount + 1;
        AudioDeviceList *result = totalCount && (totalCount <= UINT_MAX) && (totalCount <= size_t(-1) / sizeof(AudioDeviceInfo)) ? (AudioDeviceList *)malloc(sizeof(AudioDeviceList)) : NULL;

        if (result) {
            result->length = 0;
            result->devices = (AudioDeviceInfo *)calloc(totalCount, sizeof(AudioDeviceInfo));
            if (result->devices) {
                char *name = copyUTF8(L"Windows Default Audio Device");
                if (name) {
                    AudioDeviceInfo *info = result->devices + result->length++;
                    info->numInputChannels = getDefaultWASAPIChannelCount(enumerator, eCapture);
                    info->numOutputChannels = getDefaultWASAPIChannelCount(enumerator, eRender);
                    info->name = name;
                }
            } else { free(result); result = NULL; }
        }
        if (result) {
            for (UINT n = 0; n < wasapiCount; n++) {
                IMMDevice *device = NULL;
                IMMEndpoint *endpoint = NULL;
                IPropertyStore *store = NULL;
                LPWSTR id = NULL;
                PROPVARIANT prop;
                EDataFlow flow;
                PropVariantInit(&prop);
                if (SUCCEEDED(collection->Item(n, &device)) &&
                    SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&endpoint))) &&
                    SUCCEEDED(endpoint->GetDataFlow(&flow)) && ((flow == eCapture) || (flow == eRender)) &&
                    SUCCEEDED(device->GetId(&id)) && id &&
                    SUCCEEDED(device->OpenPropertyStore(STGM_READ, &store)) &&
                    readWASAPIAudioDeviceInfo(store, id, flow, &prop, result->devices + result->length, deviceKeys)) result->length++;
                PropVariantClear(&prop);
                CoTaskMemFree(id);
                if (store) store->Release();
                if (endpoint) endpoint->Release();
                if (device) device->Release();
            }
        }
        if (collection) collection->Release();

        if (asioKey) {
            if (result) {
                std::wstring deviceNameBuffer((size_t)maxDeviceNameLength + 1, L'\0');
                for (DWORD index = 0; index < asioCount; index++) {
                    DWORD size = (DWORD)deviceNameBuffer.size();
                    if (RegEnumKeyExW(asioKey, index, deviceNameBuffer.data(), &size, NULL, NULL, NULL, NULL) != ERROR_SUCCESS) break;
                    std::wstring deviceName(deviceNameBuffer.data(), size), dllPath, clsid;
                    if (!getRegistryInfo(deviceName, clsid, dllPath)) continue;

                    bool available = false;
                    long numInputs = 0, numOutputs = 0;
                    AcquireSRWLockShared(&audioHandlerMutex);
                    if (asioHandler && (asioHandler->name == deviceName) && asioHandler->isRunning()) {
                        available = true;
                        numInputs = asioHandler->numInputs;
                        numOutputs = asioHandler->numOutputs;
                    }
                    ReleaseSRWLockShared(&audioHandlerMutex);
                    if (!available) {
                        ASIOHandler *h = new ASIOHandler(deviceName, dllPath, clsid);
                        available = !h->error;
                        numInputs = h->numInputs;
                        numOutputs = h->numOutputs;
                        delete h;
                    }
                    if (available) {
                        std::wstring displayName = deviceName + L" [ASIO]";
                        char *name = copyUTF8(displayName.c_str());
                        if (name) {
                            deviceKeys[displayName].asio = displayName;
                            AudioDeviceInfo *info = result->devices + result->length++;
                            info->numInputChannels = numInputs;
                            info->numOutputChannels = numOutputs;
                            info->name = name;
                        }
                    }
                }
            }
            RegCloseKey(asioKey);
        }

        if (result) {
            AcquireSRWLockExclusive(&audioHandlerMutex);
            audioDeviceKeys.swap(deviceKeys);
            ReleaseSRWLockExclusive(&audioHandlerMutex);
        }
        return result;
    }

    void createAudioHandler() {
        static const std::wstring suffix = L" [ASIO]";
        if (!requestedDeviceName.empty()) {
            auto device = audioDeviceKeys.find(requestedDeviceName);
            if (device != audioDeviceKeys.end()) {
                const AudioDeviceKeys &keys = device->second;
                if (!keys.asio.empty() && (keys.asio.size() > suffix.size()) && (keys.asio.compare(keys.asio.size() - suffix.size(), suffix.size(), suffix) == 0)) {
                    std::wstring asioDeviceName(keys.asio, 0, keys.asio.size() - suffix.size()), clsid, dllPath;
                    if (getRegistryInfo(asioDeviceName, clsid, dllPath)) {
                        ASIOHandler *handler = new ASIOHandler(asioDeviceName, dllPath, clsid, inputEnabled, outputEnabled, numberOfChannels, interleaved, apc, apcClientdata);
                        if (handler->error) delete handler;
                        else {
                            asioHandler = handler;
                            selectedDeviceName = requestedDeviceName;
                            mapChannels();
                            return;
                        }
                    }
                } else {
                    WASAPIHandler *handler = new WASAPIHandler(inputEnabled, outputEnabled, numberOfChannels, interleaved, apc, apcClientdata, keys.input, keys.output);
                    if (handler->error) delete handler;
                    else {
                        wasapiHandler = handler;
                        selectedDeviceName = requestedDeviceName;
                        mapChannels();
                        return;
                    }
                }
            }
        }

        selectedDeviceName.clear();
        WASAPIHandler *handler = new WASAPIHandler(inputEnabled, outputEnabled, numberOfChannels, interleaved, apc, apcClientdata, std::wstring(), std::wstring());
        if (handler->error) delete handler;
        else {
            wasapiHandler = handler;
            mapChannels();
        }
    }

    static DWORD WINAPI notificationThread(void *param) {
        ((YubyAudioIOHandler *)param)->notificationThreadFunction();
        return 0;
    }

    void notificationThreadFunction() {
        bool initialized = false, notificationRegistered = false, microphonePermission = false;
        EventRegistrationToken microphoneAccessChangedToken = {};
        IMMDeviceEnumerator *enumerator = NULL;
        Microsoft::WRL::ComPtr<ABI::Windows::Security::Authorization::AppCapabilityAccess::IAppCapability>microphoneCapability;
        bool runtimeInitialized = SUCCEEDED(RoInitialize(RO_INIT_MULTITHREADED));
        
        if (runtimeInitialized) {
            using namespace ABI::Windows::Security::Authorization::AppCapabilityAccess;
            Microsoft::WRL::ComPtr<IAppCapabilityStatics> capabilityStatics;
            HRESULT permissionResult = RoGetActivationFactory(Microsoft::WRL::Wrappers::HStringReference(RuntimeClass_Windows_Security_Authorization_AppCapabilityAccess_AppCapability).Get(), IID_PPV_ARGS(capabilityStatics.GetAddressOf()));
            if (SUCCEEDED(permissionResult)) permissionResult = capabilityStatics->Create(Microsoft::WRL::Wrappers::HStringReference(L"microphone").Get(), microphoneCapability.GetAddressOf());
            using AccessChangedHandler = ABI::Windows::Foundation::ITypedEventHandler<AppCapability *, AppCapabilityAccessChangedEventArgs *>;
            Microsoft::WRL::ComPtr<AccessChangedHandler> accessChangedHandler;
            if (SUCCEEDED(permissionResult)) {
                accessChangedHandler = Microsoft::WRL::Callback<Microsoft::WRL::Implements<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>, AccessChangedHandler, Microsoft::WRL::FtmBase>>([this](IAppCapability *, IAppCapabilityAccessChangedEventArgs *) -> HRESULT {
                    notify(NotificationInputPermission);
                    return S_OK;
                });
                if (!accessChangedHandler) permissionResult = E_OUTOFMEMORY;
            }
            if (SUCCEEDED(permissionResult)) permissionResult = microphoneCapability->add_AccessChanged(accessChangedHandler.Get(), &microphoneAccessChangedToken);
            if (FAILED(permissionResult)) microphoneCapability.Reset();
            else {
                ABI::Windows::Security::Authorization::AppCapabilityAccess::AppCapabilityAccessStatus permission;
                HRESULT checkResult = microphoneCapability->CheckAccess(&permission);
                if (SUCCEEDED(checkResult)) microphonePermission = permission == ABI::Windows::Security::Authorization::AppCapabilityAccess::AppCapabilityAccessStatus_Allowed;
            }

            if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&enumerator)))) notificationRegistered = SUCCEEDED(enumerator->RegisterEndpointNotificationCallback(this));
            initialized = true;
        }

        SetEvent(readyEvent);
        if (initialized) {
            HANDLE startupEvents[2] = { stopEvent, runEvent };
            if (WaitForMultipleObjects(2, startupEvents, FALSE, INFINITE) != WAIT_OBJECT_0 + 1) initialized = false;
        }

        if (initialized) {
            ULONGLONG nextDeviceCheck = GetTickCount64() + 100, notificationDeadline = 0;
            unsigned int changedDefaults = 0;
            HANDLE events[2] = { stopEvent, wakeEvent };
            while (WaitForSingleObject(stopEvent, 0) == WAIT_TIMEOUT) {
                ULONGLONG now = GetTickCount64();
                DWORD timeout = now >= nextDeviceCheck ? 0 : (DWORD)(nextDeviceCheck - now), event = WaitForMultipleObjects(2, events, FALSE, timeout);
                if (event == WAIT_OBJECT_0) break; else if (event == WAIT_OBJECT_0 + 1) {
                    LONG notifications = InterlockedExchange(&pendingEvents, 0);
                    if (notifications & NotificationDeviceSelection) {
                        AcquireSRWLockExclusive(&audioHandlerMutex);
                        bool selectedUnavailable = !selectedDeviceName.empty() && (audioDeviceKeys.find(selectedDeviceName) == audioDeviceKeys.end());
                        if ((selectedDeviceName != requestedDeviceName) || selectedUnavailable || (audioStartRequested && !wasapiHandler && !asioHandler)) {
                            if (wasapiHandler) delete wasapiHandler; wasapiHandler = NULL;
                            if (asioHandler) delete asioHandler; asioHandler = NULL;
                            if (audioStartRequested) createAudioHandler(); else selectedDeviceName = requestedDeviceName;
                        }
                        ReleaseSRWLockExclusive(&audioHandlerMutex);
                    }
                    if (notifications & NotificationMapChannels) {
                        AcquireSRWLockExclusive(&audioHandlerMutex);
                        if (asioHandler && !asioHandler->error) {
                            ChannelMap map = {};
                            for (int n = 0; n < MAX_MAPPABLE_CHANNELS; n++) map.inputMap[n] = map.outputMap[n] = -1;
                            int bytes = WideCharToMultiByte(CP_UTF8, 0, asioHandler->name.c_str(), -1, NULL, 0, NULL, NULL);
                            char *deviceName = bytes ? (char *)malloc(bytes) : NULL;
                            if (deviceName && !WideCharToMultiByte(CP_UTF8, 0, asioHandler->name.c_str(), -1, deviceName, bytes, NULL, NULL)) { free(deviceName); deviceName = NULL; }
                            map.numInputChannels = inputEnabled ? (unsigned int)asioHandler->numInputs : 0;
                            map.numOutputChannels = outputEnabled ? (unsigned int)asioHandler->numOutputs : 0;
                            map.inputDeviceName = map.numInputChannels ? (deviceName ? deviceName : "Unknown ASIO Device") : NULL;
                            map.outputDeviceName = map.numOutputChannels ? (deviceName ? deviceName : "Unknown ASIO Device") : NULL;
                            callback(clientdata, YubyAudioIOEvent::MapChannels, &map);
                            setDefaultChannelMapIfEmpty(map);
                            asioHandler->applyMap(map);
                            free(deviceName);
                        }
                        if (wasapiHandler && !wasapiHandler->error && (wasapiHandler->mapInputChannels || wasapiHandler->mapOutputChannels) && wasapiHandler->thread && (WaitForSingleObject(wasapiHandler->thread, 0) == WAIT_TIMEOUT)) {
                            ChannelMap map = {};
                            for (int n = 0; n < MAX_MAPPABLE_CHANNELS; n++) map.inputMap[n] = map.outputMap[n] = -1;
                            map.inputDeviceName = wasapiHandler->mapInputChannels ? (wasapiHandler->inputDeviceName ? wasapiHandler->inputDeviceName : "Unknown Audio Device") : NULL;
                            map.outputDeviceName = wasapiHandler->mapOutputChannels ? (wasapiHandler->outputDeviceName ? wasapiHandler->outputDeviceName : "Unknown Audio Device") : NULL;
                            map.numInputChannels = wasapiHandler->mapInputChannels;
                            map.numOutputChannels = wasapiHandler->mapOutputChannels;
                            callback(clientdata, YubyAudioIOEvent::MapChannels, &map);
                            setDefaultChannelMapIfEmpty(map);
                            wasapiHandler->applyMap(map);
                        }
                        ReleaseSRWLockExclusive(&audioHandlerMutex);
                    }
                    if ((notifications & NotificationInputPermission) && microphoneCapability) {
                        ABI::Windows::Security::Authorization::AppCapabilityAccess::AppCapabilityAccessStatus permission;
                        if (FAILED(microphoneCapability->CheckAccess(&permission))) OutputDebugStringA("Windows microphone permission query failed\n");
                        else {
                            bool p = permission == ABI::Windows::Security::Authorization::AppCapabilityAccess::AppCapabilityAccessStatus_Allowed;
                            if (microphonePermission != p) {
                                microphonePermission = p;
                                callback(clientdata, YubyAudioIOEvent::InputPermissionChanged, &microphonePermission);
                            }
                        }
                    }
                    if (notifications & NotificationInitialDevices) callDevicesChanged(enumerator);
                    if (notifications & NotificationDevicesChanged) {
                        changedDefaults |= (unsigned int)InterlockedExchange(&pendingDefaultChanges, 0);
                        notificationDeadline = GetTickCount64() + 100;
                    }
                } else if (event != WAIT_TIMEOUT) break;
                now = GetTickCount64();
                if ((WaitForSingleObject(stopEvent, 0) == WAIT_TIMEOUT) && (now >= nextDeviceCheck)) {
                    unsigned int changed = 0;
                    if ((notificationDeadline > 0) && (now >= notificationDeadline)) {
                        notificationDeadline = 0;
                        changed = changedDefaults;
                        changedDefaults = 0;
                        callDevicesChanged(enumerator);
                    }
                    AcquireSRWLockExclusive(&audioHandlerMutex);

                    bool defaultChanged = wasapiHandler && selectedDeviceName.empty() && (
                        ((changed & 1) && inputEnabled && defaultDeviceChanged(enumerator, eCapture, wasapiHandler->inputDeviceID)) ||
                        ((changed & 2) && outputEnabled && defaultDeviceChanged(enumerator, eRender, wasapiHandler->outputDeviceID)));
                    if (audioStartRequested && defaultChanged) {
                        if (wasapiHandler) delete wasapiHandler; wasapiHandler = NULL;
                        if (asioHandler) delete asioHandler; asioHandler = NULL;
                        createAudioHandler();
                    }

                    ReleaseSRWLockExclusive(&audioHandlerMutex);
                    nextDeviceCheck = GetTickCount64() + 100;
                }
            }
        }

        if (microphoneCapability) microphoneCapability->remove_AccessChanged(microphoneAccessChangedToken);
        microphoneCapability.Reset();
        if (notificationRegistered) enumerator->UnregisterEndpointNotificationCallback(this);
        if (enumerator) enumerator->Release(); 
        if (runtimeInitialized) RoUninitialize();
        Release();
    }
};

#undef WASAPI_CHECK

unsigned int YubyAudioIO::preferredBufferSizeMs = 12; 
static YubyAudioIOHandler *yubyAudioIOHandler = NULL;
void YubyAudioIO::start(audioProcessingCallback apc, void *clientdata, unsigned int numberOfChannels, bool interleaved, bool inputEnabled, bool outputEnabled) { if (yubyAudioIOHandler) yubyAudioIOHandler->start(apc, clientdata, numberOfChannels, interleaved, inputEnabled, outputEnabled); }
void YubyAudioIO::stop() { if (yubyAudioIOHandler) yubyAudioIOHandler->stop(); }
void YubyAudioIO::mapChannels() { if (yubyAudioIOHandler) yubyAudioIOHandler->mapChannels(); }
void YubyAudioIO::setDevice(const char *deviceName) { if (yubyAudioIOHandler) yubyAudioIOHandler->setDevice(deviceName); }

void YubyAudioIO::initialize(YubyAudioIOCallback callback, void *clientdata) {
    if (!callback) return;
    yubyAudioIOHandler = new YubyAudioIOHandler(callback, clientdata, false); 
    yubyAudioIOHandler->start();
}

void YubyAudioIO::initializeWithASIO(YubyAudioIOCallback callback, void *clientdata) {
    if (!callback) return;
    yubyAudioIOHandler = new YubyAudioIOHandler(callback, clientdata, true); 
    yubyAudioIOHandler->start();
}

void YubyAudioIO::shutdown() {
    if (!yubyAudioIOHandler) return; else yubyAudioIOHandler->shutdown();
    yubyAudioIOHandler = NULL;
}

bool YubyAudioIO::ASIOMain(int *returnCode) {
    if (!returnCode) return false;
    int argc = 0;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    const wchar_t sharedMemoryPrefix[] = L"Local\\YubyAudioIO.ASIO.";
    const size_t prefixLength = (sizeof(sharedMemoryPrefix) / sizeof(sharedMemoryPrefix[0])) - 1;
    GUID sharedMemoryID;
    bool isASIOProcess = argv && ((argc == 4) || ((argc == 5) && !wcscmp(argv[4], L"start"))) && (wcslen(argv[1]) == prefixLength + 38) && !wcsncmp(argv[1], sharedMemoryPrefix, prefixLength) && SUCCEEDED(CLSIDFromString(argv[1] + prefixLength, &sharedMemoryID));
    if (!isASIOProcess) { LocalFree(argv); return false; }

    if (!AttachConsole(ATTACH_PARENT_PROCESS)) AllocConsole();
    SetErrorMode(SEM_NOGPFAULTERRORBOX | SEM_FAILCRITICALERRORS | SEM_NOALIGNMENTFAULTEXCEPT | SEM_NOOPENFILEERRORBOX);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDERR);

    ASIOProcess *p = new ASIOProcess();
    *returnCode = p->main(argc, argv);
    delete p;

    LocalFree(argv);
    FreeConsole();
    return true;
}

#endif
