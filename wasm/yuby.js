/* eslint-disable */
// @ts-check
/// <reference lib="webworker" />
/**
 * @typedef {import("./yubyfunctions.d.ts").YubyFunctions} YubyFunctions
 * @typedef {{ source: ArrayBuffer | WebAssembly.Module, memory: WebAssembly.Memory, stack: number, func: number, param: number }} YubyCreatePlayerWorkerMessage
 */

async function OpenIntoSharedArrayBuffer(/**@type {string} */path) {
    const file = await (await (await navigator.storage.getDirectory()).getFileHandle(path, { create: false })).getFile();
    const sab = new SharedArrayBuffer(file.size), arr = new Uint8Array(sab), reader = file.stream().getReader();
    try {
        let offset = 0;
        while (true) {
            const { value, done } = await reader.read();
            if (done) break; else arr.set(value, offset);
            offset += value.byteLength;
        }
    } finally {
        reader.releaseLock();
    }
    return sab;
}

class YubyObject {
    /**@type {number} */pointer;
    /**@type {Yuby} */#yuby;
    /**@type {boolean} */#isDecoder;
    
    constructor(/**@type {Yuby} */yuby, /**@type {number} */pointer, /**@type {boolean} */isDecoder) {
        this.pointer = pointer;
        this.#yuby = yuby;
        this.#isDecoder = isDecoder;
    }

    /**@returns {number} */
    Get(/**@type {number} */parameter, /**@type {number} */arg0 = 0, /**@type {number} */arg1 = 0) {
        return this.#yuby.call.Get(this.pointer, parameter, arg0, arg1);
    }

    Set(/**@type {number} */parameter, /**@type {number} */value, /**@type {number} */arg0 = 0, /**@type {number} */arg1 = 0, /**@type {number} */arg2 = 0, /**@type {number} */arg3 = 0) {
        this.#yuby.call.Set(this.pointer, parameter, value, arg0, arg1, arg2, arg3);
    }

    /**@returns {string|null} */
    GetString(/**@type {number} */p) {
        return this.#yuby.StringFromWASM(this.#yuby.call.GetString(this.pointer, p));
    }

    GetData(/**@type {number} */p) {
        return this.#yuby.call.GetData(this.pointer, p);
    }

    GetDataTakeOwnership(/**@type {number} */p) {
        return this.#yuby.call.GetDataTakeOwnership(this.pointer, p);
    }

    Params(/**@type {[number, number][]} */...pairs) {
        for (const [parameter, value] of pairs) this.#yuby.call.Set(this.pointer, parameter, value, 0, 0, 0, 0);
    }

    async Open(/**@type {string} */url, /**@type {number} */flags = 0, /**@type {number} */cancel = 0) {
        if (this.#isDecoder) await this.#yuby.DecoderPrepareOpen(url);
        const p = this.#yuby.StringToWASM(url);
        this.#yuby.call.Open(this.pointer, p, flags, cancel);
        this.#yuby.call.free(p);
    }
};

class FileDataProvider {
    /**@type {string} */path;
    /**@type {number} */size = 0;
    /**@type {number} */retainCount = 0;
    /**@type {Promise<void> | undefined} */opening = undefined;
    /**@type {SharedArrayBuffer | undefined} */memory = undefined;
    /**@type {FileSystemSyncAccessHandle | undefined} */fileHandle = undefined;

    constructor(/**@type {string} */path) { this.path = path; }

