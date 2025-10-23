// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab

#ifndef CEPH_ACCONFIG_H
#define CEPH_ACCONFIG_H

// 基本的配置定义
#define HAVE_INTTYPES_H 1
#define HAVE_STDINT_H 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_UNISTD_H 1
#define HAVE_STRING_H 1
#define HAVE_STDLIB_H 1
#define HAVE_MEMORY_H 1
#define HAVE_ERRNO_H 1
#define HAVE_FCNTL_H 1
#define HAVE_SYS_STAT_H 1
#define HAVE_SYS_TIME_H 1
#define HAVE_TIME_H 1
#define HAVE_SYS_SOCKET_H 1
#define HAVE_NETINET_IN_H 1
#define HAVE_ARPA_INET_H 1
#define HAVE_NETDB_H 1
#define HAVE_SIGNAL_H 1
#define HAVE_PTHREAD_H 1
#define HAVE_DLFCN_H 1

// 基本类型定义
#define SIZEOF_CHAR 1
#define SIZEOF_SHORT 2
#define SIZEOF_INT 4
#define SIZEOF_LONG 8
#define SIZEOF_LONG_LONG 8
#define SIZEOF_VOID_P 8

// 编译器特性
#define HAVE_STD_STRING 1
#define HAVE_STD_VECTOR 1
#define HAVE_STD_MAP 1
#define HAVE_STD_SET 1
#define HAVE_STD_LIST 1
#define HAVE_STD_DEQUE 1
#define HAVE_STD_STACK 1
#define HAVE_STD_QUEUE 1

// 平台特性
#ifdef __linux__
#define HAVE_LINUX_FALLOCATE 1
#define HAVE_LINUX_FIEMAP 1
#endif

#ifdef __APPLE__
#define HAVE_MACOS_FALLOCATE 1
#endif

#ifdef _WIN32
#define HAVE_WINDOWS_FALLOCATE 1
#endif

// 网络特性
#define HAVE_SOCKET 1
#define HAVE_INET_NTOA 1
#define HAVE_INET_ATON 1
#define HAVE_GETHOSTBYNAME 1
#define HAVE_GETHOSTBYADDR 1

// 文件系统特性
#define HAVE_OPEN 1
#define HAVE_CLOSE 1
#define HAVE_READ 1
#define HAVE_WRITE 1
#define HAVE_LSEEK 1
#define HAVE_FSTAT 1
#define HAVE_STAT 1
#define HAVE_ACCESS 1
#define HAVE_UNLINK 1
#define HAVE_RENAME 1
#define HAVE_MKDIR 1
#define HAVE_RMDIR 1
#define HAVE_CHDIR 1
#define HAVE_GETCWD 1

// 内存管理
#define HAVE_MALLOC 1
#define HAVE_FREE 1
#define HAVE_REALLOC 1
#define HAVE_CALLOC 1
#define HAVE_MEMSET 1
#define HAVE_MEMCPY 1
#define HAVE_MEMMOVE 1
#define HAVE_MEMCMP 1
#define HAVE_MEMCHR 1

// 字符串处理
#define HAVE_STRCPY 1
#define HAVE_STRNCPY 1
#define HAVE_STRCAT 1
#define HAVE_STRNCAT 1
#define HAVE_STRCMP 1
#define HAVE_STRNCMP 1
#define HAVE_STRCHR 1
#define HAVE_STRRCHR 1
#define HAVE_STRSTR 1
#define HAVE_STRLEN 1
#define HAVE_STRDUP 1
#define HAVE_STRNDUP 1

// 数学函数
#define HAVE_SQRT 1
#define HAVE_POW 1
#define HAVE_LOG 1
#define HAVE_LOG10 1
#define HAVE_EXP 1
#define HAVE_SIN 1
#define HAVE_COS 1
#define HAVE_TAN 1
#define HAVE_ASIN 1
#define HAVE_ACOS 1
#define HAVE_ATAN 1
#define HAVE_ATAN2 1
#define HAVE_FLOOR 1
#define HAVE_CEIL 1
#define HAVE_FABS 1
#define HAVE_FMOD 1

// 时间函数
#define HAVE_TIME 1
#define HAVE_GETTIMEOFDAY 1
#define HAVE_CLOCK_GETTIME 1
#define HAVE_USLEEP 1
#define HAVE_SLEEP 1

// 随机数
#define HAVE_RAND 1
#define HAVE_SRAND 1
#define HAVE_RANDOM 1
#define HAVE_SRANDOM 1

// 环境变量
#define HAVE_GETENV 1
#define HAVE_SETENV 1
#define HAVE_UNSETENV 1
#define HAVE_PUTENV 1

// 进程控制
#define HAVE_FORK 1
#define HAVE_EXEC 1
#define HAVE_WAIT 1
#define HAVE_WAITPID 1
#define HAVE_KILL 1
#define HAVE_GETPID 1
#define HAVE_GETPPID 1

// 信号处理
#define HAVE_SIGNAL 1
#define HAVE_SIGACTION 1
#define HAVE_SIGPROCMASK 1
#define HAVE_SIGSUSPEND 1
#define HAVE_SIGWAIT 1

// 线程
#define HAVE_PTHREAD_CREATE 1
#define HAVE_PTHREAD_JOIN 1
#define HAVE_PTHREAD_DETACH 1
#define HAVE_PTHREAD_EXIT 1
#define HAVE_PTHREAD_MUTEX_INIT 1
#define HAVE_PTHREAD_MUTEX_DESTROY 1
#define HAVE_PTHREAD_MUTEX_LOCK 1
#define HAVE_PTHREAD_MUTEX_UNLOCK 1
#define HAVE_PTHREAD_COND_INIT 1
#define HAVE_PTHREAD_COND_DESTROY 1
#define HAVE_PTHREAD_COND_WAIT 1
#define HAVE_PTHREAD_COND_SIGNAL 1
#define HAVE_PTHREAD_COND_BROADCAST 1

// 动态链接
#define HAVE_DLOPEN 1
#define HAVE_DLSYM 1
#define HAVE_DLCLOSE 1
#define HAVE_DLERROR 1

// 其他特性
#define HAVE_GETOPT 1
#define HAVE_GETOPT_LONG 1
#define HAVE_GETOPT_LONG_ONLY 1
#define HAVE_ISATTY 1
#define HAVE_TTYNAME 1
#define HAVE_CTERMID 1
#define HAVE_CTERMID_R 1
#define HAVE_CTERMID_S 1

#endif // CEPH_ACCONFIG_H
