// The Linux driver for MAIN's insufficient-balance case
// (tests/runner/RUN-MAIN.md "Driver protocol"). Each invocation runs one verb
// and prints exactly one JSON object on stdout; a failure exits nonzero with
// one redacted line on stderr.
//
// The app under test is the real user surface: the locally built daemon .deb
// and GUI AppImage that MAIN's linux/test-main.sh builds into out/acceptance,
// installed into one disposable privileged arm64 container (the data-plane
// container shape of test-main.sh, on the existing
// urnetwork-linux-builder-gui image plus the AT-SPI, keyring and X tools in
// Dockerfile). Inside it, container.sh runs urnetworkd in a child cgroup
// exactly as build/all/acceptance/run-linux.sh does, an Xvfb display, a session
// bus with a Secret Service, a fake org.freedesktop.Notifications server
// (notifications.py) and the GUI. The GUI is driven only through its
// accessibility tree (atspi.py over AT-SPI), as a user would: sign in with
// email and password, Connect, the kill switch, and the Disconnect in the
// insufficient-balance alert.
//
// Elements are found by role and English accessible name with the GUI pinned
// to the C locale (its gettext sources are the English strings). GTK 4.14,
// the Ubuntu 24.04 floor, reports an empty AccessibleId and exposes no widget
// name over AT-SPI, so a role and name, anchored structurally, is the stable
// handle the toolkit offers.
//
// Egress probes and bulk traffic come from curl in the container's own cgroup,
// i.e. a non-app process whose sockets the daemon does not mark: connected,
// they traverse the tunnel; held, they fail; released, they use the direct
// path.
//
// Credentials reach this program only as setup's one argument: the absolute
// path of a private file the runner creates for this case's fresh account
// (URNETWORK_INSUFFICIENT_BALANCE_CREDENTIALS and the vault are never read).
// setup checks the file's shape and permissions but never reads the values
// into memory it prints, and mounts it read-only into the case's container;
// container.sh copies it privately to the GUI user, and atspi.py types the
// values into the GUI. Later verbs need no credentials: they act on the
// signed-in container recorded in the state directory, and teardown removes
// that container with the session and the copy, so the next case's setup signs
// in fresh with its own account.
//
// The verb logic is in pure functions over a commandRunner and a clock so
// main_test.go exercises it with fakes. Build-free: go run, standard library.
package main

import (
	"bufio"
	"bytes"
	"context"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net"
	"os"
	"os/exec"
	"path/filepath"
	"strconv"
	"strings"
	"time"
)

// English accessible names (gettext msgids) the driver looks for.
const (
	heldNoticeText      = "Your traffic is held in the tunnel until you upgrade or disconnect."
	alertTitleText      = "Insufficient balance"
	disconnectText      = "Disconnect"
	connectText         = "Connect"
	selectedProviderTag = "Selected provider"
	killSwitchText      = "Kill switch"
	moreOptionsText     = "More options"
	loginPlaceholder    = "Enter your email or phone number"
	getStartedText      = "Get started"
	passwordPlaceholder = "Password"
	signInText          = "Sign in"
	skipText            = "Skip"
	freePlanText        = "Continue with the free plan"
)

// Upgrade affordances that may sit next to Disconnect in the alert.
var upgradeTexts = []string{"Upgrade", "Get Pro", "Upgrade to Pro"}

// Sign-in failures the GUI shows in place; reported verbatim (no secrets).
var signInErrorTexts = []string{
	"Sign in failed",
	"There was an error logging in",
	"Something went wrong.",
}

const (
	baseImage     = "urnetwork-linux-builder-gui:arm64"
	driverImage   = "urnetwork-linux-insufficient-balance:arm64"
	containerHome = "/opt/ib"
	stateFileName = "linux-driver.json"
	// GUI sign-in and first home render, inside setup's 10 min budget
	signInTimeout = 5 * time.Minute
	uiPoll        = 2 * time.Second
	// a kill-switch or disconnect press is reflected in the tree within this
	settleTimeout = 15 * time.Second
)

// ---- process and clock seams ----

type commandRunner interface {
	// Run returns stdout. A nonzero exit is an error carrying the last stderr
	// line, bounded.
	Run(ctx context.Context, name string, args ...string) ([]byte, error)
}

type execRunner struct{}

