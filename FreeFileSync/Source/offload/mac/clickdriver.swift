// *****************************************************************************
// * Offload window - external mouse driver.                                   *
// *                                                                           *
// * SwiftUI ignores synthetic NSEvents handed to its hosting view, so this     *
// * posts real CGEvents at the positions the window published, then checks     *
// * what changed in that published state. Needs Accessibility permission for   *
// * the process that posts (System Settings -> Privacy & Security).            *
// *                                                                           *
// *   clickdriver --state /tmp/offload-ui.json                                 *
// *****************************************************************************

import AppKit
import CoreGraphics

struct Snapshot
{
    var anchors: [String: CGPoint] = [:]
    var selectedSources = 0
    var selectedDestinations = 0
    var canAddTransfer = false
    var allPaused = false
    var transfers: [[String: Any]] = []
    var pid: pid_t = 0

    func flag(_ row: Int, _ key: String) -> Bool
    {
        guard row < transfers.count else { return false }
        return (transfers[row][key] as? Bool) ?? false
    }

    func text(_ row: Int, _ key: String) -> String
    {
        guard row < transfers.count else { return "" }
        return (transfers[row][key] as? String) ?? ""
    }
}

func loadState(_ path: String) -> Snapshot?
{
    guard let data = try? Data(contentsOf: URL(fileURLWithPath: path)),
          let root = try? JSONSerialization.jsonObject(with: data) as? [String: Any]
    else { return nil }

    var snapshot = Snapshot()
    snapshot.selectedSources = root["selectedSources"] as? Int ?? 0
    snapshot.selectedDestinations = root["selectedDestinations"] as? Int ?? 0
    snapshot.canAddTransfer = root["canAddTransfer"] as? Bool ?? false
    snapshot.allPaused = root["allPaused"] as? Bool ?? false
    snapshot.transfers = root["transfers"] as? [[String: Any]] ?? []
    snapshot.pid = pid_t(root["pid"] as? Int ?? 0)

    if let anchors = root["anchors"] as? [String: [String: Double]]
    {
        for (id, rect) in anchors
        {
            if let x = rect["x"], let y = rect["y"] { snapshot.anchors[id] = CGPoint(x: x, y: y) }
        }
    }
    return snapshot
}

func waitForState(_ path: String, timeout: Double = 15,
                  _ predicate: (Snapshot) -> Bool) -> Snapshot?
{
    let deadline = Date().addingTimeInterval(timeout)
    while Date() < deadline
    {
        if let snapshot = loadState(path), predicate(snapshot) { return snapshot }
        Thread.sleep(forTimeInterval: 0.1)
    }
    return loadState(path)
}

///A real hardware-level click at a global display point (top-left origin).
func click(_ point: CGPoint)
{
    let source = CGEventSource(stateID: .hidSystemState)
    let move = CGEvent(mouseEventSource: source, mouseType: .mouseMoved,
                       mouseCursorPosition: point, mouseButton: .left)
    let down = CGEvent(mouseEventSource: source, mouseType: .leftMouseDown,
                       mouseCursorPosition: point, mouseButton: .left)
    let up = CGEvent(mouseEventSource: source, mouseType: .leftMouseUp,
                     mouseCursorPosition: point, mouseButton: .left)
    move?.post(tap: .cghidEventTap)
    Thread.sleep(forTimeInterval: 0.05)
    down?.post(tap: .cghidEventTap)
    Thread.sleep(forTimeInterval: 0.08)
    up?.post(tap: .cghidEventTap)
}

let args = CommandLine.arguments
guard let flagIndex = args.firstIndex(of: "--state"), flagIndex + 1 < args.count else
{
    print("usage: clickdriver --state <state.json>")
    exit(2)
}
let statePath = args[flagIndex + 1]

guard let firstState = waitForState(statePath,
                                   { $0.anchors["src-tile-0"] != nil && $0.anchors["dst-tile-1"] != nil })
else
{
    print("FAIL: the window never published a state file at \(statePath)")
    exit(2)
}
var snapshot = firstState

print("driver: app pid \(snapshot.pid), \(snapshot.anchors.count) click targets published")
NSRunningApplication(processIdentifier: snapshot.pid)?.activate(options: [.activateIgnoringOtherApps])
Thread.sleep(forTimeInterval: 0.8)

var checks = 0
var failures = 0

func check(_ ok: Bool, _ what: String)
{
    checks += 1
    if ok { print("   ok   \(what)") }
    else  { failures += 1; print("   FAIL \(what)") }
}

