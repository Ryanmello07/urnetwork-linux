package main

import (
	"bytes"
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"
)

// Every test uses fakes: no docker, container, display, account or network.

// ---- fixtures ----

func node(role, name string, showing bool, children ...map[string]any) map[string]any {
	states := []string{"enabled", "sensitive"}
	if showing {
		states = append(states, "showing", "visible")
	}
	return map[string]any{"role": role, "name": name, "states": states, "attributes": map[string]string{}, "children": children}
}

func withStates(n map[string]any, extra ...string) map[string]any {
	n["states"] = append(n["states"].([]string), extra...)
	return n
}

func withAttribute(n map[string]any, key, value string) map[string]any {
	n["attributes"].(map[string]string)[key] = value
	return n
}

// treeJson assigns child-index paths, as atspi.py does, and encodes.
func treeJson(root map[string]any) string {
	var assign func(n map[string]any, path []int)
	assign = func(n map[string]any, path []int) {
		n["path"] = path
		for i, c := range n["children"].([]map[string]any) {
			assign(c, append(append([]int{}, path...), i))
		}
	}
	assign(root, []int{})
	b, _ := json.Marshal(root)
	return string(b)
}

// homeTree is the signed-in window: the nav (whose "Connect" is a decoy), the
// Connect page with its round button, and an optional extra card.
func homeTree(roundLabel string, killSwitchShowing, killSwitchOn bool, cards ...map[string]any) string {
	killSwitch := node("toggle button", killSwitchText, killSwitchShowing)
	if killSwitchOn {
		withStates(killSwitch, "checked")
	}
	pane := node("generic", "", true,
		node("button", selectedProviderTag+", Best available provider", true),
		node("push button", roundLabel, true),
		node("push button", moreOptionsText, true),
		node("generic", "", killSwitchShowing, killSwitch),
	)
	page := node("generic", "", true, append([]map[string]any{pane}, cards...)...)
	nav := node("generic", "", true, node("push button", connectText, true), node("push button", "Account", true))
	return treeJson(node("application", "urnetwork-gui", false, node("frame", "URnetwork", true, nav, page)))
}

// heldBanner is the alert the protocol expects: the held notice with
// Disconnect next to Upgrade.
func heldBanner(showing bool, buttons ...string) map[string]any {
	header := node("generic", "", showing, node("label", alertTitleText, showing))
	card := node("generic", "", showing, header, node("label", heldNoticeText, showing))
	for _, b := range buttons {
		card["children"] = append(card["children"].([]map[string]any), node("push button", b, showing))
	}
	return card
}

func loginTree() string {
	email := withAttribute(node("text", "", true), "placeholder-text", loginPlaceholder)
	return treeJson(node("application", "urnetwork-gui", false, node("frame", "URnetwork", true,
		email, node("push button", getStartedText, true), node("push button", "Sign in with Google", true))))
}

func passwordTree() string {
	password := withAttribute(node("password text", "", true), "placeholder-text", passwordPlaceholder)
	return treeJson(node("application", "urnetwork-gui", false, node("frame", "URnetwork", true,
		password, node("push button", signInText, true))))
}

// fakeRunner answers docker commands from scripted responses keyed by the
// command line, and records every call.
type fakeRunner struct {
	calls     []string
	trees     []string // successive "atspi tree" results; the last repeats
	tunnel    string
	notifyLog string
	egress    string
	failOn    map[string]error
	onAction  func(args string)
}

func (self *fakeRunner) Run(ctx context.Context, name string, args ...string) ([]byte, error) {
	line := name + " " + strings.Join(args, " ")
	self.calls = append(self.calls, line)
	for prefix, err := range self.failOn {
		if strings.HasPrefix(line, prefix) {
			return nil, err
		}
	}
	if name != "docker" || len(args) == 0 {
		return nil, errors.New("unexpected command")
	}
	if args[0] != "exec" {
		if args[0] == "ps" {
			return []byte("abc123\n"), nil
		}
		return nil, nil
	}
	verb := strings.Join(args[3:], " ")
	switch {
	case verb == "atspi tree":
		t := self.trees[0]
		if len(self.trees) > 1 {
			self.trees = self.trees[1:]
		}
		return []byte(t), nil
	case strings.HasPrefix(verb, "atspi action"), strings.HasPrefix(verb, "atspi focus"), strings.HasPrefix(verb, "atspi type-credential"):
		if self.onAction != nil {
			self.onAction(verb)
		}
		return nil, nil
	case verb == "tunnel":
		return []byte(self.tunnel + "\n"), nil
	case verb == "notifications":
		return []byte(self.notifyLog), nil
	case verb == "egress":
		return []byte(self.egress + "\n"), nil
	case verb == "traffic", verb == "start":
		return nil, nil
	}
	return nil, fmt.Errorf("unexpected exec %q", verb)
}

func (self *fakeRunner) actions() []string {
	var out []string
	for _, c := range self.calls {
		if i := strings.Index(c, "container.sh atspi "); 0 <= i && !strings.HasSuffix(c, "atspi tree") {
			out = append(out, c[i+len("container.sh atspi "):])
		}
	}
	return out
}

type fakeClock struct{ now time.Time }

func (self *fakeClock) Now() time.Time { return self.now }
func (self *fakeClock) Sleep(ctx context.Context, d time.Duration) error {
	self.now = self.now.Add(d)
	return ctx.Err()
}

