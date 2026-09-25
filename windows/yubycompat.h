#ifndef yubycompat
#define yubycompat
#include "./yuby.h"
#if _WIN32
#include <malloc.h>
#endif
namespace SuperpoweredCompat {

template <typename T, yuby::Parameter P>
class Property {
public:
    Property(void *obj) : object(obj) {}
    Property& operator=(T value) { Set(object, P, (double)value); return *this; }
    Property& operator=(const Property& other) { return *this = (T)other; }
    operator T() const { return (T)Get(object, P); }
private:
    void *object;
};

class Gate {
public:
    void *obj;
    unsigned int samplerate;
    Property<bool, yuby::OnOff>enabled;
    Property<float, yuby::WetPercent>wet;
    Property<float, yuby::BeatsPerMinute>bpm;
    Property<float, yuby::NumBeats>beats;
    Gate(unsigned int samplerateHz) : samplerate(samplerateHz), obj(yuby::Create(yuby::ObjectType::Gate)), enabled(obj), wet(obj), bpm(obj), beats(obj) {}
    ~Gate() { yuby::Delete(obj); }
    bool process(float *input, float *output, unsigned int numberOfFrames) { return yuby::Process(obj, (const float *)input, NULL, output, samplerate, numberOfFrames); }
private:
    Gate(const Gate&);
    Gate& operator=(const Gate&);
};

class Roll {
public:
    void *obj;
    unsigned int samplerate;
    Property<bool, yuby::OnOff>enabled;
    Property<float, yuby::WetPercent>wet;
    Property<float, yuby::BeatsPerMinute>bpm;
    Property<float, yuby::NumBeats>beats;
    Roll(unsigned int samplerateHz) : samplerate(samplerateHz), obj(yuby::Create(yuby::ObjectType::Roll)), enabled(obj), wet(obj), bpm(obj), beats(obj) {}
    ~Roll() { yuby::Delete(obj); }
    bool process(float *input, float *output, unsigned int numberOfFrames) { return yuby::Process(obj, (const float *)input, NULL, output, samplerate, numberOfFrames); }
private:
    Roll(const Roll&);
    Roll& operator=(const Roll&);
};

class Bitcrusher {
public:
    void *obj;
    unsigned int samplerate;
    Property<bool, yuby::OnOff>enabled;
    Property<float, yuby::FrequencyHz>frequency;
    Property<float, yuby::NumBits>bits;
    Bitcrusher(unsigned int samplerateHz) : samplerate(samplerateHz), obj(yuby::Create(yuby::ObjectType::BitCrusher)), enabled(obj), frequency(obj), bits(obj) {}
    ~Bitcrusher() { yuby::Delete(obj); }
    bool process(float *input, float *output, unsigned int numberOfFrames) { return yuby::Process(obj, (const float *)input, NULL, output, samplerate, numberOfFrames); }
private:
    Bitcrusher(const Bitcrusher&);
    Bitcrusher& operator=(const Bitcrusher&);
};

class Clipper {
public:
    void *obj;
    Property<float, yuby::ThresholdDecibel>thresholdDb;
    Property<float, yuby::CeilingDecibel>maximumDb;
    Clipper() : obj(yuby::Create(yuby::ObjectType::Clipper)), thresholdDb(obj), maximumDb(obj) {}
    ~Clipper() { yuby::Delete(obj); }
    bool process(float *input, float *output, unsigned int numberOfFrames) { return yuby::Process(obj, (const float *)input, NULL, output, 0, numberOfFrames); }
private:
    Clipper(const Clipper&);
    Clipper& operator=(const Clipper&);
};

class Limiter {
public:
    void *obj;
    unsigned int samplerate;
    Property<bool, yuby::OnOff>enabled;
    Property<float, yuby::CeilingDecibel>ceilingDb;
    Property<float, yuby::ThresholdDecibel>thresholdDb;
    Property<float, yuby::ReleaseSeconds>releaseSec;
    Limiter(unsigned int samplerateHz) : samplerate(samplerateHz), obj(yuby::Create(yuby::ObjectType::Limiter)), enabled(obj), ceilingDb(obj), thresholdDb(obj), releaseSec(obj) {}
    ~Limiter() { yuby::Delete(obj); }
    bool process(float *input, float *output, unsigned int numberOfFrames) { return yuby::Process(obj, (const float *)input, NULL, output, samplerate, numberOfFrames); }
    float getGainReductionDb() { return (float)yuby::Get(obj, yuby::GainReductionDecibel); }
private:
    Limiter(const Limiter&);
    Limiter& operator=(const Limiter&);
};

class Compressor {
public:
    void *obj;
    unsigned int samplerate;
    Property<bool, yuby::OnOff>enabled;
    Property<float, yuby::InputGainDecibel>inputGainDb;
    Property<float, yuby::OutputGainDecibel>outputGainDb;
    Property<float, yuby::WetPercent>wet;
    Property<float, yuby::AttackSeconds>attackSec;
    Property<float, yuby::ReleaseSeconds>releaseSec;
    Property<float, yuby::Ratio>ratio;
    Property<float, yuby::ThresholdDecibel>thresholdDb;
    float hpCutOffHz;
    Compressor(unsigned int samplerateHz) : samplerate(samplerateHz), obj(yuby::Create(yuby::ObjectType::Compressor)), enabled(obj), inputGainDb(obj), outputGainDb(obj), wet(obj), attackSec(obj), releaseSec(obj), ratio(obj), thresholdDb(obj), hpCutOffHz(1.0f) {}
    ~Compressor() { yuby::Delete(obj); }
    bool process(float *input, float *output, unsigned int numberOfFrames) { return yuby::Process(obj, (const float *)input, NULL, output, samplerate, numberOfFrames); }
    float getGainReductionDb() { return (float)yuby::Get(obj, yuby::GainReductionDecibel); }
private:
    Compressor(const Compressor&);
    Compressor& operator=(const Compressor&);
};

class Compressor2 {
public:
    void *obj;
    unsigned int samplerate;
    Property<bool, yuby::OnOff>enabled;
    Property<float, yuby::AttackSeconds>attackSec;
    Property<float, yuby::HoldSeconds>holdSec;
    Property<float, yuby::ReleaseSeconds>releaseSec;
    Property<float, yuby::Ratio>ratio;
    Property<float, yuby::ThresholdDecibel>thresholdDb;
    Property<float, yuby::KneeWidthDecibel>softKneeDb;
    Property<float, yuby::OutputGainDecibel>outputGainDb;
    Property<bool, yuby::AutoGain>automaticGain;
    Compressor2(unsigned int samplerateHz) : samplerate(samplerateHz), obj(yuby::Create(yuby::ObjectType::Compressor)), enabled(obj), attackSec(obj), holdSec(obj), releaseSec(obj), ratio(obj), thresholdDb(obj), softKneeDb(obj), outputGainDb(obj), automaticGain(obj) {}
    ~Compressor2() { yuby::Delete(obj); }
    bool process(float *input, float *output, unsigned int numberOfFrames) { return yuby::Process(obj, (const float *)input, NULL, output, samplerate, numberOfFrames); }
    bool processWithSidechain(float *input, float *sidechain, float *output, unsigned int numberOfFrames) { return yuby::Process(obj, (const float *)input, (const float *)sidechain, output, samplerate, numberOfFrames); }
    float getGainReductionDb() { return (float)yuby::Get(obj, yuby::GainReductionDecibel); }
private:
    Compressor2(const Compressor2&);
    Compressor2& operator=(const Compressor2&);
};

class Filter {
public:
    typedef enum FilterType { Resonant_Lowpass = 0, Resonant_Highpass = 1, Bandlimited_Bandpass = 2, Bandlimited_Notch = 3, LowShelf = 4, HighShelf = 5, Parametric = 6, CustomCoefficients = 7 } FilterType;
    void *obj;
    unsigned int samplerate;
    Property<bool, yuby::OnOff>enabled;
    Property<float, yuby::FrequencyHz>frequency;
    Property<float, yuby::OutputGainDecibel>decibel;
    Property<float, yuby::ResonancePercent>resonance;
    Property<float, yuby::Octave>octave;
    Property<float, yuby::SlopePercent>slope;
    Property<FilterType, yuby::Type>type;
    Filter(unsigned int samplerateHz) : samplerate(samplerateHz), obj(yuby::Create(yuby::ObjectType::Filter)), enabled(obj), frequency(obj), decibel(obj), resonance(obj), octave(obj), slope(obj), type(obj) {}
    ~Filter() { yuby::Delete(obj); }
    void setCustomCoefficients(float b0, float b1, float b2, float a1, float a2) { YubySetParameters(obj, { yuby::FilterB0, b0 }, { yuby::FilterB1, b1 }, { yuby::FilterB2, b2 }, { yuby::FilterA1, a1 }, { yuby::FilterA2, a2 }); }
    bool process(float *input, float *output, unsigned int numberOfFrames) { return yuby::Process(obj, (const float *)input, NULL, output, samplerate, numberOfFrames); }
private:
    Filter(const Filter&);
    Filter& operator=(const Filter&);
};

class ThreeBandEQ {
public:
    void *obj;
    unsigned int samplerate;
    Property<bool, yuby::OnOff>enabled;
    Property<float, yuby::Low>low;
    Property<float, yuby::Mid>mid;
    Property<float, yuby::High>high;
    ThreeBandEQ(unsigned int samplerateHz) : samplerate(samplerateHz), obj(yuby::Create(yuby::ObjectType::EQ)), enabled(obj), low(obj), mid(obj), high(obj) {}
    ~ThreeBandEQ() { yuby::Delete(obj); }
    bool process(float *input, float *output, unsigned int numberOfFrames) { return yuby::Process(obj, (const float *)input, NULL, output, samplerate, numberOfFrames); }
private:
    ThreeBandEQ(const ThreeBandEQ&);
    ThreeBandEQ& operator=(const ThreeBandEQ&);
};

class Whoosh {
public:
    void *obj;
    unsigned int samplerate;
    Property<bool, yuby::OnOff>enabled;
    Property<float, yuby::WetPercent>wet;
    Property<float, yuby::FrequencyHz>frequency;
    Whoosh(unsigned int samplerateHz) : samplerate(samplerateHz), obj(yuby::Create(yuby::ObjectType::Whoosh)), enabled(obj), wet(obj), frequency(obj) {}
    ~Whoosh() { yuby::Delete(obj); }
    bool process(float *input, float *output, unsigned int numberOfFrames) { return yuby::Process(obj, (const float *)input, NULL, output, samplerate, numberOfFrames); }
private:
    Whoosh(const Whoosh&);
    Whoosh& operator=(const Whoosh&);
};

class Flanger {
public:
    void *obj;
    unsigned int samplerate;
    Property<bool, yuby::OnOff>enabled;
    Property<float, yuby::WetPercent>wet;
    Property<float, yuby::DepthPercent>depth;
    Property<float, yuby::NumBeats>lfoBeats;
    Property<float, yuby::BeatsPerMinute>bpm;
    Flanger(unsigned int samplerateHz) : samplerate(samplerateHz), obj(yuby::Create(yuby::ObjectType::Flanger)), enabled(obj), wet(obj), depth(obj), lfoBeats(obj), bpm(obj) {}
    ~Flanger() { yuby::Delete(obj); }
    bool process(float *input, float *output, unsigned int numberOfFrames) { return yuby::Process(obj, (const float *)input, NULL, output, samplerate, numberOfFrames); }
private:
    Flanger(const Flanger&);
    Flanger& operator=(const Flanger&);
};

class Echo {
public:
    void *obj, *fxObj;
    unsigned int samplerate;
    Property<bool, yuby::OnOff>enabled;
    Property<float, yuby::DryPercent>dry;
    Property<float, yuby::WetPercent>wet;
    Property<float, yuby::BeatsPerMinute>bpm;
    Property<float, yuby::NumBeats>beats;
    Property<float, yuby::DecayPercent>decay;
    Echo(unsigned int samplerateHz) : samplerate(samplerateHz), obj(yuby::Create(yuby::ObjectType::Echo)), enabled(obj), dry(obj), wet(obj), bpm(obj), beats(obj), decay(obj), fxObj(NULL) {}
    ~Echo() { yuby::Delete(obj); }
    void setMix(float mix) { yuby::Set(obj, yuby::MixPercent, mix); }
    bool process(float *input, float *output, unsigned int numberOfFrames) { return yuby::Process(obj, (const float *)input, NULL, output, samplerate, numberOfFrames); }
    bool processWithFx(float *input, float *output, unsigned int numberOfFrames, void *fx) {
        if (fxObj != fx) { fxObj = fx; yuby::SetColorFx(obj, fxObj); }
        return yuby::Process(obj, (const float *)input, NULL, output, samplerate, numberOfFrames);
    }
private:
    Echo(const Echo&);
    Echo& operator=(const Echo&);
};

class Delay {
public:
    void *obj, *fxObj;
    unsigned int samplerate;
    Property<float, yuby::DelayMs>delayMs;
    Delay(unsigned int maximumDelayMs, unsigned int maximumSamplerate, unsigned int maximumNumberOfFramesToProcess, unsigned int samplerateHz) : samplerate(samplerateHz), obj(yuby::Create(yuby::ObjectType::Delay)), delayMs(obj), fxObj(NULL), output((float *)malloc(maximumNumberOfFramesToProcess * maximumSamplerate * 8)) {
        YubySetParameters(obj, { yuby::MaxDelayMs, (double)maximumDelayMs }, { yuby::OnOff, 1.0 });
    }
    ~Delay() { yuby::Delete(obj); if (output) free(output); }
    const float * const process(float *input, int numberOfFrames) {
        return !output || !yuby::Process(obj, (const float *)input, NULL, output, samplerate, numberOfFrames) ? NULL : output;
    }
    const float * const processWithFx(float *input, int numberOfFrames, void *fx) {
        if (fxObj != fx) { fxObj = fx; yuby::SetColorFx(obj, fxObj); }
        return !output || !yuby::Process(obj, (const float *)input, NULL, output, samplerate, numberOfFrames) ? NULL : output;
    }
private:
    float *output;
    Delay(const Delay&);
    Delay& operator=(const Delay&);
};

class Reverb {
public:
    void *obj;
    unsigned int samplerate;
    Property<bool, yuby::OnOff> enabled;
    Property<float, yuby::DryPercent> dry;
    Property<float, yuby::WetPercent> wet;
    Property<float, yuby::MixPercent> mix;
    Property<float, yuby::WidthPercent> width;
    Property<float, yuby::DampPercent> damp;
    Property<float, yuby::DecayPercent> roomSize;
    Property<float, yuby::DelayMs> predelayMs;
    Property<float, yuby::LowCutHz> lowCutHz;
    Reverb(unsigned int samplerateHz) : samplerate(samplerateHz), obj(yuby::Create(yuby::ObjectType::Reverb)), enabled(obj), dry(obj), wet(obj), mix(obj), width(obj), damp(obj), roomSize(obj), predelayMs(obj), lowCutHz(obj) {}
    ~Reverb() { yuby::Delete(obj); }
    bool process(float *input, float *output, unsigned int numberOfFrames) {
        return yuby::Process(obj, (const float *)input, NULL, output, samplerate, numberOfFrames);
    }
private:
    Reverb(const Reverb&);
    Reverb& operator=(const Reverb&);
};

class Resampler {
public:
    void *obj;
    Property<float, yuby::PlaybackRate> rate;
    Resampler() : obj(yuby::Create(yuby::ObjectType::Resampler)), rate(obj) {}
    ~Resampler() { yuby::Delete(obj); }
    int process(short int *input, float *output, int numberOfFrames, bool reverse = false, bool highQuality = false, float rateAdd = 0.0f) {
        YubySetParameters(obj, { yuby::Reverse, reverse ? 1.0 : 0.0 }, { yuby::HighQuality, highQuality ? 1.0 : 0.0 }, { yuby::PlaybackRateIncrease, rateAdd });
        return yuby::Process(obj, (const float *)input, NULL, output, 0, numberOfFrames) ? (int)yuby::Get(obj, yuby::FramesCreated) : 0;
    }
private:
    Resampler(const Resampler&);
    Resampler& operator=(const Resampler&);
};

class AutomaticVocalPitchCorrection {
public:
    typedef enum TunerScale { CHROMATIC = 0, CMAJOR = 2741, CSHARPMAJOR = 1387, DMAJOR = 2774, DSHARPMAJOR = 1453, EMAJOR = 2906, FMAJOR = 1717, FSHARPMAJOR = 3434, GMAJOR = 2773, GSHARPMAJOR = 1451, AMAJOR = 2902, ASHARPMAJOR = 1709, BMAJOR = 3418, AMINOR = CMAJOR, ASHARPMINOR = CSHARPMAJOR, BMINOR = DMAJOR, CMINOR = DSHARPMAJOR, CSHARPMINOR = EMAJOR, DMINOR = FMAJOR, DSHARPMINOR = FSHARPMAJOR, EMINOR = GMAJOR, FMINOR = GSHARPMAJOR, FSHARPMINOR = AMAJOR, GMINOR = ASHARPMAJOR, GSHARPMINOR = BMAJOR, CUSTOM = CHROMATIC } TunerScale;
    typedef enum TunerRange { WIDE = 0, BASS = 1, TENOR = 2, ALTO = 3, SOPRANO = 4 } TunerRange;
    typedef enum TunerSpeed { SUBTLE = 0, MEDIUM = 1, EXTREME = 2 } TunerSpeed;
    typedef enum TunerClamp { OFF = 0, LOOSE = 1, TIGHT = 2 } TuneClamp;
    void *obj;
    unsigned int samplerate;
    Property<TunerScale, yuby::Scale> scale;
    Property<TunerRange, yuby::Range> range;
    Property<TunerSpeed, yuby::Speed> speed;
    Property<TunerClamp, yuby::Clamp> clamp;
    Property<float, yuby::FrequencyHz> frequencyOfA;
    AutomaticVocalPitchCorrection() : samplerate(48000), obj(yuby::Create(yuby::ObjectType::AutoTune)), scale(obj), range(obj), speed(obj), clamp(obj), frequencyOfA(obj) {}
    ~AutomaticVocalPitchCorrection() { yuby::Delete(obj); }
    void process(float *input, float *output, bool stereo, unsigned int numberOfFrames) { yuby::Process(obj, (const float *)input, NULL, output, samplerate, numberOfFrames); }
    void reset() { yuby::Set(obj, yuby::Reset, 1.0); }
    void setCustomScaleNote(unsigned char note, bool enabled) {
        unsigned int s = (unsigned int)yuby::Get(obj, yuby::Scale), v = 1 << note;
        if (enabled) s |= v; else s &= ~v;
        yuby::Set(obj, yuby::Scale, (double)s);
    }
    bool getCustomScaleNote(unsigned char note) { return ((int)yuby::Get(obj, yuby::Scale) & (1 << note)) != 0; }
private:
    AutomaticVocalPitchCorrection(const AutomaticVocalPitchCorrection&);
    AutomaticVocalPitchCorrection& operator=(const AutomaticVocalPitchCorrection&);
};

class AEC {
public:
    typedef enum MicToSpeakerDistance { MicToSpeakerDistance_Handheld = 0, MicToSpeakerDistance_Close = 1, MicToSpeakerDistance_Room = 2, MicToSpeakerDistance_Large = 3 } MicToSpeakerDistance;
    void *obj;
    unsigned int samplerate;
    Property<MicToSpeakerDistance, yuby::Distance> distance;
    Property<float, yuby::DoubleTalkSensitivityPercent> doubleTalkSensitivity;
    Property<float, yuby::Speed> qualityVsQuickAdapt;
    AEC() : samplerate(48000), obj(yuby::Create(yuby::ObjectType::AEC)), distance(obj), doubleTalkSensitivity(obj), qualityVsQuickAdapt(obj) {}
    ~AEC() { yuby::Delete(obj); }
    void process(float *loudspeaker, float *mic, float *output, unsigned int numberOfFrames) { yuby::Process(obj, (const float *)loudspeaker, (const float *)mic, output, samplerate, numberOfFrames); }
    void reset() { yuby::Set(obj, yuby::Reset, 1.0); }
private:
    AEC(const AEC&);
    AEC& operator=(const AEC&);
};

class BandpassFilterbank {
public:
    void *obj;
    unsigned int samplerate;
    BandpassFilterbank(unsigned int numBands, float *frequencies, float *widths, unsigned int samplerateHz, unsigned int numGroups = 0) : nBands(numBands), samplerate(samplerateHz), obj(yuby::Create(yuby::ObjectType::BandpassFilterbank)), bands((float *)malloc((numBands + 3) * 4)), currentResult(NULL) {
        yuby::Set(obj, yuby::NumBins, numBands);
        for (unsigned int n = 0; n < numBands; n++) { yuby::Set(obj, yuby::FrequencyHz, frequencies[n], n); yuby::Set(obj, yuby::Octave, widths[n], n); }
    }
    ~BandpassFilterbank() { yuby::Delete(obj); if (bands) free(bands); }
    void process(float *input, unsigned int numberOfFrames, int group = 0) {
        yuby::Process(obj, (const float *)input, NULL, NULL, samplerate, numberOfFrames);
        currentResult = NULL;
    }
    void processNoAdd(float *input, unsigned int numberOfFrames, int group = 0) {
        yuby::Set(obj, yuby::Reset, yuby::BandpassFilterbankResetFlag::Bands);
        yuby::Process(obj, (const float *)input, NULL, NULL, samplerate, numberOfFrames);
        currentResult = NULL;
    }
    float *getBands() { if (bands) yuby::Process(obj, NULL, NULL, bands, 0, 0); currentResult = bands; return bands; }
    void resetBands() { yuby::Set(obj, yuby::Reset, yuby::BandpassFilterbankResetFlag::Bands); }
    float getAverageVolume() { getBands(); return currentResult[nBands + 1]; }
    float getSumVolume() { getBands(); return currentResult[nBands]; }
    void resetSumAndAverageVolume() { yuby::Set(obj, yuby::Reset, yuby::BandpassFilterbankResetFlag::SumAndAverage); }
    float getPeakVolume() { getBands(); return currentResult[nBands + 2]; }
    void resetPeakVolume() { yuby::Set(obj, yuby::Reset, yuby::BandpassFilterbankResetFlag::Peak); }
private:
    float *bands, *currentResult;
    unsigned int nBands;
    BandpassFilterbank(const BandpassFilterbank&);
    BandpassFilterbank& operator=(const BandpassFilterbank&);
};

class Analyzer {
public:
    void *obj;
    float peakDb, averageDb, loudpartsAverageDb, bpm, beatgridStartMs;
    int keyIndex, waveformSize, overviewSize;
    Analyzer(unsigned int samplerate, int lengthSeconds) : obj(yuby::Create(yuby::ObjectType::Analyzer)) {
        YubySetParameters(obj, { yuby::SamplerateHz, (double)samplerate }, { yuby::DurationSeconds, (double)lengthSeconds });
    }
    ~Analyzer() { yuby::Delete(obj); }
    void process(float *input, unsigned int numberOfFrames, int lengthSeconds = -1) { yuby::Process(obj, (const float *)input, NULL, NULL, 0, numberOfFrames); }
    void makeResults(float minimumBpm, float maximumBpm, float knownBpm, float aroundBpm, bool getBeatgridStartMs, float aroundBeatgridStartMs, bool makeOverviewWaveform, bool makeLowMidHighWaveforms, bool getKeyIndex) {
        YubySetParameters(obj, { yuby::MinBPM, minimumBpm }, { yuby::MaxBPM, maximumBpm }, { yuby::BeatsPerMinute, knownBpm }, { yuby::FirstBeatMs, aroundBeatgridStartMs });
        peakDb = (float)yuby::Get(obj, yuby::PeakDecibel);
        averageDb = (float)yuby::Get(obj, yuby::AverageDecibel);
        loudpartsAverageDb = (float)yuby::Get(obj, yuby::LoudPartsAverageDecibel);
        bpm = (float)yuby::Get(obj, yuby::BeatsPerMinute);
        if (getBeatgridStartMs) beatgridStartMs = (float)yuby::Get(obj, yuby::FirstBeatMs);
        if (getKeyIndex) keyIndex = (int)yuby::Get(obj, yuby::KeyIndex);
        overviewSize = (int)yuby::Get(obj, yuby::DurationSeconds);
        waveformSize = (int)yuby::Get(obj, yuby::DurationFrames);
    }
    unsigned char *getPeakWaveform(bool takeOwnership = false) { return takeOwnership ? (unsigned char *)yuby::GetDataTakeOwnership(obj, yuby::PeakWaveform) : (unsigned char *)yuby::GetData(obj, yuby::PeakWaveform); }
    unsigned char *getAverageWaveform(bool takeOwnership = false) { return takeOwnership ? (unsigned char *)yuby::GetDataTakeOwnership(obj, yuby::AverageWaveform) : (unsigned char *)yuby::GetData(obj, yuby::AverageWaveform); }
    unsigned char *getLowWaveform(bool takeOwnership = false) { return takeOwnership ? (unsigned char *)yuby::GetDataTakeOwnership(obj, yuby::LowWaveform) : (unsigned char *)yuby::GetData(obj, yuby::LowWaveform); }
    unsigned char *getMidWaveform(bool takeOwnership = false) { return takeOwnership ? (unsigned char *)yuby::GetDataTakeOwnership(obj, yuby::MidWaveform) : (unsigned char *)yuby::GetData(obj, yuby::MidWaveform); }
    unsigned char *getHighWaveform(bool takeOwnership = false) { return takeOwnership ? (unsigned char *)yuby::GetDataTakeOwnership(obj, yuby::HighWaveform) : (unsigned char *)yuby::GetData(obj, yuby::HighWaveform); }
    unsigned char *getNotes(bool takeOwnership = false) { return NULL; }
    char *getOverviewWaveform(bool takeOwnership = false) { return takeOwnership ? (char *)yuby::GetDataTakeOwnership(obj, yuby::OverviewWaveform) : (char *)yuby::GetData(obj, yuby::OverviewWaveform); }
private:
    Analyzer(const Analyzer&);
    Analyzer& operator=(const Analyzer&);
};

class Recorder {
public:
    void *obj;
    unsigned int samplerate;
    Recorder(const char *tempPath = NULL, bool mono = false) : obj(yuby::Create(yuby::ObjectType::Recorder)) {}
    ~Recorder() { yuby::Delete(obj); }
    bool prepare(const char *destinationPath, unsigned int samplerateHz, bool fadeInFadeOut, unsigned int minimumLengthSeconds) {
        samplerate = samplerateHz;
        YubySetParameters(obj, { yuby::Fade, fadeInFadeOut ? 1.0 : 0.0 }, { yuby::AttackSeconds, (double)minimumLengthSeconds });
        yuby::Open(obj, destinationPath);
        return true;
    }
    void stop() { yuby::Open(obj, NULL); }
    bool isFinished() { return true; }
    void addToTracklist(char *artist, char *title, int becameAudibleSeconds = 0, bool takeOwnership = false) { yuby::AddToTracklist(obj, artist, title, becameAudibleSeconds, takeOwnership); }
    unsigned int recordNonInterleaved(float *left, float *right, unsigned int numberOfFrames) {
        float *temp = (float *)alloca(numberOfFrames * 8);
        if (temp) {
            yuby::interleave(left, right, temp, numberOfFrames);
            yuby::Process(obj, temp, NULL, NULL, samplerate, numberOfFrames);
        }
        return (unsigned int)yuby::Get(obj, yuby::DurationSeconds);
    }
    unsigned int recordInterleaved(float *input, unsigned int numberOfFrames) {
        yuby::Process(obj, (const float *)input, NULL, NULL, samplerate, numberOfFrames);
        return (unsigned int)yuby::Get(obj, yuby::DurationSeconds);
    }
private:
    Recorder(const Recorder&);
    Recorder& operator=(const Recorder&);
};

class Decoder { 
public:
    typedef enum Format { Format_MP3 = 0, Format_AAC = 1, Format_AIFF = 2, Format_WAV = 3, FLAC = 4, ALAC = 5 } Format;
    static const int OpenSuccess = 0;
    static const int EndOfFile = 0;
    static const int BufferingTryAgainLater = -1;
    static const int NetworkError = -2;
    static const int Error = -3;
    void *obj;
    static const char *statusCodeToString(int code) { return ""; }
    Decoder() : obj(yuby::Create(yuby::ObjectType::Decoder)) {}
    ~Decoder() { yuby::Delete(obj); }
    int getDurationFrames() { return (int)yuby::Get(obj, yuby::DurationFrames); }
    double getDurationSeconds() { return yuby::Get(obj, yuby::DurationSeconds); }
    unsigned int getSamplerate() { return (unsigned int)yuby::Get(obj, yuby::SamplerateHz); }
    Format getFormat() { return (Format)yuby::Get(obj, yuby::Format); }
    unsigned int getFramesPerChunk() { return (unsigned int)yuby::Get(obj, yuby::FramesPerPacket); }
    int getPositionFrames() { return (int)yuby::Get(obj, yuby::PositionFrames); }
    float getBufferedStartPercent() { return 0.0f; }
    float getBufferedEndPercent() { return (float)yuby::Get(obj, yuby::BufferedPercent); }
    const char *getFullyDownloadedFilePath() { return NULL; }
    unsigned int getCurrentBps() { return 0; }
    int open(const char *path, bool metaOnly = false, int offset = 0, int length = 0, int stemsIndex = 0, void *customHTTPRequest = 0) {
        unsigned int flags = yuby::OpenFlag::MeasureSilence | (metaOnly ? yuby::OpenFlag::ParseMetadataOnly : 0);
        yuby::Open(obj, path, flags);
        return yuby::GetString(obj, yuby::ErrorMessage) == NULL ? OpenSuccess : Error;
    }
    int decodeAudio(short int *output, unsigned int numberOfFrames) {
        if (!yuby::Process(obj, NULL, NULL, (float *)output, 0, numberOfFrames)) return Error;
        int f = (int)yuby::Get(obj, yuby::FramesCreated);
        if (f == 0) return BufferingTryAgainLater; else if (f < 0) return EndOfFile; else return f;
    }
    bool setPositionQuick(int positionFrames) { return yuby::Set(obj, yuby::PositionFrames, positionFrames); }
    bool setPositionPrecise(int positionFrames) { return yuby::Set(obj, yuby::PositionFramesPrecise, positionFrames); }
    int getAudioStartFrame(unsigned int limitFrames = 0, int thresholdDb = 0) { return (int)yuby::Get(obj, yuby::StartMs); }
    const char *getStemsJSONString(bool takeOwnership = false) { return takeOwnership ? yuby::GetStringTakeOwnership(obj, yuby::StemsJSON) : yuby::GetString(obj, yuby::StemsJSON); }
    void parseAllID3Frames(bool skipImages, unsigned int maxFrameDataSize) {}
    void startParsingID3Frames(bool skipImages, unsigned int maxFrameDataSize) {}
    unsigned int readNextID3Frame() { return (unsigned int)yuby::Get(obj, yuby::ID3FrameDataSizeBytes); }
    unsigned int getID3FrameName() { unsigned int u = 0; const char *p = yuby::GetString(obj, yuby::ID3FrameName); if (p) memcpy(&u, p, 4); return u; }
    void *getID3FrameData() { return yuby::GetData(obj, yuby::ID3FrameData); }
    unsigned int getID3FrameDataLengthBytes() { return (unsigned int)yuby::Get(obj, yuby::ID3FrameDataSizeBytes); }
    char *getID3FrameAsString(bool takeOwnership = false) { return takeOwnership ? yuby::GetStringTakeOwnership(obj, yuby::ID3FrameDataAsString) : (char *)yuby::GetString(obj, yuby::ID3FrameDataAsString); }
    char *getArtist(bool takeOwnership = false) { return takeOwnership ? yuby::GetStringTakeOwnership(obj, yuby::Artist) : (char *)yuby::GetString(obj, yuby::Artist); }
    char *getTitle(bool takeOwnership = false) { return takeOwnership ? yuby::GetStringTakeOwnership(obj, yuby::Title) : (char *)yuby::GetString(obj, yuby::Title); }
    char *getAlbum(bool takeOwnership = false) { return takeOwnership ? yuby::GetStringTakeOwnership(obj, yuby::Album) : (char *)yuby::GetString(obj, yuby::Album); }
    unsigned int getTrackIndex() { return (unsigned int)yuby::Get(obj, yuby::TrackIndex); }
    void *getImage(bool takeOwnership = false) { return takeOwnership ? yuby::GetDataTakeOwnership(obj, yuby::Image) : yuby::GetData(obj, yuby::Image); }
    unsigned int getImageSizeBytes() { return (unsigned int)yuby::Get(obj, yuby::ImageSizeBytes); }
    float getBPM() { return (float)yuby::Get(obj, yuby::BeatsPerMinute); }
    void reconnectToMediaserver() {}
private:
    Decoder(const Decoder&);
    Decoder& operator=(const Decoder&);
};

class AdvancedAudioPlayer {
public:
    static const float MaxPlaybackRate;
    void *obj;
    
