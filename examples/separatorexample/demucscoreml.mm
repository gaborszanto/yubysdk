#import <CoreML/CoreML.h>
#include "./demucsbackend.h"

static inline void fp16tofp32(void *input, void *output, int n) {
    unsigned short int *i = (unsigned short int *)input; int *o = (int *)output;
    while (n--) {
        unsigned short int fp16 = *i++;
        if ((fp16 == 0) || (fp16 == 0x8000)) { *o++ = 0; continue; } // positive or negative zero
        int s = (fp16 << 16) & 0x80000000, e = ((fp16 >> 10) & 0x0000001F) + 112, m = fp16 & 0x000003FF;
        *o++ = s | (e << 23) | (m << 13);
    }
}

struct DemucsCoreML {
public:
    MLModel *model;
    MLMultiArray *spectral, *waveform, *freqOut, *timeOut;
    MLDictionaryFeatureProvider *inputFeatures;
    
    DemucsCoreML(const std::filesystem::path &modelPath) : model(nil), freqOut(nil), timeOut(nil) {
        @autoreleasepool {
            MLModelConfiguration *configuration = [[MLModelConfiguration alloc] init];
            configuration.modelDisplayName = @"HTDemucs";
            configuration.computeUnits = MLComputeUnitsCPUAndGPU;
            configuration.allowLowPrecisionAccumulationOnGPU = NO;
            dispatch_semaphore_t semaphore = dispatch_semaphore_create(0);
            NSURL *modelURL = [NSURL fileURLWithFileSystemRepresentation:modelPath.c_str() isDirectory:YES relativeToURL:nil];
            [MLModel compileModelAtURL:modelURL completionHandler:^(NSURL * _Nullable compiledModelURL, NSError * _Nullable compilationError) {
                if (!compiledModelURL) {
                    NSLog(@"Failed to compile Core ML model at %@: %@", modelURL, compilationError);
                    dispatch_semaphore_signal(semaphore);
                } else [MLModel loadContentsOfURL:compiledModelURL configuration:configuration completionHandler:^(MLModel * _Nullable model_, NSError * _Nullable error) {
                    if (error) NSLog(@"%@", error); else if (model_) model = [model_ retain];
                    dispatch_semaphore_signal(semaphore);
                }];
            }];
            dispatch_semaphore_wait(semaphore, DISPATCH_TIME_FOREVER);
            dispatch_release(semaphore);
            [configuration release];
            
            spectral = [[MLMultiArray alloc] initWithShape:@[@1, @4, @numBins, @numFrames] dataType:MLMultiArrayDataTypeFloat32 error:nil];
            waveform = [[MLMultiArray alloc] initWithShape:@[@1, @2, @segmentLength] dataType:MLMultiArrayDataTypeFloat32 error:nil];
            inputFeatures = [[MLDictionaryFeatureProvider alloc] initWithDictionary:@{
                @"spectral_magnitude": [MLFeatureValue featureValueWithMultiArray:spectral],
                @"audio_waveform": [MLFeatureValue featureValueWithMultiArray:waveform]
            } error:nil];
            memset(spectral.dataPointer, 0, spectral.count * 4);
            memset(waveform.dataPointer, 0, waveform.count * 4);
        }
    }
    
    ~DemucsCoreML() {
        [freqOut release];
        [timeOut release];
        [model release];
        [inputFeatures release];
        [spectral release];
        [waveform release];
    }
    
    float *getSpectralDataPointer() { return (float *)spectral.dataPointer; }
    float *getWaveformDataPointer() { return (float *)waveform.dataPointer; }
    bool measure() { @autoreleasepool { return [model predictionFromFeatures:inputFeatures error:nil] != nil; } }
    
    bool process() {
        @autoreleasepool {
            [freqOut release];
            [timeOut release];
            id<MLFeatureProvider> modelOutput = [model predictionFromFeatures:inputFeatures error:nil];
            freqOut = [[modelOutput featureValueForName:@"freq_output"].multiArrayValue retain];
            timeOut = [[modelOutput featureValueForName:@"time_output"].multiArrayValue retain];
            return freqOut && timeOut;
        }
    }
    
