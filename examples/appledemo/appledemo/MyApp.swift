import SwiftUI

#if os(macOS)
import AppKit

final class AppDelegate: NSObject, NSApplicationDelegate {
    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool {
        true
    }

    func applicationWillTerminate(_ notification: Notification) {
        demoEnd()
    }
}
#else
import UIKit

final class AppDelegate: NSObject, UIApplicationDelegate {
    func applicationWillTerminate(_ application: UIApplication) {
        demoEnd()
    }
}
#endif

@main struct MyApp: App {
#if os(macOS)
    @NSApplicationDelegateAdaptor(AppDelegate.self) private var appDelegate
#else
    @UIApplicationDelegateAdaptor(AppDelegate.self) private var appDelegate
#endif

    init() {
        guard let mp3URL = Bundle.main.url(forResource: "tropical-breeze", withExtension: "mp3") else {
            fatalError("tropical-breeze.mp3 is missing from the application bundle.")
        }
        mp3URL.withUnsafeFileSystemRepresentation { mp3Path in
            guard let mp3Path else { fatalError("Unable to resolve the bundled MP3 path.") }
            demoStart(mp3Path)
        }
    }

    var body: some Scene {
        WindowGroup {
            ContentView()
        }
    }
}