    typedef enum JogMode { JogMode_Scratch = 0, JogMode_PitchBend = 1,  JogMode_Parameter = 2 } JogMode;
    typedef enum PlayerEvent { PlayerEvent_None = 0, PlayerEvent_Opening = 1, PlayerEvent_OpenFailed = 2, PlayerEvent_Opened = 10, PlayerEvent_ConnectionLost = 3, PlayerEvent_ProgressiveDownloadFinished = 11
    } PlayerEvent;
    typedef enum SyncMode { SyncMode_None = 0, SyncMode_Tempo = 1, SyncMode_TempoAndBeat = 2 } SyncMode;

    unsigned int outputSamplerate;
    Property<double, yuby::PlaybackRate> playbackRate;
    Property<bool, yuby::TimeStretching> timeStretching;
    Property<float, yuby::FormantCorrectionPercent> formantCorrection;
    Property<double, yuby::OriginalBPM> originalBPM;
    Property<bool, yuby::FixDoubleOrHalfBPM> fixDoubleOrHalfBPM;
    Property<double, yuby::FirstBeatMs> firstBeatMs;
    Property<double, yuby::DefaultQuantum> defaultQuantum;
    Property<AdvancedAudioPlayer::SyncMode, yuby::SyncMode> syncMode;
    Property<double, yuby::BeatsPerMinute> syncToBpm;
    Property<double, yuby::MsElapsedSinceLastBeat> syncToMsElapsedSinceLastBeat;
    Property<double, yuby::Phase> syncToPhase;
    Property<double, yuby::Quantum> syncToQuantum;
    Property<int, yuby::PitchShiftCents> pitchShiftCents;
    Property<bool, yuby::LoopOnEnd> loopOnEOF;
    Property<bool, yuby::ReverseToForwardAtLoopStart> reverseToForwardAtLoopStart;
    Property<unsigned char, yuby::TimeStretchingSound> timeStretchingSound;
    
