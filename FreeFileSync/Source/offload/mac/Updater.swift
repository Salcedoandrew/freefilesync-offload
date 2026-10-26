// *****************************************************************************
// * Offload window - check for updates, and replace the app in place.          *
// *                                                                           *
// * The shape follows tauri-plugin-updater (what alic uses): check the release *
// * feed, download the archive, install it, restart. macOS will not let a      *
// * running bundle overwrite itself, so the last step is a small shell script  *
// * that waits for this process to exit, deletes the old bundle, moves the new *
// * one in, and reopens it. If anything fails, the script puts the old bundle  *
// * back before reopening it, so a failed update never leaves no app at all.   *
// *                                                                           *
// * What this does NOT prove: that the download came from us. The build is     *
// * ad-hoc signed, so there is no Developer ID to check against. The SHA-256   *
// * against the release's own SHA256SUMS.txt proves the bytes arrived intact;  *
// * it does not prove who wrote them. A signed appcast or notarization is the  *
// * only fix for that, and both need an Apple Developer ID.                    *
// *****************************************************************************

import AppKit
import Foundation
import CryptoKit

//--- versions ----------------------------------------------------------------

///A release version: dot-separated numbers, with any leading "v" and any
///pre-release suffix ("0.4.0-mod") reduced to the numbers. Comparison is numeric
///per component, so 0.10.0 is newer than 0.9.0 — string comparison gets that wrong.
struct AppVersion: Equatable, Comparable, CustomStringConvertible
{
    let components: [Int]
    let raw: String

    init?(_ text: String)
    {
        var s = text.trimmingCharacters(in: .whitespacesAndNewlines)
        if s.hasPrefix("v") || s.hasPrefix("V") { s.removeFirst() }
        guard let numeric = s.split(whereSeparator: { $0 == "-" || $0 == "+" }).first, !numeric.isEmpty else { return nil }

        var parts: [Int] = []
        for piece in numeric.split(separator: ".")
        {
            //"4rc1" counts as 4: the leading digits are the number
            let digits = piece.prefix(while: { $0.isNumber })
            guard let n = Int(digits) else { return nil }
            parts.append(n)
        }
        guard !parts.isEmpty else { return nil }
        components = parts
        raw = s
    }

    var description: String { raw }

    ///Component-wise, so 1.0 and 1.0.0 are the same release, and a pre-release
    ///suffix ("0.4.0-mod") does not make a version different from its base.
    static func == (a: AppVersion, b: AppVersion) -> Bool
    {
        let count = max(a.components.count, b.components.count)
        for i in 0..<count
        {
            let l = i < a.components.count ? a.components[i] : 0
            let r = i < b.components.count ? b.components[i] : 0
            if l != r { return false }
        }
        return true
    }

    static func < (a: AppVersion, b: AppVersion) -> Bool
    {
        let count = max(a.components.count, b.components.count)
        for i in 0..<count
        {
            let l = i < a.components.count ? a.components[i] : 0
            let r = i < b.components.count ? b.components[i] : 0
            if l != r { return l < r }
        }
        return false
    }
}

//--- errors ------------------------------------------------------------------

enum UpdateError: LocalizedError
{
    case feedUnreadable(String)
    case releaseUnusable(String)
    case noZipAsset
    case downloadFailed(String)
    case checksumMismatch(expected: String, got: String)
    case checksumMissing
    case unpackFailed(String)
    case unexpectedBundle(String)
    case signatureRejected
    case versionMismatch(expected: String, got: String)
    case appNotReplaceable(String)
    case cancelled

    var errorDescription: String?
    {
        switch self
        {
        case .feedUnreadable(let why):
            return "Could not read the release feed: \(why)"
        case .releaseUnusable(let why):
            return "The latest release cannot be used: \(why)"
        case .noZipAsset:
            return "The latest release has no Offload zip attached."
        case .downloadFailed(let why):
            return "The download failed: \(why)"
        case .checksumMismatch(let expected, let got):
            return "The download does not match the published checksum (expected \(expected.prefix(12))…, got \(got.prefix(12))…). Nothing was replaced."
        case .checksumMissing:
            return "The release publishes no checksum for this download, so it could not be verified."
        case .unpackFailed(let why):
            return "The archive did not unpack: \(why)"
        case .unexpectedBundle(let why):
            return "The archive does not contain the app this bundle is: \(why)"
        case .signatureRejected:
            return "The downloaded app does not pass its own code signature check. Nothing was replaced."
        case .versionMismatch(let expected, let got):
            return "The download is version \(got), but the release says \(expected). Nothing was replaced."
        case .appNotReplaceable(let why):
            return "This copy of Offload cannot replace itself: \(why)"
        case .cancelled:
            return "The update was cancelled."
        }
    }
}

