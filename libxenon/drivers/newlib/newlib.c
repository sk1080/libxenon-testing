#include <_ansi.h>
#include <_syslist.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/iosupport.h>
#include <sys/dirent.h>
#include <sys/errno.h>
#include <sys/time.h>

#include <diskio/disc_io.h>
#include <xenon_soc/xenon_power.h>

#include <debug.h>

#define MAXPATHLEN 2048

extern unsigned char heap_begin;
unsigned char *heap_ptr;

void *sbrk(ptrdiff_t incr) {
    unsigned char *res;
    if (!heap_ptr)
        heap_ptr = &heap_begin;
    res = heap_ptr;
    heap_ptr += incr;
    return res;
}
void (*stdout_hook)(const char *text, int len) = 0;

/**
 * Fake vfs
 */
ssize_t vfs_console_write(struct _reent *r, int fd, const char *src, size_t len) {
    if (stdout_hook)
        stdout_hook(src, len);
    size_t i;
    for (i = 0; i < len; ++i)
        putch(((const char*) src)[i]);
    return len;
}
//---------------------------------------------------------------------------------
const devoptab_t console_optab = {
    //---------------------------------------------------------------------------------
    "stdout", // device name
    0, // size of file structure
    NULL, // device open
    NULL, // device close
    vfs_console_write, // device write
    NULL, // device read
    NULL, // device seek
    NULL, // device fstat
    NULL, // device stat
    NULL, // device link
    NULL, // device unlink
    NULL, // device chdir
    NULL, // device rename
    NULL, // device mkdir
    0, // dirStateSize
    NULL, // device diropen_r
    NULL, // device dirreset_r
    NULL, // device dirnext_r
    NULL, // device dirclose_r
    NULL, // device statvfs_r
    NULL, // device ftrunctate_r
    NULL, // device fsync_r
    NULL, // deviceData;
};

// 22 nov 2005
#define	RTC_BASE	1132614024UL//1005782400UL

static int xenon_gettimeofday(void * unused, struct timeval * tp, void * tzp) {
    unsigned char msg[16] = {0x04};
    unsigned long long msec;
    unsigned long sec;

    xenon_smc_send_message(msg);
    xenon_smc_receive_response(msg);

    msec = msg[1] | (msg[2] << 8) | (msg[3] << 16) | (msg[4] << 24) | ((unsigned long long) msg[5] << 32);

    sec = msec / 1000;
    tp->tv_sec = sec + RTC_BASE;
    msec -= sec * 1000;
    tp->tv_usec = msec * 1000;

    return 0;
}


/*
 * System stuff
 */

//---------------------------------------------------------------------------------
__syscalls_t __syscalls = {
    //---------------------------------------------------------------------------------
    NULL, // sbrk
    NULL, // lock_init
    NULL, // lock_close
    NULL, // lock_release
    NULL, // lock_acquire
    NULL, // malloc_lock
    NULL, // malloc_unlock
    NULL, // exit
    xenon_gettimeofday // gettod_r
};

int __libc_lock_init(int *lock, int recursive) {

    if (__syscalls.lock_init) {
        return __syscalls.lock_init(lock, recursive);
    }

    return 0;
}

int __libc_lock_close(int *lock) {

    if (__syscalls.lock_close) {
        return __syscalls.lock_close(lock);
    }

    return 0;
}

int __libc_lock_release(int *lock) {

    if (__syscalls.lock_release) {
        return __syscalls.lock_release(lock);
    }

    return 0;
}

int __libc_lock_acquire(int *lock) {

    if (__syscalls.lock_acquire) {
        return __syscalls.lock_acquire(lock);
    }

    return 0;
}

void __malloc_lock(struct _reent *ptr) {

    if (__syscalls.malloc_lock) {
        __syscalls.malloc_lock(ptr);
    }
}

void __malloc_unlock(struct _reent *ptr) {

    if (__syscalls.malloc_unlock) {
        __syscalls.malloc_unlock(ptr);
    }
}

extern void enet_quiesce();
extern void usb_shutdown(void);

void shutdown_drivers() {
    // some drivers require a shutdown
    enet_quiesce();
    usb_shutdown();
}

