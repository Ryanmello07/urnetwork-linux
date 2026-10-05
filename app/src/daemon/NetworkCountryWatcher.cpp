// SPDX-License-Identifier: MPL-2.0
#include "daemon/NetworkCountryWatcher.hpp"

#include <fstream>
#include <iterator>
#include <utility>

#include "NetworkQuality.hpp"
#include "daemon/DaemonLog.hpp"

namespace urnw {
namespace {

constexpr const char* kModemManagerService = "org.freedesktop.ModemManager1";
constexpr const char* kModemManagerPath = "/org/freedesktop/ModemManager1";
constexpr const char* kModemInterface = "org.freedesktop.ModemManager1.Modem";
constexpr const char* kModem3gppInterface = "org.freedesktop.ModemManager1.Modem.Modem3gpp";
constexpr const char* kBearerInterface = "org.freedesktop.ModemManager1.Bearer";
// MMModemPortType: the network interface a modem's data leaves through.
constexpr guint32 kModemPortTypeNet = 2;

constexpr guint kTickSeconds = 5;
constexpr int64_t kModemReadIntervalSeconds = 60;
constexpr gint kCallTimeoutMillis = 5000;

int64_t MonotonicSeconds() { return g_get_monotonic_time() / G_USEC_PER_SEC; }

std::string ReadProcText(const char* path) {
  std::ifstream in(path);
  if (!in) return {};
  return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// The modems in a GetManagedObjects reply ("(a{oa{sa{sv}}})"): each one's
// network ports and operator code, plus the paths of its bearers, whose
// interfaces are asked for next (bearers are not in the object manager).
std::vector<ModemReading> ParseManagedObjects(
    GVariant* reply, std::vector<std::pair<size_t, std::string>>* bearers) {
  std::vector<ModemReading> modems;
  GVariant* objects = nullptr;
  g_variant_get(reply, "(@a{oa{sa{sv}}})", &objects);
  GVariantIter objectIter;
  g_variant_iter_init(&objectIter, objects);
  const gchar* path = nullptr;
  GVariant* interfaces = nullptr;
  while (g_variant_iter_next(&objectIter, "{&o@a{sa{sv}}}", &path, &interfaces)) {
    GVariant* modem = g_variant_lookup_value(interfaces, kModemInterface, G_VARIANT_TYPE("a{sv}"));
    if (modem != nullptr) {
      ModemReading reading;
      if (GVariant* ports = g_variant_lookup_value(modem, "Ports", G_VARIANT_TYPE("a(su)"))) {
        GVariantIter portIter;
        g_variant_iter_init(&portIter, ports);
        const gchar* name = nullptr;
        guint32 type = 0;
        while (g_variant_iter_next(&portIter, "(&su)", &name, &type)) {
          if (type == kModemPortTypeNet && name != nullptr) {
            reading.data_interfaces.emplace_back(name);
          }
        }
        g_variant_unref(ports);
      }
      if (GVariant* paths = g_variant_lookup_value(modem, "Bearers", G_VARIANT_TYPE("ao"))) {
        GVariantIter bearerIter;
        g_variant_iter_init(&bearerIter, paths);
        const gchar* bearer = nullptr;
        while (g_variant_iter_next(&bearerIter, "&o", &bearer)) {
          bearers->emplace_back(modems.size(), bearer);
        }
        g_variant_unref(paths);
      }
      if (GVariant* gpp = g_variant_lookup_value(interfaces, kModem3gppInterface,
                                                 G_VARIANT_TYPE("a{sv}"))) {
        const gchar* operatorCode = nullptr;
        if (g_variant_lookup(gpp, "OperatorCode", "&s", &operatorCode) && operatorCode != nullptr) {
          reading.operator_code = operatorCode;
        }
        g_variant_unref(gpp);
      }
      modems.push_back(std::move(reading));
      g_variant_unref(modem);
    }
    g_variant_unref(interfaces);
  }
  g_variant_unref(objects);
  return modems;
}

// A bearer's data interface from its Properties.GetAll reply ("(a{sv})"), ""
// unless it is connected: Interface names it only while it is.
std::string ConnectedBearerInterface(GVariant* reply) {
  GVariant* properties = nullptr;
  g_variant_get(reply, "(@a{sv})", &properties);
  gboolean connected = FALSE;
  const gchar* iface = nullptr;
  std::string result;
  if (g_variant_lookup(properties, "Connected", "b", &connected) && connected &&
      g_variant_lookup(properties, "Interface", "&s", &iface) && iface != nullptr) {
    result = iface;
  }
  g_variant_unref(properties);
  return result;
}

}  // namespace

NetworkCountryWatcher::NetworkCountryWatcher(std::string excludedInterface, Listener listener)
    : excludedInterface_(std::move(excludedInterface)),
      listener_(std::move(listener)),
      box_(std::make_shared<NetworkCountryWatcher*>(this)) {}

NetworkCountryWatcher::~NetworkCountryWatcher() {
  // A reply still in flight finds a null box and touches nothing.
  *box_ = nullptr;
  if (tickId_ != 0) g_source_remove(tickId_);
  if (watchId_ != 0) g_bus_unwatch_name(watchId_);
  if (cancellable_ != nullptr) {
    g_cancellable_cancel(cancellable_);
    g_object_unref(cancellable_);
  }
  if (bus_ != nullptr) g_object_unref(bus_);
}

void NetworkCountryWatcher::Start() {
  if (watchId_ != 0) return;
  cancellable_ = g_cancellable_new();
  ReadDefaultRoute();
  // NONE, not AUTO_START: a ModemManager the user disabled stays stopped. The
  // vanished handler runs once at the start when it is not running, which is
  // the first reading on most machines.
  watchId_ = g_bus_watch_name(G_BUS_TYPE_SYSTEM, kModemManagerService,
                              G_BUS_NAME_WATCHER_FLAGS_NONE,
                              &NetworkCountryWatcher::OnModemManagerAppeared,
                              &NetworkCountryWatcher::OnModemManagerVanished, this, nullptr);
  tickId_ = g_timeout_add_seconds(kTickSeconds, &NetworkCountryWatcher::OnTick, this);
}

bool NetworkCountryWatcher::ReadDefaultRoute() {
  // IPv4 first, the route the daemon's own control-plane sockets take; an
  // IPv6-only network (a carrier's v6-only APN) has only the other one.
  std::string iface =
      LinuxDefaultRouteInterface(ReadProcText("/proc/net/route"), excludedInterface_);
  if (iface.empty()) {
    iface = LinuxDefaultRouteInterfaceV6(ReadProcText("/proc/net/ipv6_route"), excludedInterface_);
  }
  if (iface == defaultRouteInterface_) return false;
  defaultRouteInterface_ = std::move(iface);
  return true;
}

void NetworkCountryWatcher::OnModemManagerAppeared(GDBusConnection* connection, const gchar*,
                                                   const gchar*, gpointer data) {
  auto* self = static_cast<NetworkCountryWatcher*>(data);
  if (connection != nullptr && connection != self->bus_) {
    if (self->bus_ != nullptr) g_object_unref(self->bus_);
    self->bus_ = static_cast<GDBusConnection*>(g_object_ref(connection));
  }
  self->modemManagerRunning_ = true;
  self->modemManagerAnswered_ = false;
  self->ReadModems();
}

void NetworkCountryWatcher::OnModemManagerVanished(GDBusConnection*, const gchar*,
                                                   gpointer data) {
  auto* self = static_cast<NetworkCountryWatcher*>(data);
  self->modemManagerRunning_ = false;
  self->modemManagerAnswered_ = false;
  self->modems_.clear();
  // A reply still in flight describes a ModemManager that is gone.
  ++self->readGeneration_;
  self->readInFlight_ = false;
  self->readAgain_ = false;
  self->pendingModems_.clear();
  self->pendingBearers_ = 0;
  self->Decide();
}

gboolean NetworkCountryWatcher::OnTick(gpointer data) {
  auto* self = static_cast<NetworkCountryWatcher*>(data);
  if (self->ReadDefaultRoute()) {
    // A new path. With ModemManager running its modems are read again first,
    // since a bearer that just connected may be what carries it; the read
    // decides when it lands.
    if (self->modemManagerRunning_) {
      self->ReadModems();
    } else {
      self->Decide();
    }
  } else if (self->modemManagerRunning_ &&
             MonotonicSeconds() - self->lastReadMonotonicSeconds_ >= kModemReadIntervalSeconds) {
    // A registration moves on its own (crossing a border while roaming).
    self->ReadModems();
  }
  return G_SOURCE_CONTINUE;
}

void NetworkCountryWatcher::ReadModems() {
  if (!modemManagerRunning_ || bus_ == nullptr) return;
  if (readInFlight_) {
    readAgain_ = true;
    return;
  }
  readInFlight_ = true;
  lastReadMonotonicSeconds_ = MonotonicSeconds();
  // The modems and their properties, in one call. Bearers are not in the
  // object manager, so their interfaces are asked for separately below.
  g_dbus_connection_call(bus_, kModemManagerService, kModemManagerPath,
                         "org.freedesktop.DBus.ObjectManager", "GetManagedObjects", nullptr,
                         G_VARIANT_TYPE("(a{oa{sa{sv}}})"), G_DBUS_CALL_FLAGS_NO_AUTO_START,
                         kCallTimeoutMillis, cancellable_,
                         &NetworkCountryWatcher::OnManagedObjects,
                         new CallCtx{box_, ++readGeneration_, 0});
}

void NetworkCountryWatcher::OnManagedObjects(GObject* source, GAsyncResult* result,
                                             gpointer data) {
  std::unique_ptr<CallCtx> ctx(static_cast<CallCtx*>(data));
  GError* error = nullptr;
  GVariant* reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), result, &error);
  NetworkCountryWatcher* self = ctx->box ? *ctx->box : nullptr;
  if (self == nullptr || ctx->generation != self->readGeneration_) {
    if (reply != nullptr) g_variant_unref(reply);
    g_clear_error(&error);
    return;
  }
  if (reply == nullptr) {
    // Asked again once a minute; said once per distinct failure.
    const std::string message = error != nullptr ? error->message : "unknown error";
    if (message != self->lastReadError_) {
      DaemonLogf("[country] ModemManager did not list its modems: %s\n", message.c_str());
      self->lastReadError_ = message;
    }
    g_clear_error(&error);
    self->FinishRead(/*answered=*/false);
    return;
  }
  self->lastReadError_.clear();

