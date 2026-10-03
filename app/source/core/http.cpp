#include "http.h"
#include <curl/curl.h>
#include <malloc.h>
#include "log.h"
#include "appdir.h"

namespace http {

namespace {

constexpr u32 SOC_BUFFER = 0x100000;
// Theme Plaza's bot filter refuses requests without a User-Agent and those of common HTTP libraries.
const char* const USER_AGENT = APP_USER_AGENT;

u32* g_socBuf = nullptr;
bool g_up = false;
CURL* g_curl = nullptr;      // single requests: one reused connection
CURLM* g_multi = nullptr;    // batches: a few connections side by side, driven from the one network thread
volatile bool g_cancel = false;

struct Sink {
    std::vector<u8>* body = nullptr; FILE* file = nullptr;
    size_t max = 0, got = 0; bool overflow = false, writeFailed = false;
    const Progress* progress = nullptr;
    const std::function<bool(const u8*, size_t)>* chunk = nullptr;
};

size_t onData(char* p, size_t sz, size_t n, void* user) {
    Sink* s = (Sink*)user;
    size_t len = sz * n;
    if (s->chunk) { if (!(*s->chunk)((const u8*)p, len)) return 0; }
    else if (s->file) { if (fwrite(p, 1, len, s->file) != len) { s->writeFailed = true; return 0; } }
    else {
        if (s->got + len > s->max) { s->overflow = true; return 0; }
        s->body->insert(s->body->end(), (u8*)p, (u8*)p + len);
    }
    s->got += len;
    return len;
}

int onProgress(void* user, curl_off_t total, curl_off_t now, curl_off_t, curl_off_t) {
    Sink* s = (Sink*)user;
    if (g_cancel) return 1;
    if (s->progress && *s->progress && !(*s->progress)((size_t)now, (size_t)total)) return 1;
    return 0;
}

void configure(CURL* c, const std::string& url, Sink* sink, const char* range) {
    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_USERAGENT, USER_AGENT);
    // Text answers may come gzipped. File downloads are asked for as they are: they are compressed already,
    // and a transformed answer would arrive without its length, which the progress bar needs.
    if (!sink->file && !sink->chunk && !range) curl_easy_setopt(c, CURLOPT_ACCEPT_ENCODING, "");
    if (range) curl_easy_setopt(c, CURLOPT_RANGE, range);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_MAXREDIRS, 4L);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 12L);
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_LIMIT, 1L);     // give up when nothing arrives for 20 seconds
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_TIME, 20L);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(c, CURLOPT_BUFFERSIZE, 32 * 1024L);
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 0L);      // no certificate store on the console
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, onData);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, sink);
    curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, onProgress);
    curl_easy_setopt(c, CURLOPT_XFERINFODATA, sink);
}

Response run(const std::string& url, Sink& sink, const char* range = nullptr) {
    Response r;
    if (!g_up || g_cancel) { r.error = "network not started"; return r; }
    if (!g_curl) g_curl = curl_easy_init();
    CURL* c = g_curl;
    if (!c) { r.error = "out of memory"; return r; }
    curl_easy_reset(c);
    configure(c, url, &sink, range);
    CURLcode rc = curl_easy_perform(c);
    if (rc == CURLE_OK) curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &r.status);
    else {
        r.error = sink.overflow ? "response too large" : curl_easy_strerror(rc);
        bool stopped = (rc == CURLE_WRITE_ERROR || rc == CURLE_ABORTED_BY_CALLBACK) && sink.chunk;      // the receiver of a stream said "enough"
        if (!stopped && !g_cancel) LOG("http %s: %s", url.c_str(), r.error.c_str());    // nor is a request cut short by leaving the app
        // start the next request on a fresh connection
        curl_easy_cleanup(g_curl); g_curl = nullptr;
    }
    return r;
}

}  // namespace