extern void return_to_xell(unsigned int nand_addr, unsigned int phy_loading_addr);
#define XELL_FOOTER_OFFSET (256*1024-16)
#define XELL_FOOTER_LENGTH 16
#define XELL_FOOTER "xxxxxxxxxxxxxxxx"

void try_return_to_xell(unsigned int nand_addr, unsigned int phy_loading_addr) {
    if (!memcmp((void*) (nand_addr + XELL_FOOTER_OFFSET), XELL_FOOTER, XELL_FOOTER_LENGTH))
        return_to_xell(nand_addr, phy_loading_addr);
}

void _exit(int status) {
    char s[256];
    int i, stuck = 0;

    sprintf(s, "[Exit] with code %d\n", status);
    vfs_console_write(NULL, 0, s, strlen(s));

    for (i = 0; i < 6; ++i) {
        if (xenon_is_thread_task_running(i)) {
            sprintf(s, "Thread %d is still running !\n", i);
            vfs_console_write(NULL, 0, s, strlen(s));
            stuck = 1;
        }
    }

    shutdown_drivers();

    if (stuck) {
        sprintf(s, "Can't reload Xell, looping...");
        vfs_console_write(NULL, 0, s, strlen(s));
    } else {
        sprintf(s, "Reloading Xell...");
        vfs_console_write(NULL, 0, s, strlen(s));
        xenon_set_single_thread_mode();

        try_return_to_xell(0xc8070000, 0x1c000000); // xell-gggggg (ggboot)
        try_return_to_xell(0xc8095060, 0x1c040000); // xell-2f (freeboot)
        try_return_to_xell(0xc8100000, 0x1c000000); // xell-1f, xell-ggggggg
    }

    for (;;);
}
//---------------------------------------------------------------------------------

void abort(void) {
    vfs_console_write(NULL, 0, "Abort called.\n", sizeof ("Abort called.\n") - 1);
    _exit(1);
}
//---------------------------------------------------------------------------------

int
_DEFUN(execve, (name, argv, env),
        char *name _AND
        char **argv _AND
        char **env) {
    struct _reent *r = _REENT;
    r->_errno = ENOSYS;
    return -1;
}

//---------------------------------------------------------------------------------

int
_DEFUN(fork, (),
        _NOARGS) {
    struct _reent *r = _REENT;
    r->_errno = ENOSYS;
    return -1;
}
//---------------------------------------------------------------------------------

int _DEFUN(getpid, (),
        _NOARGS) {
    struct _reent *ptr = _REENT;
    ptr->_errno = ENOSYS;
    return -1;
}
//---------------------------------------------------------------------------------

int
_DEFUN(getrusage, (who, usage),
        int who _AND
        struct rusage *usage) {
    struct _reent *ptr = _REENT;
    ptr->_errno = ENOSYS;
    return -1;
}

int
_DEFUN(gettimeofday, (ptimeval, ptimezone),
        struct timeval *ptimeval _AND
        void *ptimezone) {
    struct _reent *ptr = _REENT;


    if (__syscalls.gettod_r) return __syscalls.gettod_r(ptr, ptimeval, ptimezone);

    ptr->_errno = ENOSYS;
    return -1;

}

int _DEFUN(isatty, (file),
        int file) {
    return 0;
}

int _DEFUN(kill, (pid, sig),
        int pid _AND
        int sig) {
    struct _reent *ptr = _REENT;
    ptr->_errno = ENOSYS;
    return -1;
}
//---------------------------------------------------------------------------------

/*
int
_DEFUN(wait, (status),
        int *status) {
    struct _reent *r = _REENT;
    r->_errno = ENOSYS;
    return -1;
}
 */

/**
 * User etc ..
 */
#define ROOT_UID 0
#define ROOT_GID 0

uid_t getuid(void) {
    return ROOT_UID;
}

uid_t geteuid(void) {
    return ROOT_UID;
}

gid_t getgid(void) {
    return ROOT_GID;
}

gid_t getegid(void) {
    return ROOT_GID;
}

int setuid(uid_t uid) {
    return (uid == ROOT_UID ? 0 : -1);
}

int setgid(gid_t gid) {
    return (gid == ROOT_GID ? 0 : -1);
}

pid_t getppid(void) {
    return 0;
}

void *getpwuid(uid_t uid) {
    TR
    return NULL;
}

