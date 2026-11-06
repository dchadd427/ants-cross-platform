#pragma once

// A file that is written whole or not at all: how the server keeps a small file that something else reads later (the site statistics, the result file of a room). The text goes to a temporary file next to it
// (`<path>.tmp`: always a new plain file, with the mode of any new file, 0666 less the umask; what is found in its place, a leftover of a crash, a link, a pipe or another name of a file, is removed first and never
// opened as it is), is made durable (fsync, _commit on Windows) and only then given its name by a rename, so that the file is the whole new text or the whole old one, never half of either; a link of the file's
// name is replaced, never followed to the file it points to.

#include <string>

namespace ants::server {

/// Writes `text` as the file `path`. False with `why` ("cannot write <tmp> (the reason)" or "cannot rename <tmp>: the reason") when anything fails; the temporary file that this call made is removed then (a folder of that
/// name is not touched), and `path` is as it was.
bool write_whole_file(const std::string& path, const std::string& text, std::string& why);

}  // namespace ants::server
