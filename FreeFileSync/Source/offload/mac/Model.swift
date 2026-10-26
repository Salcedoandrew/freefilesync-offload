// *****************************************************************************
// * Offload window - devices, transfers and the polling loop.                 *
// *****************************************************************************

import Foundation
import SwiftUI

enum DeviceKind
{
    case folder, card, drive

    var glyph: String
    {
        switch self
        {
        case .folder: return "folder"
        case .card:   return "card"
        case .drive:  return "drive"
        }
    }
}

///What the engine says a transfer out of this source would carry. "measuring" is a
///real state, not a placeholder: the button stays disabled until it settles, because
///guessing the size is exactly how a destination ends up too small.
enum SourceSize: Equatable
{
    case measuring
    case known(bytes: UInt64, files: UInt64)
    case failed(String)
}

///Can this destination hold what the selected sources will carry?
enum FitVerdict: Equatable
{
    case fits
    case short(by: UInt64)
    case unavailable //the drive was ejected or renamed since it was added
    case unknown     //source not measured yet, or the volume reports no free space

    ///True when starting would be knowingly wrong, so the transfer must not start.
    var blocksTransfer: Bool
    {
        switch self
        {
        case .short, .unavailable: return true
        case .fits, .unknown:      return false
        }
    }
}

///The decision itself, with no UI state in it, so it can be tested without a window.
enum FitCheck
{
    static func verdict(required: UInt64?, free: UInt64) -> FitVerdict
    {
        guard let required else { return .unknown }
        guard free > 0 else { return .unknown } //volume not mounted / no answer
        return required <= free ? .fits : .short(by: required - free)
    }
}

struct Device: Identifiable, Equatable
{
    let id = UUID()
    var name: String
    var path: String
    var kind: DeviceKind
    var totalBytes: UInt64
    var freeBytes: UInt64
    var size: SourceSize = .measuring

    ///nil asks the file system whether the path is there. Only the preview data sets
    ///it, so a rendered screenshot never depends on which volumes exist right now.
    var existsOnDisk: Bool? = nil

    var capacityText: String
    {
        totalBytes > 0 ? Format.capacity(totalBytes) : "—"
    }

    var measuredBytes: UInt64?
    {
        if case .known(let bytes, _) = size { return bytes }
        return nil
    }

    var measuredFiles: UInt64?
    {
        if case .known(_, let files) = size { return files }
        return nil
    }
}

enum Volume
{
    ///Free space on the volume holding exactly this path, or 0 when it cannot be
    ///answered. Deliberately no fallback to a parent: an ejected drive would then
    ///report the internal disk's free space and wave a doomed transfer through.
    static func freeSpace(at path: String) -> UInt64
    {
        guard let attrs = try? FileManager.default.attributesOfFileSystem(forPath: path),
              let free = (attrs[.systemFreeSize] as? NSNumber)?.uint64Value else { return 0 }
        return free
    }
}

enum Format
{
    /// Decimal units: a "6 TB" drive really is 6e12 bytes.
    static func capacity(_ bytes: UInt64) -> String
    {
        let units = ["B", "KB", "MB", "GB", "TB", "PB"]
        var value = Double(bytes)
        var unit = 0
        while value >= 1000, unit < units.count - 1
        {
            value /= 1000
            unit += 1
        }
        //one decimal when it says something, none when it does not: 92.8 GB, 585 GB
        let tenths = (value * 10).rounded() / 10
        return tenths == tenths.rounded()
            ? String(format: "%.0f %@", tenths, units[unit])
            : String(format: "%.1f %@", tenths, units[unit])
    }

    static func rate(_ bytesPerSecond: Double) -> String
    {
        guard bytesPerSecond.isFinite, bytesPerSecond > 0 else { return "—" }
        return capacity(UInt64(bytesPerSecond)) + "/sec"
    }