func notifyLine(summary, body string) string {
	b, _ := json.Marshal(map[string]any{"method": "Notify", "summary": summary, "body": body})
	return string(b) + "\n"
}

func newTestDriver(t *testing.T, runner *fakeRunner) *driver {
	t.Helper()
	dir := t.TempDir()
	return &driver{
		linuxDir: dir,
		stateDir: dir,
		outDir:   filepath.Join(dir, "out"),
		version:  "0.0.0-0",
		runner:   runner,
		clock:    &fakeClock{now: time.Unix(0, 0)},
	}
}

func withContainer(t *testing.T, d *driver, baseline int) {
	t.Helper()
	if err := d.saveState(driverState{Container: "c1", NotificationBaseline: baseline}); err != nil {
		t.Fatal(err)
	}
}

func runVerb(t *testing.T, d *driver, args ...string) (map[string]any, string, int) {
	t.Helper()
	var stdout, stderr bytes.Buffer
	code := run(context.Background(), d, args, &stdout, &stderr)
	var out map[string]any
	if code == 0 {
		dec := json.NewDecoder(&stdout)
		if err := dec.Decode(&out); err != nil {
			t.Fatalf("stdout is not one JSON object: %q", stdout.String())
		}
		if dec.More() {
			t.Fatalf("stdout has more than one JSON object")
		}
	} else if stdout.Len() != 0 {
		t.Fatalf("a failing verb printed to stdout: %q", stdout.String())
	}
	return out, stderr.String(), code
}

// ---- observe ----

func TestObserveHeldStateWithAlertDisconnectAndUpgrade(t *testing.T) {
	r := &fakeRunner{
		trees:     []string{homeTree(disconnectText, false, false, heldBanner(true, "Upgrade", disconnectText))},
		tunnel:    "up",
		notifyLog: notifyLine("Other", "x") + notifyLine(alertTitleText, heldNoticeText),
	}
	d := newTestDriver(t, r)
	withContainer(t, d, 0)
	out, stderr, code := runVerb(t, d, "observe")
	if code != 0 {
		t.Fatalf("observe failed: %s", stderr)
	}
	want := map[string]any{"connect_requested": true, "connected": true, "insufficient_balance_alert": true,
		"disconnect_visible": true, "upgrade_visible": true, "insufficient_balance_notifications": float64(1)}
	if fmt.Sprint(out) != fmt.Sprint(want) {
		t.Fatalf("observe = %v, want %v", out, want)
	}
}

// Today's Linux GUI keeps the banner on the hidden legacy page and offers no
// Disconnect beside an Upgrade: the driver must report exactly that.
func TestObserveHiddenBannerIsNoAlert(t *testing.T) {
	r := &fakeRunner{
		trees:     []string{homeTree(disconnectText, false, false, heldBanner(false, "Get Pro"))},
		tunnel:    "up",
		notifyLog: notifyLine(alertTitleText, heldNoticeText),
	}
	d := newTestDriver(t, r)
	withContainer(t, d, 0)
	o, err := d.observe(context.Background())
	if err != nil {
		t.Fatal(err)
	}
	if o.Alert || o.DisconnectButton || o.UpgradeButton || !o.ConnectRequested || o.Notifications != 1 {
		t.Fatalf("observe = %+v", o)
	}
}

// The round button reads Disconnect while connected; it is not the alert's
// Disconnect, even when the alert has no button of its own.
func TestRoundDisconnectIsNotTheAlertDisconnect(t *testing.T) {
	root, err := parseTree([]byte(homeTree(disconnectText, false, false, heldBanner(true))))
	if err != nil {
		t.Fatal(err)
	}
	a := findAlert(root)
	if !a.alert || a.disconnect != nil || a.upgrade != nil {
		t.Fatalf("alert = %+v", a)
	}
}

func TestAlertGroupDoesNotReachUnrelatedButtons(t *testing.T) {
	// a Disconnect three levels above the notice is not next to Upgrade
	far := node("generic", "", true, node("generic", "", true, heldBanner(true, "Upgrade")), node("push button", disconnectText, true))
	root, err := parseTree([]byte(homeTree(connectText, false, false, far)))
	if err != nil {
		t.Fatal(err)
	}
	a := findAlert(root)
	if a.disconnect != nil || a.upgrade == nil {
		t.Fatalf("alert = %+v", a)
	}
}

func TestNavConnectIsNotTheRoundButton(t *testing.T) {
	root, err := parseTree([]byte(homeTree(connectText, false, false)))
	if err != nil {
		t.Fatal(err)
	}
	b := connectActionButton(root)
	if b == nil || len(b.Path) != 4 || b.Path[1] != 1 {
		t.Fatalf("round button = %+v", b)
	}
}

func TestObserveDisconnectedAndNotificationBaseline(t *testing.T) {
	r := &fakeRunner{
		trees:     []string{homeTree(connectText, false, false)},
		tunnel:    "down",
		notifyLog: notifyLine(alertTitleText, "") + notifyLine(alertTitleText, ""),
	}
	d := newTestDriver(t, r)
	withContainer(t, d, 1)
	o, err := d.observe(context.Background())
	if err != nil {
		t.Fatal(err)
	}
	if o.ConnectRequested || o.Connected || o.Notifications != 1 {
		t.Fatalf("observe = %+v", o)
	}
}

