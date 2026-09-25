#if __APPLE__
#include "./yubyaudioio.h"
#import <AVFAudio/AVFAudio.h>
#import <AudioToolbox/AudioToolbox.h>
#import <AudioUnit/AudioUnit.h>
#include <TargetConditionals.h>

#if __has_feature(objc_arc)
    #define ARCRetain(object) (object)
    #define ARCRelease(object) ((void)0)
    #define ARCStrong __strong
#else
    #define ARCRetain(object) [(object) retain]
    #define ARCRelease(object) [(object) release]
    #define ARCStrong
#endif

#define MAXFRAMES 4096

static void destroyAudioUnit(AudioComponentInstance *unit) {
    if (*unit == NULL) return;
    AudioOutputUnitStop(*unit);
    AudioUnitUninitialize(*unit);
    AudioComponentInstanceDispose(*unit);
    *unit = NULL;
}

static void applyChannelMapOnMainThread(AudioUnit audioUnit, const int *map, unsigned int numDestinationChannels, bool output) {
#if !TARGET_IPHONE_SIMULATOR
    if (!audioUnit || !numDestinationChannels) return;
    bool hasMapping = false;
    for (unsigned int n = 0; n < MAX_MAPPABLE_CHANNELS; n++) if (map[n] != -1) { hasMapping = true; break; }
    if (!hasMapping) return;
    SInt32 *channelMap = (SInt32 *)malloc(sizeof(SInt32) * numDestinationChannels);
    if (!channelMap) return;
    for (unsigned int n = 0; n < numDestinationChannels; n++) channelMap[n] = -1;
    if (output) for (unsigned int n = 0; n < MAX_MAPPABLE_CHANNELS; n++) {
        int destination = map[n];
        if ((destination >= 0) && ((unsigned int)destination < numDestinationChannels)) channelMap[destination] = n;
    } else {
        unsigned int to = numDestinationChannels < MAX_MAPPABLE_CHANNELS ? numDestinationChannels : MAX_MAPPABLE_CHANNELS;
        for (unsigned int n = 0; n < to; n++) channelMap[n] = map[n];
    }
    AudioUnitSetProperty(audioUnit, kAudioOutputUnitProperty_ChannelMap, kAudioUnitScope_Output, output ? 0 : 1, channelMap, sizeof(SInt32) * numDestinationChannels);
    free(channelMap);
#endif
}

#if TARGET_OS_OSX
#define AppleHandler macOSHandler
#define INPUT_BUFFER_COUNT 3
#define INPUT_BUFFER_INDEX_MASK 3
#define INPUT_BUFFER_READY 4

static unsigned int atomicExchange(volatile unsigned int *value, unsigned int newValue) {
    unsigned int oldValue;
    do oldValue = __sync_val_compare_and_swap(value, 0, 0); while (!__sync_bool_compare_and_swap(value, oldValue, newValue));
    return oldValue;
}

static unsigned int getAudioDeviceChannelCountOnMainThread(AudioDeviceID deviceID, bool input) {
    AudioObjectPropertyAddress address = { kAudioDevicePropertyStreamConfiguration, input ? kAudioDevicePropertyScopeInput : kAudioDevicePropertyScopeOutput, kAudioObjectPropertyElementMain };
    UInt32 size = 0, result = 0;
    if (AudioObjectGetPropertyDataSize(deviceID, &address, 0, NULL, &size) || !size) return 0;
    AudioBufferList *bufferList = (AudioBufferList *)malloc(size);
    if (!bufferList) return 0;
    if (!AudioObjectGetPropertyData(deviceID, &address, 0, NULL, &size, bufferList)) for (unsigned int b = 0; b < bufferList->mNumberBuffers; b++) result += bufferList->mBuffers[b].mNumberChannels;
    free(bufferList);
    return result;
}

static char *getAudioDeviceNameOnMainThread(AudioDeviceID deviceID) {
    AudioObjectPropertyAddress deviceNameAddress = { kAudioObjectPropertyName, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain };
    UInt32 size = sizeof(CFStringRef);
    CFStringRef cstr = NULL;
    AudioObjectGetPropertyData(deviceID, &deviceNameAddress, 0, NULL, &size, &cstr);
    if (!cstr) return NULL;
    const char *u = [(__bridge NSString *)cstr UTF8String];
    char *r = u ? strdup(u) : NULL;
    CFRelease(cstr);
    return r;
}

@interface macOSIO: NSObject
@end
@implementation macOSIO {
    ChannelMap map;
    YubyAudioIOCallback mainCallback;
    audioProcessingCallback ioCallback;
    void *ioClientdata, *mainClientdata;
    AudioDeviceID audioDeviceID, inputDeviceID, outputDeviceID;
    AudioUnit inputUnit, outputUnit;
    AudioBufferList *inputBuffers[INPUT_BUFFER_COUNT];
    float **inputBufs[INPUT_BUFFER_COUNT], **outputBufs;
    unsigned int inputFrames[INPUT_BUFFER_COUNT], numberOfChannels, lastPreferredBufferSizeMs, inputWriteBuffer, inputReadBuffer, samplerate;
    volatile unsigned int pendingInputBuffer;
    bool interleaved, inputRequested, inputEnabled, outputEnabled, listeningDefaultInput, listeningDefaultOutput, stopped;
}

static OSStatus macOSAudioInputCallback(void *inRefCon, AudioUnitRenderActionFlags *ioActionFlags, const AudioTimeStamp *inTimeStamp, UInt32 inBusNumber, UInt32 inNumberFrames, __attribute__((unused)) AudioBufferList *ioData) {
    macOSIO *io = (__bridge macOSIO *)inRefCon;
    if (io->stopped) return noErr;
    
    if (io->lastPreferredBufferSizeMs != YubyAudioIO::preferredBufferSizeMs) {
        io->lastPreferredBufferSizeMs = YubyAudioIO::preferredBufferSizeMs;
        [io performSelectorOnMainThread:@selector(setBufferSizeOnMainThread) withObject:nil waitUntilDone:NO];
    }
    
    if (((inNumberFrames & 7) != 0) || (inNumberFrames < 32) || (inNumberFrames > MAXFRAMES)) return kAudioUnitErr_InvalidParameter;
    
    unsigned int inputBufferIndex = io->inputWriteBuffer;
    AudioBufferList *buffer = io->inputBuffers[inputBufferIndex];
    buffer->mNumberBuffers = io->interleaved ? 1 : io->numberOfChannels;
    buffer->mBuffers[0].mNumberChannels = io->interleaved ? io->numberOfChannels : 1;
    buffer->mBuffers[0].mDataByteSize = MAXFRAMES * 4 * buffer->mBuffers[0].mNumberChannels;
    for (unsigned int n = 1; n < buffer->mNumberBuffers; n++) {
        buffer->mBuffers[n].mDataByteSize = buffer->mBuffers[0].mDataByteSize;
        buffer->mBuffers[n].mNumberChannels = buffer->mBuffers[0].mNumberChannels;
    }
    bool hasInput = !AudioUnitRender(io->inputUnit, ioActionFlags, inTimeStamp, inBusNumber, inNumberFrames, buffer);
    if (hasInput) {
        io->inputFrames[inputBufferIndex] = inNumberFrames;
        io->inputWriteBuffer = atomicExchange(&io->pendingInputBuffer, inputBufferIndex | INPUT_BUFFER_READY) & INPUT_BUFFER_INDEX_MASK;
    }
    
    if (hasInput && !io->outputUnit && io->ioCallback) {
        if (io->interleaved) {
            float *inputBuffers[1] = { (float *)buffer->mBuffers[0].mData };
            io->ioCallback(io->ioClientdata, inputBuffers, NULL, inNumberFrames, io->samplerate, inTimeStamp->mHostTime);
        } else io->ioCallback(io->ioClientdata, io->inputBufs[inputBufferIndex], NULL, inNumberFrames, io->samplerate, inTimeStamp->mHostTime);
    }
    return noErr;
}