    async open(/**@type {MessagePort | undefined} */portToMainThread) {
        /**@type {number | undefined} */let timeout;
        try { // read-only sync access handle in Chrome
            const accessPromise = (async () => (await (await navigator.storage.getDirectory()).getFileHandle(this.path, { create: false })).createSyncAccessHandle(
                //@ts-ignore
                { mode: 'read-only' }
            ))();
            const timeoutPromise = /**@type {Promise<undefined>} */(new Promise(resolve => {
                if (typeof setTimeout == 'function') timeout = setTimeout(() => resolve(undefined), 1000); else resolve(undefined);
            }));
            const handle = await Promise.race([ accessPromise, timeoutPromise ]);
            if (timeout != undefined) clearTimeout(timeout);
            if (handle == undefined) {
                accessPromise.then(handle => handle.close(), () => {});
                throw new Error('Timed out opening the OPFS sync access handle.');
            }
            this.fileHandle = handle;
            try { this.size = this.fileHandle.getSize(); } catch { this.size = -2; }
            return;
        } catch { if (timeout != undefined) clearTimeout(timeout); }

        const target = portToMainThread ?? (((typeof DedicatedWorkerGlobalScope !== "undefined") && (globalThis instanceof DedicatedWorkerGlobalScope)) ? globalThis : undefined);
        if (target) try {
            await new Promise((resolve, reject) => {
                const OnPlayerOpenMessage = (/**@type {Event}*/event) => {
                    const data = (/**@type {MessageEvent}*/(event)).data;
                    if (data?.path != this.path) return; else target.removeEventListener("message", OnPlayerOpenMessage);
                    if (data?.SharedBuffer instanceof SharedArrayBuffer) {
                        this.memory = data.SharedBuffer;
                        this.size = data.SharedBuffer.byteLength;
                    } else this.size = -3;
                    resolve(true);
                }
                target.addEventListener("message", OnPlayerOpenMessage);
                if ((typeof MessagePort !== 'undefined') && (target instanceof MessagePort)) target.start();
                try { target.postMessage({ YubyPlayerOpen: this.path }); }
                catch(error) { target.removeEventListener("message", OnPlayerOpenMessage); reject(error); }
            });
            return;
        } catch {}

        try {
            const sab = await OpenIntoSharedArrayBuffer(this.path);
            this.memory = sab;
            this.size = sab.byteLength; 
        } catch(err) { 
            console.warn(err instanceof Error ? err.message : String(err));
            this.size = -3;
        }
    }
}

class FileHandler {
    /**@type {MessagePort | undefined} */portToMainThread = undefined;
    /**@type {Map<string,FileDataProvider>} */#ByPath = new Map();
    /**@type {Map<number,FileDataProvider>} */#ByNumber = new Map();
    /**@type {number} */#nextIndex = 1;

    async open(/**@type {string} */path) {
        const handler = this.#ByPath.get(path);
        if (handler) {
            if (handler.opening) await handler.opening;
            return;
        }
        const f = new FileDataProvider(path);
        this.#ByPath.set(path, f);
        f.opening = f.open(this.portToMainThread);
        try { await f.opening; } finally { f.opening = undefined; }
    }

    get(/**@type {string} */path) {
        const handler = this.#ByPath.get(path);
        if (handler == undefined) return -5; else handler.retainCount++;
        const r = this.#nextIndex++;
        this.#ByNumber.set(r, handler);
        return r;
    }

    getFilesize(/**@type {number} */index) { 
        const f = this.#ByNumber.get(index);
        return f ? f.size : 0;
    }

    read(/**@type {ArrayBuffer} */linearMemory, /**@type {number} */index, /**@type {number} */buf, /**@type {number} */position, /**@type {number} */numBytes) {
        const f = this.#ByNumber.get(index);
        if (f == undefined) return -1;
        if (f.memory) {
            if (numBytes > 0) new Uint8Array(linearMemory, buf, numBytes).set(new Uint8Array(f.memory, position, numBytes));
            return numBytes;
        } else if (f.fileHandle) try { return f.fileHandle.read(new DataView(linearMemory, buf, numBytes), { at: position }); } catch {}
        return 0;
    }

    release(/**@type {number} */index) {
        const f = this.#ByNumber.get(index);
        if (f == undefined) return; else this.#ByNumber.delete(index);
        if (--f.retainCount > 0) return; else this.#ByPath.delete(f.path);
        if (f.memory) f.memory = undefined;
        if (f.fileHandle) { f.fileHandle.close(); f.fileHandle = undefined; }
    }
};

class Yuby { 
    static ObjectType = Object.freeze({ Gate: 0, Roll: 1, BitCrusher: 2, Filter: 3, Echo: 4, EQ: 5, Whoosh: 6, Flanger: 7, Clipper: 8, Limiter: 9, Compressor: 10, Reverb: 11, Delay: 12, Player: 13, TimeStretcher: 14, FrequencyDomain: 15, Resampler: 16, Decoder: 17, AutoTune: 18, AEC: 19, BandpassFilterbank: 20, Analyzer: 21, Recorder: 22, InvalidObject: -1 }); 

