// SPDX-License-Identifier: MPL-2.0
#include "ClientEvents.hpp"

#include <cstdio>
#include <fstream>

#include <glib.h>
#include <glibmm/datetime.h>
#include <urnetwork_sdk.h>

#include "Ui.hpp"

#ifndef UR_APP_VERSION
#define UR_APP_VERSION "0.0.0"
#endif

namespace urnw {
namespace {

// the server's per-call cap (sdk MaxClientEventsPerCall)
constexpr size_t kMaxPerCall = 200;
// a batch that fails this many sends is dropped: the loop tolerates gaps,
// not a queue that never drains
constexpr int kMaxAttempts = 3;
// pending events kept on disk at most; the oldest go first
constexpr size_t kMaxPending = 1000;
constexpr unsigned kFlushDelayMs = 2500;   // after an add: batch the burst
constexpr unsigned kRetryDelayMs = 30000;  // after a failed send

std::string EventsPath(const std::string& storageDir) { return storageDir + "/client_events.json"; }

std::string NowRfc3339() {
  auto now = Glib::DateTime::create_now_utc();
  return now.format_iso8601();
}

}  // namespace

std::string ClientEventLocale() {
  const char* const* names = g_get_language_names();
  std::string tag = names && names[0] ? names[0] : "";
  if (tag.empty() || tag == "C" || tag == "POSIX") return "en";
  // "en_US.UTF-8" -> "en-US"
  if (const auto dot = tag.find('.'); dot != std::string::npos) tag.erase(dot);
  if (const auto at = tag.find('@'); at != std::string::npos) tag.erase(at);
  for (auto& c : tag) {
    if (c == '_') c = '-';
  }
  return tag;
}

ClientEventQueue::ClientEventQueue(std::string storageDir)
    : storageDir_(std::move(storageDir)), appVersion_(UR_APP_VERSION), locale_(ClientEventLocale()) {
  NewSession();
  Load();
}

ClientEventQueue::~ClientEventQueue() {
  ++*epoch_;
  flushTimer_.disconnect();
  Save();
}

void ClientEventQueue::Attach(uint64_t apiHandle, std::function<bool()> canSend) {
  apiHandle_ = apiHandle;
  canSend_ = std::move(canSend);
  if (!pending_.empty()) ScheduleFlush(kFlushDelayMs);
}

void ClientEventQueue::NewSession() {
  char* id = g_uuid_string_random();
  session_ = id ? id : "";
  if (id) g_free(id);
}

void ClientEventQueue::Flush() {
  flushTimer_.disconnect();
  SendBatch();
}

// ---- the facade -------------------------------------------------------------

void ClientEventQueue::OnboardingStepShown(const std::string& step, int64_t index, int64_t elapsedMs) {
  Add(urnet_new_onboarding_step_shown_event(step.c_str(), index, elapsedMs));
}
void ClientEventQueue::OnboardingStepCompleted(const std::string& step, int64_t index, int64_t elapsedMs) {
  Add(urnet_new_onboarding_step_completed_event(step.c_str(), index, elapsedMs));
}
void ClientEventQueue::OnboardingStepSkipped(const std::string& step, int64_t index, int64_t elapsedMs) {
  Add(urnet_new_onboarding_step_skipped_event(step.c_str(), index, elapsedMs));
}
void ClientEventQueue::ConnectFirst() { Add(urnet_new_connect_first_event()); }
void ClientEventQueue::FeedbackSubmitted(int64_t rating, const std::string& reason,
                                         const std::string& text) {
  Add(urnet_new_feedback_submitted_event(rating, reason.c_str(), text.c_str()));
}
void ClientEventQueue::PurchaseStarted(const std::string& store, const std::string& product,
                                       const std::string& plan, bool trial, double price,
                                       const std::string& currency) {
  Add(urnet_new_purchase_started_event(store.c_str(), product.c_str(), plan.c_str(), trial, price,
                                       currency.c_str()));
}
void ClientEventQueue::PurchaseCompleted(const std::string& store, const std::string& product,
                                         const std::string& plan, bool trial, double price,
                                         const std::string& currency) {
  Add(urnet_new_purchase_completed_event(store.c_str(), product.c_str(), plan.c_str(), trial,
                                         price, currency.c_str()));
}
void ClientEventQueue::PurchaseCancelled(const std::string& store, const std::string& product,
                                         const std::string& plan, bool trial, double price,
                                         const std::string& currency) {
  Add(urnet_new_purchase_cancelled_event(store.c_str(), product.c_str(), plan.c_str(), trial,
                                         price, currency.c_str()));
}
void ClientEventQueue::PurchaseFailed(const std::string& store, const std::string& product,
                                      const std::string& plan, bool trial, double price,
                                      const std::string& currency, const std::string& errorClass) {
  Add(urnet_new_purchase_failed_event(store.c_str(), product.c_str(), plan.c_str(), trial, price,
                                      currency.c_str(), errorClass.c_str()));
}
void ClientEventQueue::OfferScreenShown(const std::string& surface, const std::string& experiment,
                                        const std::string& variant, const std::string& tier,
                                        double priceShown, const std::string& currency,
                                        int64_t expiresInS) {
  Add(urnet_new_offer_screen_shown_event(surface.c_str(), experiment.c_str(), variant.c_str(),
                                         tier.c_str(), priceShown, currency.c_str(), expiresInS));
}
void ClientEventQueue::OfferCardTapped(const std::string& plan) {
  Add(urnet_new_offer_card_tapped_event(plan.c_str()));
}
void ClientEventQueue::OfferCtaTapped(const std::string& plan, const std::string& store) {
  Add(urnet_new_offer_cta_tapped_event(plan.c_str(), store.c_str()));
}
void ClientEventQueue::OfferDeclined(const std::string& control, int64_t elapsedMs) {
  Add(urnet_new_offer_declined_event(control.c_str(), elapsedMs));
}
void ClientEventQueue::SignupOptoutChanged(bool productUpdates) {
  Add(urnet_new_signup_optout_changed_event(productUpdates));
}
void ClientEventQueue::WidgetAdded(const std::string& kind) {
  Add(urnet_new_widget_added_event(kind.c_str()));
}

// ---- the queue ----------------------------------------------------------------

void ClientEventQueue::Add(char* sdkEventJson) {
  if (!sdkEventJson) return;
  nlohmann::json event = nlohmann::json::parse(sdkEventJson, nullptr, false);
  urnet_free_string(sdkEventJson);
  if (!event.is_object() || !event.contains("name")) return;
  // the envelope the SDK queue would fill
  event["at"] = NowRfc3339();
  event["platform"] = platform_;
  event["app_version"] = appVersion_;
  event["locale"] = locale_;
  event["session"] = session_;
  pending_.push_back(std::move(event));
  while (kMaxPending < pending_.size()) pending_.erase(pending_.begin());
  Save();
  ScheduleFlush(kFlushDelayMs);
}

void ClientEventQueue::Load() {
  std::ifstream in(EventsPath(storageDir_));
  if (!in.good()) return;
  nlohmann::json parsed = nlohmann::json::parse(in, nullptr, false);
  if (!parsed.is_array()) return;
  for (auto& e : parsed) {
    if (e.is_object() && e.contains("name")) pending_.push_back(std::move(e));
  }
}

void ClientEventQueue::Save() const {
  const std::string path = EventsPath(storageDir_);
  if (pending_.empty()) {
    std::remove(path.c_str());
    return;
  }
  std::ofstream out(path, std::ios::trunc);
  if (!out.good()) return;
  out << nlohmann::json(pending_).dump();
}

void ClientEventQueue::ScheduleFlush(unsigned delayMs) {
  if (flushTimer_.connected()) return;
  flushTimer_ = Glib::signal_timeout().connect(
      [this] {
        flushTimer_.disconnect();
        SendBatch();
        return false;
      },
      delayMs);
}

void ClientEventQueue::SendBatch() {
  if (inFlight_ || pending_.empty() || apiHandle_ == 0) return;
  if (canSend_ && !canSend_()) return;  // no session yet: wait on disk
  const size_t count = std::min(kMaxPerCall, pending_.size());
  nlohmann::json args = nlohmann::json::object();
  args["events"] = nlohmann::json(std::vector<nlohmann::json>(pending_.begin(), pending_.begin() + count));
  const std::string json = args.dump();
  inFlight_ = true;
  inFlightCount_ = count;
  const uint64_t issued = *epoch_;

  struct Ticket {
    ClientEventQueue* queue;
    std::shared_ptr<uint64_t> epoch;
    uint64_t issued;
  };
  auto* ticket = new Ticket{this, epoch_, issued};
  urnet_api_client_events_send(
      apiHandle_, json.c_str(),
      +[](void* userData, const char* resultJson, const char* err) {
        std::unique_ptr<Ticket> t(static_cast<Ticket*>(userData));
        const bool ok = err == nullptr && resultJson != nullptr;
        std::string result = resultJson ? resultJson : "";
        if (err) std::fprintf(stderr, "[events] send failed: %s\n", err);
        ClientEventQueue* queue = t->queue;
        std::shared_ptr<uint64_t> epoch = t->epoch;
        const uint64_t issued = t->issued;
        PostToMain([queue, epoch, issued, ok, result = std::move(result)] {
          if (*epoch != issued) return;  // the queue is gone
          queue->OnSent(issued, ok, result);
        });
      },
      ticket);
}

void ClientEventQueue::OnSent(uint64_t, bool ok, const std::string& resultJson) {
  inFlight_ = false;
  bool drop = ok;
  if (ok) {
    // {accepted, rejected:[{index,message}]}: rejected events are schema
    // failures, never retried; the whole batch is done either way
    nlohmann::json result = nlohmann::json::parse(resultJson, nullptr, false);
    if (result.is_object() && result.contains("rejected") && result["rejected"].is_array()) {
      for (const auto& r : result["rejected"]) {
        if (r.is_object() && r.contains("message") && r["message"].is_string()) {
          std::fprintf(stderr, "[events] rejected: %s\n", r["message"].get<std::string>().c_str());
        }
      }
    }
    attempts_ = 0;
  } else {
    ++attempts_;
    if (kMaxAttempts <= attempts_) {
      std::fprintf(stderr, "[events] dropping %zu events after %d failed sends\n", inFlightCount_,
                   attempts_);
      drop = true;
      attempts_ = 0;
    }
  }
  if (drop) {
    const size_t n = std::min(inFlightCount_, pending_.size());
    pending_.erase(pending_.begin(), pending_.begin() + n);
    Save();
  }
  inFlightCount_ = 0;
  if (!pending_.empty()) ScheduleFlush(ok ? kFlushDelayMs : kRetryDelayMs);
}

}  // namespace urnw
