// The crypto invoice's QR (mmm/ZCASH-CONTRACT.md §6.2 item 2): the server's payment URI
// encoded with the vendored qrcodegen (third_party/qrcodegen, the extender
// share code's encoder) into a square module bitmap, which
// CryptoInvoiceView.cpp draws ink on paper in a Gtk::DrawingArea at a whole
// number of pixels per module (crypto_pay::QrModulePixels).
//
// Level M, not the share code's H: a payment URI carries no centred glyph to
// cover, wallets scan M codes as a matter of course, and H would push a Zcash
// unified address from version 14 to about 19. qrcodegen raises the level on
// its own when that costs no version.
//
// The caller encodes only a URI urnet::checkCryptoPaymentUri accepted; this
// knows nothing about schemes. Header-only and free of GTK and the SDK, so
// tests/CryptoPayTest.cpp encodes the URIs the server builds.
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <cstddef>
#include <exception>
#include <string>
#include <vector>

#include <qrcodegen.hpp>

namespace urnw::crypto_pay {

// A row-major module bitmap, `count` modules on a side; empty (count 0) when
// the text could not be encoded at all.
struct QrModules {
  int count = 0;
  std::vector<bool> dark;

  bool Dark(int x, int y) const {
    return dark[static_cast<size_t>(y) * static_cast<size_t>(count) + static_cast<size_t>(x)];
  }
};

inline QrModules EncodePaymentQr(const std::string& uri) {
  QrModules modules;
  if (uri.empty()) return modules;
  try {
    const qrcodegen::QrCode qr =
        qrcodegen::QrCode::encodeText(uri.c_str(), qrcodegen::QrCode::Ecc::MEDIUM);
    const int size = qr.getSize();
    modules.dark.resize(static_cast<size_t>(size) * static_cast<size_t>(size));
    for (int y = 0; y < size; ++y) {
      for (int x = 0; x < size; ++x) {
        modules.dark[static_cast<size_t>(y) * static_cast<size_t>(size) +
                     static_cast<size_t>(x)] = qr.getModule(x, y);
      }
    }
    modules.count = size;
  } catch (const std::exception&) {
    // too long for version 40 (a URI is a few hundred bytes, so never in
    // practice): no code, and the address and amount carry the invoice
    modules = QrModules{};
  }
  return modules;
}

}  // namespace urnw::crypto_pay
