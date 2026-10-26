// *****************************************************************************
// * Offload mode for FreeFileSync - report writers.                            *
// *****************************************************************************
#pragma once

#include "job.h"

#include <string>
#include <vector>

namespace offload
{
//All writers return the error text (empty on success) instead of throwing.
std::string writeCsv (const JobReport& job, const Zstring& filePath);
std::string writeJson(const JobReport& job, const Zstring& filePath);

//MHL (Media Hash List, 1.1 shape) for one destination, written at its root.
std::string writeMhl (const JobReport& job, const DestReport& dest, const Zstring& filePath);

std::string toIso8601(time_t t);
std::string humanBytes(uint64_t bytes);

//Escapes a UTF-8 string for a JSON value. Shared with the CLI's --json summary so
//the report file and the one-line summary cannot escape differently.
std::string jsonEscape(const std::string& s);
}