    void getLeft(int stem, float *real, float *imag, float *output) {
        @autoreleasepool {
            extractChannel(freqOut, 4 * stem + 0, real);
            extractChannel(freqOut, 4 * stem + 1, imag);
            extractChannel1D(timeOut, 2 * stem + 0, output);
        }
    }
    
    void getRight(int stem, float *real, float *imag, float *output) {
        @autoreleasepool {
            extractChannel(freqOut, 4 * stem + 2, real);
            extractChannel(freqOut, 4 * stem + 3, imag);
            extractChannel1D(timeOut, 2 * stem + 1, output);
        }
    }
    
    static void extractChannel(MLMultiArray *array, int channel, float *result) {
        int hStride = array.strides[2].intValue, wStride = array.strides[3].intValue, baseOffset = channel * array.strides[1].intValue;
        if (array.dataType == MLMultiArrayDataTypeFloat32) {
            const float *ptr = (const float *)array.dataPointer;
            if (wStride == 1) for (int h = 0; h < numBins; h++) memcpy(result + h * numFrames, ptr + baseOffset + h * hStride, numFrames * 4);
            else for (int h = 0; h < numBins; h++) for (int w = 0; w < numFrames; w++) result[h * numFrames + w] = ptr[baseOffset + h * hStride + w * wStride];
        } else if (array.dataType == MLMultiArrayDataTypeFloat16) {
            const uint16_t *ptr = (const uint16_t *)array.dataPointer;
            if (wStride == 1) {
                for (int h = 0; h < numBins; h++) fp16tofp32((void *)(ptr + baseOffset + h * hStride), result + h * numFrames, numFrames);
            } else {
                for (int h = 0; h < numBins; h++) {
                    for (int w = 0; w < numFrames; w++) fp16tofp32((void *)(ptr + baseOffset + h * hStride + w * wStride), result + h * numFrames + w, 1);
                }
            }
        }
    }

    static void extractChannel1D(MLMultiArray *array, int channel, float *result) {
        int baseOffset = channel * array.strides[1].intValue, wStride = array.strides[2].intValue;
        if (array.dataType == MLMultiArrayDataTypeFloat32) {
            const float *ptr = (const float *)array.dataPointer;
            if (wStride == 1) memcpy(result, ptr + baseOffset, segmentLength * 4);
            else for (int w = 0; w < segmentLength; w++) result[w] = ptr[baseOffset + w * wStride];
        } else if (array.dataType == MLMultiArrayDataTypeFloat16) {
            const uint16_t *ptr = (const uint16_t *)array.dataPointer;
            if (wStride == 1) fp16tofp32((void *)(ptr + baseOffset), result, segmentLength);
            else for (int w = 0; w < segmentLength; w++) fp16tofp32((void *)(ptr + baseOffset + w * wStride), result + w, 1);
        }
    }
};

void *InitBackend(const std::filesystem::path &modelPath) {
    DemucsCoreML *d = new DemucsCoreML(modelPath);
    if (d->model) return d;
    delete d;
    return NULL;
}
void DestroyBackend(void *backend) { delete (DemucsCoreML *)backend; }
float *GetSpectralDataPointer(void *backend) { return ((DemucsCoreML *)backend)->getSpectralDataPointer(); }
float *GetWaveformDataPointer(void *backend) { return ((DemucsCoreML *)backend)->getWaveformDataPointer(); }
bool Measure(void *backend) { return backend && ((DemucsCoreML *)backend)->measure(); }
bool Process(void *backend) { return ((DemucsCoreML *)backend)->process(); }
void GetLeft(void *backend, int stem, float *real, float *imag, float *output) { ((DemucsCoreML *)backend)->getLeft(stem, real, imag, output); }
void GetRight(void *backend, int stem, float *real, float *imag, float *output) { ((DemucsCoreML *)backend)->getRight(stem, real, imag, output); }