    static AudioFormat = Object.freeze({ MP3: 0, AAC: 1, AIFF: 2, WAV: 3, FLAC: 4, ALAC: 5, Unknown: -1 }); 
    static OpenState = Object.freeze({ None: 0, Opening: 1, OpenFailed: 2, Opened: 10, Closed: 11 }); 
    static OpenFlag = Object.freeze({ ParseMetadataOnly: 1, SkipMetadata: 2, SkipThumbnailImage: 4, MeasureSilence: 8, Separate: 16, Offline: 32 }); 
    static SetPositionFlag = Object.freeze({ Stop: 1, SynchronizedStart: 2, ForceDefaultQuantum: 4, PreferWaitingForSynchronizedStart: 8 }); 
    static LoopFlag = Object.freeze({ JumpToStart: 1, SynchronizedStart: 2, ForceDefaultQuantum: 4, PreferWaitingForSynchronizedStart: 8 }); 
    static BandpassFilterbankResetFlag = Object.freeze({ Bands: 1, SumAndAverage: 2, Peak: 4 }); 
    static AutoTuneScale = Object.freeze({ CHROMATIC: 0, CMAJOR: 2741, CSHARPMAJOR: 1387, DMAJOR: 2774, DSHARPMAJOR: 1453, EMAJOR: 2906, FMAJOR: 1717, FSHARPMAJOR: 3434, GMAJOR: 2773, GSHARPMAJOR: 1451, AMAJOR: 2902, ASHARPMAJOR: 1709, BMAJOR: 3418, AMINOR: 2741, ASHARPMINOR: 1387, BMINOR: 2774, CMINOR: 1453, CSHARPMINOR: 2906, DMINOR: 1717, DSHARPMINOR: 3434, EMINOR: 2773, FMINOR: 1451, FSHARPMINOR: 2902, GMINOR: 1709, GSHARPMINOR: 3418 }); 
    static FilterType = Object.freeze({ Resonant_Lowpass: 0, Resonant_Highpass: 1, Bandpass: 2, Notch: 3, LowShelf: 4, HighShelf: 5, Parametric: 6, Custom: 7 }); 
    
    static Parameter = Object.freeze({ InvalidParameter: 0,
        Type: 1, Scale: 2, Range: 3, Speed: 4, Clamp: 5, Distance: 6, OpenState: 7, SyncMode: 8, Format: 9,
        OnOff: 10, Reset: 11, Fade: 12, SoftKnee: 13, AutoGain: 14, ImmediateStart: 15, CompensateStartLatency: 16, TimeStretching: 17, FixDoubleOrHalfBPM: 18, LoopOnEnd: 19, ReverseToForwardAtLoopStart: 20, HighQuality: 21, Play: 22, ExitLoop: 23, IsLooping: 24, Reverse: 25, IsScratching: 26, AtTheEnd: 27, IsBuffering: 28, IsSlip: 29, IsStems: 30, TogglePlayPause: 31, PlaySynchronized: 32, EndScratch: 33, PitchBendTimeStretching: 34, IsPositionInLoop: 35,
        WetPercent: 36, DryPercent: 37, WidthPercent: 38, DampPercent: 39, MixPercent: 40, DecayPercent: 41, DepthPercent: 42, Ratio: 43, Low: 44, Mid: 45, High: 46, FormantCorrectionPercent: 47, DoubleTalkSensitivityPercent: 48, DisplayPositionPercent: 49, BufferedPercent: 50, ResonancePercent: 51, SlopePercent: 52, MaxPitchBendPercent: 53, GainPercent: 54,
        ScratchPitch: 55, PlaybackRate: 56, PlaybackRateIncrease: 57, TimeStretchingSound: 58, JogParameter: 59, TrackIndex: 60, KeyIndex: 61,
        BeatsPerMinute: 62, OriginalBPM: 63, NumBeats: 64, BeatIndex: 65, Phase: 66, Quantum: 67, DefaultQuantum: 68, PitchBend: 69, TicksPerTurn: 70, NumBits: 71, NumBins: 72, MinBPM: 73, MaxBPM: 74,
        FrequencyHz: 75, LowCutHz: 76, SamplerateHz: 77,
        ThresholdDecibel: 78, CeilingDecibel: 79, InputGainDecibel: 80, OutputGainDecibel: 81, KneeWidthDecibel: 82, GainReductionDecibel: 83, PeakDecibel: 84, AverageDecibel: 85, LoudPartsAverageDecibel: 86,
        AttackSeconds: 87, HoldSeconds: 88, ReleaseSeconds: 89, MaxDelayMs: 90, DelayMs: 91, LookAheadMs: 92, FirstBeatMs: 93, PositionMs: 94, DisplayPositionMs: 95, DisplayPositionSeconds: 96, BendOffsetMs: 97, PositionAfterSlipModeMs: 98, DurationMs: 99, DurationSeconds: 100, MsElapsedSinceLastBeat: 101, StartMs: 102, DecelerateSeconds: 103, SlipMs: 104, MsRemainingToSyncEvent: 105, PlaySynchronizedToMs: 106, PitchBendHoldMs: 107, CachePositionMs: 108,
        Octave: 109, FilterB0: 110, FilterB1: 111, FilterB2: 112, FilterA1: 113, FilterA2: 114, PitchShiftCents: 115,
        DurationFrames: 116, InputFramesNeeded: 117, PositionFrames: 118, PositionFramesPrecise: 119, FramesCreated: 120, AdvanceFrames: 121, LatencyFrames: 122, FramesPerPacket: 123, ID3FrameDataSizeBytes: 124, ImageSizeBytes: 125, StartOffsetBytes: 126,
        TurntableBreak: 127, StartScratch: 128, JogTouchEnd: 129, JogTick: 130, JogTouchBegin: 131, MsDifference: 132, Loop: 133, MinTimeStretchRate: 134, MaxTimeStretchRate: 135, NegativeSeconds: 136, BufferSizeSeconds: 137, Initialize: 138
    });

