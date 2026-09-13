# qrcodegen (vendored)

Project Nayuki's QR Code generator library, C++ port — the single-file encoder
behind the extender share code (connect/EXTENDER.md K7/K8: "the code renders
through a vendored single-file encoder" on Windows and Linux).

* Upstream: <https://github.com/nayuki/QR-Code-generator>, `cpp/qrcodegen.{hpp,cpp}`
* License: **MIT** (`LICENSE` here, and the notice at the head of each file).
  NOT MPL-2.0 like the rest of this tree — keep the upstream headers intact.
* Vendored 2026-09-13, upstream `master`:

      qrcodegen.hpp  sha256 b779c3b156cf7a57ce789d6fee4fc991ccc2913774d26c909d22bb8f26b2a793
      qrcodegen.cpp  sha256 8948b57053deb5d132bfc675ca2688b7abef9f03ec633c0de59770c945a66fc9

VENDORED RATHER THAN PACKAGED because no distribution in the support matrix
ships a QR *encoder* as a library the GUI could link (`libqrencode` is C and
GPL/LGPL-adjacent packaging varies; zxing-cpp, which this app does depend on,
is used for *decoding* only). One 1.4 kloc MIT file with no dependencies is a
smaller liability than a per-distro optional dependency that would silently
drop the share screen.

UPDATING: replace both files verbatim from upstream, re-record the hashes
above, and change nothing else — there are no local patches and there must not
be any. The only API this tree uses is `qrcodegen::QrCode::encodeText(text,
Ecc::HIGH)` plus `getSize()` / `getModule(x, y)`.
