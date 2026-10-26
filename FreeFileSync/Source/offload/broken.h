// *****************************************************************************
// * Offload mode for FreeFileSync - broken-file check.                         *
// *                                                                            *
// * A camera card that was pulled mid-write, or a card going bad, often holds  *
// * files whose headers are garbage while the rest of the bytes are fine.      *
// * This check reads only the head of the file (plus the last 12 bytes for     *
// * PNG's IEND chunk) - no full decode - so the source scan stays fast.        *
// * A file that fails is BROKEN: it is still copied faithfully (a corrupt      *
// * file may be partially recoverable, and losing it would be worse), but it   *
// * is flagged in the report and never counted as verified-good.               *
// *****************************************************************************
#pragma once

#include <zen/zstring.h>

#include <cstddef>
#include <cstdint>
#include <string>

namespace offload
{
//Pure header judgement, so tests can feed bytes directly. extLower is the
//lowercase extension without the dot ("jpg", "png", "cr3", ...). head is the
//start of the file, tail its last bytes (up to 12, for PNG's IEND).
//Returns false for extensions this check does not know: only formats we can
//judge get judged, anything else is never "broken".
//On a broken file returns true and fills reason with a plain-language cause.
bool checkMediaHeader(const std::string& extLower,
                      const void* head, size_t headLen,
                      const void* tail, size_t tailLen,
                      uint64_t fileSize, std::string& reason);

//Opens the file, reads its head (and tail for PNG) and judges it. A file
//that cannot be opened or read is NOT broken here: the copy step reports
//the real I/O error, and this check must never invent a corruption verdict
//out of a read failure.
bool checkFileBroken(const Zstring& filePath, uint64_t fileSize, std::string& reason);
}