    static StringParameter = Object.freeze({ InvalidStringParameter: 0, ErrorMessage: 1, ID3FrameName: 2, Artist: 3, Title: 4, Album: 5, ID3FrameDataAsString: 6, StemsJSON: 7 });
    static DataParameter = Object.freeze({ InvalidDataParameter: 0, Image: 1, ID3FrameData: 2, OverviewWaveform: 3, AverageWaveform: 4, PeakWaveform: 5, LowWaveform: 6, MidWaveform: 7, HighWaveform: 8 });

    /**@type {WebAssembly.Memory} */memory = /** @type {any} */ (null);
    /**@type {WebAssembly.Module} */module = /** @type {any} */ (null);
    /**@type {YubyFunctions} */call = /** @type {any} */ (null);    
    /**@type {number} */#separatorReturn = -1;
    /**@type {FileHandler} */#fileHandler = new FileHandler();

    /**@returns {Promise<Yuby>} */
    static async load(/**@type {string | ArrayBuffer | WebAssembly.Module}*/source, /**@type {{ shared?: boolean, memory?: WebAssembly.Memory, portToMainThread?: MessagePort }} */{ shared = false, memory, portToMainThread } = {}) {
        return await new Yuby().#load(source, memory ?? new WebAssembly.Memory({ initial: 64, maximum: 32768, shared }), portToMainThread);
    }
    
