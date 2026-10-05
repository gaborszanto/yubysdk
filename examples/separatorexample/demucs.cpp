#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES
#endif
#include "./separatorexample.h"
#include "./demucsbackend.h"
#include <chrono>
#include <thread>
#include <mutex>
#include <future>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

static inline void fillSplitForward(float *rp, float *ip, const float *window, int dst, const float *src, int count) {
    if ((dst & 1) && (count > 0)) {
        ip[dst >> 1] = src[0] * window[dst];
        dst++; src++; count--;
    }
    int pair = dst >> 1, pairs = count >> 1;
    for (int i = 0; i < pairs; i++) {
        int s = i << 1, d = dst + s;
        rp[pair + i] = src[s] * window[d];
        ip[pair + i] = src[s + 1] * window[d + 1];
    }
    if (count & 1) rp[pair + pairs] = src[pairs << 1] * window[dst + (pairs << 1)];
}

static inline void fillSplitReverse(float *rp, float *ip, const float *window, int dst, const float *signal, int src, int count) {
    if ((dst & 1) && (count > 0)) {
        ip[dst >> 1] = signal[src] * window[dst];
        dst++; src--; count--;
    }
    int pair = dst >> 1, pairs = count >> 1;
    for (int i = 0; i < pairs; i++) {
        int s = src - (i << 1), d = dst + (i << 1);
        rp[pair + i] = signal[s] * window[d];
        ip[pair + i] = signal[s - 1] * window[d + 1];
    }
    if (count & 1) rp[pair + pairs] = signal[src - (pairs << 1)] * window[dst + (pairs << 1)];
}

static inline void inverseOverlapAdd(float *output, const float *rp, const float *ip, const float *rewWindow, int cropStart, int begin, int end, int start) {
    if (begin >= end) return;
    int local = begin - start, outidx = begin - cropStart;
    if (local & 1) {
        output[outidx] += ip[local >> 1] * rewWindow[local];
        local++; outidx++; begin++;
    }
    int pairs = (end - begin) >> 1, pair = local >> 1;
    for (int i = 0; i < pairs; i++) {
        int localEven = local + (i << 1), dst = outidx + (i << 1);
        output[dst] += rp[pair + i] * rewWindow[localEven];
        output[dst + 1] += ip[pair + i] * rewWindow[localEven + 1];
    }
    if ((end - begin) & 1) {
        int localEven = local + (pairs << 1), dst = outidx + (pairs << 1);
        output[dst] += rp[localEven >> 1] * rewWindow[localEven];
    }
}

struct Instance {
    float tail[4 * 2 * processOverlap], result[4 * 2 * segmentLength], inputLeft[segmentLength + 32], inputRight[segmentLength + 32];
    void *resampler;
    unsigned int bufferedInputFrames;
    bool noResample, hasTail;
    
    void init() {
        resampler = yuby::Create(yuby::ObjectType::Resampler);
        reset(sampleRate);
    }
    
    void dealloc() {
        yuby::Delete(resampler);
        free(this);
    }
    
    void reset(unsigned int sr) {
        noResample = sr == sampleRate;
        if (!noResample) {
            yuby::Set(resampler, yuby::Reset, 1.0);
            yuby::Set(resampler, yuby::PlaybackRate, (double)sr / (double)sampleRate);
        }
        bufferedInputFrames = 0;
        hasTail = false;
    }
};

struct DemucsSingleton {
    float rewWindow[fftSize], fwdWindow[fftSize], rp[fftSize / 2], ip[fftSize / 2], segmentWeight[segmentLength], prevOverlapGain[processOverlap], curOverlapGain[processOverlap], ireal[channelSize], iimag[channelSize];
    std::mutex mutex;
    void *backend;
    
