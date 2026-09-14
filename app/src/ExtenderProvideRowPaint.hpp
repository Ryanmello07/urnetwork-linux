// The GTK side of the provider extender row (EXTENDER.md N7), shared by the
// connect page's settings row and the earnings page's read-only row so the two
// paint one way: the dot in the provide glyph's palette, the state text in the
// catalog's words, and the one-line state label with its tooltip and error
// color. The reading itself is ExtenderProvidePresentation.hpp's. Each page
// keeps only its own part: the switch and its description on the connect page,
// the button's accessible label on the earnings page.
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <string>

#include <gtkmm.h>

#include "ExtenderProvidePresentation.hpp"
#include "I18n.hpp"
#include "Ui.hpp"

namespace urnw {

// The dot's color: grey is the muted text, green and red are the provide
// glyph's green and coral, yellow is its paused amber, so the extender row and
// the provide row above it never show two yellows.
inline const Rgba& ExtenderDotColor(extender::ProvideDot dot) {
  switch (dot) {
    case extender::ProvideDot::Grey: return kUrTextMuted;
    case extender::ProvideDot::Green: return kUrGreen;
    case extender::ProvideDot::Yellow: return kUrAmber;
    case extender::ProvideDot::Red: return kUrCoral;
  }
  return kUrTextMuted;
}

// The row's state text in the catalog's words.
inline std::string ExtenderStateText(const extender::ProvideRow& row) {
  return extender::StateTextFor(
      row, [](const char* key, const char* english) { return std::string(T_(key, english)); },
      [](const std::string& pattern, const std::string& argument) {
        return Format(pattern.c_str(), argument);
      });
}

// Paints a visible row's dot and state line and returns the state text: one
// line cut with an ellipsis, the whole text as the tooltip since a listen
// failure names every carrier, and the error color in the error state.
inline std::string PaintExtenderProvideRow(Gtk::Label& dot, Gtk::Label& state,
                                           const extender::ProvideRow& row) {
  const std::string text = ExtenderStateText(row);
  dot.set_markup("<span foreground='" + HexForMarkup(ExtenderDotColor(row.dot)) + "'>●</span>");
  state.set_text(text);
  state.set_tooltip_text(text);
  if (row.errorText) {
    state.add_css_class("ur-error-text");
  } else {
    state.remove_css_class("ur-error-text");
  }
  return text;
}

}  // namespace urnw