static OSStatus macOSAudioOutputCallback(void *inRefCon, AudioUnitRenderActionFlags *ioActionFlags, const AudioTimeStamp *inTimeStamp, __attribute__((unused)) UInt32 inBusNumber, UInt32 inNumberFrames, AudioBufferList *ioData) {
    macOSIO *io = (__bridge macOSIO *)inRefCon;
    if (io->stopped) return noErr;
    
    if (io->lastPreferredBufferSizeMs != YubyAudioIO::preferredBufferSizeMs) {
        io->lastPreferredBufferSizeMs = YubyAudioIO::preferredBufferSizeMs;
        [io performSelectorOnMainThread:@selector(setBufferSizeOnMainThread) withObject:nil waitUntilDone:NO];
    }
    
    if (((inNumberFrames & 7) != 0) || (inNumberFrames < 32) || (inNumberFrames > MAXFRAMES) || ((io->interleaved ? ioData->mBuffers[0].mNumberChannels : ioData->mNumberBuffers) != io->numberOfChannels)) return kAudioUnitErr_InvalidParameter;
    bool silence = true;
    
    if (io->ioCallback) {
        unsigned int pendingInputBuffer = __sync_val_compare_and_swap(&io->pendingInputBuffer, 0, 0);
        bool hasInput = pendingInputBuffer & INPUT_BUFFER_READY;
        if (hasInput) io->inputReadBuffer = atomicExchange(&io->pendingInputBuffer, io->inputReadBuffer) & INPUT_BUFFER_INDEX_MASK;
        unsigned int inputBufferIndex = io->inputReadBuffer;
        hasInput = hasInput && (io->inputFrames[inputBufferIndex] == inNumberFrames);
        if (io->interleaved) {
            float *inputBuffers[1] = { hasInput ? (float *)io->inputBuffers[inputBufferIndex]->mBuffers[0].mData : NULL };
            float *outputBuffers[1] = { (float *)ioData->mBuffers[0].mData };
            silence = !io->ioCallback(io->ioClientdata, hasInput ? inputBuffers : NULL, outputBuffers, inNumberFrames, io->samplerate, inTimeStamp->mHostTime);
        } else {
            float **inputs = hasInput ? io->inputBufs[inputBufferIndex] : NULL;
            for (unsigned int n = 0; n < io->numberOfChannels; n++) io->outputBufs[n] = (float *)ioData->mBuffers[n].mData;
            silence = !io->ioCallback(io->ioClientdata, inputs, io->outputBufs, inNumberFrames, io->samplerate, inTimeStamp->mHostTime);
        }
    }
    
    if (silence) { // Despite of ioActionFlags, it outputs garbage sometimes, so must zero the buffers:
        *ioActionFlags |= kAudioUnitRenderAction_OutputIsSilence;
        for (unsigned int n = 0; n < ioData->mNumberBuffers; n++) memset(ioData->mBuffers[n].mData, 0, ioData->mBuffers[n].mDataByteSize);
    };
    
    return noErr;
}

- (void)setDeviceOnMainThread:(AudioDeviceID)deviceID {
    if (stopped || (audioDeviceID == deviceID)) return; else audioDeviceID = deviceID;
    lastPreferredBufferSizeMs = YubyAudioIO::preferredBufferSizeMs;
    [self updateDefaultDeviceListenersOnMainThread];
    [self createAudioUnitsOnMainThread];
}

- (id)initOnMainThreadWithIOCallback:(audioProcessingCallback)apc audioProcessingClientdata:(void *)acd mainCallback:(YubyAudioIOCallback)mcc mainClientdata:(void *)mcd numberOfChannels:(unsigned int)nc interleaved:(bool)i inputEnabled:(bool)ie outputEnabled:(bool)oe hasRecordPermission:(bool)hasRecordPermission audioDeviceID:(AudioDeviceID)aid {
    self = [super init];
    if (!self) return nil;
    ioCallback = apc;
    mainCallback = mcc;
    ioClientdata = acd;
    mainClientdata = mcd;
    numberOfChannels = nc;
    interleaved = i;
    inputEnabled = ie && hasRecordPermission;
    inputRequested = ie;
    outputEnabled = oe;
    lastPreferredBufferSizeMs = YubyAudioIO::preferredBufferSizeMs;
    audioDeviceID = aid;
    inputDeviceID = outputDeviceID = 0xffffffff;
    stopped = listeningDefaultInput = listeningDefaultOutput = false;
    inputWriteBuffer = 0;
    inputReadBuffer = 2;
    for (unsigned int n = 0; n < INPUT_BUFFER_COUNT; n++) inputFrames[n] = 0;
    pendingInputBuffer = 1;
    
    unsigned int numBufs = interleaved ? 1 : numberOfChannels;
    for (unsigned int b = 0; b < INPUT_BUFFER_COUNT; b++) {
        inputBuffers[b] = (AudioBufferList *)malloc(offsetof(AudioBufferList, mBuffers[0]) + sizeof(AudioBuffer) * numBufs);
        if (!inputBuffers[b]) abort(); else inputBuffers[b]->mNumberBuffers = numBufs;
    }
    if (!interleaved) {
        for (unsigned int b = 0; b < INPUT_BUFFER_COUNT; b++) inputBufs[b] = (float **)malloc(sizeof(float *) * numberOfChannels);
        outputBufs = (float **)malloc(sizeof(float *) * numberOfChannels);
        if (!inputBufs[0] || !inputBufs[1] || !inputBufs[2] || !outputBufs) abort();
    }
    for (unsigned int n = 0; n < numBufs; n++) {
        for (unsigned int b = 0; b < INPUT_BUFFER_COUNT; b++) {
            inputBuffers[b]->mBuffers[n].mNumberChannels = interleaved ? numberOfChannels : 1;
            inputBuffers[b]->mBuffers[n].mDataByteSize = MAXFRAMES * 4 * inputBuffers[b]->mBuffers[n].mNumberChannels;
            inputBuffers[b]->mBuffers[n].mData = calloc(1, inputBuffers[b]->mBuffers[n].mDataByteSize);
            if (!inputBuffers[b]->mBuffers[n].mData) abort();
            if (!interleaved) inputBufs[b][n] = (float *)inputBuffers[b]->mBuffers[n].mData;
        }
    }
    
    CFRunLoopRef runLoop = NULL;
    AudioObjectPropertyAddress rladdress = { kAudioHardwarePropertyRunLoop, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain };
    AudioObjectSetPropertyData(kAudioObjectSystemObject, &rladdress, 0, NULL, sizeof(CFRunLoopRef), &runLoop);
    [self updateDefaultDeviceListenersOnMainThread];
    [self createAudioUnitsOnMainThread];
    return self;
}

- (void)onRecordPermissionChangedOnMainThread:(bool)hasRecordPermission {
    inputEnabled = hasRecordPermission ? inputRequested : false;
    [self createAudioUnitsOnMainThread];
}

- (void)resetOnMainThread {
    destroyAudioUnit(&inputUnit);
    destroyAudioUnit(&outputUnit);
    if (map.inputDeviceName) free((char *)map.inputDeviceName);
    if (map.outputDeviceName) free((char *)map.outputDeviceName);
    map.inputDeviceName = map.outputDeviceName = NULL;
    map.numInputChannels = map.numOutputChannels = 0;
    inputDeviceID = outputDeviceID = 0xffffffff;
    inputWriteBuffer = 0;
    inputReadBuffer = 2;
    for (unsigned int n = 0; n < INPUT_BUFFER_COUNT; n++) inputFrames[n] = 0;
    pendingInputBuffer = 1;
}

- (void)stopAndDeallocOnMainThread {
    if (stopped) return; else stopped = true;
    [NSObject cancelPreviousPerformRequestsWithTarget:self];
    AudioObjectPropertyAddress inputAddress = { kAudioHardwarePropertyDefaultInputDevice, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain };
    AudioObjectPropertyAddress outputAddress = { kAudioHardwarePropertyDefaultOutputDevice, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain };
    if (listeningDefaultInput) AudioObjectRemovePropertyListener(kAudioObjectSystemObject, &inputAddress, macOSDefaultDeviceChangedCallback, (__bridge void *)self);
    if (listeningDefaultOutput) AudioObjectRemovePropertyListener(kAudioObjectSystemObject, &outputAddress, macOSDefaultDeviceChangedCallback, (__bridge void *)self);
    listeningDefaultInput = listeningDefaultOutput = false;
    [self resetOnMainThread];
    for (unsigned int b = 0; b < INPUT_BUFFER_COUNT; b++) {
        if (inputBuffers[b]) for (unsigned int n = 0; n < (interleaved ? 1 : numberOfChannels); n++) {
            if (inputBuffers[b]->mBuffers[n].mData) free(inputBuffers[b]->mBuffers[n].mData); inputBuffers[b]->mBuffers[n].mData = NULL;
        }
        if (inputBuffers[b]) free(inputBuffers[b]); inputBuffers[b] = NULL;
        if (inputBufs[b]) free(inputBufs[b]); inputBufs[b] = NULL;
    }
    if (outputBufs) free(outputBufs);
    outputBufs = NULL;
    ARCRelease(self);
}