func TestObserveBeforeSetupFails(t *testing.T) {
	d := newTestDriver(t, &fakeRunner{})
	_, stderr, code := runVerb(t, d, "observe")
	if code == 0 || !strings.Contains(stderr, "setup has not run") || strings.Count(stderr, "\n") != 1 {
		t.Fatalf("code=%d stderr=%q", code, stderr)
	}
}

// ---- notifications ----

func TestCountBalanceNotifications(t *testing.T) {
	log := notifyLine(alertTitleText, heldNoticeText) +
		`{"method":"CloseNotification","id":1}` + "\n" +
		notifyLine("Update available", "") +
		notifyLine("Ungültig", heldNoticeText) +
		"\n"
	n, err := countBalanceNotifications([]byte(log))
	if err != nil || n != 2 {
		t.Fatalf("count = %d, %v", n, err)
	}
	if _, err := countBalanceNotifications([]byte("not json\n")); err == nil {
		t.Fatal("malformed line accepted")
	}
}

// ---- egress ----

func TestEgressVerb(t *testing.T) {
	for _, c := range []struct {
		probe string
		want  string
		fails bool
	}{
		{"ip 203.0.113.9", `{"ip":"203.0.113.9"}`, false},
		{"ip 2001:db8::1", `{"ip":"2001:db8::1"}`, false},
		{"error https://checkip.amazonaws.com/: curl: (28) timed out", `{"error":"https://checkip.amazonaws.com/: curl: (28) timed out"}`, false},
		{"error", `{"error":"probe failed"}`, false},
		{"ip <html>", "", true},
		{"", "", true},
	} {
		r := &fakeRunner{egress: c.probe}
		d := newTestDriver(t, r)
		withContainer(t, d, 0)
		var stdout, stderr bytes.Buffer
		code := run(context.Background(), d, []string{"egress"}, &stdout, &stderr)
		if c.fails != (code != 0) || strings.TrimSpace(stdout.String()) != c.want {
			t.Fatalf("probe %q: code=%d stdout=%q stderr=%q", c.probe, code, stdout.String(), stderr.String())
		}
	}
}

// direct-egress needs an address; a failed probe is a driver failure there.
func TestDirectEgressRequiresAddress(t *testing.T) {
	r := &fakeRunner{egress: "error offline"}
	d := newTestDriver(t, r)
	withContainer(t, d, 0)
	_, stderr, code := runVerb(t, d, "direct-egress")
	if code == 0 || !strings.Contains(stderr, "offline") {
		t.Fatalf("code=%d stderr=%q", code, stderr)
	}
}

// ---- actions ----

func TestPressDisconnectRefusesRoundButton(t *testing.T) {
	r := &fakeRunner{trees: []string{homeTree(disconnectText, false, false, heldBanner(true))}}
	d := newTestDriver(t, r)
	withContainer(t, d, 0)
	_, stderr, code := runVerb(t, d, "press-disconnect")
	if code == 0 || !strings.Contains(stderr, "no Disconnect button next to Upgrade") {
		t.Fatalf("code=%d stderr=%q", code, stderr)
	}
	if len(r.actions()) != 0 {
		t.Fatalf("pressed something: %v", r.actions())
	}
}

func TestPressDisconnectClicksAlertButton(t *testing.T) {
	r := &fakeRunner{trees: []string{homeTree(disconnectText, false, false, heldBanner(true, "Upgrade", disconnectText))}}
	d := newTestDriver(t, r)
	withContainer(t, d, 0)
	if _, stderr, code := runVerb(t, d, "press-disconnect"); code != 0 {
		t.Fatal(stderr)
	}
	// page card index 1 of the page; the button is the banner's 4th child
	if got := r.actions(); len(got) != 1 || got[0] != "action 0,1,1,3 click" {
		t.Fatalf("actions = %v", got)
	}
}

func TestConnectPressesRoundButtonOnlyWhenDisconnected(t *testing.T) {
	r := &fakeRunner{trees: []string{homeTree(connectText, false, false)}}
	d := newTestDriver(t, r)
	withContainer(t, d, 0)
	if _, stderr, code := runVerb(t, d, "connect"); code != 0 {
		t.Fatal(stderr)
	}
	if got := r.actions(); len(got) != 1 || got[0] != "action 0,1,0,1 click" {
		t.Fatalf("actions = %v", got)
	}
	r2 := &fakeRunner{trees: []string{homeTree(disconnectText, false, false)}}
	d2 := newTestDriver(t, r2)
	withContainer(t, d2, 0)
	if _, stderr, code := runVerb(t, d2, "connect"); code != 0 || len(r2.actions()) != 0 {
		t.Fatalf("stderr=%q actions=%v", stderr, r2.actions())
	}
}