    void initialize(const std::filesystem::path &modelPath) {
        backend = InitBackend(modelPath.c_str());
        if (!backend) { Demucs::speed.store(0); return; }
        
        for (int n = 0; n < fftSize; n++) rewWindow[n] = (float)(0.5 * (1.0 - cos(2.0 * M_PI * double(n) / double(fftSize))));
        static const float fwdScale = (float)(0.5 / sqrt((double)fftSize)); // normalize to match Python's torch.stft(normalized=True).
        static const float rewScale = 2.0f * (float)sqrt((double)fftSize) / (3.0f * (float)fftSize); // inverse spectrum scaling included
        for (int n = 0; n < fftSize; n++) {
            float v = rewWindow[n];
            fwdWindow[n] = v * fwdScale;
            rewWindow[n] = v * rewScale;
        }
        int half = segmentLength / 2;
        float invHalf = 1.0f / (float)half;
        for (int n = 0; n < half; n++) segmentWeight[n] = (float)(n + 1) * invHalf;
        for (int n = half; n < segmentLength; n++) segmentWeight[n] = (float)(segmentLength - n) * invHalf;
        for (int n = 0; n < processOverlap; n++) {
            float prev = segmentWeight[processStride + n], cur = segmentWeight[n], sum = prev + cur;
            prevOverlapGain[n] = (sum > 1e-8f) ? (prev / sum) * 32767.0f : 0.0f;
            curOverlapGain[n] = (sum > 1e-8f) ? (cur / sum) * 32767.0f : 0.0f;
        }
        
        // Measure the processing performance on a background thread with a timeout.
        std::packaged_task<void()> task([this] { measureThread(); });
        auto done = task.get_future();
        std::thread(std::move(task)).detach();
        if (done.wait_for(std::chrono::duration<double, std::milli>(std::max<double>(1000.0, chunkDurationMs + chunkDurationMs / MinimumAcceptableSpeed * 4))) == std::future_status::timeout) {
            int expected = -1;
            Demucs::speed.compare_exchange_strong(expected, 1);
        }
    }
    
    void measureThread() {
        double bestSpeed = 0;
        
        // Measure the stem separation speed up to 4 times or until timeout.
        for (int n = 0; (Demucs::speed.load() == -1) && (n < 4); n++) {
            auto start = std::chrono::steady_clock::now();
            if (!Measure(backend)) { Demucs::speed.store(0); return; }
            double measuredSpeed = chunkDurationMs / std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
            if (measuredSpeed > bestSpeed) bestSpeed = measuredSpeed;
            if (measuredSpeed < (n == 0 ? 1.0 : MinimumAcceptableSpeed)) break; // The first run may be slower than the rest, but still must achieve 1x.
            if ((n != 0) && (measuredSpeed > MinimumAcceptableSpeed * 2)) break; // Fast enough, no need to test more.
        }
        
        int measuredSpeed = (int)floor(bestSpeed), expected = -1;
        Demucs::speed.compare_exchange_strong(expected, measuredSpeed);
    }
    
    void dealloc() {
        if (backend) DestroyBackend(backend);
        delete this;
    }
    
    void inverseSpec(const float *real, const float *imag, float *output) {
        static const int cropStart = fftSize / 2 + specPadLeft, cropEnd = cropStart + segmentLength;
        
        for (int f = 0; f < totalFrames; f++) {
            int srcFrame = f - 2;
            if ((srcFrame < 0) || (srcFrame >= numFrames)) continue;
            
            rp[0] = real[srcFrame]; ip[0] = 0.0f;
            for (int k = 1; k < numBins; k++) {
                int idx = k * numFrames + srcFrame;
                rp[k] = real[idx];
                ip[k] = imag[idx];
            }
            
            yuby::fftReal(rp, ip, fftSizeLog, false);
            
            int start = f * hopSize, begin = start < cropStart ? cropStart : start, end = start + fftSize;
            if (end > cropEnd) end = cropEnd;
            inverseOverlapAdd(output, rp, ip, rewWindow, cropStart, begin, end, start);
        }
    }
    