//--- the release feed --------------------------------------------------------

struct ReleaseAsset: Equatable
{
    let name: String
    let url: URL
    let bytes: Int64
}

struct ReleaseInfo: Equatable
{
    let version: AppVersion
    let tag: String
    let notes: String
    let pageURL: URL
    let zip: ReleaseAsset
    let sums: ReleaseAsset?
}

///What github.com/…/releases/latest returns, narrowed to the fields used here.
struct FeedRelease: Decodable
{
    struct Asset: Decodable
    {
        let name: String
        let browserDownloadURL: URL
        let size: Int64

        enum CodingKeys: String, CodingKey
        {
            case name
            case browserDownloadURL = "browser_download_url"
            case size
        }
    }

    let tagName: String
    let body: String?
    let htmlURL: URL
    let draft: Bool
    let prerelease: Bool
    let assets: [Asset]

    enum CodingKeys: String, CodingKey
    {
        case tagName = "tag_name"
        case body
        case htmlURL = "html_url"
        case draft
        case prerelease
        case assets
    }

    func releaseInfo() throws -> ReleaseInfo
    {
        guard !draft, !prerelease else { throw UpdateError.releaseUnusable("it is a draft or a pre-release") }
        guard let version = AppVersion(tagName) else { throw UpdateError.releaseUnusable("the tag \(tagName) is not a version") }

        let zips = assets.filter { $0.name.hasSuffix(".zip") && $0.name.hasPrefix("Offload-") }
        //prefer the archive named for this version and this architecture, then any Offload zip
        let wanted = "Offload-\(version.raw)-macos-arm64.zip"
        guard let zip = zips.first(where: { $0.name == wanted })
                      ?? zips.sorted(by: { $0.name > $1.name }).first
        else { throw UpdateError.noZipAsset }

        let sums = assets.first { $0.name.lowercased() == "sha256sums.txt" || $0.name.lowercased().hasSuffix(".sha256") }
        return ReleaseInfo(version: version,
                           tag: tagName,
                           notes: body ?? "",
                           pageURL: htmlURL,
                           zip: ReleaseAsset(name: zip.name, url: zip.browserDownloadURL, bytes: zip.size),
                           sums: sums.map { ReleaseAsset(name: $0.name, url: $0.browserDownloadURL, bytes: $0.size) })
    }
}

//--- checksums ---------------------------------------------------------------

enum Checksum
{
    ///Parses `shasum -a 256` output: "<hex>  <filename>", one per line.
    static func parse(_ text: String) -> [String: String]
    {
        var out: [String: String] = [:]
        for line in text.split(separator: "\n")
        {
            let fields = line.split(whereSeparator: { $0 == " " || $0 == "\t" }).map(String.init)
            guard fields.count >= 2 else { continue }
            let hex = fields[0].lowercased()
            guard hex.count == 64, hex.allSatisfy({ $0.isHexDigit }) else { continue }
            //some tools prefix the name with "*"
            var name = fields[1]
            if name.hasPrefix("*") { name.removeFirst() }
            out[(name as NSString).lastPathComponent.lowercased()] = hex
        }
        return out
    }

    ///Streams the file so a large archive is never held in memory twice.
    static func sha256(of url: URL) throws -> String
    {
        let handle = try FileHandle(forReadingFrom: url)
        defer { try? handle.close() }
        var hasher = SHA256()
        while autoreleasepool(invoking: {
            let chunk = try? handle.read(upToCount: 1 << 20)
            guard let chunk, !chunk.isEmpty else { return false }
            hasher.update(data: chunk)
            return true
        }) {}
        return hasher.finalize().map { String(format: "%02x", $0) }.joined()
    }
}

//--- the bundle on disk ------------------------------------------------------

struct AppBundle
{
    let path: URL
    let version: AppVersion
    let bundleID: String