- (void)recreate {
    if (stopped) return; else if (![NSThread isMainThread]) [self performSelectorOnMainThread:@selector(recreate) withObject:nil waitUntilDone:NO];
    else {
        [NSObject cancelPreviousPerformRequestsWithTarget:self selector:@selector(createAudioUnitsOnMainThread) object:nil];
        [self performSelector:@selector(createAudioUnitsOnMainThread) withObject:nil afterDelay:0.0];
    }
}

static OSStatus macOSDefaultDeviceChangedCallback(__attribute__((unused)) AudioObjectID inObjectID, __attribute__((unused)) UInt32 inNumberAddresses, __attribute__((unused)) const AudioObjectPropertyAddress inAddresses[], void *inClientData) {
    [(__bridge macOSIO *)inClientData recreate];
    return noErr;
}

static void macOSStreamFormatChangedCallback(void *inRefCon, AudioUnit inUnit, __attribute__((unused)) AudioUnitPropertyID inID, AudioUnitScope inScope, AudioUnitElement inElement) {
    macOSIO *io = (__bridge macOSIO *)inRefCon;
    if (((inUnit == io->outputUnit) && (inScope == kAudioUnitScope_Output) && (inElement == 0)) ||
        ((inUnit == io->inputUnit ) && (inScope == kAudioUnitScope_Input ) && (inElement == 1))) [io recreate];
}

- (void)updateDefaultDeviceListenersOnMainThread {
    bool needsDefaultInput = inputRequested && ((audioDeviceID == 0xffffffff) || !getAudioDeviceChannelCountOnMainThread(audioDeviceID, true));
    if (needsDefaultInput != listeningDefaultInput) {
        AudioObjectPropertyAddress inputAddress = { kAudioHardwarePropertyDefaultInputDevice, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain };
        if (needsDefaultInput) AudioObjectAddPropertyListener(kAudioObjectSystemObject, &inputAddress, macOSDefaultDeviceChangedCallback, (__bridge void *)self);
        else AudioObjectRemovePropertyListener(kAudioObjectSystemObject, &inputAddress, macOSDefaultDeviceChangedCallback, (__bridge void *)self);
        listeningDefaultInput = needsDefaultInput;
    }
    bool needsDefaultOutput = outputEnabled && (audioDeviceID == 0xffffffff);
    if (needsDefaultOutput != listeningDefaultOutput) {
        AudioObjectPropertyAddress outputAddress = { kAudioHardwarePropertyDefaultOutputDevice, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain };
        if (needsDefaultOutput) AudioObjectAddPropertyListener(kAudioObjectSystemObject, &outputAddress, macOSDefaultDeviceChangedCallback, (__bridge void *)self);
        else AudioObjectRemovePropertyListener(kAudioObjectSystemObject, &outputAddress, macOSDefaultDeviceChangedCallback, (__bridge void *)self);
        listeningDefaultOutput = needsDefaultOutput;
    }
}

- (void)setBufferSizeOnMainThread {
    if (stopped || (samplerate < 1)) return;
    float sec = (float)lastPreferredBufferSizeMs;
    if (sec < 1.0f) sec = 0.001f; else sec *= 0.001f;
    unsigned int frames = (unsigned int)powf(2.0f, floorf(log2f(float(samplerate) * sec)));
    if (frames > MAXFRAMES) frames = MAXFRAMES;
    AudioObjectPropertyAddress address = { kAudioDevicePropertyBufferFrameSize, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain };
    if (inputDeviceID != 0xffffffff) AudioObjectSetPropertyData(inputDeviceID, &address, 0, NULL, sizeof(UInt32), &frames);
    if ((outputDeviceID != 0xffffffff) && (outputDeviceID != inputDeviceID)) AudioObjectSetPropertyData(outputDeviceID, &address, 0, NULL, sizeof(UInt32), &frames);
}

static void makeStreamFormat(AudioUnit au, AudioStreamBasicDescription *format, bool input, unsigned int numberOfChannels, bool interleaved) {
    UInt32 size = 0;
    AudioUnitGetPropertyInfo(au, kAudioUnitProperty_StreamFormat, input ? kAudioUnitScope_Input : kAudioUnitScope_Output, input ? 1 : 0, &size, NULL);
    AudioUnitGetProperty(au, kAudioUnitProperty_StreamFormat, input ? kAudioUnitScope_Input : kAudioUnitScope_Output, input ? 1 : 0, format, &size);
    format->mFormatID = kAudioFormatLinearPCM;
    format->mFormatFlags = (AudioFormatFlags)kAudioFormatFlagIsFloat | (AudioFormatFlags)kAudioFormatFlagsNativeEndian | (AudioFormatFlags)kAudioFormatFlagIsPacked;
    if (!interleaved) format->mFormatFlags |= kAudioFormatFlagIsNonInterleaved;
    format->mChannelsPerFrame = numberOfChannels;
    format->mBitsPerChannel = 32;
    format->mFramesPerPacket = 1;
    format->mBytesPerFrame = format->mBytesPerPacket = interleaved ? (numberOfChannels * 4) : 4;
}

static AudioUnit createAudioUnitOnMainThread(AudioComponent component, bool isInput) {
    AudioUnit au = NULL;
    if (AudioComponentInstanceNew(component, &au) || (au == NULL)) return NULL;
    UInt32 value = isInput ? 1 : 0;
    if (AudioUnitSetProperty(au, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Input, 1, &value, sizeof(value))) {
        destroyAudioUnit(&au);
        return NULL;
    }
    value = isInput ? 0 : 1;
    if (AudioUnitSetProperty(au, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Output, 0, &value, sizeof(value))) {
        destroyAudioUnit(&au);
        return NULL;
    }
    return au;
}

static AudioDeviceID getDefaultAudioDeviceOnMainThread(bool input) {
    AudioDeviceID deviceID = 0;
    UInt32 size = sizeof(AudioDeviceID);
    AudioObjectPropertyAddress address = { input ? kAudioHardwarePropertyDefaultInputDevice : kAudioHardwarePropertyDefaultOutputDevice, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain };
    return (AudioObjectGetPropertyData(kAudioObjectSystemObject, &address, 0, NULL, &size, &deviceID) == noErr) ? deviceID : 0xffffffff;
}