void *getpwnam(const char *name) {
    TR
    return NULL;
};

void *getgrnam(const char *name) {
    TR
    return NULL;
};

void *getgrgid(gid_t gid) {
    TR
    return NULL;
};


/**
 * HANDLES
 */

#define MAX_HANDLES 1024

static __handle* handles[MAX_HANDLES];

__LOCK_INIT_RECURSIVE(static, __hndl_lock);

void __release_handle(int fd) {

    if (fd < 3 || fd >= MAX_HANDLES + 3) return;

    fd -= 3;
    __handle* handle = handles[fd];
    if (NULL != handle) {
        free(handle);
        handles[fd] = NULL;
    }

}

int __alloc_handle(int size) {

    int i, ret = -1;

    __lock_acquire_recursive(__hndl_lock);
    for (i = 0; i < MAX_HANDLES; i++) {
        if (handles[i] == NULL) break;
    }

    if (i < MAX_HANDLES) {
        handles[i] = malloc(size);
        if (NULL != handles[i]) ret = i + 3;
    }
    __lock_release_recursive(__hndl_lock);

    return ret;
}

__handle *__get_handle(int fd) {

    if (fd < 3 || fd > MAX_HANDLES + 3) return NULL;

    return handles[fd - 3];

}

/**
 * IO Support
 */

static int defaultDevice = -1;

//---------------------------------------------------------------------------------

void setDefaultDevice(int device) {
    //---------------------------------------------------------------------------------

    if (device > 2 && device <= STD_MAX)
        defaultDevice = device;
}

//---------------------------------------------------------------------------------
const devoptab_t dotab_stdnull = {
    //---------------------------------------------------------------------------------
    "stdnull", // device name
    0, // size of file structure
    NULL, // device open
    NULL, // device close
    NULL, // device write
    NULL, // device read
    NULL, // device seek
    NULL, // device fstat
    NULL, // device stat
    NULL, // device link
    NULL, // device unlink
    NULL, // device chdir
    NULL, // device rename
    NULL, // device mkdir
    0, // dirStateSize
    NULL, // device diropen_r
    NULL, // device dirreset_r
    NULL, // device dirnext_r
    NULL, // device dirclose_r
    NULL, // device statvfs_r
    NULL, // device ftruncate_r
    NULL, // device fsync_r
    NULL // deviceData
};

//---------------------------------------------------------------------------------
const devoptab_t *devoptab_list[STD_MAX] = {
    //---------------------------------------------------------------------------------
    //&dotab_stdnull, &dotab_stdnull, &dotab_stdnull, &dotab_stdnull,
    &console_optab, &console_optab, &console_optab, &dotab_stdnull,
    &dotab_stdnull, &dotab_stdnull, &dotab_stdnull, &dotab_stdnull,
    &dotab_stdnull, &dotab_stdnull, &dotab_stdnull, &dotab_stdnull,
    &dotab_stdnull, &dotab_stdnull, &dotab_stdnull, &dotab_stdnull
};

//---------------------------------------------------------------------------------

int FindDevice(const char* name) {
    //---------------------------------------------------------------------------------
    int i = 0, namelen, dev = -1;

    if (strchr(name, ':') == NULL) return defaultDevice;

    while (i < STD_MAX) {
        if (devoptab_list[i]) {
            namelen = strlen(devoptab_list[i]->name);
            if (strncmp(devoptab_list[i]->name, name, namelen) == 0) {
                if (name[namelen] == ':' || (isdigit(name[namelen]) && name[namelen + 1] == ':')) {
                    dev = i;
                    break;
                }
            }
        }
        i++;
    }

    return dev;
}

//---------------------------------------------------------------------------------

int RemoveDevice(const char* name) {
    //---------------------------------------------------------------------------------
    int dev = FindDevice(name);

    if (-1 != dev) {
        devoptab_list[dev] = &dotab_stdnull;
        return 0;
    }

    return -1;

}

//---------------------------------------------------------------------------------

