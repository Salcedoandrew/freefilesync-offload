// *****************************************************************************
// * Offload mode - C ABI over the C++ engine.                                  *
// *                                                                            *
// * The window is a native macOS app; Swift cannot call C++ directly, so this  *
// * is the only boundary between the UI and the engine. Everything here is a  *
// * plain C value or an opaque handle, and no C++ type appears in a signature. *
// *****************************************************************************
#ifndef OFFLOAD_CAPI_H
#define OFFLOAD_CAPI_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

//Mirrors offload::HashAlgo / offload::VerifyMode.
enum
{
    OFFLOAD_HASH_XXH64 = 0,
    OFFLOAD_HASH_MD5   = 1,
    OFFLOAD_HASH_SHA1  = 2,
};

enum
{
    OFFLOAD_VERIFY_SIZE = 0,
    OFFLOAD_VERIFY_HASH = 1,
};

//Mirrors offload::Progress::Phase.
enum
{
    OFFLOAD_PHASE_SCANNING  = 0,
    OFFLOAD_PHASE_COPYING   = 1,
    OFFLOAD_PHASE_VERIFYING = 2,
    OFFLOAD_PHASE_DONE      = 3,
};

typedef struct OffloadJob OffloadJob;

typedef struct
{
    uint64_t bytesDone;      //written, or hashed back, across the whole transfer
    uint64_t bytesTotal;     //planned bytes; 0 until the source has been scanned
    uint64_t currentFileDone;
    uint64_t currentFileSize;
    uint64_t filesDone;
    uint64_t filesTotal;
    uint64_t bytesWritten;   //bytes this run actually put on the destination
    int32_t  phase;          //OFFLOAD_PHASE_*
    int32_t  finished;       //1 once the worker thread returned
    int32_t  ok;             //1 only if finished and every file verified
    int32_t  cancelled;
    uint64_t verifiedCount;
    uint64_t skippedCount;
    uint64_t failedCount;    //failed + mismatch
    uint64_t fileCount;
    double   elapsedSeconds;
} OffloadProgress;

//Starts one source -> destination transfer on its own thread and returns at once.
//reportDir may be NULL (no reports) or a directory that already exists.
//Returns NULL if the source cannot be opened; offload_job_error() then explains why.
OffloadJob* offload_job_start(const char* source, const char* dest, const char* reportDir,
                              int32_t hashAlgo, int32_t verifyMode, int32_t dryRun);

//Copies the latest snapshot into *out. Safe to call from any thread, as often as
//the caller likes; safe before and after the worker finishes.
void offload_job_poll(OffloadJob* job, OffloadProgress* out);

//Relative path of the file being worked on, UTF-8. Valid until the next call.
const char* offload_job_current_file(OffloadJob* job);

//Human-readable reason for a start failure or a failed run, UTF-8. "" when fine.
const char* offload_job_error(OffloadJob* job);

//Path of the CSV report written for a finished job, UTF-8, or "" if none.
const char* offload_job_report_csv(OffloadJob* job);

void offload_job_pause (OffloadJob* job, int32_t paused); //takes effect between blocks
int32_t offload_job_paused(OffloadJob* job);
void offload_job_cancel(OffloadJob* job); //finishes the file in flight, then stops

//Cancels if still running, joins the worker thread and frees the handle. Never
//blocks for longer than the file currently in flight takes to finish.
void offload_job_destroy(OffloadJob* job);

//Bytes and file count a transfer from this folder would carry, using the engine's
//own walk and junk rules. Returns 1 on success, 0 on failure (see
//offload_measure_error). Worth calling on a background thread: it walks the tree.
int32_t offload_source_measure(const char* source, uint64_t* outBytes, uint64_t* outFiles);

//Why the last measure call on this thread failed, UTF-8. "" when it succeeded.
const char* offload_measure_error(void);

//Engine build stamp ("0.2.0"), for the About box and for support reports.
const char* offload_engine_version(void);

#ifdef __cplusplus
}
#endif
#endif
