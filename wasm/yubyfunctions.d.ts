export type YubyFunctions = {
    nop: () => void;
    malloc: (numBytes:number) => number;
    free: (p:number) => void;
    malloc32: (numBytes:number) => number;
    free32: (p:number) => void;
    YubyInit: (licenseKey:number) => void;
    Create: (type:number) => number;
    Delete: (obj:number) => void;
    Get: (yubyObject:number, p:number, arg0:number, arg1:number) => number;
    GetString: (yubyObject:number, p:number) => number;
    GetStringTakeOwnership: (yubyObject:number, p:number) => number;
    GetData: (yubyObject:number, p:number) => number;
    GetDataTakeOwnership: (yubyObject:number, p:number) => number;
    Set: (yubyObject:number, p:number, value:number, arg0:number, arg1:number, arg2:number, arg3:number) => boolean;
    SetColorFx: (yubyObject:number, yubyFxObject:number) => void;
    Process: (yubyObject:number, input:number, sidechain:number, output:number, samplerateHz:number, numFrames:number, numStereoChannels:number) => boolean;
    Open: (yubyObject:number, url:number, flags:number, cancel:number) => void;
    add: (inputA:number, inputB:number, output:number, numValues:number) => void;
    addfour: (inputA:number, inputB:number, inputC:number, inputD:number, output:number, numValues:number) => void;
    peak: (input:number, numValues:number) => number;
    balance: (input:number, output:number, leftGainStart:number, leftGainEnd:number, rightGainStart:number, rightGainEnd:number, numFrames:number, getPeaks:number) => void;
    mul: (input:number, output:number, startMultiplier:number, endMultiplier:number, numFrames:number) => number;
    mulinc: (input:number, output:number, multiplier:number, increase:number, numFrames:number) => number;
    multiply: (input:number, output:number, numValues:number, multiplier:number) => void;
    muladd: (inputA:number, inputB:number, output:number, startMultiplier:number, endMultiplier:number, numFrames:number) => number;
    muladdinc: (inputA:number, inputB:number, output:number, multiplier:number, increase:number, numFrames:number) => number;
    cross: (inputA:number, inputB:number, output:number, startMultiplierA:number, endMultiplierA:number, startMultiplierB:number, endMultiplierB:number, numFrames:number) => void;
    interleave: (left:number, right:number, output:number, numFrames:number) => void;
    interleaveadd: (left:number, right:number, input:number, output:number, numFrames:number) => void;
    deinterleave: (input:number, left:number, right:number, numFrames:number, multiplier:number) => void;
    stereotomono: (input:number, output:number, leftGainStart:number, leftGainEnd:number, rightGainStart:number, rightGainEnd:number, numFrames:number) => void;
    stereotomidside: (input:number, output:number, numFrames:number) => void;
    midsidetostereo: (input:number, output:number, numFrames:number) => void;
    shortinttofloat: (input:number, output:number, numFrames:number) => void;
    floattoshortint: (input:number, output:number, numFrames:number) => void;
    fftComplex: (real:number, imag:number, logSize:number, forward:number) => void;
    fftReal: (real:number, imag:number, logSize:number, forward:number) => void;
    fftPolar: (mag:number, phase:number, logSize:number, forward:number, valueOfPi:number) => void;
    CreatePlayerWithStemSeparator: (separator0:number, separator1:number) => number;
    AddToTracklist: (recorder:number, artist:number, title:number, becameAudibleSeconds:number, takeOwnership:number) => void;
    GetAllocatedMemory: () => number;
    GetLinearMemorySize: () => number;
    PlayerWorkerFunction: (func:number, param:number) => number;
    FreeStack: () => number;
    __stack_pointer: WebAssembly.Global;
};
declare global {
    class AudioWorkletProcessor {
        readonly port: MessagePort;
    }
    function registerProcessor(name: string, processor: new (...args: any[]) => AudioWorkletProcessor): void;
}
export {};