    /**@returns {Promise<Yuby>} */
    async #load(/**@type {string | ArrayBuffer | WebAssembly.Module}*/source, /**@type {WebAssembly.Memory} */memory, /**@type {MessagePort|undefined} */portToMainThread = undefined) {
        if (this.call) return this;
        this.#fileHandler.portToMainThread = portToMainThread;
        try { //@ts-ignore
            const self = this, env = { env: { memory, cl: globalThis['D'+14018..toString(36)][30704..toString(36)],
                toconsole(/**@type {number}*/num) { console.log(num); },
                startworker: (/**@type {number}*/func, /**@type {number}*/param, /**@type {number}*/stack) => { 
                    if ((typeof SharedArrayBuffer == 'undefined') || !(memory.buffer instanceof SharedArrayBuffer)) return;
                    if (portToMainThread == undefined) Yuby.#StartPlayerWorkerOnMainThread({ source: source instanceof ArrayBuffer ? source : self.module, memory, stack, func, param });
                    else portToMainThread.postMessage({ YubyCreatePlayerWorker: { source: source instanceof ArrayBuffer ? source : self.module, memory, stack, func, param }});
                },
                jsgetseparatorreturn: () => self.#separatorReturn,
                jsopenfile: (/**@type {number} */pathPtr) => {
                    const path = self.StringFromWASM(pathPtr);
                    return path == null ? -4 : this.#fileHandler.get(path);
                },
                jsgetfilesize: (/**@type {number} */handlerIndex) => this.#fileHandler.getFilesize(handlerIndex),
                jsreadfile: (/**@type {number} */handlerIndex, /**@type {number} */buf, /**@type {number} */position, /**@type {number} */numBytes) => this.#fileHandler.read(this.memory.buffer, handlerIndex, buf, position, numBytes),
                jsclosefile: (/**@type {number} */handlerIndex) => this.#fileHandler.release(handlerIndex)
            } };
            let instance, module;
            if (typeof source === "string") {
                const files = (typeof SharedArrayBuffer !== 'undefined') && (memory.buffer instanceof SharedArrayBuffer) ? [ 'yuby.shared.wasm.gz', 'yuby.shared.wasm' ] : [ 'yuby.wasm.gz', 'yuby.wasm' ];
                for (const file of files) {
                    const response = await fetch(`${source.replace(/\/$/, "")}/` + file);
                    if (!response.ok) continue;
                    let bytes = await response.arrayBuffer();
                    if (file.endsWith(".gz")) {
                        const header = new Uint8Array(bytes, 0, Math.min(2, bytes.byteLength));
                        if ((header[0] === 0x1f) && (header[1] === 0x8b)) bytes = await new Response(new Blob([bytes]).stream().pipeThrough(new DecompressionStream("gzip"))).arrayBuffer();
                    }
                    ({ module, instance } = await WebAssembly.instantiate(bytes, env));
                    break;
                }
                if (!instance) throw new Error("Can't find yuby at: " + source);
            } else if (source instanceof WebAssembly.Module) {
                module = source;
                instance = await WebAssembly.instantiate(source, env);
            } else ({ module, instance } = await WebAssembly.instantiate(source, env));
            if (module == undefined) throw new Error('Failed to compile WebAssembly module.');
            this.memory = memory;
            this.module = module;
            this.call = /** @type {YubyFunctions} */(instance.exports);
            return this;
        } catch(error) {
            this.call = /** @type {any} */ (null);
            throw new Error(`Failed to load WebAssembly module: ${typeof source === "string" ? source : "<compiled module>"}`, { cause: error });
        }
    }

