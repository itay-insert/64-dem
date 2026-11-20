#pragma once

// Absolute or relative ASCII 8.3 paths, at most 4095 bytes excluding NUL.
// On success, outputs are new[] allocations owned by the caller. The parent
// starts and ends with '/' and contains slash-separated, padded 11-byte names.
// For a relative input, join the encoded parent to the current directory.
// The filename is an 11-byte padded short name followed by NUL.
// On failure, outputs are null. Output pointers must be distinct.
bool fs_split_path(const char *path, char **parent_out, char **filename_out);

// Join NUL-terminated, already encoded FAT paths without changing name padding.
// Parent must be absolute; leading child slashes are treated as separators.
// Preserves a child's trailing slash. Returns new[] storage (delete[] to free),
// or nullptr for null/empty inputs or a result longer than 4095 bytes.
char *fs_join_path(const char *parent, const char *son);
