// *****************************************************************************
// * Offload window - tests for the updater.                                    *
// *                                                                           *
// *   Offload --update-test              versions, feed, checksums, the swap   *
// *   Offload --check-update             ask GitHub, print, exit               *
// *   Offload --update-selftest URL      full run against a local feed         *
// *                                                                           *
// * The swap test builds two throwaway bundles and lets the real script        *
// * replace one with the other, because that is the only part that can destroy *
// * the app if it is wrong.                                                    *
// *****************************************************************************

import AppKit
import Foundation

enum UpdateSelfTest
{
    private static var checks = 0
    private static var failures = 0

    private static func expect(_ ok: Bool, _ what: String)
    {
        checks += 1
        guard !ok else { return }
        failures += 1
        print("FAIL \(what)")
    }

    //-- the whole unit pass ----------------------------------------------------

    static func run() -> Int32
    {
        versions()
        feed()
        installGate()
        sums()
        hashing()
        script()
        bundleFacts()
        swap()
        failedSwapRestores()

        print("\(checks - failures)/\(checks) update checks passed")
        return failures == 0 ? 0 : 1
    }

    //-- versions ---------------------------------------------------------------

    private static func versions()
    {
        func newer(_ a: String, _ b: String) -> Bool
        {
            guard let l = AppVersion(a), let r = AppVersion(b) else { return false }
            return l > r
        }
        func same(_ a: String, _ b: String) -> Bool
        {
            guard let l = AppVersion(a), let r = AppVersion(b) else { return false }
            return l == r
        }

        expect(newer("0.4.1", "0.4.0"), "0.4.1 is newer than 0.4.0")
        expect(newer("0.10.0", "0.9.0"), "0.10.0 is newer than 0.9.0 (numeric, not string)")
        expect(!newer("0.4.0", "0.4.0"), "equal versions are not newer")
        expect(!newer("0.3.9", "0.4.0"), "older is not newer")
        expect(newer("v0.5.0", "0.4.0"), "a leading v is ignored")
        expect(same("1.0", "1.0.0"), "1.0 equals 1.0.0")
        expect(newer("1.0.0.1", "1.0.0"), "a fourth component counts")
        expect(same("0.4.0-mod", "0.4.0"), "a pre-release suffix reduces to the numbers")
        expect(AppVersion("banana") == nil, "a non-version does not parse")
        expect(AppVersion("") == nil, "an empty tag does not parse")
        expect(AppVersion("v2.1.0")?.description == "2.1.0", "description drops the v")
    }

    //-- feed -------------------------------------------------------------------

    private static let sampleJSON = """
    {
      "tag_name": "v0.4.1",
      "body": "Fix the thing.",
      "html_url": "https://github.com/Salcedoandrew/freefilesync-offload/releases/tag/v0.4.1",
      "draft": false,
      "prerelease": false,
      "assets": [
        {"name": "Offload-0.4.1-macos-arm64.zip",
         "browser_download_url": "https://example.invalid/Offload-0.4.1-macos-arm64.zip",
         "size": 6780846},
        {"name": "SHA256SUMS.txt",
         "browser_download_url": "https://example.invalid/SHA256SUMS.txt",
         "size": 96}
      ]
    }
    """

    private static func feed()
    {
        guard let data = sampleJSON.data(using: .utf8),
              let decoded = try? JSONDecoder().decode(FeedRelease.self, from: data),
              let release = try? decoded.releaseInfo()
        else { return expect(false, "the sample feed decodes") }

        expect(release.version == AppVersion("0.4.1"), "the tag becomes version 0.4.1")
        expect(release.zip.name == "Offload-0.4.1-macos-arm64.zip", "the zip asset is chosen")
        expect(release.sums?.name == "SHA256SUMS.txt", "the checksum asset is found")
        expect(release.notes.contains("Fix the thing"), "the release notes survive")
        expect(release.zip.bytes == 6_780_846, "the asset size survives")

        //a pre-release must not be offered as an update
        let pre = sampleJSON.replacingOccurrences(of: "\"prerelease\": false", with: "\"prerelease\": true")
        if let d = pre.data(using: .utf8), let f = try? JSONDecoder().decode(FeedRelease.self, from: d)
        {
            expect((try? f.releaseInfo()) == nil, "a pre-release is refused")
        }

        //a release without a zip is not installable
        let noZip = sampleJSON.replacingOccurrences(of: "Offload-0.4.1-macos-arm64.zip", with: "notes.pdf")
            .replacingOccurrences(of: "SHA256SUMS.txt", with: "checksums.doc")
        if let d = noZip.data(using: .utf8), let f = try? JSONDecoder().decode(FeedRelease.self, from: d)
        {
            expect((try? f.releaseInfo()) == nil, "a release without an Offload zip is refused")
        }
    }

