#define O_RDONLY  0x000
#define O_WRONLY  0x001
#define O_RDWR    0x002
#define O_CREATE  0x200
#define O_TRUNC   0x400
#define O_NOFOLLOW   0x004  // 访问符号文件本省，不进行符号转发

#define MAX_SYMLINK_DEPTH 10