    static func at(_ path: URL) -> AppBundle?
    {
        let plist = path.appendingPathComponent("Contents/Info.plist")
        guard let data = try? Data(contentsOf: plist),
              let any = try? PropertyListSerialization.propertyList(from: data, format: nil),
              let info = any as? [String: Any],
              let short = info["CFBundleShortVersionString"] as? String,
              let version = AppVersion(short),
              let id = info["CFBundleIdentifier"] as? String
        else { return nil }
        return AppBundle(path: path, version: version, bundleID: id)
    }

    static var current: AppBundle? { at(Bundle.main.bundleURL) }

    ///Gatekeeper runs a downloaded app from a read-only shadow copy; replacing that
    ///would "succeed" and vanish on the next launch, so updates are refused there.
    var isTranslocated: Bool { path.path.contains("/AppTranslocation/") }

    ///Can this user delete and recreate the bundle in its folder?
    var isReplaceable: Bool
    {
        let fm = FileManager.default
        let parent = path.deletingLastPathComponent()
        return !isTranslocated && fm.isWritableFile(atPath: parent.path) && fm.isWritableFile(atPath: path.path)
    }
}

//--- install -----------------------------------------------------------------

enum UpdateInstaller
{
    ///Unpacks the archive and returns the one app inside it.
    static func stage(zip: URL, into dir: URL) throws -> URL
    {
        try FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        let p = Process()
        p.executableURL = URL(fileURLWithPath: "/usr/bin/ditto")
        p.arguments = ["-x", "-k", zip.path, dir.path]
        var err = ""
        let pipe = Pipe()
        p.standardError = pipe
        try p.run()
        let data = pipe.fileHandleForReading.readDataToEndOfFile()
        err = String(decoding: data, as: UTF8.self)
        p.waitUntilExit()
        guard p.terminationStatus == 0 else {
            throw UpdateError.unpackFailed(err.isEmpty ? "ditto exited \(p.terminationStatus)" : err)
        }

        let contents = (try? FileManager.default.contentsOfDirectory(at: dir, includingPropertiesForKeys: nil)) ?? []
        let apps = contents.filter { $0.pathExtension == "app" }
        guard apps.count == 1, let app = apps.first else {
            throw UpdateError.unexpectedBundle("found \(apps.count) .app bundles")
        }
        return app
    }

    ///Everything the download must satisfy before the running app is touched.
    static func verify(staged: URL, expecting release: ReleaseInfo, current: AppBundle) throws
    {
        guard let new = AppBundle.at(staged) else {
            throw UpdateError.unexpectedBundle("no readable Info.plist")
        }
        guard new.bundleID == current.bundleID else {
            throw UpdateError.unexpectedBundle("bundle id \(new.bundleID), expected \(current.bundleID)")
        }
        guard new.version == release.version else {
            throw UpdateError.versionMismatch(expected: release.version.raw, got: new.version.raw)
        }

        let p = Process()
        p.executableURL = URL(fileURLWithPath: "/usr/bin/codesign")
        p.arguments = ["--verify", "--deep", "--strict", staged.path]
        p.standardError = Pipe()
        p.standardOutput = Pipe()
        try p.run()
        p.waitUntilExit()
        guard p.terminationStatus == 0 else { throw UpdateError.signatureRejected }
    }

    ///The checksum the release publishes for the zip it ships. An install has no
    ///business continuing without one: the app is about to replace itself, and a
    ///download that nobody can check is exactly what the checksum exists to catch.
    ///Kept out of performInstall so the rule has one owner and can be tested without
    ///a network or a real bundle.
    static func checksumAsset(for release: ReleaseInfo) throws -> ReleaseAsset
    {
        guard let sums = release.sums else { throw UpdateError.checksumMissing }
        return sums
    }

