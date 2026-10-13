// SPDX-License-Identifier: MPL-2.0
#include "ApprovalCheck.hpp"

#include <unistd.h>

#include <gio/gio.h>
#include <glib.h>
#include <libsoup/soup.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>

#include "ApprovalDescriptor.hpp"
#include "Config.hpp"

// The build stamp, for the User-Agent: meson passes -DUR_APP_VERSION into the
// GUI; the fallback keeps this TU self-contained (UpdateChecker.cpp does the same).
#ifndef UR_APP_VERSION
#define UR_APP_VERSION "0.0.0"
#endif

namespace urnw {
namespace {

// The one answer of this run. Written by the worker thread, read from the UI thread.
std::atomic<int> g_state{static_cast<int>(ApprovalPage::NotChecked)};
// The contract versions the page listed (ApprovalDescriptor.hpp, Listing): written before
// g_state says Live, so a reader that sees Live sees them.
std::atomic<std::uint32_t> g_contracts{0};

// The whole check's budget, as the two clocks libsoup and GLib take it.
constexpr guint kBudgetMs = static_cast<guint>(approval::kCheckBudget.count());
constexpr guint kBudgetSeconds = static_cast<guint>(
    std::chrono::duration_cast<std::chrono::seconds>(approval::kCheckBudget).count());

// The session's timeout bounds each step of the request, not the whole of it,
// so a timer on the main loop cancels whatever is still running when the
// budget is spent. g_cancellable_cancel is safe from any thread.
gboolean CancelOnDeadline(gpointer cancellable) {
  g_cancellable_cancel(G_CANCELLABLE(cancellable));
  return G_SOURCE_REMOVE;
}

// What one probe holds, released on every way out of it: the deadline first,
// so it cannot fire into a request that has already ended.
struct ProbeHold {
  GCancellable* cancellable = nullptr;
  GSource* deadline = nullptr;
  SoupSession* session = nullptr;
  SoupMessage* message = nullptr;
  GInputStream* body = nullptr;

