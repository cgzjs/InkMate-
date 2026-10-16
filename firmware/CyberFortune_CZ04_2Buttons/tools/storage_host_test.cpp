#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstring>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "storage_policy.h"

class String {
 public:
  std::string value;
  String(const char *s = "") : value(s) {}
  String(std::string s) : value(std::move(s)) {}
  size_t length() const { return value.size(); }
  const char *c_str() const { return value.c_str(); }
  int indexOf(char c) const { auto p = value.find(c); return p == std::string::npos ? -1 : (int)p; }
  String substring(size_t p) const { return value.substr(p); }
  String substring(size_t p, size_t q) const { return value.substr(p, q - p); }
  void trim() {
    auto first = value.find_first_not_of(" \t\r\n");
    value = first == std::string::npos ? "" : value.substr(first, value.find_last_not_of(" \t\r\n") - first + 1);
  }
  void replace(const char *a, const char *b) {
    size_t p = 0;
    while ((p = value.find(a, p)) != std::string::npos) { value.replace(p, strlen(a), b); p += strlen(b); }
  }
  String &operator+=(char c) { value += c; return *this; }
  bool operator==(const char *s) const { return value == s; }
  bool operator!=(const char *s) const { return value != s; }
};

bool writeFailure = false, renameFailure = false;
using Bytes = std::vector<uint8_t>;
class File {
 public:
  std::shared_ptr<Bytes> bytes;
  size_t pos = 0;
  File() = default;
  explicit File(std::shared_ptr<Bytes> b) : bytes(std::move(b)) {}
  explicit operator bool() const { return bool(bytes); }
  void close() { bytes.reset(); }
  size_t size() const { return bytes ? bytes->size() : 0; }
  bool seek(size_t p) { if (!bytes) return false; pos = p; return true; }
  uint32_t position() const { return pos; }
  bool available() const { return bytes && pos < bytes->size(); }
  int peek() const { return available() ? (*bytes)[pos] : -1; }
  int read() { return available() ? (*bytes)[pos++] : -1; }
  size_t read(uint8_t *out, size_t count) {
    if (!bytes || pos >= bytes->size()) return 0;
    count = std::min(count, bytes->size() - pos);
    memcpy(out, bytes->data() + pos, count); pos += count; return count;
  }
  size_t write(const uint8_t *in, size_t count) {
    if (!bytes || writeFailure) return 0;
    if (pos + count > bytes->size()) bytes->resize(pos + count);
    memcpy(bytes->data() + pos, in, count); pos += count; return count;
  }
  String readStringUntil(char c) {
    std::string s;
    while (available()) { char v = read(); if (v == c) break; s += v; }
    return s;
  }
};
struct FakeFS {
  std::map<std::string, std::shared_ptr<Bytes>> files;
  bool exists(const char *p) const { return files.count(p); }
  bool remove(const char *p) { return files.erase(p) != 0; }
  File open(const char *p, const char *mode) {
    if (strcmp(mode, "w") == 0) files[p] = std::make_shared<Bytes>();
    auto i = files.find(p); return i == files.end() ? File() : File(i->second);
  }
  bool rename(const char *a, const char *b) {
    if (renameFailure || !exists(a)) return false;
    files[b] = files[a]; files.erase(a); return true;
  }
  void put(const char *p, std::string text) { files[p] = std::make_shared<Bytes>(text.begin(), text.end()); }
  std::string get(const char *p) const {
    const auto &v = *files.at(p); return std::string(v.begin(), v.end());
  }
} LittleFS;
enum { UPLOAD_FILE_START, UPLOAD_FILE_WRITE, UPLOAD_FILE_END, UPLOAD_FILE_ABORTED };
struct HTTPUpload {
  int status = 0;
  String name;
  uint8_t *buf = nullptr;
  size_t currentSize = 0, totalSize = 0;
};
struct FakeServer {
  HTTPUpload state;
  String route = "/book";
  std::map<std::string, String> args;
  int responseCode = 0;
  String response;
  HTTPUpload &upload() { return state; }
  String uri() const { return route; }
  String arg(const char *p) { return args[p]; }
  bool hasArg(const char *p) const { return args.count(p); }
  void send(int code, const char *, String body) { responseCode = code; response = body; }
} server;
struct FakePreferences {
  std::map<std::string, String> data;
  void remove(const char *p) { data.erase(p); }
  void putString(const char *p, String v) { data[p] = v; }
} prefs;
bool large = false, fileSystemReady = true;
uint32_t availableBytes = 640 * 1024;
uint32_t contentUploadMax(bool vocab) { return storageUploadLimit(large, vocab, availableBytes); }
uint16_t vocabWordLimit() { return large ? 20000 : 5000; }
String truncateUtf8Units(String v, int) { return v; }
void delay(int) {}
uint8_t utf8SequenceLength(uint8_t c) { return c < 0x80 ? 1 : (c < 0xE0 ? 2 : (c < 0xF0 ? 3 : 4)); }
constexpr uint8_t BOOK_LINES_PER_PAGE = 5, BOOK_LINE_UNITS = 32;
constexpr uint32_t BOOK_CHECKPOINT_PAGES = 64;
constexpr uint16_t VOCAB_CHECKPOINT_WORDS = 64;
const char *BOOK_PATH = "/book.txt", *BOOK_INDEX_PATH = "/book.idx";
const char *VOCAB_CET6_PATH = "/vocab_cet6.tsv", *VOCAB_IELTS_PATH = "/vocab_ielts.tsv";
const char *VOCAB_CET6_PROGRESS_PATH = "/vocab_cet6.dat", *VOCAB_IELTS_PROGRESS_PATH = "/vocab_ielts.dat";
const char *CONTENT_UPLOAD_PATH = "/content.upload";
bool bookReaderActive = false, bookHasNext = false;
uint32_t bookPageIndex = 0, bookCurrentOffset = 0, bookNextOffset = 0;
uint8_t vocabDeck = 0, vocabIndexedDeck = 0xFF, vocabQueueCount = 0;
uint16_t vocabWordCount = 0;
bool vocabStudyActive = false;
const char *vocabWordPath() { return vocabDeck == 1 ? VOCAB_IELTS_PATH : VOCAB_CET6_PATH; }
const char *vocabIndexPath() { return vocabDeck == 1 ? "/vocab_ielts.idx" : "/vocab_cet6.idx"; }
File contentUploadFile;
String contentUploadError, contentUploadTarget, contentUploadLine;
bool contentUploadComplete = false, contentUploadStarted = false, contentUploadIsVocab = false;
uint32_t contentUploadBytes = 0, contentUploadLimit = 0;
uint16_t contentUploadWords = 0;
uint8_t currentPage = 0;
constexpr uint8_t PAGE_EBOOK = 20, PAGE_VOCAB = 24;
bool screenOff = true;
void renderVocabPage() {}
void renderEbookPage() {}
void renderMessage(const char *, const char *, const char *, const char *) {}
bool readBookPage(uint32_t, String[BOOK_LINES_PER_PAGE], uint32_t &, bool &);
void renderBookReaderPage() {
  String lines[BOOK_LINES_PER_PAGE];
  assert(readBookPage(bookCurrentOffset, lines, bookNextOffset, bookHasNext));
}

