#pragma once
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <utility>

// The device's HalStorage.h brings in Arduino.h, and with it millis().
inline unsigned long millis() {
  using namespace std::chrono;
  return static_cast<unsigned long>(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

inline size_t storageReadCalls = 0;
inline size_t storageReadBytes = 0;
inline size_t failStorageRead = 0;
inline size_t failStorageFrom = 0;
inline size_t injectedReadFailures = 0;
inline size_t openStorageFiles = 0;
class HalFile {
 public:
  std::FILE* file = nullptr;
  HalFile() = default;
  HalFile(const HalFile&) = delete;
  HalFile& operator=(const HalFile&) = delete;
  HalFile(HalFile&& other) : file(std::exchange(other.file, nullptr)) {}
  HalFile& operator=(HalFile&& other) {
    close();
    file = std::exchange(other.file, nullptr);
    return *this;
  }
  ~HalFile() { close(); }
  void close() {
    if (file) {
      std::fclose(file);
      --openStorageFiles;
    }
    file = nullptr;
  }
  size_t size() {
    auto pos = std::ftell(file);
    std::fseek(file, 0, SEEK_END);
    auto len = std::ftell(file);
    std::fseek(file, pos, SEEK_SET);
    return len;
  }
  bool seekSet(size_t offset) { return file && std::fseek(file, offset, SEEK_SET) == 0; }
  int read(void* bytes, size_t count) {
    ++storageReadCalls;
    if (failStorageRead == storageReadCalls || (failStorageFrom && storageReadCalls >= failStorageFrom)) {
      ++injectedReadFailures;
      return 0;
    }
    const auto got = file ? std::fread(bytes, 1, count, file) : 0;
    storageReadBytes += got;
    return got;
  }
  size_t write(const void* bytes, size_t count) { return file ? std::fwrite(bytes, 1, count, file) : 0; }
};
inline struct StorageStub {
  bool openFileForRead(const char*, const char* path, HalFile& file) {
    file.close();
    file.file = std::fopen(path, "rb");
    if (file.file) ++openStorageFiles;
    return file.file;
  }
  bool openFileForWrite(const char*, const char* path, HalFile& file) {
    file.close();
    file.file = std::fopen(path, "wb");
    if (file.file) ++openStorageFiles;
    return file.file;
  }
  bool exists(const char* path) {
    std::FILE* f = std::fopen(path, "rb");
    if (!f) return false;
    std::fclose(f);
    return true;
  }
  bool remove(const char* path) { return std::remove(path) == 0; }
} Storage;
