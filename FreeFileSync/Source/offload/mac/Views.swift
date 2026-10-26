// *****************************************************************************
// * Offload window - three panes: sources, transfers, destinations.            *
// *                                                                            *
// * Layout follows the reference workflow (card on the left, drives on the     *
// * right, one row per source -> destination in the middle) with original      *
// * drawing: no vendor artwork is used anywhere.                              *
// *****************************************************************************

import SwiftUI
import UniformTypeIdentifiers
import AppKit

//ImageRenderer lays out without a window, and a ScrollView then renders as an
//empty viewport. Offscreen snapshots therefore get the same content laid out
//directly; the running app always scrolls.
struct SnapshotRenderingKey: EnvironmentKey
{
    static let defaultValue = false
}

extension EnvironmentValues
{
    var snapshotRendering: Bool
    {
        get { self[SnapshotRenderingKey.self] }
        set { self[SnapshotRenderingKey.self] = newValue }
    }
}

struct RootView: View
{
    @ObservedObject var model: AppModel

    var body: some View
    {
        HStack(spacing: 0)
        {
            DevicePane(title: "Sources",
                       devices: model.sources,
                       selection: $model.selectedSources,
                       isTargeted: $model.isTargetingSources,
                       showsAddTile: true,
                       emptyHint: "Drop a card or a folder here",
                       onAdd: pickSource,
                       onDropURL: model.addSource,
                       anchorPrefix: "src")
            .frame(width: 158)

            separator

            TransfersPane(model: model)
                .frame(maxWidth: .infinity)

            separator

            DevicePane(title: "Destinations",
                       devices: model.destinations,
                       selection: $model.selectedDestinations,
                       isTargeted: $model.isTargetingDestinations,
                       showsAddTile: true,
                       emptyHint: "Drop a drive here",
                       onAdd: pickDestination,
                       onDropURL: model.addDestination,
                       warningFor: { model.warning(for: $0) },
                       anchorPrefix: "dst")
            .frame(width: 176)
        }
        .padding(.top, 26)
        .background(Theme.windowBG)
        .preferredColorScheme(.dark)
        //published frames, so the mouse-driven UI test can click without guessing
        .onPreferenceChange(AnchorFrames.self) { model.anchorFrames = $0 }
        //A finished run says what it copied and how fast, once the last row stops.
        .sheet(item: summarySheet)
        { summary in
            RunSummaryView(summary: summary) { model.dismissSummary() }
        }
    }

    ///Nil while --ui-serve is driving the window: a sheet there would cover the
    ///buttons the mouse test is about to click.
    private var summarySheet: Binding<RunSummary?>
    {
        Binding(get: { AppModel.suppressesSummarySheet ? nil : model.summary },
                set: { model.summary = $0 })
    }

    private var separator: some View
    {
        Rectangle().fill(Theme.stroke).frame(width: 1)
    }

    private func pickSource()
    {
        for url in openPanel(title: "Choose a card or folder to copy")
        { model.addSource(url: url) }
    }

    private func pickDestination()
    {
        for url in openPanel(title: "Choose a destination", canCreateFolder: true)
        { model.addDestination(url: url) }
    }

    private func openPanel(title: String, canCreateFolder: Bool = false) -> [URL]
    {
        let panel = NSOpenPanel()
        panel.title = title
        panel.canChooseDirectories = true
        panel.canChooseFiles = false
        panel.allowsMultipleSelection = true
        //draws the panel's own "New Folder" button: a destination is often chosen
        //on a fresh drive, where the folder you want does not exist yet
        panel.canCreateDirectories = canCreateFolder
        return panel.runModal() == .OK ? panel.urls : []
    }
}

//--- device column -----------------------------------------------------------

struct DevicePane: View
{
    let title: String
    let devices: [Device]
    @Binding var selection: Set<UUID>
    @Binding var isTargeted: Bool
    let showsAddTile: Bool
    let emptyHint: String
    let onAdd: () -> Void
    let onDropURL: (URL) -> Void
    var warningFor: (Device) -> String? = { _ in nil }
    var anchorPrefix: String = "device"