bool init() {
    if (g_up) return true;
    g_socBuf = (u32*)memalign(0x1000, SOC_BUFFER);
    if (!g_socBuf) return false;
    Result rc = socInit(g_socBuf, SOC_BUFFER);
    if (R_FAILED(rc)) { LOG("socInit failed %08lX", (unsigned long)rc); free(g_socBuf); g_socBuf = nullptr; return false; }
    sslcInit(0);      // the TLS library takes its random numbers from the system's ssl service
    curl_global_init(CURL_GLOBAL_DEFAULT);
    acInit();
    g_up = true;
    return true;
}

void fini() {
    if (!g_up) return;
    if (g_curl) { curl_easy_cleanup(g_curl); g_curl = nullptr; }
    if (g_multi) { curl_multi_cleanup(g_multi); g_multi = nullptr; }
    curl_global_cleanup();
    acExit();
    sslcExit();
    socExit();
    g_up = false;
}

void cancelAll() { g_cancel = true; }

bool wifi() {
    u32 status = 0;
    if (R_FAILED(ACU_GetWifiStatus(&status))) return true;   // cannot tell: let a request decide
    return status != 0;
}

Response get(const std::string& url, size_t maxBody, const Progress& progress, const char* range) {
    Response r; Sink s; s.body = &r.body; s.max = maxBody; s.progress = &progress;
    Response done = run(url, s, range);
    done.body = std::move(r.body);
    return done;
}

Response stream(const std::string& url, const std::function<bool(const u8*, size_t)>& onData, const Progress& progress) {
    Sink s; s.chunk = &onData; s.progress = &progress;
    return run(url, s);
}

Response download(const std::string& url, FILE* out, const Progress& progress) {
    Sink s; s.file = out; s.progress = &progress;
    Response r = run(url, s);
    r.writeFailed = s.writeFailed;
    return r;
}

void getBatch(std::vector<BatchItem>& items, int parallel, const std::function<void(size_t)>& done) {
    const size_t n = items.size();
    if (!g_up || g_cancel) { for (size_t i = 0; i < n; i++) { items[i].response.error = "network not started"; if (done) done(i); } return; }
    if (!g_multi) g_multi = curl_multi_init();
    curl_multi_setopt(g_multi, CURLMOPT_MAX_HOST_CONNECTIONS, (long)parallel);
    std::vector<Sink> sinks(n);
    size_t next = 0; int active = 0;
    auto add = [&](size_t i) {
        CURL* c = curl_easy_init();
        if (!c) { items[i].response.error = "out of memory"; if (done) done(i); return; }
        sinks[i].body = &items[i].response.body; sinks[i].max = items[i].maxBody;
        configure(c, items[i].url, &sinks[i], items[i].range);
        curl_easy_setopt(c, CURLOPT_PRIVATE, (char*)(uintptr_t)i);
        curl_multi_add_handle(g_multi, c);
        active++;
    };
    while (next < n && active < parallel) add(next++);
    while (active > 0) {
        int running = 0;
        curl_multi_perform(g_multi, &running);
        int left = 0;
        while (CURLMsg* msg = curl_multi_info_read(g_multi, &left)) {
            if (msg->msg != CURLMSG_DONE) continue;
            CURL* c = msg->easy_handle;
            char* priv = nullptr;
            curl_easy_getinfo(c, CURLINFO_PRIVATE, &priv);
            size_t i = (size_t)(uintptr_t)priv;
            Response& r = items[i].response;
            if (msg->data.result == CURLE_OK) curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &r.status);
            else r.error = sinks[i].overflow ? "response too large" : curl_easy_strerror(msg->data.result);
            curl_multi_remove_handle(g_multi, c);
            curl_easy_cleanup(c);
            active--;
            if (done) done(i);
            if (next < n) add(next++);
        }
        if (active > 0) curl_multi_poll(g_multi, nullptr, 0, 200, nullptr);
        // requests that could not even be set up while others were running
        while (active < parallel && next < n) add(next++);
        if (active == 0 && next >= n) break;
    }
}

std::string escape(const std::string& in) {
    static const char* hex = "0123456789ABCDEF";
    std::string o;
    for (unsigned char ch : in) {
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '.' || ch == '~') o += (char)ch;
        else { o += '%'; o += hex[ch >> 4]; o += hex[ch & 15]; }
    }
    return o;
}

}  // namespace http
