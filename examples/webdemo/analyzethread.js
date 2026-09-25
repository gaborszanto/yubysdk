import { Yuby } from "../../wasm/yuby.js";

async function analyze(e) {
    removeEventListener("message", analyze);
    // Create an independent Yuby instance, not using the shared memory of the main thread.
    const yuby = await Yuby.load('../../wasm/'); 

    // Create the Decoder and open the memory buffer.
    const decoder = yuby.CreateObject(Yuby.ObjectType.Decoder);
    await decoder.Open(e.data);
    const error = decoder.GetString(Yuby.StringParameter.ErrorMessage);
    if (error) { console.error(error); close(); return; }

    // Prepare the analyzer and some buffers.
    const durationSeconds = decoder.Get(Yuby.Parameter.DurationSeconds);
    const samplerate = decoder.Get(Yuby.Parameter.SamplerateHz);
    const framesPerPacket = decoder.Get(Yuby.Parameter.FramesPerPacket);
    const readStep = Math.floor(samplerate / framesPerPacket) * framesPerPacket;
    const intbuf = yuby.call.malloc(readStep * 4 + 16384);
    const floatbuf = yuby.call.malloc(readStep * 8 + 32768);
    const analyzer = yuby.CreateObject(Yuby.ObjectType.Analyzer);
    analyzer.Params([Yuby.Parameter.SamplerateHz, samplerate], [Yuby.Parameter.DurationSeconds, durationSeconds]);
    
    // Analyze.
    let processed = false;
    decoder.Set(Yuby.Parameter.PositionFrames, 0);
    while (true) {
        if (!yuby.call.Process(decoder.pointer, 0, 0, intbuf, 0, readStep, 1)) break;
        const f = decoder.Get(Yuby.Parameter.FramesCreated);
        if (f < 1) break; else processed = true;
        yuby.call.shortinttofloat(intbuf, floatbuf, f);
        if (!yuby.call.Process(analyzer.pointer, floatbuf, 0, 0, 0, f, 1)) { processed = false; break; }
    }

    // Show some results.
    if (processed) console.log("bpm: " + analyzer.Get(Yuby.Parameter.BeatsPerMinute), "loudness: " + analyzer.Get(Yuby.Parameter.LoudPartsAverageDecibel));
    else console.error("analyze error");
    close();
    // No cleanup or dealloc? close() shuts down this Worker along with everything in it.
}

addEventListener("message", analyze);