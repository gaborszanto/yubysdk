#ifndef yubyheader
#define yubyheader
#ifndef YUBYFUNCTION
  #define YUBYFUNCTION
#endif

namespace yuby {

YUBYFUNCTION void YubyInit(const char *licenseKey);

enum class ObjectType: int { Gate = 0, Roll = 1, BitCrusher = 2, Filter = 3, Echo = 4, EQ = 5, Whoosh = 6, Flanger = 7, Clipper = 8, Limiter = 9, Compressor = 10, Reverb = 11, Delay = 12, Player = 13, TimeStretcher = 14, FrequencyDomain = 15, Resampler = 16, Decoder = 17, AutoTune = 18, AEC = 19, BandpassFilterbank = 20, Analyzer = 21, Recorder = 22, InvalidObject = -1 };
YUBYFUNCTION void *Create(ObjectType t);
YUBYFUNCTION void Delete(void *yubyObject);

struct AudioFormat { static const int MP3 = 0, AAC = 1, AIFF = 2, WAV = 3, FLAC = 4, ALAC = 5, Unknown = -1; };
struct OpenState { static const int None = 0, Opening = 1, OpenFailed = 2, Opened = 10, Closed = 11; };
struct OpenFlag { static const unsigned int ParseMetadataOnly = 1, SkipMetadata = 2, SkipThumbnailImage = 4, MeasureSilence = 8, Separate = 16, Offline = 32; };
struct SetPositionFlag { static const unsigned int Stop = 1, SynchronizedStart = 2, ForceDefaultQuantum = 4, PreferWaitingForSynchronizedStart = 8; };
struct LoopFlag { static const unsigned int JumpToStart = 1, SynchronizedStart = 2, ForceDefaultQuantum = 4, PreferWaitingForSynchronizedStart = 8; };
struct BandpassFilterbankResetFlag { static const unsigned int Bands = 1, SumAndAverage = 2, Peak = 4; };
struct AutoTuneScale { static const unsigned int CHROMATIC = 0, CMAJOR = 2741, CSHARPMAJOR = 1387, DMAJOR = 2774, DSHARPMAJOR = 1453, EMAJOR = 2906, FMAJOR = 1717, FSHARPMAJOR = 3434, GMAJOR = 2773, GSHARPMAJOR = 1451, AMAJOR = 2902, ASHARPMAJOR = 1709, BMAJOR = 3418, AMINOR = CMAJOR, ASHARPMINOR = CSHARPMAJOR, BMINOR = DMAJOR, CMINOR = DSHARPMAJOR, CSHARPMINOR = EMAJOR, DMINOR = FMAJOR, DSHARPMINOR = FSHARPMAJOR, EMINOR = GMAJOR, FMINOR = GSHARPMAJOR, FSHARPMINOR = AMAJOR, GMINOR = ASHARPMAJOR, GSHARPMINOR = BMAJOR; };
struct FilterType { static const int Resonant_Lowpass = 0, Resonant_Highpass = 1, Bandpass = 2, Notch = 3, LowShelf = 4, HighShelf = 5, Parametric = 6, Custom = 7; };

enum Parameter: unsigned int { InvalidParameter = 0,
    Type = 1, Scale = 2, Range = 3, Speed = 4, Clamp = 5, Distance = 6, OpenState = 7, SyncMode = 8, Format = 9,
    OnOff = 10, Reset = 11, Fade = 12, SoftKnee = 13, AutoGain = 14, ImmediateStart = 15, CompensateStartLatency = 16, TimeStretching = 17, FixDoubleOrHalfBPM = 18, LoopOnEnd = 19, ReverseToForwardAtLoopStart = 20, HighQuality = 21, Play = 22, ExitLoop = 23, IsLooping = 24, Reverse = 25, IsScratching = 26, AtTheEnd = 27, IsBuffering = 28, IsSlip = 29, IsStems = 30, TogglePlayPause = 31, PlaySynchronized = 32, EndScratch = 33, PitchBendTimeStretching = 34, IsPositionInLoop = 35,
    WetPercent = 36, DryPercent = 37, WidthPercent = 38, DampPercent = 39, MixPercent = 40, DecayPercent = 41, DepthPercent = 42, Ratio = 43, Low = 44, Mid = 45, High = 46, FormantCorrectionPercent = 47, DoubleTalkSensitivityPercent = 48, DisplayPositionPercent = 49, BufferedPercent = 50, ResonancePercent = 51, SlopePercent = 52, MaxPitchBendPercent = 53, GainPercent = 54,
    ScratchPitch = 55, PlaybackRate = 56, PlaybackRateIncrease = 57, TimeStretchingSound = 58, JogParameter = 59, TrackIndex = 60, KeyIndex = 61,
    BeatsPerMinute = 62, OriginalBPM = 63, NumBeats = 64, BeatIndex = 65, Phase = 66, Quantum = 67, DefaultQuantum = 68, PitchBend = 69, TicksPerTurn = 70, NumBits = 71, NumBins = 72, MinBPM = 73, MaxBPM = 74,
    FrequencyHz = 75, LowCutHz = 76, SamplerateHz = 77,
    ThresholdDecibel = 78, CeilingDecibel = 79, InputGainDecibel = 80, OutputGainDecibel = 81, KneeWidthDecibel = 82, GainReductionDecibel = 83, PeakDecibel = 84, AverageDecibel = 85, LoudPartsAverageDecibel = 86,
    AttackSeconds = 87, HoldSeconds = 88, ReleaseSeconds = 89, MaxDelayMs = 90, DelayMs = 91, LookAheadMs = 92, FirstBeatMs = 93, PositionMs = 94, DisplayPositionMs = 95, DisplayPositionSeconds = 96, BendOffsetMs = 97, PositionAfterSlipModeMs = 98, DurationMs = 99, DurationSeconds = 100, MsElapsedSinceLastBeat = 101, StartMs = 102, DecelerateSeconds = 103, SlipMs = 104, MsRemainingToSyncEvent = 105, PlaySynchronizedToMs = 106, PitchBendHoldMs = 107, CachePositionMs = 108,
    Octave = 109, FilterB0 = 110, FilterB1 = 111, FilterB2 = 112, FilterA1 = 113, FilterA2 = 114, PitchShiftCents = 115,
    DurationFrames = 116, InputFramesNeeded = 117, PositionFrames = 118, PositionFramesPrecise = 119, FramesCreated = 120, AdvanceFrames = 121, LatencyFrames = 122, FramesPerPacket = 123, ID3FrameDataSizeBytes = 124, ImageSizeBytes = 125, StartOffsetBytes = 126,
    TurntableBreak = 127, StartScratch = 128, JogTouchEnd = 129, JogTick = 130, JogTouchBegin = 131, MsDifference = 132, Loop = 133, MinTimeStretchRate = 134, MaxTimeStretchRate = 135, NegativeSeconds = 136, BufferSizeSeconds = 137, Initialize = 138
};
YUBYFUNCTION double Get(void *yubyObject, Parameter p, double arg0 = 0.0, double arg1 = 0.0);
YUBYFUNCTION bool Set(void *yubyObject, Parameter p, double value, double arg0 = 0.0, double arg1 = 0.0, double arg2 = 0.0, double arg3 = 0.0);

enum StringParameter: unsigned int { InvalidStringParameter = 0,
    ErrorMessage = 1, ID3FrameName = 2, Artist = 3, Title = 4, Album = 5, ID3FrameDataAsString = 6, StemsJSON = 7
};
YUBYFUNCTION const char *GetString(void *yubyObject, StringParameter p);
YUBYFUNCTION char *GetStringTakeOwnership(void *yubyObject, StringParameter p);

enum DataParameter: unsigned int { InvalidDataParameter = 0,
    Image = 1, ID3FrameData = 2, OverviewWaveform = 3, AverageWaveform = 4, PeakWaveform = 5, LowWaveform = 6, MidWaveform = 7, HighWaveform = 8
};
YUBYFUNCTION void *GetData(void *yubyObject, DataParameter p);
YUBYFUNCTION void *GetDataTakeOwnership(void *yubyObject, DataParameter p);

YUBYFUNCTION void Open(void *yubyObject, const char *url, unsigned int flags = 0, unsigned char *cancel = 0);
YUBYFUNCTION bool Process(void *yubyObject, const float *input, const float *sidechain, float *output, unsigned int samplerateHz, unsigned int numFrames, unsigned char numStereoChannels = 1);

YUBYFUNCTION void SetColorFx(void *yubyObject, void *yubyFxObject);
YUBYFUNCTION void AddToTracklist(void *recorder, const char *artist, const char *title, int becameAudibleSeconds, bool takeOwnership);

YUBYFUNCTION void add(const float *inputA, const float *inputB, float *output, unsigned int numValues);
YUBYFUNCTION void addfour(const float *inputA, const float *inputB, const float *inputC, const float *inputD, float *output, unsigned int numValues);
YUBYFUNCTION float peak(const float *input, unsigned int numValues); // return = max(abs(input))
YUBYFUNCTION void balance(const float *input, float *output, float leftGainStart, float leftGainEnd, float rightGainStart, float rightGainEnd, unsigned int numFrames, bool getPeaks);
YUBYFUNCTION float mul(const float *input, float *output, float startMultiplier, float endMultiplier, unsigned int numFrames);
YUBYFUNCTION float mulinc(const float *input, float *output, float multiplier, float increase, unsigned int numFrames);
YUBYFUNCTION void multiply(const float *input, float *output, unsigned int numValues, float multiplier);
YUBYFUNCTION float muladd(const float *inputA, const float *inputB, float *output, float startMultiplier, float endMultiplier, unsigned int numFrames);
YUBYFUNCTION float muladdinc(const float *inputA, const float *inputB, float *output, float multiplier, float increase, unsigned int numFrames);
YUBYFUNCTION void cross(const float *inputA, const float *inputB, float *output, float startMultiplierA, float endMultiplierA, float startMultiplierB, float endMultiplierB, unsigned int numFrames);
YUBYFUNCTION void interleave(const float *left, const float *right, float *output, unsigned int numFrames);
YUBYFUNCTION void interleaveadd(const float *left, const float *right, const float *input, float *output, unsigned int numFrames);
YUBYFUNCTION void deinterleave(const float *input, float *left, float *right, unsigned int numFrames, float multiplier);
YUBYFUNCTION void stereotomono(const float *input, float *output, float leftGainStart, float leftGainEnd, float rightGainStart, float rightGainEnd, unsigned int numFrames);
YUBYFUNCTION void stereotomidside(const float *input, float *output, unsigned int numFrames);
YUBYFUNCTION void midsidetostereo(const float *input, float *output, unsigned int numFrames);
YUBYFUNCTION void shortinttofloat(const short int *input, float *output, int numFrames);
YUBYFUNCTION void floattoshortint(const float *input, short int *output, unsigned int numFrames);
YUBYFUNCTION void fftComplex(float *real, float *imag, int logSize, bool forward);
YUBYFUNCTION void fftReal(float *real, float *imag, int logSize, bool forward);
YUBYFUNCTION void fftPolar(float *mag, float *phase, int logSize, bool forward, float valueOfPi = 0.0f);
YUBYFUNCTION void *createWAV(const char *path, unsigned int samplerate, unsigned char numChannels);
YUBYFUNCTION bool writeWAV(void *wav, short int *audio, unsigned int numberOfBytes);
YUBYFUNCTION void closeWAV(void *wav);

struct AudioInMemoryHeader { long long int retainCount, totalSizeBytes, status, firstChunkAddress; };
struct AudioInMemoryChunk { long long int nextChunkAddress, payloadSizeBytes, payload; };

struct StemSeparator {
public:
    unsigned int outputSamplerate, chunkSize;
    #ifndef WASM
    virtual int process(unsigned int resetWithInputSamplerate, short int *input, unsigned int numFrames, short int *output, unsigned int outputChannelOffset) { return -1; }
    virtual ~StemSeparator() {}
    #endif
};
YUBYFUNCTION void *CreatePlayerWithStemSeparator(StemSeparator *separator0, StemSeparator *separator1);

}

// Helpers:

#define YubyDeleteObjects(...) do { \
    void *_objects_[] = { __VA_ARGS__ }; \
    const int _to_ = (int)(sizeof(_objects_) / sizeof(_objects_[0])); \
    for (int _n_ = 0; _n_ < _to_; _n_++) yuby::Delete(_objects_[_n_]); \
} while (0)

struct YubyParameterListItem { yuby::Parameter p; double v; };
#define YubySetParameters(_object_, ...) do { \
    void *_o_ = (_object_); \
    const YubyParameterListItem _p_[] = { __VA_ARGS__ }; \
    const int _to_ = (int)(sizeof(_p_) / sizeof(_p_[0])); \
    for (int _n_ = 0; _n_ < _to_; _n_++) yuby::Set(_o_, _p_[_n_].p, _p_[_n_].v); \
} while (0)

inline static bool yubyIsFinite(float x) {
    volatile union { unsigned int u; float f; } uf; uf.f = x;
    return (uf.u & 0x7f800000u) != 0x7f800000u;
}

inline static bool yubyIsFinite(double x) {
    volatile union { unsigned long long int u; double f; } uf;
    uf.f = x;
    return (uf.u & 0x7ff0000000000000ull) != 0x7ff0000000000000ull;
}

#endif
