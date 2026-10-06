// The session bus side of the handover. The process keeps one connection,
// GApplication's shared session connection, from its launch to its end: the
// handover object and kExitingName live on it, and holding it until the
// process ends is what makes kExitingName last exactly as long as the process
// (the bus drops a connection's names when its socket closes, including at a
// crash).
//
// A launch's waits run on a main context of their own, so the default one,
// where the handover object answers, is not iterated before the instance has
// started.
//
// SPDX-License-Identifier: MPL-2.0
#include "SingleInstance.hpp"

#include <glib.h>

#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace urnw::instance {
namespace {

// org.freedesktop.DBus RequestName: the flag and the replies the claims use.
constexpr guint32 kDoNotQueue = 4;
constexpr guint32 kPrimaryOwner = 1;
constexpr guint32 kExists = 3;
constexpr guint32 kAlreadyOwner = 4;

constexpr const char* kHandoverXml = R"XML(
<node>
  <interface name="com.bringyour.network.Instance">
    <method name="Handover">
      <arg name="kind" type="s" direction="in"/>
      <arg name="uris" type="as" direction="in"/>
      <arg name="activation_token" type="s" direction="in"/>
      <arg name="answer" type="s" direction="out"/>
    </method>
  </interface>
</node>)XML";

// The process's side of the handover. Never destroyed: a dispatch can still
// reach the gate while statics are destroyed, and the connection must stay
// open until the process ends.
struct Instance {
  GDBusConnection* bus = nullptr;
  guint objectId = 0;
  Gate gate;
};

Instance& TheInstance() {
  static Instance* instance = new Instance();
  return *instance;
}

// GtkApplication's platform data for a startup notification id
// (gtk_application_add_platform_data), which before_emit reads.
GVariant* PlatformData(const std::string& activationToken) {
  GVariantBuilder builder;
  g_variant_builder_init(&builder, G_VARIANT_TYPE_VARDICT);
  if (!activationToken.empty()) {
    g_variant_builder_add(&builder, "{sv}", "activation-token",
                          g_variant_new_string(activationToken.c_str()));
    g_variant_builder_add(&builder, "{sv}", "desktop-startup-id",
                          g_variant_new_string(activationToken.c_str()));
  }
  return g_variant_builder_end(&builder);
}

GVariant* UrisVariant(const Launch& launch) {
  GVariantBuilder builder;
  g_variant_builder_init(&builder, G_VARIANT_TYPE_STRING_ARRAY);
  for (const std::string& uri : launch.uris) g_variant_builder_add(&builder, "s", uri.c_str());
  return g_variant_builder_end(&builder);
}

// A failed handover call, by what it says about the holder. Consumes error.
Handed HandedFromError(GError* error, const char* call) {
  Handed handed = Handed::Failed;
  if (g_error_matches(error, G_IO_ERROR, G_IO_ERROR_TIMED_OUT) ||
      g_error_matches(error, G_DBUS_ERROR, G_DBUS_ERROR_TIMEOUT) ||
      g_error_matches(error, G_DBUS_ERROR, G_DBUS_ERROR_TIMED_OUT)) {
    handed = Handed::TimedOut;
  } else if (g_error_matches(error, G_DBUS_ERROR, G_DBUS_ERROR_SERVICE_UNKNOWN) ||
             g_error_matches(error, G_DBUS_ERROR, G_DBUS_ERROR_NAME_HAS_NO_OWNER) ||
             g_error_matches(error, G_DBUS_ERROR, G_DBUS_ERROR_NO_REPLY) ||
             g_error_matches(error, G_DBUS_ERROR, G_DBUS_ERROR_DISCONNECTED)) {
    handed = Handed::Gone;
  } else if (g_error_matches(error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD) ||
             g_error_matches(error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_OBJECT) ||
             g_error_matches(error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_INTERFACE)) {
    handed = Handed::Unsupported;
  } else {
    g_warning("launch: %s failed: %s", call, error ? error->message : "no detail");
  }
  g_clear_error(&error);
  return handed;
}

