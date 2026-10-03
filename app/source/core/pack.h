// A theme, splash or badge set as it sits on the SD card: either a zip file or a plain folder holding
// the same files. Reads the zip format directly (stored and deflated entries, no zip64).
#pragma once
#include <3ds.h>
#include <cstdio>
#include <string>
#include <vector>

struct PackEntry {
    std::string name;        // path inside the pack, as stored
    u32 size = 0;            // uncompressed size
    u32 comp = 0;            // compressed size (zip)
    u32 crc = 0;             // CRC-32 of the uncompressed data (zip; 0 for folders)
    u32 offset = 0;          // zip: offset of the local header
    u16 method = 0;          // zip: 0 stored, 8 deflate
};

class Pack {
public:
    ~Pack() { close(); }
    // zip file or folder. isDir: 1 folder, 0 file, -1 find out (costs one more call to the file system).
    bool open(const std::string& path, int isDir = -1);
    void close();
    bool isZip() const { return zip_ != nullptr; }
    const std::vector<PackEntry>& entries() const { return entries_; }
    // Finds a file by name without regard to case or to the folder it is in (the least nested match wins).
    const PackEntry* find(const char* name) const;
    // Reads a whole entry. Fails if it is larger than maxSize.
    bool read(const PackEntry& e, std::vector<u8>& out, size_t maxSize);
    // Reads the first n bytes of an entry (less if it is shorter). Returns the number of bytes read.
    size_t readHead(const PackEntry& e, u8* out, size_t n);
    // Streams an entry to a callback in pieces; stops and fails if the callback returns false.
    template <typename F> bool stream(const PackEntry& e, F&& sink) {
        return streamImpl(e, [](void* ctx, const u8* p, size_t n) { return (*(F*)ctx)(p, n); }, &sink);
    }

private:
    bool streamImpl(const PackEntry& e, bool (*sink)(void*, const u8*, size_t), void* ctx);
    bool openZip();
    bool openDir();
    std::string path_;
    FILE* zip_ = nullptr;
    std::vector<PackEntry> entries_;
};

// Reads one entry from start to end with its own file handle and buffers, for use on another thread
// than the one the Pack lives on (music streaming).
class EntryStream {
public:
    ~EntryStream() { close(); }
    // packPath: the zip file or folder; entryName: as Pack::find takes it.
    bool open(const std::string& packPath, const char* entryName);
    size_t read(u8* out, size_t n);     // fewer than n only at the end
    bool rewind();
    void close();
    u32 size() const { return size_; }

private:
    bool start();
    FILE* f_ = nullptr;
    void* z_ = nullptr;                 // z_stream, when the entry is deflated
    u8* in_ = nullptr;
    u32 dataOffset_ = 0, comp_ = 0, size_ = 0, left_ = 0;
    bool deflated_ = false, ended_ = false;
};

u32 crc32Of(const u8* data, size_t n, u32 crc = 0);