func TestKillSwitchExpandsOptionsAndToggles(t *testing.T) {
	r := &fakeRunner{trees: []string{
		homeTree(connectText, false, false), // collapsed
		homeTree(connectText, true, false),  // after More options
		homeTree(connectText, true, true),   // after toggle
	}}
	d := newTestDriver(t, r)
	withContainer(t, d, 0)
	if _, stderr, code := runVerb(t, d, "kill-switch", "on"); code != 0 {
		t.Fatal(stderr)
	}
	want := []string{"action 0,1,0,2 click", "action 0,1,0,3,0 toggle"}
	if got := r.actions(); fmt.Sprint(got) != fmt.Sprint(want) {
		t.Fatalf("actions = %v, want %v", got, want)
	}
	// already in the wanted state: no press
	r2 := &fakeRunner{trees: []string{homeTree(connectText, true, false)}}
	d2 := newTestDriver(t, r2)
	withContainer(t, d2, 0)
	if _, stderr, code := runVerb(t, d2, "kill-switch", "off"); code != 0 || len(r2.actions()) != 0 {
		t.Fatalf("stderr=%q actions=%v", stderr, r2.actions())
	}
}

func TestUnknownVerbsAndArguments(t *testing.T) {
	d := newTestDriver(t, &fakeRunner{})
	for _, args := range [][]string{{}, {"dance"}, {"kill-switch"}, {"kill-switch", "maybe"}, {"observe", "x"}} {
		if _, _, code := runVerb(t, d, args...); code == 0 {
			t.Fatalf("%v accepted", args)
		}
	}
}

// ---- setup and teardown ----

// writeCredentials writes the runner's per-case credentials file, which the
// test then passes as setup's argument, and returns its path.
func writeCredentials(t *testing.T, d *driver, content string, mode os.FileMode) string {
	t.Helper()
	path := filepath.Join(d.stateDir, "credentials")
	if err := os.WriteFile(path, []byte(content), mode); err != nil {
		t.Fatal(err)
	}
	if err := os.Chmod(path, mode); err != nil {
		t.Fatal(err)
	}
	return path
}

func writeArtifacts(t *testing.T, d *driver) {
	t.Helper()
	os.MkdirAll(d.outDir, 0700)
	for _, name := range []string{"urnetwork-daemon_0.0.0-0_arm64.deb", "URnetwork-0.0.0-0-arm64.AppImage"} {
		os.WriteFile(filepath.Join(d.outDir, name), []byte("x"), 0600)
	}
}

func TestSetupSignsInThroughTheGui(t *testing.T) {
	r := &fakeRunner{
		trees: []string{
			loginTree(),
			passwordTree(),
			homeTree(connectText, false, false), // home reached
			homeTree(connectText, false, false), // disconnected check
			homeTree(connectText, false, false), // kill switch collapsed
			homeTree(connectText, true, false),  // expanded, off
		},
		notifyLog: notifyLine(alertTitleText, ""),
	}
	d := newTestDriver(t, r)
	credentials := writeCredentials(t, d, "email: a@example.com\npassword: \"secret-value\"\n", 0600)
	writeArtifacts(t, d)
	out, stderr, code := runVerb(t, d, "setup", credentials)
	if code != 0 {
		t.Fatalf("setup failed: %s", stderr)
	}
	if out["kill_switch_supported"] != true {
		t.Fatalf("setup = %v", out)
	}
	want := []string{
		"focus 0,0", "type-credential email", "action 0,1 click",
		"focus 0,0", "type-credential password", "action 0,1 click",
		"action 0,1,0,2 click",
	}
	if got := r.actions(); fmt.Sprint(got) != fmt.Sprint(want) {
		t.Fatalf("actions = %v, want %v", got, want)
	}
	s, _ := d.loadState()
	if s.Container != d.containerName() || s.NotificationBaseline != 1 {
		t.Fatalf("state = %+v", s)
	}
	for _, c := range r.calls {
		if strings.Contains(c, "secret-value") || strings.Contains(c, "a@example.com") {
			t.Fatalf("a credential value reached a command line: %q", c)
		}
	}
	var sawRun bool
	for _, c := range r.calls {
		if strings.HasPrefix(c, "docker run ") {
			sawRun = strings.Contains(c, "--cgroupns=host") && strings.Contains(c, credentials+":/opt/ib-private/credentials:ro")
		}
	}
	if !sawRun {
		t.Fatal("container was not started with the data-plane shape and a read-only credentials mount")
	}
}

func TestSetupReportsGuiSignInError(t *testing.T) {
	failed := treeJson(node("application", "urnetwork-gui", false, node("frame", "URnetwork", true,
		node("label", "Sign in failed", true))))
	r := &fakeRunner{trees: []string{failed}}
	d := newTestDriver(t, r)
	credentials := writeCredentials(t, d, "email: a@example.com\npassword: p\n", 0600)
	writeArtifacts(t, d)
	_, stderr, code := runVerb(t, d, "setup", credentials)
	if code == 0 || !strings.Contains(stderr, "sign-in failed in the GUI: Sign in failed") {
		t.Fatalf("code=%d stderr=%q", code, stderr)
	}
	// the container is recorded so teardown removes it
	if s, _ := d.loadState(); s.Container == "" {
		t.Fatal("container not recorded")
	}
}

func TestSetupStuckScreenTimesOut(t *testing.T) {
	r := &fakeRunner{trees: []string{treeJson(node("application", "urnetwork-gui", false, node("frame", "URnetwork", true,
		node("push button", "Mystery", true))))}}
	d := newTestDriver(t, r)
	credentials := writeCredentials(t, d, "email: a@example.com\npassword: p\n", 0600)
	writeArtifacts(t, d)
	_, stderr, code := runVerb(t, d, "setup", credentials)
	if code == 0 || !strings.Contains(stderr, "did not reach the Connect page") || !strings.Contains(stderr, "Mystery") {
		t.Fatalf("code=%d stderr=%q", code, stderr)
	}
}

