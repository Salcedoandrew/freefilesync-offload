// *****************************************************************************
// * Offload window - entry point.                                             *
// *                                                                           *
// *   offload                          the app                                 *
// *   offload --render out.png         render the window offscreen (screenshots)*
// *   offload --render summary         render a finished run's confirmation    *
// *   offload --self-test SRC DST...   drive the engine through the same Swift *
// *                                    bridge the window uses, no GUI          *
// *****************************************************************************

import AppKit
import SwiftUI

//--- offscreen render --------------------------------------------------------

@MainActor
func renderWindow(to path: String, empty: Bool = false, tooSmall: Bool = false,
                  summary: Bool = false,
                  width: CGFloat = 900, height: CGFloat = 566) -> Int32
{
    //One offscreen render per window: the main one, or the confirmation a finished
    //run leaves behind. The sheet is sized by its own content, so it needs no frame.
    let content: AnyView
    if summary
    {
        content = AnyView(RunSummaryView(summary: Demo.summary) {})
    }
    else
    {
        let model = empty ? AppModel(startClock: false) : Demo.model(tooSmall: tooSmall)
        content = AnyView(RootView(model: model).frame(width: width, height: height))
    }

    let view = content
        .environment(\.colorScheme, .dark)
        .environment(\.snapshotRendering, true)

    let renderer = ImageRenderer(content: view)
    renderer.scale = 2
    renderer.isOpaque = true

    guard let cgImage = renderer.cgImage else
    {
        FileHandle.standardError.write("render failed: no image\n".data(using: .utf8)!)
        return 1
    }
    let rep = NSBitmapImageRep(cgImage: cgImage)
    guard let png = rep.representation(using: .png, properties: [:]) else
    {
        FileHandle.standardError.write("render failed: no PNG\n".data(using: .utf8)!)
        return 1
    }
    do
    {
        try png.write(to: URL(fileURLWithPath: path))
        print("wrote \(path) (\(cgImage.width)x\(cgImage.height))")
        return 0
    }
    catch
    {
        FileHandle.standardError.write("render failed: \(error)\n".data(using: .utf8)!)
        return 1
    }
}

//--- fit check boundaries ----------------------------------------------------

func runFitTest() -> Int32
{
    var checks = 0
    var failures = 0

    func expect(_ got: FitVerdict, _ want: FitVerdict, _ what: String)
    {
        checks += 1
        guard got != want else { return }
        failures += 1
        print("FAIL \(what): got \(got), want \(want)")
    }

    func expectBlocks(_ verdict: FitVerdict, _ want: Bool, _ what: String)
    {
        checks += 1
        guard verdict.blocksTransfer != want else { return }
        failures += 1
        print("FAIL \(what): blocksTransfer is \(verdict.blocksTransfer), want \(want)")
    }

    let gb: UInt64 = 1_000_000_000
    expect(FitCheck.verdict(required: 100 * gb, free: 200 * gb), .fits, "plenty of room")
    expect(FitCheck.verdict(required: 100 * gb, free: 100 * gb), .fits, "exactly fits")
    expect(FitCheck.verdict(required: 100 * gb, free: 100 * gb - 1), .short(by: 1), "one byte short")
    expect(FitCheck.verdict(required: 300 * gb, free: 100 * gb), .short(by: 200 * gb), "shortfall arithmetic")
    expect(FitCheck.verdict(required: 0, free: 1), .fits, "empty source")
    expect(FitCheck.verdict(required: nil, free: 200 * gb), .unknown, "unmeasured source")
    expect(FitCheck.verdict(required: 100 * gb, free: 0), .unknown, "volume reports nothing")

    //blocking policy: an unmeasured size is not a reason to refuse, a known shortfall is
    expectBlocks(.short(by: 1), true, "a shortfall must block")
    expectBlocks(.unavailable, true, "an ejected drive must block")
    expectBlocks(.fits, false, "a fitting drive must not block")
    expectBlocks(.unknown, false, "an unknown size must not block")

    print("\(checks - failures)/\(checks) fit checks passed")
    return failures == 0 ? 0 : 1
}

//--- the window ---------------------------------------------------------------

@MainActor
func makeMainWindow(model: AppModel) -> NSWindow
{
    //the updater refuses to run while a copy is in flight. The model lives as long as
    //the window does, and the app quits with its last window, so a strong capture here
    //cannot outlive the thing it points at.
    Updater.shared.hasActiveWork = { model.hasActiveTransfers }

    let window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 900, height: 566),
                          styleMask: [.titled, .closable, .miniaturizable, .resizable,
                                      .fullSizeContentView],
                          backing: .buffered, defer: false)
    window.title = "Offload"
    window.titleVisibility = .hidden
    window.titlebarAppearsTransparent = true
    window.isMovableByWindowBackground = true
    window.minSize = NSSize(width: 760, height: 440)
    window.appearance = NSAppearance(named: .darkAqua)
    window.backgroundColor = NSColor(Theme.windowBG)
    window.contentView = NSHostingView(rootView: RootView(model: model))
    window.center()
    return window
}

