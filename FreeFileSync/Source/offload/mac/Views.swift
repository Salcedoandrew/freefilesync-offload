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
        for url in openPanel(title: "Choose a destination")
        { model.addDestination(url: url) }
    }

    private func openPanel(title: String) -> [URL]
    {
        let panel = NSOpenPanel()
        panel.title = title
        panel.canChooseDirectories = true
        panel.canChooseFiles = false
        panel.allowsMultipleSelection = true
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
                Image(systemName: "chevron.down")
                    .font(.system(size: 9, weight: .semibold))
                    .foregroundColor(Theme.textTertiary)
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

            footer
        }
        .background(Theme.columnBG)
    }

    private var addTitle: String
    {
        let n = model.plannedTransferCount
        return n > 0 ? "Add \(n) Transfer\(n == 1 ? "" : "s")" : "Add Transfer"
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
            Text("Pick a source on the left, one or more destinations on the right,\nthen add the transfer.")
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
                            .fill(model.canAddTransfer ? Theme.accent : Theme.buttonBG2)
                    )
            }
            .buttonStyle(.plain)
            .disabled(!model.canAddTransfer)
            .testAnchor("add-transfers")

            if !model.fitSummary.text.isEmpty
            {
                Text(model.fitSummary.text)
                    .font(Theme.rowSmall)
                    .foregroundColor(model.fitSummary.isWarning ? Theme.amber : Theme.textTertiary)
                    .lineLimit(1)
                    .truncationMode(.tail)
            }

            Spacer(minLength: 8)

            HStack(spacing: 7)
            {
                Text("Paused")
                    .font(Theme.rowSmall)
                    .foregroundColor(Theme.textSecondary)

                PauseSwitch(isOn: model.allPaused) { model.togglePauseAll() }
            }
        }
        .padding(.horizontal, 16)
        .padding(.vertical, 9)
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
                        Text(transfer.statusText)
                            .font(Theme.rowSmall)
                            .foregroundColor(Theme.textTertiary)
                            .lineLimit(1)
                    }
                }
            }

            HStack(spacing: 5)
            {
                CircleButton(system: "xmark") { transfer.cancel() }
                    .testAnchor("row-\(index)-cancel")
                    .help("Cancel this transfer")
                CircleButton(system: transfer.paused ? "play.fill" : "pause.fill")
                { transfer.togglePause() }
                    .testAnchor("row-\(index)-pause")
                    .help(transfer.paused ? "Resume" : "Pause")
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