func TestSetupPreconditions(t *testing.T) {
	d := newTestDriver(t, &fakeRunner{})
	credentials := writeCredentials(t, d, "email: a@example.com\npassword: hunter2-secret\n", 0644)
	_, stderr, code := runVerb(t, d, "setup", credentials)
	if code == 0 || !strings.Contains(stderr, "private") || strings.Contains(stderr, "hunter2") {
		t.Fatalf("code=%d stderr=%q", code, stderr)
	}
	writeCredentials(t, d, "email: a@example.com\npassword: x\nextra: y\n", 0600)
	if _, stderr, code := runVerb(t, d, "setup", credentials); code == 0 || !strings.Contains(stderr, "exactly one email and one password") {
		t.Fatalf("stderr=%q", stderr)
	}
	writeCredentials(t, d, "email: a@example.com\npassword: x\n", 0600)
	if _, stderr, code := runVerb(t, d, "setup", credentials); code == 0 || !strings.Contains(stderr, "run linux/test-main.sh") {
		t.Fatalf("stderr=%q", stderr)
	}
}

func TestTeardownRemovesContainerAndIsIdempotent(t *testing.T) {
	r := &fakeRunner{}
	d := newTestDriver(t, r)
	withContainer(t, d, 0)
	if _, stderr, code := runVerb(t, d, "teardown"); code != 0 {
		t.Fatal(stderr)
	}
	if !strings.Contains(strings.Join(r.calls, "\n"), "docker rm -f c1") {
		t.Fatalf("calls = %v", r.calls)
	}
	if _, err := os.Stat(d.statePath()); !errors.Is(err, os.ErrNotExist) {
		t.Fatal("state left behind")
	}
	// nothing recorded: still checks its own container name, still succeeds
	if _, stderr, code := runVerb(t, d, "teardown"); code != 0 {
		t.Fatal(stderr)
	}
}

func TestContainerNameIsPerStateDirectory(t *testing.T) {
	a := &driver{stateDir: "/a/android"}
	b := &driver{stateDir: "/a/linux"}
	if a.containerName() == b.containerName() || a.containerName() != (&driver{stateDir: "/a/android"}).containerName() {
		t.Fatal("container names are not stable per state directory")
	}
}

func TestRedactIsOneBoundedLine(t *testing.T) {
	s := redact("a\nb\tc " + strings.Repeat("x", 400))
	if strings.ContainsAny(s, "\n\t") || len(s) > 303 {
		t.Fatalf("redact = %q", s)
	}
}

// ---- the held alert on the Connect page (linux fix/insufficient-balance-disconnect e66d69e) ----

// connectPageTree models e66d69e's widget hierarchy as AT-SPI reports it:
//
//	frame "URnetwork"
//	  nav: button "Connect", button "Account"
//	  stack
//	    ConnectPage > pane A clamp > paneAContent_ (vertical box):
//	      statusRow [status dot, status column [status text]]
//	      hero_ button (named by the status text)
//	      locationRow_ button "Selected provider, Best available provider"
//	      connectBtn_ button (the round action; "Disconnect" while a session is up)
//	      heldAlert_ box (ConnectPage.cpp:506-537):
//	        label (the held notice)
//	        actions box: button "Upgrade", button "Disconnect"
//	      button "More options"
//	      moreOptionsHost_ box [extra...]
//	    connect-legacy page (not showing): the drawer banner with the same notice
//
// actions are the held alert's action-row buttons; extra goes into
// moreOptionsHost_.
func connectPageTree(roundLabel string, alertShowing bool, actions []string, extra ...map[string]any) string {
	var row []map[string]any
	for _, a := range actions {
		row = append(row, node("push button", a, alertShowing))
	}
	heldAlert := node("panel", "", alertShowing,
		node("label", heldNoticeText, alertShowing),
		node("panel", "", alertShowing, row...))
	paneAContent := node("panel", "", true,
		node("panel", "", true, node("label", "", true), node("panel", "", true, node("label", "Connected", true))),
		node("push button", "Connected", true),
		node("push button", selectedProviderTag+", Best available provider", true),
		node("push button", roundLabel, true),
		heldAlert,
		node("push button", moreOptionsText, true),
		node("panel", "", true, extra...),
	)
	connectPage := node("panel", "", true, node("panel", "", true, paneAContent))
	legacy := node("panel", "", false, heldBanner(false, "Get Pro"))
	stack := node("panel", "", true, connectPage, legacy)
	nav := node("panel", "", true, node("push button", connectText, true), node("push button", "Account", true))
	return treeJson(node("application", "urnetwork-gui", false, node("frame", "URnetwork", true, nav, stack)))
}

// the alert's Disconnect, sibling of Upgrade: frame 0, stack 1, page 0,
// clamp 0, paneAContent 0, its child 4 (heldAlert_), actions 1, button 1
const heldAlertDisconnectPath = "0,1,0,0,0,4,1,1"

// The round button: paneAContent child 3.
const roundButtonPath = "0,1,0,0,0,3"

