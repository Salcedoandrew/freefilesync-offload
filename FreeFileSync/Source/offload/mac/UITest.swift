// *****************************************************************************
// * Offload window - click targets and observable state, for the mouse test.   *
// *                                                                            *
// * SwiftUI does not act on synthetic NSEvents handed to its hosting view, so   *
// * the test clicks from outside with real CGEvents. Two halves make that      *
// * possible and reliable:                                                     *
// *   - every clickable view publishes its frame here, so the driver never      *
// *     guesses a control's position;                                          *
// *   - the app writes those frames plus its state to a JSON file, so the       *
// *     driver can both aim and check what the click actually did.              *
// *****************************************************************************

import SwiftUI
import AppKit

struct AnchorFrames: PreferenceKey
{
    static let defaultValue: [String: CGRect] = [:]

    static func reduce(value: inout [String: CGRect], nextValue: () -> [String: CGRect])
    {
        value.merge(nextValue()) { _, new in new }
    }
}

extension View
{
    ///Publishes this view's frame under `id`. A transparent GeometryReader per
    ///interactive element is the whole cost.
    @ViewBuilder
    func testAnchor(_ id: String?) -> some View
    {
        if let id
        {
            background(
                GeometryReader { geo in
                    Color.clear.preference(key: AnchorFrames.self, value: [id: geo.frame(in: .global)])
                }
            )
        }
        else
        {
            self
        }
    }
}

///Writes what the window is showing where a driver can read it.
@MainActor
enum UIStateWriter
{
    static func write(model: AppModel, window: NSWindow, to path: String)
    {
        let contentHeight = window.contentView?.bounds.height ?? window.frame.height
        let screenHeight = NSScreen.screens.first?.frame.height ?? 0

        var anchors: [String: [String: Double]] = [:]
        for (id, rect) in model.anchorFrames
        {
            //SwiftUI: window content origin, top-left. CGEvent: global display,
            //top-left. Flip through AppKit's window coordinates to get there.
            let inWindow = CGPoint(x: rect.midX, y: contentHeight - rect.midY)
            let screen = window.convertPoint(toScreen: inWindow)
            anchors[id] = ["x": screen.x,
                           "y": screenHeight - screen.y,
                           "w": rect.width,
                           "h": rect.height]
        }

        let transfers: [[String: Any]] = model.transfers.map
        { t in
            ["destination": t.destination.name,
             "phase": String(describing: t.phase),
             "barFraction": t.fraction,
             "pausedPublished": t.paused,
             "enginePaused": t.job?.isPaused ?? false,
             "engineCancelled": t.job?.isCancelled ?? false,
             "filesDone": t.filesDone,
             "verified": t.verifiedCount]
        }

        let payload: [String: Any] = [
            "pid": ProcessInfo.processInfo.processIdentifier,
            "window": ["x": window.frame.origin.x, "y": window.frame.origin.y,
                       "w": window.frame.width, "h": window.frame.height],
            "anchors": anchors,
            "selectedSources": model.selectedSources.count,
            "selectedDestinations": model.selectedDestinations.count,
            "plannedTransfers": model.plannedTransferCount,
            "canAddTransfer": model.canAddTransfer,
            "canStartTransfer": model.canStartTransfer,
            "queuedTransfers": model.queuedCount,
            "fitSummary": model.fitSummary.text,
            "transfers": transfers,
        ]

        guard let data = try? JSONSerialization.data(withJSONObject: payload,
                                                     options: [.prettyPrinted, .sortedKeys])
        else { return }
        try? data.write(to: URL(fileURLWithPath: path))
    }

    ///The window's own view hierarchy, so no screen-recording permission is needed.
    static func capture(_ window: NSWindow, to path: String) -> Bool
    {
        guard let view = window.contentView,
              let rep = view.bitmapImageRepForCachingDisplay(in: view.bounds) else { return false }
        view.cacheDisplay(in: view.bounds, to: rep)
        guard let png = rep.representation(using: .png, properties: [:]) else { return false }
        do { try png.write(to: URL(fileURLWithPath: path)); return true }
        catch { return false }
    }
}