    static func duration(_ seconds: Double) -> String
    {
        guard seconds.isFinite, seconds > 0 else { return "" }

        func unit(_ n: Int, _ one: String, _ many: String) -> String
        {
            "\(n) \(n == 1 ? one : many)"
        }
        if seconds < 1    { return "" } //sub-second: say nothing rather than "0 seconds"
        if seconds < 50   { return unit(Int(seconds.rounded()), "second", "seconds") }
        if seconds < 3600 { return unit(Int((seconds / 60).rounded()), "minute", "minutes") }

        let h = Int(seconds / 3600)
        let m = Int((seconds.truncatingRemainder(dividingBy: 3600)) / 60)
        return m > 0 ? "\(unit(h, "hr", "hr")) \(m) min" : unit(h, "hr", "hr")
    }
}

final class Transfer: ObservableObject, Identifiable
{
    enum Phase
    {
        case preparing, copying, verifying, done, cancelled, failed
    }

    let id = UUID()
    let source: Device
    let destination: Device
    let subfolder: String
    let job: EngineJob?

    @Published var fraction: Double = 0
    @Published var phase: Phase = .preparing
    @Published var currentFile: String = ""
    @Published var bytesPerSecond: Double = 0
    @Published var etaSeconds: Double = 0
    @Published var paused: Bool = false
    @Published var filesDone: UInt64 = 0
    @Published var filesTotal: UInt64 = 0
    @Published var failedCount: UInt64 = 0
    @Published var verifiedCount: UInt64 = 0
    @Published var bytesDone: UInt64 = 0
    @Published var bytesTotal: UInt64 = 0
    @Published var reportPath: String = ""
    @Published var errorText: String = ""

    private var lastBytes: UInt64 = 0
    private var lastTime = Date()
    private var sawFinish = false

    init(source: Device, destination: Device, subfolder: String, job: EngineJob?)
    {
        self.source = source
        self.destination = destination
        self.subfolder = subfolder
        self.job = job
    }

    var isFinished: Bool { phase == .done || phase == .cancelled || phase == .failed }
    var isRunning: Bool { !isFinished && !paused }

    var targetText: String
    {
        subfolder.isEmpty ? destination.name : "\(destination.name) · \(subfolder)"
    }

    var statusText: String
    {
        switch phase
        {
        case .preparing:
            return filesTotal == 0 ? "Preparing…" : "Preparing… \(filesTotal) files"
        case .copying:
            let eta = Format.duration(etaSeconds)
            let rate = Format.rate(bytesPerSecond)
            return eta.isEmpty ? "Copying (\(rate))" : "About \(eta) (\(rate))"
        case .verifying:
            let eta = Format.duration(etaSeconds)
            let rate = Format.rate(bytesPerSecond)
            return eta.isEmpty ? "Verifying (\(rate))" : "Verifying · about \(eta) (\(rate))"
        case .done:
            return "Completed"
        case .cancelled:
            return "Cancelled"
        case .failed:
            return failedCount > 0 ? "\(failedCount) file\(failedCount == 1 ? "" : "s") failed"
                                   : (errorText.isEmpty ? "Failed" : errorText)
        }
    }

    //Colour of the bar fill, following the same semantics the CLI reports.
    var barColors: [Color]
    {
        switch phase
        {
        case .done:                       return [Theme.green, Theme.greenDim]
        case .failed where failedCount>0:  return [Theme.red, Theme.red.opacity(0.75)]
        case .cancelled:                  return [Theme.textTertiary, Theme.textTertiary.opacity(0.7)]
        case .verifying:                  return [Color(hex: 0x63C7FF), Color(hex: 0x2B93E8)]
        default:                          return [Theme.blue, Theme.blueDim]
        }
    }