int AddDevice(const devoptab_t* device) {
    //---------------------------------------------------------------------------------

    int devnum;

    for (devnum = 3; devnum < STD_MAX; devnum++) {

        if ((!strcmp(devoptab_list[devnum]->name, device->name) &&
                strlen(devoptab_list[devnum]->name) == strlen(device->name)) ||
                !strcmp(devoptab_list[devnum]->name, "stdnull")
                )
            break;
    }

    if (devnum == STD_MAX) {
        devnum = -1;
    } else {
        devoptab_list[devnum] = device;
    }
    return devnum;
}

//---------------------------------------------------------------------------------

const devoptab_t* GetDeviceOpTab(const char *name) {
    //---------------------------------------------------------------------------------
    int dev = FindDevice(name);
    if (dev >= 0 && dev < STD_MAX) {
        return devoptab_list[dev];
    } else {
        return NULL;
    }
}

/**
 * File IO
 */

//---------------------------------------------------------------------------------

int _DEFUN(open, (file, flags, mode),
        const char *file _AND
        int flags _AND
        int mode) {
    struct _reent *r = _REENT;

    __handle *handle;
    int dev, fd, ret;

    dev = FindDevice(file);

    fd = -1;
    if (dev != -1 && devoptab_list[dev]->open_r) {

        fd = __alloc_handle(sizeof (__handle) + devoptab_list[dev]->structSize);

        if (-1 != fd) {
            handle = __get_handle(fd);
            handle->device = dev;
            handle->fileStruct = ((void *) handle) + sizeof (__handle);

            ret = devoptab_list[dev]->open_r(r, handle->fileStruct, file, flags, mode);

            if (ret == -1) {
                __release_handle(fd);
                fd = -1;
            }
        } else {
            r->_errno = ENOSR;
        }
    } else {
        r->_errno = ENOSYS;
    }

    return fd;
}


//---------------------------------------------------------------------------------

_ssize_t _DEFUN(read, (fileDesc, ptr, len),
        int fileDesc _AND
        char *ptr _AND
        size_t len) {
    //---------------------------------------------------------------------------------
    struct _reent *r = _REENT;
    int ret = -1;
    unsigned int dev = 0;
    unsigned int fd = -1;

    __handle * handle = NULL;

    if (fileDesc != -1) {
        if (fileDesc < 3) {
            dev = fileDesc;
        } else {
            handle = __get_handle(fileDesc);

            if (NULL == handle) return ret;

            dev = handle->device;
            fd = (int) handle->fileStruct;
        }
        if (devoptab_list[dev]->read_r)
            ret = devoptab_list[dev]->read_r(r, fd, ptr, len);
    }
    return ret;
}

_ssize_t _DEFUN(write, (fileDesc, ptr, len),
        int fileDesc _AND
        const char *ptr _AND
        int len) {
    struct _reent *r = _REENT;
    int ret = -1;
    unsigned int dev = 0;
    unsigned int fd = -1;

    __handle * handle = NULL;


    if (fileDesc != -1) {
        if (fileDesc < 3) {
            dev = fileDesc;
            ret = len;
        } else {
            handle = __get_handle(fileDesc);

            if (NULL == handle) return ret;

            dev = handle->device;
            fd = (int) handle->fileStruct;
        }
        if (devoptab_list[dev]->write_r)
            ret = devoptab_list[dev]->write_r(r, fd, ptr, len);
    }
    return ret;
}


//---------------------------------------------------------------------------------

int _DEFUN(fstat, (fileDesc, st),
        int fileDesc _AND
        struct stat *st) {
    //---------------------------------------------------------------------------------
    struct _reent *r = _REENT;
    int ret = -1;
    unsigned int dev = 0;
    unsigned int fd = -1;

    __handle * handle = NULL;

    if (fileDesc != -1) {
        if (fileDesc < 3) {
            dev = fileDesc;
        } else {
            handle = __get_handle(fileDesc);

            if (NULL == handle) return ret;

            dev = handle->device;
            fd = (int) handle->fileStruct;
        }
        if (devoptab_list[dev]->fstat_r) {
            ret = devoptab_list[dev]->fstat_r(r, fd, st);
        } else {
            r->_errno = ENOSYS;
        }
    }
    return ret;
}

int
_DEFUN(stat, (file, st),
        const char *file _AND
        struct stat *st) {
    //---------------------------------------------------------------------------------
    struct _reent *r = _REENT;
    int dev, ret;

    dev = FindDevice(file);

    if (dev != -1 && devoptab_list[dev]->stat_r) {
        ret = devoptab_list[dev]->stat_r(r, file, st);
    } else {
        ret = -1;
        r->_errno = ENODEV;
    }
    return ret;
}