- (void)createAudioUnitsOnMainThread {
    if (stopped) return; else [self resetOnMainThread];
    AudioComponentDescription desc = { kAudioUnitType_Output, kAudioUnitSubType_HALOutput, kAudioUnitManufacturer_Apple, 0, 0 };
    AudioComponent component = AudioComponentFindNext(NULL, &desc);
    
    if (outputEnabled) {
        AudioUnit outau = createAudioUnitOnMainThread(component, false);
        if (outau) {
            AudioDeviceID device = audioDeviceID != 0xffffffff ? audioDeviceID : getDefaultAudioDeviceOnMainThread(false);
            if ((device != 0xffffffff) && !AudioUnitSetProperty(outau, kAudioOutputUnitProperty_CurrentDevice, kAudioUnitScope_Global, 0, &device, sizeof(device))) {
                AudioUnitAddPropertyListener(outau, kAudioUnitProperty_StreamFormat, macOSStreamFormatChangedCallback, (__bridge void *)self);
                AudioStreamBasicDescription format;
                makeStreamFormat(outau, &format, false, numberOfChannels, interleaved);
                samplerate = (unsigned int)format.mSampleRate;
                if (!AudioUnitSetProperty(outau, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &format, sizeof(format))) {
                    AURenderCallbackStruct callbackStruct;
                    callbackStruct.inputProc = macOSAudioOutputCallback;
                    callbackStruct.inputProcRefCon = (__bridge void *)self;
                    if (!AudioUnitSetProperty(outau, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &callbackStruct, sizeof(callbackStruct)) && !AudioUnitInitialize(outau)) {
                        outputUnit = outau; outau = NULL;
                        outputDeviceID = device;
                        map.outputDeviceName = getAudioDeviceNameOnMainThread(device);
                        map.numOutputChannels = getAudioDeviceChannelCountOnMainThread(device, false);
                    }
                }
            }
            destroyAudioUnit(&outau);
        }
    }

    if (inputEnabled) {
        AudioUnit inau = createAudioUnitOnMainThread(component, true);
        if (inau) {
            AudioDeviceID device = audioDeviceID;
            if (device != 0xffffffff) {
                if (!getAudioDeviceChannelCountOnMainThread(device, true)) device = getDefaultAudioDeviceOnMainThread(true);
            } else device = getDefaultAudioDeviceOnMainThread(true);
            if ((device != 0xffffffff) && !AudioUnitSetProperty(inau, kAudioOutputUnitProperty_CurrentDevice, kAudioUnitScope_Global, 0, &device, sizeof(device))) {
                AudioUnitAddPropertyListener(inau, kAudioUnitProperty_StreamFormat, macOSStreamFormatChangedCallback, (__bridge void *)self);
                AudioStreamBasicDescription format;
                makeStreamFormat(inau, &format, true, numberOfChannels, interleaved);
                if (outputEnabled) format.mSampleRate = samplerate; else samplerate = (unsigned int)format.mSampleRate;
                if (!AudioUnitSetProperty(inau, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 1, &format, sizeof(format))) {
                    AURenderCallbackStruct callbackStruct;
                    callbackStruct.inputProc = macOSAudioInputCallback;
                    callbackStruct.inputProcRefCon = (__bridge void *)self;
                    if (!AudioUnitSetProperty(inau, kAudioOutputUnitProperty_SetInputCallback, kAudioUnitScope_Global, 0, &callbackStruct, sizeof(callbackStruct)) && !AudioUnitInitialize(inau)) {
                        inputUnit = inau; inau = NULL;
                        inputDeviceID = device;
                        map.inputDeviceName = getAudioDeviceNameOnMainThread(device);
                        map.numInputChannels = getAudioDeviceChannelCountOnMainThread(device, true);
                    }
                }
            }
            destroyAudioUnit(&inau);
        }
    }
    
    [self mapChannelsOnMainThread];
    [self setBufferSizeOnMainThread];
    if (inputUnit) AudioOutputUnitStart(inputUnit);
    if (outputUnit) AudioOutputUnitStart(outputUnit);
}

- (void)mapChannelsOnMainThread {
    if (stopped) return;
    for (int n = 0; n < MAX_MAPPABLE_CHANNELS; n++) map.inputMap[n] = map.outputMap[n] = -1;
    mainCallback(mainClientdata, YubyAudioIOEvent::MapChannels, &map);
    applyChannelMapOnMainThread(outputUnit, map.outputMap, map.numOutputChannels, true);
    applyChannelMapOnMainThread(inputUnit, map.inputMap, numberOfChannels, false);
}

@end

@interface macOSHandler: NSObject
@end
@implementation macOSHandler {
    YubyAudioIOCallback callback;
    void *clientdata;
    macOSIO *io;
    NSMutableDictionary<NSString *, NSNumber *>*audioDeviceIds;
    NSTimer *permissionTimer;
    AVAudioApplicationRecordPermission lastRecordPermission;
    AudioDeviceID selectedAudioDevice;
    bool stopped, inputEnabled;
}

+ (void)initialize:(YubyAudioIOCallback)cb clientdata:(void *)cd set:(macOSHandler * ARCStrong *)s {
    if ([NSThread isMainThread]) {
        *s = [[macOSHandler alloc] initOnMainThreadWithCallback:cb clientdata:cd];
        [*s startOnMainThread];
    } else dispatch_sync(dispatch_get_main_queue(), ^{
        *s = [[macOSHandler alloc] initOnMainThreadWithCallback:cb clientdata:cd];
        [*s startOnMainThread];
    });
}

- (id)initOnMainThreadWithCallback:(YubyAudioIOCallback)cb clientdata:(void *)cd {
    self = [super init];
    if (!self) return nil;
    callback = cb;
    clientdata = cd;
    audioDeviceIds = [[NSMutableDictionary alloc] init];
    selectedAudioDevice = 0xffffffff;
    stopped = false;
    return self;
}

- (void)startOnMainThread {
    CFRunLoopRef runLoop = NULL;
    AudioObjectPropertyAddress rladdress = { kAudioHardwarePropertyRunLoop, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain };
    AudioObjectSetPropertyData(kAudioObjectSystemObject, &rladdress, 0, NULL, sizeof(CFRunLoopRef), &runLoop);
    AudioObjectPropertyAddress hdaddress = { kAudioHardwarePropertyDevices, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain };
    AudioObjectAddPropertyListener(kAudioObjectSystemObject, &hdaddress, macOSDevicesChangedCallback, (__bridge void *)self);
    lastRecordPermission = [AVAudioApplication sharedInstance].recordPermission;
    [self onRecordPermissionChangedOnMainThread];
    [self callDevicesChangedCallbackOnMainThread];
    permissionTimer = [NSTimer scheduledTimerWithTimeInterval:1.0 target:self selector:@selector(checkInputPermissionOnMainThread) userInfo:nil repeats:YES];
}

- (void)start:(audioProcessingCallback)apc audioProcessingClientdata:(void *)acd numberOfChannels:(unsigned int)nc interleaved:(bool)i inputEnabled:(bool)ie outputEnabled:(bool)oe {
    if ([NSThread isMainThread]) {
        [io stopAndDeallocOnMainThread]; io = nil;
        if (stopped) return; else inputEnabled = ie;
        if (ie && (lastRecordPermission == AVAudioApplicationRecordPermissionUndetermined)) [self requestRecordPermissionOnMainThread];
        io = [[macOSIO alloc] initOnMainThreadWithIOCallback:apc audioProcessingClientdata:acd mainCallback:callback mainClientdata:clientdata numberOfChannels:nc interleaved:i inputEnabled:ie outputEnabled:oe hasRecordPermission:lastRecordPermission == AVAudioApplicationRecordPermissionGranted audioDeviceID:selectedAudioDevice];
    } else dispatch_sync(dispatch_get_main_queue(), ^{
        [io stopAndDeallocOnMainThread]; io = nil;
        if (stopped) return; else inputEnabled = ie;
        if (ie && (lastRecordPermission == AVAudioApplicationRecordPermissionUndetermined)) [self requestRecordPermissionOnMainThread];
        io = [[macOSIO alloc] initOnMainThreadWithIOCallback:apc audioProcessingClientdata:acd mainCallback:callback mainClientdata:clientdata numberOfChannels:nc interleaved:i inputEnabled:ie outputEnabled:oe hasRecordPermission:lastRecordPermission == AVAudioApplicationRecordPermissionGranted audioDeviceID:selectedAudioDevice];
    });
}

- (void)stop {
    if ([NSThread isMainThread]) {
        if (!stopped) { [io stopAndDeallocOnMainThread]; io = nil; }
    } else [self performSelectorOnMainThread:@selector(stop) withObject:nil waitUntilDone:YES];
}

- (void)mapChannels {
    if ([NSThread isMainThread]) {
        if (!stopped) [io mapChannelsOnMainThread];
    } else [self performSelectorOnMainThread:@selector(mapChannels) withObject:nil waitUntilDone:NO];
}

- (void)stopAndDealloc {
    if (![NSThread isMainThread]) {
        [self performSelectorOnMainThread:@selector(stopAndDealloc) withObject:nil waitUntilDone:YES];
        return;
    }
    if (stopped) return; else stopped = true;
    [io stopAndDeallocOnMainThread]; io = nil;
    ARCRelease(audioDeviceIds); audioDeviceIds = nil;
    [NSObject cancelPreviousPerformRequestsWithTarget:self];
    [permissionTimer invalidate]; permissionTimer = nil;
    AudioObjectPropertyAddress hdaddress = { kAudioHardwarePropertyDevices, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain };
    AudioObjectRemovePropertyListener(kAudioObjectSystemObject, &hdaddress, macOSDevicesChangedCallback, (__bridge void *)self);
    ARCRelease(self);
}

- (void)checkInputPermissionOnMainThread {
    if (stopped) return;
    AVAudioApplicationRecordPermission p = [AVAudioApplication sharedInstance].recordPermission;
    if (p == lastRecordPermission) return; else lastRecordPermission = p;
    [self onRecordPermissionChangedOnMainThread];
}