    //-- the install gate -------------------------------------------------------

    ///A release that publishes no checksum cannot be verified. The install must be
    ///refused at the gate, before anything is downloaded and long before the running
    ///app is moved aside — an update nobody can check is the one thing the checksum
    ///step exists to stop.
    private static func installGate()
    {
        let noSums = sampleJSON.replacingOccurrences(of: "SHA256SUMS.txt", with: "release-notes.txt")
        guard let data = noSums.data(using: .utf8),
              let decoded = try? JSONDecoder().decode(FeedRelease.self, from: data),
              let release = try? decoded.releaseInfo()
        else { return expect(false, "a release without a sums asset still decodes") }

        expect(release.sums == nil, "a release with no sums asset has no checksum")
        expect(release.zip.name == "Offload-0.4.1-macos-arm64.zip", "its zip is still found")

        var refused = false
        var wrongError = ""
        do
        {
            _ = try UpdateInstaller.checksumAsset(for: release)
        }
        catch UpdateError.checksumMissing
        {
            refused = true
        }
        catch
        {
            wrongError = "\(error)"
        }
        expect(refused, "an install is refused when the release publishes no checksum\(wrongError.isEmpty ? "" : " (got: \(wrongError))"))")

        //the case that must still be accepted: the sample release, with its sums file
        if let okData = sampleJSON.data(using: .utf8),
           let okDecoded = try? JSONDecoder().decode(FeedRelease.self, from: okData),
           let okRelease = try? okDecoded.releaseInfo()
        {
            let asset = try? UpdateInstaller.checksumAsset(for: okRelease)
            expect(asset?.name == "SHA256SUMS.txt", "a release with a checksum passes the gate")
        }
        else
        {
            expect(false, "the sample feed decodes for the positive case")
        }
    }

    //-- checksums --------------------------------------------------------------

    private static func sums()
    {
        let hex = String(repeating: "a", count: 64)
        let text = """
        \(hex)  Offload-0.4.1-macos-arm64.zip
        \(String(repeating: "b", count: 64))  *other.zip
        not-a-checksum  ignored.zip
        """
        let parsed = Checksum.parse(text)
        expect(parsed["offload-0.4.1-macos-arm64.zip"] == hex, "the sums file maps name to hash")
        expect(parsed["other.zip"]?.first == "b", "a * prefix is stripped and names are case-folded")
        expect(parsed["ignored.zip"] == nil, "a malformed line is skipped")
    }

    private static func hashing()
    {
        let url = FileManager.default.temporaryDirectory.appendingPathComponent("offload-hash-\(UUID().uuidString)")
        var bytes = [UInt8](repeating: 0, count: 3_000_000)
        for i in bytes.indices { bytes[i] = UInt8.random(in: 0...255) }
        guard (try? Data(bytes).write(to: url)) != nil else { return expect(false, "the hash fixture is written") }
        defer { try? FileManager.default.removeItem(at: url) }

        let ours = (try? Checksum.sha256(of: url)) ?? ""
        let p = Process()
        p.executableURL = URL(fileURLWithPath: "/usr/bin/shasum")
        p.arguments = ["-a", "256", url.path]
        let pipe = Pipe()
        p.standardOutput = pipe
        try? p.run()
        p.waitUntilExit()
        let out = String(decoding: pipe.fileHandleForReading.readDataToEndOfFile(), as: UTF8.self)
        let theirs = out.split(separator: " ").first.map(String.init) ?? ""

        expect(ours.count == 64, "sha256 produces 64 hex characters")
        expect(ours == theirs, "sha256 matches shasum -a 256")
    }