    static void setTempFolder(const char *path) {}
    static const char *getTempFolderPath() { return NULL; }
    static const char *statusCodeToString(int code) { return ""; }
    
    AdvancedAudioPlayer(unsigned int samplerate, unsigned char cachedPointCount, unsigned int internalBufferSizeSeconds = 2, unsigned int negativeSeconds = 0, float minimumTimestretchingPlaybackRate = 0.501f, float maximumTimestretchingPlaybackRate = 2.0f, bool enableStems = false) :
        obj(yuby::Create(yuby::ObjectType::Player)),
        outputSamplerate(samplerate),
        playbackRate(obj), timeStretching(obj), formantCorrection(obj), originalBPM(obj), fixDoubleOrHalfBPM(obj), firstBeatMs(obj), defaultQuantum(obj), syncToBpm(obj), syncToMsElapsedSinceLastBeat(obj), syncToPhase(obj), syncToQuantum(obj), pitchShiftCents(obj), syncMode(obj), loopOnEOF(obj), reverseToForwardAtLoopStart(obj), timeStretchingSound(obj) {
            YubySetParameters(obj, { yuby::MinTimeStretchRate, minimumTimestretchingPlaybackRate }, { yuby::MaxTimeStretchRate, maximumTimestretchingPlaybackRate }, { yuby::NegativeSeconds, (double)negativeSeconds }, { yuby::BufferSizeSeconds, (double)internalBufferSizeSeconds });
    }
    ~AdvancedAudioPlayer() { yuby::Delete(obj); }
    void open(const char *path, void *customHTTPRequest = 0, bool skipSilenceAtBeginning = false, bool measureSilenceAtEnd = false) {
        yuby::Open(obj, path, skipSilenceAtBeginning ? yuby::OpenFlag::MeasureSilence : 0);
    }
    void open(const char *path, int offset, int length, void *customHTTPRequest = 0, bool skipSilenceAtBeginning = false, bool measureSilenceAtEnd = false) {
        yuby::Open(obj, path, skipSilenceAtBeginning ? yuby::OpenFlag::MeasureSilence : 0);
    }
    PlayerEvent getLatestEvent() { return ((PlayerEvent)yuby::Get(obj, yuby::OpenState)); }
    int getOpenErrorCode() { return 0; }
    const char *getFullyDownloadedFilePath() { return NULL; }
    bool eofRecently() { return yuby::Get(obj, yuby::AtTheEnd) != 0.0; }
    bool isWaitingForBuffering() { return yuby::Get(obj, yuby::IsBuffering) != 0.0; }
    double getAudioStartMs() { return yuby::Get(obj, yuby:: StartMs); }
    double getAudioEndMs() { return 0; }
    double getPositionMs() { return yuby::Get(obj, yuby::PositionMs); }
    double getDisplayPositionMs() { return yuby::Get(obj, yuby::DisplayPositionMs); }
    float getDisplayPositionPercent() { return (float)yuby::Get(obj, yuby::DisplayPositionPercent); }
    int getDisplayPositionSeconds() { return (int)yuby::Get(obj, yuby::DisplayPositionSeconds); }
    double afterSlipModeWillJumpBackToPositionMs() { return yuby::Get(obj, yuby::PositionAfterSlipModeMs); }
    double getDurationMs() { return yuby::Get(obj, yuby::DurationMs); }
    unsigned int getDurationSeconds() { return (unsigned int)yuby::Get(obj, yuby::DurationSeconds); }
    void play() { yuby::Set(obj, yuby::Play, 1.0); }
    void playSynchronized() { yuby::Set(obj, yuby::PlaySynchronized, 1.0); }
    void playSynchronizedToPosition(double positionMs) { yuby::Set(obj, yuby::PlaySynchronizedToMs, positionMs); }
    void pause(float decelerateSeconds = 0, unsigned int slipMs = 0) {
        if (decelerateSeconds > 0.0f) yuby::Set(obj, yuby::TurntableBreak, decelerateSeconds, slipMs); else yuby::Set(obj, yuby::Play, 0.0);
    }
    void togglePlayback() { yuby::Set(obj, yuby::TogglePlayPause, 1.0); }
    bool isPlaying() { return yuby::Get(obj, yuby::Play) != 0.0; }
    void seek(double percent) { yuby::Set(obj, yuby::DisplayPositionPercent, percent); }
    void setPosition(double ms, bool andStop, bool synchronisedStart, bool forceDefaultQuantum = false, bool preferWaitingforSynchronisedStart = false) {
        unsigned int flags = 0;
        if (andStop) flags |= yuby::SetPositionFlag::Stop;
        if (synchronisedStart) flags |= yuby::SetPositionFlag::SynchronizedStart;
        if (forceDefaultQuantum) flags |= yuby::SetPositionFlag::ForceDefaultQuantum;
        if (preferWaitingforSynchronisedStart) flags |= yuby::SetPositionFlag::PreferWaitingForSynchronizedStart;
        yuby::Set(obj, yuby::PositionMs, ms, flags);
    }
    void cachePosition(double ms, unsigned char pointID = 255) { yuby::Set(obj, yuby::CachePositionMs, ms, pointID); }
    bool processStereo(float *buffer, bool mix, unsigned int numberOfFrames, float volume = 1.0f) {
        yuby::Set(obj, yuby::GainPercent, volume, 0);
        return yuby::Process(obj, mix ? buffer : NULL, NULL, buffer, outputSamplerate, numberOfFrames);
    }
    bool process8Channels(float *buffer0, float *buffer1, float *buffer2, float *buffer3, bool mix, unsigned int numberOfFrames, float volume0, float volume1, float volume2, float volume3) {
        yuby::Set(obj, yuby::GainPercent, volume0, 0);
        yuby::Set(obj, yuby::GainPercent, volume1, 1);
        yuby::Set(obj, yuby::GainPercent, volume2, 2);
        yuby::Set(obj, yuby::GainPercent, volume3, 3);
        float *temp = (float *)alloca(numberOfFrames * 32);
        if (mix) {
            memcpy(temp, buffer0, numberOfFrames * 8);
            memcpy(temp + numberOfFrames * 2, buffer1, numberOfFrames * 8);
            memcpy(temp + numberOfFrames * 4, buffer2, numberOfFrames * 8);
            memcpy(temp + numberOfFrames * 6, buffer3, numberOfFrames * 8);
        }
        bool r = yuby::Process(obj, mix ? temp : NULL, NULL, temp, outputSamplerate, numberOfFrames, 4);
        if (r) {
            memcpy(buffer0, temp, numberOfFrames * 8);
            memcpy(buffer1, temp + numberOfFrames * 2, numberOfFrames * 8);
            memcpy(buffer2, temp + numberOfFrames * 4, numberOfFrames * 8);
            memcpy(buffer3, temp + numberOfFrames * 6, numberOfFrames * 8);
        }
        return r;
    }
    bool isStems() { return yuby::Get(obj, yuby::IsStems) != 0.0; }
    void processSTEMSMaster(float *input, float *output, unsigned int numberOfFrames, float volume = 1.0f) {
        yuby::Process(obj, input, NULL, output, outputSamplerate, numberOfFrames);
    }
    void onMediaserverInterrupt() {}
    float getBufferedStartPercent() { return 0.0f; }
    float getBufferedEndPercent() { return (float)yuby::Get(obj, yuby::BufferedPercent); }
    double getCurrentBpm() { return yuby::Get(obj, yuby::BeatsPerMinute); }
    double getMsElapsedSinceLastBeat() { return yuby::Get(obj, yuby::MsElapsedSinceLastBeat); }
    float getBeatIndex() { return (float) yuby::Get(obj, yuby::BeatIndex); }
    double getPhase() { return yuby::Get(obj, yuby::Phase); }
    double getQuantum() { return yuby::Get(obj, yuby::Quantum); }
    double getMsDifference(double phase, double quantum) { return yuby::Get(obj, yuby::MsDifference, quantum, phase); }
    double getMsRemainingToSyncEvent() { return yuby::Get(obj, yuby::MsRemainingToSyncEvent); }
    void loop(double startMs, double lengthMs, bool jumpToStartMs, unsigned char pointID, bool synchronisedStart, unsigned int numLoops = 0, bool forceDefaultQuantum = false, bool preferWaitingforSynchronisedStart = false) {
        unsigned int flags = 0;
        if (jumpToStartMs) flags |= yuby::LoopFlag::JumpToStart;
        if (synchronisedStart) flags |= yuby::LoopFlag::SynchronizedStart;
        if (forceDefaultQuantum) flags |= yuby::LoopFlag::ForceDefaultQuantum;
        if (preferWaitingforSynchronisedStart) flags |= yuby::LoopFlag::PreferWaitingForSynchronizedStart;
        yuby::Set(obj, yuby::Loop, startMs, lengthMs, pointID, numLoops, flags);
    }
    void loopBetween(double startMs, double endMs, bool jumpToStartMs, unsigned char pointID, bool synchronisedStart, unsigned int numLoops = 0, bool forceDefaultQuantum = false, bool preferWaitingforSynchronisedStart = false) {
         unsigned int flags = 0;
        if (jumpToStartMs) flags |= yuby::LoopFlag::JumpToStart;
        if (synchronisedStart) flags |= yuby::LoopFlag::SynchronizedStart;
        if (forceDefaultQuantum) flags |= yuby::LoopFlag::ForceDefaultQuantum;
        if (preferWaitingforSynchronisedStart) flags |= yuby::LoopFlag::PreferWaitingForSynchronizedStart;
        yuby::Set(obj, yuby::Loop, startMs, endMs - startMs, pointID, numLoops, flags);
    }
    void exitLoop(bool synchronisedStart = false) { yuby::Set(obj, yuby::ExitLoop, synchronisedStart ? 1.0 : 0.0); }
    bool isLooping() { return yuby::Get(obj, yuby::IsLooping) != 0.0; }
    bool msInLoop(double ms) { return yuby::Get(obj, yuby::IsPositionInLoop, ms) != 0.0; }
    void setReverse(bool reverse, unsigned int slipMs = 0) { yuby::Set(obj, yuby::Reverse, reverse ? 1.0 : 0.0, slipMs); }
    bool isReverse() { return yuby::Get(obj, yuby::Reverse) != 0.0; }
    void pitchBend(float maxPercent, bool bendStretch, bool faster, unsigned int holdMs) {
        YubySetParameters(obj, { yuby::MaxPitchBendPercent, maxPercent }, { yuby::PitchBendTimeStretching, bendStretch ? 1.0 : 0.0 }, { yuby::PitchBendHoldMs, (double)holdMs });
        yuby::Set(obj, yuby::PitchBend, faster ? 1.0 : -1.0);
    }
    void endContinuousPitchBend() { yuby::Set(obj, yuby::PitchBend, 0); }
    double getBendOffsetMs() { return yuby::Get(obj, yuby::BendOffsetMs); }
    float getCurrentPitchBendPercent() { return (float)yuby::Get(obj, yuby::PitchBend); }
    void resetBendMsOffset() { yuby::Set(obj, yuby::BendOffsetMs, 0.0); if (isPlaying()) playSynchronized(); }
    void setBendOffsetMs(double ms) { yuby::Set(obj, yuby::BendOffsetMs, ms); }
    bool isPerformingSlip() { return yuby::Get(obj, yuby::IsSlip) != 0.0; }
    void jogTouchBegin(int ticksPerTurn, int mode, unsigned int scratchSlipMs = 0) {
        yuby::Set(obj, yuby::TicksPerTurn, ticksPerTurn);
        yuby::Set(obj, yuby::JogTouchBegin, mode, scratchSlipMs);
    }
    void jogTick(int value, bool bendStretch, float bendMaxPercent, unsigned int bendHoldMs, bool parameterModeIfNoJogTouchBegin) {
        YubySetParameters(obj, { yuby::MaxPitchBendPercent, bendMaxPercent }, { yuby::PitchBendTimeStretching, bendStretch ? 1.0 : 0.0 }, { yuby::PitchBendHoldMs, (double)bendHoldMs });
        yuby::Set(obj, yuby::JogTick, value, parameterModeIfNoJogTouchBegin ? 1 : 0);
    }
    void jogTouchEnd(float decelerate, bool synchronisedStart) { yuby::Set(obj, yuby::JogTouchEnd, decelerate, synchronisedStart ? 1.0 : 0.0); }
    void startScratch(unsigned int slipMs, bool stopImmediately) { yuby::Set(obj, yuby::StartScratch, stopImmediately ? 1 : 0, slipMs); }
    void scratch(double pitch, float smoothing) { yuby::Set(obj, yuby::ScratchPitch, pitch, smoothing); }
    void endScratch(bool returnToStateBeforeScratch) { yuby::Set(obj, yuby::EndScratch, returnToStateBeforeScratch ? 1 : 0); }
    bool isScratching() { return yuby::Get(obj, yuby::IsScratching) != 0.0; }
    double getJogParameter() { return yuby::Get(obj, yuby::JogParameter); }
};

const float AdvancedAudioPlayer::MaxPlaybackRate = 20;

}
#endif