    void forwardSTFT(const float *signal, int ch) {
        float *si = GetSpectralDataPointer(backend), *real = si + ch * 2 * channelSize, *imag = si + (ch * 2 + 1) * channelSize;
        
        for (int f = 0; f < stftTotalFrames; f++) {
            int start = f * hopSize, avail = stftSignalCount - start;
            if (avail < 1) {
                memset(rp, 0, (fftSize / 2) * 4);
                memset(ip, 0, (fftSize / 2) * 4);
            } else {
                if (avail > fftSize) avail = fftSize;
                else if (avail < fftSize) {
                    memset(rp, 0, (fftSize / 2) * 4);
                    memset(ip, 0, (fftSize / 2) * 4);
                }
                
                int dst = 0;
                if (start < specPadLeft) {
                    int count = specPadLeft - start;
                    if (count > avail) count = avail;
                    fillSplitReverse(rp, ip, fwdWindow, dst, signal, specPadLeft - start, count); dst += count;
                }
                
                int bodyStart = start + dst;
                if ((dst < avail) && (bodyStart < specPadLeft + segmentLength)) {
                    int count = specPadLeft + segmentLength - bodyStart;
                    if (count > avail - dst) count = avail - dst;
                    fillSplitForward(rp, ip, fwdWindow, dst, signal + bodyStart - specPadLeft, count); dst += count;
                }
                
                if (dst < avail) fillSplitReverse(rp, ip, fwdWindow, dst, signal, segmentLength - 2 - (start + dst - specPadLeft - segmentLength), avail - dst);
            }
            
            yuby::fftReal(rp, ip, fftSizeLog, true);
            real[0] = rp[0];
            imag[0] = 0.0f;
            
            for (int k = 1; k < numBins; k++) {
                int idx = k * stftTotalFrames;
                real[idx] = rp[k];
                imag[idx] = ip[k];
            }
            
            real++; imag++; // step for each frame
        }
    }
    
    bool processSegment(const float *left, const float *right, float *result) {
        forwardSTFT(left, 0);
        forwardSTFT(right, 1);
        float *waveformData = GetWaveformDataPointer(backend);
        memcpy(waveformData, left, segmentLength * 4);
        memcpy(waveformData + segmentLength, right, segmentLength * 4);
        if (!Process(backend)) return false;
        
        for (int stem = 0; stem < 4; stem++) {
            GetLeft(backend, stem, ireal, iimag, result + stem * 2 * segmentLength);
            inverseSpec(ireal, iimag, result + stem * 2 * segmentLength);
            GetRight(backend, stem, ireal, iimag, result + stem * 2 * segmentLength + segmentLength);
            inverseSpec(ireal, iimag, result + stem * 2 * segmentLength + segmentLength);
        }
        return true;
    }
    