    @Environment(\.snapshotRendering) private var snapshotRendering

    private var tiles: some View
    {
        VStack(spacing: 14)
        {
            if showsAddTile
            {
                AddTile(action: onAdd)
            }

            ForEach(Array(devices.enumerated()), id: \.element.id)
            { index, device in
                DeviceTile(device: device,
                           selected: selection.contains(device.id),
                           warning: warningFor(device))
                    .testAnchor("\(anchorPrefix)-tile-\(index)")
                    .onTapGesture
                    {
                        if selection.contains(device.id) { selection.remove(device.id) }
                        else { selection.insert(device.id) }
                    }
            }

            if devices.isEmpty && !showsAddTile
            {
                Text(emptyHint)
                    .font(Theme.rowSmall)
                    .foregroundColor(Theme.textTertiary)
                    .multilineTextAlignment(.center)
                    .padding(.horizontal, 18)
            }
        }
        .padding(.horizontal, 10)
        .padding(.bottom, 16)
        .frame(maxWidth: .infinity, alignment: .top)
    }

    var body: some View
    {
        VStack(spacing: 0)
        {
            Text(title)
                .font(Theme.paneHeader)
                .foregroundColor(Theme.textSecondary)
                .padding(.top, 12)
                .padding(.bottom, 10)

            if snapshotRendering
            {
                tiles
            }
            else
            {
                ScrollView { tiles }
            }

            Spacer(minLength: 0)
        }
        .background(Theme.paneBG)
        .overlay(
            Rectangle()
                .strokeBorder(isTargeted ? Theme.accent.opacity(0.8) : .clear, lineWidth: 2)
        )
        .onDrop(of: [.fileURL], isTargeted: $isTargeted) { providers in
            for provider in providers
            {
                _ = provider.loadDataRepresentation(forTypeIdentifier: UTType.fileURL.identifier)
                { data, _ in
                    guard let data, let url = URL(dataRepresentation: data, relativeTo: nil) else { return }
                    DispatchQueue.main.async { onDropURL(url) }
                }
            }
            return true
        }
    }
}

struct AddTile: View
{
    let action: () -> Void

    var body: some View
    {
        Button(action: action)
        {
            VStack(spacing: 0)
            {
                ZStack
                {
                    RoundedRectangle(cornerRadius: 6)
                        .strokeBorder(Theme.stroke, style: StrokeStyle(lineWidth: 1, dash: [4, 3]))
                        .frame(width: 62, height: 48)

                    Image(systemName: "plus")
                        .font(.system(size: 13, weight: .light))
                        .foregroundColor(Theme.textTertiary)
                }
            }
            .frame(width: 96, height: 62)
            .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
        .help("Add a folder or drive")
    }
}

struct DeviceTile: View
{
    let device: Device
    let selected: Bool
    var warning: String? = nil

    private var ringColor: Color
    {
        if warning != nil { return Theme.amber }
        return selected ? Theme.accent.opacity(0.9) : .clear
    }

    var body: some View
    {
        VStack(spacing: 5)
        {
            DeviceGlyph(kind: device.kind, width: 60, height: 47)

            Text(device.name)
                .font(Theme.deviceName)
                .foregroundColor(selected ? Theme.textPrimary : Theme.textSecondary)
                .lineLimit(1)
                .truncationMode(.middle)

            if let warning
            {
                Text(warning)
                    .font(Theme.deviceSub)
                    .foregroundColor(Theme.amber)
                    .lineLimit(1)
                    .minimumScaleFactor(0.8)
            }
            else
            {
                Text(device.capacityText)
                    .font(Theme.deviceSub)
                    .foregroundColor(Theme.textTertiary)
            }
        }
        .frame(width: 104)
        .padding(.vertical, 7)
        .background(
            RoundedRectangle(cornerRadius: 8)
                .fill(selected ? Color.white.opacity(0.05) : (warning != nil ? Theme.amber.opacity(0.06) : .clear))
        )
        .overlay(
            RoundedRectangle(cornerRadius: 8)
                .strokeBorder(ringColor, lineWidth: 1)
        )
        .contentShape(Rectangle())
    }
}

//--- device artwork ----------------------------------------------------------
//Original shapes: a folder, an SD-style card, and a hard-disk enclosure.

struct DeviceGlyph: View
{
    let kind: DeviceKind
    var width: CGFloat = 60
    var height: CGFloat = 47