- (void)onRecordPermissionChangedOnMainThread {
    if (!stopped) switch (lastRecordPermission) {
        case AVAudioApplicationRecordPermissionGranted: {
            bool t = true;
            callback(clientdata, YubyAudioIOEvent::InputPermissionChanged, &t);
            [io onRecordPermissionChangedOnMainThread:true];
        } break;
        case AVAudioApplicationRecordPermissionDenied: {
            bool f = false;
            callback(clientdata, YubyAudioIOEvent::InputPermissionChanged, &f);
            [io onRecordPermissionChangedOnMainThread:false];
        } break;
        case AVAudioApplicationRecordPermissionUndetermined: if (inputEnabled) [self requestRecordPermissionOnMainThread]; break;
    }
}

- (void)requestRecordPermissionOnMainThread {
    macOSHandler *retainedSelf = ARCRetain(self);
    [AVAudioApplication requestRecordPermissionWithCompletionHandler:^(BOOL granted) {
        dispatch_async(dispatch_get_main_queue(), ^{
            if (!retainedSelf->stopped) {
                retainedSelf->lastRecordPermission = granted ? AVAudioApplicationRecordPermissionGranted : AVAudioApplicationRecordPermissionDenied;
                bool g = granted;
                retainedSelf->callback(retainedSelf->clientdata, YubyAudioIOEvent::InputPermissionChanged, (void *)&g);
                [retainedSelf->io onRecordPermissionChangedOnMainThread:g];
            }
            ARCRelease(retainedSelf);
        });
    }];
}

static OSStatus macOSDevicesChangedCallback(__attribute__((unused)) AudioObjectID inObjectID, __attribute__((unused)) UInt32 inNumberAddresses, __attribute__((unused)) const AudioObjectPropertyAddress inAddresses[], void *inClientData) {
    macOSHandler *h = (__bridge macOSHandler *)inClientData;
    if ([NSThread isMainThread]) [h callDevicesChangedCallbackOnMainThread]; else [h performSelectorOnMainThread:@selector(callDevicesChangedCallbackOnMainThread) withObject:nil waitUntilDone:NO];
    return noErr;
}

- (void)callDevicesChangedCallbackOnMainThread {
    if (stopped) return;
    AudioObjectPropertyAddress allDevices = { kAudioHardwarePropertyDevices, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain };
    UInt32 size = 0;
    if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &allDevices, 0, NULL, &size) || !size) return;
    unsigned int numDevices = size / sizeof(AudioDeviceID);
    AudioDeviceID *devices = (AudioDeviceID *)malloc(sizeof(AudioDeviceID) * numDevices);
    if (!devices) return; else if (!AudioObjectGetPropertyData(kAudioObjectSystemObject, &allDevices, 0, NULL, &size, devices)) {
        AudioDeviceList *result = (AudioDeviceList *)malloc(sizeof(AudioDeviceList));
        if (result) {
            result->devices = (AudioDeviceInfo *)malloc(sizeof(AudioDeviceInfo) * (numDevices + 1));
            if (result->devices) {
                memset(result->devices, 0, sizeof(AudioDeviceInfo) * (numDevices + 1));
                result->devices->name = strdup("Default (System Preference)");
                if (result->devices->name) {
                    result->length = 1;
                    AudioDeviceID defaultInput = getDefaultAudioDeviceOnMainThread(true), defaultOutput = getDefaultAudioDeviceOnMainThread(false);
                    result->devices->numInputChannels = defaultInput == 0xffffffff ? 0 : getAudioDeviceChannelCountOnMainThread(defaultInput, true);
                    result->devices->numOutputChannels = defaultOutput == 0xffffffff ? 0 : getAudioDeviceChannelCountOnMainThread(defaultOutput, false);
                    [audioDeviceIds removeAllObjects];
                    bool found = selectedAudioDevice == 0xffffffff;
                    for (unsigned int n = 0; n < numDevices; n++) {
                        AudioDeviceInfo *i = result->devices + result->length;
                        i->numInputChannels = getAudioDeviceChannelCountOnMainThread(devices[n], true);
                        i->numOutputChannels = getAudioDeviceChannelCountOnMainThread(devices[n], false);
                        if (i->numInputChannels + i->numOutputChannels < 1) continue; else i->name = getAudioDeviceNameOnMainThread(devices[n]);
                        if (!i->name) continue; else result->length++;
                        if (devices[n] == selectedAudioDevice) found = true;
                        [audioDeviceIds setObject:[NSNumber numberWithUnsignedInt:devices[n]] forKey:[NSString stringWithUTF8String:i->name]];
                    }
                    if (!found) {
                        selectedAudioDevice = 0xffffffff;
                        [io setDeviceOnMainThread:selectedAudioDevice];
                    }
                    callback(clientdata, YubyAudioIOEvent::DevicesChanged, result);
                    for (unsigned int n = 0; n <= numDevices; n++) if (result->devices[n].name) free((char *)result->devices[n].name);
                }
                free(result->devices);
            }
            free(result);
        }
    }
    free(devices);
}

- (void)setDevice:(const char *)deviceName {
    NSString *s = deviceName ? [[NSString alloc] initWithUTF8String:deviceName] : nil;
    if ([NSThread isMainThread]) {
        NSNumber *v = s ? audioDeviceIds[s] : nil;
        selectedAudioDevice = v ? v.unsignedIntValue : 0xffffffff;
        if (!stopped) [io setDeviceOnMainThread:selectedAudioDevice];
        ARCRelease(s);
    } else dispatch_sync(dispatch_get_main_queue(), ^{
        NSNumber *v = s ? audioDeviceIds[s] : nil;
        selectedAudioDevice = v ? v.unsignedIntValue : 0xffffffff;
        if (!stopped) [io setDeviceOnMainThread:selectedAudioDevice];
        ARCRelease(s);
    });
}

@end

#else
#import <UIKit/UIKit.h>
#include <mach/mach_time.h>
#define AppleHandler iosHandler

@interface iosIO: NSObject
@end
@implementation iosIO {
    ChannelMap map;
    audioProcessingCallback ioCallback;
    YubyAudioIOCallback mainCallback;
    void *ioClientdata, *mainClientdata;
    float **inputBufs, **outputBufs;
    AudioBufferList *inputBuffer;
    AudioComponentInstance audioUnit;
    uint64_t lastCallbackTime;
    unsigned int lastPreferredBufferSizeMs;
    int numberOfChannels, silenceFrames, samplerate;
    bool audioUnitShouldRun, audioUnitRuns, background, inputEnabled, inputRequested, outputEnabled, interleaved, interrupted, stopped;
}

- (id)initOnMainThreadWithIOCallback:(audioProcessingCallback)apc audioProcessingClientdata:(void *)acd mainCallback:(YubyAudioIOCallback)mcc mainClientdata:(void *)mcd numberOfChannels:(unsigned int)nc interleaved:(bool)i inputEnabled:(bool)ie outputEnabled:(bool)oe hasRecordPermission:(bool)hasRecordPermission {
    self = [super init];
    if (!self) return nil;
    mainCallback = mcc;
    ioCallback = apc;
    mainClientdata = mcd;
    ioClientdata = acd;
    numberOfChannels = nc;
    interleaved = i;
    inputEnabled = ie && hasRecordPermission;
    inputRequested = ie;
    outputEnabled = oe;
    lastPreferredBufferSizeMs = YubyAudioIO::preferredBufferSizeMs;
    background = audioUnitRuns = audioUnitShouldRun = interrupted = stopped = false;
    
    if (!interleaved) {
        outputBufs = (float **)malloc(sizeof(float *) * numberOfChannels);
        if (!outputBufs) abort();
    }
    
    [self restartAudioOnMainThread];
    [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(onForeground) name:UIApplicationDidBecomeActiveNotification object:nil];
    [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(onBackground) name:UIApplicationDidEnterBackgroundNotification object:nil];
    [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(onMediaServerReset:) name:AVAudioSessionMediaServicesWereResetNotification object:[AVAudioSession sharedInstance]];
    [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(onAudioSessionInterrupted:) name:AVAudioSessionInterruptionNotification object:[AVAudioSession sharedInstance]];
    [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(onRouteChange:) name:AVAudioSessionRouteChangeNotification object:[AVAudioSession sharedInstance]];
    return self;
}

