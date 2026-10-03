// HTTP for the network worker thread: one reused connection (keep-alive) through libcurl.
#pragma once
#include <3ds.h>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace http {

bool init();          // sockets + curl; false when the network stack could not be started
void fini();
void cancelAll();     // makes running and future requests fail at once (used when the app closes)
bool wifi();          // the console reports a Wi-Fi connection

struct Response {
    long status = 0;          // HTTP status, 0 when the request itself failed
    std::vector<u8> body;
    std::string error;        // transport error text when status == 0
    bool writeFailed = false; // download(): the file could not be written (SD card full or removed)
    bool ok() const { return status >= 200 && status < 300; }
};

// progress(now, total) is called while the body arrives (total 0 if unknown); return false to cancel.
using Progress = std::function<bool(size_t now, size_t total)>;

// Call only from the network worker.
// range: optional byte range such as "0-99".
Response get(const std::string& url, size_t maxBody = 4 * 1024 * 1024, const Progress& progress = nullptr, const char* range = nullptr);
// Hands the body to a callback piece by piece as it arrives; the callback returns false to stop.
// progress is also asked while nothing arrives (connecting, a stalled server); returning false from it stops the request.
Response stream(const std::string& url, const std::function<bool(const u8* data, size_t size)>& onData, const Progress& progress = nullptr);
// Streams the body into a file. Returns the response without a body.
Response download(const std::string& url, FILE* out, const Progress& progress = nullptr);

// Several small GETs side by side (up to `parallel` connections), all driven from the calling thread.
// done(i) is called as each one finishes, in the order they finish.
struct BatchItem { std::string url; const char* range = nullptr; size_t maxBody = 64 * 1024; Response response; };
void getBatch(std::vector<BatchItem>& items, int parallel, const std::function<void(size_t index)>& done);

std::string escape(const std::string& s);   // for a query-string value

}  // namespace http
