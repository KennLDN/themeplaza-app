// Theme Plaza's public endpoints. Blocking calls for the
// network worker; they return plain data and never touch Items.
#pragma once
#include <string>
#include <vector>
#include "formats.h"
#include "model.h"

namespace plaza {

struct List {
    bool ok = false;          // the request reached the site and the answer made sense
    bool reached = false;     // false: no connection (as opposed to an error from the site)
    int pages = 0;
    std::vector<long> ids;    // up to 24, newest first
    std::string error;
    std::string usedQuery;    // the query string that produced this list (pass it to listRaw for further pages)
};
// Points the client at another server, e.g. "http://192.168.1.20:8377" (a staging copy or the test server).
void setBase(const std::string& url);

// query: free text, may be empty. tags: any of these.
List list(Kind kind, int page, const std::string& query, const std::vector<std::string>& tags);
List listRaw(Kind kind, int page, const std::string& rawQuery);

// The same list from the data behind the site's own browse pages, which can also sort (order 1 = most
// downloaded, 2 = most liked) and returns whole rows in one request. It needs https and is not a published
// interface, so the app uses it only for the orders the published one cannot give.
struct Row { long id = 0; std::string title, author, desc; int downloads = 0, likes = 0; bool bgm = false; };
struct RowList {
    bool ok = false, reached = false;
    int pages = 0;
    std::vector<Row> rows;
};
RowList listSorted(Kind kind, int page, const std::string& query, const std::vector<std::string>& tags, int order);
// The v2 list, if the server has one: whole rows, any order, in one plain-http request. A server
// that does not have it yet is noticed on the first try and not asked again in this session.
RowList listV2(Kind kind, int page, const std::string& query, const std::vector<std::string>& tags, int order);
bool v2();     // the server answered a v2 request
bool v2Possible();   // false once the server has shown it has no v2 endpoints
// The icons of these items in one response, 0x1200 bytes each, in the order asked.
bool iconsV2(const std::vector<long>& ids, std::vector<u8>& out);

struct Details {
    bool ok = false;
    std::string title, desc, author;
    int downloads = 0, likes = 0;
    bool bgm = false;
    std::vector<std::string> tags;
};
Details details(long id);

// Name, author, description and icon in the console's own format.
bool smdh(long id, fmt::Smdh& out);

std::string urlSmdh(long id);
std::string urlDownload(long id);
std::string urlPreview(long id, const char* part);   // part: "" (uploader's screenshot), "top", "bottom", "icon"
std::string urlBgm(long id);

// A file name for a downloaded item: "Title by Author (id).zip", safe for the SD card's file system.
std::string fileName(const std::string& title, const std::string& author, long id);
// The id at the end of such a file name, or 0.
long idFromFileName(const std::string& name);
// Reads a Theme Plaza link or QR code payload and returns the item id, or 0.
long idFromLink(const std::string& text);

}  // namespace plaza