func (execRunner) Run(ctx context.Context, name string, args ...string) ([]byte, error) {
	cmd := exec.CommandContext(ctx, name, args...)
	cmd.WaitDelay = 5 * time.Second
	var stdout, stderr bytes.Buffer
	cmd.Stdout, cmd.Stderr = &stdout, &stderr
	if err := cmd.Run(); err != nil {
		return stdout.Bytes(), fmt.Errorf("%s %s: %v: %s", name, firstArg(args), err, bounded(lastLine(stderr.String()), 200))
	}
	return stdout.Bytes(), nil
}

func firstArg(args []string) string {
	if len(args) == 0 {
		return ""
	}
	return args[0]
}

type clock interface {
	Now() time.Time
	Sleep(ctx context.Context, d time.Duration) error
}

type realClock struct{}

func (realClock) Now() time.Time { return time.Now() }
func (realClock) Sleep(ctx context.Context, d time.Duration) error {
	t := time.NewTimer(d)
	defer t.Stop()
	select {
	case <-ctx.Done():
		return ctx.Err()
	case <-t.C:
		return nil
	}
}

// ---- accessibility tree ----

// accessibleNode is one atspi.py tree node. Path is the child-index path from
// the application root, which atspi.py resolves again for an action.
type accessibleNode struct {
	Role       string            `json:"role"`
	Name       string            `json:"name"`
	States     []string          `json:"states"`
	Attributes map[string]string `json:"attributes"`
	Actions    []string          `json:"actions"`
	Path       []int             `json:"path"`
	Children   []*accessibleNode `json:"children"`
	parent     *accessibleNode
}

func parseTree(b []byte) (*accessibleNode, error) {
	var root accessibleNode
	if err := json.Unmarshal(b, &root); err != nil {
		return nil, errors.New("accessibility tree is not valid JSON")
	}
	var link func(n *accessibleNode)
	link = func(n *accessibleNode) {
		for _, c := range n.Children {
			c.parent = n
			link(c)
		}
	}
	link(&root)
	return &root, nil
}

func (self *accessibleNode) has(state string) bool {
	for _, s := range self.States {
		if s == state {
			return true
		}
	}
	return false
}

// showing: mapped and on screen, i.e. what the user can see.
func (self *accessibleNode) showing() bool { return self.has("showing") && self.has("visible") }

func (self *accessibleNode) isButton() bool {
	return self.Role == "push button" || self.Role == "button"
}

func (self *accessibleNode) isSwitch() bool {
	return self.Role == "toggle button" || self.Role == "switch" || self.Role == "check box"
}

func (self *accessibleNode) checked() bool { return self.has("checked") || self.has("pressed") }

func (self *accessibleNode) walk(fn func(*accessibleNode) bool) bool {
	if !fn(self) {
		return false
	}
	for _, c := range self.Children {
		if !c.walk(fn) {
			return false
		}
	}
	return true
}

func (self *accessibleNode) find(match func(*accessibleNode) bool) *accessibleNode {
	var found *accessibleNode
	self.walk(func(n *accessibleNode) bool {
		if match(n) {
			found = n
			return false
		}
		return true
	})
	return found
}

func (self *accessibleNode) contains(other *accessibleNode) bool {
	for n := other; n != nil; n = n.parent {
		if n == self {
			return true
		}
	}
	return false
}

func pathArg(path []int) string {
	parts := make([]string, len(path))
	for i, p := range path {
		parts[i] = strconv.Itoa(p)
	}
	return strings.Join(parts, ",")
}

// connectActionButton is the round connect button of the Connect page: the
// button reading Connect or Disconnect that sits beside the "Selected provider"
// row. The nav item named Connect has no such sibling.
func connectActionButton(root *accessibleNode) *accessibleNode {
	return root.find(func(n *accessibleNode) bool {
		if !n.isButton() || !n.showing() || (n.Name != connectText && n.Name != disconnectText) || n.parent == nil {
			return false
		}
		for _, sibling := range n.parent.Children {
			if strings.HasPrefix(sibling.Name, selectedProviderTag) {
				return true
			}
		}
		return false
	})
}

// alertState is what the insufficient-balance alert shows.
type alertState struct {
	alert      bool
	disconnect *accessibleNode
	upgrade    *accessibleNode
}

