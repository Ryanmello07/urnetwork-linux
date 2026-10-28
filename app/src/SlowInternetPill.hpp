// The Connect page's slow-internet pill (apple/DELAY.md §1, §5.2; variant A+,
// §13 decision 8): a black rectangle with square corners, like the tunnel's,
// in a 1 px amber outline at 35%. The 18 px tunnel (TunnelIndicator), "Poor
// signal" in amber, or "Fallback" when the slow reading came from a fallback
// transport, then the upload and download numbers in tabular digits, the
// worse one amber; slow_internet::PillContent decides what it says. With too
// little room (a long translation, a narrow pane) it drops the better
// direction, then the numbers. It is a readout, not a button: nothing opens
// from it. One accessible element: the label, then the numbers.
//
// It follows the SDK's level as it comes, with no dwell (that is the
// notification's), and appears with the tunnel's first square. On a return
// to normal the squares fade out as a green check and "Internet back to
// normal" fade in; that holds 3 s, then the pill fades. The reading going
// away is no recovery: the pill fades once the tunnel's last square has
// started fading (tunnel::ContainerFadeOutDelayMs). With animations off
// everything lands at once. Where it sits is the page's decision
// (ConnectPage::ApplyFold): the right end of the rightmost pane's header.
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <cstdint>
#include <optional>

#include <gtkmm.h>

#include "SlowInternet.hpp"
#include "TunnelIndicator.hpp"

namespace urnw {

class SlowInternetPill : public Gtk::Widget {
 public:
  SlowInternetPill();
  ~SlowInternetPill() override;

  // The reading to show (MainWindow has gated it on the session already).
  // Moves with motion only while the page is on screen.
  void Apply(const slow_internet::Reading& reading);

 protected:
  // As wide as the widest line that fits the room its header leaves it
  // (HeaderRoom), and no narrower than its narrowest.
  void measure_vfunc(Gtk::Orientation orientation, int for_size, int& minimum, int& natural,
                     int& minimum_baseline, int& natural_baseline) const override;
  // The frame takes the width of the line that fits, at the right end: room
  // it cannot use stays beside the header's title, not inside the frame.
  void size_allocate_vfunc(int width, int height, int baseline) override;

 private:
  // The width its header leaves it once the title and the rest have their
  // natural widths. The pill asks for no more: more would widen a rail pane,
  // whose natural width sets its width, and cut the title short for a line
  // the pill cannot show.
  int HeaderRoom() const;
  // Reads the room again; a new one asks for a new width once the layout
  // pass that found it is done.
  void CheckRoom();
  // the copy's lines, the first that fits shown (SlowInternetPill.cpp)
  class Lines;
  // one direction: its muted arrow, then its number
  struct Number {
    Gtk::Box* root = nullptr;
    Gtk::Label* value = nullptr;
  };
  // the label, then the numbers that show
  struct Line {
    Gtk::Box* root = nullptr;
    Gtk::Label* label = nullptr;
    Number up;
    Number down;
  };
  static Line MakeLine();
  static void FillLine(Line& line, const slow_internet::PillContent& content);

  // Motion only while the page is on screen, and with animations on.
  bool Animates() const;
  // The words and the accessible name. A swap between the slow words and the
  // recovered ones fades the new ones in, the check with them, when animated.
  void Draw(const slow_internet::PillContent& content, bool animate);
  // It appears with the first square.
  void Appear(bool animate);
  // The squares fade out as the check and "Internet back to normal" fade in;
  // that holds, then the pill fades.
  void Recover(bool animate);
  // The squares fade out from the inside first; the pill follows once the
  // last of them has started, keeping its words.
  void Leave(bool animate);
  void FadeOut(int delayMs, bool animate);

  Gtk::Box* frame_ = nullptr;
  TunnelIndicator* tunnel_ = nullptr;
  Gtk::Image* check_ = nullptr;
  Lines* lines_ = nullptr;
  Line full_;
  Line worseOnly_;
  Line labelOnly_;
  Gtk::Label* recovered_ = nullptr;

  // HeaderRoom as last read, -1 out of a header. It moves with the header's
  // width and the others' words, so it is read again after each layout of
  // the window while the pill is on screen (GTK allocates a child that only
  // moved without telling it).
  int room_ = -1;
  sigc::connection roomResize_;
  sigc::connection layoutHook_;

  slow_internet::Reading reading_;
  bool applied_ = false;
  // what the pill shows; none while it is not up or is leaving
  std::optional<slow_internet::PillContent> shown_;
  // what it draws: it keeps its words while it fades out
  slow_internet::PillContent content_;
  // up (fading in or shown) rather than gone or on its way out
  bool up_ = false;
  // "Internet back to normal" holds, then the pill fades
  sigc::connection hold_;
  // bumped by every appearance and disappearance: a fade in flight from an
  // earlier one stops writing
  uint64_t fadeGeneration_ = 0;
  // the same for the swap between the slow words and the recovered ones
  uint64_t swapGeneration_ = 0;
};

}  // namespace urnw