int statvfs(const char *path, struct statvfs *buf) {
    struct _reent *r = _REENT;

    int ret;
    int device = FindDevice(path);

    ret = -1;

    if (device != -1 && devoptab_list[device]->statvfs_r) {

        ret = devoptab_list[device]->statvfs_r(r, path, buf);

    } else {
        r->_errno = ENOSYS;
    }

    return ret;
}

int _DEFUN(fsync, (fileDesc),
        int fileDesc) {
    int ret = -1;
    unsigned int dev = 0;
    unsigned int fd = -1;

    __handle * handle;

    if (fileDesc < 3) {
        errno = EINVAL;
    } else {
        handle = __get_handle(fileDesc);

        if (NULL == handle) {
            errno = EINVAL;
            return ret;
        }

        dev = handle->device;
        fd = (int) handle->fileStruct;

        if (devoptab_list[dev]->fsync_r)
            ret = devoptab_list[dev]->fsync_r(_REENT, fd);
    }

    return ret;
}

int _DEFUN(ftruncate, (fileDesc, len),
        int fileDesc _AND
        off_t len) {
    int ret = -1;
    unsigned int dev = 0;
    unsigned int fd = -1;

    __handle * handle;

    if (fileDesc < 3) {
        errno = EINVAL;
    } else {
        handle = __get_handle(fileDesc);

        if (NULL == handle) {
            errno = EINVAL;
            return ret;
        }

        dev = handle->device;
        fd = (int) handle->fileStruct;

        if (devoptab_list[dev]->ftruncate_r)
            ret = devoptab_list[dev]->ftruncate_r(_REENT, fd, len);
    }

    return ret;
}

int _DEFUN(link, (existing, new),
        const char *existing _AND
        const char *new) {
    struct _reent *r = _REENT;
    int ret;
    int sourceDev = FindDevice(existing);
    int destDev = FindDevice(new);

    ret = -1;

    if (sourceDev == destDev) {
        if (devoptab_list[destDev]->link_r) {
            ret = devoptab_list[destDev]->link_r(r, existing, new);
        } else {
            r->_errno = ENOSYS;
        }
    } else {
        r->_errno = EXDEV;
    }

    return ret;
}


//---------------------------------------------------------------------------------

_off_t _DEFUN(lseek, (fileDesc, pos, dir),
        int fileDesc _AND
        _off_t pos _AND
        int dir) {
    //---------------------------------------------------------------------------------
    struct _reent *r = _REENT;
    //---------------------------------------------------------------------------------
    _off_t ret = -1;
    unsigned int dev = 0;
    unsigned int fd = -1;

    __handle * handle;

    if (fileDesc != -1) {

        if (fileDesc < 3) {
            dev = fileDesc;
        } else {
            handle = __get_handle(fileDesc);

            if (NULL == handle) return ret;

            dev = handle->device;
            fd = (int) handle->fileStruct;
        }

        if (devoptab_list[dev]->seek_r)
            ret = devoptab_list[dev]->seek_r(r, fd, pos, dir);

    }
    return ret;

}

int
_DEFUN(rename, (existing, newName),
        _CONST char *existing _AND
        _CONST char *newName) {
    struct _reent *r = _REENT;

    int ret;
    int sourceDev = FindDevice(existing);
    int destDev = FindDevice(newName);

    ret = -1;

    if (sourceDev == destDev) {
        if (devoptab_list[destDev]->rename_r) {
            ret = devoptab_list[destDev]->rename_r(r, existing, newName);
        } else {
            r->_errno = ENOSYS;
        }
    } else {
        r->_errno = EXDEV;
    }

    return ret;
}

//---------------------------------------------------------------------------------

int _DEFUN(unlink, (name),
        const char *name) {
    //---------------------------------------------------------------------------------
    struct _reent *r = _REENT;
    int dev, ret;

    dev = FindDevice(name);
    if (dev != -1 && devoptab_list[dev]->unlink_r) {
        ret = devoptab_list[dev]->unlink_r(r, name);
    } else {
        ret = -1;
        r->_errno = ENODEV;
    }

    return ret;
}

