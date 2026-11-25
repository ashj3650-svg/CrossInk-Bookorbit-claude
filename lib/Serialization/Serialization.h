#pragma once
#include <HalStorage.h>

#include <iostream>
#include <limits>

namespace serialization {
// Every string these files hold is a title, an author, a path, an href or a short label.
// A length past this is a damaged file, never data: reading it would ask std::string for
// up to 4 GB, and with exceptions disabled a failed allocation aborts (a boot loop when
// the damaged file is read at startup). Rejecting it makes the read fail cleanly instead.
constexpr uint32_t MAX_STRING_BYTES = 16 * 1024;

template <typename T>
inline void writePod(std::ostream& os, const T& value) {
  os.write(reinterpret_cast<const char*>(&value), sizeof(T));
}

template <typename T>
inline void writePod(FsFile& file, const T& value) {
  file.write(reinterpret_cast<const uint8_t*>(&value), sizeof(T));
}

template <typename T>
inline bool tryWritePod(FsFile& file, const T& value) {
  return file.write(reinterpret_cast<const uint8_t*>(&value), sizeof(T)) == sizeof(T);
}

template <typename T>
inline void readPod(std::istream& is, T& value) {
  is.read(reinterpret_cast<char*>(&value), sizeof(T));
}

template <typename T>
inline void readPod(FsFile& file, T& value) {
  file.read(reinterpret_cast<uint8_t*>(&value), sizeof(T));
}

template <typename T>
inline bool tryReadPod(FsFile& file, T& value) {
  return file.read(reinterpret_cast<uint8_t*>(&value), sizeof(T)) == sizeof(T);
}

inline void writeString(std::ostream& os, const std::string& s) {
  const uint32_t len = s.size();
  writePod(os, len);
  os.write(s.data(), len);
}

inline void writeString(FsFile& file, const std::string& s) {
  const uint32_t len = s.size();
  writePod(file, len);
  file.write(reinterpret_cast<const uint8_t*>(s.data()), len);
}

inline bool tryWriteString(FsFile& file, const std::string& s) {
  const uint32_t len = s.size();
  return tryWritePod(file, len) && (len == 0 || file.write(reinterpret_cast<const uint8_t*>(s.data()), len) == len);
}

inline void readString(std::istream& is, std::string& s) {
  uint32_t len = 0;
  readPod(is, len);
  if (len > MAX_STRING_BYTES) {
    s.clear();
    is.setstate(std::ios::failbit);
    return;
  }
  s.resize(len);
  is.read(&s[0], len);
}

inline void readString(FsFile& file, std::string& s) {
  uint32_t len = 0;
  readPod(file, len);
  if (len > MAX_STRING_BYTES) {
    s.clear();
    return;
  }
  s.resize(len);
  file.read(&s[0], len);
}

inline bool tryReadString(FsFile& file, std::string& s) {
  uint32_t len = 0;
  if (!tryReadPod(file, len)) {
    return false;
  }
  if (len > MAX_STRING_BYTES) {
    return false;
  }
  s.resize(len);
  const int readLen = static_cast<int>(len);
  return len == 0 || file.read(&s[0], readLen) == readLen;
}
}  // namespace serialization