    ///The script that does what a running process cannot do to itself.
    ///`pid` is the app's; `0` skips the wait, which is how the tests drive it.
    static func swapScript() -> String
    {
        """
        #!/bin/sh
        # args: <pid> <installed app> <staged app> <relaunch 0|1>
        pid="$1"; app="$2"; staged="$3"; relaunch="$4"
        log="$(dirname "$0")/last-swap.log"
        note() { echo "$(date '+%Y-%m-%d %H:%M:%S') $*" >> "$log" 2>/dev/null; }
        note "start pid=$pid app=$app"

        if [ "$pid" != "0" ]; then
            i=0
            while [ $i -lt 120 ]; do
                kill -0 "$pid" 2>/dev/null || break
                sleep 0.25
                i=$((i + 1))
            done
        fi

        backup="$app.old-$$"
        if ! mv "$app" "$backup" 2>/dev/null; then
            note "failed: could not move the old bundle aside (permissions?)"
            rm -f "$0"
            [ "$relaunch" = "1" ] && open "$app"
            exit 2
        fi

        if mv "$staged" "$app" 2>/dev/null &&
           /usr/bin/codesign --verify --deep --strict "$app" >/dev/null 2>&1; then
            xattr -dr com.apple.quarantine "$app" 2>/dev/null
            rm -rf "$backup"
            rmdir "$(dirname "$staged")" 2>/dev/null
            note "ok: replaced, old bundle deleted"
            rm -f "$0"
            [ "$relaunch" = "1" ] && open "$app"
            exit 0
        fi

        # put the old bundle back: a failed update must not leave no app
        rm -rf "$app"
        rm -rf "$staged"
        mv "$backup" "$app" 2>/dev/null
        note "failed: new bundle rejected, old bundle restored"
        rm -f "$0"
        [ "$relaunch" = "1" ] && open "$app"
        exit 3
        """
    }

    ///Writes the script, starts it detached, and returns once it is running.
    static func spawnSwap(pid: Int32, installed: URL, staged: URL, relaunch: Bool) throws -> URL
    {
        let dir = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
            .appendingPathComponent("Offload", isDirectory: true)
            .appendingPathComponent("update", isDirectory: true)
        try FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)

        let script = dir.appendingPathComponent("swap-\(UUID().uuidString).sh")
        try swapScript().write(to: script, atomically: true, encoding: .utf8)
        try FileManager.default.setAttributes([.posixPermissions: 0o755], ofItemAtPath: script.path)

        let p = Process()
        p.executableURL = URL(fileURLWithPath: "/bin/sh")
        p.arguments = [script.path, String(pid), installed.path, staged.path, relaunch ? "1" : "0"]
        //detached from this process's lifetime: it has to outlive us
        p.qualityOfService = .userInitiated
        try p.run()
        return script
    }
}

//--- the updater -------------------------------------------------------------

@MainActor
final class Updater: ObservableObject
{
    enum Phase: Equatable
    {
        case idle
        case checking
        case upToDate(current: AppVersion, latest: AppVersion)
        case available(ReleaseInfo)
        case downloading(ReleaseInfo, fraction: Double)
        case installing(ReleaseInfo)
        case failed(String)
    }

    static let shared = Updater()

    ///Both are written once, before the run loop starts, from the command line.
    ///`nonisolated(unsafe)` says so instead of spreading MainActor around the tests.
    nonisolated(unsafe) static var feedOverride: URL?

    ///--update-selftest: swap the bundle, but do not reopen it afterwards.
    nonisolated(unsafe) static var isSelfTest = false

    @Published private(set) var phase: Phase = .idle

    ///The window answers this before quitting: an update must not kill a running copy.
    var hasActiveWork: () -> Bool = { false }

    private var downloadTask: Task<Void, Never>?
    private let session: URLSession = {
        let config = URLSessionConfiguration.ephemeral
        config.timeoutIntervalForRequest = 30
        config.httpAdditionalHeaders = ["Accept": "application/vnd.github+json"]
        return URLSession(configuration: config)
    }()

    ///The feed a check reads. GitHub hands back one release here, not a list, so the
    ///app never gets to pick by position - it has to decide on the version number.
    ///That is why this is `/releases/latest` and not `/releases`: the list is ordered
    ///by publish date, and a patch published late would outrank a newer version.
    nonisolated static var latestEndpoint: URL
    {
        URL(string: "https://api.github.com/repos/Salcedoandrew/freefilesync-offload/releases/latest")!
    }

    var feedURL: URL
    {
        if let override = Updater.feedOverride { return override }
        return Updater.latestEndpoint
    }

    var currentVersion: AppVersion?
    {
        AppBundle.current?.version
    }

    ///The comparison a check ends on, out on its own so the self-test can drive it with
    ///real release payloads: a copy one release behind is offered the feed's version, a
    ///current copy is told it is current, and a feed that lags behind the running copy
    ///never offers a downgrade.
    nonisolated static func decision(current: AppVersion, release: ReleaseInfo) -> Phase
    {
        release.version > current
            ? .available(release)
            : .upToDate(current: current, latest: release.version)
    }

    //-- checking ---------------------------------------------------------------

    func check() async
    {
        if case .checking = phase { return }
        phase = .checking
        await performCheck()
    }