//---------------------------------------------------------------------------------

int _DEFUN(close, (fileDesc),
        int fileDesc) {
    //---------------------------------------------------------------------------------
    struct _reent *ptr = _REENT;
    int ret = -1;
    unsigned int dev = 0;
    unsigned int fd = -1;

    if (fileDesc != -1) {

        __handle *handle = __get_handle(fileDesc);

        if (handle != NULL) {
            dev = handle->device;
            fd = (unsigned int) handle->fileStruct;

            if (devoptab_list[dev]->close_r)
                ret = devoptab_list[dev]->close_r(ptr, fd);

            __release_handle(fileDesc);
        }
    }
    return ret;
}
/**
 * Dir IO
 */

/* CWD always start with "/" */
static char _current_working_directory [MAXPATHLEN] = "/";

#define DIRECTORY_SEPARATOR_CHAR '/'
const char DIRECTORY_SEPARATOR[] = "/";
const char DIRECTORY_THIS[] = ".";
const char DIRECTORY_PARENT[] = "..";

int _concatenate_path(struct _reent *r, char *path, const char *extra, int maxLength) {
    char *pathEnd;
    int pathLength;
    const char *extraEnd;
    int extraSize;

    pathLength = strnlen(path, maxLength);

    /* assumes path ends in a directory separator */
    if (pathLength >= maxLength) {
        r->_errno = ENAMETOOLONG;
        return -1;
    }
    pathEnd = path + pathLength;
    if (pathEnd[-1] != DIRECTORY_SEPARATOR_CHAR) {
        pathEnd[0] = DIRECTORY_SEPARATOR_CHAR;
        pathEnd += 1;
    }

    extraEnd = extra;

    /* If the extra bit starts with a slash, start at root */
    if (extra[0] == DIRECTORY_SEPARATOR_CHAR) {
        pathEnd = strchr(path, DIRECTORY_SEPARATOR_CHAR) + 1;
        pathEnd[0] = '\0';
    }
    do {
        /* Advance past any separators in extra */
        while (extra[0] == DIRECTORY_SEPARATOR_CHAR) {
            extra += 1;
        }

        /* Grab the next directory name from extra */
        extraEnd = strchr(extra, DIRECTORY_SEPARATOR_CHAR);
        if (extraEnd == NULL) {
            extraEnd = strrchr(extra, '\0');
        } else {
            extraEnd += 1;
        }

        extraSize = (extraEnd - extra);
        if (extraSize == 0) {
            break;
        }

        if ((strncmp(extra, DIRECTORY_THIS, sizeof (DIRECTORY_THIS) - 1) == 0)
                && ((extra[sizeof (DIRECTORY_THIS) - 1] == DIRECTORY_SEPARATOR_CHAR)
                || (extra[sizeof (DIRECTORY_THIS) - 1] == '\0'))) {
            /* Don't copy anything */
        } else if ((strncmp(extra, DIRECTORY_PARENT, sizeof (DIRECTORY_PARENT) - 1) == 0)
                && ((extra[sizeof (DIRECTORY_PARENT) - 1] == DIRECTORY_SEPARATOR_CHAR)
                || (extra[sizeof (DIRECTORY_PARENT) - 1] == '\0'))) {
            /* Go up one level of in the path */
            if (pathEnd[-1] == DIRECTORY_SEPARATOR_CHAR) {
                // Remove trailing separator
                pathEnd[-1] = '\0';
            }
            pathEnd = strrchr(path, DIRECTORY_SEPARATOR_CHAR);
            if (pathEnd == NULL) {
                /* Can't go up any higher, return false */
                r->_errno = ENOENT;
                return -1;
            }
            pathLength = pathEnd - path;
            pathEnd += 1;
        } else {
            pathLength += extraSize;
            if (pathLength >= maxLength) {
                r->_errno = ENAMETOOLONG;
                return -1;
            }
            /* Copy the next part over */
            strncpy(pathEnd, extra, extraSize);
            pathEnd += extraSize;
        }
        pathEnd[0] = '\0';
        extra += extraSize;
    } while (extraSize != 0);

    if (pathEnd[-1] != DIRECTORY_SEPARATOR_CHAR) {
        pathEnd[0] = DIRECTORY_SEPARATOR_CHAR;
        pathEnd[1] = 0;
        pathEnd += 1;
    }

    return 0;
}

