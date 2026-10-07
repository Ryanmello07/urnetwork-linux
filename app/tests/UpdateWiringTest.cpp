// The updater exists AND something calls it.
//
// ReleaseSelectionTest proves the decisions; this proves the call sites, the
// same way TunnelPolicyTest reads Tunnel.cpp and EarningsPageTeardownTest
// reads EarningsPage.cpp: a checker that nothing starts, a button that
// reports "no UpdateChecker in this build", an AppImage whose embedded update
// info points at a host that does not resolve -- each is a shipped defect
// this file fails on. UpdateChecker needs GTK and libsoup, so the sources are
// read as text.
//
// SPDX-License-Identifier: MPL-2.0
#include "TestHarness.hpp"

#include "ReleaseSelection.hpp"

#include <fstream>
#include <sstream>
#include <string>

#ifndef UR_SRC_DIR
#define UR_SRC_DIR ""
#endif

namespace {

std::string ReadSource(const std::string& relative) {
  std::ifstream in(std::string(UR_SRC_DIR) + "/" + relative, std::ios::binary);
  std::stringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

// The body of the function whose definition starts with `signature`, or ""
// (braces balanced from the first '{' after the signature).
std::string FunctionBody(const std::string& source, const std::string& signature) {
  const size_t at = source.find(signature);
  if (at == std::string::npos) return std::string();
  const size_t open = source.find('{', at);
  if (open == std::string::npos) return std::string();
  int depth = 0;
  for (size_t i = open; i < source.size(); ++i) {
    if (source[i] == '{') ++depth;
    if (source[i] == '}' && --depth == 0) return source.substr(open, i - open + 1);
  }
  return std::string();
}

bool Has(const std::string& haystack, const char* needle) {
  return haystack.find(needle) != std::string::npos;
}

}  // namespace

UR_TEST(theWindowBuildsBindsAndStartsTheChecker) {
  const std::string source = ReadSource("MainWindow.cpp");
  if (source.empty()) {
    UR_FAIL("could not read MainWindow.cpp to check the updater is wired");
    return;
  }
  UR_EXPECT_TRUE_MSG("MainWindow does not construct an UpdateChecker",
                     Has(source, "updates_ = std::make_unique<UpdateChecker>()"));
  UR_EXPECT_TRUE_MSG("the developer page is not handed the checker",
                     Has(source, "developerPage_->SetUpdateChecker(updates_.get())"));
  UR_EXPECT_TRUE_MSG("the settings page is not handed the checker",
                     Has(source, "settingsPage_->SetUpdateChecker(updates_.get())"));
  UR_EXPECT_TRUE_MSG("no handler replays snapshots into Settings",
                     Has(source, "settingsPage_->ApplyUpdate(snap)"));
  UR_EXPECT_TRUE_MSG("no handler replays snapshots into Developer",
                     Has(source, "developerPage_->ApplyUpdateCheck(snap)"));
  // bind-then-replay-then-start: a launch check that fires before the
  // handler is bound is a check nobody sees
  const size_t handler = source.find("updates_->SetHandler(");
  const size_t replay = source.find("settingsPage_->ApplyUpdate(updates_->Current())");
  const size_t start = source.find("updates_->Start()");
  UR_EXPECT_TRUE_MSG("the checker is never started", start != std::string::npos);
  UR_EXPECT_TRUE_MSG("the handler is bound after Start()",
                     handler != std::string::npos && handler < start);
  UR_EXPECT_TRUE_MSG("the current snapshot is not replayed before Start()",
                     replay != std::string::npos && replay < start);
}

UR_TEST(theDeveloperButtonRunsARealCheck) {
  const std::string source = ReadSource("DeveloperPage.cpp");
  if (source.empty()) {
    UR_FAIL("could not read DeveloperPage.cpp to check the button");
    return;
  }
  const std::string intro = FunctionBody(source, "void DeveloperPage::BuildIntroCard()");
  if (intro.empty()) {
    UR_FAIL("could not find DeveloperPage::BuildIntroCard");
    return;
  }
  UR_EXPECT_TRUE_MSG("Check for updates does not call UpdateChecker::CheckNow",
                     Has(intro, "updates_->CheckNow()"));
  UR_EXPECT_TRUE_MSG("the 'no UpdateChecker in this build' stub is still the whole handler",
                     !Has(intro, "no UpdateChecker in this build"));
  const std::string apply =
      FunctionBody(source, "void DeveloperPage::ApplyUpdateCheck(");
  UR_EXPECT_TRUE_MSG("ApplyUpdateCheck does not render the outcome", Has(apply, "updateCheckText_"));
  UR_EXPECT_TRUE_MSG("ApplyUpdateCheck does not name the newest release",
                     Has(apply, "snap.newestVersion"));
}

UR_TEST(settingsDrivesTheCheckerAndRendersItsStates) {
  const std::string source = ReadSource("SettingsPage.cpp");
  if (source.empty()) {
    UR_FAIL("could not read SettingsPage.cpp to check the notice");
    return;
  }
  const std::string general = FunctionBody(source, "void SettingsPage::BuildGeneralSection(");
  UR_EXPECT_TRUE_MSG("the auto-check toggle no longer reaches the checker",
                     Has(general, "updates_->SetAutoCheckEnabled(on)"));
  UR_EXPECT_TRUE_MSG("the toggle still carries the 'does not exist in this tree' TODO",
                     !Has(general, "TODO(sdk-wiring): urnw::UpdateChecker"));
  const std::string button = FunctionBody(source, "void SettingsPage::OnUpdateButton()");
  UR_EXPECT_TRUE_MSG("Install does not begin the apply", Has(button, "updates_->BeginInstall()"));
  UR_EXPECT_TRUE_MSG("Relaunch does not relaunch", Has(button, "updates_->Relaunch()"));
  UR_EXPECT_TRUE_MSG("the packaged installs are not sent to the release page",
                     Has(button, "snap.releasePage"));
  // the install path is the AppImage's alone
  UR_EXPECT_TRUE_MSG("BeginInstall is not gated on the AppImage kind",
                     Has(button, "snap.kind == update::InstallKind::AppImage && updates_"));
  const std::string apply = FunctionBody(source, "void SettingsPage::ApplyUpdate(");
  for (const char* phase : {"Phase::Available", "Phase::Downloading", "Phase::Verifying",
                            "Phase::Installing", "Phase::Ready", "Phase::Downloaded",
                            "Phase::Failed"}) {
    UR_EXPECT_TRUE_MSG(std::string("ApplyUpdate does not render ") + phase, Has(apply, phase));
  }
  UR_EXPECT_TRUE_MSG("the package-manager command is not shown", Has(apply, "snap.command"));
}

UR_TEST(theCheckerVerifiesBeforeItSwapsAndReadsOnlyTheOfficialList) {
  const std::string source = ReadSource("UpdateChecker.cpp");
  if (source.empty()) {
    UR_FAIL("could not read UpdateChecker.cpp");
    return;
  }
  // the URL comes from the one constant, never a second literal
  UR_EXPECT_TRUE_MSG("the check does not use update::ReleasesApiUrl()",
                     Has(source, "update::ReleasesApiUrl()"));
  UR_EXPECT_TRUE_MSG("UpdateChecker.cpp names a GitHub host of its own",
                     !Has(source, "api.github.com") && !Has(source, "github.com/"));
  UR_EXPECT_TRUE_MSG("UpdateChecker.cpp names the nightly repo", !Has(source, "urnetwork/build"));
  // the list is asked for by the repository id, and a redirect fails it; the
  // AppImage download follows the storage host's
  const std::string fetch = FunctionBody(source, "bool FetchUrl(");
  UR_EXPECT_TRUE_MSG("FetchUrl does not refuse redirects when asked to",
                     Has(fetch, "if (!followRedirects) soup_message_add_flags(msg, "
                                "SOUP_MESSAGE_NO_REDIRECT)"));
  const std::string check = FunctionBody(source, "void UpdateChecker::RunCheck()");
  UR_EXPECT_TRUE_MSG("the release list request follows redirects",
                     Has(check, "kMaxJsonBytes,\n      /*followRedirects=*/false"));
  // an empty stable repo answers an empty list, which is "no update"; a 404
  // from a list asked for by id means the id no longer resolves, and fails
  // the check like any other status
  UR_EXPECT_TRUE_MSG("a 404 from the release list reads as 'no releases yet'",
                     !Has(check, "== 404") && !Has(check, "404 ==") &&
                         Has(check, "if (!fetched) {"));
  // verify, THEN swap -- and a mismatch unlinks the download
  const std::string apply = FunctionBody(source, "void UpdateChecker::RunApply()");
  if (apply.empty()) {
    UR_FAIL("could not find UpdateChecker::RunApply");
    return;
  }
  const size_t verify = apply.find("actual != offer.digestHex");
  const size_t swap = apply.find("::rename(part.c_str(), appimage.c_str())");
  const size_t save = apply.find("::rename(part.c_str(), finalPath.c_str())");
  UR_EXPECT_TRUE_MSG("the apply never compares the digest", verify != std::string::npos);
  UR_EXPECT_TRUE_MSG("the apply never swaps the image", swap != std::string::npos);
  UR_EXPECT_TRUE_MSG("the digest is compared AFTER the swap", verify < swap);
  UR_EXPECT_TRUE_MSG("the digest is compared AFTER the save", verify < save);
  UR_EXPECT_TRUE_MSG("the AppImage download does not follow the storage host's redirect",
                     Has(apply, "kMaxImageBytes, /*followRedirects=*/true"));
  UR_EXPECT_TRUE_MSG("the apply never runs a package manager or elevates",
                     !Has(apply, "sudo") && !Has(apply, "pkexec") && !Has(apply, "g_spawn"));
  // the running image is kept
  UR_EXPECT_TRUE_MSG("the running image is not kept as .bak",
                     Has(apply, "update::BackupPath(appimage)"));
  // the next launch is what removes it
  const std::string cleanup = FunctionBody(source, "void UpdateChecker::CleanupStaleFiles()");
  UR_EXPECT_TRUE_MSG("the next launch does not remove the .bak",
                     Has(cleanup, "update::BackupPath(appimage)"));
}

UR_TEST(theTarballUpdateAsksTheSameRepositoryIdWithoutRedirects) {
  // install.sh --update is the daemon tarball's half of the same rule: the
  // API by kUpdateRepoId, its answer never followed elsewhere
  const std::string script = ReadSource("../../packaging/tarball/install.sh");
  if (script.empty()) {
    UR_FAIL("could not read packaging/tarball/install.sh");
    return;
  }
  const std::string id = std::to_string(urnw::update::kUpdateRepoId);
  UR_EXPECT_TRUE_MSG("install.sh does not pin the repository id ReleaseSelection.hpp pins",
                     Has(script, ("UPDATE_REPO_ID='" + id + "'").c_str()));
  UR_EXPECT_TRUE_MSG("install.sh does not ask the API by the repository id",
                     Has(script, "UPDATE_API_URL=\"https://api.github.com/repositories/"
                                 "${UPDATE_REPO_ID}/releases/latest\""));
  const size_t api = script.find("-o \"${UPDATE_JSON}\"");
  const size_t call = script.rfind("curl ", api);
  UR_EXPECT_TRUE_MSG("install.sh's API request is not found", api != std::string::npos &&
                                                               call != std::string::npos);
  if (api != std::string::npos && call != std::string::npos) {
    UR_EXPECT_TRUE_MSG("install.sh's API request follows redirects (-L)",
                       script.compare(call, 9, "curl -sS ") == 0);
  }
}

UR_TEST(theAppImageCarriesNoZsyncUpdateInformation) {
  // GitHub Releases answers zsync's multi-range requests with HTTP 501 and
  // the old embedded host (get.ur.network) does not resolve, so update info
  // that names either is a channel that cannot work. Updates come from the
  // in-app checker.
  const std::string script = ReadSource("../../packaging/make-appimage.sh");
  if (script.empty()) {
    UR_FAIL("could not read packaging/make-appimage.sh");
    return;
  }
  UR_EXPECT_TRUE_MSG("make-appimage.sh still passes --updateinformation",
                     !Has(script, "--updateinformation"));
  UR_EXPECT_TRUE_MSG("make-appimage.sh still names get.ur.network",
                     !Has(script, "get.ur.network"));
  UR_EXPECT_TRUE_MSG("make-appimage.sh still produces or expects a .zsync",
                     !Has(script, ".zsync"));
  UR_EXPECT_TRUE_MSG("make-appimage.sh does not say where updates come from",
                     Has(script, "UpdateChecker"));
}
