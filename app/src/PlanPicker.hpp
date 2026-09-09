// The plan picker the onboarding welcome page and the upgrade sheet share:
// the yearly plan in the Pro-gold dress (price, saving, the free trial line,
// the Best value pill) selected by default, the monthly plan plain below it
// with no trial. ONE implementation, so the two surfaces cannot drift in
// order, default, copy or the CTA label rule (yearly -> "Start free trial",
// monthly -> "Subscribe": only the yearly plan carries the trial).
//
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <gtkmm.h>

#include "PricePresentation.hpp"

namespace urnw {

struct PlanCardTexts;

// The free trial the yearly plan starts with, in days, as printed on the plan
// card. The trial itself is the server's Stripe checkout session setting
// (subscription_stripe_controller: trial_period_days) and must match this.
inline constexpr int64_t kFreeTrialDays = 14;

class GoldPlanCard;

class PlanPicker : public Gtk::Box {
 public:
  PlanPicker();

  bool Yearly() const { return yearly_; }
  // Select a plan programmatically: repaints, does NOT fire on_select (the
  // host sets its own CTA label from CtaLabel).
  void Select(bool yearly);
  // The CTA label for a selection: only the yearly plan carries the trial.
  static std::string CtaLabel(bool yearly);
  // Reprints both cards from the price tier and, while it is active, the
  // welcome offer (the yearly card becomes the offer card: first-year price,
  // then the regular price, the trial line). Called by the host from the
  // balance store's feed; the picker starts on the standard tier.
  void SetPrices(const PriceTierView& tier, const OfferView& offer);
  void SetTexts(const PlanCardTexts& texts);

  // Fired on a tap, after the selection changed.
  std::function<void(bool yearly)> on_select;

 private:
  void Paint();

  bool yearly_ = true;
  GoldPlanCard* yearlyCard_ = nullptr;
  Gtk::Button* monthlyCard_ = nullptr;
  Gtk::Label* monthlyDot_ = nullptr;
  Gtk::Label* monthlyText_ = nullptr;
  Gtk::Label* monthlyLine_ = nullptr;
  std::vector<Gtk::Label*> reservedLines_;  // sizes the monthly card like the yearly
};

}  // namespace urnw
