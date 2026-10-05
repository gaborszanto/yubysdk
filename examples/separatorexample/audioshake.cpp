#include "./separatorexample.h"
#include <audioshakesdk.h>
#include <future>
#include <chrono>
#include <thread>
#include <fstream>
#include <vector>
#include <algorithm>
#include <cmath>
#include <cstring>

#define AudioShakeFlags (AudioShakeSeparator::AudioShakeSeparatorFlags::inputInt16 | AudioShakeSeparator::AudioShakeSeparatorFlag::inputInterleavedStereo | AudioShakeSeparator::AudioShakeSeparatorFlag::outputInt16 | AudioShakeSeparator::AudioShakeSeparatorFlag::outputInterleavedStereo)

std::atomic<int> AudioShake::speed{-1};
static AudioShakeSeparator *firstSeparator = NULL;
static std::atomic<bool> firstSeparatorUsed = false;
static unsigned int firstChunkSize = 0;
    
static void measureThread(double chunkDurationMs) {
    double bestSpeed = 0;
    
    // Measure the stem separation speed up to 4 times or until timeout.
    for (int n = 0; (AudioShake::speed.load() == -1) && (n < 4); n++) {
        void **output;
        auto start = std::chrono::steady_clock::now();
        firstSeparator->process(NULL, firstSeparator->getFramesNeeded(), &output);
        double measuredSpeed = chunkDurationMs / std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        if (measuredSpeed > bestSpeed) bestSpeed = measuredSpeed;
        if (measuredSpeed < (n == 0 ? 1.0 : MinimumAcceptableSpeed)) break; // The first run may be slower than the rest, but still must achieve 1x.
        if ((n != 0) && (measuredSpeed > MinimumAcceptableSpeed * 2)) break; // Fast enough, no need to test more.
    }

    int measuredSpeed = (int)floor(bestSpeed), expected = -1;
    if (AudioShake::speed.compare_exchange_strong(expected, measuredSpeed) && (measuredSpeed >= MinimumAcceptableSpeed)) return;
    delete firstSeparator;
    firstSeparator = NULL;
}

void AudioShake::initialize(const std::filesystem::path &modelPath) {
    // Load the model to memory.
    if (modelPath.empty()) { speed.store(0); return; }
    std::ifstream file(modelPath, std::ios::binary | std::ios::ate);
    std::streamoff size = file.tellg();
    if (size <= 0) { speed.store(0); return; }
    std::vector<char> model(static_cast<std::size_t>(size));
    file.seekg(0);
    if (!file.read(model.data(), size)) { speed.store(0); return; }
    
    // Initialize a separator instance.
    AudioShakeSeparator *s = new AudioShakeSeparator(AudioShakeClientId, AudioShakeClientSecret, model.data(), static_cast<unsigned int>(model.size()), 0, AudioShakeFlags);
    if (s->getInitializationError()) { delete s; speed.store(0); return; } else firstSeparator = s;
    firstChunkSize = s->getFramesNeeded();
    double chunkDurationMs = ((double)firstChunkSize / (double)s->getOutputSamplerate()) * 1000.0;
    
    // Measure the processing performance on a background thread with a timeout.
    std::packaged_task<void(double)> task(measureThread);
    auto done = task.get_future();
    std::thread(std::move(task), chunkDurationMs).detach();
    if (done.wait_for(std::chrono::duration<double, std::milli>(std::max(1000.0, chunkDurationMs + chunkDurationMs / MinimumAcceptableSpeed * 4))) == std::future_status::timeout) {
        int expected = -1;
        speed.compare_exchange_strong(expected, 1);
    }
}
    
AudioShake *AudioShake::GetSeparatorForYubyPlayer() {
    if ((speed.load() < MinimumAcceptableSpeed) || !firstSeparator) return NULL;
    bool expected = false;
    return new AudioShake(firstSeparatorUsed.compare_exchange_strong(expected, true) ? firstSeparator : new AudioShakeSeparator(AudioShakeClientId, AudioShakeClientSecret, firstSeparator, 0xffffffff, 0, AudioShakeFlags));
}

AudioShake::AudioShake(AudioShakeSeparator *s) : yuby::StemSeparator(), separator(s) {
    outputSamplerate = s->getOutputSamplerate();
    chunkSize = firstChunkSize;
}
    
AudioShake::~AudioShake() {
    if (separator) delete separator;
}

int AudioShake::process(unsigned int resetWithInputSamplerate, short int *input, unsigned int numFrames, short int *output, unsigned int outputChannelOffset) {
    if (!separator) return -1;
    if (resetWithInputSamplerate > 0) separator->prepareForNewContent(resetWithInputSamplerate, AudioShakeFlags);
    void *ii[1] = { input }, **stems;
    int r = separator->process(input ? ii : NULL, numFrames, &stems);
    if (r <= 0) return r;
    for (int s = 0; s < 4; s++) memcpy(output + outputChannelOffset * s, stems[s], r * 4);
    return r;
}
