// The app-wide client event queue: the one place the desktop app records the
// product events the onboarding optimization loop reads (POST /client/events,
// a closed schema the server validates).
//
// The SDK owns the schema. Every event is built by an SDK constructor
// (urnet_new_*_event, which returns the event as JSON with its props), never
// assembled by hand here, so the desktop cannot drift from the other apps.
// The queue itself is the desktop's own: the SDK's ClientEventQueue is not
// reachable through the C ABI (the struct is exported empty and carries no
// methods), so this class fills the envelope fields the SDK queue would
// (at, platform, app_version, locale, session), persists the pending events
// under the app's storage directory, and sends them in batches through the
// raw C export urnet_api_client_events_send — the typed hpp wrapper drops the
// props on the way through, which is why the raw call is used.
//
// Delivery: at most 200 events per call, a batch is retried up to three
// times across flushes and then dropped; rejected events (schema failures)
// are dropped with their batch. Events are only sent while a session exists
// (the endpoint is authenticated); before that they wait on disk.
//
// Threading: everything public runs on the GTK main loop. The send callback
// arrives on an SDK thread and marshals back through PostToMain.
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <glibmm/main.h>
#include <nlohmann/json.hpp>

namespace urnw {

class ClientEventQueue {
 public:
  // storageDir: where the pending events persist (client_events.json).
  explicit ClientEventQueue(std::string storageDir);
  ~ClientEventQueue();

  ClientEventQueue(const ClientEventQueue&) = delete;
  ClientEventQueue& operator=(const ClientEventQueue&) = delete;

  // Binds the queue to the SDK Api (its C handle) and the session gate: the
  // queue only sends while canSend() is true. Call once after the SDK is up.
  void Attach(uint64_t apiHandle, std::function<bool()> canSend);
  // Sends what is pending now (window hide, logout, quit).
  void Flush();
  // A new session id for the events that follow (launch, sign-in).
  void NewSession();
  size_t Pending() const { return pending_.size(); }
  const std::string& Session() const { return session_; }

  // ---- the facade: one method per SDK event constructor -------------------
  void OnboardingStepShown(const std::string& step, int64_t index, int64_t elapsedMs);
  void OnboardingStepCompleted(const std::string& step, int64_t index, int64_t elapsedMs);
  void OnboardingStepSkipped(const std::string& step, int64_t index, int64_t elapsedMs);
  void ConnectFirst();
  void FeedbackSubmitted(int64_t rating, const std::string& reason, const std::string& text);
  void PurchaseStarted(const std::string& store, const std::string& product,
                       const std::string& plan, bool trial, double price,
                       const std::string& currency);
  void PurchaseCompleted(const std::string& store, const std::string& product,
                         const std::string& plan, bool trial, double price,
                         const std::string& currency);
  void PurchaseCancelled(const std::string& store, const std::string& product,
                         const std::string& plan, bool trial, double price,
                         const std::string& currency);
  void PurchaseFailed(const std::string& store, const std::string& product,
                      const std::string& plan, bool trial, double price,
                      const std::string& currency, const std::string& errorClass);
  void OfferScreenShown(const std::string& surface, const std::string& experiment,
                        const std::string& variant, const std::string& tier, double priceShown,
                        const std::string& currency, int64_t expiresInS);
  void OfferCardTapped(const std::string& plan);
  void OfferCtaTapped(const std::string& plan, const std::string& store);
  void OfferDeclined(const std::string& control, int64_t elapsedMs);
  void SignupOptoutChanged(bool productUpdates);
  void WidgetAdded(const std::string& kind);

 private:
  // Takes the SDK constructor's JSON (ownership: freed with urnet_free_string),
  // fills the envelope and queues it.
  void Add(char* sdkEventJson);
  void Load();
  void Save() const;
  void ScheduleFlush(unsigned delayMs);
  void SendBatch();
  void OnSent(uint64_t issued, bool ok, const std::string& resultJson);

  std::string storageDir_;
  std::string platform_ = "linux";
  std::string appVersion_;
  std::string locale_;
  std::string session_;
  uint64_t apiHandle_ = 0;
  std::function<bool()> canSend_;

  std::vector<nlohmann::json> pending_;
  bool inFlight_ = false;
  size_t inFlightCount_ = 0;
  int attempts_ = 0;  // failed sends of the batch at the head of the queue
  sigc::connection flushTimer_;
  // outlives the send callbacks: a callback landing after the queue is gone
  // finds the flag down and does nothing
  std::shared_ptr<uint64_t> epoch_ = std::make_shared<uint64_t>(0);
};

// The locale tag the events carry ("en-US"), from the process's language list.
std::string ClientEventLocale();

}  // namespace urnw