    var body: some View
    {
        switch kind
        {
        case .folder: folder
        case .card:   card
        case .drive:  drive
        }
    }

    private var folder: some View
    {
        ZStack(alignment: .topLeading)
        {
            RoundedRectangle(cornerRadius: 5)
                .fill(LinearGradient(colors: [Color(hex: 0x7CBCF4), Color(hex: 0x3B86D6)],
                                     startPoint: .top, endPoint: .bottom))
                .frame(width: width, height: height * 0.80)
                .offset(y: height * 0.20)

            RoundedRectangle(cornerRadius: 3)
                .fill(LinearGradient(colors: [Color(hex: 0x96CCF8), Color(hex: 0x6BAEEA)],
                                     startPoint: .top, endPoint: .bottom))
                .frame(width: width * 0.46, height: height * 0.26)
                .offset(x: 2, y: height * 0.06)
        }
        .frame(width: width, height: height, alignment: .topLeading)
        .shadow(color: .black.opacity(0.45), radius: 3, x: 0, y: 2)
    }

    private var card: some View
    {
        ZStack(alignment: .topLeading)
        {
            RoundedRectangle(cornerRadius: 4)
                .fill(LinearGradient(colors: [Color(hex: 0xF2C64E), Color(hex: 0xD4951A)],
                                     startPoint: .topLeading, endPoint: .bottomTrailing))
                .frame(width: width * 0.72, height: height * 0.94)

            //white label band, like the write-protect sticker on a real card
            RoundedRectangle(cornerRadius: 1.5)
                .fill(Color(hex: 0xF4F2EC).opacity(0.92))
                .frame(width: width * 0.52, height: height * 0.26)
                .offset(x: width * 0.10, y: height * 0.10)

            //contact strip
            VStack(spacing: 1.5)
            {
                ForEach(0..<3, id: \.self) { _ in
                    RoundedRectangle(cornerRadius: 0.5)
                        .fill(Color.black.opacity(0.35))
                        .frame(width: width * 0.34, height: 1.6)
                }
            }
            .offset(x: width * 0.19, y: height * 0.68)
        }
        .frame(width: width, height: height)
        .shadow(color: .black.opacity(0.45), radius: 3, x: 0, y: 2)
    }

    private var drive: some View
    {
        ZStack(alignment: .topLeading)
        {
            RoundedRectangle(cornerRadius: 4)
                .fill(LinearGradient(colors: [Color(hex: 0xEDEFF2), Color(hex: 0xAEB4BB)],
                                     startPoint: .topLeading, endPoint: .bottomTrailing))
                .frame(width: width * 0.94, height: height * 0.92)

            RoundedRectangle(cornerRadius: 4)
                .fill(Color(hex: 0xD3D7DC))
                .frame(width: width * 0.94, height: height * 0.20)

            HStack(spacing: 3)
            {
                Circle().fill(Theme.green.opacity(0.9)).frame(width: 3.5, height: 3.5)
                Capsule().fill(Color.black.opacity(0.10)).frame(width: width * 0.30, height: 2)
            }
            .offset(x: width * 0.08, y: height * 0.68)
        }
        .frame(width: width, height: height)
        .shadow(color: .black.opacity(0.5), radius: 3, x: 0, y: 2)
    }
}

//--- transfers ---------------------------------------------------------------

struct TransfersPane: View
{
    @ObservedObject var model: AppModel
    @ObservedObject private var updater = Updater.shared
    @Environment(\.snapshotRendering) private var snapshotRendering

