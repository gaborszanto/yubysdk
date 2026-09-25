import SwiftUI

struct ContentView: View {
    @State private var playing = false
    @State private var mic = false
    @State private var reverbOn = false

    var body: some View {
        Button(playing ? "Pause" : "Play") { playing = togglePlayPause() }
            .padding()
        Button(mic ? "Mic On" : "Mic Off") { mic = toggleMicrophone() }
            .padding()
        Button(reverbOn ? "Reverb On" : "Reverb Off") { reverbOn = toggleReverb() }
            .padding()
    }
}

#Preview {
    ContentView()
}