// The alert is the block holding the held notice (on the Connect page,
// ConnectPage's heldAlert_: the notice label and an action row with Upgrade
// and Disconnect). Its Disconnect counts only as the sibling of an Upgrade in
// one row inside that block, so the round connect button (also "Disconnect"
// while a session is up) and any button elsewhere on the page never do. The
// legacy drawer's "Insufficient balance" heading alone is not the alert.
// upgrade_visible is any Upgrade inside the block.
func findAlert(root *accessibleNode) alertState {
	anchor := root.find(func(n *accessibleNode) bool { return n.showing() && n.Name == heldNoticeText })
	if anchor == nil {
		return alertState{}
	}
	state := alertState{alert: true}
	block := anchor.parent
	if block == nil {
		return state
	}
	round := connectActionButton(root)
	block.walk(func(row *accessibleNode) bool {
		var upgrade, disconnect *accessibleNode
		for _, n := range row.Children {
			if n == round || !n.isButton() || !n.showing() {
				continue
			}
			if n.Name == disconnectText && disconnect == nil {
				disconnect = n
			}
			for _, u := range upgradeTexts {
				if n.Name == u && upgrade == nil {
					upgrade = n
				}
			}
		}
		if upgrade != nil && disconnect != nil {
			state.upgrade, state.disconnect = upgrade, disconnect
			return false
		}
		return true
	})
	if state.upgrade == nil {
		// Upgrade alone in the block: reported, so a missing Disconnect is
		// named as such
		state.upgrade = block.find(func(n *accessibleNode) bool {
			if !n.isButton() || !n.showing() {
				return false
			}
			for _, u := range upgradeTexts {
				if n.Name == u {
					return true
				}
			}
			return false
		})
	}
	return state
}

// ---- notifications ----

// notificationRecord is one notifications.py log line.
type notificationRecord struct {
	Method  string `json:"method"`
	Summary string `json:"summary"`
	Body    string `json:"body"`
}

// countBalanceNotifications counts Notify calls for the insufficient-balance
// notice. A re-post that replaces the first one is a second post and counts:
// the contract is one post per episode. CloseNotification (withdraw) does not.
func countBalanceNotifications(log []byte) (int, error) {
	count := 0
	s := bufio.NewScanner(bytes.NewReader(log))
	s.Buffer(make([]byte, 64<<10), 1<<20)
	for s.Scan() {
		line := strings.TrimSpace(s.Text())
		if line == "" {
			continue
		}
		var r notificationRecord
		if err := json.Unmarshal([]byte(line), &r); err != nil {
			return 0, errors.New("notification log has a malformed line")
		}
		if r.Method == "Notify" && (r.Summary == alertTitleText || r.Body == heldNoticeText) {
			count++
		}
	}
	return count, s.Err()
}

// ---- egress ----

// egressOutput is the protocol's {"ip"} or {"error"}.
type egressOutput struct {
	Ip    string `json:"ip,omitempty"`
	Error string `json:"error,omitempty"`
}

// parseEgress reads container.sh's probe line: "ip <addr>" or "error <why>".
// Anything else is a driver fault, never an address.
func parseEgress(b []byte) (egressOutput, error) {
	line := strings.TrimSpace(string(b))
	switch {
	case strings.HasPrefix(line, "ip "):
		ip := strings.TrimSpace(strings.TrimPrefix(line, "ip "))
		if net.ParseIP(ip) == nil {
			return egressOutput{}, errors.New("egress probe returned an invalid address")
		}
		return egressOutput{Ip: ip}, nil
	case strings.HasPrefix(line, "error"):
		why := strings.TrimSpace(strings.TrimPrefix(line, "error"))
		if why == "" {
			why = "probe failed"
		}
		return egressOutput{Error: bounded(why, 200)}, nil
	}
	return egressOutput{}, errors.New("egress probe printed no result")
}

// ---- credentials ----