    func refresh()
    {
        guard let job else { return }
        let raw = job.poll()

        bytesDone  = raw.bytesDone
        bytesTotal = raw.bytesTotal
        filesDone  = raw.filesDone
        filesTotal = raw.filesTotal
        verifiedCount = raw.verifiedCount
        failedCount   = raw.failedCount
        paused        = raw.phase != Int32(OFFLOAD_PHASE_DONE) && job.isPaused


        if raw.bytesTotal > 0
        {
            fraction = min(1, Double(raw.bytesDone) / Double(raw.bytesTotal))
        }

        switch raw.phase
        {
        case Int32(OFFLOAD_PHASE_SCANNING):  phase = .preparing
        case Int32(OFFLOAD_PHASE_COPYING):   phase = .copying
        case Int32(OFFLOAD_PHASE_VERIFYING): phase = .verifying
        default: break
        }

        let file = job.currentFile
        if !file.isEmpty { currentFile = (file as NSString).lastPathComponent }

        //Rate from successive polls: a 1 s moving average reads far steadier than
        //instantaneous, and ETA follows from it.
        let now = Date()
        let dt = now.timeIntervalSince(lastTime)
        if dt >= 0.25, raw.bytesDone >= lastBytes
        {
            let instant = Double(raw.bytesDone - lastBytes) / dt
            bytesPerSecond = bytesPerSecond == 0 ? instant : bytesPerSecond * 0.7 + instant * 0.3
            lastBytes = raw.bytesDone
            lastTime = now
            if bytesPerSecond > 1_000
            {
                let remaining = raw.bytesTotal > raw.bytesDone ? Double(raw.bytesTotal - raw.bytesDone) : 0
                etaSeconds = remaining / bytesPerSecond
            }
        }

        if raw.finished != 0 && !sawFinish
        {
            sawFinish = true
            reportPath = job.reportPath
            errorText = job.errorText
            if raw.cancelled != 0
            {
                phase = .cancelled
            }
            else if raw.failedCount > 0 || !errorText.isEmpty
            {
                phase = .failed
            }
            else
            {
                phase = .done
                fraction = 1
                bytesPerSecond = 0
                etaSeconds = 0
            }
        }
    }

    func togglePause()
    {
        job?.setPaused(!paused)
        paused.toggle()
    }

    func cancel() { job?.cancel() }
}

final class AppModel: ObservableObject
{
    @Published var sources: [Device] = []
    @Published var destinations: [Device] = []
    @Published var transfers: [Transfer] = []

    ///An update must not replace the app out from under a running copy.
    var hasActiveTransfers: Bool { transfers.contains { !$0.isFinished } }
    @Published var selectedSources: Set<UUID> = []
    @Published var selectedDestinations: Set<UUID> = []
    @Published var allPaused = false
    @Published var isTargetingSources = false
    @Published var isTargetingDestinations = false

    ///Frames of clickable views, published by the views themselves. Used only by
    ///`--ui-test`; the running app never reads it.
    var anchorFrames: [String: CGRect] = [:]

    private var timer: Timer?
    private var measuredAt: [UUID: Date] = [:]
    private var lastFreeSpacePoll = Date.distantPast
    private let reportRoot: URL

    init(startClock: Bool = true)
    {
        let pictures = FileManager.default.urls(for: .picturesDirectory, in: .userDomainMask).first
            ?? FileManager.default.homeDirectoryForCurrentUser
        reportRoot = pictures.appendingPathComponent("Offload Reports", isDirectory: true)

        if startClock
        {
            timer = Timer.scheduledTimer(withTimeInterval: 0.1, repeats: true) { [weak self] _ in
                self?.tick()
            }
            RunLoop.main.add(timer!, forMode: .common)
        }
    }

    deinit { timer?.invalidate() }

    func tick()
    {
        for t in transfers where !t.isFinished { t.refresh() }

        //free space moves while transfers run, so the fit check must not read a
        //number from ten minutes ago
        if Date().timeIntervalSince(lastFreeSpacePoll) >= 2
        {
            lastFreeSpacePoll = Date()
            refreshFreeSpace()
            remeasureStaleSources()
        }
    }

    func refreshFreeSpace()
    {
        for (i, device) in destinations.enumerated()
        {
            let free = Volume.freeSpace(at: device.path)
            if free != destinations[i].freeBytes { destinations[i].freeBytes = free }
        }
    }

    //--- device intake ---------------------------------------------------------

    func addSource(url: URL)
    {
        add(url: url, kind: nil, into: &sources, selected: &selectedSources)
        if let device = sources.last { startMeasurement(of: device.id) }
    }