Claim ClaimOn(GDBusConnection* bus) {
  GError* error = nullptr;
  GVariant* reply = g_dbus_connection_call_sync(
      bus, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "RequestName",
      g_variant_new("(su)", kBusName, kDoNotQueue), G_VARIANT_TYPE("(u)"),
      G_DBUS_CALL_FLAGS_NONE, -1, nullptr, &error);
  if (!reply) {
    g_warning("launch: claiming %s failed: %s", kBusName, error ? error->message : "no detail");
    g_clear_error(&error);
    return Claim::Failed;
  }
  guint32 code = 0;
  g_variant_get(reply, "(u)", &code);
  g_variant_unref(reply);
  if (code == kPrimaryOwner || code == kAlreadyOwner) return Claim::Primary;
  if (code == kExists) return Claim::Held;
  g_warning("launch: claiming %s answered %u", kBusName, code);
  return Claim::Failed;
}

std::optional<std::string> OwnerOn(GDBusConnection* bus, const std::string& name) {
  GError* error = nullptr;
  GVariant* reply = g_dbus_connection_call_sync(
      bus, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "GetNameOwner",
      g_variant_new("(s)", name.c_str()), G_VARIANT_TYPE("(s)"), G_DBUS_CALL_FLAGS_NONE, -1,
      nullptr, &error);
  if (!reply) {
    const bool unowned = g_error_matches(error, G_DBUS_ERROR, G_DBUS_ERROR_NAME_HAS_NO_OWNER);
    if (!unowned) {
      g_warning("launch: looking up %s failed: %s", name.c_str(),
                error ? error->message : "no detail");
    }
    g_clear_error(&error);
    if (unowned) return std::string();
    return std::nullopt;
  }
  const gchar* owner = nullptr;
  g_variant_get(reply, "(&s)", &owner);
  std::string result = owner ? owner : "";
  g_variant_unref(reply);
  return result;
}

Handed HandoverOn(GDBusConnection* bus, const std::string& holder, const Launch& launch,
                  int budgetMillis) {
  GError* error = nullptr;
  GVariant* reply = g_dbus_connection_call_sync(
      bus, holder.c_str(), kObjectPath, kInterface, "Handover",
      g_variant_new("(s@ass)", ToWire(launch.kind), UrisVariant(launch),
                    launch.activationToken.c_str()),
      G_VARIANT_TYPE("(s)"), G_DBUS_CALL_FLAGS_NO_AUTO_START, budgetMillis, nullptr, &error);
  if (!reply) return HandedFromError(error, "the handover");
  const gchar* wire = nullptr;
  g_variant_get(reply, "(&s)", &wire);
  const std::optional<Answer> answer = AnswerFromWire(wire ? wire : "");
  g_variant_unref(reply);
  if (!answer) return Handed::Failed;
  return *answer == Answer::Served ? Handed::Served : Handed::Refused;
}

// org.freedesktop.Application on GApplication's object: what a remote sends,
// with a reply.
Handed LegacyHandoverOn(GDBusConnection* bus, const std::string& holder, const Launch& launch,
                        int budgetMillis) {
  GVariant* platform = PlatformData(launch.activationToken);
  const bool open = !launch.uris.empty();
  GVariant* parameters = open ? g_variant_new("(@as@a{sv})", UrisVariant(launch), platform)
                              : g_variant_new("(@a{sv})", platform);
  GError* error = nullptr;
  GVariant* reply = g_dbus_connection_call_sync(
      bus, holder.c_str(), kApplicationPath, "org.freedesktop.Application",
      open ? "Open" : "Activate", parameters, nullptr, G_DBUS_CALL_FLAGS_NO_AUTO_START,
      budgetMillis, nullptr, &error);
  if (!reply) return HandedFromError(error, "the activation");
  g_variant_unref(reply);
  return Handed::Served;
}

// What a wait is told by NameOwnerChanged; shared with the subscription until
// its destroy notify runs.
struct NameWait {
  bool gone = false;
};

