// ServiceSetup — see ServiceSetup.hpp for the three rules this file obeys.
//
// Everything that can block lives in the anonymous namespace at the top:
//   * Proc      — g_spawn with a DEADLINE and a cancel flag. Nothing here ever
//                 waits forever on a child, including an open polkit dialog.
//   * Http      — a small HTTP/1.1 GET over GIO's TLS socket client. GIO is
//                 already a dependency (the SNI tray uses raw GDBus from the
//                 same library) and glib-networking is present in every
//                 environment this app runs in — the GNOME runtime requires it
//                 and no GTK desktop is without it. That buys https with no
//                 new dependency and no curl subprocess whose absence inside a
//                 Flatpak runtime would be a silent failure.
//   * Sha256    — GChecksum, streamed; the file is never held in memory.
//
// SPDX-License-Identifier: MPL-2.0
#include "ServiceSetup.hpp"

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <utility>

#include <gio/gio.h>
#include <glib.h>
#include <glib/gstdio.h>

#include <nlohmann/json.hpp>

#include "AppPrefs.hpp"
#include "ControlClient.hpp"
#include "Ui.hpp"

#ifndef UR_APP_VERSION
#define UR_APP_VERSION "0.0.0"
#endif

namespace urnw {
namespace {

// ---------------------------------------------------------------------------
// logging (the app's idiom: a tagged line on stderr, which the log tail picks
// up). Never logs a URL's query or a path outside the cache.
// ---------------------------------------------------------------------------
template <typename... Args>
void Log(const char* fmt, Args... args) {
  std::fprintf(stderr, fmt, args...);
  std::fputc('\n', stderr);
}

std::int64_t NowMs() { return g_get_monotonic_time() / 1000; }

std::string Trim(const std::string& s) {
  const auto b = s.find_first_not_of(" \t\r\n");
  if (b == std::string::npos) return {};
  const auto e = s.find_last_not_of(" \t\r\n");
  return s.substr(b, e - b + 1);
}

std::string LowerAscii(std::string s) {
  for (char& c : s) {
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  }
  return s;
}

// The last non-empty line of a child's stderr — the one sentence worth keeping
// out of an installer that printed forty.
std::string LastLine(const std::string& text) {
  std::size_t end = text.find_last_not_of("\r\n \t");
  if (end == std::string::npos) return {};
  const std::size_t start = text.find_last_of('\n', end);
  return Trim(text.substr(start == std::string::npos ? 0 : start + 1, end - (start == std::string::npos ? 0 : start)));
}

std::string ShellQuote(const std::string& s) {
  gchar* q = g_shell_quote(s.c_str());
  std::string out = q ? q : s;
  g_free(q);
  return out;
}

// ---------------------------------------------------------------------------
// filesystem helpers
// ---------------------------------------------------------------------------
bool PathExists(const std::string& p) {
  return !p.empty() && g_file_test(p.c_str(), G_FILE_TEST_EXISTS);
}

bool RemoveTree(const std::string& path) {
  if (path.empty()) return true;
  if (!g_file_test(path.c_str(), G_FILE_TEST_IS_DIR) ||
      g_file_test(path.c_str(), G_FILE_TEST_IS_SYMLINK)) {
    return g_remove(path.c_str()) == 0 || !PathExists(path);
  }
  GDir* dir = g_dir_open(path.c_str(), 0, nullptr);
  if (dir) {
    const gchar* name = nullptr;
    while ((name = g_dir_read_name(dir)) != nullptr) {
      RemoveTree(path + "/" + name);
    }
    g_dir_close(dir);
  }
  return g_rmdir(path.c_str()) == 0 || !PathExists(path);
}

// ---------------------------------------------------------------------------
// Proc: a child process with a deadline. Used for systemctl, tar, pkexec and
// flatpak-spawn. The deadline is the reason there is no spinner that never
// settles; the cancel flag is the reason Cancel() takes ~100 ms and not 15
// minutes when a polkit dialog is sitting open.
// ---------------------------------------------------------------------------
struct ProcResult {
  bool spawned = false;
  bool exited = false;
  bool timedOut = false;
  bool cancelled = false;  // OUR cancel flag fired, so we killed it
  int exitCode = -1;
  std::string out;
  std::string err;
  std::string spawnError;
};

constexpr std::size_t kMaxCapturedOutput = 256 * 1024;

ProcResult RunProcess(const std::vector<std::string>& argv, int timeoutMs,
                      const std::atomic<bool>* cancel) {
  ProcResult r;
  if (argv.empty()) {
    r.spawnError = "empty argv";
    return r;
  }
  std::vector<char*> cargv;
  cargv.reserve(argv.size() + 1);
  for (const auto& a : argv) cargv.push_back(const_cast<char*>(a.c_str()));
  cargv.push_back(nullptr);

  GPid pid = 0;
  gint outFd = -1;
  gint errFd = -1;
  GError* error = nullptr;
  const gboolean ok = g_spawn_async_with_pipes(
      nullptr, cargv.data(), nullptr,
      static_cast<GSpawnFlags>(G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD),
      nullptr, nullptr, &pid, nullptr, &outFd, &errFd, &error);
  if (!ok) {
    r.spawnError = error && error->message ? error->message : "spawn failed";
    if (error) g_error_free(error);
    return r;
  }
  r.spawned = true;

  const int flagsOut = fcntl(outFd, F_GETFL, 0);
  if (flagsOut >= 0) fcntl(outFd, F_SETFL, flagsOut | O_NONBLOCK);
  const int flagsErr = fcntl(errFd, F_GETFL, 0);
  if (flagsErr >= 0) fcntl(errFd, F_SETFL, flagsErr | O_NONBLOCK);

  const std::int64_t deadline = timeoutMs > 0 ? NowMs() + timeoutMs : 0;
  bool outEof = false;
  bool errEof = false;
  bool reaped = false;
  int status = 0;
  bool killedTerm = false;
  bool killedKill = false;
  std::int64_t killedAtMs = 0;
  std::int64_t reapedAtMs = 0;

  auto drain = [&](int fd, std::string& into, bool& eof) {
    if (fd < 0 || eof) return;
    for (;;) {
      char buf[4096];
      const ssize_t n = read(fd, buf, sizeof(buf));
      if (n > 0) {
        if (into.size() < kMaxCapturedOutput) {
          into.append(buf, static_cast<std::size_t>(n));
          if (into.size() > kMaxCapturedOutput) into.resize(kMaxCapturedOutput);
        }
        continue;
      }
      if (n == 0) {
        eof = true;
        return;
      }
      if (errno == EINTR) continue;
      if (errno == EAGAIN || errno == EWOULDBLOCK) return;
      eof = true;
      return;
    }
  };

  while (true) {
    struct pollfd fds[2];
    int nfds = 0;
    if (!outEof && outFd >= 0) {
      fds[nfds].fd = outFd;
      fds[nfds].events = POLLIN;
      fds[nfds].revents = 0;
      ++nfds;
    }
    if (!errEof && errFd >= 0) {
      fds[nfds].fd = errFd;
      fds[nfds].events = POLLIN;
      fds[nfds].revents = 0;
      ++nfds;
    }
    if (nfds > 0) {
      const int rc = poll(fds, static_cast<nfds_t>(nfds), 100);
      if (rc > 0) {
        // Both fds are non-blocking, so draining the one that was not flagged
        // costs a single EAGAIN and keeps this branch free of index bookkeeping.
        if (!outEof && outFd >= 0) drain(outFd, r.out, outEof);
        if (!errEof && errFd >= 0) drain(errFd, r.err, errEof);
      }
    } else {
      g_usleep(50 * 1000);
    }

    if (!reaped) {
      const pid_t w = waitpid(static_cast<pid_t>(pid), &status, WNOHANG);
      if (w == static_cast<pid_t>(pid)) {
        reaped = true;
        reapedAtMs = NowMs();
      } else if (w < 0 && errno != EINTR) {
        // Nothing left to wait for: treat as reaped so the loop can end.
        reaped = true;
        reapedAtMs = NowMs();
        status = 0;
      }
    }
    // Leave once the child is gone AND its pipes are drained, so a fast exit
    // never loses the last line of stderr...
    if (reaped && outEof && errEof) break;
    if (reaped && killedKill) break;
    // ...but NEVER wait on the pipes forever. `pkexec install.sh` leaves
    // grandchildren (systemctl, udevadm, restorecon) holding the inherited
    // write ends, and one of those outliving the installer would otherwise
    // hang this loop after the exit status was already known. The status is
    // the contract; the output is a courtesy.
    if (reaped && NowMs() - reapedAtMs > 3000) break;

    const bool wantCancel = cancel != nullptr && cancel->load();
    const bool wantTimeout = deadline != 0 && NowMs() > deadline;
    if ((wantCancel || wantTimeout) && !killedTerm) {
      r.cancelled = wantCancel;
      r.timedOut = wantTimeout && !wantCancel;
      kill(static_cast<pid_t>(pid), SIGTERM);
      killedTerm = true;
      killedAtMs = NowMs();
    } else if (killedTerm && !killedKill && NowMs() - killedAtMs > 2000) {
      kill(static_cast<pid_t>(pid), SIGKILL);
      killedKill = true;
    }
  }

  if (outFd >= 0) close(outFd);
  if (errFd >= 0) close(errFd);
  g_spawn_close_pid(pid);

  if (WIFEXITED(status)) {
    r.exited = true;
    r.exitCode = WEXITSTATUS(status);
  } else if (WIFSIGNALED(status)) {
    r.exited = false;
    r.exitCode = 128 + WTERMSIG(status);
  }
  return r;
}

bool OnPath(const char* program) {
  gchar* found = g_find_program_in_path(program);
  const bool ok = found != nullptr;
  g_free(found);
  return ok;
}

// ---------------------------------------------------------------------------
// Sha256 (GChecksum, streamed)
// ---------------------------------------------------------------------------
std::string Sha256File(const std::string& path, const std::atomic<bool>* cancel) {
  std::ifstream in(path, std::ios::binary);
  if (!in.good()) return {};
  GChecksum* sum = g_checksum_new(G_CHECKSUM_SHA256);
  if (!sum) return {};
  std::vector<char> buf(64 * 1024);
  while (in.good()) {
    in.read(buf.data(), static_cast<std::streamsize>(buf.size()));
    const std::streamsize n = in.gcount();
    if (n > 0) {
      g_checksum_update(sum, reinterpret_cast<const guchar*>(buf.data()),
                        static_cast<gssize>(n));
    }
    if (cancel != nullptr && cancel->load()) {
      g_checksum_free(sum);
      return {};
    }
  }
  const gchar* hex = g_checksum_get_string(sum);
  std::string out = hex ? hex : "";
  g_checksum_free(sum);
  return LowerAscii(out);
}

// ---------------------------------------------------------------------------
// Http: one GET, over TLS, with a per-read timeout and a byte cap.
// Handles content-length, chunked, connection-close and up to 5 redirects —
// which is exactly what GitHub's releases API and its asset CDN hop need.
// ---------------------------------------------------------------------------
struct HttpOutcome {
  bool ok = false;
  int status = 0;
  std::string error;        // transport-level, English, for the log
  bool tlsUnavailable = false;
  bool rateLimited = false;
  std::int64_t bytes = 0;
};

using HttpSink = std::function<bool(const char*, std::size_t)>;
using HttpProgress = std::function<void(std::int64_t, std::int64_t)>;

constexpr int kSocketTimeoutSeconds = 20;
constexpr std::size_t kMaxHeaderBytes = 64 * 1024;
constexpr std::int64_t kMaxJsonBytes = 8 * 1024 * 1024;
constexpr std::int64_t kMaxAssetBytes = 400LL * 1024 * 1024;

struct StreamReader {
  GInputStream* in = nullptr;
  GCancellable* cancellable = nullptr;
  const std::atomic<bool>* cancel = nullptr;
  std::string buf;
  std::size_t pos = 0;
  bool eof = false;
  std::string error;

