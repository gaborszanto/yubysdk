# Yuby SDK

The fastest, lowest-latency audio engine. Consistent audio across macOS, iOS, Windows, Android, Linux, and the web, with the same features, quality, and performance. Supports Intel 64-bit and ARM64 architectures.

## Folders

- [`android/`](android/) — Android library and headers.
- [`linux/`](linux/) — Linux static library and headers.
- [`wasm/`](wasm/) — JavaScript/WebAssembly library.
- [`windows/`](windows/) — Windows static library and headers.
- [`yuby.xcframework/`](yuby.xcframework/) — Framework for Apple platforms: macOS, iOS, iPadOS, tvOS, and visionOS.
- [`io/`](io/) — Open-source, cross-platform audio I/O library for Android, Windows, macOS, and iOS. Supports audio device listing and selection, channel mapping (routing), low-latency audio, and multiple sample formats. Supports both WASAPI and ASIO on Windows.

### Examples

- [`examples/androiddemo/`](examples/androiddemo/) — Android Studio project.
- [`examples/appledemo/`](examples/appledemo/) — Xcode project.
- [`examples/webdemo/`](examples/webdemo/) — Web browser demo written in JavaScript.
- [`examples/windemo/`](examples/windemo/) — Visual Studio project.

## Transitioning from Superpowered

Include `yubycompat.h` to replace Superpowered classes. It covers most Superpowered audio features. Yuby objects have lifecycles similar to those of Superpowered objects: create, manage, and destroy them as you did with Superpowered, using Yuby's syntax.

## API discovery

`yubyinfo.json` enables programmatic API discovery. In C++, you can use `yubyjson.h` to parse it.

## Running the web demo

1. Serve the entire Yuby repository on `localhost`.
2. Configure the server to send CORS and COEP headers.
3. Open your browser's developer console.
4. Open `http://localhost:PORT/examples/webdemo/index.html`, replacing `PORT` with your server's port number.