  ProbeHold() = default;
  ProbeHold(const ProbeHold&) = delete;
  ProbeHold& operator=(const ProbeHold&) = delete;
  ~ProbeHold() {
    if (deadline) {
      g_source_destroy(deadline);
      g_source_unref(deadline);
    }
    if (body) {
      g_input_stream_close(body, nullptr, nullptr);
      g_object_unref(body);
    }
    if (message) g_object_unref(message);
    if (session) g_object_unref(session);
    if (cancellable) g_object_unref(cancellable);
  }
};

// "offered", or the closed word for why the page is not offered. Nothing about the
// response is in the word, let alone the response, and no GError message is kept.
// `listed` is the versions the page lists (bit n for version n) when it is offered,
// and the caller reads it only then.
std::string_view Probe(std::uint32_t& listed) {
  const auto started = std::chrono::steady_clock::now();
  const auto tooSlow = [&started] {
    return std::chrono::steady_clock::now() - started > approval::kCheckBudget;
  };

  {
    GUri* uri = g_uri_parse(config::kApprovalDescriptorUrl, G_URI_FLAGS_NONE, nullptr);
    if (!uri) return "bad-url";
    // http is reachable only through a test origin (Config.hpp)
    const char* scheme = g_uri_get_scheme(uri);
    const bool known =
        scheme && (std::strcmp(scheme, "https") == 0 || std::strcmp(scheme, "http") == 0);
    g_uri_unref(uri);
    if (!known) return "bad-url";
  }

  ProbeHold hold;
  // made here, on the worker, and used nowhere else; libsoup 3 adds no cookie jar,
  // and the default proxy resolver is the desktop's
  hold.session = soup_session_new();
  soup_session_set_user_agent(hold.session, "URnetwork-Linux/" UR_APP_VERSION);
  soup_session_set_timeout(hold.session, kBudgetSeconds);
  // no credentials are offered to the page, and the 4096-byte cap is on the bytes as
  // they arrive: nothing is decompressed first
  soup_session_remove_feature_by_type(hold.session, SOUP_TYPE_AUTH_MANAGER);
  soup_session_remove_feature_by_type(hold.session, SOUP_TYPE_CONTENT_DECODER);

  hold.message = soup_message_new(SOUP_METHOD_GET, config::kApprovalDescriptorUrl);
  if (!hold.message) return "bad-url";
  // A redirect is a failure, not a hop: the page is where the constant says, or it is not
  // live. The 3xx comes back as its status.
  soup_message_add_flags(hold.message, SOUP_MESSAGE_NO_REDIRECT);
  soup_message_headers_append(soup_message_get_request_headers(hold.message), "Accept",
                              "application/json");

  hold.cancellable = g_cancellable_new();
  hold.deadline = g_timeout_source_new(kBudgetMs);
  g_source_set_callback(hold.deadline, CancelOnDeadline, g_object_ref(hold.cancellable),
                        g_object_unref);
  g_source_attach(hold.deadline, nullptr);

  GError* error = nullptr;
  hold.body = soup_session_send(hold.session, hold.message, hold.cancellable, &error);
  if (!hold.body) {
    g_clear_error(&error);
    return tooSlow() ? "too-slow" : "no-connection";
  }
  if (tooSlow()) return "too-slow";

  if (soup_message_get_status(hold.message) != 200) return "bad-status";

  // the header as received: IsJsonContentType reads its media type and parameters
  const char* contentType =
      soup_message_headers_get_one(soup_message_get_response_headers(hold.message), "Content-Type");

  // the body, at most one chunk past what the descriptor may be (enough to tell it is too large)
  std::string body;
  for (;;) {
    if (tooSlow()) return "too-slow";
    char chunk[1024];
    const gssize n = g_input_stream_read(hold.body, chunk, sizeof chunk, hold.cancellable, &error);
    if (n < 0) {
      g_clear_error(&error);
      return tooSlow() ? "too-slow" : "unreadable";
    }
    if (n == 0) break;
    body.append(chunk, static_cast<std::size_t>(n));
    if (body.size() > approval::kMaxDescriptorBytes) break;
  }
  if (tooSlow()) return "too-slow";
  const approval::Listing answer = approval::ReadDescriptor(contentType ? contentType : "", body);
  listed = answer.contracts;
  return approval::ToString(answer.verdict);
}

void Run() {
  std::uint32_t listed = 0;
  const std::string_view word = Probe(listed);
  const bool live = word == approval::ToString(approval::DescriptorVerdict::Offered);
  // success and failure alike are kept for the run: nothing asks again
  g_contracts.store(live ? listed : 0U, std::memory_order_release);
  g_state.store(static_cast<int>(live ? ApprovalPage::Live : ApprovalPage::Down),
                std::memory_order_release);
  g_message("approval: the page check: %.*s", static_cast<int>(word.size()), word.data());
}

}  // namespace

bool ProcessIsElevated() { return ::geteuid() == 0 || ::getuid() != ::geteuid(); }

void StartApprovalPageCheck() {
  int expected = static_cast<int>(ApprovalPage::NotChecked);
  if (!g_state.compare_exchange_strong(expected, static_cast<int>(ApprovalPage::Checking))) return;
  try {
    std::thread(Run).detach();
  } catch (const std::system_error&) {
    g_state.store(static_cast<int>(ApprovalPage::Down), std::memory_order_release);
    g_warning("approval: the page check could not start");
  }
}

ApprovalPage ApprovalPageState() {
  return static_cast<ApprovalPage>(g_state.load(std::memory_order_acquire));
}

bool ApprovalPageLists(std::int64_t contract) {
  if (ApprovalPageState() != ApprovalPage::Live) return false;
  approval::Listing answer;
  answer.contracts = g_contracts.load(std::memory_order_acquire);
  return answer.Lists(contract);
}

}  // namespace urnw