    func performCheck() async
    {
        do
        {
            let (data, response) = try await session.data(from: feedURL)
            if let http = response as? HTTPURLResponse, http.statusCode != 200
            {
                throw UpdateError.feedUnreadable("HTTP \(http.statusCode)")
            }
            let feed = try JSONDecoder().decode(FeedRelease.self, from: data)
            let release = try feed.releaseInfo()
            guard let current = currentVersion else { throw UpdateError.feedUnreadable("this bundle has no version") }

            phase = Updater.decision(current: current, release: release)
        }
        catch let error as UpdateError
        {
            phase = .failed(error.localizedDescription)
        }
        catch
        {
            phase = .failed(UpdateError.feedUnreadable(error.localizedDescription).localizedDescription)
        }
    }

    //-- installing -------------------------------------------------------------

    func install(_ release: ReleaseInfo)
    {
        guard downloadTask == nil else { return }
        if hasActiveWork()
        {
            phase = .failed("There are transfers running. Finish or cancel them, then check again.")
            return
        }
        guard let current = AppBundle.current else
        {
            phase = .failed("Offload could not read its own bundle.")
            return
        }
        guard current.isReplaceable else
        {
            let why = current.isTranslocated
                ? "it is running from Gatekeeper's read-only copy. Move it to /Applications and open it there."
                : "the folder it lives in is not writable by this user."
            phase = .failed(UpdateError.appNotReplaceable(why).localizedDescription)
            return
        }

        phase = .downloading(release, fraction: 0)
        downloadTask = Task { await self.performInstall(release, current: current) }
    }

    private func performInstall(_ release: ReleaseInfo, current: AppBundle) async
    {
        let work = FileManager.default.temporaryDirectory
            .appendingPathComponent("offload-update-\(UUID().uuidString)", isDirectory: true)
        defer { downloadTask = nil }

        do
        {
            try FileManager.default.createDirectory(at: work, withIntermediateDirectories: true)

            //the archive, with progress
            let (zip, response) = try await download(release.zip, into: work)
            if let http = response as? HTTPURLResponse, http.statusCode != 200
            {
                throw UpdateError.downloadFailed("HTTP \(http.statusCode)")
            }

            //The checksum, always. A release that does not publish one cannot be
            //verified, and an update that cannot be verified must not replace the
            //installed app — the whole point of the check is that a byte of
            //difference stops here, which it cannot do if the check is optional.
            let sumsAsset = try UpdateInstaller.checksumAsset(for: release)
            let (sumsFile, sumsResponse) = try await download(sumsAsset, into: work)
            if let http = sumsResponse as? HTTPURLResponse, http.statusCode != 200
            {
                throw UpdateError.downloadFailed("checksum file: HTTP \(http.statusCode)")
            }
            let text = (try? String(contentsOf: sumsFile, encoding: .utf8)) ?? ""
            guard let expected = Checksum.parse(text)[release.zip.name.lowercased()] else
            {
                throw UpdateError.checksumMissing
            }
            let got = try Checksum.sha256(of: zip)
            guard got == expected else
            {
                throw UpdateError.checksumMismatch(expected: expected, got: got)
            }

            phase = .installing(release)
            let staged = try UpdateInstaller.stage(zip: zip, into: work.appendingPathComponent("unpacked"))
            try UpdateInstaller.verify(staged: staged, expecting: release, current: current)

            //off the temporary volume, into a place the swap script can still read after we exit
            let holding = work.appendingPathComponent("staged", isDirectory: true)
            try FileManager.default.createDirectory(at: holding, withIntermediateDirectories: true)
            let stagedApp = holding.appendingPathComponent(staged.lastPathComponent)
            try FileManager.default.moveItem(at: staged, to: stagedApp)

            let script = try UpdateInstaller.spawnSwap(pid: ProcessInfo.processInfo.processIdentifier,
                                                       installed: current.path,
                                                       staged: stagedApp,
                                                       relaunch: !Updater.isSelfTest)
            NSLog("Offload update: \(release.version) staged, swap script \(script.path)")
            if Updater.isSelfTest
            {
                print("selftest: staged \(release.version.raw) at \(stagedApp.path)")
                print("selftest: swap script \(script.path), exiting so it can run")
                exit(0)
            }

            //the script is waiting for this process to go away
            try? await Task.sleep(nanoseconds: 300_000_000)
            NSApp.terminate(nil)
        }
        catch let error as UpdateError
        {
            fail(error.localizedDescription)
        }
        catch
        {
            fail(UpdateError.downloadFailed(error.localizedDescription).localizedDescription)
        }
    }