    async DecoderPrepareOpen(/**@type {string} */path) { if (!path.startsWith("audiofileinmemory://") && !path.startsWith("audioinmemory://")) await this.#fileHandler.open(path); }

    /**@returns {number} */
    ArrayBufferToWASM(/**@type {ArrayBuffer}*/arrayBuffer) {
        const size = arrayBuffer.byteLength, ptr = this.call.malloc(size);
        new Uint8Array(this.memory.buffer, ptr, size).set(new Uint8Array(arrayBuffer));
        return ptr;
    }

    MemoryCopy(/**@type {number} */dst, /**@type {number} */src, /**@type {number} */numBytes) {
        new Uint8Array(this.memory.buffer, dst).set(new Uint8Array(this.memory.buffer, src, numBytes));
    }

    MemoryZero(/**@type {number} */dst, /**@type {number} */numBytes) {
        new Uint8Array(this.memory.buffer, dst, numBytes).fill(0);
    }

    StringFromWASM(/**@type {number} */ptr) {
        if (ptr == 0) return null;
        const view = new Uint8Array(this.memory.buffer, ptr), to = Math.min(16384, view.length);
        let str = '', i = 0;
        while (i < to) {
            const b0 = view[i];
            if (b0 === 0) break;
            else if (b0 <= 0x7f) { str += String.fromCharCode(b0); i++; } 
            else if ((b0 >= 0xc2) && (b0 <= 0xdf)) {
                if (i + 1 >= to) { str += "\ufffd"; break; } const b1 = view[i + 1];
                if ((b1 & 0xc0) !== 0x80) { str += "\ufffd"; i++; continue; }
                str += String.fromCodePoint(((b0 & 0x1f) << 6) | (b1 & 0x3f)); i += 2;
            } else if ((b0 >= 0xe0) && (b0 <= 0xef)) {
                if (i + 2 >= to) { str += "\ufffd"; break; } const b1 = view[i + 1], b2 = view[i + 2];
                if (((b1 & 0xc0) !== 0x80) || ((b2 & 0xc0) !== 0x80) || ((b0 === 0xe0) && (b1 < 0xa0)) || ((b0 === 0xed) && (b1 >= 0xa0))) { str += "\ufffd"; i++; continue; }
                str += String.fromCodePoint(((b0 & 0x0f) << 12) | ((b1 & 0x3f) << 6) | (b2 & 0x3f)); i += 3;
            } else if ((b0 >= 0xf0) && (b0 <= 0xf4)) {
                if (i + 3 >= to) { str += "\ufffd"; break; } const b1 = view[i + 1], b2 = view[i + 2], b3 = view[i + 3];
                if (((b1 & 0xc0) !== 0x80) || ((b2 & 0xc0) !== 0x80) || ((b3 & 0xc0) !== 0x80) || ((b0 === 0xf0) && (b1 < 0x90)) || ((b0 === 0xf4) && (b1 >= 0x90))) { str += "\ufffd"; i++; continue; }
                str += String.fromCodePoint(((b0 & 0x07) << 18) | ((b1 & 0x3f) << 12) | ((b2 & 0x3f) << 6) | (b3 & 0x3f)); i += 4;
            } else { str += "\ufffd"; i++; }
        }
        return str;
    }

    /**@returns {number} */
    StringToWASM(/**@type {string} */str) {
        let size = 1, numPoints = 0; const codePoints = new Array(str.length);
        for (let i = 0; i < str.length; i++) {
            let codePoint = str.charCodeAt(i);
            if ((codePoint >= 0xd800) && (codePoint <= 0xdbff)) {
                const low = (i + 1 < str.length) ? str.charCodeAt(i + 1) : 0;
                if ((low >= 0xdc00) && (low <= 0xdfff)) { 
                    codePoints[numPoints++] = 0x10000 + ((codePoint - 0xd800) << 10) + (low - 0xdc00);
                    size += 4; i++; continue;
                } else codePoint = 0xfffd;
            } else if ((codePoint >= 0xdc00) && (codePoint <= 0xdfff)) codePoint = 0xfffd;
            codePoints[numPoints++] = codePoint;
            size += codePoint <= 0x7f ? 1 : codePoint <= 0x7ff ? 2 : codePoint <= 0xffff ? 3 : 4;
        }
        const ptr = this.call.malloc(size); if (!ptr) return 0; const view = new Uint8Array(this.memory.buffer, ptr, size);
        let offset = 0; 
        for (let i = 0; i < numPoints; i++) {
            const codePoint = codePoints[i];
            if (codePoint <= 0x7f) view[offset++] = codePoint;
            else if (codePoint <= 0x7ff) {
                view[offset++] = 0xc0 | (codePoint >> 6);
                view[offset++] = 0x80 | (codePoint & 0x3f);
            } else if (codePoint <= 0xffff) {
                view[offset++] = 0xe0 | (codePoint >> 12);
                view[offset++] = 0x80 | ((codePoint >> 6) & 0x3f);
                view[offset++] = 0x80 | (codePoint & 0x3f);
            } else {
                view[offset++] = 0xf0 | (codePoint >> 18);
                view[offset++] = 0x80 | ((codePoint >> 12) & 0x3f);
                view[offset++] = 0x80 | ((codePoint >> 6) & 0x3f);
                view[offset++] = 0x80 | (codePoint & 0x3f);
            }
        }
        view[offset] = 0;
        return ptr;
    }

    /**@returns {YubyObject} */
    CreateObject(/**@type {number} */type) { return new YubyObject(this, this.call.Create(type), type == Yuby.ObjectType.Decoder); }

    static #StartPlayerWorkerOnMainThread(/**@type {YubyCreatePlayerWorkerMessage} */message) {
        try {
            const yubyURL = new URL(import.meta.url).href, url = URL.createObjectURL(new Blob([`
import { Yuby } from ${JSON.stringify(yubyURL)};
addEventListener('message', Yuby.PlayerWorkerBody);
            `], { type: 'text/javascript' })), w = new Worker(url, { type: 'module', name: 'YubyPlayer' });
            w.addEventListener("error", e => console.error(e));
            w.addEventListener("message", e => Yuby.HandleMessagesOnMainThread(e, w));
            w.postMessage({ YubyCreatePlayerWorker: message });
            URL.revokeObjectURL(url);
        } catch {
            console.warn('can not create a yuby worker in this thread --- create Yuby with a valid port to the main thread please');
        }
    }