// checkCredentials validates setup's argument: an absolute path, mountable
// as is, to a private regular file (mode 0600-like) with exactly one email and
// one password. It never returns or prints the values.
func checkCredentials(path string) error {
	if !filepath.IsAbs(path) {
		return errors.New("the insufficient-balance credentials file path must be absolute")
	}
	if strings.Contains(path, ":") {
		// docker run -v separates fields with ':'
		return errors.New("the insufficient-balance credentials file path must not contain ':'")
	}
	info, err := os.Lstat(path)
	if err != nil {
		return errors.New("insufficient-balance credentials file is missing")
	}
	if !info.Mode().IsRegular() || info.Mode().Perm()&0077 != 0 {
		return errors.New("insufficient-balance credentials file must be a private regular file")
	}
	f, err := os.Open(path)
	if err != nil {
		return errors.New("insufficient-balance credentials file is not readable")
	}
	defer f.Close()
	seen := map[string]bool{}
	s := bufio.NewScanner(io.LimitReader(f, 64<<10))
	for s.Scan() {
		line := strings.TrimSpace(s.Text())
		if line == "" || strings.HasPrefix(line, "#") {
			continue
		}
		key, value, ok := strings.Cut(line, ":")
		key = strings.TrimSpace(key)
		if !ok || (key != "email" && key != "password") || seen[key] || strings.TrimSpace(value) == "" {
			return errors.New("insufficient-balance credentials file must hold exactly one email and one password")
		}
		seen[key] = true
	}
	if s.Err() != nil || !seen["email"] || !seen["password"] {
		return errors.New("insufficient-balance credentials file must hold exactly one email and one password")
	}
	return nil
}

// ---- driver ----

type driverState struct {
	Container            string `json:"container"`
	NotificationBaseline int    `json:"notification_baseline"`
}

type driver struct {
	linuxDir string
	stateDir string
	outDir   string
	version  string
	runner   commandRunner
	clock    clock
}

func (self *driver) statePath() string { return filepath.Join(self.stateDir, stateFileName) }

func (self *driver) loadState() (driverState, error) {
	var s driverState
	b, err := os.ReadFile(self.statePath())
	if errors.Is(err, os.ErrNotExist) {
		return s, nil
	}
	if err != nil {
		return s, err
	}
	if err := json.Unmarshal(b, &s); err != nil {
		return s, errors.New("driver state is corrupt")
	}
	return s, nil
}

func (self *driver) saveState(s driverState) error {
	b, _ := json.Marshal(s)
	return os.WriteFile(self.statePath(), b, 0600)
}

// containerName is stable per state directory, so a rerun replaces its own
// leftover container and never another run's.
func (self *driver) containerName() string {
	sum := sha256.Sum256([]byte(self.stateDir))
	return "urnetwork-insufficient-balance-" + hex.EncodeToString(sum[:])[:12]
}

func (self *driver) requireContainer() (driverState, error) {
	s, err := self.loadState()
	if err == nil && s.Container == "" {
		err = errors.New("setup has not run")
	}
	return s, err
}

func (self *driver) inContainer(ctx context.Context, container string, args ...string) ([]byte, error) {
	return self.runner.Run(ctx, "docker", append([]string{"exec", container, containerHome + "/container.sh"}, args...)...)
}

func (self *driver) tree(ctx context.Context, container string) (*accessibleNode, error) {
	b, err := self.inContainer(ctx, container, "atspi", "tree")
	if err != nil {
		return nil, err
	}
	return parseTree(b)
}

func (self *driver) act(ctx context.Context, container string, n *accessibleNode, action string) error {
	_, err := self.inContainer(ctx, container, "atspi", "action", pathArg(n.Path), action)
	return err
}

func (self *driver) tunnelUp(ctx context.Context, container string) (bool, error) {
	b, err := self.inContainer(ctx, container, "tunnel")
	if err != nil {
		return false, err
	}
	switch strings.TrimSpace(string(b)) {
	case "up":
		return true, nil
	case "down":
		return false, nil
	}
	return false, errors.New("tunnel state is unreadable")
}

func (self *driver) notifications(ctx context.Context, container string) (int, error) {
	b, err := self.inContainer(ctx, container, "notifications")
	if err != nil {
		return 0, err
	}
	return countBalanceNotifications(b)
}

// artifacts are the MAIN linux build outputs (linux/test-main.sh defaults).
func (self *driver) artifacts() (deb, appImage string, err error) {
	deb = filepath.Join(self.outDir, "urnetwork-daemon_"+self.version+"_arm64.deb")
	appImage = filepath.Join(self.outDir, "URnetwork-"+self.version+"-arm64.AppImage")
	for _, p := range []string{deb, appImage} {
		if info, statErr := os.Stat(p); statErr != nil || !info.Mode().IsRegular() {
			return "", "", fmt.Errorf("missing locally built %s; run linux/test-main.sh (MAIN builds it) first", filepath.Base(p))
		}
	}
	return deb, appImage, nil
}

