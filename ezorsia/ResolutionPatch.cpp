#include "ResolutionPatch.h"
#include "ResolutionPatchSites.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <sstream>

namespace ResolutionPatch {
namespace {
std::mutex applyMutex;
void* receiptImage = nullptr;
std::vector<unsigned char> receipt;
#ifdef RESOLUTION_PATCH_TESTING
void* testImage = nullptr;
int failAfterWrites = -1;
#endif
unsigned char* Image() {
#ifdef RESOLUTION_PATCH_TESTING
    if (testImage) return static_cast<unsigned char*>(testImage);
#endif
    return reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
}
bool Read(void* destination, const void* source, size_t size) {
    __try { memcpy(destination, source, size); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
struct Span {
    uint32_t va;
    std::vector<unsigned char> original, desired, before, verify;
};
struct Page { void* address; SIZE_T size; DWORD protection; };
bool RestorePages(const std::vector<Page>& pages) {
    bool success = true;
    for (auto i = pages.rbegin(); i != pages.rend(); ++i) {
        DWORD ignored;
        if (!VirtualProtect(i->address, i->size, i->protection, &ignored)) success = false;
    }
    return success;
}
std::string Failure(const char* reason, uint32_t address) {
    char message[180];
    sprintf_s(message, "%s at 0x%08X (Win32=%lu)", reason, address, GetLastError());
    return message;
}
bool Identity(unsigned char* image) {
    IMAGE_DOS_HEADER dos{};
    IMAGE_NT_HEADERS32 nt{};
    if (!Read(&dos, image, sizeof(dos)) || dos.e_magic != IMAGE_DOS_SIGNATURE ||
        dos.e_lfanew < sizeof(dos) || dos.e_lfanew > 4096 ||
        !Read(&nt, image+dos.e_lfanew, sizeof(nt))) return false;
    return nt.Signature == IMAGE_NT_SIGNATURE && nt.FileHeader.Machine == IMAGE_FILE_MACHINE_I386 &&
        nt.FileHeader.TimeDateStamp == kImageTimestamp && nt.OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC &&
        nt.OptionalHeader.ImageBase == kImageBase && nt.OptionalHeader.SizeOfImage == kImageSize;
}
}

void Batch::Add(uint32_t address, Kind kind, const unsigned char* bytes, unsigned size) {
    if (!error_.empty()) return;
    const Site* site = nullptr;
    for (const auto& candidate : kSites)
        if (candidate.address == address && candidate.size == size && candidate.kind == kind) { site = &candidate; break; }
    if (!site) { error_ = Failure("Unregistered resolution write", address); return; }
    for (const auto& write : writes_)
        if (address < write.site->address+write.site->size && write.site->address < address+size) {
            error_ = Failure("Overlapping resolution write", address); return;
        }
    writes_.push_back({site, std::vector<unsigned char>(bytes, bytes+size)});
}
void Batch::WriteInt(uint32_t address, unsigned value) {
    Add(address, Kind::Integer, reinterpret_cast<const unsigned char*>(&value), sizeof(value));
}
void Batch::WriteByte(uint32_t address, unsigned char value) { Add(address, Kind::Byte, &value, 1); }
void Batch::WriteByteArray(uint32_t address, const unsigned char* bytes, unsigned size) { Add(address, Kind::Bytes, bytes, size); }
void Batch::FillBytes(uint32_t address, unsigned char value, unsigned size) {
    if (size > 256) { error_ = "Resolution fill exceeds supported length"; return; }
    const std::vector<unsigned char> bytes(size, value);
    Add(address, Kind::Fill, bytes.data(), size);
}
void Batch::CodeCave(void* target, uint32_t address, unsigned size) {
    if (size < 5 || size > 256) { error_ = "Invalid resolution jump length"; return; }
    std::vector<unsigned char> bytes(size, 0x90);
    bytes[0] = 0xE9;
    const uint32_t relative = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(target) -
        (reinterpret_cast<uintptr_t>(Image()) + address-kImageBase+5));
    memcpy(bytes.data()+1, &relative, sizeof(relative));
    Add(address, Kind::Jump, bytes.data(), size);
}

bool Batch::Apply(std::string& error) {
    const std::lock_guard<std::mutex> lock(applyMutex);
    if (!error_.empty()) { error = error_; return false; }
    unsigned char* image = Image();
    if (!Identity(image)) { error = "Unsupported client executable identity"; return false; }
#ifndef RESOLUTION_PATCH_TESTING
    // Legacy code caves contain absolute return addresses. Never pretend ASLR is supported.
    if (reinterpret_cast<uintptr_t>(image) != kImageBase) { error = "Unsupported client image base"; return false; }
#endif
    std::map<uint32_t, unsigned char> expected;
    for (const auto& site : kSites) {
        if (site.guard < kImageBase || site.guardSize > kImageSize || site.guard-kImageBase > kImageSize-site.guardSize) {
            error = "Invalid compiled resolution manifest"; return false;
        }
        for (unsigned i = 0; i < site.guardSize; ++i) {
            const auto inserted = expected.emplace(site.guard+i, static_cast<unsigned char>(site.original[i]));
            if (!inserted.second && inserted.first->second != static_cast<unsigned char>(site.original[i])) {
                error = "Conflicting resolution instruction guards"; return false;
            }
        }
    }
    std::vector<Span> spans;
    for (const auto& byte : expected) {
        if (spans.empty() || spans.back().va+spans.back().original.size() != byte.first)
            spans.push_back({byte.first, {}, {}, {}, {}});
        spans.back().original.push_back(byte.second);
    }
    std::vector<unsigned char> actual, pristine, nextReceipt;
    for (auto& span : spans) {
        span.desired = span.original;
        span.before.resize(span.original.size());
        span.verify.resize(span.original.size());
        if (!Read(span.before.data(), image+span.va-kImageBase, span.before.size())) {
            error = Failure("Unreadable resolution site", span.va); return false;
        }
        actual.insert(actual.end(), span.before.begin(), span.before.end());
        pristine.insert(pristine.end(), span.original.begin(), span.original.end());
        for (const auto& write : writes_) {
            const auto& site = *write.site;
            if (site.address >= span.va && site.address-span.va+site.size <= span.desired.size())
                std::copy(write.bytes.begin(), write.bytes.end(), span.desired.begin()+site.address-span.va);
        }
        nextReceipt.insert(nextReceipt.end(), span.desired.begin(), span.desired.end());
    }
    // Only the entire pristine image or our entire last successful result is accepted.
    // Arbitrary immediates, partially applied patches and other modules' edits are rejected.
    if (actual != pristine && (receiptImage != image || actual != receipt)) {
        size_t offset = 0;
        const auto& allowed = receiptImage == image && receipt.size() == actual.size() ? receipt : pristine;
        for (const auto& span : spans) {
            for (size_t i = 0; i < span.before.size(); ++i)
                if (span.before[i] != allowed[offset+i]) {
                    char detail[160];
                    sprintf_s(detail, "Resolution signature mismatch at 0x%08X: expected %02X, actual %02X",
                        span.va+static_cast<uint32_t>(i), allowed[offset+i], span.before[i]);
                    error = detail; return false;
                }
            offset += span.before.size();
        }
        error = "Resolution signature mismatch"; return false;
    }
    if (actual == nextReceipt) { error = "Resolution patches already match"; return true; }

    SYSTEM_INFO system{}; GetSystemInfo(&system);
    std::map<uintptr_t, DWORD> toProtect;
    for (const auto& span : spans) {
        if (span.before == span.desired) continue;
        const uintptr_t begin = reinterpret_cast<uintptr_t>(image)+span.va-kImageBase;
        const uintptr_t end = begin+span.desired.size()-1;
        for (uintptr_t p = begin-begin%system.dwPageSize; p <= end; p += system.dwPageSize) {
            MEMORY_BASIC_INFORMATION info{};
            if (!VirtualQuery(reinterpret_cast<void*>(p), &info, sizeof(info)) || info.State != MEM_COMMIT ||
                info.AllocationBase != image || (info.Protect & (PAGE_NOACCESS | PAGE_GUARD))) {
                error = Failure("Invalid resolution memory page", span.va); return false;
            }
            toProtect.emplace(p, info.Protect);
        }
    }
    std::vector<Page> pages;
    pages.reserve(toProtect.size()); // No allocations once protection/writes start.
    for (const auto& page : toProtect) {
        DWORD previous;
        if (!VirtualProtect(reinterpret_cast<void*>(page.first), system.dwPageSize, PAGE_EXECUTE_READWRITE, &previous)) {
            const DWORD failure = GetLastError();
            const bool restored = RestorePages(pages);
            SetLastError(failure);
            error = restored ? "Unable to protect resolution pages; no bytes written" : "Protection rollback failed; startup must abort";
            return false;
        }
        pages.push_back({reinterpret_cast<void*>(page.first), system.dwPageSize, previous});
    }
    bool success = true;
    unsigned completed = 0;
    for (auto& span : spans) {
        if (span.before == span.desired) continue;
#ifdef RESOLUTION_PATCH_TESTING
        if (failAfterWrites >= 0 && completed == static_cast<unsigned>(failAfterWrites)) { success = false; break; }
#endif
        if (!Read(image+span.va-kImageBase, span.desired.data(), span.desired.size()) ||
            !Read(span.verify.data(), image+span.va-kImageBase, span.verify.size()) || span.verify != span.desired) {
            success = false; break;
        }
        ++completed;
    }
    if (success) success = FlushInstructionCache(GetCurrentProcess(), image, kImageSize) != FALSE;
    if (success) success = RestorePages(pages);
    if (!success) {
        bool rollback = true;
        for (const auto& page : pages) {
            DWORD ignored;
            if (!VirtualProtect(page.address, page.size, PAGE_EXECUTE_READWRITE, &ignored)) rollback = false;
        }
        for (const auto& span : spans)
            if (span.before != span.desired && !Read(image+span.va-kImageBase, span.before.data(), span.before.size())) rollback = false;
        if (!FlushInstructionCache(GetCurrentProcess(), image, kImageSize)) rollback = false;
        if (!RestorePages(pages)) rollback = false;
        error = rollback ? "Resolution write failed; all original bytes restored" : "Resolution rollback failed; startup must abort";
        return false;
    }
    receiptImage = image;
    receipt = std::move(nextReceipt);
    error = "Resolution patch signatures verified; batch applied";
    return true;
}

void Report(bool success, const std::string& message) {
    OutputDebugStringA((std::string(success ? "BeiDou patch OK: " : "BeiDou patch ERROR: ")+message+"\n").c_str());
    wchar_t path[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (!length || length >= MAX_PATH) return;
    wchar_t* leaf = wcsrchr(path, L'\\');
    if (!leaf || wcscpy_s(leaf+1, MAX_PATH-(leaf+1-path), L"patch-integrity.log")) return;
    FILE* file = nullptr;
    if (_wfopen_s(&file, path, L"a") || !file) return;
    SYSTEMTIME time{}; GetLocalTime(&time);
    fprintf(file, "%04u-%02u-%02u %02u:%02u:%02u %s: %s\n", time.wYear, time.wMonth, time.wDay,
        time.wHour, time.wMinute, time.wSecond, success ? "OK" : "ERROR", message.c_str());
    fclose(file);
}
#ifdef RESOLUTION_PATCH_TESTING
void SetImageForTesting(void* image) { testImage = image; receiptImage = nullptr; receipt.clear(); }
void FailAfterWritesForTesting(int count) { failAfterWrites = count; }
#endif
}