    int process(Instance *ii, unsigned int resetWithInputSamplerate, short int *input, unsigned int numInputFrames, short int *output, unsigned int outputChannelOffset) {
        if (!backend) return -1;
        
        std::lock_guard<std::mutex> lock(mutex);
        if (resetWithInputSamplerate > 0) ii->reset(resetWithInputSamplerate);
        unsigned int produced = 0;
        short int *oo = (short int *)malloc(outputChannelOffset * 4 * 4);
        
        while (numInputFrames > 0) {
            if (ii->noResample) {
                unsigned int consumeFrames = segmentLength - ii->bufferedInputFrames;
                if (consumeFrames > numInputFrames) consumeFrames = numInputFrames;
                numInputFrames -= consumeFrames;
                static const float mul = 1.0f / 32767.0f;
                for (unsigned int n = 0; n < consumeFrames; n++) {
                    ii->inputLeft[ii->bufferedInputFrames + n] = *input++ * mul;
                    ii->inputRight[ii->bufferedInputFrames + n] = *input++ * mul;
                }
                ii->bufferedInputFrames += consumeFrames;
            } else {
                unsigned int consumeFrames = (unsigned int)((segmentLength - ii->bufferedInputFrames) * yuby::Get(ii->resampler, yuby::PlaybackRate)) + 1;
                if (consumeFrames > numInputFrames) consumeFrames = numInputFrames;
                numInputFrames -= consumeFrames;
                yuby::Process(ii->resampler, (const float *)input, NULL, ii->result, 0, consumeFrames);
                input += consumeFrames * 2;
                unsigned int framesCreated = (unsigned int)yuby::Get(ii->resampler, yuby::FramesCreated);
                yuby::deinterleave(ii->result, ii->inputLeft + ii->bufferedInputFrames, ii->inputRight + ii->bufferedInputFrames, framesCreated, 1.0f);
                ii->bufferedInputFrames += framesCreated;
            }
            
            if (ii->bufferedInputFrames < segmentLength) continue;
            if (!processSegment(ii->inputLeft, ii->inputRight, ii->result)) return -1;
            static const int si[4] = { 1, 2, 3, 0 };
            
            for (int stem = 0; stem < 4; stem++) {
                short int *dst = oo + outputChannelOffset * si[stem] + produced * 2;
                float *srcLeft = ii->result + stem * 2 * segmentLength, *srcRight = srcLeft + segmentLength;
                float *tailLeft = ii->tail + stem * 2 * processOverlap, *tailRight = tailLeft + processOverlap;
                
                if (ii->hasTail) for (int n = 0; n < processOverlap; n++) {
                    float L = tailLeft[n] * prevOverlapGain[n] + srcLeft[n] * curOverlapGain[n];
                    float R = tailRight[n] * prevOverlapGain[n] + srcRight[n] * curOverlapGain[n];
                    if (L < -32767.0f) L = -32767.0f; else if (L > 32767.0f) L = 32767.0f;
                    if (R < -32767.0f) R = -32767.0f; else if (R > 32767.0f) R = 32767.0f;
                    dst[n * 2] = (short int)L;
                    dst[n * 2 + 1] = (short int)R;
                }
                
                for (int n = ii->hasTail ? processOverlap : 0; n < processStride; n++) {
                    float L = srcLeft[n] * 32767.0f, R = srcRight[n] * 32767.0f;
                    if (L < -32767.0f) L = -32767.0f; else if (L > 32767.0f) L = 32767.0f;
                    if (R < -32767.0f) R = -32767.0f; else if (R > 32767.0f) R = 32767.0f;
                    dst[n * 2] = (short int)L;
                    dst[n * 2 + 1] = (short int)R;
                }
                
                memcpy(tailLeft, srcLeft + processStride, processOverlap * 4);
                memcpy(tailRight, srcRight + processStride, processOverlap * 4);
            }
            
            ii->hasTail = true;
            produced += processStride;
            ii->bufferedInputFrames -= processStride;
            memmove(ii->inputLeft, ii->inputLeft + processStride, ii->bufferedInputFrames * 4);
            memmove(ii->inputRight, ii->inputRight + processStride, ii->bufferedInputFrames * 4);
        }
        
        for (int stem = 0; stem < 4; stem++) memcpy(output + outputChannelOffset * stem, oo + outputChannelOffset * stem, produced * 4);
        free(oo);
        return (int)produced;
    }
};

static DemucsSingleton *singleton = NULL;
std::atomic<int> Demucs::speed{-1};

void Demucs::initialize(const std::filesystem::path &modelPath) {
    singleton = new (std::nothrow) DemucsSingleton;
    if (singleton) singleton->initialize(modelPath); else speed.store(0);
}

Demucs *Demucs::GetSeparatorForYubyPlayer() {
    if (Demucs::speed.load() < MinimumAcceptableSpeed) return NULL;
    Demucs *o = new Demucs();
    o->instance = (Instance *)malloc(sizeof(Instance));
    if (o->instance) ((Instance *)o->instance)->init();
    o->outputSamplerate = sampleRate;
    o->chunkSize = segmentLength;
    return o;
}

Demucs::~Demucs() {
    if (instance) ((Instance *)instance)->dealloc();
}

int Demucs::process(unsigned int resetWithInputSamplerate, short int *input, unsigned int numInputFrames, short int *output, unsigned int outputChannelOffset) {
    if (!instance) return -1; else return singleton->process((Instance *)instance, resetWithInputSamplerate, input, numInputFrames, output, outputChannelOffset);
}
