// The plan picker's two cards (PlanPicker.cpp) are a radio pair the keyboard
// and a screen reader can use. The yearly card was an overlay with a click
// gesture, which Tab never reached, so after picking the monthly plan the
// keyboard could not pick the yearly one back; and neither card had a name,
// since a card's content is a box. The cards need GTK, so this reads the
// picker's source and the onboarding css.
// SPDX-License-Identifier: MPL-2.0
#include "TestHarness.hpp"

#include <fstream>
#include <initializer_list>
#include <sstream>
#include <string>

#ifndef UR_SRC_DIR
#define UR_SRC_DIR ""
#endif

namespace {

std::string ReadPlanSource(const std::string& relative) {
  std::ifstream in(std::string(UR_SRC_DIR) + "/" + relative, std::ios::binary);
  std::stringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

// From `start` to the first `end` after it; empty when either is gone.
std::string PlanSpan(const std::string& source, const std::string& start,
                     const std::string& end) {
  const size_t from = source.find(start);
  if (from == std::string::npos) return std::string();
  const size_t to = source.find(end, from + start.size());
  if (to == std::string::npos) return std::string();
  return source.substr(from, to - from);
}

bool PlanHas(const std::string& text, const std::string& needle) {
  if (text.find(needle) != std::string::npos) return true;
  UR_FAIL("missing: " + needle);
  return false;
}

}  // namespace

// The yearly card is in the tab order, a radio, and Enter or Space picks it.
UR_TEST(PlanPicker_TheYearlyCardTakesTheKeyboard) {
  const std::string card =
      PlanSpan(ReadPlanSource("PlanPicker.cpp"), "class GoldPlanCard", "\n};\n");
  UR_EXPECT_TRUE(!card.empty());
  PlanHas(card, "g_object_set(gobj(), \"accessible-role\", GTK_ACCESSIBLE_ROLE_RADIO, nullptr);");
  PlanHas(card, "set_focusable(true);");
  PlanHas(card, "Gtk::EventControllerKey::create()");
  const std::string keys = PlanSpan(card, "signal_key_pressed()", "add_controller(keys);");
  for (const char* key :
       {"GDK_KEY_Return", "GDK_KEY_KP_Enter", "GDK_KEY_space", "GDK_KEY_KP_Space"}) {
    PlanHas(keys, key);
  }
  PlanHas(keys, "if (on_select) on_select();");
  PlanHas(card, "SetPlanCardChecked(*this, selected);");
  PlanHas(card, "kit::SetAccessibleLabel(*this, PlanCardName(");
}

// The monthly card is a radio too, named by its lines and checked when it is
// the selection.
UR_TEST(PlanPicker_TheMonthlyCardIsANamedRadio) {
  const std::string source = ReadPlanSource("PlanPicker.cpp");
  PlanHas(source, "g_object_new(GTK_TYPE_BUTTON, \"accessible-role\", GTK_ACCESSIBLE_ROLE_RADIO");
  PlanHas(source,
          "kit::SetAccessibleLabel(*monthlyCard_, PlanCardName({texts.monthlyPrice, "
          "texts.monthlyLine}));");
  PlanHas(source, "SetPlanCardChecked(*monthlyCard_, !yearly_);");
  PlanHas(source, "GTK_ACCESSIBLE_STATE_CHECKED");
}

// The yearly card draws itself, so its focus ring comes from the css.
UR_TEST(PlanPicker_TheYearlyCardShowsFocus) {
  PlanHas(ReadPlanSource("PlanPicker.cpp"), "add_css_class(\"ur-onb-gold-card\");");
  PlanHas(ReadPlanSource("ReferralPanel.cpp"),
          ".ur-onb-gold-card:focus-visible { outline: 2px solid");
}