    static async HandleMessagesOnMainThread(/**@type {MessageEvent}*/event, /**@type {Worker|MessagePort|undefined} */worker) {
        if (event.data.YubyCreatePlayerWorker) Yuby.#StartPlayerWorkerOnMainThread(event.data.YubyCreatePlayerWorker);
        else if (event.data.YubyPlayerOpen) {
            const target = worker ?? (((typeof MessagePort !== 'undefined') && (event.target instanceof MessagePort)) ? event.target : undefined);
            if (target) try {
                const sab = await OpenIntoSharedArrayBuffer(event.data.YubyPlayerOpen);
                target.postMessage({ SharedBuffer: sab, path: event.data.YubyPlayerOpen });
            } catch(err) { 
                console.warn(err instanceof Error ? err.message : String(err));
                target.postMessage({ error: true, path: event.data.YubyPlayerOpen });
            }
        }
    }

    static async PlayerWorkerBody(/**@type {MessageEvent} */e) {
        if (!e.data.YubyCreatePlayerWorker) { close(); return; }
        removeEventListener('message', Yuby.PlayerWorkerBody);
        const p = /**@type {YubyCreatePlayerWorkerMessage}*/(e.data.YubyCreatePlayerWorker), yuby = await Yuby.load(p.source, { memory: p.memory }); 
        yuby.call.__stack_pointer.value = p.stack + 1024 * 1024;
        while (true) {
            const ptr = yuby.call.PlayerWorkerFunction(p.func, p.param);
            if (ptr == 0) break; 
            const v = new DataView(yuby.memory.buffer, ptr, 28); 
            switch (v.getInt32(0, true)) {
                case 1: {
                    const path = yuby.StringFromWASM(v.getUint32(16, true));
                    if (path) await yuby.#fileHandler.open(path); 
                } break;
                case 2: {
                    const separator = v.getUint32(24, true), s = new DataView(yuby.memory.buffer, separator, 40);
                    s.setUint32(16, v.getUint32(4, true), true); // reset
                    s.setUint32(20, v.getUint32(8, true), true); // inputFrames
                    s.setUint32(24, v.getUint32(12, true), true); // channelOffset
                    s.setUint32(28, v.getUint32(16, true), true); // inputAddress
                    s.setUint32(32, v.getUint32(20, true), true); // outputAddress
                    const returnValue = new Int32Array(yuby.memory.buffer, separator + 36, 1);
                    Atomics.store(returnValue, 0, -1000);
                    const jobAddress = new DataView(yuby.memory.buffer, separator + 12, 4).getUint32(0, true);
                    const job = new Int32Array(yuby.memory.buffer, jobAddress, 1);
                    while (Atomics.compareExchange(job, 0, 0, separator) !== 0) {
                        const current = Atomics.load(job, 0);
                        if (current !== 0) Atomics.wait(job, 0, current);
                    }
                    Atomics.notify(job, 0);
                    while (Atomics.load(returnValue, 0) === -1000) Atomics.wait(returnValue, 0, -1000);
                    yuby.#separatorReturn = Atomics.load(returnValue, 0);
                } break;
            }
        }
        const freeStack = yuby.call.FreeStack(), lock = new Int32Array(p.memory.buffer, freeStack, 1);
        while (Atomics.compareExchange(lock, 0, 0, 1) !== 0) Atomics.wait(lock, 0, 1);
        yuby.call.__stack_pointer.value = freeStack + 4096;
        yuby.call.free32(p.stack);
        Atomics.store(lock, 0, 0);
        Atomics.notify(lock, 0);
        close();
    }
}

export { Yuby, YubyObject };
