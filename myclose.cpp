// #define UNW_LOCAL_ONLY
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <dlfcn.h>
#include <atomic>
#include <cxxabi.h>
// #include <libunwind.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <execinfo.h>
#include <sys/syscall.h>

/*
 * g++ -Wall -O0 -shared -std=gnu++11 -fPIC -o libexample.so thisfile.cpp
 */

static __attribute__((unused)) pid_t gettid( void ) { return syscall( __NR_gettid ); }

#define _USE_BACKTRACE
 
typedef int (*orig_close_f_type)(int fd);

static std::atomic<int> first = {1};
static orig_close_f_type orig_close = NULL;
static FILE *fp = NULL;

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

#pragma weak _close = close
#pragma weak __close = close
extern "C" int close(int fd)
{
    if (first)
    {
        int err = 0;
        char log_file[256] = { "" };

        sprintf(log_file, "/tmp/close_wrapper.%d.log", getpid());

        fp = fopen(log_file, "w");
        if (fp == NULL)
        {
            err = errno;
            fprintf(stderr, "close() wrapper log_file open failed, errno = %d\n", err);
            fp = stderr;
        }

        orig_close = (orig_close_f_type)dlsym(RTLD_NEXT, "close");
        if (orig_close == NULL)
        {
            int err = errno;
            fprintf(fp, "close() wrapper dlsym failed, errno = %d\n", err);
            fflush(fp);
        }
        first = 0;
    }

    char fmt[256] = { "" };
    char timestamp[256] = { "?" };
    struct tm *tm = NULL;
    struct timeval tv;

    int srtn = gettimeofday(&tv, NULL);
    if (srtn == 0)
    {
        if((tm = localtime(&tv.tv_sec)) != NULL)
        {
            strftime(fmt, 255, "%Y-%m-%d %H:%M:%S.%%03u", tm);
            snprintf(timestamp, 255, fmt, tv.tv_usec/1000);
        }
    }

    bool issock = false;
    struct stat sb;
    srtn = fstat(fd, &sb);
    if (srtn == 0)
    {
        if (S_ISSOCK(sb.st_mode))
            issock = true;
    }

    if (!issock)
    {
        if (orig_close == NULL)
        {
            errno = ENOSYS;
            return -1;
        }
        else
        {
            errno = 0;
            return orig_close(fd);
        }
    }

    pthread_mutex_lock(&lock);

    if (orig_close == NULL)
    {
        fprintf(fp, "%s %u %u close() wrapper orig socket close(%d) not found error\n", timestamp, getpid(), gettid(), fd);
        errno = ENOSYS;
        srtn = -1;
    }
    else
    {
        srtn = orig_close(fd);
        fprintf(fp, "%s %u %u close() wrapper orig socket close(%d) returns %d\n", timestamp, getpid(), gettid(), fd, srtn);
    }

#ifdef _USE_BACKTRACE
    int nptrs = 0;
    void *buffer[100];
    char **strings = NULL;

    nptrs = backtrace(buffer, 25);
    strings = backtrace_symbols(buffer, nptrs);
    if (strings != NULL)
    {
        for (int j = 0; j < nptrs; j++)
        {
            char symb[256] = { "" };
            char *strt = strchr(strings[j], '(');
            if (strt)
            {
                char *end1 = strchr(strt, '+');
                if (end1 == NULL)
                    end1 = strchr(strt, ')');
                if (end1 != NULL)
                {
                    end1--;
                    int i = 0;
                    char *p = strt;
                    while (p++ != end1)
                        symb[i++] = *p;
                    symb[i] = '\0';
                    int status = 0;
                    char *demangled = abi::__cxa_demangle(symb, nullptr, nullptr, &status);
                    if ( (status == 0) && (demangled) )
                    {
                        fprintf(fp, "%-*s%s\n", j+1, " ", demangled);
                        free(demangled);
                    }
                    else
                        fprintf(fp, "%-*s%s\n", j+1, " ", strings[j]);
                }
                else
                    fprintf(fp, "%-*s%s\n", j+1, " ", strings[j]);
            }
            else
                fprintf(fp, "%-*s%s\n", j+1, " ", strings[j]);
        }
        free(strings);
    }
#else
    unw_cursor_t cursor;
    unw_context_t context;
    
    // Initialize cursor to current frame for local unwinding.
    unw_getcontext(&context);
    unw_init_local(&cursor, &context);
    
    // Unwind frames one by one, going up the frame stack.
    while (unw_step(&cursor) > 0) {
        unw_word_t offset, pc;
        unw_get_reg(&cursor, UNW_REG_IP, &pc);
        if (pc == 0) {
            break;
        }
        fprintf(fp, "0x%lx:", pc);
      
        char sym[256];
        if (unw_get_proc_name(&cursor, sym, sizeof(sym), &offset) == 0) {
            char *nameptr = sym;
            int status;
            char *demangled = abi::__cxa_demangle(sym, nullptr, nullptr, &status);
            if (status == 0) {
                nameptr = demangled;
            }
            fprintf(fp, " (%s+0x%lx)\n", nameptr, offset);
            free(demangled);
        } else {
            fprintf(fp, " -- error: unable to obtain symbol name for this frame\n");
        }
    }
#endif

    fflush(fp);

    pthread_mutex_unlock(&lock);

    return srtn;
}