Wait AwaitGoneOn(GDBusConnection* bus, const std::string& name, int budgetMillis) {
  GMainContext* context = g_main_context_new();
  g_main_context_push_thread_default(context);
  auto wait = std::make_shared<NameWait>();
  const guint subscription = g_dbus_connection_signal_subscribe(
      bus, "org.freedesktop.DBus", "org.freedesktop.DBus", "NameOwnerChanged",
      "/org/freedesktop/DBus", name.c_str(), G_DBUS_SIGNAL_FLAGS_NONE,
      [](GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar*,
         GVariant* parameters, gpointer data) {
        const gchar* newOwner = nullptr;
        g_variant_get(parameters, "(&s&s&s)", nullptr, nullptr, &newOwner);
        if (newOwner && *newOwner == '\0') {
          (*static_cast<std::shared_ptr<NameWait>*>(data))->gone = true;
        }
      },
      new std::shared_ptr<NameWait>(wait),
      [](gpointer data) { delete static_cast<std::shared_ptr<NameWait>*>(data); });
  // looked up after the subscription, so a change from here on is delivered
  const std::optional<std::string> owner = OwnerOn(bus, name);
  Wait result = Wait::Failed;
  if (owner && owner->empty()) {
    result = Wait::Gone;
  } else if (owner) {
    bool expired = false;
    GSource* timeout = g_timeout_source_new(static_cast<guint>(budgetMillis));
    g_source_set_callback(
        timeout,
        [](gpointer data) -> gboolean {
          *static_cast<bool*>(data) = true;
          return G_SOURCE_REMOVE;
        },
        &expired, nullptr);
    g_source_attach(timeout, context);
    while (!wait->gone && !expired) g_main_context_iteration(context, TRUE);
    g_source_destroy(timeout);
    g_source_unref(timeout);
    result = wait->gone ? Wait::Gone : Wait::TimedOut;
  }
  g_dbus_connection_signal_unsubscribe(bus, subscription);
  // the subscription's last callbacks and its destroy notify run here
  while (g_main_context_pending(context)) g_main_context_iteration(context, FALSE);
  g_main_context_pop_thread_default(context);
  g_main_context_unref(context);
  return result;
}

Bus SessionBus(GDBusConnection* connection) {
  Bus bus;
  bus.claim = [connection] { return ClaimOn(connection); };
  bus.owner = [connection](const std::string& name) { return OwnerOn(connection, name); };
  bus.handover = [connection](const std::string& holder, const Launch& launch, int budgetMillis) {
    return HandoverOn(connection, holder, launch, budgetMillis);
  };
  bus.handoverLegacy = [connection](const std::string& holder, const Launch& launch,
                                    int budgetMillis) {
    return LegacyHandoverOn(connection, holder, launch, budgetMillis);
  };
  bus.awaitGone = [connection](const std::string& name, int budgetMillis) {
    return AwaitGoneOn(connection, name, budgetMillis);
  };
  return bus;
}

void OnHandoverCall(GDBusConnection*, const gchar*, const gchar*, const gchar*,
                    const gchar* method, GVariant* parameters, GDBusMethodInvocation* invocation,
                    gpointer) {
  if (g_strcmp0(method, "Handover") != 0) {
    g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD,
                                          "no such method");
    return;
  }
  const gchar* kind = nullptr;
  GVariantIter* uris = nullptr;
  const gchar* token = nullptr;
  g_variant_get(parameters, "(&sas&s)", &kind, &uris, &token);
  Launch launch;
  // a kind from a newer launch shows the window
  launch.kind = LaunchKindFromWire(kind ? kind : "").value_or(LaunchKind::Open);
  const gchar* uri = nullptr;
  while (g_variant_iter_next(uris, "&s", &uri)) launch.uris.emplace_back(uri);
  g_variant_iter_free(uris);
  launch.activationToken = token ? token : "";
  // the links are never logged: a wallet callback carries the address and its signature
  g_message("launch: a launch reached this instance (%s)", ToWire(launch.kind));
  TheInstance().gate.Take(std::move(launch), [invocation](Answer answer) {
    g_dbus_method_invocation_return_value(invocation, g_variant_new("(s)", ToWire(answer)));
  });
}

}  // namespace

