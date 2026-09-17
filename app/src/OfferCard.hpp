// The welcome offer's supporting lines (mmm/onboarding/PLAN.md "in-app offer
// screen"), shared by every surface that prints the offer — the onboarding
// welcome page under the plan picker, the final offer page, and the upgrade
// sheet — so the deadline, the trial timeline and the terms cannot drift:
//
//   Available until <local date, time>        (static: never a countdown)
//   Today     Free trial starts
//   Day 12    Reminder before the charge
//   Day 14    $30 for the year, cancel anytime before
//   14 days free, then $30 for your first year, then $40/year. Cancel anytime.
//
// The compact form (the upgrade sheet) is the deadline line and the terms
// only. All texts come from the store; the numbers from the offer and tier.
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <string>

#include <gtkmm.h>

#include "PricePresentation.hpp"

namespace urnw {

// The plan cards' texts for a tier and (optionally) the offer, composed from
// the store's strings. One composer, so the picker and the offer page agree.
struct PlanCardTexts {
  std::string yearlyPrice;       // "$40/year" | "$30 for your first year"
  std::string yearlySecondary;   // "Save 33%" | "then $40/year"
  std::string yearlyEquivalent;  // "≈ $3.34/month · billed once a year" or ""
  std::string yearlyTrial;       // "Includes 14 day free trial"
  std::string monthlyPrice;      // "$5/month"
  std::string monthlyLine;       // "Billed monthly · cancel anytime"
};
PlanCardTexts ComposePlanCardTexts(const PriceTierView& tier, const OfferView& offer,
                                   int64_t trialDays);

// The offer's deadline as the store's sentence, in the user's locale.
std::string OfferDeadlineText(const OfferView& offer);
// Seconds until the offer expires (0 when unknown or past).
int64_t OfferExpiresInSeconds(const OfferView& offer);

class OfferCard : public Gtk::Box {
 public:
  explicit OfferCard(bool compact = false);
  void Update(const OfferView& offer, const PriceTierView& tier, int64_t trialDays);

 private:
  bool compact_;
  Gtk::Label* deadline_ = nullptr;
  Gtk::Label* chargeLine_ = nullptr;
  Gtk::Label* terms_ = nullptr;
};

}  // namespace urnw
