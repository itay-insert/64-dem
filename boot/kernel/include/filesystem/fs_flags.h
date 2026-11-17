#ifndef FS_FLAGS_H
#define FS_FLAGS_H



#define RD_ONLY 0
#define WR_ONLY 1
#define RD_WR 2
#define ACCMODE 3


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