Launch LaunchFor(GApplication* application, const Arguments& arguments) {
  Launch launch;
  launch.kind = arguments.kind;
  for (const std::string& path : arguments.paths) {
    GFile* file = g_file_new_for_commandline_arg(path.c_str());
    gchar* uri = g_file_get_uri(file);
    launch.uris.emplace_back(uri ? uri : "");
    g_free(uri);
    g_object_unref(file);
  }
  // the id GDK took from the environment when it loaded, which only
  // GtkApplication can read
  GVariantBuilder builder;
  g_variant_builder_init(&builder, G_VARIANT_TYPE_VARDICT);
  G_APPLICATION_GET_CLASS(application)->add_platform_data(application, &builder);
  GVariant* platform = g_variant_ref_sink(g_variant_builder_end(&builder));
  const gchar* token = nullptr;
  if (g_variant_lookup(platform, "activation-token", "&s", &token) ||
      g_variant_lookup(platform, "desktop-startup-id", "&s", &token)) {
    launch.activationToken = token ? token : "";
  }
  g_variant_unref(platform);
  return launch;
}

Outcome LaunchOnSessionBus(const Launch& launch) {
  Instance& instance = TheInstance();
  GError* error = nullptr;
  instance.bus = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &error);
  if (!instance.bus) {
    // GApplication runs without uniqueness then too
    g_message("launch: no session bus (%s); starting without a single instance",
              error ? error->message : "no detail");
    g_clear_error(&error);
    return Outcome::Start;
  }
  // the handover object before the claim: an instance answers from the moment
  // it holds the name
  static const GDBusInterfaceVTable kVtable = {OnHandoverCall, nullptr, nullptr, {nullptr}};
  GDBusNodeInfo* info = g_dbus_node_info_new_for_xml(kHandoverXml, nullptr);
  if (info) {
    instance.objectId = g_dbus_connection_register_object(instance.bus, kObjectPath,
                                                          info->interfaces[0], &kVtable, nullptr,
                                                          nullptr, &error);
    g_dbus_node_info_unref(info);
  }
  if (instance.objectId == 0) {
    g_warning("launch: the handover object could not be registered: %s",
              error ? error->message : "no detail");
    g_clear_error(&error);
    return Outcome::Fallback;
  }
  const Outcome outcome = RunLaunch(SessionBus(instance.bus), launch);
  if (outcome != Outcome::Start) {
    g_dbus_connection_unregister_object(instance.bus, instance.objectId);
    instance.objectId = 0;
  }
  return outcome;
}

void OpenLaunches(GApplication* application, Gate::Serve serve) {
  TheInstance().gate.Open([application, serve = std::move(serve)](const Launch& launch) {
    GVariant* platform = g_variant_ref_sink(PlatformData(launch.activationToken));
    GApplicationClass* applicationClass = G_APPLICATION_GET_CLASS(application);
    applicationClass->before_emit(application, platform);
    serve(launch);
    applicationClass->after_emit(application, platform);
    g_variant_unref(platform);
  });
}

void BeginExiting() {
  Instance& instance = TheInstance();
  instance.gate.Close([&instance] {
    if (!instance.bus) instance.bus = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, nullptr);
    if (!instance.bus) return;
    // queued behind an instance that still owns it, if any: a launch waits
    // until no connection owns it
    GError* error = nullptr;
    GVariant* reply = g_dbus_connection_call_sync(
        instance.bus, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",
        "RequestName", g_variant_new("(su)", kExitingName, 0u), G_VARIANT_TYPE("(u)"),
        G_DBUS_CALL_FLAGS_NONE, -1, nullptr, &error);
    if (!reply) {
      g_warning("launch: %s could not be claimed: %s", kExitingName,
                error ? error->message : "no detail");
      g_clear_error(&error);
      return;
    }
    g_variant_unref(reply);
    g_message("launch: this instance is exiting; launches are refused until it has ended");
  });
}

}  // namespace urnw::instance