    ///One place to end an install badly, so a self-test can never spin forever
    ///waiting on a run loop after the work has already failed.
    private func fail(_ why: String)
    {
        phase = .failed(why)
        if Updater.isSelfTest
        {
            print("selftest: refused — \(why)")
            exit(3)
        }
    }

    private func download(_ asset: ReleaseAsset, into dir: URL) async throws -> (URL, URLResponse)
    {
        var request = URLRequest(url: asset.url)
        request.setValue("application/octet-stream", forHTTPHeaderField: "Accept")

        let job = AssetDownload(destination: dir.appendingPathComponent(asset.name)) { [weak self] written, expected in
            guard let self, case .downloading(let release, _) = self.phase else { return }
            let total = expected > 0 ? expected : max(asset.bytes, 1)
            let fraction = min(1, Double(written) / Double(total))
            Task { @MainActor in self.phase = .downloading(release, fraction: fraction) }
        }
        return try await job.run(request)
    }

    //-- the menu action --------------------------------------------------------

    func checkFromMenu()
    {
        Task {
            await check()
            switch phase
            {
            case .upToDate(let current, let latest):
                alert(title: "Offload is up to date",
                      text: "You are running \(current.raw). The latest release is \(latest.raw).")
            case .failed(let why):
                alert(title: "Could not check for updates", text: why)
            case .available(let release):
                //the window shows the install button; the alert names the version
                alert(title: "Offload \(release.version.raw) is available",
                      text: "Install it from the bar at the bottom of the window.")
            default:
                break
            }
        }
    }

    private func alert(title: String, text: String)
    {
        let a = NSAlert()
        a.messageText = title
        a.informativeText = text
        a.alertStyle = .informational
        a.addButton(withTitle: "OK")
        a.runModal()
    }
}

///One download, owned end to end: URLSession deletes its temporary file the moment
///`didFinishDownloadingTo` returns, so the delegate moves the bytes to a path it
///controls before handing them back.
private final class AssetDownload: NSObject, URLSessionDownloadDelegate
{
    private let destination: URL
    private let onProgress: (Int64, Int64) -> Void
    private var session: URLSession?
    private var continuation: CheckedContinuation<(URL, URLResponse), Error>?

    init(destination: URL, onProgress: @escaping (Int64, Int64) -> Void)
    {
        self.destination = destination
        self.onProgress = onProgress
    }

    func run(_ request: URLRequest) async throws -> (URL, URLResponse)
    {
        try await withCheckedThrowingContinuation { continuation in
            self.continuation = continuation
            let config = URLSessionConfiguration.ephemeral
            config.timeoutIntervalForRequest = 30
            let session = URLSession(configuration: config, delegate: self, delegateQueue: nil)
            self.session = session
            session.downloadTask(with: request).resume()
        }
    }

    func urlSession(_ session: URLSession, downloadTask: URLSessionDownloadTask,
                    didWriteData bytesWritten: Int64,
                    totalBytesWritten: Int64,
                    totalBytesExpectedToWrite: Int64)
    {
        onProgress(totalBytesWritten, totalBytesExpectedToWrite)
    }

    func urlSession(_ session: URLSession, downloadTask: URLSessionDownloadTask,
                    didFinishDownloadingTo location: URL)
    {
        let fm = FileManager.default
        do
        {
            try? fm.removeItem(at: destination)
            do
            {
                try fm.moveItem(at: location, to: destination)
            }
            catch
            {
                //across volumes moveItem fails; copy instead
                try fm.copyItem(at: location, to: destination)
            }
            finish(.success((destination, downloadTask.response ?? URLResponse())))
        }
        catch
        {
            finish(.failure(UpdateError.downloadFailed(error.localizedDescription)))
        }
    }

    func urlSession(_ session: URLSession, task: URLSessionTask, didCompleteWithError error: Error?)
    {
        if let error { finish(.failure(UpdateError.downloadFailed(error.localizedDescription))) }
    }

    private func finish(_ result: Result<(URL, URLResponse), Error>)
    {
        guard let continuation else { return }
        self.continuation = nil
        session?.finishTasksAndInvalidate()
        session = nil
        continuation.resume(with: result)
    }
}