  void Compact() {
    if (pos > 32 * 1024) {
      buf.erase(0, pos);
      pos = 0;
    }
  }

  bool Fill() {
    if (eof) return false;
    char tmp[16 * 1024];
    GError* err = nullptr;
    const gssize n = g_input_stream_read(in, tmp, sizeof(tmp), cancellable, &err);
    if (n < 0) {
      error = err && err->message ? err->message : "read failed";
      if (err) g_error_free(err);
      eof = true;
      return false;
    }
    if (n == 0) {
      eof = true;
      return false;
    }
    buf.append(tmp, static_cast<std::size_t>(n));
    return true;
  }

  // A CRLF- (or bare LF-) terminated line, without its terminator.
  bool Line(std::string& out) {
    for (;;) {
      const std::size_t nl = buf.find('\n', pos);
      if (nl != std::string::npos) {
        std::size_t end = nl;
        if (end > pos && buf[end - 1] == '\r') --end;
        out.assign(buf, pos, end - pos);
        pos = nl + 1;
        Compact();
        return true;
      }
      if (buf.size() - pos > kMaxHeaderBytes) {
        error = "header line too long";
        return false;
      }
      if (!Fill()) return false;
    }
  }

  // want < 0 reads to EOF. Returns false on error/abort.
  bool Consume(std::int64_t want, const HttpSink& sink, std::int64_t* counted) {
    std::int64_t remaining = want;
    while (remaining != 0) {
      if (pos >= buf.size()) {
        if (!Fill()) {
          if (!error.empty()) return false;
          return want < 0;  // EOF only ends a read-to-EOF body cleanly
        }
      }
      std::size_t avail = buf.size() - pos;
      if (remaining > 0 && static_cast<std::int64_t>(avail) > remaining) {
        avail = static_cast<std::size_t>(remaining);
      }
      if (!sink(buf.data() + pos, avail)) {
        error = "aborted";
        return false;
      }
      pos += avail;
      if (counted) *counted += static_cast<std::int64_t>(avail);
      if (remaining > 0) remaining -= static_cast<std::int64_t>(avail);
      Compact();
      if (cancel != nullptr && cancel->load()) {
        error = "cancelled";
        return false;
      }
    }
    return true;
  }
};

std::string HeaderValue(const std::vector<std::pair<std::string, std::string>>& headers,
                        const char* name) {
  for (const auto& h : headers) {
    if (h.first == name) return h.second;
  }
  return {};
}

HttpOutcome HttpGetOnce(const std::string& url, const char* accept, std::int64_t maxBytes,
                        const HttpSink& sink, const HttpProgress& progress,
                        const std::atomic<bool>* cancel, std::string* redirectTo);

HttpOutcome HttpGet(const std::string& url, const char* accept, std::int64_t maxBytes,
                    const HttpSink& sink, const HttpProgress& progress,
                    const std::atomic<bool>* cancel) {
  std::string current = url;
  for (int hop = 0; hop < 6; ++hop) {
    std::string next;
    HttpOutcome outcome = HttpGetOnce(current, accept, maxBytes, sink, progress, cancel, &next);
    if (next.empty()) return outcome;
    current = next;
  }
  HttpOutcome tooMany;
  tooMany.error = "too many redirects";
  return tooMany;
}

HttpOutcome HttpGetOnce(const std::string& url, const char* accept, std::int64_t maxBytes,
                        const HttpSink& sink, const HttpProgress& progress,
                        const std::atomic<bool>* cancel, std::string* redirectTo) {
  HttpOutcome result;
  redirectTo->clear();

  GError* error = nullptr;
  GUri* uri = g_uri_parse(url.c_str(), G_URI_FLAGS_ENCODED, &error);
  if (!uri) {
    result.error = error && error->message ? error->message : "bad url";
    if (error) g_error_free(error);
    return result;
  }
  const std::string scheme = g_uri_get_scheme(uri) ? g_uri_get_scheme(uri) : "";
  const std::string host = g_uri_get_host(uri) ? g_uri_get_host(uri) : "";
  std::string path = g_uri_get_path(uri) ? g_uri_get_path(uri) : "";
  const std::string query = g_uri_get_query(uri) ? g_uri_get_query(uri) : "";
  const gint port = g_uri_get_port(uri);
  g_uri_unref(uri);

  if (scheme != "https") {
    result.error = "refusing a non-https url";
    return result;
  }
  if (host.empty()) {
    result.error = "url has no host";
    return result;
  }
  if (path.empty()) path = "/";
  if (!query.empty()) path += "?" + query;

  // No TLS backend means https cannot be spoken at all — a distinct,
  // actionable state (install glib-networking), never a mystery timeout.
  GTlsBackend* backend = g_tls_backend_get_default();
  if (backend == nullptr || !g_tls_backend_supports_tls(backend)) {
    result.error = "no TLS backend (glib-networking is missing)";
    result.tlsUnavailable = true;
    return result;
  }

  GSocketClient* client = g_socket_client_new();
  g_socket_client_set_tls(client, TRUE);
  g_socket_client_set_timeout(client, kSocketTimeoutSeconds);
  GCancellable* cancellable = g_cancellable_new();

  const std::string hostAndPort =
      port > 0 ? host + ":" + std::to_string(static_cast<int>(port)) : host;
  GSocketConnection* conn =
      g_socket_client_connect_to_host(client, hostAndPort.c_str(), 443, cancellable, &error);
  if (!conn) {
    result.error = error && error->message ? error->message : "connect failed";
    if (error) {
      if (error->domain == G_TLS_ERROR) result.tlsUnavailable = false;
      g_error_free(error);
    }
    g_object_unref(cancellable);
    g_object_unref(client);
    return result;
  }

  GOutputStream* out = g_io_stream_get_output_stream(G_IO_STREAM(conn));
  GInputStream* in = g_io_stream_get_input_stream(G_IO_STREAM(conn));

  std::string request;
  request += "GET " + path + " HTTP/1.1\r\n";
  request += "Host: " + host + "\r\n";
  request += "User-Agent: urnetwork-linux/" UR_APP_VERSION " (+https://ur.io)\r\n";
  request += std::string("Accept: ") + (accept ? accept : "*/*") + "\r\n";
  request += "Accept-Encoding: identity\r\n";
  request += "X-GitHub-Api-Version: 2022-11-28\r\n";
  request += "Connection: close\r\n\r\n";

  gsize written = 0;
  if (!g_output_stream_write_all(out, request.data(), request.size(), &written, cancellable,
                                 &error)) {
    result.error = error && error->message ? error->message : "write failed";
    if (error) g_error_free(error);
    g_object_unref(conn);
    g_object_unref(cancellable);
    g_object_unref(client);
    return result;
  }

  StreamReader reader;
  reader.in = in;
  reader.cancellable = cancellable;
  reader.cancel = cancel;

  auto finish = [&](HttpOutcome outcome) {
    g_object_unref(conn);
    g_object_unref(cancellable);
    g_object_unref(client);
    return outcome;
  };

  std::string statusLine;
  if (!reader.Line(statusLine)) {
    result.error = reader.error.empty() ? "no response" : reader.error;
    return finish(result);
  }
  {
    const std::size_t sp = statusLine.find(' ');
    if (sp == std::string::npos) {
      result.error = "malformed status line";
      return finish(result);
    }
    result.status = std::atoi(statusLine.c_str() + sp + 1);
  }

  std::vector<std::pair<std::string, std::string>> headers;
  std::size_t headerBytes = 0;
  for (;;) {
    std::string line;
    if (!reader.Line(line)) {
      result.error = reader.error.empty() ? "truncated headers" : reader.error;
      return finish(result);
    }
    if (line.empty()) break;
    headerBytes += line.size();
    if (headerBytes > kMaxHeaderBytes) {
      result.error = "headers too large";
      return finish(result);
    }
    const std::size_t colon = line.find(':');
    if (colon == std::string::npos) continue;
    headers.emplace_back(LowerAscii(Trim(line.substr(0, colon))), Trim(line.substr(colon + 1)));
  }

  // Redirects: GitHub's browser_download_url always hops to a CDN host.
  if (result.status == 301 || result.status == 302 || result.status == 303 ||
      result.status == 307 || result.status == 308) {
    std::string location = HeaderValue(headers, "location");
    if (location.empty()) {
      result.error = "redirect without a location";
      return finish(result);
    }
    if (location.compare(0, 8, "https://") != 0) {
      if (!location.empty() && location[0] == '/') {
        location = "https://" + host + location;
      } else {
        result.error = "refusing a non-https redirect";
        return finish(result);
      }
    }
    *redirectTo = location;
    result.ok = false;
    return finish(result);
  }

  // 403/429 from api.github.com with a spent quota is the one HTTP failure the
  // user can do something about (wait, or try later), so it gets its own state.
  if (result.status == 429 ||
      (result.status == 403 && HeaderValue(headers, "x-ratelimit-remaining") == "0")) {
    result.rateLimited = true;
  }

  const std::string transferEncoding = LowerAscii(HeaderValue(headers, "transfer-encoding"));
  const std::string contentLength = HeaderValue(headers, "content-length");
  const std::int64_t declared = contentLength.empty() ? -1 : std::atoll(contentLength.c_str());
  if (declared > maxBytes) {
    result.error = "response larger than the cap";
    return finish(result);
  }

  std::int64_t received = 0;
  std::int64_t lastReport = -1;
  HttpSink guarded = [&](const char* data, std::size_t n) {
    if (received + static_cast<std::int64_t>(n) > maxBytes) return false;
    if (!sink(data, n)) return false;
    received += static_cast<std::int64_t>(n);
    if (progress && (received - lastReport > 256 * 1024 || lastReport < 0)) {
      lastReport = received;
      progress(received, declared);
    }
    return true;
  };

  bool bodyOk = false;
  if (transferEncoding.find("chunked") != std::string::npos) {
    bodyOk = true;
    for (;;) {
      std::string sizeLine;
      if (!reader.Line(sizeLine)) {
        bodyOk = false;
        break;
      }
      const std::size_t semi = sizeLine.find(';');
      if (semi != std::string::npos) sizeLine = sizeLine.substr(0, semi);
      const std::int64_t chunk = std::strtoll(Trim(sizeLine).c_str(), nullptr, 16);
      if (chunk <= 0) break;  // the terminating 0 chunk (trailers are ignored)
      if (!reader.Consume(chunk, guarded, nullptr)) {
        bodyOk = false;
        break;
      }
      std::string crlf;
      if (!reader.Line(crlf)) {
        bodyOk = false;
        break;
      }
    }
  } else if (declared >= 0) {
    bodyOk = reader.Consume(declared, guarded, nullptr);
  } else {
    bodyOk = reader.Consume(-1, guarded, nullptr);
  }

  if (!bodyOk) {
    result.error = reader.error.empty() ? "body transfer failed" : reader.error;
    return finish(result);
  }
  if (progress) progress(received, declared >= 0 ? declared : received);
  result.bytes = received;
  result.ok = result.status == 200;
  return finish(result);
}

// ---------------------------------------------------------------------------
// json helpers (parse(..., false) never throws; a malformed body is discarded)
// ---------------------------------------------------------------------------
std::string JsonStr(const nlohmann::json& obj, const char* key) {
  if (!obj.is_object()) return {};
  const auto it = obj.find(key);
  if (it == obj.end() || !it->is_string()) return {};
  return it->get<std::string>();
}
bool JsonFlag(const nlohmann::json& obj, const char* key) {
  if (!obj.is_object()) return false;
  const auto it = obj.find(key);
  return it != obj.end() && it->is_boolean() && it->get<bool>();
}
std::int64_t JsonInt(const nlohmann::json& obj, const char* key) {
  if (!obj.is_object()) return 0;
  const auto it = obj.find(key);
  if (it == obj.end() || !it->is_number_integer()) return 0;
  return it->get<std::int64_t>();
}

// ---------------------------------------------------------------------------
// the prefs keys (AppPrefs.hpp does the whole-file read-modify-write)
// ---------------------------------------------------------------------------
constexpr const char* kPrefChannel = "service_channel";
constexpr const char* kPrefRepo = "service_channel_repo";
constexpr const char* kPrefTag = "service_channel_tag";

constexpr const char* kUnitName = "urnetworkd.service";

// One process-lifetime memo: once flatpak-spawn has been proven unusable
// (the portal refused, or --talk-name=org.freedesktop.Flatpak was not
// granted), stop paying a failed round trip on every refresh.
std::atomic<bool> gHostSpawnKnownBad{false};

}  // namespace

// ---------------------------------------------------------------------------
// small statics
// ---------------------------------------------------------------------------

const char* ServiceSetup::DefaultBetaRepo() { return "Ryanmello07/urnetwork-linux"; }
const char* ServiceSetup::UpstreamRepo() { return "urnetwork/urnetwork-linux"; }

bool ServiceSetup::ChannelAvailable(Channel channel) {
  // Upstream publishes no Linux daemon build yet. Settings GREYS THE ROW OUT
  // rather than hiding it — a hidden option cannot say "not yet".
  return channel != Channel::UpstreamMain;
}

std::string ServiceSetup::ChannelUnavailableReason(Channel channel) {
  if (channel == Channel::UpstreamMain) {
    return "The upstream repository does not publish a Linux service build yet.";
  }
  return {};
}

const char* ServiceSetup::ChannelRepo(const ChannelConfig& config) {
  if (!config.repo.empty()) return config.repo.c_str();
  return config.channel == Channel::UpstreamMain ? UpstreamRepo() : DefaultBetaRepo();
}

ServiceSetup::ChannelConfig ServiceSetup::LoadChannel() {
  ChannelConfig config;
  const std::string name = prefs::Get<std::string>(kPrefChannel, std::string("beta"));
  if (name == "tag") {
    config.channel = Channel::PinnedTag;
  } else if (name == "upstream") {
    config.channel = Channel::UpstreamMain;
  } else {
    config.channel = Channel::BetaLatest;
  }
  config.repo = prefs::Get<std::string>(kPrefRepo, std::string());
  config.tag = Trim(prefs::Get<std::string>(kPrefTag, std::string()));
  // A stored channel that is no longer (or not yet) available degrades to the
  // default instead of leaving the app unable to install anything.
  if (!ChannelAvailable(config.channel)) config.channel = Channel::BetaLatest;
  if (config.channel == Channel::PinnedTag && config.tag.empty()) {
    config.channel = Channel::BetaLatest;
  }
  return config;
}

void ServiceSetup::SaveChannel(const ChannelConfig& config) {
  const char* name = "beta";
  if (config.channel == Channel::PinnedTag) name = "tag";
  if (config.channel == Channel::UpstreamMain) name = "upstream";
  prefs::Set<std::string>(kPrefChannel, std::string(name));
  prefs::Set<std::string>(kPrefRepo, config.repo);
  prefs::Set<std::string>(kPrefTag, config.tag);
}

ServiceSetup::Environment ServiceSetup::Env() {
  // DETECTED, never guessed: flatpak writes /.flatpak-info into every sandbox.
  static const Environment env =
      g_file_test("/.flatpak-info", G_FILE_TEST_EXISTS) ? Environment::Flatpak
                                                        : Environment::Native;
  return env;
}

bool ServiceSetup::HostSpawnAvailable() {
  if (Env() != Environment::Flatpak) return false;
  if (gHostSpawnKnownBad.load()) return false;
  static const bool present = OnPath("flatpak-spawn");
  return present;
}

std::string ServiceSetup::Arch() {
  struct utsname u {};
  if (uname(&u) != 0) return {};
  const std::string machine = u.machine;
  if (machine == "x86_64" || machine == "amd64") return "amd64";
  if (machine == "aarch64" || machine == "arm64") return "arm64";
  return {};
}

std::string ServiceSetup::CacheDir() {
  // $XDG_CACHE_HOME — NOT /tmp. Inside a Flatpak /tmp is private to the
  // sandbox, so a file staged there is invisible to the host side of the
  // elevated step; the app's cache dir is a host-visible bind mount at the
  // very same path inside and out, which is what makes `flatpak-spawn --host
  // pkexec <path>` resolve to the file we actually verified.
  const char* explicitDir = g_getenv("XDG_CACHE_HOME");
  const std::string base =
      explicitDir && *explicitDir ? std::string(explicitDir) : std::string(g_get_user_cache_dir());
  if (base.empty()) return {};
  const std::string dir = base + "/urnetwork/service";
  if (g_mkdir_with_parents(dir.c_str(), 0700) != 0) return {};
  return dir;
}

std::uint64_t ServiceSetup::ParseReleaseCode(const std::string& tagIn) {
  std::string t = tagIn;
  if (!t.empty() && t.front() == 'v') t.erase(0, 1);
  std::size_t i = 0;
  auto digits = [&](std::size_t maxLen, std::uint64_t& value) -> std::size_t {
    std::size_t n = 0;
    value = 0;
    while (i + n < t.size() && n < maxLen && t[i + n] >= '0' && t[i + n] <= '9') {
      value = value * 10 + static_cast<std::uint64_t>(t[i + n] - '0');
      ++n;
    }
    return n;
  };
  std::uint64_t year = 0;
  std::uint64_t month = 0;
  std::uint64_t day = 0;
  std::uint64_t code = 0;
  if (digits(4, year) != 4) return 0;
  i += 4;
  if (i >= t.size() || t[i] != '.') return 0;
  ++i;
  std::size_t n = digits(2, month);
  if (n == 0 || month < 1 || month > 12) return 0;
  i += n;
  if (i >= t.size() || t[i] != '.') return 0;
  ++i;
  n = digits(2, day);
  if (n == 0 || day < 1 || day > 31) return 0;
  i += n;
  if (i >= t.size() || t[i] != '-') return 0;
  ++i;
  n = digits(18, code);
  if (n == 0) return 0;
  i += n;
  const std::string rest = t.substr(i);
  if (!rest.empty() && rest != "-beta") return 0;
  return code;
}

std::string ServiceSetup::DigestHexFromAssetDigest(const std::string& digest) {
  static const std::string kPrefix = "sha256:";
  if (digest.size() != kPrefix.size() + 64) return {};
  if (digest.compare(0, kPrefix.size(), kPrefix) != 0) return {};
  std::string hex;
  hex.reserve(64);
  for (std::size_t i = kPrefix.size(); i < digest.size(); ++i) {
    const char c = digest[i];
    const bool isHex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
    if (!isHex) return {};
    hex.push_back(c >= 'A' && c <= 'F' ? static_cast<char>(c - 'A' + 'a') : c);
  }
  return hex;
}

std::string ServiceSetup::AssetNameFor(const std::string& version, const std::string& arch) {
  // ASSET NAMES ARE A CONTRACT (packaging/make-install-tarball.sh writes this
  // exact name). Changing either side alone breaks the in-app checker.
  return "urnetwork-daemon-" + version + "-" + arch + ".install.tar.gz";
}

const char* ToString(ServiceSetup::State state) {
  switch (state) {
    case ServiceSetup::State::Unknown: return "Unknown";
    case ServiceSetup::State::NotInstalled: return "NotInstalled";
    case ServiceSetup::State::Stopped: return "Stopped";
    case ServiceSetup::State::Running: return "Running";
    case ServiceSetup::State::VersionMismatch: return "VersionMismatch";
  }
  return "Unknown";
}

const char* ToString(ServiceSetup::Phase phase) {
  switch (phase) {
    case ServiceSetup::Phase::Idle: return "Idle";
    case ServiceSetup::Phase::Working: return "Working";
    case ServiceSetup::Phase::Cancelled: return "Cancelled";
    case ServiceSetup::Phase::Failed: return "Failed";
    case ServiceSetup::Phase::Succeeded: return "Succeeded";
  }
  return "Idle";
}

const char* ToString(ServiceSetup::Stage stage) {
  switch (stage) {
    case ServiceSetup::Stage::Idle: return "Idle";
    case ServiceSetup::Stage::CheckingRelease: return "CheckingRelease";
    case ServiceSetup::Stage::Downloading: return "Downloading";
    case ServiceSetup::Stage::Verifying: return "Verifying";
    case ServiceSetup::Stage::Extracting: return "Extracting";
    case ServiceSetup::Stage::Elevating: return "Elevating";
    case ServiceSetup::Stage::Confirming: return "Confirming";
  }
  return "Idle";
}

const char* ToString(ServiceSetup::Failure failure) {
  switch (failure) {
    case ServiceSetup::Failure::None: return "None";
    case ServiceSetup::Failure::UnsupportedArch: return "UnsupportedArch";
    case ServiceSetup::Failure::NoNetwork: return "NoNetwork";
    case ServiceSetup::Failure::TlsUnavailable: return "TlsUnavailable";
    case ServiceSetup::Failure::ReleaseUnavailable: return "ReleaseUnavailable";
    case ServiceSetup::Failure::TagNotFound: return "TagNotFound";
    case ServiceSetup::Failure::RateLimited: return "RateLimited";
    case ServiceSetup::Failure::AssetMissing: return "AssetMissing";
    case ServiceSetup::Failure::DigestMissing: return "DigestMissing";
    case ServiceSetup::Failure::DownloadFailed: return "DownloadFailed";
    case ServiceSetup::Failure::ChecksumMismatch: return "ChecksumMismatch";
    case ServiceSetup::Failure::CacheUnwritable: return "CacheUnwritable";
    case ServiceSetup::Failure::TarMissing: return "TarMissing";
    case ServiceSetup::Failure::ExtractFailed: return "ExtractFailed";
    case ServiceSetup::Failure::PolkitUnavailable: return "PolkitUnavailable";
    case ServiceSetup::Failure::SpawnUnavailable: return "SpawnUnavailable";
    case ServiceSetup::Failure::ElevatedFailed: return "ElevatedFailed";
    case ServiceSetup::Failure::ElevatedTimeout: return "ElevatedTimeout";
    case ServiceSetup::Failure::DaemonDidNotAppear: return "DaemonDidNotAppear";
  }
  return "None";
}

// ---------------------------------------------------------------------------
// systemd
// ---------------------------------------------------------------------------

ServiceSetup::SystemdFacts ServiceSetup::ProbeSystemd(int timeoutMs) {
  SystemdFacts facts;
  std::vector<std::string> argv;
  const bool sandbox = Env() == Environment::Flatpak;
  if (sandbox) {
    // systemctl is NOT in the GNOME runtime. The host's is reachable only
    // through the Flatpak portal, which needs
    // --talk-name=org.freedesktop.Flatpak. When that is absent, systemd is
    // simply not consulted — a fact about the ENVIRONMENT, never a claim
    // about the service.
    if (!HostSpawnAvailable()) return facts;
    argv = {"flatpak-spawn", "--host", "systemctl"};
    facts.viaHostSpawn = true;
  } else {
    if (!g_file_test("/run/systemd/system", G_FILE_TEST_IS_DIR)) return facts;
    if (!OnPath("systemctl")) return facts;
    argv = {"systemctl"};
  }
  argv.push_back("show");
  argv.push_back(kUnitName);
  argv.push_back("--property=LoadState");
  argv.push_back("--property=ActiveState");
  argv.push_back("--property=UnitFileState");

  const ProcResult proc = RunProcess(argv, timeoutMs, nullptr);
  if (!proc.spawned) {
    if (sandbox) gHostSpawnKnownBad.store(true);
    return facts;
  }
  if (!proc.exited || proc.exitCode != 0) {
    // flatpak-spawn exits non-zero when the portal refuses the call at all;
    // remember that so every later refresh stops paying for it.
    if (sandbox && proc.out.find('=') == std::string::npos) gHostSpawnKnownBad.store(true);
    if (proc.out.find('=') == std::string::npos) return facts;
  }

  std::size_t start = 0;
  while (start <= proc.out.size()) {
    const std::size_t nl = proc.out.find('\n', start);
    const std::string line =
        Trim(proc.out.substr(start, nl == std::string::npos ? std::string::npos : nl - start));
    const std::size_t eq = line.find('=');
    if (eq != std::string::npos) {
      const std::string key = line.substr(0, eq);
      const std::string value = line.substr(eq + 1);
      if (key == "LoadState") facts.loadState = value;
      if (key == "ActiveState") facts.activeState = value;
      if (key == "UnitFileState") facts.unitFileState = value;
    }
    if (nl == std::string::npos) break;
    start = nl + 1;
  }
  facts.consulted = !facts.loadState.empty() || !facts.activeState.empty();
  return facts;
}

// ---------------------------------------------------------------------------
// Classify — PURE. No I/O, so the derivation sentence is testable.
// ---------------------------------------------------------------------------

ServiceSetup::Observation ServiceSetup::Classify(const DaemonFacts& daemon,
                                                 const SystemdFacts& systemd,
                                                 bool socketPresent, Environment env,
                                                 const Release* target) {
  Observation o;
  o.environment = env;
  o.socketPresent = socketPresent;
  o.systemdConsulted = systemd.consulted;
  o.installedVersion = daemon.daemonVersion;
  if (target != nullptr) o.targetVersion = target->version;

  // (1) The socket is there and this user may not use it. That is the
  // `urnetwork` group, whose fix is `usermod -aG urnetwork $USER` plus a
  // re-login — NOT an install. Offering "Set up the VPN service" here would
  // make the user reinstall a working daemon for nothing, so the card stays
  // hidden and the app's existing remediation keeps the floor.
  if (daemon.valid && daemon.permissionDenied) {
    o.state = State::Unknown;
    o.permissionDenied = true;
    o.derivation =
        "the control socket exists and this user is not permitted to connect "
        "to it (the urnetwork group)";
    return o;
  }

  // (2) A stale sandbox bind mount: the daemon restarted, systemd recreated
  // /run/urnetwork, and this sandbox still holds the deleted inode. Nothing is
  // wrong with the service; only relaunching the app fixes it.
  if (daemon.valid && daemon.staleSandboxMount) {
    o.state = State::Unknown;
    o.derivation =
        "the sandbox's bind mount of the control socket directory is stale; "
        "relaunching the app is the fix, not reinstalling the service";
    return o;
  }

  // (3) The handshake itself reported skew. The daemon is telling us it is the
  // wrong version — stronger evidence than any release comparison, and true
  // even with no network.
  if (daemon.valid && daemon.protocolSkew) {
    o.state = State::VersionMismatch;
    o.derivation = "the control handshake reported a protocol/SDK version disagreement";
    return o;
  }

  // (4) The handshake completed: the daemon is serving.
  if (daemon.valid && daemon.helloOk) {
    o.state = State::Running;
    o.derivation = "the control handshake completed";
    if (systemd.consulted && !systemd.activeState.empty()) {
      o.derivation += "; systemd reports the unit " + systemd.activeState;
    }
    const std::uint64_t have = ParseReleaseCode(daemon.daemonVersion);
    const std::uint64_t want = target != nullptr ? target->code : 0;
    // A daemon whose version is not release grammar (a dev build, "0.0.0") is
    // never nagged: every release would outrank it forever. An installed
    // daemon NEWER than the channel is not a mismatch either — that is a
    // deliberate pin, not a problem.
    if (have != 0 && want != 0 && want > have) {
      o.state = State::VersionMismatch;
      o.derivation += "; the selected channel publishes a newer release (" +
                      daemon.daemonVersion + " -> " + target->version + ")";
    }
    return o;
  }

  // (5) No handshake. systemd is the only source that can say "there is no
  // unit at all" — the ONLY positive evidence of absence this app can obtain.
  if (systemd.consulted) {
    const std::string where =
        systemd.viaHostSpawn ? "systemd (through the host spawn portal)" : "systemd";
    if (systemd.loadState == "not-found") {
      o.state = State::NotInstalled;
      o.confidence = Confidence::Observed;
      o.derivation = where + " has no " + kUnitName + " unit";
      return o;
    }
    if (systemd.loadState == "masked" || systemd.unitFileState == "masked") {
      o.state = State::Stopped;
      o.derivation = where + " reports the unit masked; it cannot start until it is unmasked";
      return o;
    }
    if (systemd.activeState == "active") {
      // The unit is up and the socket did not answer: something is genuinely
      // odd (an overridden socket path, a daemon still starting its listener).
      // No claim — Unknown hides the card rather than offering a wrong fix.
      o.state = State::Unknown;
      o.derivation = where + " reports the unit active, but the control socket did not answer";
      return o;
    }
    o.state = State::Stopped;
    o.derivation = where + " reports the unit " +
                   (systemd.activeState.empty() ? std::string("inactive") : systemd.activeState);
    return o;
  }

  // (6) No handshake and no systemd evidence — the sandbox-without-spawn case.
  if (socketPresent) {
    // The socket file is there and nothing answered on it: something has been
    // installed on this host. That is observed, not inferred.
    o.state = State::Stopped;
    o.confidence = Confidence::Observed;
    o.derivation = "the control socket is present and did not answer";
    return o;
  }
  o.state = State::NotInstalled;
  o.confidence = Confidence::Inferred;
  if (env == Environment::Flatpak) {
    o.derivation =
        "no control socket, and systemd cannot be consulted from this sandbox "
        "(the unit's runtime directory disappears with the unit, so installed-"
        "but-stopped looks the same from here). The offered action covers both: "
        "the installer sets up, repairs, starts and upgrades";
  } else {
    o.derivation = "no control socket and no systemd to ask";
  }
  return o;
}

// ---------------------------------------------------------------------------
// releases
// ---------------------------------------------------------------------------

namespace {

// Read ONE release object into a Release, pairing the asset's url and digest
// in a single visit so a repo that changes mid-flight can never pair this hash
// with a different download.
bool ReadRelease(const nlohmann::json& rel, const std::string& arch, bool wantPrerelease,
                 bool requirePrereleaseMatch, ServiceSetup::Release* out,
                 ServiceSetup::Failure* skipReason) {
  if (!rel.is_object()) return false;
  if (JsonFlag(rel, "draft")) return false;
  const bool prerelease = JsonFlag(rel, "prerelease");
  if (requirePrereleaseMatch && prerelease != wantPrerelease) return false;

  const std::string tag = JsonStr(rel, "tag_name");
  if (tag.empty()) return false;
  std::string version = tag;
  if (!version.empty() && version.front() == 'v') version.erase(0, 1);

  ServiceSetup::Release r;
  r.tag = tag;
  r.version = version;
  r.code = ServiceSetup::ParseReleaseCode(tag);
  r.prerelease = prerelease;
  r.assetName = ServiceSetup::AssetNameFor(version, arch);

  const auto assets = rel.find("assets");
  if (assets != rel.end() && assets->is_array()) {
    for (const auto& asset : *assets) {
      if (!asset.is_object()) continue;
      if (JsonStr(asset, "name") != r.assetName) continue;
      r.assetUrl = JsonStr(asset, "browser_download_url");
      r.digestHex = ServiceSetup::DigestHexFromAssetDigest(JsonStr(asset, "digest"));
      r.assetSize = JsonInt(asset, "size");
    }
  }
  if (r.assetUrl.empty()) {
    if (skipReason) *skipReason = ServiceSetup::Failure::AssetMissing;
    return false;
  }
  if (r.digestHex.size() != 64) {
    // Present asset, absent (or malformed) digest: the download would be
    // unverifiable, which disqualifies the release outright. Never
    // best-effort — an unverified root install is not a feature.
    if (skipReason) *skipReason = ServiceSetup::Failure::DigestMissing;
    return false;
  }
  *out = r;
  return true;
}

}  // namespace

bool ServiceSetup::FetchRelease(const ChannelConfig& config, Release* out, Failure* failure,
                                std::string* detail, const std::atomic<bool>* cancel) {
  auto fail = [&](Failure f, const std::string& why) {
    if (failure) *failure = f;
    if (detail) *detail = why;
    return false;
  };
  if (failure) *failure = Failure::None;
  if (detail) detail->clear();

  const std::string arch = Arch();
  if (arch.empty()) {
    return fail(Failure::UnsupportedArch,
                "URnetwork ships amd64 and arm64 service builds only");
  }
  if (!ChannelAvailable(config.channel)) {
    return fail(Failure::ReleaseUnavailable, ChannelUnavailableReason(config.channel));
  }

  const std::string repo = ChannelRepo(config);
  std::string url;
  const bool pinned = config.channel == Channel::PinnedTag;
  if (pinned) {
    if (config.tag.empty()) return fail(Failure::TagNotFound, "no release tag is pinned");
    gchar* escaped = g_uri_escape_string(config.tag.c_str(), nullptr, FALSE);
    url = "https://api.github.com/repos/" + repo + "/releases/tags/" +
          (escaped ? escaped : config.tag);
    g_free(escaped);
  } else {
    url = "https://api.github.com/repos/" + repo + "/releases?per_page=30";
  }

  std::string body;
  HttpSink sink = [&body](const char* data, std::size_t n) {
    body.append(data, n);
    return true;
  };
  HttpOutcome outcome =
      HttpGet(url, "application/vnd.github+json", kMaxJsonBytes, sink, nullptr, cancel);

  // A pinned tag typed without the leading 'v' is the single most likely user
  // typo, and the tags endpoint 404s on it. Retry once with the minted form
  // rather than telling the user their tag does not exist.
  if (pinned && !outcome.ok && outcome.status == 404 && config.tag.front() != 'v') {
    body.clear();
    gchar* escaped = g_uri_escape_string(("v" + config.tag).c_str(), nullptr, FALSE);
    url = "https://api.github.com/repos/" + repo + "/releases/tags/" +
          (escaped ? escaped : ("v" + config.tag));
    g_free(escaped);
    outcome = HttpGet(url, "application/vnd.github+json", kMaxJsonBytes, sink, nullptr, cancel);
  }

  if (!outcome.ok) {
    if (outcome.tlsUnavailable) {
      return fail(Failure::TlsUnavailable,
                  "https is unavailable in this environment (glib-networking)");
    }
    if (outcome.rateLimited) {
      return fail(Failure::RateLimited, "GitHub rate-limited this unauthenticated check");
    }
    if (outcome.status == 404) {
      return fail(pinned ? Failure::TagNotFound : Failure::ReleaseUnavailable,
                  pinned ? ("no release is tagged " + config.tag + " in " + repo)
                         : (repo + " has no releases"));
    }
    if (outcome.status != 0) {
      return fail(Failure::ReleaseUnavailable,
                  "GitHub answered HTTP " + std::to_string(outcome.status));
    }
    return fail(Failure::NoNetwork, outcome.error.empty() ? "the check could not reach GitHub"
                                                          : outcome.error);
  }

  const nlohmann::json parsed = nlohmann::json::parse(body, nullptr, false);
  Failure skip = Failure::None;
  Release best;

  if (pinned) {
    if (!parsed.is_object()) {
      return fail(Failure::ReleaseUnavailable, "the release document was not a JSON object");
    }
    if (!ReadRelease(parsed, arch, false, false, &best, &skip)) {
      if (skip == Failure::AssetMissing) {
        return fail(Failure::AssetMissing,
                    "release " + config.tag + " carries no " +
                        AssetNameFor(JsonStr(parsed, "tag_name"), arch));
      }
      if (skip == Failure::DigestMissing) {
        return fail(Failure::DigestMissing,
                    "release " + config.tag + "'s asset carries no usable sha256 digest");
      }
      return fail(Failure::ReleaseUnavailable, "release " + config.tag + " could not be read");
    }
  } else {
    if (!parsed.is_array()) {
      return fail(Failure::ReleaseUnavailable, "the release list was not a JSON array");
    }
    const bool wantPrerelease = config.channel == Channel::BetaLatest;
    std::uint64_t newestParsed = 0;
    for (const auto& rel : parsed) {
      const std::uint64_t code = ParseReleaseCode(JsonStr(rel, "tag_name"));
      if (code > newestParsed) newestParsed = code;
      Release candidate;
      Failure why = Failure::None;
      if (!ReadRelease(rel, arch, wantPrerelease, true, &candidate, &why)) {
        if (why != Failure::None && code >= newestParsed) skip = why;
        continue;
      }
      if (candidate.code == 0) continue;  // not release grammar: unrankable
      if (candidate.code > best.code) best = candidate;
    }
    if (!best.valid()) {
      if (skip == Failure::AssetMissing) {
        return fail(Failure::AssetMissing,
                    "the newest release in " + repo + " carries no " +
                        AssetNameFor("<version>", arch));
      }
      if (skip == Failure::DigestMissing) {
        return fail(Failure::DigestMissing,
                    "the newest release in " + repo + " has no usable sha256 digest");
      }
      return fail(Failure::ReleaseUnavailable,
                  repo + " publishes no " + std::string(wantPrerelease ? "prerelease" : "release") +
                      " with a service build for " + arch);
    }
  }

  *out = best;
  return true;
}

// ---------------------------------------------------------------------------
// the elevated step
// ---------------------------------------------------------------------------

ServiceSetup::ElevatedResult ServiceSetup::RunElevatedVerb(const std::string& installer,
                                                           int timeoutMs,
                                                           const std::atomic<bool>* cancel) {
  ElevatedResult result;
  std::vector<std::string> argv;
  if (Env() == Environment::Flatpak) {
    // The officially suggested way to run a host command from a sandbox, and
    // what other VPN-adjacent Flatpaks do. ONE native polkit prompt.
    //
    // TODO(flatpak-manifest): this needs `--talk-name=org.freedesktop.Flatpak`
    // and `--socket=session-bus` in packaging/flatpak/network.ur.urnetwork.yml.
    // Flathub's linter flags that permission; the exception needs a
    // justification PR, and the justification belongs in a comment beside the
    // option. Without it this call fails and the flow lands on
    // Failure::SpawnUnavailable, which shows the paste-able command — the
    // designed no-permission fallback.
    argv = {"flatpak-spawn", "--host", "pkexec", installer};
  } else {
    argv = {"pkexec", installer};
  }
  // --yes: the installer's prompts are for a terminal, and there is none here.
  // Every question it would ask has already been answered by this flow.
  argv.push_back("--yes");

  const ProcResult proc = RunProcess(argv, timeoutMs, cancel);
  result.spawned = proc.spawned;
  result.exited = proc.exited;
  result.timedOut = proc.timedOut;
  result.exitCode = proc.exitCode;
  result.stderrTail = LastLine(proc.err);
  if (proc.cancelled) {
    result.cancelled = true;
    return result;
  }
  // pkexec's own exit codes: 126 = the authorization dialog was dismissed or
  // authorization could not be obtained; 127 = the program could not be
  // executed. A dismissed dialog is a NORMAL outcome — the user changed their
  // mind — so it renders as "cancelled", silently, and never as an error. (The
  // "could not execute" half of 127 is caught BEFORE this function by the
  // pkexec/flatpak-spawn presence checks, which have their own states.)
  if (proc.exited && (proc.exitCode == 126 || proc.exitCode == 127)) {
    result.cancelled = true;
  }
  return result;
}

std::string ServiceSetup::FallbackCommandFor(const std::string& stagedInstaller,
                                             const Release& release) {
  // Already downloaded AND verified: the shortest honest line.
  if (!stagedInstaller.empty()) {
    return "pkexec " + ShellQuote(stagedInstaller);
  }
  // Not staged yet: a line that does the SAME verification this app would —
  // the digest is the one GitHub stamped on the asset, carried here verbatim.
  if (release.valid()) {
    const std::string dir = "${TMPDIR:-/tmp}";
    const std::string file = dir + "/" + release.assetName;
    return "curl -fsSL " + ShellQuote(release.assetUrl) + " -o \"" + file + "\" && echo \"" +
           release.digestHex + "  " + file + "\" | sha256sum -c - && tar xzf \"" + file +
           "\" -C \"" + dir + "\" && sudo \"" + dir + "/urnetwork-daemon/install.sh\"";
  }
  // Nothing known yet: the documented one-liner from
  // packaging/tarball/install.sh's own header.
  return "curl -fsSL https://get.ur.network/urnetwork-daemon.tar.gz | tar xz && "
         "sudo urnetwork-daemon/install.sh";
}

// ---------------------------------------------------------------------------
// the instance
// ---------------------------------------------------------------------------

ServiceSetup& ServiceSetup::Instance() {
  static ServiceSetup instance;
  return instance;
}

ServiceSetup::ServiceSetup() {
  snapshot_.fallbackCommand = FallbackCommandFor(std::string(), Release{});
}

ServiceSetup::~ServiceSetup() { Shutdown(); }

void ServiceSetup::SetDaemonFactsProvider(FactsProvider provider) {
  std::lock_guard<std::mutex> lock(mutex_);
  provider_ = std::move(provider);
}

std::uint64_t ServiceSetup::AddHandler(Handler handler) {
  std::lock_guard<std::mutex> lock(mutex_);
  const std::uint64_t token = nextHandlerToken_++;
  handlers_.emplace_back(token, std::move(handler));
  return token;
}

void ServiceSetup::RemoveHandler(std::uint64_t token) {
  std::lock_guard<std::mutex> lock(mutex_);
  for (auto it = handlers_.begin(); it != handlers_.end(); ++it) {
    if (it->first == token) {
      handlers_.erase(it);
      return;
    }
  }
}

ServiceSetup::Snapshot ServiceSetup::Current() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return snapshot_;
}