//--- menu ---------------------------------------------------------------------

///Offload had no menu bar at all, so Cmd-Q did nothing and there was nowhere to put
///"Check for Updates". This is the standard application menu, and nothing more.
@MainActor
func installMainMenu(delegate: AppDelegate)
{
    let main = NSMenu()

    let appItem = NSMenuItem()
    main.addItem(appItem)
    let appMenu = NSMenu(title: "Offload")

    let about = NSMenuItem(title: "About Offload",
                           action: #selector(AppDelegate.showAbout(_:)),
                           keyEquivalent: "")
    about.target = delegate
    appMenu.addItem(about)
    appMenu.addItem(.separator())

    let check = NSMenuItem(title: "Check for Updates…",
                           action: #selector(AppDelegate.checkForUpdates(_:)),
                           keyEquivalent: "")
    check.target = delegate
    appMenu.addItem(check)
    appMenu.addItem(.separator())

    appMenu.addItem(withTitle: "Hide Offload", action: #selector(NSApplication.hide(_:)), keyEquivalent: "h")
    let quit = NSMenuItem(title: "Quit Offload", action: #selector(NSApplication.terminate(_:)), keyEquivalent: "q")
    quit.target = NSApp
    appMenu.addItem(quit)

    appItem.submenu = appMenu
    NSApp.mainMenu = main
}

//--- mouse-driven UI test (serving half) -------------------------------------

struct UITestFixture
{
    let root: String
    let card: String
    let destinations: [String]
}

@MainActor
func makeUITestFixture(fileCount: Int = 8, fileBytes: Int = 6_000_000) -> UITestFixture?
{
    let fm = FileManager.default
    let root = NSTemporaryDirectory() + "offload-uitest-\(Int(Date().timeIntervalSince1970))"
    let card = root + "/card"
    let destinations = [root + "/dest-a", root + "/dest-b"]

    do
    {
        try fm.createDirectory(atPath: card + "/DCIM", withIntermediateDirectories: true)
        for d in destinations { try fm.createDirectory(atPath: d, withIntermediateDirectories: true) }
        for i in 0..<fileCount
        {
            var bytes = [UInt8](repeating: 0, count: fileBytes)
            for j in bytes.indices { bytes[j] = UInt8.random(in: 0...255) }
            //A real CR3 header — box size, "ftyp", brand "crx " — in front of the
            //random payload. The engine judges file heads during the scan, so a
            //file named .CR3 whose head is noise is a damaged file: it still copies,
            //but it can no longer be counted as verified, and this fixture exists to
            //exercise the transfer path, not the damaged-file path.
            let cr3Header: [UInt8] = [0x00, 0x00, 0x00, 0x18, 0x66, 0x74, 0x79, 0x70,
                                      0x63, 0x72, 0x78, 0x20]
            for (k, b) in cr3Header.enumerated() where k < bytes.count { bytes[k] = b }
            try Data(bytes).write(to: URL(fileURLWithPath: "\(card)/DCIM/IMG_\(1000 + i).CR3"))
        }
    }
    catch
    {
        print("ui-test: cannot build the fixture: \(error)")
        return nil
    }
    return UITestFixture(root: root, card: card, destinations: destinations)
}

///Selector-based timer: it fires on the main run loop, so the window and the model
///are only ever touched from the thread that owns them.
final class StateTicker: NSObject
{
    private let model: AppModel
    private let window: NSWindow
    private let statePath: String
    private var ticks = 0
    private var timer: Timer?

    init(model: AppModel, window: NSWindow, statePath: String)
    {
        self.model = model
        self.window = window
        self.statePath = statePath
    }

    func start()
    {
        let timer = Timer(timeInterval: 0.15, target: self,
                          selector: #selector(tick), userInfo: nil, repeats: true)
        RunLoop.main.add(timer, forMode: .common)
        self.timer = timer
    }

    @objc private func tick()
    {
        MainActor.assumeIsolated
        {
            UIStateWriter.write(model: model, window: window, to: statePath)
            ticks += 1
            if ticks % 13 == 0 { _ = UIStateWriter.capture(window, to: statePath + ".png") }
        }
    }
}

///Held so the ticker is not deallocated while the app runs.
@MainActor var uiTicker: StateTicker?

///Held so the menu's targets outlive the call that builds it.
@MainActor var menuDelegate: AppDelegate?

///Opens the real window on a fixture and publishes its click targets and state.
///The driver (`clickdriver`) reads that file, posts genuine CGEvents and checks
///what changed. Runs until the driver kills it.
@MainActor
func runUIServe(statePath: String) -> Int32
{
    guard let fixture = makeUITestFixture() else { return 1 }

    //The summary is still built (the driver reads it from the state file), but it
    //is not presented: a sheet would cover the buttons the driver clicks.
    AppModel.suppressesSummarySheet = true
    let model = AppModel()
    let window = makeMainWindow(model: model)
    NSApp.setActivationPolicy(.regular)
    let delegate = AppDelegate()
    menuDelegate = delegate
    installMainMenu(delegate: delegate)
    NSApp.finishLaunching()
    window.makeKeyAndOrderFront(nil)
    NSRunningApplication.current.activate(options: [.activateIgnoringOtherApps])
    window.makeKey()

    model.addSource(url: URL(fileURLWithPath: fixture.card))
    for destination in fixture.destinations
    {
        model.addDestination(url: URL(fileURLWithPath: destination))
    }

    let ticker = StateTicker(model: model, window: window, statePath: statePath)
    ticker.start()
    uiTicker = ticker //keep it alive for the life of the process

    print("ui-serve: window \(Int(window.frame.width))x\(Int(window.frame.height)) at \(window.frame.origin)")
    print("ui-serve: state \(statePath)")
    print("ui-serve: pid \(ProcessInfo.processInfo.processIdentifier)")
    fflush(stdout)

    NSApp.run()
    return 0
}

//--- headless self-test ------------------------------------------------------

func runSelfTest(source: String, destinations: [String], reportRoot: String) -> Int32
{
    let fm = FileManager.default
    var jobs: [(String, EngineJob, Transfer)] = []

    //Same measurement and fit rule the window uses, before a byte is written.
    var required: UInt64?
    switch SourceMeasure.measure(source)
    {
    case .success(let m):
        required = m.bytes
        print("source: \(Format.capacity(m.bytes)) in \(m.files) files")
    case .failure(let why):
        print("source: cannot measure (\(why.message)) — fit not checked")
    }
    for dest in destinations
    {
        //the engine creates the destination root, so measure the disk it lands on
        try? fm.createDirectory(atPath: dest, withIntermediateDirectories: true)
        let free = Volume.freeSpace(at: dest)
        let verdict = FitCheck.verdict(required: required, free: free)
        switch verdict
        {
        case .fits:
            print("fit: \(dest) holds it (\(Format.capacity(free)) free)")
        case .short(let by):
            print("fit: \(dest) is \(Format.capacity(by)) short — refusing before writing anything")
            return 1
        case .unknown:
            print("fit: \(dest) unknown — proceeding")
        case .unavailable:
            print("fit: \(dest) is not connected — refusing")
            return 1
        }
    }

    for (index, dest) in destinations.enumerated()
    {
        let reportDir = "\(reportRoot)/job\(index)"
        guard let job = EngineJob(source: source, destination: dest, reportDir: reportDir) else
        {
            print("FAIL: engine refused \(source) -> \(dest)")
            return 1
        }
        let srcDevice = Device(name: (source as NSString).lastPathComponent, path: source,
                               kind: .card, totalBytes: 0, freeBytes: 0)
        let dstDevice = Device(name: (dest as NSString).lastPathComponent, path: dest,
                               kind: .drive, totalBytes: 0, freeBytes: 0)
        let transfer = Transfer(source: srcDevice, destination: dstDevice,
                                subfolder: "", job: job)
        jobs.append((dest, job, transfer))
        print("started \(source) -> \(dest)")
    }

    var lastReport = Date.distantPast
    var pending = jobs.count
    while pending > 0
    {
        pending = 0
        for (_, _, transfer) in jobs
        {
            transfer.refresh()
            if !transfer.isFinished { pending += 1 }
        }

        if Date().timeIntervalSince(lastReport) >= 0.25
        {
            lastReport = Date()
            for (dest, _, t) in jobs
            {
                let pct = Int((t.fraction * 100).rounded())
                let state: String
                switch t.phase
                {
                case .done: state = "done"
                case .failed: state = "FAILED(\(t.failedCount))"
                case .cancelled: state = "cancelled"
                default: state = t.statusText
                }
                print(String(format: "  %-28@ %3d%% %llu/%llu files %@",
                             (dest as NSString).lastPathComponent as NSString,
                             pct, t.filesDone, t.filesTotal, state))
            }
        }
        if pending > 0 { Thread.sleep(forTimeInterval: 0.2) }
    }

    var ok = true
    print("")
    for (dest, _, t) in jobs
    {
        let name = (dest as NSString).lastPathComponent
        switch t.phase
        {
        case .done:
            print("  \(name): \(t.verifiedCount) verified of \(t.filesDone), report \(t.reportPath)")
        default:
            ok = false
            print("  \(name): \(t.phase) \(t.errorText)")
        }
    }
    print(ok ? "self-test PASS" : "self-test FAIL")
    _ = fm
    return ok ? 0 : 1
}

//--- app ---------------------------------------------------------------------

final class AppDelegate: NSObject, NSApplicationDelegate
{
    private var window: NSWindow?

    func applicationDidFinishLaunching(_ notification: Notification)
    {
        installMainMenu(delegate: self)
        let window = makeMainWindow(model: AppModel())
        window.makeKeyAndOrderFront(nil)
        NSApp.activate(ignoringOtherApps: true)
        self.window = window
    }

    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool { true }

    @MainActor @objc func checkForUpdates(_ sender: Any?)
    {
        Updater.shared.checkFromMenu()
    }

    ///The stock panel says the name and the version and nothing else, which is not
    ///much to show someone who just asked what this thing is. The credits carry who
    ///made it, the website and the email, and the sentence the README leads with;
    ///the copyright line underneath comes from NSHumanReadableCopyright in Info.plist.
    @MainActor @objc func showAbout(_ sender: Any?)
    {
        let centred = NSMutableParagraphStyle()
        centred.alignment = .center

        let credits = NSMutableAttributedString()

        credits.append(NSAttributedString(
            string: "Andrew Salcedo\n",
            attributes: [.font: NSFont.systemFont(ofSize: 12, weight: .semibold),
                         .foregroundColor: NSColor.labelColor,
                         .paragraphStyle: centred]))

        credits.append(NSAttributedString(
            string: "heartfeltmedia.co\n",
            attributes: [.font: NSFont.systemFont(ofSize: 11),
                         .foregroundColor: NSColor.linkColor,
                         .link: URL(string: "https://heartfeltmedia.co")!,
                         .paragraphStyle: centred]))

        credits.append(NSAttributedString(
            string: "gcastroandrew@gmail.com\n\n",
            attributes: [.font: NSFont.systemFont(ofSize: 11),
                         .foregroundColor: NSColor.linkColor,
                         .link: URL(string: "mailto:gcastroandrew@gmail.com")!,
                         .paragraphStyle: centred]))

        credits.append(NSAttributedString(
            string: "Verified media offload for camera cards: copies to every destination "
                  + "at once, hashes every byte, reads each copy back, and leaves a report "
                  + "you can check a year later.",
            attributes: [.font: NSFont.systemFont(ofSize: 11),
                         .foregroundColor: NSColor.secondaryLabelColor,
                         .paragraphStyle: centred]))

        NSApp.orderFrontStandardAboutPanel(options: [
            .applicationName: "Offload",
            .credits: credits,
        ])
        NSApp.activate(ignoringOtherApps: true)
    }
}

//--- dispatch ----------------------------------------------------------------

@main
struct OffloadMain
{
    @MainActor
    static func main()
    {
        let args = Array(CommandLine.arguments.dropFirst())

        if args.contains("--fit-test")
        {
            exit(runFitTest())
        }

        if args.contains("--update-test")
        {
            exit(UpdateSelfTest.run())
        }

        if args.contains("--check-update")
        {
            UpdateSelfTest.runCheckUpdate()
        }

        if let i = args.firstIndex(of: "--update-selftest"), i + 1 < args.count
        {
            UpdateSelfTest.runEndToEnd(feed: args[i + 1])
        }

        if let i = args.firstIndex(of: "--ui-serve"), i + 1 < args.count
        {
            exit(runUIServe(statePath: args[i + 1]))
        }

        if let i = args.firstIndex(of: "--render")
        {
            //`--render summary` (or `--render out.png --summary`) captures the
            //confirmation window instead of the main one.
            var wantsSummary = args.contains("--summary")
            var out = i + 1 < args.count ? args[i + 1] : "offload-window.png"
            //`--render --summary` puts the flag where the path goes
            if out.hasPrefix("--") { out = "offload-window.png" }
            if out == "summary"
            {
                wantsSummary = true
                out = "offload-summary.png"
            }
            exit(renderWindow(to: out,
                              empty: args.contains("--empty"),
                              tooSmall: args.contains("--fit"),
                              summary: wantsSummary))
        }

        if let i = args.firstIndex(of: "--self-test")
        {
            let rest = Array(args[(i + 1)...])
            guard rest.count >= 2 else
            {
                print("usage: offload --self-test SOURCE DEST [DEST...]")
                exit(2)
            }
            let reportRoot = NSTemporaryDirectory() + "offload-selftest-\(Int(Date().timeIntervalSince1970))"
            exit(runSelfTest(source: rest[0], destinations: Array(rest[1...]), reportRoot: reportRoot))
        }

        let app = NSApplication.shared
        let delegate = AppDelegate()
        app.delegate = delegate
        app.setActivationPolicy(.regular)
        app.run()
    }
}
