// The Material Design Icons glyphs this app draws, as their SVG path data on
// MDI's 24x24 viewBox: Material Design Icons 7.4.47 by Pictogrammers,
// https://pictogrammers.com, under the Apache License 2.0. Every glyph is one
// path filled in the current colour; BrandIcons.cpp reads it with
// GlyphPath.hpp and fills it with Cairo, so no glyph depends on an icon theme
// (a themed icon a theme lacks renders as a blank, BrandIcons.hpp).
//
//   - Account -> Sessions: the row's face profile (MDI head-outline) and the
//     session rows' device logos, REVOKE-UI-FINAL.md §8, verbatim;
//   - the Account pane's row icons (AccountPage.cpp), each the MDI glyph
//     closest to the meaning of its row (Android's and Apple's icon where those
//     apps have the row): Profile's account circle, Referrals' heart;
//   - the Sessions page's refresh and copy commands.
//
// Pure C++17: tests/GlyphPathTest.cpp reads every path here.
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <cstddef>

namespace urnw {

enum class MdiGlyph {
  // REVOKE-UI-FINAL.md §8
  SessionFace,
  DeviceAndroid,
  DeviceApple,
  DeviceWindows,
  DeviceLinux,
  DeviceWeb,
  DeviceCli,
  DeviceServer,
  DeviceUnknown,
  // the Account pane's rows
  AccountCircle,  // Profile (the network name)
  LockReset,      // Update password
  Key,            // a login method
  Login,          // Login methods (Add)
  Barcode,        // Auth code (the login stack's auth-code glyph is a barcode too)
  Identifier,     // Client ID
  Heart,          // Referrals
  Logout,         // Sign out
  Delete,         // Delete account
  // the Sessions page's commands
  Refresh,
  Copy,
};

struct MdiGlyphData {
  MdiGlyph glyph;
  // its path data, verbatim; ahead of the name, so a row never reads like a
  // {"key", "English"} lookup to tests/CatalogLookupTest.cpp
  const char* path;
  const char* mdiName;  // the glyph's name in the MDI set
};

// One row per MdiGlyph, in declaration order.
inline constexpr MdiGlyphData kMdiGlyphs[] = {
    // session-face-profile (head-outline)
    {MdiGlyph::SessionFace,
     "M13 1C8.4 1 4.6 4.4 4.1 8.9L2.5 11C2 11.8 1.9 12.8 2.3 13.6C2.7 14.3 3.3 14.8 4 14.9V16"
     "C4 17.8 5.3 19.4 7 19.9V23H18V17.5C20.5 15.8 22 13.1 22 10C22 5 18 1 13 1M16 16.3V21H9"
     "V18H8C6.9 18 6 17.1 6 16V13H4.5C4.1 13 3.8 12.5 4.1 12.2L6 9.7C6.2 6 9.2 3 13 3C16.9 3 "
     "20 6.1 20 10C20 12.8 18.4 15.2 16 16.3Z",
     "head-outline"},
    // device-android (android)
    {MdiGlyph::DeviceAndroid,
     "M16.61 15.15C16.15 15.15 15.77 14.78 15.77 14.32S16.15 13.5 16.61 13.5H16.61C17.07 13.5 "
     "17.45 13.86 17.45 14.32C17.45 14.78 17.07 15.15 16.61 15.15M7.41 15.15C6.95 15.15 6.57 "
     "14.78 6.57 14.32C6.57 13.86 6.95 13.5 7.41 13.5H7.41C7.87 13.5 8.24 13.86 8.24 14.32"
     "C8.24 14.78 7.87 15.15 7.41 15.15M16.91 10.14L18.58 7.26C18.67 7.09 18.61 6.88 18.45 "
     "6.79C18.28 6.69 18.07 6.75 18 6.92L16.29 9.83C14.95 9.22 13.5 8.9 12 8.91C10.47 8.91 9 "
     "9.24 7.73 9.82L6.04 6.91C5.95 6.74 5.74 6.68 5.57 6.78C5.4 6.87 5.35 7.08 5.44 7.25L7.1 "
     "10.13C4.25 11.69 2.29 14.58 2 18H22C21.72 14.59 19.77 11.7 16.91 10.14H16.91Z",
     "android"},
    // device-apple (apple)
    {MdiGlyph::DeviceApple,
     "M18.71,19.5C17.88,20.74 17,21.95 15.66,21.97C14.32,22 13.89,21.18 12.37,21.18C10.84,"
     "21.18 10.37,21.95 9.1,22C7.79,22.05 6.8,20.68 5.96,19.47C4.25,17 2.94,12.45 4.7,9.39"
     "C5.57,7.87 7.13,6.91 8.82,6.88C10.1,6.86 11.32,7.75 12.11,7.75C12.89,7.75 14.37,6.68 "
     "15.92,6.84C16.57,6.87 18.39,7.1 19.56,8.82C19.47,8.88 17.39,10.1 17.41,12.63C17.44,"
     "15.65 20.06,16.66 20.09,16.67C20.06,16.74 19.67,18.11 18.71,19.5M13,3.5C13.73,2.67 "
     "14.94,2.04 15.94,2C16.07,3.17 15.6,4.35 14.9,5.19C14.21,6.04 13.07,6.7 11.95,6.61C11.8,"
     "5.46 12.36,4.26 13,3.5Z",
     "apple"},
    // device-windows (microsoft-windows)
    {MdiGlyph::DeviceWindows,
     "M3,12V6.75L9,5.43V11.91L3,12M20,3V11.75L10,11.9V5.21L20,3M3,13L9,13.09V19.9L3,18.75V13"
     "M20,13.25V22L10,20.09V13.1L20,13.25Z",
     "microsoft-windows"},
    // device-linux (linux)
    {MdiGlyph::DeviceLinux,
     "M14.62,8.35C14.2,8.63 12.87,9.39 12.67,9.54C12.28,9.85 11.92,9.83 11.53,9.53C11.33,9.37 "
     "10,8.61 9.58,8.34C9.1,8.03 9.13,7.64 9.66,7.42C11.3,6.73 12.94,6.78 14.57,7.45C15.06,"
     "7.66 15.08,8.05 14.62,8.35M21.84,15.63C20.91,13.54 19.64,11.64 18,9.97C17.47,9.42 17.14,"
     "8.8 16.94,8.09C16.84,7.76 16.77,7.42 16.7,7.08C16.5,6.2 16.41,5.3 16,4.47C15.27,2.89 14,"
     "2.07 12.16,2C10.35,2.05 9,2.81 8.21,4.4C8,4.83 7.85,5.28 7.75,5.74C7.58,6.5 7.43,7.29 "
     "7.25,8.06C7.1,8.71 6.8,9.27 6.29,9.77C4.68,11.34 3.39,13.14 2.41,15.12C2.27,15.41 2.13,"
     "15.7 2.04,16C1.85,16.66 2.33,17.12 3.03,16.96C3.47,16.87 3.91,16.78 4.33,16.65C4.74,"
     "16.5 4.9,16.6 5,17C5.65,19.15 7.07,20.66 9.24,21.5C13.36,23.06 18.17,20.84 19.21,16.92"
     "C19.28,16.65 19.38,16.55 19.68,16.65C20.14,16.79 20.61,16.89 21.08,17C21.57,17.09 21.93,"
     "16.84 22,16.36C22.03,16.1 21.94,15.87 21.84,15.63",
     "linux"},
    // device-web (web)
    {MdiGlyph::DeviceWeb,
     "M16.36,14C16.44,13.34 16.5,12.68 16.5,12C16.5,11.32 16.44,10.66 16.36,10H19.74C19.9,"
     "10.64 20,11.31 20,12C20,12.69 19.9,13.36 19.74,14M14.59,19.56C15.19,18.45 15.65,17.25 "
     "15.97,16H18.92C17.96,17.65 16.43,18.93 14.59,19.56M14.34,14H9.66C9.56,13.34 9.5,12.68 "
     "9.5,12C9.5,11.32 9.56,10.65 9.66,10H14.34C14.43,10.65 14.5,11.32 14.5,12C14.5,12.68 "
     "14.43,13.34 14.34,14M12,19.96C11.17,18.76 10.5,17.43 10.09,16H13.91C13.5,17.43 12.83,"
     "18.76 12,19.96M8,8H5.08C6.03,6.34 7.57,5.06 9.4,4.44C8.8,5.55 8.35,6.75 8,8M5.08,16H8"
     "C8.35,17.25 8.8,18.45 9.4,19.56C7.57,18.93 6.03,17.65 5.08,16M4.26,14C4.1,13.36 4,12.69 "
     "4,12C4,11.31 4.1,10.64 4.26,10H7.64C7.56,10.66 7.5,11.32 7.5,12C7.5,12.68 7.56,13.34 "
     "7.64,14M12,4.03C12.83,5.23 13.5,6.57 13.91,8H10.09C10.5,6.57 11.17,5.23 12,4.03M18.92,8"
     "H15.97C15.65,6.75 15.19,5.55 14.59,4.44C16.43,5.07 17.96,6.34 18.92,8M12,2C6.47,2 2,6.5 "
     "2,12A10,10 0 0,0 12,22A10,10 0 0,0 22,12A10,10 0 0,0 12,2Z",
     "web"},
    // device-cli (console)
    {MdiGlyph::DeviceCli,
     "M20,19V7H4V19H20M20,3A2,2 0 0,1 22,5V19A2,2 0 0,1 20,21H4A2,2 0 0,1 2,19V5C2,3.89 2.9,3 "
     "4,3H20M13,17V15H18V17H13M9.58,13L5.57,9H8.4L11.7,12.3C12.09,12.69 12.09,13.33 11.7,13.72"
     "L8.42,17H5.59L9.58,13Z",
     "console"},
    // device-server (server)
    {MdiGlyph::DeviceServer,
     "M4,1H20A1,1 0 0,1 21,2V6A1,1 0 0,1 20,7H4A1,1 0 0,1 3,6V2A1,1 0 0,1 4,1M4,9H20A1,1 0 0,"
     "1 21,10V14A1,1 0 0,1 20,15H4A1,1 0 0,1 3,14V10A1,1 0 0,1 4,9M4,17H20A1,1 0 0,1 21,18V22"
     "A1,1 0 0,1 20,23H4A1,1 0 0,1 3,22V18A1,1 0 0,1 4,17M9,5H10V3H9V5M9,13H10V11H9V13M9,21H10"
     "V19H9V21M5,3V5H7V3H5M5,11V13H7V11H5M5,19V21H7V19H5Z",
     "server"},
    // device-unknown (help)
    {MdiGlyph::DeviceUnknown,
     "M10,19H13V22H10V19M12,2C17.35,2.22 19.68,7.62 16.5,11.67C15.67,12.67 14.33,13.33 13.67,"
     "14.17C13,15 13,16 13,17H10C10,15.33 10,13.92 10.67,12.92C11.33,11.92 12.67,11.33 13.5,"
     "10.67C15.92,8.43 15.32,5.26 12,5A3,3 0 0,0 9,8H6A6,6 0 0,1 12,2Z",
     "help"},
    // account-circle-outline
    {MdiGlyph::AccountCircle,
     "M12,2A10,10 0 0,0 2,12A10,10 0 0,0 12,22A10,10 0 0,0 22,12A10,10 0 0,0 12,2M7.07,18.28"
     "C7.5,17.38 10.12,16.5 12,16.5C13.88,16.5 16.5,17.38 16.93,18.28C15.57,19.36 13.86,20 12,"
     "20C10.14,20 8.43,19.36 7.07,18.28M18.36,16.83C16.93,15.09 13.46,14.5 12,14.5C10.54,14.5 "
     "7.07,15.09 5.64,16.83C4.62,15.5 4,13.82 4,12C4,7.59 7.59,4 12,4C16.41,4 20,7.59 20,12"
     "C20,13.82 19.38,15.5 18.36,16.83M12,6C10.06,6 8.5,7.56 8.5,9.5C8.5,11.44 10.06,13 12,13"
     "C13.94,13 15.5,11.44 15.5,9.5C15.5,7.56 13.94,6 12,6M12,11A1.5,1.5 0 0,1 10.5,9.5A1.5,"
     "1.5 0 0,1 12,8A1.5,1.5 0 0,1 13.5,9.5A1.5,1.5 0 0,1 12,11Z",
     "account-circle-outline"},
    // lock-reset
    {MdiGlyph::LockReset,
     "M12.63,2C18.16,2 22.64,6.5 22.64,12C22.64,17.5 18.16,22 12.63,22C9.12,22 6.05,20.18 "
     "4.26,17.43L5.84,16.18C7.25,18.47 9.76,20 12.64,20A8,8 0 0,0 20.64,12A8,8 0 0,0 12.64,4"
     "C8.56,4 5.2,7.06 4.71,11H7.47L3.73,14.73L0,11H2.69C3.19,5.95 7.45,2 12.63,2M15.59,10.24"
     "C16.09,10.25 16.5,10.65 16.5,11.16V15.77C16.5,16.27 16.09,16.69 15.58,16.69H10.05C9.54,"
     "16.69 9.13,16.27 9.13,15.77V11.16C9.13,10.65 9.54,10.25 10.04,10.24V9.23C10.04,7.7 "
     "11.29,6.46 12.81,6.46C14.34,6.46 15.59,7.7 15.59,9.23V10.24M12.81,7.86C12.06,7.86 11.44,"
     "8.47 11.44,9.23V10.24H14.19V9.23C14.19,8.47 13.57,7.86 12.81,7.86Z",
     "lock-reset"},
    // key-outline
    {MdiGlyph::Key,
     "M21 18H15V15H13.3C12.2 17.4 9.7 19 7 19C3.1 19 0 15.9 0 12S3.1 5 7 5C9.7 5 12.2 6.6 "
     "13.3 9H24V15H21V18M17 16H19V13H22V11H11.9L11.7 10.3C11 8.3 9.1 7 7 7C4.2 7 2 9.2 2 12"
     "S4.2 17 7 17C9.1 17 11 15.7 11.7 13.7L11.9 13H17V16M7 15C5.3 15 4 13.7 4 12S5.3 9 7 9 "
     "10 10.3 10 12 8.7 15 7 15M7 11C6.4 11 6 11.4 6 12S6.4 13 7 13 8 12.6 8 12 7.6 11 7 11Z",
     "key-outline"},
    // login
    {MdiGlyph::Login,
     "M11 7L9.6 8.4L12.2 11H2V13H12.2L9.6 15.6L11 17L16 12L11 7M20 19H12V21H20C21.1 21 22 "
     "20.1 22 19V5C22 3.9 21.1 3 20 3H12V5H20V19Z",
     "login"},
    // barcode
    {MdiGlyph::Barcode,
     "M2,6H4V18H2V6M5,6H6V18H5V6M7,6H10V18H7V6M11,6H12V18H11V6M14,6H16V18H14V6M17,6H20V18H17V6"
     "M21,6H22V18H21V6Z",
     "barcode"},
    // identifier
    {MdiGlyph::Identifier,
     "M10 7V9H9V15H10V17H6V15H7V9H6V7H10M16 7C17.11 7 18 7.9 18 9V15C18 16.11 17.11 17 16 17"
     "H12V7M16 9H14V15H16V9Z",
     "identifier"},
    // heart-outline
    {MdiGlyph::Heart,
     "M12.1,18.55L12,18.65L11.89,18.55C7.14,14.24 4,11.39 4,8.5C4,6.5 5.5,5 7.5,5C9.04,5 "
     "10.54,6 11.07,7.36H12.93C13.46,6 14.96,5 16.5,5C18.5,5 20,6.5 20,8.5C20,11.39 16.86,"
     "14.24 12.1,18.55M16.5,3C14.76,3 13.09,3.81 12,5.08C10.91,3.81 9.24,3 7.5,3C4.42,3 2,"
     "5.41 2,8.5C2,12.27 5.4,15.36 10.55,20.03L12,21.35L13.45,20.03C18.6,15.36 22,12.27 22,8.5"
     "C22,5.41 19.58,3 16.5,3Z",
     "heart-outline"},
    // logout
    {MdiGlyph::Logout,
     "M17 7L15.59 8.41L18.17 11H8V13H18.17L15.59 15.58L17 17L22 12M4 5H12V3H4C2.9 3 2 3.9 2 5"
     "V19C2 20.1 2.9 21 4 21H12V19H4V5Z",
     "logout"},
    // delete-outline
    {MdiGlyph::Delete,
     "M6,19A2,2 0 0,0 8,21H16A2,2 0 0,0 18,19V7H6V19M8,9H16V19H8V9M15.5,4L14.5,3H9.5L8.5,4H5V6"
     "H19V4H15.5Z",
     "delete-outline"},
    // refresh
    {MdiGlyph::Refresh,
     "M17.65,6.35C16.2,4.9 14.21,4 12,4A8,8 0 0,0 4,12A8,8 0 0,0 12,20C15.73,20 18.84,17.45 "
     "19.73,14H17.65C16.83,16.33 14.61,18 12,18A6,6 0 0,1 6,12A6,6 0 0,1 12,6C13.66,6 15.14,"
     "6.69 16.22,7.78L13,11H20V4L17.65,6.35Z",
     "refresh"},
    // content-copy
    {MdiGlyph::Copy,
     "M19,21H8V7H19M19,5H8A2,2 0 0,0 6,7V21A2,2 0 0,0 8,23H19A2,2 0 0,0 21,21V7A2,2 0 0,0 19,5"
     "M16,1H4A2,2 0 0,0 2,3V17H4V3H16V1Z",
     "content-copy"},
};

inline constexpr size_t kMdiGlyphCount = sizeof(kMdiGlyphs) / sizeof(kMdiGlyphs[0]);

// The glyph's path data; "" for a value outside the table (which a test rules
// out).
inline constexpr const char* MdiGlyphPath(MdiGlyph glyph) {
  for (const MdiGlyphData& data : kMdiGlyphs) {
    if (data.glyph == glyph) return data.path;
  }
  return "";
}

}  // namespace urnw