    private var rows: some View
    {
        VStack(spacing: 0)
        {
            ForEach(Array(model.transfers.enumerated()), id: \.element.id)
            { index, transfer in
                TransferRow(transfer: transfer, index: index)
                Rectangle().fill(Theme.strokeSoft).frame(height: 1)
                    .padding(.leading, 66)
            }
        }
        .padding(.top, 4)
    }

    var body: some View
    {
        VStack(spacing: 0)
        {
            HStack(spacing: 0)
            {
                Spacer()
                Text("Transfers")
                    .font(Theme.paneHeader)
                    .foregroundColor(Theme.textSecondary)
                Spacer()
            }
            .overlay(alignment: .trailing)
            {
                HStack(spacing: 10)
                {
                    //Appears only when a row has finished: a permanently dimmed
                    //button in the header is clutter, and this one has one job.
                    if model.finishedCount > 0
                    {
                        Button(action: model.clearFinished)
                        {
                            Text("Clear Finished")
                                .font(.system(size: 11, weight: .medium))
                                .foregroundColor(Theme.textSecondary)
                                .padding(.horizontal, 9)
                                .padding(.vertical, 4)
                                .background(
                                    RoundedRectangle(cornerRadius: 6).fill(Theme.buttonBG2)
                                )
                        }
                        .buttonStyle(.plain)
                        .testAnchor("clear-finished")
                        .help("Remove the transfers that completed or were cancelled")
                    }

                    Image(systemName: "chevron.down")
                        .font(.system(size: 9, weight: .semibold))
                        .foregroundColor(Theme.textTertiary)
                }
                .padding(.trailing, 16)
            }
            .padding(.top, 12)
            .padding(.bottom, 10)

            Rectangle().fill(Theme.strokeSoft).frame(height: 1)

            if model.transfers.isEmpty
            {
                emptyState
            }
            else if snapshotRendering
            {
                rows
                Spacer(minLength: 0)
            }
            else
            {
                ScrollView { rows }
            }

            Rectangle().fill(Theme.strokeSoft).frame(height: 1)

            settingsRow

            Rectangle().fill(Theme.strokeSoft).frame(height: 1)

            footer
        }
        .background(Theme.columnBG)
        //A measurement taken with the old patterns is not an answer about the
        //next run, so the fit check is re-taken the moment the Skip field moves.
        .onChange(of: model.excludeText)
        { _ in model.excludeFilterChanged() }
    }

    ///Two fields that shape every queued run: the folder to create inside each
    ///destination, and what to leave behind. Both may stay blank, and blank means
    ///exactly what this window did before they existed.
    private var settingsRow: some View
    {
        HStack(spacing: 8)
        {
            settingField(icon: "folder",
                         placeholder: "Subfolder \u{00B7} %Date% works",
                         text: $model.subfolderTemplate,
                         anchor: "subfolder-field",
                         help: "Folder to create under each destination. Blank uses the card's own name. " +
                               "%Date% (2026-10-26), %Year%, %Month%, %Day%, %Card%. A slash nests folders.")

            settingField(icon: "line.3.horizontal.decrease",
                         placeholder: "Skip \u{00B7} *.LRV | MISC/",
                         text: $model.excludeText,
                         anchor: "skip-field",
                         help: "What not to copy, separated by |. * and ? are wildcards, a trailing / skips a whole " +
                               "folder, and matching ignores case. Same patterns as --exclude on the command line.")
        }
        .padding(.horizontal, 16)
        .padding(.vertical, 8)
    }