  std::vector<std::pair<size_t, std::string>> bearers;
  self->pendingModems_ = ParseManagedObjects(reply, &bearers);
  g_variant_unref(reply);
  if (bearers.empty()) {
    self->FinishRead(/*answered=*/true);
    return;
  }
  self->pendingBearers_ = static_cast<int>(bearers.size());
  for (const auto& [modemIndex, bearerPath] : bearers) {
    g_dbus_connection_call(self->bus_, kModemManagerService, bearerPath.c_str(),
                           "org.freedesktop.DBus.Properties", "GetAll",
                           g_variant_new("(s)", kBearerInterface), G_VARIANT_TYPE("(a{sv})"),
                           G_DBUS_CALL_FLAGS_NO_AUTO_START, kCallTimeoutMillis,
                           self->cancellable_, &NetworkCountryWatcher::OnBearerProperties,
                           new CallCtx{self->box_, ctx->generation, modemIndex});
  }
}

void NetworkCountryWatcher::OnBearerProperties(GObject* source, GAsyncResult* result,
                                               gpointer data) {
  std::unique_ptr<CallCtx> ctx(static_cast<CallCtx*>(data));
  GError* error = nullptr;
  GVariant* reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), result, &error);
  NetworkCountryWatcher* self = ctx->box ? *ctx->box : nullptr;
  if (self == nullptr || ctx->generation != self->readGeneration_) {
    if (reply != nullptr) g_variant_unref(reply);
    g_clear_error(&error);
    return;
  }
  // A bearer that went away between the two reads carries nothing, which is
  // what an error here means.
  g_clear_error(&error);
  if (reply != nullptr) {
    std::string iface = ConnectedBearerInterface(reply);
    if (!iface.empty() && ctx->modem < self->pendingModems_.size()) {
      self->pendingModems_[ctx->modem].data_interfaces.push_back(std::move(iface));
    }
    g_variant_unref(reply);
  }
  if (--self->pendingBearers_ <= 0) self->FinishRead(/*answered=*/true);
}

void NetworkCountryWatcher::FinishRead(bool answered) {
  readInFlight_ = false;
  modemManagerAnswered_ = answered;
  modems_ = answered ? std::move(pendingModems_) : std::vector<ModemReading>();
  pendingModems_.clear();
  pendingBearers_ = 0;
  Decide();
  if (readAgain_) {
    readAgain_ = false;
    ReadModems();
  }
}

void NetworkCountryWatcher::Decide() {
  // A ModemManager that is running has not said anything until its first read
  // lands; deciding before it would announce a country it never reported.
  if (modemManagerRunning_ && !modemManagerAnswered_ && readInFlight_) return;
  const NetworkCountryReading next = ReadNetworkCountry(
      defaultRouteInterface_, modemManagerRunning_, modemManagerAnswered_, modems_);
  if (delivered_ && next == reading_) return;
  reading_ = next;
  delivered_ = true;
  DaemonLogf("[country] network country \"%s\" (%s, default route %s)\n",
             reading_.country_code.c_str(), ToString(reading_.source),
             reading_.interface_name.empty() ? "none" : reading_.interface_name.c_str());
  if (listener_) listener_(reading_);
}

}  // namespace urnw