    ///Walks the source on a background queue; a slow card must not freeze the window.
    ///The numbers come from the engine, so "what will be copied" has one owner.
    func startMeasurement(of id: UUID, resetState: Bool = true)
    {
        guard let index = sources.firstIndex(where: { $0.id == id }) else { return }
        //a periodic re-measure must not blank a known size and disable the button
        if resetState { sources[index].size = .measuring }
        measuredAt[id] = Date()
        let path = sources[index].path

        DispatchQueue.global(qos: .userInitiated).async
        { [weak self] in
            let result = SourceMeasure.measure(path)
            DispatchQueue.main.async
            {
                guard let self, let i = self.sources.firstIndex(where: { $0.id == id }) else { return }
                switch result
                {
                case .success(let m): self.sources[i].size = .known(bytes: m.bytes, files: m.files)
                case .failure(let e): self.sources[i].size = .failed(e.message)
                }
            }
        }
    }

    ///A card's contents can change between transfers; re-measure rather than trust
    ///a number from a while ago. Only when nothing is running.
    private func remeasureStaleSources()
    {
        guard transfers.allSatisfy(\.isFinished) else { return }
        for device in sources where isSelected(device.id)
        {
            if let at = measuredAt[device.id], Date().timeIntervalSince(at) < 30 { continue }
            startMeasurement(of: device.id, resetState: false)
        }
    }

    func addDestination(url: URL)
    {
        add(url: url, kind: .drive, into: &destinations, selected: &selectedDestinations)
    }

    private func add(url: URL, kind: DeviceKind?, into list: inout [Device], selected: inout Set<UUID>)
    {
        let path = url.path
        if list.contains(where: { $0.path == path }) { return }

        let values = try? url.resourceValues(forKeys: [.volumeNameKey, .volumeTotalCapacityKey,
                                                       .volumeAvailableCapacityKey, .isDirectoryKey])
        let attrs = try? FileManager.default.attributesOfFileSystem(forPath: path)
        let total = (attrs?[.systemSize] as? NSNumber)?.uint64Value
            ?? UInt64(values?.volumeTotalCapacity ?? 0)
        let free = (attrs?[.systemFreeSize] as? NSNumber)?.uint64Value
            ?? UInt64(values?.volumeAvailableCapacity ?? 0)

        //A folder is named after itself; only a volume root takes the volume's name,
        //otherwise every folder on the boot disk would read "Macintosh HD".
        let resolvedKind = kind ?? DeviceKind.folder
        let volumeName = values?.volumeName ?? url.lastPathComponent
        let name = isVolumeRoot(path: path) && !volumeName.isEmpty ? volumeName : url.lastPathComponent
        var device = Device(name: name,
                            path: path, kind: resolvedKind,
                            totalBytes: total, freeBytes: free)

        //A folder that is a mount point is a drive; keep the glyph honest.
        if kind == nil, isVolumeRoot(path: path) { device.kind = .drive }
        if kind == nil, looksLikeCameraCard(path: path) { device.kind = .card }

        list.append(device)
        selected.insert(device.id)
    }

    private func isVolumeRoot(path: String) -> Bool
    {
        let volumes = FileManager.default.mountedVolumeURLs(includingResourceValuesForKeys: nil,
                                                           options: [.skipHiddenVolumes]) ?? []
        return volumes.contains { $0.path == path }
    }

    private func looksLikeCameraCard(path: String) -> Bool
    {
        //Camcorder/camera cards carry a DCIM tree at the root.
        FileManager.default.fileExists(atPath: path + "/DCIM")
    }

    //--- transfers -------------------------------------------------------------

    var selectedSourceDevices: [Device] { sources.filter { selectedSources.contains($0.id) } }
    var selectedDestinationDevices: [Device] { destinations.filter { selectedDestinations.contains($0.id) } }

    var plannedTransferCount: Int { selectedSourceDevices.count * selectedDestinationDevices.count }

    private func isSelected(_ id: UUID) -> Bool { selectedSources.contains(id) }

    //--- fit check ---------------------------------------------------------

    ///Total the selected sources would carry, or nil while any of them is still being
    ///measured. A destination that cannot hold this must not be allowed to start.
    var requiredBytes: UInt64?
    {
        let selected = selectedSourceDevices
        guard !selected.isEmpty else { return nil }
        var total: UInt64 = 0
        for source in selected
        {
            guard let bytes = source.measuredBytes else { return nil }
            total += bytes
        }
        return total
    }

