#ifndef FS_FLAGS_H
#define FS_FLAGS_H



#define RD_ONLY 0
#define WR_ONLY 1
#define RD_WR 2
#define ACCMODE 3
#define FS_OPEN_CREATE    0x04
#define FS_OPEN_EXCLUSIVE 0x08
#define FS_OPEN_TRUNCATE  0x10
#define FS_OPEN_APPEND    0x20

/* Stable error values returned as negative results by fs_open. */
#define FS_OPEN_ERR_NO_ENTRY   2
#define FS_OPEN_ERR_IO         5
#define FS_OPEN_ERR_ACCESS    13
#define FS_OPEN_ERR_EXISTS    17
#define FS_OPEN_ERR_NOT_DIR   20
#define FS_OPEN_ERR_DIRECTORY 21
#define FS_OPEN_ERR_INVALID   22
#define FS_OPEN_ERR_NO_SPACE  28



#define S_IRUSR    0400
#define S_IWUSR    0200
#define S_IXUSR    0100

#define S_IRGRP    0040
#define S_IWGRP    0020
#define S_IXGRP    0010

#define S_IROTH    0004
#define S_IWOTH    0002
#define S_IXOTH    0001

#define S_IRWXU    0700
#define S_IRWXG    0070
#define S_IRWXO    0007

/* seek() origins. */
#define SEEK_SET   0
#define SEEK_CUR   1
#define SEEK_END   2


#endif