int chdir(const char *path) {
    struct _reent *r = _REENT;

    int dev;
    const char *pathPosition;
    char temp_cwd [MAXPATHLEN];

    /* Make sure the path is short enough */
    if (strnlen(path, MAXPATHLEN + 1) >= MAXPATHLEN) {
        r->_errno = ENAMETOOLONG;
        return -1;
    }

    if (strchr(path, ':') != NULL) {
        strncpy(temp_cwd, path, MAXPATHLEN);
        /* Move path past device name */
        path = strchr(path, ':') + 1;
    } else {
        strncpy(temp_cwd, _current_working_directory, MAXPATHLEN);
    }

    pathPosition = strchr(temp_cwd, ':') + 1;

    /* Make sure the path starts in the root directory */
    if (pathPosition[0] != DIRECTORY_SEPARATOR_CHAR) {
        r->_errno = ENOENT;
        return -1;
    }

    /* Concatenate the path to the CWD */
    if (_concatenate_path(r, temp_cwd, path, MAXPATHLEN) == -1) {
        return -1;
    }

    /* Get device from path name */
    dev = FindDevice(temp_cwd);

    if ((dev < 0) || (devoptab_list[dev]->chdir_r == NULL)) {
        r->_errno = ENODEV;
        return -1;
    }

    /* Try changing directories on the device */
    if (devoptab_list[dev]->chdir_r(r, temp_cwd) == -1) {
        return -1;
    }

    /* Since it worked, set the new CWD and default device */
    setDefaultDevice(dev);
    strncpy(_current_working_directory, temp_cwd, MAXPATHLEN);

    return 0;
}

char *getcwd(char *buf, size_t size) {

    struct _reent *r = _REENT;

    if (size == 0) {
        r->_errno = EINVAL;
        return NULL;
    }

    if (size < (strnlen(_current_working_directory, MAXPATHLEN) + 1)) {
        r->_errno = ERANGE;
        return NULL;
    }

    strncpy(buf, _current_working_directory, size);

    return buf;
}

int chmod(const char *path, mode_t mode) {
    int ret, dev;
    struct _reent *r = _REENT;

    /* Get device from path name */
    dev = FindDevice(path);

    if ((dev < 0) || (devoptab_list[dev]->chmod_r == NULL)) {
        r->_errno = ENODEV;
        ret = -1;
    } else {
        ret = devoptab_list[dev]->chmod_r(r, path, mode);
    }

    return ret;
}

int fchmod(int fd, mode_t mode) {
    int ret = -1, dev;
    struct _reent *r = _REENT;

    if (fd != -1) {

        __handle *handle = __get_handle(fd);

        if (handle != NULL) {

            dev = handle->device;
            fd = (unsigned int) handle->fileStruct;

            if (devoptab_list[dev]->fchmod_r)
                ret = devoptab_list[dev]->fchmod_r(r, fd, mode);

        }
    }
    return ret;
}

static DIR_ITER * __diropen(const char *path) {
    struct _reent *r = _REENT;
    DIR_ITER *handle = NULL;
    DIR_ITER *dir = NULL;
    int dev;

    dev = FindDevice(path);

    if (dev != -1 && devoptab_list[dev]->diropen_r) {

        handle = (DIR_ITER *) malloc(sizeof (DIR_ITER) + devoptab_list[dev]->dirStateSize);

        if (NULL != handle) {
            handle->device = dev;
            handle->dirStruct = ((void *) handle) + sizeof (DIR_ITER);

            dir = devoptab_list[dev]->diropen_r(r, handle, path);

            if (dir == NULL) {
                free(handle);
                handle = NULL;
            }
        } else {
            r->_errno = ENOSR;
            handle = NULL;
        }
    } else {
        r->_errno = ENOSYS;
    }

    return handle;
}

static int __dirreset(DIR_ITER *dirState) {
    struct _reent *r = _REENT;
    int ret = -1;
    int dev = 0;

    if (dirState != NULL) {
        dev = dirState->device;

        if (devoptab_list[dev]->dirreset_r) {
            ret = devoptab_list[dev]->dirreset_r(r, dirState);
        } else {
            r->_errno = ENOSYS;
        }
    }
    return ret;
}

