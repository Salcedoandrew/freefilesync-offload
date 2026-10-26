// *****************************************************************************
// * Offload window - sample state for the rendered screenshots.                *
// *                                                                            *
// * Only ever constructed by `--render`; the real app starts empty and fills   *
// * its panes from dropped cards and drives.                                   *
// *****************************************************************************

import Foundation

enum Demo
{
    static func model(tooSmall: Bool = false) -> AppModel
    {
        let model = AppModel(startClock: false)

        let card = Device(name: "Untitled", path: "/Volumes/Untitled", kind: .card,
                          totalBytes: 92_800_000_000, freeBytes: 0,
                          size: .known(bytes: 92_800_000_000, files: 1_284))
        let projectX = Device(name: "Production X", path: "/Volumes/Production X", kind: .drive,
                              totalBytes: 6_000_000_000_000, freeBytes: 5_600_000_000_000)
        let backup01 = Device(name: "Backup", path: "/Volumes/Backup", kind: .drive,
                              totalBytes: 4_000_000_000_000, freeBytes: 962_200_000_000)
        //the too-small state exists so the warning can be screenshotted: a 34 GB
        //drive cannot hold a 92.8 GB card
        let backup02 = Device(name: "Backup 02", path: "/Volumes/Backup 02", kind: .drive,
                              totalBytes: 1_000_000_000_000,
                              freeBytes: tooSmall ? 34_000_000_000 : 784_500_000_000)
        let ssd = Device(name: "SSD", path: "/Volumes/SSD", kind: .drive,
                         totalBytes: 2_000_000_000_000, freeBytes: 1_905_400_000_000)

        model.sources = [card].map(preview)
        model.destinations = [projectX, backup01, backup02, ssd].map(preview)
        model.selectedSources = [card.id]
        model.selectedDestinations = tooSmall ? [backup02.id] : [backup01.id]

        //one finished row, two moving: exactly the state the reference shows
        model.transfers = [
            make(from: card, to: projectX, subfolder: "Day 6",
                 fraction: 1.0, phase: .done, file: "", filesDone: 214, filesTotal: 214),
            make(from: card, to: backup01, subfolder: "Day 6",
                 fraction: 0.62, phase: .copying, file: "PX03467.mp4",
                 filesDone: 133, filesTotal: 214, rate: 536_000_000, remaining: 58),
            make(from: card, to: backup02, subfolder: "Day 6",
                 fraction: 0.38, phase: .copying, file: "DSC_20487.DNG",
                 filesDone: 81, filesTotal: 214, rate: 582_000_000, remaining: 124),
        ]
        return model
    }

    ///The confirmation window's sample run: the reference card copied to three
    ///drives, at rates the measurements behind the README support.
    static var summary: RunSummary
    {
        RunSummary(records:
        [
            TransferRecord(source: "Untitled", destination: "Production X · Day 6",
                           files: 1_284, bytes: 92_800_000_000, seconds: 74, failed: 0,
                           reportPath: "Offload Reports/2026-10-26 09.12.40 Untitled to Production X/report.csv"),
            TransferRecord(source: "Untitled", destination: "Backup · Day 6",
                           files: 1_284, bytes: 92_800_000_000, seconds: 96, failed: 0,
                           reportPath: "Offload Reports/2026-10-26 09.12.40 Untitled to Backup/report.csv"),
            TransferRecord(source: "Untitled", destination: "Backup 02 · Day 6",
                           files: 1_284, bytes: 92_800_000_000, seconds: 118, failed: 0,
                           reportPath: "Offload Reports/2026-10-26 09.12.40 Untitled to Backup 02/report.csv"),
        ])
    }

    ///Demo devices are not mounted anywhere; say so instead of asking the disk.
    private static func preview(_ device: Device) -> Device
    {
        var copy = device
        copy.existsOnDisk = true
        return copy
    }

    private static func make(from source: Device, to destination: Device, subfolder: String,
                             fraction: Double, phase: Transfer.Phase, file: String,
                             filesDone: UInt64, filesTotal: UInt64,
                             rate: Double = 0, remaining: Double = 0) -> Transfer
    {
        let t = Transfer(source: source, destination: destination, subfolder: subfolder, job: nil)
        t.fraction = fraction
        t.phase = phase
        t.currentFile = file
        t.filesDone = filesDone
        t.filesTotal = filesTotal
        t.bytesPerSecond = rate
        t.etaSeconds = remaining
        t.bytesTotal = 92_800_000_000
        t.bytesDone = UInt64(Double(t.bytesTotal) * fraction)
        return t
    }
}
