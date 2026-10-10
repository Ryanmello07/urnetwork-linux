// SPDX-License-Identifier: MPL-2.0
#include "Formatters.hpp"

#include "I18n.hpp"
#include "RelativeTimeSpan.hpp"

#include <algorithm>
#include <cstdio>
#include <unordered_set>

#include <arpa/inet.h>
#include <glib.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <urnetwork_sdk.hpp>

namespace urnw {
namespace {

// ">=100 -> %.0f, >=10 -> %.1f, else %.2f" magnitude formatting shared by the
// byte and bit rate helpers.
std::string FormatScaled(double value, const char* unit) {
  char buf[64];
  if (value >= 100) {
    std::snprintf(buf, sizeof(buf), "%.0f %s", value, unit);
  } else if (value >= 10) {
    std::snprintf(buf, sizeof(buf), "%.1f %s", value, unit);
  } else {
    std::snprintf(buf, sizeof(buf), "%.2f %s", value, unit);
  }
  return buf;
}

}  // namespace

std::string FormatByteCountCompact(int64_t byteCount) {
  constexpr double kKib = 1024.0;
  constexpr double kMib = kKib * 1024;
  constexpr double kGib = kMib * 1024;
  constexpr double kTib = kGib * 1024;
  const double v = static_cast<double>(byteCount);
  if (v < kKib) return std::to_string(byteCount) + " B";
  if (v < kMib) return FormatScaled(v / kKib, "KiB");
  if (v < kGib) return FormatScaled(v / kMib, "MiB");
  if (v < kTib) return FormatScaled(v / kGib, "GiB");
  return FormatScaled(v / kTib, "TiB");
}

std::string FormatByteRate(int64_t bytesPerSecond) {
  return FormatByteCountCompact(bytesPerSecond) + "/s";
}

std::string FormatPacketRate(int64_t packetsPerSecond) {
  return FormatCountCompact(packetsPerSecond) + " pkt/s";
}

std::string FormatBitRate(int64_t bitsPerSecond) {
  const double v = static_cast<double>(bitsPerSecond);
  if (v < 1e3) return std::to_string(bitsPerSecond) + " bps";
  if (v < 1e6) return FormatScaled(v / 1e3, "Kbps");
  if (v < 1e9) return FormatScaled(v / 1e6, "Mbps");
  return FormatScaled(v / 1e9, "Gbps");
}

std::string RelativeTime(int64_t secondsAgo) {
  const relative_time::Span span = relative_time::SpanFor(secondsAgo);
  switch (span.unit) {
    case relative_time::Unit::Now:
      return T_("now", "now");
    case relative_time::Unit::Seconds:
      return Format(T_("seconds_ago_abbrev", "{}s ago"), span.count);
    case relative_time::Unit::Minutes:
      return Format(T_("minutes_ago_abbrev", "{}m ago"), span.count);
    case relative_time::Unit::Hours:
      return Format(T_("hours_ago_abbrev", "{}h ago"), span.count);
    case relative_time::Unit::Days:
      return Format(T_("days_ago_abbrev", "{}d ago"), span.count);
    case relative_time::Unit::Date:
      // a week or more: the day it happened, not "12d ago"
      return LocalDate(g_get_real_time() / G_USEC_PER_SEC - secondsAgo);
  }
  return T_("now", "now");
}

namespace {

// A Unix second in this machine's zone, in a g_date_time_format pattern; ""
// when it cannot be represented.
std::string FormatLocal(int64_t unixSeconds, const char* pattern) {
  GDateTime* moment = g_date_time_new_from_unix_local(unixSeconds);
  if (!moment) return {};
  gchar* text = g_date_time_format(moment, pattern);
  g_date_time_unref(moment);
  std::string out = text ? text : "";
  g_free(text);
  return out;
}

}  // namespace

std::string LocalDate(int64_t unixSeconds) { return FormatLocal(unixSeconds, "%x"); }

std::string LocalDateTime(int64_t unixSeconds) { return FormatLocal(unixSeconds, "%x, %R"); }

bool IsIpAddressValue(const std::string& value) {
  unsigned char buf[sizeof(struct in6_addr)];
  return inet_pton(AF_INET, value.c_str(), buf) == 1 ||
         inet_pton(AF_INET6, value.c_str(), buf) == 1;
}

bool IsValidDohUrl(const std::string& value) {
  GUri* uri = g_uri_parse(value.c_str(), G_URI_FLAGS_NONE, nullptr);
  if (!uri) return false;
  const char* scheme = g_uri_get_scheme(uri);
  const char* host = g_uri_get_host(uri);
  const bool ok = scheme && g_ascii_strcasecmp(scheme, "https") == 0 && host && *host;
  g_uri_unref(uri);
  return ok;
}

std::string TrimWhitespace(const std::string& value) {
  const char* ws = " \t\r\n";
  const size_t begin = value.find_first_not_of(ws);
  if (begin == std::string::npos) return "";
  const size_t end = value.find_last_not_of(ws);
  return value.substr(begin, end - begin + 1);
}

}  // namespace urnw