#include "firmware_functions.inc"

void reset() {
  contentUploadFile.close(); LittleFS.files.clear(); prefs.data.clear();
  writeFailure = renameFailure = false; large = false; fileSystemReady = true;
  availableBytes = 640 * 1024;
  contentUploadStarted = contentUploadComplete = false;
  contentUploadError = ""; server = FakeServer();
  vocabDeck = 0; vocabIndexedDeck = 0xFF; vocabWordCount = 0;
  resetBookPagination();
}
void begin(bool vocabulary = false) {
  server.route = vocabulary ? "/vocab-file" : "/book";
  server.args["deck"] = "0";
  server.state.name = vocabulary ? "vocabulary" : "book";
  server.state.status = UPLOAD_FILE_START; server.state.totalSize = 0;
  handleContentUpload();
}
void chunk(std::string text) {
  server.state.status = UPLOAD_FILE_WRITE;
  server.state.buf = (uint8_t *)text.data(); server.state.currentSize = text.size();
  handleContentUpload(); server.state.totalSize += text.size();
}
void finish() {
  server.state.status = UPLOAD_FILE_END; handleContentUpload(); handleContentUploadFinished();
}
void upload(std::string text, bool vocab = false, size_t block = 1024) {
  begin(vocab);
  for (size_t p = 0; p < text.size(); p += block) chunk(text.substr(p, block));
  finish();
}
std::string words(size_t count) {
  std::string s;
  for (size_t i = 0; i < count; i++) s += "word" + std::to_string(i) + "\tmeaning\n";
  return s;
}

