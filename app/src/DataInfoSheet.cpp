// SPDX-License-Identifier: MPL-2.0
#include "DataInfoSheet.hpp"

#include <glib.h>

#include <cstdint>
#include <string>

#include "DataInfo.hpp"
#include "Formatters.hpp"
#include "I18n.hpp"
#include "PaneKit.hpp"
#include "Ui.hpp"

namespace urnw {
namespace {

// The wall clock in epoch milliseconds: the refresh is a UTC midnight, which
// the monotonic clock does not know.
int64_t WallClockMillis() { return g_get_real_time() / 1000; }

// One amount: the usage bar's dot and name with the amount at the right, and
// what it means under them. Returns the amount's label.
Gtk::Label* AppendDataInfoRow(Gtk::Box& parent, const Glib::ustring& name, const Rgba& color,
                              const Glib::ustring& explanation) {
  auto* row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 4);
  auto* line = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
  auto* dot = Gtk::make_managed<Gtk::Label>();
  dot->set_markup("<span foreground='" + HexForMarkup(color) + "'>●</span>");
  dot->add_css_class("ur-caption-11");
  dot->set_valign(Gtk::Align::CENTER);
  line->append(*dot);
  auto* label = Gtk::make_managed<Gtk::Label>(name);
  label->add_css_class("dim-label");
  label->set_xalign(0);
  label->set_hexpand(true);
  line->append(*label);
  auto* amount = Gtk::make_managed<Gtk::Label>();
  amount->add_css_class("dim-label");
  amount->set_xalign(1);
  line->append(*amount);
  row->append(*line);
  auto* text = Gtk::make_managed<Gtk::Label>(explanation);
  text->set_xalign(0);
  text->set_wrap(true);
  row->append(*text);
  parent.append(*row);
  return amount;
}

}  // namespace

std::string FreeRefreshCountdownText() {
  return data_info::FormatRefreshCountdown(
      data_info::FreeRefreshCountdown(WallClockMillis()),
      [](int64_t hours, int64_t minutes) {
        return Format(T_("provider_connected_duration_hours", "{0}h {1}m"), hours, minutes);
      },
      [](int64_t minutes) {
        return Format(T_("provider_connected_duration_minutes", "{}m"), minutes);
      });
}

void FreeRefreshTicker::Start(std::function<void()> apply) {
  apply_ = std::move(apply);
  apply_();
  Arm();
}

void FreeRefreshTicker::Stop() {
  apply_ = nullptr;
  timeout_.disconnect();
}

// one shot, re-armed at each change: the countdown moves once per displayed
// minute, so the line never shows a stale minute and the loop never spins
void FreeRefreshTicker::Arm() {
  timeout_.disconnect();
  timeout_ = Glib::signal_timeout().connect(
      [this]() -> bool {
        if (apply_) {
          apply_();
          Arm();
        }
        return false;
      },
      static_cast<unsigned>(data_info::MillisUntilCountdownChanges(WallClockMillis())));
}

DataInfoSheet::DataInfoSheet(Gtk::Window& parent, SubscriptionBalanceStore& balance)
    : balance_(balance) {
  set_title(T_("data_info_title", "About your data"));
  set_transient_for(parent);
  set_modal(true);
  set_default_size(420, -1);
  set_resizable(false);
  set_hide_on_close(true);
  AddEscapeToClose(*this);

  auto* root = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 16);
  root->set_margin(24);
  set_child(*root);

  auto* title = Gtk::make_managed<Gtk::Label>(T_("data_info_title", "About your data"));
  title->add_css_class("title-2");
  title->set_xalign(0);
  root->append(*title);

  // the usage bar's names and colors (UsageBar.cpp)
  usedValue_ = AppendDataInfoRow(*root, T_("used_data_key", "Used"), kUrElectricBlue,
                                 T_("data_info_used", "Data you've used so far."));
  pendingValue_ = AppendDataInfoRow(
      *root, T_("pending_data_key", "Pending"), kUrCoral,
      T_("data_info_pending",
         "Data reserved for your open connections. What they don't use is returned when they close."));
  availableValue_ = AppendDataInfoRow(*root, T_("available_data_key", "Available"), kUrTextFaint,
                                      T_("data_info_available", "Data you can still use."));

  root->append(*Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::HORIZONTAL));

  // the daily balance, as the Account page's row prints it
  auto* daily = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
  auto* dailyLabel =
      Gtk::make_managed<Gtk::Label>(T_("daily_data_balance_label", "Daily Data Balance:"));
  dailyLabel->add_css_class("dim-label");
  dailyLabel->set_xalign(0);
  dailyLabel->set_hexpand(true);
  daily->append(*dailyLabel);
  dailyValue_ = Gtk::make_managed<Gtk::Label>();
  dailyValue_->add_css_class("dim-label");
  daily->append(*dailyValue_);
  root->append(*daily);

  refreshLabel_ = Gtk::make_managed<Gtk::Label>();
  refreshLabel_->add_css_class("dim-label");
  refreshLabel_->set_xalign(0);
  refreshLabel_->set_wrap(true);
  root->append(*refreshLabel_);

  auto* close = Gtk::make_managed<Gtk::Button>(T_("close", "Close"));
  close->set_halign(Gtk::Align::END);
  close->signal_clicked().connect([this] { set_visible(false); });
  root->append(*close);

  signal_hide().connect([this] { refreshTicker_.Stop(); });
}

void DataInfoSheet::Open() {
  // before the first balance there is nothing to report: a dash, not a
  // fabricated "0 B" (the Account page's rule for async fields)
  if (balance_.HasFetched()) {
    const auto info = data_info::DataInfoFrom(
        balance_.StartBalanceByteCount(), balance_.AvailableByteCount(),
        balance_.PendingByteCount(), [](int64_t bytes) { return FormatByteCountCompact(bytes); });
    usedValue_->set_text(info.used);
    pendingValue_->set_text(info.pending);
    availableValue_->set_text(info.available);
    dailyValue_->set_text(info.daily);
  } else {
    for (auto* value : {usedValue_, pendingValue_, availableValue_, dailyValue_}) {
      value->set_text("—");
    }
  }
  // Pro gets no free daily grant, so no refresh line
  const bool refresh = data_info::ShowsFreeRefresh(balance_.IsPro());
  refreshLabel_->set_visible(refresh);
  if (refresh) {
    refreshTicker_.Start([this] {
      refreshLabel_->set_text(Format(
          T_("data_info_refresh_at", "Free data refreshes daily at 00:00 UTC (in {})."),
          FreeRefreshCountdownText()));
    });
  } else {
    refreshTicker_.Stop();
  }
  present();
}

}  // namespace urnw
