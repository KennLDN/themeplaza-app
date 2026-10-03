#include "pack.h"
#include <cstring>
#include <dirent.h>
#include <strings.h>
#include <sys/stat.h>
#include <zlib.h>

namespace {

inline u16 rd16(const u8* p) { return p[0] | (p[1] << 8); }
inline u32 rd32(const u8* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((u32)p[3] << 24); }

const char* baseName(const std::string& s) {
    size_t i = s.find_last_of('/');
    return i == std::string::npos ? s.c_str() : s.c_str() + i + 1;
}

int depth(const std::string& s) {
    int d = 0;
    for (char c : s) if (c == '/') d++;
    return d;
}

constexpr size_t CHUNK = 16 * 1024;

}  // namespace

u32 crc32Of(const u8* data, size_t n, u32 crc) { return crc32(crc, data, n); }

bool Pack::open(const std::string& path, int isDir) {
    close();
    path_ = path;
    if (isDir < 0) {
        struct stat st;
        if (stat(path.c_str(), &st) != 0) return false;
        isDir = S_ISDIR(st.st_mode) ? 1 : 0;
    }
    return isDir ? openDir() : openZip();
}

void Pack::close() {
    if (zip_) { fclose(zip_); zip_ = nullptr; }
    entries_.clear();
}

bool Pack::openDir() {
    // one level of subfolders is enough for the layouts seen in the wild
    for (int level = 0; level < 2; level++) {
        std::vector<std::string> dirs = {""};
        if (level == 1) {
            dirs.clear();
            DIR* d = opendir(path_.c_str());
            if (!d) return false;
            while (dirent* de = readdir(d)) if (de->d_type == DT_DIR && de->d_name[0] != '.') dirs.push_back(std::string(de->d_name) + "/");
            closedir(d);
        }
        for (auto& sub : dirs) {
            DIR* d = opendir((path_ + "/" + sub).c_str());
            if (!d) continue;
            while (dirent* de = readdir(d)) {
                if (de->d_type == DT_DIR) continue;
                PackEntry e;
                e.name = sub + de->d_name;
                struct stat st;
                if (stat((path_ + "/" + e.name).c_str(), &st) == 0) e.size = e.comp = (u32)st.st_size;
                entries_.push_back(std::move(e));
            }
            closedir(d);
        }
    }
    return true;
}

bool Pack::openZip() {
    zip_ = fopen(path_.c_str(), "rb");
    if (!zip_) return false;
    setvbuf(zip_, nullptr, _IOFBF, CHUNK);
    fseek(zip_, 0, SEEK_END);
    long size = ftell(zip_);
    if (size < 22) return false;
    // the end-of-central-directory record is the last thing in the file, followed only by an optional comment
    // One read of the last few KB normally brings both that record and the directory it points to.
    std::vector<u8> tail;
    long eocd = -1, tailStart = 0;
    for (long want : {4096L, 70000L}) {
        long n = want < size ? want : size;
        tail.resize(n);
        fseek(zip_, size - n, SEEK_SET);
        if (fread(tail.data(), 1, n, zip_) != (size_t)n) return false;
        tailStart = size - n;
        for (long i = n - 22; i >= 0; i--)
            if (tail[i] == 'P' && tail[i + 1] == 'K' && tail[i + 2] == 5 && tail[i + 3] == 6) { eocd = i; break; }
        if (eocd >= 0 || n == size) break;
    }
    if (eocd < 0) return false;
    u32 count = rd16(&tail[eocd + 10]), cdSize = rd32(&tail[eocd + 12]), cdOff = rd32(&tail[eocd + 16]);
    if (cdOff + (u64)cdSize > (u64)size || cdSize > 4 * 1024 * 1024) return false;
    std::vector<u8> cd(cdSize);
    if ((long)cdOff >= tailStart && (size_t)(cdOff - tailStart) + cdSize <= tail.size()) memcpy(cd.data(), &tail[cdOff - tailStart], cdSize);
    else {
        fseek(zip_, cdOff, SEEK_SET);
        if (fread(cd.data(), 1, cdSize, zip_) != cdSize) return false;
    }
    size_t p = 0;
    entries_.reserve(count);
    for (u32 i = 0; i < count && p + 46 <= cdSize; i++) {
        const u8* h = &cd[p];
        if (rd32(h) != 0x02014b50) break;
        u16 nameLen = rd16(h + 28), extraLen = rd16(h + 30), commentLen = rd16(h + 32);
        if (p + 46 + nameLen > cdSize) break;
        PackEntry e;
        e.method = rd16(h + 10);
        e.crc = rd32(h + 16); e.comp = rd32(h + 20); e.size = rd32(h + 24);
        e.offset = rd32(h + 42);
        e.name.assign((const char*)h + 46, nameLen);
        for (char& c : e.name) if (c == '\\') c = '/';      // some Windows tools write folders this way
        p += 46 + nameLen + extraLen + commentLen;
        if (e.name.empty() || e.name.back() == '/') continue;   // a folder entry
        entries_.push_back(std::move(e));
    }
    return true;
}

const PackEntry* Pack::find(const char* name) const {
    const PackEntry* best = nullptr; int bestDepth = 99;
    for (auto& e : entries_) {
        if (strcasecmp(baseName(e.name), name) != 0) continue;
        int d = depth(e.name);
        if (d < bestDepth) { best = &e; bestDepth = d; }
    }
    return best;
}

