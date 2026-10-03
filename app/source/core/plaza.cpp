#include "plaza.h"
#include <cstdlib>
#include <cstring>
#include <jansson.h>
#include "http.h"
#include "log.h"

namespace plaza {

namespace {

// Plain http works for the API and the downloads and spares the console the TLS handshake; the site's own
// pages need https. Both can be pointed at another server (a staging copy, a test server) with setBase().
std::string BASE = "http://themeplaza.art";
std::string SITE = "https://themeplaza.art";
int g_v2 = -1;      // -1 not tried yet, 0 the server has no v2 endpoints, 1 it has

json_t* parse(const http::Response& r) {
    if (!r.ok() || r.body.empty()) return nullptr;
    json_error_t e;
    return json_loadb((const char*)r.body.data(), r.body.size(), 0, &e);
}

std::string str(json_t* o, const char* key) {
    json_t* v = json_object_get(o, key);
    return json_is_string(v) ? json_string_value(v) : "";
}

int num(json_t* o, const char* key) {
    json_t* v = json_object_get(o, key);
    return json_is_integer(v) ? (int)json_integer_value(v) : json_is_true(v) ? 1 : 0;
}

List listOnce(Kind kind, int page, const std::string& q) {
    List out;
    out.usedQuery = q;
    std::string url = BASE + "/api/anemone/v1/list?page=" + std::to_string(page) + "&category=" + std::to_string((int)kind + 1);
    if (!q.empty()) url += "&query=" + http::escape(q);
    http::Response r = http::get(url, 64 * 1024);
    if (r.status == 0) { out.error = "No connection to Theme Plaza"; return out; }
    out.reached = true;
    json_t* j = parse(r);
    if (!j) { out.error = r.status == 403 ? "Theme Plaza refused the request" : "Theme Plaza gave an answer the app does not understand"; return out; }
    out.ok = true;
    if (json_is_true(json_object_get(j, "success"))) {
        out.pages = num(j, "pages");
        json_t* items = json_object_get(j, "items");
        size_t i; json_t* v;
        json_array_foreach(items, i, v) if (json_is_integer(v)) out.ids.push_back((long)json_integer_value(v));
    }   // "No items found" is success:false with nothing else; it stays an empty, valid list
    json_decref(j);
    return out;
}

void rowsFromJson(json_t* items, std::vector<Row>& rows) {
    size_t i; json_t* it;
    json_array_foreach(items, i, it) {
        if (!json_is_object(it)) continue;
        Row row;
        json_t* id = json_object_get(it, "id");
        row.id = json_is_integer(id) ? (long)json_integer_value(id) : json_is_string(id) ? atol(json_string_value(id)) : 0;
        row.title = str(it, "title"); row.author = str(it, "author"); row.desc = str(it, "description");
        row.downloads = num(it, "downloads"); row.likes = num(it, "likes"); row.bgm = num(it, "bgm") != 0;
        if (row.id) rows.push_back(std::move(row));
    }
}

}  // namespace

void setBase(const std::string& url) {
    std::string u = url;
    while (!u.empty() && (u.back() == '/' || u.back() == '\n' || u.back() == '\r' || u.back() == ' ')) u.pop_back();
    if (u.empty()) return;
    BASE = SITE = u;
    LOG("Theme Plaza server set to %s", u.c_str());
}

RowList listV2(Kind kind, int page, const std::string& query, const std::vector<std::string>& tags, int order) {
    RowList out;
    if (g_v2 == 0) return out;
    std::string url = BASE + "/api/anemone/v2/list?category=" + std::to_string((int)kind + 1) + "&page=" + std::to_string(page) +
                      "&sort=" + (order == 1 ? "downloads" : order == 2 ? "likes" : "new");
    if (!query.empty()) url += "&query=" + http::escape(query);
    if (!tags.empty()) {
        url += "&tags=";
        for (size_t i = 0; i < tags.size(); i++) url += (i ? "," : "") + http::escape(tags[i]);
    }
    http::Response r = http::get(url, 256 * 1024);
    if (r.status == 0) return out;
    out.reached = true;
    json_t* j = parse(r);
    // a server without this endpoint answers 404 or with a web page: remember that and use the older ways
    if (!j || !json_is_object(j) || !json_object_get(j, "success")) {
        if (j) json_decref(j);
        if (r.status == 404 || r.status == 200) g_v2 = 0;   // other answers (a refusal, a server error) say nothing about the endpoint
        return out;
    }
    g_v2 = 1;
    out.ok = true;
    out.pages = num(j, "pages");
    rowsFromJson(json_object_get(j, "items"), out.rows);
    json_decref(j);
    return out;
}

bool v2() { return g_v2 == 1; }
bool v2Possible() { return g_v2 != 0; }

bool iconsV2(const std::vector<long>& ids, std::vector<u8>& out) {
    if (g_v2 != 1 || ids.empty()) return false;
    std::string url = BASE + "/api/anemone/v2/icons?ids=";
    for (size_t i = 0; i < ids.size(); i++) url += (i ? "," : "") + std::to_string(ids[i]);
    http::Response r = http::get(url, ids.size() * fmt::SMDH_ICON_SIZE + 1024);
    if (!r.ok() || r.body.size() != ids.size() * fmt::SMDH_ICON_SIZE) return false;
    out = std::move(r.body);
    return true;
}

List listRaw(Kind kind, int page, const std::string& rawQuery) { return listOnce(kind, page, rawQuery); }

List list(Kind kind, int page, const std::string& query, const std::vector<std::string>& tags) {
    std::string tagPart;
    if (!tags.empty()) {
        tagPart = "tag:";
        for (size_t i = 0; i < tags.size(); i++) tagPart += (i ? "," : "") + tags[i];
    }
    std::string q = tagPart + (tagPart.empty() || query.empty() ? "" : " ") + query;
    List out = listOnce(kind, page, q);
    // Plain words only match titles and descriptions. If nothing turns up on the first page, try the
    // text as a creator name, then as a tag, so "name, creator or tag" all work from one box.
    if (out.ok && out.ids.empty() && page == 1 && !query.empty() && query.find(' ') == std::string::npos) {
        List byUser = listOnce(kind, page, tagPart + (tagPart.empty() ? "" : " ") + "user:" + query);
        if (byUser.ok && !byUser.ids.empty()) return byUser;
        if (tags.empty()) {
            List byTag = listOnce(kind, page, "tag:" + query);
            if (byTag.ok && !byTag.ids.empty()) return byTag;
        }
    }
    return out;
}

RowList listSorted(Kind kind, int page, const std::string& query, const std::vector<std::string>& tags, int order) {
    RowList out;
    static const char* const kinds[3] = {"themes", "splashes", "badges"};
    std::string url = SITE + "/" + kinds[kind] + "/__data.json?x-sveltekit-invalidated=01&page=" + std::to_string(page) +
                      "&sort=" + (order == 1 ? "most-downloaded" : order == 2 ? "most-liked" : "newest");
    if (!query.empty()) url += "&query=" + http::escape(query);
    for (auto& t : tags) url += "&ftag=" + http::escape(t);
    http::Response r = http::get(url, 512 * 1024);
    if (r.status == 0) return out;
    out.reached = true;
    json_t* j = parse(r);
    if (!j) return out;
    // Every node holds one flat array; objects and arrays in it refer to their members by position.
    size_t ni; json_t* node;
    json_array_foreach(json_object_get(j, "nodes"), ni, node) {
        json_t* data = json_object_get(node, "data");
        json_t* root = json_array_get(data, 0);
        json_t* resultRef = json_object_get(root, "result");
        if (!json_is_integer(resultRef)) continue;
        auto at = [&](json_t* ref) -> json_t* { return json_is_integer(ref) && json_integer_value(ref) >= 0 ? json_array_get(data, (size_t)json_integer_value(ref)) : nullptr; };
        auto field = [&](json_t* obj, const char* key) -> json_t* { return at(json_object_get(obj, key)); };
        json_t* result = at(resultRef);
        json_t* pages = field(result, "totalPages");
        if (json_is_integer(pages)) out.pages = (int)json_integer_value(pages);
        size_t i; json_t* ref;
        json_array_foreach(field(result, "items"), i, ref) {
            json_t* it = at(ref);
            if (!json_is_object(it)) continue;
            Row row;
            json_t* id = field(it, "id");
            row.id = json_is_string(id) ? atol(json_string_value(id)) : json_is_integer(id) ? (long)json_integer_value(id) : 0;
            auto text = [&](const char* key) { json_t* v = field(it, key); return std::string(json_is_string(v) ? json_string_value(v) : ""); };
            auto number = [&](const char* key) { json_t* v = field(it, key); return json_is_integer(v) ? (int)json_integer_value(v) : 0; };
            row.title = text("title"); row.author = text("uploader"); row.desc = text("description");
            row.downloads = number("downloads"); row.likes = number("likes");
            row.bgm = json_is_object(field(it, "bgm"));
            if (row.id) out.rows.push_back(std::move(row));
        }
        out.ok = true;
    }
    json_decref(j);
    return out;
}

Details details(long id) {
    Details d;
    json_t* j = parse(http::get(BASE + "/api/anemone/v1/query?item_id=" + std::to_string(id), 64 * 1024));
    if (!j) return d;
    if (json_is_true(json_object_get(j, "success"))) {
        d.ok = true;
        d.title = str(j, "title"); d.desc = str(j, "description"); d.author = str(j, "author");
        d.downloads = num(j, "download_count"); d.likes = num(j, "likes");
        json_t* meta = json_object_get(j, "metadata");
        if (json_is_object(meta)) d.bgm = num(meta, "enable_bgm") != 0;
        size_t i; json_t* v;
        json_array_foreach(json_object_get(j, "tags"), i, v) if (json_is_string(v)) d.tags.push_back(json_string_value(v));
    }
    json_decref(j);
    return d;
}

bool smdh(long id, fmt::Smdh& out) {
    http::Response r = http::get(BASE + "/download/" + std::to_string(id) + "/smdh", 32 * 1024);
    return r.ok() && fmt::parseSmdh(r.body.data(), r.body.size(), out);
}

std::string urlSmdh(long id) { return BASE + "/download/" + std::to_string(id) + "/smdh"; }
std::string urlDownload(long id) { return BASE + "/download/" + std::to_string(id); }
std::string urlPreview(long id, const char* part) { return BASE + "/download/" + std::to_string(id) + "/preview" + (*part ? "/" : "") + part; }
std::string urlBgm(long id) { return BASE + "/download/" + std::to_string(id) + "/bgm"; }

std::string fileName(const std::string& title, const std::string& author, long id) {
    std::string base = title.empty() ? "item" : title;
    if (!author.empty()) base += " by " + author;
    std::string safe;
    for (unsigned char c : base) safe += (c < 32 || strchr("\\/:*?\"<>|", c)) ? '_' : (char)c;
    // keep the name well inside the file system's limit, without cutting a UTF-8 sequence
    size_t max = 96;
    if (safe.size() > max) { while (max > 0 && ((unsigned char)safe[max] & 0xC0) == 0x80) max--; safe.resize(max); }
    while (!safe.empty() && (safe.back() == ' ' || safe.back() == '.')) safe.pop_back();
    return safe + " (" + std::to_string(id) + ").zip";
}

long idFromFileName(const std::string& name) {
    size_t close = name.rfind(')');
    if (close == std::string::npos) return 0;
    size_t open = name.rfind('(', close);
    if (open == std::string::npos || close - open < 2 || close - open > 10) return 0;
    for (size_t i = open + 1; i < close; i++) if (name[i] < '0' || name[i] > '9') return 0;
    return atol(name.c_str() + open + 1);
}

long idFromLink(const std::string& text) {
    if (text.find("themeplaza") == std::string::npos) return 0;
    for (const char* key : {"/download/", "/item/", "item_id="}) {
        size_t p = text.find(key);
        if (p == std::string::npos) continue;
        long id = atol(text.c_str() + p + strlen(key));
        if (id > 0) return id;
    }
    return 0;
}

}  // namespace plaza
