#include "BookContentIdStub.h"

#include "util/BookContentId.h"
#include "util/ProgressSaveTime.h"

namespace BookContentIdStub {
std::string stateDir;
}

std::string BookContentId::bookStateDirName(const std::string&) { return BookContentIdStub::stateDir; }
std::string BookContentId::bookStateDir(const std::string&) { return BookContentIdStub::stateDir; }

// Dating a moved position needs the wall clock; these tests are about the progress files.
void ProgressSaveTime::record(const std::string&) {}
bool ProgressSaveTime::load(const std::string&, int64_t&) { return false; }