// setup signs the case's account in. credentials is setup's argument, the only
// source of the account; it is mounted, never copied on the host.
func (self *driver) setup(ctx context.Context, credentials string) (map[string]any, error) {
	if err := checkCredentials(credentials); err != nil {
		return nil, err
	}
	deb, appImage, err := self.artifacts()
	if err != nil {
		return nil, err
	}
	if _, err := self.runner.Run(ctx, "docker", "image", "inspect", baseImage); err != nil {
		return nil, fmt.Errorf("the Linux GUI build image %s is missing; run build/all/linux/setup.sh", baseImage)
	}
	contextDir := filepath.Join(self.linuxDir, "tests", "insufficient-balance-driver")
	if _, err := self.runner.Run(ctx, "docker", "build", "--platform", "linux/arm64",
		"--build-arg", "BASE="+baseImage, "-t", driverImage, contextDir); err != nil {
		return nil, fmt.Errorf("build driver image: %w", err)
	}
	container := self.containerName()
	// a leftover from an interrupted run of this same state directory
	self.runner.Run(ctx, "docker", "rm", "-f", container)
	logs := filepath.Join(self.stateDir, "container")
	if err := os.MkdirAll(logs, 0700); err != nil {
		return nil, err
	}
	// The data-plane container shape of linux/test-main.sh: host cgroup
	// namespace and the privileged profile the daemon's cgroup-BPF egress mark
	// and nftables need. Disposable, local, and removed by teardown.
	if _, err := self.runner.Run(ctx, "docker", "run", "-d", "--init", "--name", container,
		"--platform", "linux/arm64",
		"--cgroupns=host", "--privileged",
		"--cap-add", "NET_ADMIN", "--device", "/dev/net/tun",
		"-v", self.outDir+":/out:ro",
		"-v", contextDir+":"+containerHome+":ro",
		"-v", credentials+":/opt/ib-private/credentials:ro",
		"-v", logs+":/artifacts",
		"-e", "UR_IB_DEB=/out/"+filepath.Base(deb),
		"-e", "UR_IB_APPIMAGE=/out/"+filepath.Base(appImage),
		"-e", "UR_IB_VERSION="+self.version,
		driverImage, "sleep", "infinity"); err != nil {
		return nil, fmt.Errorf("start container: %w", err)
	}
	state := driverState{Container: container}
	if err := self.saveState(state); err != nil {
		return nil, err
	}
	if _, err := self.inContainer(ctx, container, "start"); err != nil {
		return nil, fmt.Errorf("start daemon and GUI: %w", err)
	}
	if err := self.signIn(ctx, container); err != nil {
		return nil, err
	}

	// leave it disconnected with the kill switch off
	root, err := self.tree(ctx, container)
	if err != nil {
		return nil, err
	}
	if button := connectActionButton(root); button != nil && button.Name == disconnectText {
		if err := self.act(ctx, container, button, "click"); err != nil {
			return nil, err
		}
	}
	killSwitch, err := self.findKillSwitch(ctx, container)
	if err != nil {
		return nil, err
	}
	if killSwitch != nil && killSwitch.checked() {
		if err := self.setKillSwitch(ctx, container, false); err != nil {
			return nil, err
		}
	}
	count, err := self.notifications(ctx, container)
	if err != nil {
		return nil, err
	}
	state.NotificationBaseline = count
	if err := self.saveState(state); err != nil {
		return nil, err
	}
	return map[string]any{"kill_switch_supported": killSwitch != nil}, nil
}

