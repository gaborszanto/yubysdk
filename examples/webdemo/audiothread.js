import { Yuby } from "../../wasm/yuby.js";

class DemoAudioProcessor extends AudioWorkletProcessor {
    yuby = undefined;
    microphone = false;

    constructor(options) {
        super();        
        this.initialize(options.processorOptions);
    }

    async initialize(processorOptions) {
        this.port.onmessage = (e) => { if (e.data.microphone != undefined) this.microphone = e.data.microphone; };
        this.samplerate = processorOptions.samplerate;
        this.player = processorOptions.player;
        this.reverb = processorOptions.reverb;
        this.microphone = processorOptions.microphone;
        this.yuby = await Yuby.load(processorOptions.wasm, { memory: processorOptions.memory });
        this.buffer = this.yuby.call.malloc(128 * 2 * 4);
    }

    process(inputs, outputs) {
        if (this.yuby == undefined) return true;
        let audioCreated = false;

        // if the microphone is enabled, copy and interleave the input into our buffer
        if (this.microphone && inputs[0]) {
            audioCreated = true;
            const buf = new Float32Array(this.yuby.memory.buffer, this.buffer, 128 * 2), left = inputs[0][0], right = inputs[0][Math.min(inputs[0].length - 1, 1)];
            for (let n = 0; n < 128; n++) {
                buf[n * 2 + 0] = left[n];
                buf[n * 2 + 1] = right[n];
            }
        }

        // the player outputs audio here:
        audioCreated |= this.yuby.call.Process(this.player, audioCreated ? this.buffer : 0, 0, this.buffer, this.samplerate, 128, 1);
        // the reverb processes the player's output, or if the player is paused, it may still output some "tail"
        audioCreated |= this.yuby.call.Process(this.reverb, audioCreated ? this.buffer : 0, 0, this.buffer, this.samplerate, 128, 1);

        // now set the output (deinterleave our buffer)
        if (audioCreated) {
            const buf = new Float32Array(this.yuby.memory.buffer, this.buffer, 128 * 2), left = outputs[0][0], right = outputs[0][1];
            for (let n = 0; n < 128; n++) {
                left[n] = buf[n * 2 + 0];
                right[n] = buf[n * 2 + 1];
            }
        } else {
            outputs[0][0].fill(0);
            outputs[0][1].fill(0);
        }
        return true;
    }
}

registerProcessor("demoprocessor", DemoAudioProcessor);