- (void)onRecordPermissionChangedOnMainThread:(bool)hasRecordPermission {
    inputEnabled = hasRecordPermission ? inputRequested : false;
    [self restartAudioOnMainThread];
}

- (void)everySecondOnMainThread {
    if (stopped || interrupted) return;
    if (silenceFrames > samplerate) {
        audioUnitRuns = false;
        if (audioUnit) AudioOutputUnitStop(audioUnit);
        silenceFrames = 0;
    } else if (!background && audioUnitShouldRun) {
        if (!audioUnitRuns) [self restartAudioOnMainThread];
        else {
            mach_timebase_info_data_t timebase;
            mach_timebase_info(&timebase);
            uint64_t diff = mach_absolute_time() - lastCallbackTime;
            diff *= timebase.numer;
            diff /= timebase.denom;
            if (diff > 1000000000) [self restartAudioOnMainThread]; // It didn't call the audio processing callback in the past second.
        }
    }
}

- (void)stopAndDeallocOnMainThread {
    if (stopped) return; else stopped = true;
    [NSObject cancelPreviousPerformRequestsWithTarget:self];
    destroyAudioUnit(&audioUnit);
    [[AVAudioSession sharedInstance] setActive:NO error:nil];
    [[NSNotificationCenter defaultCenter] removeObserver:self];
    if (inputBuffer) {
        if (interleaved) free(inputBuffer->mBuffers[0].mData);
        else for (int n = 0; n < numberOfChannels; n++) free(inputBuffer->mBuffers[n].mData);
        free(inputBuffer);
    };
    if (inputBufs) free(inputBufs);
    if (outputBufs) free(outputBufs);
    if (map.inputDeviceName) free((char *)map.inputDeviceName);
    ARCRelease(self);
}

- (void)onMediaServerReset:(NSNotification *)notification {
    if (stopped) return;
    if ([NSThread isMainThread]) [self restartAudioOnMainThread]; else [self performSelectorOnMainThread:@selector(restartAudioOnMainThread) withObject:nil waitUntilDone:NO];
}

- (void)endInterruption {
    if (stopped) return;
    if (![NSThread isMainThread]) {
        [self performSelectorOnMainThread:@selector(endInterruption) withObject:nil waitUntilDone:NO];
        return;
    }
    interrupted = false;
    if (audioUnitRuns || !audioUnitShouldRun) return;
    bool f = false;
    mainCallback(mainClientdata, YubyAudioIOEvent::Interruption, &f);
    [self restartAudioOnMainThread];
}

- (void)beginInterruptionOnMainThread { // phone call, etc.
    if (stopped) return;
    interrupted = true;
    if (audioUnitRuns) {
        bool t = true;
        mainCallback(mainClientdata, YubyAudioIOEvent::Interruption, &t);
    }
    audioUnitRuns = false;
    if (audioUnit) AudioOutputUnitStop(audioUnit);
}

- (void)onAudioSessionInterrupted:(NSNotification *)notification {
    if (stopped) return;
    NSNumber *interruption = [notification.userInfo objectForKey:AVAudioSessionInterruptionTypeKey];
    if (interruption != nil) switch ([interruption intValue]) {
        case AVAudioSessionInterruptionTypeBegan: {
            if ([NSThread isMainThread]) [self beginInterruptionOnMainThread];
            else [self performSelectorOnMainThread:@selector(beginInterruptionOnMainThread) withObject:nil waitUntilDone:NO];
        } break;
        case AVAudioSessionInterruptionTypeEnded: {
            NSNumber *shouldResume = [notification.userInfo objectForKey:AVAudioSessionInterruptionOptionKey];
            if ((shouldResume == nil) || [shouldResume unsignedIntegerValue] == AVAudioSessionInterruptionOptionShouldResume) [self endInterruption];
        } break;
    }
}

- (void)onForeground {
    if (!background || stopped) return; else background = false;
    [self endInterruption];
}
- (void)onBackground { if (!stopped) background = true; }

- (void)onRouteChange:(NSNotification *)notification {
    if (stopped) return;
    if (![NSThread isMainThread]) {
        [self performSelectorOnMainThread:@selector(onRouteChange:) withObject:nil waitUntilDone:NO];
        return;
    }
    if (map.inputDeviceName) free((char *)map.inputDeviceName);
    map.inputDeviceName = map.outputDeviceName = NULL;
    map.numInputChannels = map.numOutputChannels = 0;
    for (AVAudioSessionPortDescription *port in [[[AVAudioSession sharedInstance] currentRoute] outputs]) {
        map.numOutputChannels = (int)port.channels.count;
        map.inputDeviceName = strdup([[port.portName stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]] UTF8String]);
        break;
    }
    if ([[AVAudioSession sharedInstance] isInputAvailable]) for (AVAudioSessionPortDescription *port in [[[AVAudioSession sharedInstance] currentRoute] inputs]) {
        map.numInputChannels = (int)port.channels.count;
        if (map.inputDeviceName == NULL) map.inputDeviceName = strdup([[port.portName stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]] UTF8String]);
        break;
    }
    if (!map.inputDeviceName) map.inputDeviceName = strdup("Unknown Audio Device");
    map.outputDeviceName = map.inputDeviceName;
    [self mapChannelsOnMainThread];
}

- (void)applyBuffersizeOnMainThread {
    if (self->stopped || (self->samplerate < 1)) return;
    float sec = (float)lastPreferredBufferSizeMs, sr = float(self->samplerate);
    if (sec < 1.0f) sec = 0.001f; else sec *= 0.001f;
    unsigned int frames = (unsigned int)powf(2.0f, floorf(log2f(sr * sec)));
    if (frames > MAXFRAMES) frames = MAXFRAMES;
    [[AVAudioSession sharedInstance] setPreferredIOBufferDuration:frames / sr error:NULL];
}

- (void)restartAudioOnMainThread {
    if (stopped || interrupted) return;
    audioUnitRuns = false;
    destroyAudioUnit(&audioUnit);
    [[AVAudioSession sharedInstance] setActive:NO error:nil];

    if (inputEnabled && !inputBuffer) {
        if (interleaved) {
            inputBuffer = (AudioBufferList *)malloc(offsetof(AudioBufferList, mBuffers[0]) + sizeof(AudioBuffer));
            if (!inputBuffer) abort(); else inputBuffer->mNumberBuffers = 1;
            inputBuffer->mBuffers[0].mData = calloc(1, MAXFRAMES * 4 * numberOfChannels);
            if (!inputBuffer->mBuffers[0].mData) abort();
            inputBuffer->mBuffers[0].mDataByteSize = MAXFRAMES * 4 * numberOfChannels;
            inputBuffer->mBuffers[0].mNumberChannels = numberOfChannels;
        } else {
            inputBuffer = (AudioBufferList *)malloc(offsetof(AudioBufferList, mBuffers[0]) + sizeof(AudioBuffer) * numberOfChannels);
            inputBufs = (float **)malloc(sizeof(float *) * numberOfChannels);
            if (!inputBuffer || !inputBufs) abort(); else inputBuffer->mNumberBuffers = numberOfChannels;
            for (int n = 0; n < numberOfChannels; n++) {
                inputBuffer->mBuffers[n].mData = inputBufs[n] = (float *)calloc(1, MAXFRAMES * 4);
                if (!inputBufs[n]) abort();
                inputBuffer->mBuffers[n].mDataByteSize = MAXFRAMES * 4;
                inputBuffer->mBuffers[n].mNumberChannels = 1;
            }
        }
    }

    if (inputEnabled && outputEnabled) [[AVAudioSession sharedInstance] setCategory:AVAudioSessionCategoryPlayAndRecord withOptions:IOS_AVAUDIOSESSIONCATEGORYOPTIONS error:NULL];
    else [[AVAudioSession sharedInstance] setCategory:inputEnabled ? AVAudioSessionCategoryRecord : AVAudioSessionCategoryPlayback withOptions:IOS_AVAUDIOSESSIONCATEGORYOPTIONS error:NULL];
    [[AVAudioSession sharedInstance] setMode:IOS_AVAUDIOSESSION_MODE error:NULL];
    if (IOS_PREFER_SPEAKER_OVER_RECEIVER) [[AVAudioSession sharedInstance] overrideOutputAudioPort:AVAudioSessionPortOverrideSpeaker error:nil];
    self->samplerate = IOS_PREFERRED_SAMPLERATE;
    [[AVAudioSession sharedInstance] setPreferredSampleRate:self->samplerate error:NULL];
    [self applyBuffersizeOnMainThread];
    [[AVAudioSession sharedInstance] setActive:YES error:NULL];
    audioUnit = [self createRemoteIOOnMainThread];
    [self onRouteChange:nil];
    audioUnitShouldRun = true;
    if ((audioUnit == NULL) || AudioOutputUnitStart(audioUnit)) return; else audioUnitRuns = true;
}

