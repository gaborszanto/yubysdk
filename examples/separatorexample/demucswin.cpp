// NuGet repo: Microsoft.ML.OnnxRuntime.DirectML
#ifdef _WIN32
#include <Windows.h>
#include <onnxruntime_c_api.h> 
#include <onnxruntime_float16.h>
#include <dml_provider_factory.h>
#include "./demucsbackend.h"

struct Float16 : onnxruntime_float16::Float16Impl<Float16> {
    static uint16_t fromFloat(float value) noexcept { return ToUint16Impl(value); }
    static float toFloat(uint16_t bits) noexcept {
        Float16 value;
        value.val = bits;
        return value.ToFloatImpl();
    }
};

constexpr const char *inputNames[] = { "spectral_magnitude", "audio_waveform" };
constexpr const char *outputNames[] = { "freq_output", "time_output" };
constexpr size_t inputSizes[] = { 4 * channelSize, 2 * segmentLength };

struct DemucsONNX {
    const OrtApi *api = OrtGetApiBase()->GetApi(ORT_API_VERSION);
    OrtEnv *environment = nullptr;
    OrtSession* session = nullptr;
    float *inputData[2]{};
    OrtValue *inputs[2]{}, *outputs[2]{};
    void *inputBuffers[2]{}, *outputBuffers[2]{};
    bool inputHalf[2]{}, outputHalf[2]{};

    ~DemucsONNX() {
        if (!api) return;
        for (int n = 0; n < 2; n++) if (inputData[n]) free(inputData[n]);
        for (auto *value : inputs) if (value) api->ReleaseValue(value);
        for (auto *value : outputs) if (value) api->ReleaseValue(value);
        if (session) api->ReleaseSession(session);
        if (environment) api->ReleaseEnv(environment);
    }

    bool check(OrtStatus *status) const {
        if (!status) return true;
        OutputDebugStringA(api->GetErrorMessage(status));
        OutputDebugStringA("\n");
        api->ReleaseStatus(status);
        return false;
    }

    bool initialize(const std::filesystem::path &modelPath) {
        if (!api || !check(api->CreateEnv(ORT_LOGGING_LEVEL_WARNING, "Demucs", &environment))) return false;

        OrtSessionOptions *rawOptions = nullptr;
        if (!check(api->CreateSessionOptions(&rawOptions))) return false;

        std::unique_ptr<OrtSessionOptions, decltype(api->ReleaseSessionOptions)> options(rawOptions, api->ReleaseSessionOptions);
        if (!check(api->SetSessionGraphOptimizationLevel(options.get(), ORT_ENABLE_ALL))) return false;
        if (!check(api->DisableMemPattern(options.get()))) return false;
        if (!check(api->SetSessionExecutionMode(options.get(), ORT_SEQUENTIAL))) return false;
        if (!check(api->AddSessionConfigEntry(options.get(), "ep.dml.disable_graph_fusion", "1"))) return false;

        const OrtDmlApi *directML = nullptr;
        if (!check(api->GetExecutionProviderApi("DML", ORT_API_VERSION, reinterpret_cast<const void **>(&directML)))) return false;
        OrtDmlDeviceOptions deviceOptions = { HighPerformance, Gpu };
        if (!check(directML->SessionOptionsAppendExecutionProvider_DML2(options.get(), &deviceOptions)) && !check(directML->SessionOptionsAppendExecutionProvider_DML(options.get(), 0))) return false;
        if (!check(api->CreateSession(environment, modelPath.c_str(), options.get(), &session))) return false;

        OrtAllocator *allocator = nullptr;
        if (!check(api->GetAllocatorWithDefaultOptions(&allocator))) return false;
        OrtMemoryInfo *rawMemory = nullptr;
        if (!check(api->CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &rawMemory))) return false;

        std::unique_ptr<OrtMemoryInfo, decltype(api->ReleaseMemoryInfo)> memory(rawMemory, api->ReleaseMemoryInfo);
        constexpr int64_t inputShapes[2][4] = {{1, 4, numBins, numFrames}, {1, 2, segmentLength}};
        constexpr int64_t outputShapes[2][4] = {{1, 16, numBins, numFrames}, {1, 8, segmentLength}};