func TestHeldAlertOnConnectPageWithTwoDisconnects(t *testing.T) {
	r := &fakeRunner{
		trees:     []string{connectPageTree(disconnectText, true, []string{"Upgrade", disconnectText})},
		tunnel:    "up",
		notifyLog: notifyLine(alertTitleText, heldNoticeText),
	}
	d := newTestDriver(t, r)
	withContainer(t, d, 0)
	o, err := d.observe(context.Background())
	if err != nil {
		t.Fatal(err)
	}
	if !o.Alert || !o.DisconnectButton || !o.UpgradeButton || !o.ConnectRequested || o.Notifications != 1 {
		t.Fatalf("observe = %+v", o)
	}
	if _, stderr, code := runVerb(t, d, "press-disconnect"); code != 0 {
		t.Fatal(stderr)
	}
	got := r.actions()
	if len(got) != 1 || got[0] != "action "+heldAlertDisconnectPath+" click" {
		t.Fatalf("pressed %v, want the held alert's Disconnect %s (never the round button %s)", got, heldAlertDisconnectPath, roundButtonPath)
	}
}

// A Disconnect elsewhere on the page is not "next to Upgrade".
func TestHeldAlertDisconnectMustBeUpgradeSibling(t *testing.T) {
	stray := node("push button", disconnectText, true)
	r := &fakeRunner{trees: []string{connectPageTree(disconnectText, true, []string{"Upgrade"}, stray)}, tunnel: "up"}
	d := newTestDriver(t, r)
	withContainer(t, d, 0)
	o, err := d.observe(context.Background())
	if err != nil {
		t.Fatal(err)
	}
	if !o.Alert || o.DisconnectButton {
		t.Fatalf("observe = %+v, want the alert without a sibling Disconnect", o)
	}
	_, stderr, code := runVerb(t, d, "press-disconnect")
	if code == 0 || len(r.actions()) != 0 {
		t.Fatalf("pressed %v (stderr %q), want a refusal", r.actions(), stderr)
	}
}

// An Upgrade elsewhere on the page does not make the alert's Disconnect "next
// to Upgrade", and does not count as the alert's Upgrade.
func TestHeldAlertUpgradeMustBeDisconnectSibling(t *testing.T) {
	stray := node("push button", "Upgrade", true)
	r := &fakeRunner{trees: []string{connectPageTree(disconnectText, true, []string{disconnectText}, stray)}, tunnel: "up"}
	d := newTestDriver(t, r)
	withContainer(t, d, 0)
	o, err := d.observe(context.Background())
	if err != nil {
		t.Fatal(err)
	}
	if !o.Alert || o.UpgradeButton || o.DisconnectButton {
		t.Fatalf("observe = %+v, want the alert without an Upgrade/Disconnect pair", o)
	}
}

// The alert is the held notice. A bare "Insufficient balance" heading (the
// legacy drawer's title) is not it.
func TestHeadingWithoutHeldNoticeIsNoAlert(t *testing.T) {
	heading := node("panel", "", true, node("label", alertTitleText, true),
		node("panel", "", true, node("push button", "Upgrade", true), node("push button", disconnectText, true)))
	root, err := parseTree([]byte(connectPageTree(disconnectText, false, []string{"Upgrade", disconnectText}, heading)))
	if err != nil {
		t.Fatal(err)
	}
	if a := findAlert(root); a.alert || a.disconnect != nil || a.upgrade != nil {
		t.Fatalf("alert = %+v", a)
	}
}

// Hidden (no held state): nothing is reported even though both Disconnects
// and the legacy notice exist in the tree.
func TestHiddenHeldAlertIsNoAlert(t *testing.T) {
	root, err := parseTree([]byte(connectPageTree(disconnectText, false, []string{"Upgrade", disconnectText})))
	if err != nil {
		t.Fatal(err)
	}
	if a := findAlert(root); a.alert || a.disconnect != nil || a.upgrade != nil {
		t.Fatalf("alert = %+v", a)
	}
	if b := connectActionButton(root); b == nil || pathArg(b.Path) != roundButtonPath {
		t.Fatalf("round button = %+v", b)
	}
}

// ---- per-run account: credentials only as setup's argument ----

const credentialsEnv = "URNETWORK_INSUFFICIENT_BALANCE_CREDENTIALS"

// privateFile writes a credentials-shaped file and forces its mode.
func privateFile(t *testing.T, dir, name, content string, mode os.FileMode) string {
	t.Helper()
	path := filepath.Join(dir, name)
	if err := os.WriteFile(path, []byte(content), mode); err != nil {
		t.Fatal(err)
	}
	if err := os.Chmod(path, mode); err != nil {
		t.Fatal(err)
	}
	return path
}

// signInTrees walks login, password, home, then setup's disconnected and kill
// switch checks.
func signInTrees() []string {
	return []string{
		loginTree(),
		passwordTree(),
		homeTree(connectText, false, false),
		homeTree(connectText, false, false),
		homeTree(connectText, false, false),
		homeTree(connectText, true, false),
	}
}

// containerSim stands in for the container: it remembers the host file that
// docker run mounts at /opt/ib-private/credentials and, as container.sh and
// atspi.py do, types values read from that mount and nothing else.
type containerSim struct {
	*fakeRunner
	mounted string
	typed   map[string]string
}