ServiceSetup::DaemonFacts ServiceSetup::PullFacts() const {
  FactsProvider provider;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    provider = provider_;
  }
  if (!provider) return DaemonFacts{};
  return provider();
}

void ServiceSetup::Mutate(std::uint64_t epoch, const std::function<void(Snapshot&)>& fn) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (epoch != epochCell_->load()) return;
    fn(snapshot_);
  }
  Publish(epoch);
}

void ServiceSetup::Publish(std::uint64_t epoch) {
  Snapshot copy;
  std::vector<Handler> handlers;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (epoch != epochCell_->load()) return;
    ++snapshot_.revision;
    copy = snapshot_;
    handlers.reserve(handlers_.size());
    for (const auto& entry : handlers_) handlers.push_back(entry.second);
  }
  if (handlers.empty()) return;
  // The epoch cell is a shared_ptr, NOT `this`: a post that outlives the
  // object must not dereference it to decide whether to run.
  auto cell = epochCell_;
  PostToMain([cell, epoch, copy, handlers]() {
    if (!cell || cell->load() != epoch) return;  // an abandoned run must not repaint
    for (const auto& handler : handlers) {
      if (handler) handler(copy);
    }
  });
}

void ServiceSetup::RefreshNow(bool withReleaseCheck) { Request(false, withReleaseCheck); }

