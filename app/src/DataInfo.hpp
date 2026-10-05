// The "About your data" sheet: what Used, Pending and Available mean, the
// daily balance and when the free data refreshes.
//
// The sheet explains the usage bar: Used, Pending (balance held by open
// connections, returned when they close) and Available, the daily balance the
// server reports (start_balance_byte_count, never a hard-coded amount), and
// when the free data refreshes. The server grants the free daily balance at
// 00:00 UTC to every network without Pro (RefreshFreeTransferBalances), so the
// refresh is the next UTC midnight after the wall clock the caller passes in.
// It opens from the info button by the daily balance on Account and from the
// Why? link in the Connect page's out-of-balance alert.
//
// Standard library only: the caller passes the time (epoch milliseconds), the
// strings and the byte format, so the unit tests need no GTK or SDK.
//
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <cstdint>

namespace urnw {
namespace data_info {

constexpr int64_t kDayMillis = 24LL * 60 * 60 * 1000;
constexpr int64_t kMinuteMillis = 60000;

// The next free data refresh: the first 00:00 UTC strictly after nowMillis.
// Epoch time has no leap seconds, so UTC days are whole multiples of a day.
constexpr int64_t NextFreeRefreshMillis(int64_t nowMillis) {
  // division truncates toward zero; a time before 1970 belongs to the day before
  return (nowMillis / kDayMillis - (nowMillis % kDayMillis < 0 ? 1 : 0) + 1) * kDayMillis;
}

struct RefreshCountdown {
  int64_t hours = 0;
  int64_t minutes = 0;
};

constexpr bool operator==(const RefreshCountdown& a, const RefreshCountdown& b) {
  return a.hours == b.hours && a.minutes == b.minutes;
}

// The time left until the next free data refresh, rounded up to whole minutes
// so it never reads zero while the refresh is still ahead.
constexpr RefreshCountdown FreeRefreshCountdown(int64_t nowMillis) {
  const int64_t totalMinutes =
      (NextFreeRefreshMillis(nowMillis) - nowMillis + kMinuteMillis - 1) / kMinuteMillis;
  return RefreshCountdown{totalMinutes / 60, totalMinutes % 60};
}

// How long until FreeRefreshCountdown shows a different value, so a timeout
// re-armed with it ticks once per displayed minute.
constexpr int64_t MillisUntilCountdownChanges(int64_t nowMillis) {
  return (NextFreeRefreshMillis(nowMillis) - nowMillis - 1) % kMinuteMillis + 1;
}

// The countdown as a compact duration ("5h 12m", or "12m" in the last hour),
// with the provider_connected_duration_hours / _minutes strings.
template <typename HoursAndMinutes, typename MinutesOnly>
auto FormatRefreshCountdown(RefreshCountdown countdown, HoursAndMinutes hoursAndMinutes,
                            MinutesOnly minutesOnly) {
  return 0 < countdown.hours ? hoursAndMinutes(countdown.hours, countdown.minutes)
                             : minutesOnly(countdown.minutes);
}

template <typename Text>
struct DataInfo {
  Text used;
  Text pending;
  Text available;
  Text daily;
};

// The sheet's amounts from the balance, split the way the usage bar splits
// it: used is start - available - pending, clamped at 0 (the server samples
// the values independently, so the raw difference can go negative).
template <typename FormatBytes>
auto DataInfoFrom(int64_t startBalanceByteCount, int64_t availableByteCount,
                  int64_t pendingByteCount, FormatBytes formatBytes) {
  const int64_t available = availableByteCount < 0 ? 0 : availableByteCount;
  const int64_t pending = pendingByteCount < 0 ? 0 : pendingByteCount;
  const int64_t start = startBalanceByteCount < 0 ? 0 : startBalanceByteCount;
  const int64_t rawUsed = start - available - pending;
  const int64_t used = rawUsed < 0 ? 0 : rawUsed;
  using Text = decltype(formatBytes(start));
  return DataInfo<Text>{formatBytes(used), formatBytes(pending), formatBytes(available),
                        formatBytes(start)};
}

// Whether the sheet says when the free data refreshes. Pro networks get the
// Pro grant instead of the free daily one, so the line would not apply.
constexpr bool ShowsFreeRefresh(bool pro) { return !pro; }

// Whether the Connect page's out-of-balance alert leads with when the free
// data refreshes and links Why? to the sheet: whenever the alert shows
// (balance_notice::HeldAlert), so Upgrade does not read as the only way back.
constexpr bool AlertShowsFreeRefresh(bool heldAlert) { return heldAlert; }

// Whether the upgrade sheet leads with when the free data refreshes and offers
// Wait for refresh: only when a start connect blocked by the balance opened it
// (MainWindow::ConnectBlockedByBalance). Pro is never blocked, and gets no
// free grant.
constexpr bool UpgradeShowsFreeRefresh(bool openedByBlockedConnect, bool pro) {
  return openedByBlockedConnect && !pro;
}

}  // namespace data_info
}  // namespace urnw