    private func settingField(icon: String, placeholder: String, text: Binding<String>,
                              anchor: String, help: String) -> some View
    {
        HStack(spacing: 5)
        {
            Image(systemName: icon)
                .font(.system(size: 9, weight: .medium))
                .foregroundColor(Theme.textTertiary)

            TextField(placeholder, text: text)
                .textFieldStyle(.plain)
                .font(.system(size: 11))
                .foregroundColor(Theme.textPrimary)
        }
        .padding(.horizontal, 7)
        .padding(.vertical, 4)
        .background(RoundedRectangle(cornerRadius: 6).fill(Theme.buttonBG2))
        .help(help)
        .testAnchor(anchor)
    }

    private var addTitle: String
    {
        let n = model.plannedTransferCount
        return n > 0 ? "Add \(n) Transfer\(n == 1 ? "" : "s")" : "Add Transfer"
    }

    ///One accent button at a time: while rows are waiting to go, the thing to
    ///press is Transfer, so Add steps back to a neutral fill.
    private var addIsAccent: Bool { model.canAddTransfer && model.queuedCount == 0 }

    private var startTitle: String
    {
        let n = model.queuedCount
        return n > 1 ? "Transfer \(n)" : "Transfer"
    }

    private var emptyState: some View
    {
        VStack(spacing: 10)
        {
            Spacer()
            Image(systemName: "arrow.right.square")
                .font(.system(size: 26, weight: .ultraLight))
                .foregroundColor(Theme.textTertiary)
            Text("No transfers yet")
                .font(Theme.rowTitle)
                .foregroundColor(Theme.textSecondary)
            Text("Pick a source on the left, one or more destinations on the right,\nthen Add Transfer to queue it, and Transfer to start.")
                .font(Theme.rowSmall)
                .foregroundColor(Theme.textTertiary)
                .multilineTextAlignment(.center)
            Spacer()
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .padding(.horizontal, 30)
    }

    private var footer: some View
    {
        HStack(spacing: 12)
        {
            Button(action: model.addTransfer)
            {
                Text(addTitle)
                    .font(.system(size: 11, weight: .medium))
                    .foregroundColor(model.canAddTransfer ? .white : Theme.textTertiary)
                    .padding(.horizontal, 12)
                    .padding(.vertical, 6)
                    .background(
                        RoundedRectangle(cornerRadius: 6)
                            .fill(addIsAccent ? Theme.accent : Theme.buttonBG2)
                    )
            }
            .buttonStyle(.plain)
            .disabled(!model.canAddTransfer)
            .testAnchor("add-transfers")

            //Appears only when something is waiting: Add plans, this copies.
            if model.canStartTransfer
            {
                Button(action: model.startQueuedTransfers)
                {
                    Text(startTitle)
                        .font(.system(size: 11, weight: .semibold))
                        .foregroundColor(.white)
                        .padding(.horizontal, 12)
                        .padding(.vertical, 6)
                        .background(RoundedRectangle(cornerRadius: 6).fill(Theme.accent))
                }
                .buttonStyle(.plain)
                .testAnchor("start-transfers")
                .help("Start the queued transfers")
            }

            if !model.fitSummary.text.isEmpty
            {
                Text(model.fitSummary.text)
                    .font(Theme.rowSmall)
                    .foregroundColor(model.fitSummary.isWarning ? Theme.amber : Theme.textTertiary)
                    .lineLimit(1)
                    .truncationMode(.tail)
            }

            Spacer(minLength: 8)

            updateChip

            HStack(spacing: 7)
            {
                Text("Paused")
                    .font(Theme.rowSmall)
                    .foregroundColor(Theme.textSecondary)

                PauseSwitch(isOn: model.allPaused) { model.togglePauseAll() }
                    .testAnchor("pause-all")
            }
        }
        .padding(.horizontal, 16)
        .padding(.vertical, 9)
    }

    ///Only appears when there is something to say: an update to install, a download
    ///in flight, or a failure worth reading. A permanent "You are up to date" would
    ///just take up the footer.
    @ViewBuilder
    private var updateChip: some View
    {
        switch updater.phase
        {
        case .available(let release):
            Button(action: { updater.install(release) })
            {
                Text("Update to \(release.version.raw)")
                    .font(.system(size: 11, weight: .medium))
                    .foregroundColor(.white)
                    .padding(.horizontal, 10)
                    .padding(.vertical, 5)
                    .background(RoundedRectangle(cornerRadius: 6).fill(Theme.accent))
            }
            .buttonStyle(.plain)
            .testAnchor("update-chip")

        case .downloading(_, let fraction):
            Text("Downloading update \(Int((fraction * 100).rounded()))%")
                .font(Theme.rowSmall)
                .foregroundColor(Theme.textSecondary)

        case .installing(let release):
            Text("Restarting into \(release.version.raw)…")
                .font(Theme.rowSmall)
                .foregroundColor(Theme.textSecondary)

        case .failed(let why):
            Text(why)
                .font(Theme.rowSmall)
                .foregroundColor(Theme.amber)
                .lineLimit(1)
                .truncationMode(.tail)
                .frame(maxWidth: 320, alignment: .trailing)

        case .idle, .checking, .upToDate:
            EmptyView()
        }
    }
}

struct TransferRow: View
{
    @ObservedObject var transfer: Transfer
    var index: Int = 0