void ServiceSetup::BeginInstall() { Request(true, true); }

void ServiceSetup::Request(bool install, bool withReleaseCheck) {
  if (shutdown_.load()) return;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stop_) return;
    if (install) {
      // Two elevated runs in flight would mean two polkit prompts — exactly
      // what the busy flag exists to prevent. A click while one is running is
      // dropped, not queued: the user is looking at a disabled button anyway.
      if (installInFlight_ || installRequested_) return;
      installRequested_ = true;
    } else {
      probeRequested_ = true;
      probeWithRelease_ = probeWithRelease_ || withReleaseCheck;
    }
    if (!workerStarted_) {
      workerStarted_ = true;
      worker_ = std::thread([this]() { WorkerLoop(); });
    }
  }
  cv_.notify_one();
}

void ServiceSetup::WorkerLoop() {
  for (;;) {
    bool doInstall = false;
    bool doProbe = false;
    bool withRelease = false;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      cv_.wait(lock, [this] { return stop_ || installRequested_ || probeRequested_; });
      if (stop_) return;
      // An install outranks a probe: it re-classifies at the end anyway.
      doInstall = installRequested_;
      installRequested_ = false;
      doProbe = probeRequested_ && !doInstall;
      if (doProbe) probeRequested_ = false;
      withRelease = probeWithRelease_;
      if (doProbe) probeWithRelease_ = false;
      inFlight_ = true;
      installInFlight_ = doInstall;
    }
    // A NEW action starts uncancelled, whatever the previous one ended as.
    cancel_.store(false);
    const std::uint64_t epoch = epochCell_->load();
    if (doInstall) {
      RunInstall(epoch);
    } else if (doProbe) {
      RunProbe(epoch, withRelease);
    }
    {
      std::lock_guard<std::mutex> lock(mutex_);
      inFlight_ = false;
      installInFlight_ = false;
    }
  }
}