        for (size_t i = 0; i < 2; i++) {
            ONNXTensorElementDataType inputType, outputType;
            if (!tensorType(allocator, true, inputNames[i], &inputType, inputHalf + i) || !tensorType(allocator, false, outputNames[i], &outputType, outputHalf + i)) return false;
            inputData[i] = (float *)calloc(inputSizes[i], 4);
            if (!inputData[i]) return false;
            const size_t rank = i == 0 ? 4 : 3;
            if (inputHalf[i]) {
                if (!check(api->CreateTensorAsOrtValue(allocator, inputShapes[i], rank, inputType, &inputs[i]))) return false;
            } else if (!check(api->CreateTensorWithDataAsOrtValue(memory.get(), inputData[i], inputSizes[i] * sizeof(float), inputShapes[i], rank, inputType, &inputs[i]))) return false;
            if (!check(api->CreateTensorAsOrtValue(allocator, outputShapes[i], rank, outputType, &outputs[i]))) return false;
            if (!check(api->GetTensorMutableData(inputs[i], &inputBuffers[i]))) return false;
            if (!check(api->GetTensorMutableData(outputs[i], &outputBuffers[i]))) return false;
        }
        return true;
    }

    bool tensorType(OrtAllocator *allocator, bool input, const char *name, ONNXTensorElementDataType *type, bool *half) {
        for (size_t i = 0; i < 2; i++) {
            char *tensorName = nullptr;
            if (!check(input ? api->SessionGetInputName(session, i, allocator, &tensorName) : api->SessionGetOutputName(session, i, allocator, &tensorName))) return false;
            const bool matches = std::strcmp(tensorName, name) == 0;
            allocator->Free(allocator, tensorName);
            if (!matches) continue;

            OrtTypeInfo *rawTypeInfo = nullptr;
            if (!check(input ? api->SessionGetInputTypeInfo(session, i, &rawTypeInfo) : api->SessionGetOutputTypeInfo(session, i, &rawTypeInfo))) return false;
            std::unique_ptr<OrtTypeInfo, decltype(api->ReleaseTypeInfo)> typeInfo(rawTypeInfo, api->ReleaseTypeInfo);

            const OrtTensorTypeAndShapeInfo *tensorInfo = nullptr;
            if (!check(api->CastTypeInfoToTensorInfo(typeInfo.get(), &tensorInfo)) || !tensorInfo) return false;
            if (!check(api->GetTensorElementType(tensorInfo, type))) return false;
            *half = (*type == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16);
            return *half || (*type == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT);
        }
        return false;
    }

    bool run() {
        for (size_t i = 0; i < 2; i++) if (inputHalf[i]) std::transform(inputData[i], inputData[i] + inputSizes[i], (uint16_t*)inputBuffers[i], Float16::fromFloat);
        const OrtValue *runInputs[] = {inputs[0], inputs[1]};
        return check(api->Run(session, nullptr, inputNames, runInputs, 2, outputNames, 2, outputs));
    }

    void copyOutput(size_t tensor, size_t offset, float *destination, size_t count) const {
        if (outputHalf[tensor]) {
            const uint16_t *source = ((const uint16_t *)outputBuffers[tensor]) + offset;
            std::transform(source, source + count, destination, Float16::toFloat);
        } else memcpy(destination, ((const float *)outputBuffers[tensor]) + offset, count * 4);
    }

    void getChannel(int stem, int channel, float *real, float *imag, float *output) const {
        const size_t spectralChannel = 4 * stem + 2 * channel;
        copyOutput(0, spectralChannel * channelSize, real, channelSize);
        copyOutput(0, (spectralChannel + 1) * channelSize, imag, channelSize);
        copyOutput(1, (2 * stem + channel) * segmentLength, output, segmentLength);
    }
};

void *InitBackend(const std::filesystem::path &modelPath) {
    DemucsONNX *backend = new (std::nothrow) DemucsONNX;
    if (backend->initialize(modelPath)) return backend; else delete backend;
    return nullptr;
}

void DestroyBackend(void *backend) { delete (DemucsONNX *)backend; }
float *GetSpectralDataPointer(void *backend) { return ((DemucsONNX *)backend)->inputData[0]; }
float *GetWaveformDataPointer(void *backend) { return ((DemucsONNX *)backend)->inputData[1]; }
bool Measure(void *backend) { return Process(backend); }
bool Process(void *backend) { return backend && ((DemucsONNX *)backend)->run(); }
void GetLeft(void *backend, int stem, float *real, float *imag, float *output) { ((DemucsONNX *)backend)->getChannel(stem, 0, real, imag, output); }
void GetRight(void *backend, int stem, float *real, float *imag, float *output) { ((DemucsONNX *)backend)->getChannel(stem, 1, real, imag, output); }

#endif