static void streamFormatChangedCallback(void *inRefCon, AudioUnit inUnit, AudioUnitPropertyID inID, AudioUnitScope inScope, AudioUnitElement inElement) {
    AudioStreamBasicDescription format;
    format.mSampleRate = 0;
    if ((inScope == kAudioUnitScope_Output) && (inElement == 0)) {
        UInt32 size = 0;
        AudioUnitGetPropertyInfo(inUnit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0, &size, NULL);
        AudioUnitGetProperty(inUnit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0, &format, &size);
    } else if ((inScope == kAudioUnitScope_Input) && (inElement == 1)) {
        UInt32 size = 0;
        AudioUnitGetPropertyInfo(inUnit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 1, &size, NULL);
        AudioUnitGetProperty(inUnit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 1, &format, &size);
    }
    if (format.mSampleRate == 0) return;
    int sr = (int)format.mSampleRate;
    __unsafe_unretained iosIO *self = (__bridge iosIO *)inRefCon;
    if (self->stopped || (self->samplerate == sr)) return; else self->samplerate = sr;
    [self performSelectorOnMainThread:@selector(applyBuffersizeOnMainThread) withObject:nil waitUntilDone:NO];
}

static OSStatus coreAudioProcessingCallback(void *inRefCon, AudioUnitRenderActionFlags *ioActionFlags, const AudioTimeStamp *inTimeStamp, UInt32 inBusNumber, UInt32 inNumberFrames, AudioBufferList *ioData) {
    __unsafe_unretained iosIO *self = (__bridge iosIO *)inRefCon;
    if (self->stopped) return noErr;
    self->lastCallbackTime = mach_absolute_time();

    if (!ioData) ioData = self->inputBuffer;
    if ((inNumberFrames & 7) != 0) {
        // Core Audio performs sample rate conversion, but received no streamFormatChangedCallback. Recreate audio I/O for perfect match with hardware.
        if (self->audioUnitRuns) {
            self->audioUnitRuns = false;
            [self performSelectorOnMainThread:@selector(restartAudioOnMainThread) withObject:nil waitUntilDone:NO];
        }
    }
    if (self->lastPreferredBufferSizeMs != YubyAudioIO::preferredBufferSizeMs) {
        self->lastPreferredBufferSizeMs = YubyAudioIO::preferredBufferSizeMs;
        [self performSelectorOnMainThread:@selector(applyBuffersizeOnMainThread) withObject:nil waitUntilDone:NO];
    }

    if ((inNumberFrames < 32) || (inNumberFrames > MAXFRAMES) || (int(self->interleaved ? ioData->mBuffers[0].mNumberChannels : ioData->mNumberBuffers) != self->numberOfChannels)) return kAudioUnitErr_InvalidParameter;

    bool silence = true;
    if (self->interleaved) {
        // Get audio input.
        float *inputBufs[1] = { NULL }, *outputBufs[1] = { (float *)ioData->mBuffers[0].mData };
        if (self->inputEnabled) {
            self->inputBuffer->mBuffers[0].mDataByteSize = MAXFRAMES * 4 * self->numberOfChannels;
            self->inputBuffer->mBuffers[0].mNumberChannels = self->numberOfChannels;
            self->inputBuffer->mNumberBuffers = 1;
            if (!AudioUnitRender(self->audioUnit, ioActionFlags, inTimeStamp, 1, inNumberFrames, self->inputBuffer)) inputBufs[0] = (float *)self->inputBuffer->mBuffers[0].mData;
        }
        // Make audio output.
        silence = !self->ioCallback(self->ioClientdata, self->inputEnabled && inputBufs[0] ? inputBufs : NULL, self->outputEnabled ? outputBufs : NULL, inNumberFrames, self->samplerate, inTimeStamp->mHostTime);
    } else {
        // Get audio input.
        float **inputBufs = NULL;
        if (self->inputEnabled) {
            for (int n = 0; n < self->numberOfChannels; n++) {
                self->inputBuffer->mBuffers[n].mDataByteSize = MAXFRAMES * 4;
                self->inputBuffer->mBuffers[n].mNumberChannels = 1;
            }
            self->inputBuffer->mNumberBuffers = self->numberOfChannels;
            if (!AudioUnitRender(self->audioUnit, ioActionFlags, inTimeStamp, 1, inNumberFrames, self->inputBuffer)) inputBufs = self->inputBufs;
        }
        // Make audio output.
        for (int n = 0; n < self->numberOfChannels; n++) self->outputBufs[n] = (float *)ioData->mBuffers[n].mData;
        silence = !self->ioCallback(self->ioClientdata, inputBufs, self->outputEnabled ? self->outputBufs : NULL, inNumberFrames, self->samplerate, inTimeStamp->mHostTime);
    }

    if (silence) { // Despite of ioActionFlags, it outputs garbage sometimes, so must zero the buffers:
        *ioActionFlags |= kAudioUnitRenderAction_OutputIsSilence;
        for (unsigned int n = 0; n < ioData->mNumberBuffers; n++) memset(ioData->mBuffers[n].mData, 0, ioData->mBuffers[n].mDataByteSize);
        // If the app is in the background, check if we don't output anything.
        if (IOS_SAVE_BATTERY && self->background) self->silenceFrames += inNumberFrames; else self->silenceFrames = 0;
    } else self->silenceFrames = 0;

    return noErr;
}

- (AudioUnit)createRemoteIOOnMainThread {
    if (stopped) return NULL;
    AudioUnit au;
    AudioComponentDescription desc;
    desc.componentType = kAudioUnitType_Output;
    desc.componentSubType = kAudioUnitSubType_RemoteIO;
    desc.componentFlags = desc.componentFlagsMask = 0;
    desc.componentManufacturer = kAudioUnitManufacturer_Apple;
    AudioComponent component = AudioComponentFindNext(NULL, &desc);
    if (AudioComponentInstanceNew(component, &au) != 0) return NULL;

    bool recordOnly = [[AVAudioSession sharedInstance].category isEqualToString:AVAudioSessionCategoryRecord];
    UInt32 value = recordOnly ? 0 : 1;
    if (AudioUnitSetProperty(au, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Output, 0, &value, sizeof(value))) { AudioComponentInstanceDispose(au); return NULL; };
    value = inputEnabled ? 1 : 0;
    if (AudioUnitSetProperty(au, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Input, 1, &value, sizeof(value))) { AudioComponentInstanceDispose(au); return NULL; };
    AudioUnitAddPropertyListener(au, kAudioUnitProperty_StreamFormat, streamFormatChangedCallback, (__bridge void *)self);
    UInt32 size = 0;
    AudioUnitGetPropertyInfo(au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0, &size, NULL);
    AudioStreamBasicDescription format;
    AudioUnitGetProperty(au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0, &format, &size);
    samplerate = (int)format.mSampleRate;

    format.mFormatID = kAudioFormatLinearPCM;
    format.mFormatFlags = (AudioFormatFlags)kAudioFormatFlagIsFloat | (AudioFormatFlags)kAudioFormatFlagIsPacked | (AudioFormatFlags)kAudioFormatFlagsNativeEndian;
    if (!interleaved) format.mFormatFlags |= kAudioFormatFlagIsNonInterleaved;
    format.mBitsPerChannel = 32;
    format.mFramesPerPacket = 1;
    format.mBytesPerFrame = format.mBytesPerPacket = interleaved ? (numberOfChannels * 4) : 4;
    format.mChannelsPerFrame = numberOfChannels;
    if (AudioUnitSetProperty(au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &format, sizeof(format))) { AudioComponentInstanceDispose(au); return NULL; };
    if (AudioUnitSetProperty(au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 1, &format, sizeof(format))) { AudioComponentInstanceDispose(au); return NULL; };

    AURenderCallbackStruct callbackStruct;
    callbackStruct.inputProc = coreAudioProcessingCallback;
    callbackStruct.inputProcRefCon = (__bridge void *)self;
    if (recordOnly) {
        if (AudioUnitSetProperty(au, kAudioOutputUnitProperty_SetInputCallback, kAudioUnitScope_Global, 0, &callbackStruct, sizeof(callbackStruct))) { AudioComponentInstanceDispose(au); return NULL; };
    } else {
        if (AudioUnitSetProperty(au, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &callbackStruct, sizeof(callbackStruct))) { AudioComponentInstanceDispose(au); return NULL; };
    };

    if (AudioUnitInitialize(au)) { AudioComponentInstanceDispose(au); return NULL; };
    return au;
}