static int __dirnext(DIR_ITER *dirState, char *filename, struct stat *filestat) {
    struct _reent *r = _REENT;
    int ret = -1;
    int dev = 0;

    if (dirState != NULL) {
        dev = dirState->device;

        if (devoptab_list[dev]->dirnext_r) {
            ret = devoptab_list[dev]->dirnext_r(r, dirState, filename, filestat);
        } else {
            r->_errno = ENOSYS;
        }
    }
    return ret;
}

static int __dirclose(DIR_ITER *dirState) {
    struct _reent *r = _REENT;
    int ret = -1;
    int dev = 0;

    if (dirState != NULL) {
        dev = dirState->device;

        if (devoptab_list[dev]->dirclose_r) {
            ret = devoptab_list[dev]->dirclose_r(r, dirState);
        } else {
            r->_errno = ENOSYS;
        }

        free(dirState);
    }
    return ret;
}

DIR* opendir(const char *dirname) {
    DIR* dirp = malloc(sizeof (DIR));
    if (!dirp) {
        errno = ENOMEM;
        return NULL;
    }

    dirp->dirData = __diropen(dirname);
    if (!dirp->dirData) {
        free(dirp);
        return NULL;
    }

    dirp->position = 0; // 0th position means no file name has been returned yet
    dirp->fileData.d_ino = -1;
    dirp->fileData.d_name[0] = '\0';

    return dirp;
}

int closedir(DIR *dirp) {
    int res;

    if (!dirp) {
        errno = EBADF;
        return -1;
    }

    res = __dirclose(dirp->dirData);
    free(dirp);
    return res;
}

struct dirent* readdir(DIR *dirp) {
    struct stat st;
    char filename[MAXPATHLEN];
    int res;
    int olderrno = errno;

    if (!dirp) {
        errno = EBADF;
        return NULL;
    }

    res = __dirnext(dirp->dirData, filename, &st);

    if (res < 0) {
        if (errno == ENOENT) {
            // errno == ENONENT set by dirnext means it's end of directory
            // But readdir should not touch errno in case of dir end
            errno = olderrno;
        }
        return NULL;
    }

    // We've moved forward in the directory
    dirp->position += 1;

    if (strnlen(filename, MAXPATHLEN) >= sizeof (dirp->fileData.d_name)) {
        errno = EOVERFLOW;
        return NULL;
    }

    strncpy(dirp->fileData.d_name, filename, sizeof (dirp->fileData.d_name));
    dirp->fileData.d_ino = st.st_ino;
    dirp->fileData.d_type = S_ISDIR(st.st_mode) ? DT_DIR : DT_REG;
    return &(dirp->fileData);
}

void rewinddir(DIR *dirp) {
    if (!dirp) {
        return;
    }

    __dirreset(dirp->dirData);
    dirp->position = 0;
}

void seekdir(DIR *dirp, long int loc) {
    char filename[MAXPATHLEN];

    if (!dirp || loc < 0) {
        return;
    }

    if (dirp->position > loc) {
        // The entry we want is before the one we have,
        // so we have to start again from the begining
        __dirreset(dirp->dirData);
        dirp->position = 0;
    }

    // Keep reading entries until we reach the one we want
    while ((dirp->position < loc) &&
            (__dirnext(dirp->dirData, filename, NULL) >= 0)) {
        dirp->position += 1;
    }
}

long int telldir(DIR *dirp) {
    if (!dirp) {
        return -1;
    }

    return dirp->position;
}

int mkdir(const char *path, mode_t mode) {
    struct _reent *r = _REENT;
    int ret;
    int dev = FindDevice(path);
    ret = -1;

    if (devoptab_list[dev]->mkdir_r) {
        ret = devoptab_list[dev]->mkdir_r(r, path, mode);
    } else {
        r->_errno = ENOSYS;
    }

    return ret;
}

int bdev_enum(int handle, const char **name) {
    do {
        ++handle;
    } while (handle < STD_MAX && !devoptab_list[handle]);

    if (handle >= STD_MAX) return -1;

    if (name)
        *name = devoptab_list[handle]->name;

    return handle;
}

