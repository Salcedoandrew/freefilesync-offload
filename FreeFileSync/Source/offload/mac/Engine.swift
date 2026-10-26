// *****************************************************************************
// * Offload window - the only place that talks to the C ABI.                  *
// *****************************************************************************

import Foundation

/// One source -> destination transfer running in the engine.
final class EngineJob
{
    private let handle: OpaquePointer

    init?(source: String, destination: String, reportDir: String,
          hashAlgo: Int32 = Int32(OFFLOAD_HASH_XXH64),
          verifyMode: Int32 = Int32(OFFLOAD_VERIFY_HASH),
          exclude: String = "")
    {
        let created = source.withCString { src in
            destination.withCString { dst in
                reportDir.withCString { rep in
                    exclude.withCString { skip in
                        offload_job_start(src, dst, rep, hashAlgo, verifyMode, 0, skip)
                    }
                }
            }
        }
        guard let created else { return nil }
        handle = created
    }

    deinit { offload_job_destroy(handle) }

    func poll() -> OffloadProgress
    {
        var raw = OffloadProgress()
        offload_job_poll(handle, &raw)
        return raw
    }

    var currentFile: String
    {
        guard let c = offload_job_current_file(handle) else { return "" }
        return String(cString: c)
    }

    var errorText: String
    {
        guard let c = offload_job_error(handle) else { return "" }
        return String(cString: c)
    }

    var reportPath: String
    {
        guard let c = offload_job_report_csv(handle) else { return "" }
        return String(cString: c)
    }

    func setPaused(_ paused: Bool) { offload_job_pause(handle, paused ? 1 : 0) }
    var isPaused: Bool { offload_job_paused(handle) != 0 }
    func cancel() { offload_job_cancel(handle) }

    ///The engine's own cancel flag, which survives the job finishing - so a click
    ///that arrives late still proves the button reached the engine.
    var isCancelled: Bool { poll().cancelled != 0 }

    static var version: String
    {
        guard let c = offload_engine_version() else { return "?" }
        return String(cString: c)
    }
}

///What a transfer out of a folder would actually carry: same walk, same junk rules
///as the copy itself, so the fit check and the job cannot disagree.
struct SourceMeasurement
{
    let bytes: UInt64
    let files: UInt64
}

struct MeasureFailure: Error
{
    let message: String
}

enum SourceMeasure
{
    ///`exclude` is the Skip field's text. Passing it is not optional politeness:
    ///the fit check has to count what the job will copy, so a filtered run and the
    ///number beside the destination must come from the same patterns.
    static func measure(_ path: String, exclude: String = "") -> Result<SourceMeasurement, MeasureFailure>
    {
        var bytes: UInt64 = 0
        var files: UInt64 = 0
        let ok = path.withCString
        { src in
            exclude.withCString { skip in
                offload_source_measure(src, skip, &bytes, &files)
            }
        }
        guard ok != 0 else
        {
            let raw = offload_measure_error().map { String(cString: $0) } ?? ""
            return .failure(MeasureFailure(message: raw.isEmpty ? "cannot measure the source" : raw))
        }
        return .success(SourceMeasurement(bytes: bytes, files: files))
    }
}