- (void)mapChannelsOnMainThread {
    if (stopped) return;
    for (int n = 0; n < MAX_MAPPABLE_CHANNELS; n++) map.inputMap[n] = map.outputMap[n] = -1;
    mainCallback(mainClientdata, YubyAudioIOEvent::MapChannels, &map);
    applyChannelMapOnMainThread(audioUnit, map.outputMap, map.numOutputChannels, true);
    applyChannelMapOnMainThread(audioUnit, map.inputMap, numberOfChannels, false);
}

@end

@interface iosHandler: NSObject
@end
@implementation iosHandler {
    YubyAudioIOCallback callback;
    void *clientdata;
    NSTimer *stopTimer;
    iosIO *io;
    AVAudioApplicationRecordPermission lastRecordPermission;
    bool stopped, inputEnabled;
}

+ (void)initialize:(YubyAudioIOCallback)cb clientdata:(void *)cd set:(iosHandler * ARCStrong *)s {
    if ([NSThread isMainThread]) {
        *s = [[iosHandler alloc] initOnMainThreadWithCallback:cb clientdata:cd];
        [*s startOnMainThread];
    } else dispatch_sync(dispatch_get_main_queue(), ^{
        *s = [[iosHandler alloc] initOnMainThreadWithCallback:cb clientdata:cd];
        [*s startOnMainThread];
    });
}

- (id)initOnMainThreadWithCallback:(YubyAudioIOCallback)cb clientdata:(void *)cd {
    self = [super init];
    if (!self) return nil;
    callback = cb;
    clientdata = cd;
    stopped = inputEnabled = false;
    lastRecordPermission = [AVAudioApplication sharedInstance].recordPermission;
    return self;
}

- (void)stop {
    if ([NSThread isMainThread]) {
        if (!stopped) { [io stopAndDeallocOnMainThread]; io = nil; }
    } else [self performSelectorOnMainThread:@selector(stop) withObject:nil waitUntilDone:YES];
}

- (void)mapChannels {
    if ([NSThread isMainThread]) {
        if (!stopped) [io mapChannelsOnMainThread];
    } else [self performSelectorOnMainThread:@selector(mapChannels) withObject:nil waitUntilDone:NO];
}

- (void)stopAndDealloc {
    if (![NSThread isMainThread]) {
        [self performSelectorOnMainThread:@selector(stopAndDealloc) withObject:nil waitUntilDone:YES];
        return;
    }
    if (stopped) return; else stopped = true;
    [io stopAndDeallocOnMainThread]; io = nil;
    [NSObject cancelPreviousPerformRequestsWithTarget:self];
    [stopTimer invalidate]; stopTimer = nil;
    ARCRelease(self);
}

- (void)startOnMainThread {
    [self onRecordPermissionChangedOnMainThread];
    stopTimer = [NSTimer scheduledTimerWithTimeInterval:1.0 target:self selector:@selector(everySecondOnMainThread) userInfo:nil repeats:YES];
    static const AudioDeviceInfo iOSDeviceInfo = { "Audio devices are automatically handled by iOS.", 0, 0 };
    static const AudioDeviceList iOSDeviceList = { 1, (AudioDeviceInfo *)&iOSDeviceInfo };
    callback(clientdata, YubyAudioIOEvent::DevicesChanged, (void *)&iOSDeviceList);
}

- (void)onRecordPermissionChangedOnMainThread {
    if (!stopped) switch (lastRecordPermission) {
        case AVAudioApplicationRecordPermissionGranted: {
            bool t = true;
            callback(clientdata, YubyAudioIOEvent::InputPermissionChanged, &t);
            [io onRecordPermissionChangedOnMainThread:true];
        } break;
        case AVAudioApplicationRecordPermissionDenied: {
            bool f = false;
            callback(clientdata, YubyAudioIOEvent::InputPermissionChanged, &f);
            [io onRecordPermissionChangedOnMainThread:false];
        } break;
        case AVAudioApplicationRecordPermissionUndetermined: if (inputEnabled) [self requestRecordPermissionOnMainThread]; break;
    }
}

- (void)requestRecordPermissionOnMainThread {
    iosHandler *retainedSelf = ARCRetain(self);
    [AVAudioApplication requestRecordPermissionWithCompletionHandler:^(BOOL granted) {
        dispatch_async(dispatch_get_main_queue(), ^{
            if (!retainedSelf->stopped) {
                retainedSelf->lastRecordPermission = granted ? AVAudioApplicationRecordPermissionGranted : AVAudioApplicationRecordPermissionDenied;
                bool g = granted;
                retainedSelf->callback(retainedSelf->clientdata, YubyAudioIOEvent::InputPermissionChanged, (void *)&g);
                [retainedSelf->io onRecordPermissionChangedOnMainThread:g];
            }
            ARCRelease(retainedSelf);
        });
    }];
}

- (void)everySecondOnMainThread {
    if (stopped) return;
    AVAudioApplicationRecordPermission p = [AVAudioApplication sharedInstance].recordPermission;
    if (lastRecordPermission != p) {
        lastRecordPermission = p;
        [self onRecordPermissionChangedOnMainThread];
    }
    [io everySecondOnMainThread];
}

- (void)start:(audioProcessingCallback)apc audioProcessingClientdata:(void *)acd numberOfChannels:(unsigned int)nc interleaved:(bool)i inputEnabled:(bool)ie outputEnabled:(bool)oe {
    if ([NSThread isMainThread]) {
        [io stopAndDeallocOnMainThread]; io = nil;
        if (stopped) return; else inputEnabled = ie;
        if (ie && (lastRecordPermission == AVAudioApplicationRecordPermissionUndetermined)) [self requestRecordPermissionOnMainThread];
        io = [[iosIO alloc] initOnMainThreadWithIOCallback:apc audioProcessingClientdata:acd mainCallback:callback mainClientdata:clientdata numberOfChannels:nc interleaved:i inputEnabled:ie outputEnabled:oe hasRecordPermission:lastRecordPermission == AVAudioApplicationRecordPermissionGranted];
    } else dispatch_sync(dispatch_get_main_queue(), ^{
        [io stopAndDeallocOnMainThread]; io = nil;
        if (stopped) return; else inputEnabled = ie;
        if (ie && (lastRecordPermission == AVAudioApplicationRecordPermissionUndetermined)) [self requestRecordPermissionOnMainThread];
        io = [[iosIO alloc] initOnMainThreadWithIOCallback:apc audioProcessingClientdata:acd mainCallback:callback mainClientdata:clientdata numberOfChannels:nc interleaved:i inputEnabled:ie outputEnabled:oe hasRecordPermission:lastRecordPermission == AVAudioApplicationRecordPermissionGranted];
    });
}

- (void)setDevice:(const char *)deviceName {}

@end
#endif

unsigned int YubyAudioIO::preferredBufferSizeMs = 12;
static AppleHandler *handler = nil;
void YubyAudioIO::initialize(YubyAudioIOCallback callback, void *clientdata) { [AppleHandler initialize:callback clientdata:clientdata set:&handler]; }
void YubyAudioIO::shutdown() { [handler stopAndDealloc]; handler = nil; }
void YubyAudioIO::start(audioProcessingCallback apc, void *clientdata, unsigned int numberOfChannels, bool interleaved, bool inputEnabled, bool outputEnabled) { [handler start:apc audioProcessingClientdata:clientdata numberOfChannels:numberOfChannels interleaved:interleaved inputEnabled:inputEnabled outputEnabled:outputEnabled]; }
void YubyAudioIO::stop() { [handler stop]; }
void YubyAudioIO::mapChannels() { [handler mapChannels]; }
void YubyAudioIO::setDevice(const char *deviceName) { [handler setDevice:deviceName]; }

#endif