int main() {
  assert(storageUploadLimit(false, false, 640 * 1024) == 256 * 1024);
  assert(storageUploadLimit(true, false, 0xCA0000) == 8 * 1024 * 1024);
  assert(storageUploadLimit(true, true, 0xCA0000) == 2 * 1024 * 1024);
  assert(storageUploadLimit(false, false, 65536) == 0);
  assert(storageUploadLimit(false, false, 65537) == 1);
  reset(); std::string book(128 * 1024, 'a'); LittleFS.put(BOOK_PATH, "old");
  upload(book); assert(server.responseCode == 200 && LittleFS.get(BOOK_PATH) == book);
  reset(); LittleFS.put(BOOK_PATH, "old"); upload(std::string(256 * 1024 + 1, 'a'));
  assert(server.responseCode == 400 && LittleFS.get(BOOK_PATH) == "old");
  reset(); LittleFS.put(BOOK_PATH, "old"); begin(); chunk("new");
  server.state.status = UPLOAD_FILE_ABORTED; handleContentUpload();
  assert(LittleFS.get(BOOK_PATH) == "old" && !LittleFS.exists(CONTENT_UPLOAD_PATH));
  upload("retry"); assert(server.responseCode == 200 && LittleFS.get(BOOK_PATH) == "retry");
  reset(); LittleFS.put(BOOK_PATH, "old"); writeFailure = true; upload("new");
  assert(server.responseCode == 400 && LittleFS.get(BOOK_PATH) == "old");
  reset(); LittleFS.put(BOOK_PATH, "old"); renameFailure = true; upload("new");
  assert(server.responseCode == 400 && LittleFS.get(BOOK_PATH) == "old");
  reset(); LittleFS.put(BOOK_PATH, "old"); begin(); chunk("new");
  server.state.status = UPLOAD_FILE_START; handleContentUpload(); finish();
  assert(server.responseCode == 400 && LittleFS.get(BOOK_PATH) == "old");
  reset(); availableBytes = 65536; upload("new"); assert(server.responseCode == 400);
  reset(); fileSystemReady = false; upload("new"); assert(server.responseCode == 400);
  reset(); upload(""); assert(server.responseCode == 400);
  reset(); large = true; availableBytes = 0xCA0000;
  upload(std::string(8 * 1024 * 1024, 'a'), false, 4096);
  assert(server.responseCode == 200 && LittleFS.files.at(BOOK_PATH)->size() == 8 * 1024 * 1024);
  reset(); large = true; availableBytes = 0xCA0000; LittleFS.put(BOOK_PATH, "keep");
  upload(std::string(8 * 1024 * 1024 + 1, 'a'), false, 4096);
  assert(server.responseCode == 400 && LittleFS.get(BOOK_PATH) == "keep");
  reset(); upload("word\tmeaning\nsecond\ttranslation", true, 3);
  assert(server.responseCode == 200 && contentUploadWords == 2);
  String word, meaning;
  assert(readVocabWord(1, word, meaning) && word == "second" && meaning == "translation");
  reset(); LittleFS.put(VOCAB_CET6_PATH, "old\tmeaning\n"); upload("bad\n", true);
  assert(server.responseCode == 400 && LittleFS.get(VOCAB_CET6_PATH) == "old\tmeaning\n");
  reset(); upload(std::string(257, 'a') + "\tmeaning\n", true); assert(server.responseCode == 400);
  reset(); upload(words(5001), true); assert(server.responseCode == 400);
  reset(); large = true; availableBytes = 0xCA0000;
  LittleFS.put(VOCAB_IELTS_PROGRESS_PATH, "other-progress");
  prefs.data["vDoneI"] = "keep"; prefs.data["vDoneC"] = "clear";
  upload(words(20000), true, 4096);
  assert(server.responseCode == 200 && contentUploadWords == 20000);
  assert(LittleFS.get(VOCAB_IELTS_PROGRESS_PATH) == "other-progress");
  assert(prefs.data.count("vDoneI") && !prefs.data.count("vDoneC"));
  assert(readVocabWord(19999, word, meaning) && word == "word19999");
  assert(LittleFS.files.at(vocabIndexPath())->size() < 2048);
  vocabIndexedDeck = 0xFF; vocabWordCount = 0;
  assert(loadVocabIndex() && vocabWordCount == 20000);
  reset(); large = true; availableBytes = 0xCA0000; upload(words(20001), true);
  assert(server.responseCode == 400);
  reset(); std::string pages;
  for (int i = 0; i < 9000; i++) pages += "中文阅读 line of words, continued\r\n";
  LittleFS.put(BOOK_PATH, pages); resetBookPagination(); renderBookReaderPage();
  std::vector<uint32_t> offsets{0};
  for (int i = 0; i < 1300; i++) {
    assert(bookHasNext); offsets.push_back(bookNextOffset); nextBookPage();
    assert(bookPageIndex == (uint32_t)i + 1);
  }
  assert(LittleFS.files.at(BOOK_INDEX_PATH)->size() <= (1300 / 64 + 1) * 4);
  for (int i = 1300; i > 0; i--) {
    previousBookPage(); assert(bookPageIndex == (uint32_t)i - 1);
    assert(bookCurrentOffset == offsets[i - 1]);
  }
  previousBookPage(); assert(bookPageIndex == 0);
  std::cout << "PASS: storage limits, streaming upload, abort/retry, atomic replacement, "
               "vocabulary validation, 20k disk index, isolated progress reset, 1300-page navigation\n";
}