    var requiredFiles: UInt64?
    {
        let selected = selectedSourceDevices
        guard !selected.isEmpty else { return nil }
        var total: UInt64 = 0
        for source in selected
        {
            guard let files = source.measuredFiles else { return nil }
            total += files
        }
        return total
    }

    var isMeasuringSources: Bool
    {
        selectedSourceDevices.contains { if case .measuring = $0.size { return true }; return false }
    }

    var measurementFailed: String?
    {
        for source in selectedSourceDevices
        {
            if case .failed(let why) = source.size { return "\(source.name): \(why)" }
        }
        return nil
    }

    func verdict(for destination: Device) -> FitVerdict
    {
        guard destination.existsOnDisk ?? FileManager.default.fileExists(atPath: destination.path) else
        {
            return .unavailable
        }
        return FitCheck.verdict(required: requiredBytes, free: destination.freeBytes)
    }

    ///Selected destinations that must not be started (too small, or not there).
    var blockedDestinations: [(device: Device, verdict: FitVerdict)]
    {
        selectedDestinationDevices.compactMap
        { device in
            let v = verdict(for: device)
            return v.blocksTransfer ? (device, v) : nil
        }
    }

    ///The single rule the Add button and addTransfer() both obey.
    var canAddTransfer: Bool
    {
        !selectedSourceDevices.isEmpty &&
        !selectedDestinationDevices.isEmpty &&
        !isMeasuringSources &&
        blockedDestinations.isEmpty
    }

    ///Short label for a destination tile, or nil when there is nothing to warn about.
    func warning(for destination: Device) -> String?
    {
        switch verdict(for: destination)
        {
        case .short(let by):  return "Short by \(Format.capacity(by))"
        case .unavailable:    return "Not connected"
        case .fits, .unknown: return nil
        }
    }

    ///One line explaining a disabled Add button, or a neutral summary when it is live.
    var fitSummary: (text: String, isWarning: Bool)
    {
        if let (device, verdict) = blockedDestinations.first
        {
            let more = blockedDestinations.count > 1 ? " (+ \(blockedDestinations.count - 1) more)" : ""
            switch verdict
            {
            case .short(let by): return ("\(device.name) is \(Format.capacity(by)) short\(more)", true)
            case .unavailable:   return ("\(device.name) is not connected\(more)", true)
            default: break
            }
        }
        if isMeasuringSources { return ("Measuring the source…", false) }
        if let failed = measurementFailed { return ("Size unknown (\(failed)) — fit not checked", true) }
        if let bytes = requiredBytes, let files = requiredFiles
        {
            return ("\(Format.capacity(bytes)) · \(files) file\(files == 1 ? "" : "s")", false)
        }
        return ("", false)
    }

    func addTransfer()
    {
        //Re-read free space at the moment of the decision, not two seconds earlier.
        refreshFreeSpace()
        guard canAddTransfer else { return }

        for source in selectedSourceDevices
        {
            for destination in selectedDestinationDevices
            {
                let subfolder = source.name
                let stamp = Self.stampFormatter.string(from: Date())
                let reportDir = reportRoot
                    .appendingPathComponent("\(stamp) \(source.name) to \(destination.name)", isDirectory: true)

                //The engine writes into destination/subfolder, never into the card.
                let destPath = destination.path.hasSuffix("/")
                    ? destination.path + subfolder
                    : destination.path + "/" + subfolder

                let job = EngineJob(source: source.path, destination: destPath,
                                    reportDir: reportDir.path)
                let transfer = Transfer(source: source, destination: destination,
                                        subfolder: subfolder, job: job)
                if job == nil { transfer.phase = .failed; transfer.errorText = "cannot open source" }
                transfers.append(transfer)
            }
        }
    }

    func togglePauseAll()
    {
        allPaused.toggle()
        for t in transfers where !t.isFinished { t.job?.setPaused(allPaused); t.paused = allPaused }
    }

    private static let stampFormatter: DateFormatter =
    {
        let f = DateFormatter()
        f.dateFormat = "yyyy-MM-dd HH.mm.ss"
        return f
    }()
}