func newContainerSim() *containerSim {
	return &containerSim{fakeRunner: &fakeRunner{trees: signInTrees()}, typed: map[string]string{}}
}

func (self *containerSim) Run(ctx context.Context, name string, args ...string) ([]byte, error) {
	if name == "docker" && 0 < len(args) && args[0] == "run" {
		for i, a := range args {
			if a == "-v" && i+1 < len(args) && strings.HasSuffix(args[i+1], ":/opt/ib-private/credentials:ro") {
				self.mounted = strings.TrimSuffix(args[i+1], ":/opt/ib-private/credentials:ro")
			}
		}
	}
	if name == "docker" && 5 < len(args) && args[0] == "exec" && args[3] == "atspi" && args[4] == "type-credential" {
		self.typed[args[5]] = mountedCredential(self.mounted, args[5])
	}
	return self.fakeRunner.Run(ctx, name, args...)
}

// mountedCredential reads one value as atspi.py's read_credential does.
func mountedCredential(path, key string) string {
	b, err := os.ReadFile(path)
	if err != nil {
		return ""
	}
	for _, line := range strings.Split(string(b), "\n") {
		k, v, ok := strings.Cut(strings.TrimSpace(line), ":")
		if !ok || strings.TrimSpace(k) != key {
			continue
		}
		v = strings.TrimSpace(v)
		if 2 <= len(v) && (v[0] == '"' || v[0] == '\'') && v[len(v)-1] == v[0] {
			v = v[1 : len(v)-1]
		}
		return v
	}
	return ""
}

func dockerRuns(calls []string) int {
	n := 0
	for _, c := range calls {
		if strings.HasPrefix(c, "docker run ") {
			n++
		}
	}
	return n
}

// The fake sign-in sees exactly the argument file's values; a different,
// valid file named by the old environment variable is never used.
func TestSetupReadsCredentialsOnlyFromItsArgument(t *testing.T) {
	accounts := t.TempDir()
	argument := privateFile(t, accounts, "account-a.yml", "email: \"a@example.com\"\npassword: 'secret-a'\n", 0600)
	decoy := privateFile(t, accounts, "decoy.yml", "email: env@example.com\npassword: env-secret\n", 0600)
	t.Setenv(credentialsEnv, decoy)
	sim := newContainerSim()
	d := newTestDriver(t, nil)
	d.runner = sim
	writeArtifacts(t, d)
	out, stderr, code := runVerb(t, d, "setup", argument)
	if code != 0 || out["kill_switch_supported"] != true {
		t.Fatalf("setup: code=%d out=%v stderr=%q", code, out, stderr)
	}
	if sim.mounted != argument {
		t.Fatalf("mounted %q, want the argument %q", sim.mounted, argument)
	}
	if sim.typed["email"] != "a@example.com" || sim.typed["password"] != "secret-a" {
		t.Fatalf("the GUI was typed %v, want the argument file's values", sim.typed)
	}
	for _, c := range sim.calls {
		if strings.Contains(c, decoy) || strings.Contains(c, "secret-a") || strings.Contains(c, "a@example.com") {
			t.Fatalf("the decoy path or a credential value reached a command line: %q", c)
		}
	}
	if b, _ := os.ReadFile(d.statePath()); strings.Contains(string(b), "secret-a") || strings.Contains(string(b), "a@example.com") {
		t.Fatalf("credential values were written to the state file: %s", b)
	}
}

// URNETWORK_INSUFFICIENT_BALANCE_CREDENTIALS is not part of the protocol any
// more: the driver starts without it, ignores garbage in it, and a setup
// without its argument fails even when the variable names a valid file.
func TestCredentialsEnvironmentVariableIsIgnored(t *testing.T) {
	linuxDir := t.TempDir()
	t.Setenv("URNETWORK_INSUFFICIENT_BALANCE_STATE", t.TempDir())
	t.Setenv("UR_ACCEPT_LINUX_OUT", "")
	t.Setenv("EXTERNAL_WARP_VERSION", "")
	for _, value := range []string{"", "not a path", "/nonexistent/credentials.yml"} {
		t.Setenv(credentialsEnv, value)
		if _, _, err := newDriverFromEnv([]string{"--linux", linuxDir, "teardown"}); err != nil {
			t.Fatalf("with %s=%q: %v", credentialsEnv, value, err)
		}
	}

	valid := privateFile(t, t.TempDir(), "env.yml", "email: env@example.com\npassword: env-secret\n", 0600)
	t.Setenv(credentialsEnv, valid)
	d, rest, err := newDriverFromEnv([]string{"--linux", linuxDir, "setup"})
	if err != nil {
		t.Fatal(err)
	}
	sim := newContainerSim()
	d.runner, d.clock = sim, &fakeClock{now: time.Unix(0, 0)}
	writeArtifacts(t, d)
	if _, stderr, code := runVerb(t, d, rest...); code == 0 || !strings.Contains(stderr, "credentials file") || len(sim.calls) != 0 {
		t.Fatalf("setup without its argument: code=%d stderr=%q calls=%v", code, stderr, sim.calls)
	}

	t.Setenv(credentialsEnv, "garbage")
	argument := privateFile(t, t.TempDir(), "account.yml", "email: b@example.com\npassword: secret-b\n", 0600)
	if _, stderr, code := runVerb(t, d, "setup", argument); code != 0 {
		t.Fatalf("setup with its argument: %s", stderr)
	}
	if sim.mounted != argument || sim.typed["email"] != "b@example.com" || sim.typed["password"] != "secret-b" {
		t.Fatalf("mounted %q typed %v, want the argument file", sim.mounted, sim.typed)
	}
}

