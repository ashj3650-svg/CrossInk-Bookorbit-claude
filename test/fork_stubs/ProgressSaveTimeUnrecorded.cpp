// Dating a moved position needs the wall clock; tests about the progress files record nothing.
#include "util/ProgressSaveTime.h"

void ProgressSaveTime::record(const std::string&) {}
bool ProgressSaveTime::load(const std::string&, int64_t&) { return false; }