func clickAnchor(_ id: String)
{
    guard let point = loadState(statePath)?.anchors[id] else
    {
        print("   FAIL no click target named \(id)")
        failures += 1
        return
    }
    NSRunningApplication(processIdentifier: snapshot.pid)?.activate(options: [.activateIgnoringOtherApps])
    Thread.sleep(forTimeInterval: 0.15)
    click(point)
    Thread.sleep(forTimeInterval: 0.55)
    print(String(format: "   click %@ at (%.0f, %.0f)", id, point.x, point.y))
}

//--- 0. normalise: the panes preselect devices when they are dropped in -------
snapshot = loadState(statePath) ?? snapshot
var guardCount = 0
while (snapshot.selectedSources > 0 || snapshot.selectedDestinations > 0) && guardCount < 6
{
    if snapshot.selectedSources > 0 { clickAnchor("src-tile-0") }
    if let current = loadState(statePath), current.selectedDestinations > 0
    {
        clickAnchor("dst-tile-0")
        if let again = loadState(statePath), again.selectedDestinations > 0 { clickAnchor("dst-tile-1") }
    }
    snapshot = loadState(statePath) ?? snapshot
    guardCount += 1
}
check(snapshot.selectedSources == 0 && snapshot.selectedDestinations == 0,
      "clicks cleared the preselected devices")

//--- 1. source tile ----------------------------------------------------------
clickAnchor("src-tile-0")
check(loadState(statePath)?.selectedSources == 1, "clicking a source tile selects it")

clickAnchor("src-tile-0")
check(loadState(statePath)?.selectedSources == 0, "clicking it again deselects it")

clickAnchor("src-tile-0")

//--- 2. destinations ---------------------------------------------------------
clickAnchor("dst-tile-0")
check(loadState(statePath)?.selectedDestinations == 1, "clicking a destination tile selects it")

clickAnchor("dst-tile-1")
check(loadState(statePath)?.selectedDestinations == 2, "a second destination adds to the selection")

//--- 3. Add queues, Transfer starts ------------------------------------------
//The run is started with the footer's Pause switch already on, so both jobs come
//up paused. Otherwise the next click races the copy: the fixture is 48 MB per
//destination and can finish in under a second, which would leave nothing running
//to pause and make the checks below depend on timing.
clickAnchor("pause-all")
check(loadState(statePath)?.allPaused == true, "the footer's Pause switch holds the next run back")

clickAnchor("add-transfers")
snapshot = loadState(statePath) ?? snapshot
check(snapshot.transfers.count == 2, "Add Transfers created one row per destination (got \(snapshot.transfers.count))")
check(snapshot.text(0, "phase") == "queued", "a new row waits for the Transfer button (phase \(snapshot.text(0, "phase")))")
check(snapshot.flag(0, "enginePaused") == false, "a queued row has no engine job to pause")

clickAnchor("start-transfers")
snapshot = loadState(statePath) ?? snapshot
check(snapshot.text(0, "phase") != "queued", "Transfer handed the row to the engine (phase \(snapshot.text(0, "phase")))")
check(snapshot.flag(0, "enginePaused") == true, "it started paused, because the Pause switch was on")

//--- 4. per-row resume, pause, cancel --------------------------------------
clickAnchor("row-0-pause")
check(loadState(statePath)?.flag(0, "enginePaused") == false, "the row's pause button resumed that job in the engine")

clickAnchor("row-0-pause")
check(loadState(statePath)?.flag(0, "enginePaused") == true, "clicking it again paused the job")

clickAnchor("row-0-cancel")
check(loadState(statePath)?.flag(0, "engineCancelled") == true, "the row's cancel button reached the engine")

//--- 5. Add with nothing selected does nothing --------------------------------
let rowsBefore = loadState(statePath)?.transfers.count ?? 0
clickAnchor("dst-tile-0")
clickAnchor("dst-tile-1")
check(loadState(statePath)?.selectedDestinations == 0, "both destinations are now deselected")

clickAnchor("add-transfers")
check(loadState(statePath)?.transfers.count == rowsBefore, "Add does nothing when nothing is selected")

clickAnchor("dst-tile-0")
check(loadState(statePath)?.selectedDestinations == 1, "the window still responds after that")

print("")
print("\(checks - failures)/\(checks) mouse checks passed")
exit(failures == 0 ? 0 : 1)
