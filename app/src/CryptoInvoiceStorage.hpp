// Where the crypto invoice controller keeps the saved invoice and the last
// payment method on this device (CryptoInvoiceHost.cpp), and how a sign-out
// clears them.
//
// The SDK keeps both as 0600 files (.crypto_invoice, .payment_method) in the
// directory its NewCryptoInvoiceViewController is given, and the contract
// (mmm/ZCASH-CONTRACT.md §4.4) names the network space's local-state directory, the
// one a sign-out clears. This app's GUI holds no Device outside a tunnel
// session (SdkHost.cpp BindRemoteDeviceLocked), so the device's
// OpenCryptoInvoiceViewController is out, and the C ABI does not expose the
// space's local-state path. So the GUI gives the controller a directory of
// its own, beside the network spaces the SDK keeps under the same root, one
// per network server so a self-hosted server's invoice never meets the
// official one's, and clears it itself at every sign-out
// (CryptoInvoiceHost::SignOut), which is what the SDK's sign-out would have
// done to the local-state directory. The controller also refuses to resume an
// invoice saved for another network than the jwt's.
//
// Header-only, standard library only (tests/CryptoPayTest.cpp).
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace urnw::crypto_pay {

// The per-server directory name: the host name with anything but letters,
// digits, '.', '-' and '_' made '_', and "default" for an empty one (or one
// that would name the parent).
inline std::string StorageDirName(const std::string& hostName) {
  std::string name;
  for (const char c : hostName) {
    const bool keep = ('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z') || ('0' <= c && c <= '9') ||
                      c == '.' || c == '-' || c == '_';
    name += keep ? c : '_';
  }
  if (name.empty() || name == "." || name == "..") return "default";
  return name;
}

// <root>/crypto_invoice/<server>: root is the directory the SDK's network
// spaces live in (SdkHost::Initialize's storageDir).
inline std::string StorageDir(const std::string& root, const std::string& hostName) {
  if (root.empty()) return std::string();
  return (std::filesystem::path(root) / "crypto_invoice" / StorageDirName(hostName)).string();
}

// Creates the directory, owner-only (0700), as the SDK's local-state
// directory is. False when it could not be made; the controller then keeps
// the invoice in memory only (an empty directory).
inline bool EnsureStorageDir(const std::string& dir) {
  if (dir.empty()) return false;
  std::error_code error;
  std::filesystem::create_directories(dir, error);
  if (error) return false;
  std::filesystem::permissions(dir, std::filesystem::perms::owner_all,
                               std::filesystem::perm_options::replace, error);
  return !error && std::filesystem::is_directory(dir, error);
}

// The sign-out's clearing: every file in the directory goes (the saved
// invoice with its status token, the payment method, a write's leftover temp
// file), whatever the SDK names them. Subdirectories are left alone; there
// are none. Returns how many files were removed.
inline size_t WipeStorage(const std::string& dir) {
  if (dir.empty()) return 0;
  // listed first, removed after: no removal under a live iterator
  std::vector<std::filesystem::path> files;
  std::error_code error;
  for (std::filesystem::directory_iterator entries(dir, error), end; !error && entries != end;
       entries.increment(error)) {
    std::error_code entryError;
    if (entries->is_regular_file(entryError) && !entryError) files.push_back(entries->path());
  }
  size_t removed = 0;
  for (const std::filesystem::path& file : files) {
    std::error_code removeError;
    if (std::filesystem::remove(file, removeError) && !removeError) ++removed;
  }
  return removed;
}

}  // namespace urnw::crypto_pay
