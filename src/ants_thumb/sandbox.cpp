#include "sandbox.hpp"

#if defined(__linux__)
#include <cerrno>
#include <cstddef>
#include <cstdint>

#include <linux/audit.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace ants_thumb {

#if defined(__linux__)

namespace {

// Landlock's system calls have the same numbers on every architecture; the structures and the bits are the kernel's published ABI (include/uapi/linux/landlock.h), written here so that the build does not
// depend on the version of the kernel headers it is built with
constexpr long kLandlockCreateRuleset = 444;
constexpr long kLandlockRestrictSelf = 446;
constexpr uint32_t kCreateRulesetVersion = 1u << 0;

struct RulesetAttr {
    uint64_t handled_access_fs;
    uint64_t handled_access_net;           // from ABI 4; a kernel that knows less is given the shorter size
};

// every file right that exists, by the ABI that introduced it: 1 execute .. make symlink (bits 0 .. 12), 2 refer (13), 3 truncate (14), 5 ioctl on devices (15)
uint64_t fs_rights(int abi) {
    uint64_t rights = (uint64_t{1} << 13) - 1;
    if (abi >= 2) rights |= uint64_t{1} << 13;
    if (abi >= 3) rights |= uint64_t{1} << 14;
    if (abi >= 5) rights |= uint64_t{1} << 15;
    return rights;
}

bool apply_landlock(Sandbox& out) {
    const long abi = syscall(kLandlockCreateRuleset, nullptr, size_t{0}, kCreateRulesetVersion);
    if (abi < 1) return false;                                          // (ENOSYS: no Landlock; EOPNOTSUPP: built without it or switched off)
    RulesetAttr attr{fs_rights(static_cast<int>(abi)), abi >= 4 ? uint64_t{3} : uint64_t{0}};       // (a ruleset with no rule at all: every handled right is refused)
    const size_t size = abi >= 4 ? sizeof attr : sizeof attr.handled_access_fs;
    const long fd = syscall(kLandlockCreateRuleset, &attr, size, uint32_t{0});
    if (fd < 0) return false;
    const long restricted = syscall(kLandlockRestrictSelf, static_cast<int>(fd), uint32_t{0});
    close(static_cast<int>(fd));
    if (restricted != 0) return false;
    out.landlock_abi = static_cast<int>(abi);
    out.landlock_network = abi >= 4;
    return true;
}

#if defined(__x86_64__)
constexpr uint32_t kNativeArch = AUDIT_ARCH_X86_64;
#elif defined(__aarch64__)
constexpr uint32_t kNativeArch = AUDIT_ARCH_AARCH64;
#else
constexpr uint32_t kNativeArch = 0;                                    // (an architecture whose number this file does not know: no filter)
#endif

bool apply_seccomp() {
    if (kNativeArch == 0) return false;
    constexpr uint32_t kKillProcess = 0x80000000u;                      // SECCOMP_RET_KILL_PROCESS (kernel 4.14)
    const long denied[] = {
#ifdef SYS_socket
        SYS_socket,
#endif
#ifdef SYS_socketpair
        SYS_socketpair,
#endif
#ifdef SYS_execve
        SYS_execve,
#endif
#ifdef SYS_execveat
        SYS_execveat,
#endif
    };
    constexpr size_t kMost = 8;
    sock_filter filter[4 + 2 * kMost + 1];
    size_t n = 0;
    const auto put = [&](sock_filter f) { filter[n++] = f; };
    put(BPF_STMT(BPF_LD | BPF_W | BPF_ABS, static_cast<uint32_t>(offsetof(seccomp_data, arch))));
    put(BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, kNativeArch, 1, 0));
    put(BPF_STMT(BPF_RET | BPF_K, kKillProcess));                       // (another system call interface, such as 32-bit code on an x86-64 kernel, is a way round the list: no)
    put(BPF_STMT(BPF_LD | BPF_W | BPF_ABS, static_cast<uint32_t>(offsetof(seccomp_data, nr))));
    for (const long nr : denied) {
        put(BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, static_cast<uint32_t>(nr), 0, 1));
        put(BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | EPERM));
    }
    put(BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW));
    sock_fprog program{static_cast<unsigned short>(n), filter};
    return prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &program, 0, 0) == 0;
}

}  // namespace

Sandbox apply_sandbox() {
    Sandbox out;
    out.no_new_privs = prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) == 0;
    if (!out.no_new_privs) return out;                                   // (neither of the two may be set without it)
    apply_landlock(out);
    out.seccomp = apply_seccomp();
    return out;
}

#else

Sandbox apply_sandbox() { return Sandbox{}; }

#endif

std::string describe(const Sandbox& s) {
    std::string out;
    if (s.landlock_abi > 0) out = "landlock abi " + std::to_string(s.landlock_abi) + (s.landlock_network ? " (files and tcp)" : " (files)");
    if (s.seccomp) out += std::string(out.empty() ? "" : ", ") + "seccomp (sockets, exec)";
    return out.empty() ? std::string("none") : out;
}

}  // namespace ants_thumb