    var body: some View
    {
        HStack(alignment: .center, spacing: 14)
        {
            DeviceGlyph(kind: transfer.source.kind, width: 38, height: 30)

            VStack(alignment: .leading, spacing: 6)
            {
                HStack(spacing: 5)
                {
                    Text(transfer.source.name)
                        .font(Theme.rowTitle)
                        .foregroundColor(Theme.textSecondary)
                        .lineLimit(1)

                    Image(systemName: "arrow.right")
                        .font(.system(size: 8, weight: .semibold))
                        .foregroundColor(Theme.textTertiary)

                    Text(transfer.destination.name)
                        .font(Theme.rowBold)
                        .foregroundColor(Theme.textPrimary)
                        .lineLimit(1)

                    if !transfer.subfolder.isEmpty
                    {
                        Text("· " + transfer.subfolder)
                            .font(Theme.rowTitle)
                            .foregroundColor(Theme.textSecondary)
                            .lineLimit(1)
                    }
                    Spacer(minLength: 0)
                }

                ProgressBarView(fraction: transfer.fraction, colors: transfer.barColors)

                HStack(spacing: 6)
                {
                    Text(leadingText)
                        .font(Theme.rowSmall)
                        .foregroundColor(transfer.phase == .done ? Theme.green : Theme.textTertiary)
                        .lineLimit(1)
                        .truncationMode(.middle)

                    Spacer(minLength: 0)

                    if showsTrailing
                    {
                        Text(transfer.percentText)
                            .font(Theme.rowSmall)
                            .foregroundColor(Theme.textTertiary)
                            .monospacedDigit()
                            .lineLimit(1)
                    }
                }

                //Rate, items/sec and the counts: the row is the only place a
                //long transfer reports itself, so it says how fast and how much.
                if !transfer.statsText.isEmpty
                {
                    Text(transfer.statsText)
                        .font(Theme.rowSmall)
                        .foregroundColor(Theme.textTertiary)
                        .monospacedDigit()
                        .lineLimit(1)
                        .truncationMode(.tail)
                }
            }

            HStack(spacing: 5)
            {
                CircleButton(system: "xmark") { transfer.cancel() }
                    .testAnchor("row-\(index)-cancel")
                    .help(transfer.isQueued ? "Remove this queued transfer" : "Cancel this transfer")
                //A queued row has no engine job, so a pause control there would
                //be a button that does nothing.
                if !transfer.isQueued
                {
                    PausePill(paused: transfer.paused) { transfer.togglePause() }
                        .testAnchor("row-\(index)-pause")
                        .help(transfer.paused ? "Continue this transfer" : "Pause this transfer")
                }
            }
        }
        .padding(.horizontal, 18)
        .padding(.vertical, 13)
        .opacity(transfer.phase == .cancelled ? 0.55 : 1)
    }