func TestSetupRejectsBadCredentialArguments(t *testing.T) {
	accounts := t.TempDir()
	good := "email: a@example.com\npassword: hunter2-secret\n"
	for _, c := range []struct {
		name string
		args []string
		want string
	}{
		{"no argument", []string{"setup"}, "exactly one argument"},
		{"two arguments", []string{"setup", privateFile(t, accounts, "two.yml", good, 0600), "extra"}, "exactly one argument"},
		{"relative path", []string{"setup", "account.yml"}, "must be absolute"},
		{"group-readable", []string{"setup", privateFile(t, accounts, "group.yml", good, 0640)}, "private regular file"},
		{"world-readable", []string{"setup", privateFile(t, accounts, "world.yml", good, 0604)}, "private regular file"},
		{"directory", []string{"setup", accounts}, "private regular file"},
		{"missing", []string{"setup", filepath.Join(accounts, "missing.yml")}, "missing"},
		{"malformed", []string{"setup", privateFile(t, accounts, "bad.yml", "email: a@example.com\n", 0600)}, "exactly one email and one password"},
	} {
		r := &fakeRunner{trees: signInTrees()}
		d := newTestDriver(t, r)
		writeArtifacts(t, d)
		_, stderr, code := runVerb(t, d, c.args...)
		if code == 0 || !strings.Contains(stderr, c.want) || strings.Contains(stderr, "hunter2") {
			t.Fatalf("%s: code=%d stderr=%q, want a failure naming %q", c.name, code, stderr, c.want)
		}
		if dockerRuns(r.calls) != 0 {
			t.Fatalf("%s: a container was started: %v", c.name, r.calls)
		}
	}
}

// Every later verb works from the state directory alone: the credentials
// file gone and the environment variable garbage change nothing.
func TestLaterVerbsNeedNoCredentials(t *testing.T) {
	argument := privateFile(t, t.TempDir(), "account.yml", "email: a@example.com\npassword: secret-a\n", 0600)
	sim := newContainerSim()
	d := newTestDriver(t, nil)
	d.runner = sim
	writeArtifacts(t, d)
	if _, stderr, code := runVerb(t, d, "setup", argument); code != 0 {
		t.Fatal(stderr)
	}
	if err := os.Remove(argument); err != nil {
		t.Fatal(err)
	}
	t.Setenv(credentialsEnv, "garbage")
	sim.trees = []string{homeTree(connectText, true, false)}
	sim.tunnel, sim.egress = "down", "ip 203.0.113.9"
	for _, args := range [][]string{{"observe"}, {"connect"}, {"direct-egress"}, {"egress"}, {"traffic"}, {"kill-switch", "off"}, {"teardown"}} {
		if _, stderr, code := runVerb(t, d, args...); code != 0 {
			t.Fatalf("%v: %s", args, stderr)
		}
	}
	if !strings.Contains(strings.Join(sim.calls, "\n"), "docker rm -f "+d.containerName()) {
		t.Fatal("teardown did not remove the signed-in container")
	}
}

// Case 1 and case 2 each get a fresh account and state directory: case 2
// mounts and types its own account in its own container after case 1's
// teardown removed case 1's.
func TestEachCaseSignsInFreshWithItsOwnAccount(t *testing.T) {
	accounts := t.TempDir()
	linuxDir := t.TempDir()
	cases := []struct{ file, email, password string }{
		{privateFile(t, accounts, "a.yml", "email: a@example.com\npassword: secret-a\n", 0600), "a@example.com", "secret-a"},
		{privateFile(t, accounts, "b.yml", "email: b@example.com\npassword: secret-b\n", 0600), "b@example.com", "secret-b"},
	}
	var containers []string
	for i, c := range cases {
		sim := newContainerSim()
		d := newTestDriver(t, nil)
		d.linuxDir, d.outDir, d.runner = linuxDir, filepath.Join(linuxDir, "out"), sim
		writeArtifacts(t, d)
		if _, stderr, code := runVerb(t, d, "setup", c.file); code != 0 {
			t.Fatalf("case %d setup: %s", i+1, stderr)
		}
		if sim.mounted != c.file || sim.typed["email"] != c.email || sim.typed["password"] != c.password {
			t.Fatalf("case %d mounted %q typed %v", i+1, sim.mounted, sim.typed)
		}
		if _, stderr, code := runVerb(t, d, "teardown"); code != 0 {
			t.Fatalf("case %d teardown: %s", i+1, stderr)
		}
		if !strings.Contains(strings.Join(sim.calls, "\n"), "docker rm -f "+d.containerName()) {
			t.Fatalf("case %d teardown left its container", i+1)
		}
		if _, err := os.Stat(d.statePath()); !errors.Is(err, os.ErrNotExist) {
			t.Fatalf("case %d teardown left its state", i+1)
		}
		containers = append(containers, d.containerName())
	}
	if containers[0] == containers[1] {
		t.Fatal("both cases used one container")
	}
}
