#pragma once
#include <windows.h>
#include <cstdint>
#include <string>
#include <vector>

namespace ResolutionPatch {
enum class Kind { Integer, Byte, Bytes, Jump, Fill };
struct Site {
    uint32_t address;
    unsigned size;
    Kind kind;
    uint32_t guard;
    unsigned guardSize;
    const char* original;
    const char* name;
};

// This batch runs during process initialization, before game threads/hooks can
// execute the affected code. It does not support live patching a running game.
class Batch {
public:
    void WriteInt(uint32_t address, unsigned value);
    void WriteByte(uint32_t address, unsigned char value);
    void WriteByteArray(uint32_t address, const unsigned char* bytes, unsigned size);
    void FillBytes(uint32_t address, unsigned char value, unsigned size);
    void CodeCave(void* target, uint32_t address, unsigned size);
    bool Apply(std::string& error);
private:
    struct Write { const Site* site; std::vector<unsigned char> bytes; };
    void Add(uint32_t address, Kind kind, const unsigned char* bytes, unsigned size);
    std::vector<Write> writes_;
    std::string error_;
};
void Report(bool success, const std::string& message);

#ifdef RESOLUTION_PATCH_TESTING
void SetImageForTesting(void* image);
void FailAfterWritesForTesting(int count);
#endif
}