// signIn walks the GUI's email/password sign-in until the Connect page shows.
// Each screen is recognised from the tree; values are typed by atspi.py from
// the private copy of the credentials file.
func (self *driver) signIn(ctx context.Context, container string) error {
	deadline := self.clock.Now().Add(signInTimeout)
	typedEmail, typedPassword := false, false
	for {
		root, err := self.tree(ctx, container)
		if err != nil {
			return err
		}
		if connectActionButton(root) != nil {
			return nil
		}
		if msg := shownSignInError(root); msg != "" {
			return fmt.Errorf("sign-in failed in the GUI: %s", msg)
		}
		if root.find(func(n *accessibleNode) bool { return n.showing() && n.Name == "You've got mail" }) != nil {
			return errors.New("the acceptance account requires verification; it must be a verified account")
		}
		password := root.find(func(n *accessibleNode) bool {
			return n.showing() && (n.Role == "password text" || n.Attributes["placeholder-text"] == passwordPlaceholder)
		})
		email := root.find(func(n *accessibleNode) bool {
			return n.showing() && n.Attributes["placeholder-text"] == loginPlaceholder
		})
		onboarding := root.find(func(n *accessibleNode) bool {
			return n.isButton() && n.showing() && (n.Name == skipText || n.Name == freePlanText)
		})
		switch {
		case password != nil && !typedPassword:
			if err := self.typeInto(ctx, container, password, "password"); err != nil {
				return err
			}
			if button := root.find(func(n *accessibleNode) bool { return n.isButton() && n.showing() && n.Name == signInText }); button != nil {
				if err := self.act(ctx, container, button, "click"); err != nil {
					return err
				}
			}
			typedPassword = true
		case email != nil && !typedEmail && password == nil:
			if err := self.typeInto(ctx, container, email, "email"); err != nil {
				return err
			}
			button := root.find(func(n *accessibleNode) bool { return n.isButton() && n.showing() && n.Name == getStartedText })
			if button == nil {
				return errors.New("sign-in screen has no Get started button")
			}
			if err := self.act(ctx, container, button, "click"); err != nil {
				return err
			}
			typedEmail = true
		case onboarding != nil:
			if err := self.act(ctx, container, onboarding, "click"); err != nil {
				return err
			}
		}
		if !self.clock.Now().Before(deadline) {
			return fmt.Errorf("the GUI did not reach the Connect page within %s of sign-in; showing: %s", signInTimeout, bounded(showingButtons(root), 200))
		}
		if err := self.clock.Sleep(ctx, uiPoll); err != nil {
			return err
		}
	}
}

func (self *driver) typeInto(ctx context.Context, container string, n *accessibleNode, key string) error {
	if _, err := self.inContainer(ctx, container, "atspi", "focus", pathArg(n.Path)); err != nil {
		return err
	}
	_, err := self.inContainer(ctx, container, "atspi", "type-credential", key)
	return err
}

func shownSignInError(root *accessibleNode) string {
	var msg string
	root.walk(func(n *accessibleNode) bool {
		if !n.showing() {
			return true
		}
		for _, e := range signInErrorTexts {
			if n.Name == e {
				msg = e
				return false
			}
		}
		return true
	})
	return msg
}

// showingButtons names what is on screen for a stuck-screen diagnostic.
func showingButtons(root *accessibleNode) string {
	var names []string
	root.walk(func(n *accessibleNode) bool {
		if n.isButton() && n.showing() && n.Name != "" {
			names = append(names, n.Name)
		}
		return true
	})
	return strings.Join(names, ", ")
}

func (self *driver) findKillSwitch(ctx context.Context, container string) (*accessibleNode, error) {
	match := func(n *accessibleNode) bool { return n.isSwitch() && n.Name == killSwitchText }
	root, err := self.tree(ctx, container)
	if err != nil {
		return nil, err
	}
	sw := root.find(func(n *accessibleNode) bool { return match(n) && n.showing() })
	if sw != nil {
		return sw, nil
	}
	// Simple mode collapses the options behind "More options"
	more := root.find(func(n *accessibleNode) bool { return n.isButton() && n.showing() && n.Name == moreOptionsText })
	if more == nil {
		return nil, nil
	}
	if err := self.act(ctx, container, more, "click"); err != nil {
		return nil, err
	}
	root, err = self.tree(ctx, container)
	if err != nil {
		return nil, err
	}
	return root.find(func(n *accessibleNode) bool { return match(n) && n.showing() }), nil
}

func (self *driver) setKillSwitch(ctx context.Context, container string, on bool) error {
	sw, err := self.findKillSwitch(ctx, container)
	if err != nil {
		return err
	}
	if sw == nil {
		return errors.New("the Connect page has no Kill switch")
	}
	if sw.checked() == on {
		return nil
	}
	if err := self.act(ctx, container, sw, "toggle"); err != nil {
		return err
	}
	deadline := self.clock.Now().Add(settleTimeout)
	for {
		sw, err = self.findKillSwitch(ctx, container)
		if err != nil {
			return err
		}
		if sw != nil && sw.checked() == on {
			return nil
		}
		if !self.clock.Now().Before(deadline) {
			return fmt.Errorf("the Kill switch did not turn %s", onOff(on))
		}
		if err := self.clock.Sleep(ctx, time.Second); err != nil {
			return err
		}
	}
}