    private var leadingText: String
    {
        switch transfer.phase
        {
        case .copying, .verifying:
            return transfer.paused ? "Paused · \(transfer.currentFile)" : transfer.currentFile
        default:
            return transfer.statusText
        }
    }

    private var showsTrailing: Bool
    {
        switch transfer.phase
        {
        case .copying, .verifying: return true
        default: return false
        }
    }
}

///What a finished run leaves behind: the copies it made, the rate they actually
///ran at, and where the record went. A sheet rather than an alert, because the
///numbers are the point and an alert cannot lay them out.
struct RunSummaryView: View
{
    let summary: RunSummary
    let onClose: () -> Void

    private var headline: String
    {
        summary.failed > 0
            ? "Transfer finished with \(summary.failed) damaged file\(summary.failed == 1 ? "" : "s")"
            : "Transfer complete"
    }

    ///`221 files · 21.4 GB · 1.26 GB/sec · in 0.49 sec`
    private var totals: String
    {
        var parts = ["\(Format.number(summary.files)) file\(summary.files == 1 ? "" : "s")",
                     Format.capacity(summary.bytes)]
        if summary.rate > 0 { parts.append(Format.rate(summary.rate)) }
        let took = Format.duration(summary.seconds)
        if !took.isEmpty { parts.append("in \(took)") }
        return parts.joined(separator: " · ")
    }

    var body: some View
    {
        VStack(alignment: .leading, spacing: 0)
        {
            VStack(alignment: .leading, spacing: 6)
            {
                Text(headline)
                    .font(.system(size: 15, weight: .semibold))
                    .foregroundColor(summary.failed > 0 ? Theme.amber : Theme.green)
                Text(totals)
                    .font(Theme.rowTitle)
                    .foregroundColor(Theme.textSecondary)
                    .monospacedDigit()
            }
            .frame(maxWidth: .infinity, alignment: .leading)
            .padding(.horizontal, 20)
            .padding(.top, 18)
            .padding(.bottom, 14)

            Rectangle().fill(Theme.strokeSoft).frame(height: 1)

            ScrollView
            {
                VStack(spacing: 0)
                {
                    ForEach(summary.records)
                    { record in
                        row(record)
                        Rectangle().fill(Theme.strokeSoft).frame(height: 1)
                            .padding(.leading, 20)
                    }
                }
            }
            .frame(maxHeight: 260)

            Rectangle().fill(Theme.strokeSoft).frame(height: 1)

            HStack(spacing: 10)
            {
                Text("Reports are in Pictures › Offload Reports")
                    .font(Theme.rowSmall)
                    .foregroundColor(Theme.textTertiary)
                    .lineLimit(1)
                Spacer(minLength: 8)
                Button(action: onClose)
                {
                    Text("Done")
                        .font(.system(size: 11, weight: .semibold))
                        .foregroundColor(.white)
                        .padding(.horizontal, 16)
                        .padding(.vertical, 6)
                        .background(RoundedRectangle(cornerRadius: 6).fill(Theme.accent))
                }
                .buttonStyle(.plain)
                .keyboardShortcut(.defaultAction)
                .testAnchor("summary-done")
            }
            .padding(.horizontal, 20)
            .padding(.vertical, 12)
        }
        .frame(width: 540)
        .background(Theme.columnBG)
        .preferredColorScheme(.dark)
        .testAnchor("run-summary")
    }