bool Pack::streamImpl(const PackEntry& e, bool (*sink)(void*, const u8*, size_t), void* ctx) {
    static u8 in[CHUNK], out[CHUNK];   // only ever used from the disk worker thread
    if (!zip_) {
        FILE* f = fopen((path_ + "/" + e.name).c_str(), "rb");
        if (!f) return false;
        bool ok = true;
        for (;;) {
            size_t n = fread(out, 1, CHUNK, f);
            if (n == 0) break;
            if (!sink(ctx, out, n)) { ok = false; break; }
        }
        fclose(f);
        return ok;
    }
    u8 lh[30];
    fseek(zip_, e.offset, SEEK_SET);
    if (fread(lh, 1, 30, zip_) != 30 || rd32(lh) != 0x04034b50) return false;
    fseek(zip_, e.offset + 30 + rd16(lh + 26) + rd16(lh + 28), SEEK_SET);
    u32 left = e.comp;
    if (e.method == 0) {
        while (left) {
            size_t n = left < CHUNK ? left : CHUNK;
            if (fread(out, 1, n, zip_) != n) return false;
            if (!sink(ctx, out, n)) return false;
            left -= n;
        }
        return true;
    }
    if (e.method != 8) return false;
    z_stream z{};
    if (inflateInit2(&z, -MAX_WBITS) != Z_OK) return false;
    bool ok = true; int rc = Z_OK;
    while (ok && rc != Z_STREAM_END) {
        if (z.avail_in == 0 && left) {
            size_t n = left < CHUNK ? left : CHUNK;
            if (fread(in, 1, n, zip_) != n) { ok = false; break; }
            left -= n; z.next_in = in; z.avail_in = n;
        }
        z.next_out = out; z.avail_out = CHUNK;
        rc = inflate(&z, Z_NO_FLUSH);
        // with all input read there may still be output to hand over; only "no progress possible" is an error
        if (rc != Z_OK && rc != Z_STREAM_END) { ok = false; break; }
        size_t got = CHUNK - z.avail_out;
        if (got && !sink(ctx, out, got)) ok = false;
    }
    inflateEnd(&z);
    return ok;
}

bool Pack::read(const PackEntry& e, std::vector<u8>& out, size_t maxSize) {
    if (e.size > maxSize) return false;
    out.clear();
    out.reserve(e.size);
    bool ok = stream(e, [&](const u8* p, size_t n) {
        if (out.size() + n > maxSize) return false;
        out.insert(out.end(), p, p + n);
        return true;
    });
    if (!ok || (zip_ && out.size() != e.size)) return false;
    // a zip records each file's checksum; a file damaged on the SD card must not get installed
    return !zip_ || crc32Of(out.data(), out.size()) == e.crc;
}

size_t Pack::readHead(const PackEntry& e, u8* dst, size_t want) {
    size_t got = 0;
    stream(e, [&](const u8* p, size_t n) {
        size_t take = n < want - got ? n : want - got;
        memcpy(dst + got, p, take);
        got += take;
        return got < want;   // returning false stops the stream once enough has arrived
    });
    return got;
}

// ---------- EntryStream ----------
bool EntryStream::open(const std::string& packPath, const char* entryName) {
    close();
    Pack pk;
    if (!pk.open(packPath)) return false;
    const PackEntry* e = pk.find(entryName);
    if (!e) return false;
    size_ = e->size;
    if (!pk.isZip()) {
        f_ = fopen((packPath + "/" + e->name).c_str(), "rb");
        if (!f_) return false;
        dataOffset_ = 0; comp_ = e->size; deflated_ = false;
    } else {
        if (e->method != 0 && e->method != 8) return false;
        f_ = fopen(packPath.c_str(), "rb");
        if (!f_) return false;
        u8 lh[30];
        fseek(f_, e->offset, SEEK_SET);
        if (fread(lh, 1, 30, f_) != 30 || rd32(lh) != 0x04034b50) { close(); return false; }
        dataOffset_ = e->offset + 30 + rd16(lh + 26) + rd16(lh + 28);
        comp_ = e->comp; deflated_ = e->method == 8;
    }
    setvbuf(f_, nullptr, _IOFBF, CHUNK);
    return start();
}

bool EntryStream::start() {
    fseek(f_, dataOffset_, SEEK_SET);
    left_ = comp_; ended_ = false;
    if (!deflated_) return true;
    if (!in_) in_ = new u8[CHUNK];
    if (z_) { inflateEnd((z_stream*)z_); delete (z_stream*)z_; }
    z_stream* z = new z_stream();
    z_ = z;
    return inflateInit2(z, -MAX_WBITS) == Z_OK;
}

bool EntryStream::rewind() { return f_ && start(); }

size_t EntryStream::read(u8* out, size_t n) {
    if (!f_ || ended_) return 0;
    if (!deflated_) {
        size_t want = n < left_ ? n : left_;
        size_t got = fread(out, 1, want, f_);
        left_ -= got;
        if (got < n) ended_ = true;
        return got;
    }
    z_stream* z = (z_stream*)z_;
    z->next_out = out; z->avail_out = n;
    while (z->avail_out) {
        if (z->avail_in == 0 && left_) {
            size_t want = left_ < CHUNK ? left_ : CHUNK;
            if (fread(in_, 1, want, f_) != want) { ended_ = true; break; }
            left_ -= want; z->next_in = in_; z->avail_in = want;
        }
        int rc = inflate(z, Z_NO_FLUSH);
        if (rc == Z_STREAM_END) { ended_ = true; break; }
        if (rc != Z_OK) { ended_ = true; break; }
    }
    return n - z->avail_out;
}

void EntryStream::close() {
    if (z_) { inflateEnd((z_stream*)z_); delete (z_stream*)z_; z_ = nullptr; }
    delete[] in_; in_ = nullptr;
    if (f_) { fclose(f_); f_ = nullptr; }
}