func onOff(on bool) string {
	if on {
		return "on"
	}
	return "off"
}

// observation is the protocol's observe object.
type observation struct {
	ConnectRequested bool `json:"connect_requested"`
	Connected        bool `json:"connected"`
	Alert            bool `json:"insufficient_balance_alert"`
	DisconnectButton bool `json:"disconnect_visible"`
	UpgradeButton    bool `json:"upgrade_visible"`
	Notifications    int  `json:"insufficient_balance_notifications"`
}

// observe: connected is the tunnel link (urnet0) existing, which the daemon
// keeps while a connection is requested, held or not. connect_requested is the
// GUI's own intent: the round button offering Disconnect, or the tunnel still
// capturing traffic.
func (self *driver) observe(ctx context.Context) (observation, error) {
	state, err := self.requireContainer()
	if err != nil {
		return observation{}, err
	}
	root, err := self.tree(ctx, state.Container)
	if err != nil {
		return observation{}, err
	}
	up, err := self.tunnelUp(ctx, state.Container)
	if err != nil {
		return observation{}, err
	}
	count, err := self.notifications(ctx, state.Container)
	if err != nil {
		return observation{}, err
	}
	round := connectActionButton(root)
	alert := findAlert(root)
	return observation{
		ConnectRequested: up || (round != nil && round.Name == disconnectText),
		Connected:        up,
		Alert:            alert.alert,
		DisconnectButton: alert.disconnect != nil,
		UpgradeButton:    alert.upgrade != nil,
		Notifications:    count - state.NotificationBaseline,
	}, nil
}

func (self *driver) connect(ctx context.Context) error {
	state, err := self.requireContainer()
	if err != nil {
		return err
	}
	root, err := self.tree(ctx, state.Container)
	if err != nil {
		return err
	}
	button := connectActionButton(root)
	if button == nil {
		return errors.New("the Connect page's connect button is not showing")
	}
	if button.Name == disconnectText {
		return nil
	}
	return self.act(ctx, state.Container, button, "click")
}

// pressDisconnect presses only the Disconnect inside the alert. Falling back
// to the round button would hide exactly the gap the case measures.
func (self *driver) pressDisconnect(ctx context.Context) error {
	state, err := self.requireContainer()
	if err != nil {
		return err
	}
	root, err := self.tree(ctx, state.Container)
	if err != nil {
		return err
	}
	alert := findAlert(root)
	if alert.disconnect == nil {
		return errors.New("no Disconnect button next to Upgrade in the insufficient balance alert")
	}
	return self.act(ctx, state.Container, alert.disconnect, "click")
}

func (self *driver) egress(ctx context.Context) (egressOutput, error) {
	state, err := self.requireContainer()
	if err != nil {
		return egressOutput{}, err
	}
	b, err := self.inContainer(ctx, state.Container, "egress")
	if err != nil {
		return egressOutput{}, err
	}
	return parseEgress(b)
}

func (self *driver) traffic(ctx context.Context) error {
	state, err := self.requireContainer()
	if err != nil {
		return err
	}
	_, err = self.inContainer(ctx, state.Container, "traffic")
	return err
}

// teardown removes the container (and with it the install, the signed-in
// session, its keyring and the private credentials copy, plus the mount of the
// runner's file), so the next case's setup signs in fresh with a different
// account. It never touches the account itself, and succeeds when there is
// nothing to remove.
func (self *driver) teardown(ctx context.Context) error {
	state, err := self.loadState()
	if err != nil {
		return err
	}
	container := state.Container
	if container == "" {
		container = self.containerName()
	}
	b, inspectErr := self.runner.Run(ctx, "docker", "ps", "-aq", "--filter", "name=^/"+container+"$")
	if inspectErr != nil {
		return fmt.Errorf("inspect container: %w", inspectErr)
	}
	if strings.TrimSpace(string(b)) != "" {
		if _, err := self.runner.Run(ctx, "docker", "rm", "-f", container); err != nil {
			return fmt.Errorf("remove container: %w", err)
		}
	}
	if err := os.Remove(self.statePath()); err != nil && !errors.Is(err, os.ErrNotExist) {
		return err
	}
	return nil
}

