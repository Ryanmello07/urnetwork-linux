// SPDX-License-Identifier: MPL-2.0
#include "OfferCard.hpp"

#include <glibmm/datetime.h>
#include <urnetwork_sdk.hpp>

#include "I18n.hpp"
#include "PlanPicker.hpp"  // kFreeTrialDays
#include "ReferralPanel.hpp"  // EnsureOnboardingCss
#include "Ui.hpp"

namespace urnw {
namespace {

// the reminder lands the day before the first charge (PLAN.md: day-before
// reminders outperform day-of, and a third of day-of sends are read late)
constexpr int64_t kReminderDaysBeforeCharge = 2;

Gtk::Label* MakeLine(const Glib::ustring& text, const char* cssClass) {
  auto* label = Gtk::make_managed<Gtk::Label>(text);
  label->add_css_class("ur-onb-body");
  label->add_css_class(cssClass);
  label->set_xalign(0);
  label->set_wrap(true);
  label->set_wrap_mode(Pango::WrapMode::WORD_CHAR);
  return label;
}

Gtk::Box* MakeTimelineRow(const Glib::ustring& when, Gtk::Label*& what) {
  auto* row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 12);
  auto* dot = Gtk::make_managed<Gtk::Label>();
  dot->set_markup("<span foreground='" + HexForMarkup(kProGold) + "'>●</span>");
  dot->set_valign(Gtk::Align::START);
  dot->set_margin_top(4);
  row->append(*dot);
  auto* whenLabel = MakeLine(when, "ur-onb-gold-light");
  whenLabel->set_wrap(false);
  whenLabel->set_size_request(64, -1);
  whenLabel->set_valign(Gtk::Align::START);
  row->append(*whenLabel);
  what = MakeLine("", "ur-onb-muted");
  what->set_hexpand(true);
  row->append(*what);
  return row;
}

}  // namespace

PlanCardTexts ComposePlanCardTexts(const PriceTierView& tier, const OfferView& offer,
                                   int64_t trialDays) {
  PlanCardTexts texts;
  PriceEquivalentView eq;
  if (auto computed = urnet::computePriceEquivalent(tier.yearly, tier.monthly, 2)) {
    eq.monthlyEquivalent = computed->monthly_equivalent;
    eq.showEquivalent = computed->show_equivalent;
    eq.savingPercent = computed->saving_percent;
  }
  const std::string yearly = FormatMoney(tier.yearly, tier.currency);
  const std::string monthly = FormatMoney(tier.monthly, tier.currency);
  if (offer.active) {
    texts.yearlyPrice = Format(T_("offer_first_year_price", "{} for your first year"),
                               FormatMoney(offer.firstYear, offer.currency));
    texts.yearlySecondary = Format(T_("offer_then_regular_price", "then {}/year"),
                                   FormatMoney(offer.regularYear, offer.currency));
  } else {
    texts.yearlyPrice = Format(T_("plan_price_per_year", "{}/year"), yearly);
    texts.yearlySecondary =
        ShowSaving(eq) ? Format(T_("plan_save_percent", "Save {}%"), eq.savingPercent) : "";
  }
  texts.yearlyEquivalent =
      ShowMonthlyEquivalent(tier, eq)
          ? Format(T_("plan_monthly_equivalent_line", "≈ {}/month · billed once a year"),
                   FormatMoney(eq.monthlyEquivalent, tier.currency))
          : "";
  texts.yearlyTrial = Format(T_("includes_free_trial_days", "Includes {} day free trial"), trialDays);
  texts.monthlyPrice = Format(T_("plan_price_per_month", "{}/month"), monthly);
  texts.monthlyLine = T_("plan_billed_monthly_cancel_anytime", "Billed monthly · cancel anytime");
  return texts;
}

std::string OfferDeadlineText(const OfferView& offer) {
  if (offer.expiresAt.empty()) return "";
  auto when = Glib::DateTime::create_from_iso8601(offer.expiresAt);
  if (!when) return "";
  auto local = when.to_local();
  // the locale's date, then the time: "09/14/2026, 15:04"
  const Glib::ustring stamp = local.format("%x, %R");
  return Format(T_("offer_available_until", "Available until {}"), stamp.raw());
}

int64_t OfferExpiresInSeconds(const OfferView& offer) {
  if (offer.expiresAt.empty()) return 0;
  auto when = Glib::DateTime::create_from_iso8601(offer.expiresAt);
  if (!when) return 0;
  const gint64 delta = when.to_unix() - Glib::DateTime::create_now_utc().to_unix();
  return delta < 0 ? 0 : delta;
}

OfferCard::OfferCard(bool compact) : Gtk::Box(Gtk::Orientation::VERTICAL, 8), compact_(compact) {
  EnsureOnboardingCss();
  deadline_ = MakeLine("", "ur-onb-gold-light");
  append(*deadline_);
  if (!compact_) {
    auto* timeline = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);
    timeline->set_margin_top(4);
    Gtk::Label* today = nullptr;
    timeline->append(*MakeTimelineRow(T_("offer_timeline_today", "Today"), today));
    today->set_text(T_("offer_timeline_trial_starts", "Free trial starts"));
    Gtk::Label* reminder = nullptr;
    timeline->append(*MakeTimelineRow(
        Format(T_("offer_timeline_day", "Day {}"), kFreeTrialDays - kReminderDaysBeforeCharge),
        reminder));
    reminder->set_text(T_("offer_timeline_reminder", "Reminder before the charge"));
    timeline->append(*MakeTimelineRow(Format(T_("offer_timeline_day", "Day {}"), kFreeTrialDays),
                                      chargeLine_));
    append(*timeline);
  }
  terms_ = MakeLine("", "ur-onb-muted");
  terms_->set_margin_top(4);
  append(*terms_);
}

void OfferCard::Update(const OfferView& offer, const PriceTierView& tier, int64_t trialDays) {
  const std::string first = FormatMoney(offer.firstYear, offer.currency);
  const std::string regular = FormatMoney(0 < offer.regularYear ? offer.regularYear : tier.yearly,
                                          offer.currency.empty() ? tier.currency : offer.currency);
  const std::string deadline = OfferDeadlineText(offer);
  deadline_->set_text(deadline);
  deadline_->set_visible(!deadline.empty());
  if (chargeLine_) {
    chargeLine_->set_text(
        Format(T_("offer_timeline_first_charge", "{} for the year, cancel anytime before"), first));
  }
  terms_->set_text(Format(T_("offer_terms_first_year",
                             "{} days free, then {} for your first year, then {}/year. Cancel anytime."),
                          trialDays, first, regular));
}

}  // namespace urnw