void ServiceSetup::Cancel() {
  // Bump the epoch FIRST: every post already in flight is dropped at the main
  // loop, so a late reply from the run being abandoned cannot repaint over the
  // cancelled card. The UI therefore settles IMMEDIATELY — the worker may take
  // until its next socket read to notice, but nothing it says afterwards is
  // rendered.
  epochCell_->fetch_add(1);
  cancel_.store(true);
  const std::uint64_t epoch = epochCell_->load();
  {
    std::lock_guard<std::mutex> lock(mutex_);
    snapshot_.busy = false;
    snapshot_.phase = Phase::Cancelled;
    snapshot_.stage = Stage::Idle;
    snapshot_.progress = -1.0;
  }
  Publish(epoch);
}

void ServiceSetup::Shutdown() {
  if (shutdown_.exchange(true)) return;
  cancel_.store(true);
  epochCell_->fetch_add(1);
  std::thread worker;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    stop_ = true;
    worker = std::move(worker_);
  }
  cv_.notify_all();
  // Bounded: an idle worker returns at once, and a busy one is capped by the
  // socket timeout (kSocketTimeoutSeconds) or the child-process grace period,
  // both of which are seconds, not minutes.
  if (worker.joinable()) worker.join();
}

// ---------------------------------------------------------------------------
// the probe
// ---------------------------------------------------------------------------