    //-- the swap script --------------------------------------------------------

    private static func script()
    {
        let s = UpdateInstaller.swapScript()
        expect(s.contains("kill -0 \"$pid\""), "the script waits for the app to exit")
        expect(s.contains("mv \"$app\" \"$backup\""), "the old bundle is moved aside")
        expect(s.contains("mv \"$staged\" \"$app\""), "the new bundle is moved in")
        expect(s.contains("codesign --verify"), "the new bundle is re-verified after the move")
        expect(s.contains("mv \"$backup\" \"$app\""), "a failed swap puts the old bundle back")
        expect(s.contains("last-swap.log"), "the script leaves a one-line log")
        expect(s.contains("rm -rf \"$backup\""), "a successful swap deletes the old bundle")
        expect(s.contains("xattr -dr com.apple.quarantine"), "quarantine is cleared")
        expect(s.contains("open \"$app\""), "the new bundle is reopened")
    }

    private static func bundleFacts()
    {
        let translocated = URL(fileURLWithPath: "/private/var/folders/xy/T/AppTranslocation/ABC/d/Offload.app")
        if let bundle = AppBundle.at(translocated)
        {
            expect(bundle.isTranslocated, "a translocated path is recognised")
        }
        //that path does not exist, so build one that does
        let dir = FileManager.default.temporaryDirectory.appendingPathComponent("offload-facts-\(UUID().uuidString)")
        defer { try? FileManager.default.removeItem(at: dir) }
        let fake = dir.appendingPathComponent("AppTranslocation/X/d/Offload.app")
        if (try? makeBundle(at: fake, version: "0.4.0")) != nil, let bundle = AppBundle.at(fake)
        {
            expect(bundle.isTranslocated, "a translocated bundle reports itself translocated")
            expect(!bundle.isReplaceable, "a translocated bundle is not replaceable")
        }
    }

    //-- replacing a bundle -----------------------------------------------------

    @discardableResult
    private static func makeBundle(at url: URL, version: String,
                                   id: String = "org.freefilesync.offload",
                                   sign: Bool = true) throws -> URL
    {
        let fm = FileManager.default
        let macos = url.appendingPathComponent("Contents/MacOS")
        try fm.createDirectory(at: macos, withIntermediateDirectories: true)

        let plist: [String: Any] = ["CFBundleName": "Offload",
                                    "CFBundleIdentifier": id,
                                    "CFBundleExecutable": "Offload",
                                    "CFBundlePackageType": "APPL",
                                    "CFBundleShortVersionString": version,
                                    "CFBundleVersion": version]
        let data = try PropertyListSerialization.data(fromPropertyList: plist, format: .xml, options: 0)
        try data.write(to: url.appendingPathComponent("Contents/Info.plist"))
        try "#!/bin/sh\nexit 0\n".write(to: macos.appendingPathComponent("Offload"), atomically: true, encoding: .utf8)
        try fm.setAttributes([.posixPermissions: 0o755], ofItemAtPath: macos.appendingPathComponent("Offload").path)

        if sign
        {
            let p = Process()
            p.executableURL = URL(fileURLWithPath: "/usr/bin/codesign")
            p.arguments = ["--force", "--sign", "-", url.path]
            p.standardError = Pipe()
            p.standardOutput = Pipe()
            try p.run()
            p.waitUntilExit()
        }
        return url
    }

    private static func waitFor(_ seconds: Double, until done: () -> Bool) -> Bool
    {
        let deadline = Date().addingTimeInterval(seconds)
        while Date() < deadline
        {
            if done() { return true }
            Thread.sleep(forTimeInterval: 0.1)
        }
        return done()
    }