    private func row(_ record: TransferRecord) -> some View
    {
        HStack(alignment: .top, spacing: 12)
        {
            VStack(alignment: .leading, spacing: 3)
            {
                Text(record.destination)
                    .font(Theme.rowBold)
                    .foregroundColor(Theme.textPrimary)
                    .lineLimit(1)
                Text(record.source)
                    .font(Theme.rowSmall)
                    .foregroundColor(Theme.textTertiary)
                    .lineLimit(1)
            }

            Spacer(minLength: 8)

            VStack(alignment: .trailing, spacing: 3)
            {
                Text(line(for: record))
                    .font(Theme.rowSmall)
                    .foregroundColor(record.failed > 0 ? Theme.amber : Theme.textSecondary)
                    .monospacedDigit()
                Text(reportFolder(record))
                    .font(Theme.rowSmall)
                    .foregroundColor(Theme.textTertiary)
                    .lineLimit(1)
                    .truncationMode(.middle)
            }
        }
        .padding(.horizontal, 20)
        .padding(.vertical, 11)
    }

    ///The report's own folder: the run-stamped name. The file inside it is always
    ///report.csv, so naming that would say nothing about which run it belongs to.
    private func reportFolder(_ record: TransferRecord) -> String
    {
        guard !record.reportPath.isEmpty else { return "" }
        return ((record.reportPath as NSString).deletingLastPathComponent as NSString).lastPathComponent
    }

    private func line(for record: TransferRecord) -> String
    {
        var parts = ["\(Format.number(record.files)) file\(record.files == 1 ? "" : "s")",
                     Format.capacity(record.bytes)]
        if record.rate > 0 { parts.append(Format.rate(record.rate)) }
        if record.failed > 0 { parts.append("\(record.failed) damaged") }
        return parts.joined(separator: " · ")
    }
}

struct ProgressBarView: View
{
    let fraction: Double
    let colors: [Color]

    var body: some View
    {
        GeometryReader
        { geo in
            ZStack(alignment: .leading)
            {
                Capsule().fill(Theme.track)

                Capsule()
                    .fill(LinearGradient(colors: colors, startPoint: .leading, endPoint: .trailing))
                    .frame(width: max(geo.size.width * CGFloat(min(max(fraction, 0), 1)),
                                      fraction > 0 ? 7 : 0))
            }
        }
        .frame(height: 7)
    }
}

///Pause/resume as a labelled pill rather than a bare glyph: on a running row the
///word is the thing you look for, and an 8-point triangle is not.
struct PausePill: View
{
    let paused: Bool
    let action: () -> Void

    var body: some View
    {
        Button(action: action)
        {
            HStack(spacing: 4)
            {
                Image(systemName: paused ? "play.fill" : "pause.fill")
                    .font(.system(size: 7, weight: .bold))

                Text(paused ? "Continue" : "Pause")
                    .font(Theme.rowSmall)
            }
            .foregroundColor(Theme.textSecondary)
            .padding(.horizontal, 8)
            .padding(.vertical, 3)
            .background(Capsule().fill(Theme.buttonBG))
            .contentShape(Capsule())
        }
        .buttonStyle(.plain)
    }
}

struct CircleButton: View
{
    let system: String
    let action: () -> Void

    var body: some View
    {
        Button(action: action)
        {
            ZStack
            {
                Circle().fill(Theme.buttonBG)
                Image(systemName: system)
                    .font(.system(size: 8, weight: .bold))
                    .foregroundColor(Theme.textSecondary)
            }
            .frame(width: 20, height: 20)
            .contentShape(Circle())
        }
        .buttonStyle(.plain)
    }
}

struct PauseSwitch: View
{
    let isOn: Bool
    let action: () -> Void

    var body: some View
    {
        Button(action: action)
        {
            ZStack(alignment: isOn ? .trailing : .leading)
            {
                Capsule()
                    .fill(isOn ? Theme.accent : Theme.buttonBG2)
                    .frame(width: 30, height: 17)

                Circle()
                    .fill(isOn ? .white : Theme.textTertiary)
                    .frame(width: 13, height: 13)
                    .padding(.horizontal, 2)
            }
            .contentShape(Capsule())
        }
        .buttonStyle(.plain)
    }
}
