import { Yuby } from "../../wasm/yuby.js";

const initButton = document.querySelector("#initialize");
const playButton = document.querySelector("#play");
const micButton = document.querySelector("#mic");
const reverbButton = document.querySelector("#reverb");

let yuby, player, reverb, portToAudioThread, microphone = false;

// Save a file in the webpage's private file system (OPFS).
async function saveFileInOPFS(path, data) {
    const fileHandle = await (await navigator.storage.getDirectory()).getFileHandle(path, { create: true });
    if ((await fileHandle.getFile()).size === data.byteLength) return;
    const deadline = performance.now() + 10000; // Try the save for up to 10 seconds.
    // Why multiple tries? A previous webpage session may still be closing by the browser engine, after the user closed the tab/window or hit refresh.
    while (true) { 
        let writable;
        try {
            writable = await fileHandle.createWritable();
            await writable.write(data);
            await writable.close();
            return;
        } catch (error) {
            try { await writable?.abort(); } catch {}
            if (!['NoModificationAllowedError', 'InvalidStateError'].includes(error?.name) || (performance.now() >= deadline)) throw error;
            await new Promise(resolve => setTimeout(resolve, 100));
        }
    }
}

async function initializeDemo() {
    initButton.removeEventListener("click", initializeDemo);
    initButton.disabled = true;
    if (!crossOriginIsolated) throw new Error("This demo requires cross-origin isolation (COOP and COEP headers) to enable SharedArrayBuffer.");

    // Initialize the Web Audio context first, because it must happen on user interaction.
    const audioContext = new AudioContext({ latencyHint: "interactive" });
    await audioContext.resume();
    const inputStream = await navigator.mediaDevices.getUserMedia({ audio: true, video: false });

    // Download and save the example audio file.
    const response = await fetch("../tropical-breeze.mp3");
    if (!response.ok) throw new Error(`Failed to download tropical-breeze.mp3: HTTP ${response.status}`);
    await saveFileInOPFS("tropical-breeze.mp3", await response.arrayBuffer());

    // Create the Yuby objects before starting the AudioWorklet.
    yuby = await Yuby.load("../../wasm/", { shared: true });
    // Mandatory step after yuby is created.
    yuby.call.YubyInit(0, 0, 0); // three zeros allow for time-limited development use --- visit yuby.com to get a license
    player = yuby.CreateObject(Yuby.ObjectType.Player);
    reverb = yuby.CreateObject(Yuby.ObjectType.Reverb);

    // Create the Web Audio AudioWorklet.
    await audioContext.audioWorklet.addModule("audiothread.js");
    const node = new AudioWorkletNode(audioContext, "demoprocessor", {
        numberOfInputs: 1, numberOfOutputs: 1, outputChannelCount: [2],
        channelCount: 2, channelCountMode: "explicit", channelInterpretation: "discrete",
        processorOptions: { memory: yuby.memory, player: player.pointer, reverb: reverb.pointer, wasm: yuby.module, microphone: microphone, samplerate: audioContext.sampleRate }
    });
    audioContext.createMediaStreamSource(inputStream).connect(node);
    node.connect(audioContext.destination);
    portToAudioThread = node.port;

    // Check the player's open status every 100 ms, because why not.
    setInterval(() => {
        switch (player.Get(Yuby.Parameter.OpenState)) {
            case Yuby.OpenState.Opened: console.log("yay opened"); break;
            case Yuby.OpenState.OpenFailed: console.log(player.GetString(Yuby.StringParameter.ErrorMessage) ?? "open error"); break;
        }
    }, 100);
    // Open the file from OPFS in the player. You can directly handle Yuby objects outside of the Audio Worklet now.
    // The old way of handling inside the Audio Worklet works too, but this new way removes the need to postMessage() the Audio Worklet.
    // If Yuby is created using a SharedArrayBuffer (like in this example), messaging to Yuby objects works in any Worker or Worklet.
    await player.Open("tropical-breeze.mp3");

    // Analyze the file in a Worker.
    const w = new Worker("analyzethread.js", { type: "module" });
    w.addEventListener("message", e => Yuby.HandleMessagesOnMainThread(e, w)); // Yuby on the Worker might need some stuff done on the main thread.
    w.postMessage("tropical-breeze.mp3");

    playButton.addEventListener("click", playClick);
    micButton.addEventListener("click", micClick);
    reverbButton.addEventListener("click", reverbClick);
    playButton.disabled = micButton.disabled = reverbButton.disabled = false;
}

async function playClick() {
    player.Set(Yuby.Parameter.TogglePlayPause, 0);
    document.querySelector("#play").innerText = player.Get(Yuby.Parameter.Play) ? "Pause" : "Play";
}

async function micClick() {
    microphone = !microphone;
    document.querySelector("#mic").innerText = microphone ? "Mic On" : "Mic Off";
    portToAudioThread.postMessage({ microphone: microphone });
}

async function reverbClick() {
    const wasOn = reverb.Get(Yuby.Parameter.OnOff) != 0;
    reverb.Set(Yuby.Parameter.OnOff, wasOn ? 0 : 1);
    document.querySelector("#reverb").innerText = wasOn ? "Reverb Off" : "Reverb On";
}

initButton.addEventListener("click", initializeDemo);