void ServiceSetup::RunProbe(std::uint64_t epoch, bool withReleaseCheck) {
  DaemonFacts facts = PullFacts();
  if (facts.socketPath.empty()) facts.socketPath = ControlClient::SocketPath();
  const bool socketPresent = PathExists(facts.socketPath);

  // systemd is only worth a spawn when the handshake did NOT succeed: a live
  // handshake already proves the daemon is serving, and inside a sandbox the
  // probe costs a portal round trip.
  SystemdFacts systemd;
  if (!(facts.valid && facts.helloOk)) systemd = ProbeSystemd(kSystemdTimeoutMs);

  Release target;
  bool haveTarget = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    target = offer_;
    haveTarget = offer_.valid();
  }

  const std::int64_t now = NowMs();
  bool shouldCheck = withReleaseCheck;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (shouldCheck && lastReleaseCheckMs_ != 0 &&
        now - lastReleaseCheckMs_ < kReleaseCheckThrottleMs && haveTarget) {
      shouldCheck = false;
    }
  }
  if (shouldCheck && !cancel_.load()) {
    Release fetched;
    Failure why = Failure::None;
    std::string detail;
    if (FetchRelease(LoadChannel(), &fetched, &why, &detail, &cancel_)) {
      target = fetched;
      haveTarget = true;
      std::lock_guard<std::mutex> lock(mutex_);
      offer_ = fetched;
      lastReleaseCheckMs_ = NowMs();
    } else {
      // A failed CHECK must never wipe the classification or raise an error
      // banner on its own: not knowing what the newest release is only costs
      // the mismatch comparison. It is logged, and the developer screen can
      // show it.
      Log("[svc] release check failed: %s (%s)", ToString(why), detail.c_str());
      std::lock_guard<std::mutex> lock(mutex_);
      lastReleaseCheckMs_ = NowMs();
    }
  }

  const Observation observation =
      Classify(facts, systemd, socketPresent, Env(), haveTarget ? &target : nullptr);
  Log("[svc] state=%s confidence=%s (%s)", ToString(observation.state),
      observation.confidence == Confidence::Observed ? "observed" : "inferred",
      observation.derivation.c_str());

  Mutate(epoch, [&](Snapshot& s) {
    s.observation = observation;
    if (haveTarget) {
      s.offeredVersion = target.version;
      s.offeredTag = target.tag;
    }
    if (s.stagedInstaller.empty()) {
      s.fallbackCommand = FallbackCommandFor(std::string(), haveTarget ? target : Release{});
    }
  });
}

