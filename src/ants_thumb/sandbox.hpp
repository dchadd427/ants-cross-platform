#pragma once

// What ants_thumb takes away from itself before it reads the bytes of a map (docs/MAP_SUBMISSIONS.md, "Pictures"). Linux only, best effort: a kernel or a container profile that lacks a part leaves that part
// out, and the program draws the picture all the same; nothing here ever stops it. After apply_sandbox():
//  * no new privileges (a program that it could start would not gain any), the thing that Landlock and a seccomp filter need;
//  * Landlock (kernel 5.13 and later): no file of the filesystem can be opened, made, removed or changed any more, whatever the file's mode: the program holds everything that it needs already open (the
//    archive and the map are read, the window and the renderer are up, the picture's file is open); and, with Landlock's network rules (kernel 6.7 and later), no TCP connection or bind;
//  * a seccomp filter: no socket of any kind (socket, socketpair) and no new program (execve, execveat); the system calls that SDL's dummy drivers and the renderer use are untouched.
// Whatever the map's bytes make the program do from there on, the control secret and the database that sit beside the pictures cannot be read, and nothing can be sent anywhere.

#include <string>

namespace ants_thumb {

struct Sandbox {
    bool no_new_privs{false};
    int landlock_abi{0};          // 0: no Landlock here (an old kernel, a kernel built without it, a profile that refuses it); else the version that the kernel speaks (1 .. 6)
    bool landlock_network{false}; // the TCP rules of ABI 4 are in force too
    bool seccomp{false};          // the filter is in force
    bool any() const noexcept { return landlock_abi > 0 || seccomp; }
};

/// Applies what this kernel allows and says what it applied. Never fails the program.
Sandbox apply_sandbox();

/// One line that says what is in force: "landlock abi 4 (files and tcp), seccomp (sockets, exec)", "seccomp (sockets, exec)", or "none"
std::string describe(const Sandbox& sandbox);

}  // namespace ants_thumb