    ///The real script, on throwaway bundles: old is deleted, new is in its place.
    private static func swap()
    {
        let fm = FileManager.default
        let root = fm.temporaryDirectory.appendingPathComponent("offload-swap-\(UUID().uuidString)")
        defer { try? fm.removeItem(at: root) }

        do
        {
            let installed = try makeBundle(at: root.appendingPathComponent("Apps/Offload.app"), version: "0.4.0")
            let staged = try makeBundle(at: root.appendingPathComponent("stage/Offload.app"), version: "0.4.1")

            //pid 0 means "nobody to wait for", which is how the tests drive it
            _ = try UpdateInstaller.spawnSwap(pid: 0, installed: installed, staged: staged, relaunch: false)

            let replaced = waitFor(15) { AppBundle.at(installed)?.version == AppVersion("0.4.1") }
            expect(replaced, "the installed bundle is now the new version")

            let leftovers = (try? fm.contentsOfDirectory(atPath: installed.deletingLastPathComponent().path))?
                .filter { $0.contains(".old-") } ?? []
            expect(leftovers.isEmpty, "the old bundle is deleted, not left behind")
        }
        catch
        {
            expect(false, "the swap test runs (\(error))")
        }
    }

    ///A staged bundle whose signature is broken must not replace a working app.
    private static func failedSwapRestores()
    {
        let fm = FileManager.default
        let root = fm.temporaryDirectory.appendingPathComponent("offload-swap-bad-\(UUID().uuidString)")
        defer { try? fm.removeItem(at: root) }

        do
        {
            let installed = try makeBundle(at: root.appendingPathComponent("Apps/Offload.app"), version: "0.4.0")
            let staged = try makeBundle(at: root.appendingPathComponent("stage/Offload.app"), version: "0.4.1")

            //touch the bundle after signing: codesign --verify now fails
            try "broken".write(to: staged.appendingPathComponent("Contents/Resources-oops.txt"),
                               atomically: true, encoding: .utf8)

            _ = try UpdateInstaller.spawnSwap(pid: 0, installed: installed, staged: staged, relaunch: false)

            //give it the same window the good path gets, then assert nothing moved
            _ = waitFor(5) { false }
            expect(AppBundle.at(installed)?.version == AppVersion("0.4.0"),
                   "a rejected swap leaves the old version installed")
            expect(AppBundle.at(installed) != nil, "a rejected swap leaves an app at all")
        }
        catch
        {
            expect(false, "the failed-swap test runs (\(error))")
        }
    }

    //-- command line -----------------------------------------------------------

    ///Asks the configured feed and prints the answer. Runs the main run loop, because
    ///URLSession needs it; exits from inside the task.
    static func runCheckUpdate() -> Never
    {
        Task { @MainActor in
            await Updater.shared.check()
            let version = Updater.shared.currentVersion?.raw ?? "unknown"
            switch Updater.shared.phase
            {
            case .upToDate(let current, let latest):
                print("current \(current.raw), latest \(latest.raw) — up to date")
                exit(0)
            case .available(let release):
                print("current \(version), latest \(release.version.raw) — update available")
                print("asset \(release.zip.name), \(release.zip.bytes) bytes, checksum \(release.sums?.name ?? "MISSING")")
                exit(10)
            case .failed(let why):
                print("check failed: \(why)")
                exit(1)
            default:
                print("check ended in an unexpected state")
                exit(1)
            }
        }
        RunLoop.main.run()
        exit(1)
    }

    ///Full path against a feed you control: check, download, verify, swap, exit.
    static func runEndToEnd(feed: String) -> Never
    {
        guard let url = URL(string: feed) else
        {
            print("usage: Offload --update-selftest <feed-url>")
            exit(2)
        }
        Updater.isSelfTest = true
        Updater.feedOverride = url

        Task { @MainActor in
            await Updater.shared.check()
            switch Updater.shared.phase
            {
            case .available(let release):
                print("selftest: \(release.version.raw) available as \(release.zip.name)")
                Updater.shared.install(release) //exits after the swap script is running
            case .upToDate(let current, let latest):
                print("selftest: up to date (current \(current.raw), latest \(latest.raw))")
                exit(2)
            case .failed(let why):
                print("selftest: \(why)")
                exit(3)
            default:
                print("selftest: unexpected phase")
                exit(4)
            }
        }
        RunLoop.main.run()
        exit(5)
    }
}