// ---------------------------------------------------------------------------
// the install
// ---------------------------------------------------------------------------

void ServiceSetup::RunInstall(std::uint64_t epoch) {
  auto stage = [&](Stage st, double progress) {
    Mutate(epoch, [&](Snapshot& s) {
      s.busy = true;
      s.phase = Phase::Working;
      s.stage = st;
      s.failure = Failure::None;
      s.failureDetail.clear();
      s.progress = progress;
    });
  };
  auto fail = [&](Failure why, const std::string& detail) {
    Log("[svc] install failed: %s (%s)", ToString(why), detail.c_str());
    Mutate(epoch, [&](Snapshot& s) {
      s.busy = false;
      s.phase = Phase::Failed;
      s.stage = Stage::Idle;
      s.failure = why;
      s.failureDetail = detail;
      s.progress = -1.0;
    });
  };
  auto cancelled = [&]() {
    Mutate(epoch, [&](Snapshot& s) {
      s.busy = false;
      s.phase = Phase::Cancelled;
      s.stage = Stage::Idle;
      s.failure = Failure::None;
      s.failureDetail.clear();
      s.progress = -1.0;
    });
  };

  stage(Stage::CheckingRelease, -1.0);

  const std::string arch = Arch();
  if (arch.empty()) {
    fail(Failure::UnsupportedArch, "URnetwork ships amd64 and arm64 service builds only");
    return;
  }

  Release release;
  {
    Failure why = Failure::None;
    std::string detail;
    if (!FetchRelease(LoadChannel(), &release, &why, &detail, &cancel_)) {
      if (cancel_.load()) {
        cancelled();
      } else {
        fail(why, detail);
      }
      return;
    }
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    offer_ = release;
    lastReleaseCheckMs_ = NowMs();
  }
  Mutate(epoch, [&](Snapshot& s) {
    s.offeredVersion = release.version;
    s.offeredTag = release.tag;
    s.fallbackCommand = FallbackCommandFor(std::string(), release);
  });

  const std::string cache = CacheDir();
  if (cache.empty()) {
    fail(Failure::CacheUnwritable,
         "the app's cache directory could not be created ($XDG_CACHE_HOME)");
    return;
  }

  // Housekeeping BEFORE the download, never after: a previous run's staging
  // dirs and superseded tarballs are dead weight, and clearing them here means
  // a crash mid-install cannot leave the cache growing forever.
  {
    GDir* dir = g_dir_open(cache.c_str(), 0, nullptr);
    if (dir) {
      const gchar* name = nullptr;
      std::vector<std::string> doomed;
      while ((name = g_dir_read_name(dir)) != nullptr) {
        const std::string entry = name;
        const bool staleStage = entry.compare(0, 6, "stage-") == 0;
        const bool staleTarball =
            entry.size() > 3 && entry != release.assetName &&
            entry.rfind(".install.tar.gz") != std::string::npos;
        if (staleStage || staleTarball) doomed.push_back(cache + "/" + entry);
      }
      g_dir_close(dir);
      for (const auto& path : doomed) RemoveTree(path);
    }
  }

  const std::string tarball = cache + "/" + release.assetName;

  // ---- download (UNPRIVILEGED, as the app user) ----------------------------
  bool haveVerified = false;
  if (PathExists(tarball)) {
    stage(Stage::Verifying, -1.0);
    haveVerified = Sha256File(tarball, &cancel_) == release.digestHex;
    if (!haveVerified) g_remove(tarball.c_str());
  }
  if (cancel_.load()) {
    cancelled();
    return;
  }

  if (!haveVerified) {
    stage(Stage::Downloading, 0.0);
    const std::string partial = tarball + ".part";
    g_remove(partial.c_str());
    std::ofstream file(partial, std::ios::binary | std::ios::trunc);
    if (!file.good()) {
      fail(Failure::CacheUnwritable, "could not open " + partial + " for writing");
      return;
    }
    HttpSink sink = [&file](const char* data, std::size_t n) {
      file.write(data, static_cast<std::streamsize>(n));
      return file.good();
    };
    HttpProgress report = [&](std::int64_t got, std::int64_t total) {
      const double fraction = total > 0 ? static_cast<double>(got) / static_cast<double>(total)
                                        : -1.0;
      Mutate(epoch, [&](Snapshot& s) { s.progress = fraction; });
    };
    const HttpOutcome outcome =
        HttpGet(release.assetUrl, "application/octet-stream", kMaxAssetBytes, sink, report,
                &cancel_);
    file.close();
    if (cancel_.load()) {
      g_remove(partial.c_str());
      cancelled();
      return;
    }
    if (!outcome.ok) {
      g_remove(partial.c_str());
      if (outcome.tlsUnavailable) {
        fail(Failure::TlsUnavailable, "https is unavailable in this environment");
      } else if (outcome.status != 0 && outcome.status != 200) {
        fail(Failure::DownloadFailed, "the asset server answered HTTP " +
                                          std::to_string(outcome.status));
      } else {
        fail(Failure::DownloadFailed,
             outcome.error.empty() ? "the transfer did not finish" : outcome.error);
      }
      return;
    }

    // ---- verify (UNPRIVILEGED, before anything is handed to root) ----------
    stage(Stage::Verifying, -1.0);
    const std::string actual = Sha256File(partial, &cancel_);
    if (cancel_.load()) {
      g_remove(partial.c_str());
      cancelled();
      return;
    }
    if (actual.empty() || actual != release.digestHex) {
      g_remove(partial.c_str());
      fail(Failure::ChecksumMismatch,
           "expected " + release.digestHex + ", got " + (actual.empty() ? "nothing" : actual));
      return;
    }
    if (g_rename(partial.c_str(), tarball.c_str()) != 0) {
      g_remove(partial.c_str());
      fail(Failure::CacheUnwritable, "could not move the verified download into place");
      return;
    }
  }
  Log("[svc] verified %s (sha256 %s)", release.assetName.c_str(), release.digestHex.c_str());

  // ---- extract (still UNPRIVILEGED) ---------------------------------------
  stage(Stage::Extracting, -1.0);
  std::vector<std::string> tarPrefix;
  if (OnPath("tar")) {
    tarPrefix = {"tar"};
  } else if (HostSpawnAvailable()) {
    // The runtime has no tar; borrow the host's. The cache path is identical
    // on both sides of the sandbox, so the same argument resolves.
    tarPrefix = {"flatpak-spawn", "--host", "tar"};
  } else {
    fail(Failure::TarMissing, "no tar is available to unpack the download");
    return;
  }

  // The archive's shape is a contract (make-install-tarball.sh proves a single
  // top-level `urnetwork-daemon/` before publishing). Prove it again here:
  // absolute paths and .. traversal are refused rather than trusted.
  {
    std::vector<std::string> argv = tarPrefix;
    argv.push_back("-tzf");
    argv.push_back(tarball);
    const ProcResult list = RunProcess(argv, 60000, &cancel_);
    if (list.cancelled) {
      cancelled();
      return;
    }
    if (!list.exited || list.exitCode != 0) {
      fail(Failure::ExtractFailed,
           list.err.empty() ? "tar could not read the archive" : LastLine(list.err));
      return;
    }
    std::size_t start = 0;
    bool shapeOk = true;
    std::string offender;
    while (start <= list.out.size() && shapeOk) {
      const std::size_t nl = list.out.find('\n', start);
      const std::string entry =
          Trim(list.out.substr(start, nl == std::string::npos ? std::string::npos : nl - start));
      if (!entry.empty()) {
        if (entry[0] == '/' || entry.find("..") != std::string::npos ||
            entry.compare(0, 17, "urnetwork-daemon/") != 0) {
          shapeOk = false;
          offender = entry;
        }
      }
      if (nl == std::string::npos) break;
      start = nl + 1;
    }
    if (!shapeOk) {
      fail(Failure::ExtractFailed,
           "the archive is not the shipped shape (unexpected entry: " + offender + ")");
      return;
    }
  }

  std::string stagingDir;
  {
    std::string tmpl = cache + "/stage-XXXXXX";
    std::vector<char> buffer(tmpl.begin(), tmpl.end());
    buffer.push_back('\0');
    if (g_mkdtemp(buffer.data()) == nullptr) {
      fail(Failure::CacheUnwritable, "could not create a staging directory under the app cache");
      return;
    }
    stagingDir = buffer.data();
    g_chmod(stagingDir.c_str(), 0700);
  }

  {
    std::vector<std::string> argv = tarPrefix;
    argv.push_back("-xzf");
    argv.push_back(tarball);
    argv.push_back("-C");
    argv.push_back(stagingDir);
    const ProcResult extract = RunProcess(argv, 180000, &cancel_);
    if (extract.cancelled) {
      RemoveTree(stagingDir);
      cancelled();
      return;
    }
    if (!extract.exited || extract.exitCode != 0) {
      RemoveTree(stagingDir);
      fail(Failure::ExtractFailed,
           extract.err.empty() ? "tar refused the archive" : LastLine(extract.err));
      return;
    }
  }

  const std::string installer = stagingDir + "/urnetwork-daemon/install.sh";
  if (!g_file_test(installer.c_str(), G_FILE_TEST_IS_REGULAR)) {
    RemoveTree(stagingDir);
    fail(Failure::ExtractFailed, "the archive carries no urnetwork-daemon/install.sh");
    return;
  }
  g_chmod(installer.c_str(), 0755);

  Mutate(epoch, [&](Snapshot& s) {
    s.stagedInstaller = installer;
    s.fallbackCommand = FallbackCommandFor(installer, release);
  });

  // ---- elevate: ONE polkit prompt, over an ALREADY-VERIFIED path ----------
  // Everything above ran as the app user. The elevated step is deliberately
  // dumb: it runs the installer that shipped inside the bytes we hashed.
  if (Env() == Environment::Flatpak) {
    if (!HostSpawnAvailable()) {
      fail(Failure::SpawnUnavailable,
           "this sandbox cannot run a host command (flatpak-spawn / "
           "--talk-name=org.freedesktop.Flatpak)");
      return;
    }
  } else if (!OnPath("pkexec")) {
    fail(Failure::PolkitUnavailable, "pkexec is not installed on this system (polkit)");
    return;
  }

  stage(Stage::Elevating, -1.0);
  const ElevatedResult elevated = RunElevatedVerb(installer, kElevatedTimeoutMs, &cancel_);
  if (elevated.cancelled || cancel_.load()) {
    // A dismissed polkit dialog is a NORMAL outcome. Silent: no error colour,
    // no log noise, no "something went wrong". The card simply offers again.
    cancelled();
    return;
  }
  if (!elevated.spawned) {
    fail(Env() == Environment::Flatpak ? Failure::SpawnUnavailable : Failure::PolkitUnavailable,
         "the elevated helper could not be started");
    return;
  }
  if (elevated.timedOut) {
    fail(Failure::ElevatedTimeout, "the installer did not finish within the time budget");
    return;
  }
  if (!elevated.exited || elevated.exitCode != 0) {
    fail(Failure::ElevatedFailed,
         elevated.stderrTail.empty()
             ? ("the installer exited " + std::to_string(elevated.exitCode))
             : elevated.stderrTail);
    return;
  }

  // ---- confirm: the daemon has to actually answer -------------------------
  stage(Stage::Confirming, -1.0);
  const std::int64_t deadline = NowMs() + kConfirmBudgetMs;
  Observation observation;
  bool reached = false;
  while (NowMs() < deadline) {
    if (cancel_.load()) {
      cancelled();
      return;
    }
    DaemonFacts facts = PullFacts();
    if (facts.socketPath.empty()) facts.socketPath = ControlClient::SocketPath();
    SystemdFacts systemd;
    if (!(facts.valid && facts.helloOk)) systemd = ProbeSystemd(kSystemdTimeoutMs);
    observation = Classify(facts, systemd, PathExists(facts.socketPath), Env(), &release);
    if (observation.state == State::Running) {
      reached = true;
      break;
    }
    g_usleep(750 * 1000);
  }

  if (reached) {
    RemoveTree(stagingDir);
    Mutate(epoch, [&](Snapshot& s) {
      s.busy = false;
      s.phase = Phase::Succeeded;
      s.stage = Stage::Idle;
      s.failure = Failure::None;
      s.failureDetail.clear();
      s.progress = -1.0;
      s.stagedInstaller.clear();
      s.observation = observation;
      s.fallbackCommand = FallbackCommandFor(std::string(), release);
    });
    Log("[svc] install complete: %s is running", release.version.c_str());
    return;
  }

  // The installer said it succeeded and the socket still will not answer. In
  // practice this is one thing: the user was just added to the `urnetwork`
  // group, and group membership only applies to NEW login sessions. Say that,
  // because "it failed" would be false and unactionable.
  Mutate(epoch, [&](Snapshot& s) {
    s.busy = false;
    s.phase = Phase::Failed;
    s.stage = Stage::Idle;
    s.failure = Failure::DaemonDidNotAppear;
    s.failureDetail = observation.permissionDenied
                          ? "the service is running and this session is not yet in the "
                            "urnetwork group"
                          : observation.derivation;
    s.progress = -1.0;
    s.observation = observation;
  });
}

}  // namespace urnw
