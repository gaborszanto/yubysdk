#ifndef DemucsInternalHeader
#define DemucsInternalHeader
#include <filesystem>

#define fftSize 4096
#define fftSizeLog 12
#define hopSize 1024
#define numBins 2048
#define numFrames 336
#define segmentLength 343980
#define sampleRate 44100
#define overlap 0.0625
#define specPadLeft (hopSize / 2 * 3)
#define specPadRight (specPadLeft + numFrames * hopSize - segmentLength)
#define channelSize (numBins * numFrames)
#define stftSignalCount (segmentLength + specPadLeft + specPadRight)
#define stftTotalFrames ((stftSignalCount - fftSize) / hopSize + 1)
#define totalFrames (numFrames + 4)
#define processStride ((int)(segmentLength * (1.0 - overlap)))
#define processOverlap (segmentLength - processStride)
#define chunkDurationMs (1000.0 * segmentLength / sampleRate)

void *InitBackend(const std::filesystem::path &modelPath);
void DestroyBackend(void *backend);
float *GetSpectralDataPointer(void *backend);
float *GetWaveformDataPointer(void *backend);
bool Measure(void *backend);
bool Process(void *backend);
void GetLeft(void *backend, int stem, float *real, float *imag, float *output);
void GetRight(void *backend, int stem, float *real, float *imag, float *output);

#endif
