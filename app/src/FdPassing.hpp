// FdPassing — open file descriptors over the control socket (SCM_RIGHTS), from
// the GUI to urnetworkd. One use: the GUI's own log files ride with an
// upload_logs request (PassedLogFiles.hpp). The daemon is root and never opens
// a path a client names, so the GUI opens its files with its own rights and
// hands over the descriptors instead.
//
// On an AF_UNIX stream socket the descriptors attach to the bytes of the one
// sendmsg that carries them, and the receiver's recvmsg returns them with the
// first of those bytes, never with an earlier write's: a reader that returns
// descriptors stops after the segment that carried them. So the descriptors
// arrive no later than the first byte of the frame they were sent with.
//
// Plain POSIX, no glib and no SDK, so the unit tests run it over a socketpair
// on any host (PassedLogFilesTest.cpp).
//
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <fcntl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>
#include <cstring>
#include <vector>

namespace urnw::fdpass {

// The most descriptors one message carries, and the most a connection may hold
// before a request claims them.
inline constexpr size_t kMaxFds = 16;

// Sends `size` bytes of `data` on the stream socket `socketFd` in one sendmsg,
// with `fds` attached to them (SCM_RIGHTS), and returns what sendmsg returns:
// the bytes sent, or -1 with errno set. The caller sends any rest of the data
// without descriptors. The kernel holds its own references to `fds` once this
// returns, so the caller may close its copies then. With no descriptors this
// is a plain send. More than kMaxFds is refused (EINVAL), sending nothing.
inline ssize_t SendWithFds(int socketFd, const char* data, size_t size,
                           const std::vector<int>& fds, int flags) {
  if (fds.empty()) return ::send(socketFd, data, size, flags);
  if (fds.size() > kMaxFds || size == 0) {
    errno = EINVAL;
    return -1;
  }
  iovec iov{};
  iov.iov_base = const_cast<char*>(data);
  iov.iov_len = size;
  std::vector<char> control(CMSG_SPACE(sizeof(int) * fds.size()), 0);
  msghdr message{};
  message.msg_iov = &iov;
  message.msg_iovlen = 1;
  message.msg_control = control.data();
  message.msg_controllen = static_cast<decltype(message.msg_controllen)>(control.size());
  cmsghdr* header = CMSG_FIRSTHDR(&message);
  header->cmsg_level = SOL_SOCKET;
  header->cmsg_type = SCM_RIGHTS;
  header->cmsg_len = static_cast<decltype(header->cmsg_len)>(CMSG_LEN(sizeof(int) * fds.size()));
  std::memcpy(CMSG_DATA(header), fds.data(), sizeof(int) * fds.size());
  return ::sendmsg(socketFd, &message, flags);
}

// Receives up to `size` bytes from `socketFd` into `buffer`, as recv does, and
// appends the descriptors that came with them to `fds`, close-on-exec. They
// are the caller's to close from here. `truncated` (when set) says that more
// descriptors came than kMaxFds: the kernel closed the ones that did not fit.
inline ssize_t ReceiveWithFds(int socketFd, char* buffer, size_t size, std::vector<int>* fds,
                              bool* truncated) {
  if (truncated) *truncated = false;
  iovec iov{};
  iov.iov_base = buffer;
  iov.iov_len = size;
  alignas(cmsghdr) char control[CMSG_SPACE(sizeof(int) * kMaxFds)];
  msghdr message{};
  message.msg_iov = &iov;
  message.msg_iovlen = 1;
  message.msg_control = control;
  message.msg_controllen = sizeof(control);
  int flags = 0;
#ifdef MSG_CMSG_CLOEXEC
  flags |= MSG_CMSG_CLOEXEC;
#endif
  const ssize_t n = ::recvmsg(socketFd, &message, flags);
  if (n < 0) return n;
  if (truncated && (message.msg_flags & MSG_CTRUNC) != 0) *truncated = true;
  for (cmsghdr* header = CMSG_FIRSTHDR(&message); header != nullptr;
       header = CMSG_NXTHDR(&message, header)) {
    if (header->cmsg_level != SOL_SOCKET || header->cmsg_type != SCM_RIGHTS) continue;
    const size_t count = (header->cmsg_len - CMSG_LEN(0)) / sizeof(int);
    for (size_t i = 0; i < count; ++i) {
      int fd = -1;
      std::memcpy(&fd, CMSG_DATA(header) + i * sizeof(int), sizeof(int));
#ifndef MSG_CMSG_CLOEXEC
      ::fcntl(fd, F_SETFD, FD_CLOEXEC);
#endif
      if (fds) {
        fds->push_back(fd);
      } else {
        ::close(fd);
      }
    }
  }
  return n;
}

}  // namespace urnw::fdpass