// dispatch runs one verb and returns its JSON object.
func (self *driver) dispatch(ctx context.Context, args []string) (any, error) {
	if len(args) == 0 {
		return nil, errors.New("usage: test-insufficient-balance-driver <verb> [arg]")
	}
	verb, rest := args[0], args[1:]
	if verb == "setup" && len(rest) != 1 {
		return nil, errors.New("setup takes exactly one argument: the absolute path of the insufficient-balance credentials file")
	}
	arity := map[string]int{"setup": 1, "direct-egress": 0, "connect": 0, "observe": 0, "egress": 0,
		"traffic": 0, "press-disconnect": 0, "kill-switch": 1, "teardown": 0}
	n, known := arity[verb]
	if !known || len(rest) != n {
		return nil, fmt.Errorf("unknown verb or arguments: %s", bounded(verb, 40))
	}
	empty := map[string]any{}
	switch verb {
	case "setup":
		return self.setup(ctx, rest[0])
	case "direct-egress":
		e, err := self.egress(ctx)
		if err == nil && e.Ip == "" {
			err = fmt.Errorf("direct egress probe failed: %s", e.Error)
		}
		return e, err
	case "connect":
		return empty, self.connect(ctx)
	case "observe":
		return self.observe(ctx)
	case "egress":
		return self.egress(ctx)
	case "traffic":
		return empty, self.traffic(ctx)
	case "press-disconnect":
		return empty, self.pressDisconnect(ctx)
	case "kill-switch":
		if rest[0] != "on" && rest[0] != "off" {
			return nil, errors.New("kill-switch takes on or off")
		}
		state, err := self.requireContainer()
		if err != nil {
			return nil, err
		}
		return empty, self.setKillSwitch(ctx, state.Container, rest[0] == "on")
	case "teardown":
		return empty, self.teardown(ctx)
	}
	return nil, errors.New("unreachable")
}

// run is main without the process: one JSON object on stdout, or one
// redacted stderr line and exit 1.
func run(ctx context.Context, d *driver, args []string, stdout, stderr io.Writer) int {
	out, err := d.dispatch(ctx, args)
	if err != nil {
		fmt.Fprintln(stderr, "linux driver:", redact(err.Error()))
		return 1
	}
	b, err := json.Marshal(out)
	if err != nil {
		fmt.Fprintln(stderr, "linux driver: encode result")
		return 1
	}
	stdout.Write(append(b, '\n'))
	return 0
}

// redact keeps the stderr contract: one bounded line. No message is built from
// credential values, so this only flattens and bounds.
func redact(s string) string {
	return bounded(strings.Join(strings.Fields(s), " "), 300)
}

func bounded(s string, n int) string {
	if len(s) <= n {
		return s
	}
	return s[:n] + "..."
}

func lastLine(s string) string {
	lines := strings.Split(strings.TrimSpace(s), "\n")
	return lines[len(lines)-1]
}

func newDriverFromEnv(args []string) (*driver, []string, error) {
	if len(args) < 2 || args[0] != "--linux" || !filepath.IsAbs(args[1]) {
		return nil, nil, errors.New("run through linux/test-insufficient-balance-driver")
	}
	linuxDir, rest := args[1], args[2:]
	// credentials arrive only as setup's argument, never from the environment
	stateDir := os.Getenv("URNETWORK_INSUFFICIENT_BALANCE_STATE")
	if !filepath.IsAbs(stateDir) {
		return nil, nil, errors.New("URNETWORK_INSUFFICIENT_BALANCE_STATE must be absolute")
	}
	outDir := os.Getenv("UR_ACCEPT_LINUX_OUT")
	if outDir == "" {
		outDir = filepath.Join(linuxDir, "out", "acceptance")
	}
	version := os.Getenv("EXTERNAL_WARP_VERSION")
	if version == "" {
		version = "0.0.0-0"
	}
	if strings.Trim(version, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.+-") != "" {
		return nil, nil, errors.New("EXTERNAL_WARP_VERSION contains unsupported characters")
	}
	return &driver{
		linuxDir: linuxDir,
		stateDir: stateDir,
		outDir:   outDir,
		version:  version,
		runner:   execRunner{},
		clock:    realClock{},
	}, rest, nil
}

func main() {
	d, args, err := newDriverFromEnv(os.Args[1:])
	if err != nil {
		fmt.Fprintln(os.Stderr, "linux driver:", err)
		os.Exit(2)
	}
	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Minute)
	defer cancel()
	os.Exit(run(ctx, d, args, os.Stdout, os.Stderr))
}
