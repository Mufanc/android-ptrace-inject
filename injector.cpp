#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <map>
#include <string>
#include <type_traits>

#include <dlfcn.h>
#include <elf.h>
#include <sys/mman.h>
#include <sys/ptrace.h>
#include <sys/uio.h>
#include <sys/unistd.h>
#include <sys/wait.h>


#define PREFIX "[ Hooker ] "
#define INFO(PATTERN, ...) printf(PREFIX PATTERN "\n", ## __VA_ARGS__); fflush(stdout)
#define ERROR(TITLE) perror(PREFIX TITLE); exit(1)


#define pt_regs user_pt_regs

#ifndef PTRACE_GETREGS
#define PTRACE_GETREGS PTRACE_GETREGSET
#endif

#ifndef PTRACE_SETREGS
#define PTRACE_SETREGS PTRACE_SETREGSET
#endif

#define ARM_pc pc
#define ARM_sp sp
#define ARM_cpsr pstate
#define ARM_lr regs[30]
#define ARM_r0 regs[0]

#define CPSR_T_MASK (1u << 5)


uintptr_t get_module_base(pid_t pid, std::string libpath) {
    static std::map<std::pair<pid_t, std::string>, uintptr_t> cache;

    std::pair<pid_t, std::string> key(pid, libpath);
    if (cache.contains(key)) {
        return cache[key];
    }

    char maps_path[PATH_MAX];
    sprintf(maps_path, "/proc/%d/maps", pid);

    FILE *maps = fopen(maps_path, "r");
    if (maps == nullptr) {
        ERROR("open maps");
        return 0;
    }

    void *addr = nullptr;
    char path[PATH_MAX], perms[8], offset[16];

    while (fscanf(maps, "%p-%*p %s %s %*s %*s %[^\n]", &addr, perms, offset, path) != EOF) {
        if (perms[2] != 'x') continue;
        if (strcmp(path, libpath.c_str()) != 0) continue;
        INFO("%s: %p, offset: %s", libpath.c_str(), addr, offset);
        break;
    }

    fclose(maps);

    return cache[key] = (uintptr_t) addr - strtoull(offset, nullptr, 16);
}

uintptr_t get_func_addr(pid_t pid, std::string libpath, uintptr_t local_func) {
    uintptr_t local_base = get_module_base(getpid(), libpath);
    uintptr_t remote_base = get_module_base(pid, libpath);
    uintptr_t offset = local_func - local_base;
    INFO("function offset: %lx", offset);
    return remote_base + offset;
}

std::string get_library_path(std::string libname) {
    static std::map<std::string, std::string> cache;
    
    if (cache.contains(libname)) {
        return cache[libname];
    }

    char tmpstr[PATH_MAX], libpath[PATH_MAX];
    snprintf(tmpstr, sizeof(tmpstr), "/system/lib64/%s.so", libname.c_str());

    if (realpath(tmpstr, libpath) == nullptr) {
        ERROR("resolve");
    }

    return cache[libname] = libpath;
}

void ptrace_attach(pid_t pid) {
    if (ptrace(PTRACE_ATTACH, pid, nullptr, nullptr) == -1) {
        ERROR("attach");
    }
    if (waitpid(pid, nullptr, WUNTRACED) != pid) {
        ERROR("waitpid");
    }
    INFO("attached to pid: %d", pid);
}

void ptrace_detach(pid_t pid) {
    if (ptrace(PTRACE_DETACH, pid, nullptr, nullptr) == -1) {
        ERROR("detach");
    }
}

void ptrace_get_regs(pid_t pid, pt_regs *regs) {
    iovec iov {
        .iov_base = regs,
        .iov_len = sizeof(*regs)
    };
    if (ptrace(PTRACE_GETREGS, pid, NT_PRSTATUS, &iov) == -1) {
        ERROR("backup regs");
    }
}

void ptrace_set_regs(pid_t pid, pt_regs *regs) {
    iovec iov {
        .iov_base = regs,
        .iov_len = sizeof(*regs)
    };
    if (ptrace(PTRACE_SETREGS, pid, NT_PRSTATUS, &iov) == -1) {
        ERROR("restore regs");
    }
}

void ptrace_continue(pid_t pid) {
    if (ptrace(PTRACE_CONT, pid, nullptr, nullptr)) {
        ERROR("continue");
    }
}

void ptrace_write_data(pid_t pid, void *addr, void *buffer, size_t bufsize) {
    iovec from {
        .iov_base = buffer,
        .iov_len = bufsize
    };
    iovec to {
        .iov_base = addr,
        .iov_len = bufsize
    };
    ssize_t count;
    if ((count = process_vm_writev(pid, &from, 1, &to, 1, 0)) == -1) {
        ERROR("write memory");
    }
    INFO("copied %zd bytes of data to target", count);
}

template<class... T>
void call_remote(pid_t pid, pt_regs *regs, uintptr_t addr, T... args) {
    size_t index = 0;

    INFO("calling function at %lx", addr);
    for (int64_t it : { args... }) {
        INFO("args[%zu] = %ld", index, it);
        regs->regs[index++] = it;
    }

    regs->ARM_pc = addr;

    if (regs->ARM_pc & 1) {
        regs->ARM_pc &= ~1;
        regs->ARM_cpsr |= CPSR_T_MASK;
    } else {
        regs->ARM_cpsr &= ~CPSR_T_MASK;
    }

    regs->ARM_lr = 0;

    ptrace_set_regs(pid, regs);

    int status = 0;
    while (status != ((SIGSEGV << 8) | 0x7f)) {
        ptrace_continue(pid);
        waitpid(pid, &status, WUNTRACED);
        INFO("substatus: 0x%08x", status);
    }

    ptrace_get_regs(pid, regs);
}


#define USE_REMOTE_FUNC(libname, func) template <class T, class... Args>  \
T func##_remote (pid_t pid, pt_regs *regs, Args... args) {  \
    auto path = get_library_path(#libname);  \
    uintptr_t addr = get_func_addr(pid, path, (uintptr_t) func);  \
    call_remote(pid, regs, addr, args...);  \
    return (T) regs->ARM_r0;  \
}


USE_REMOTE_FUNC(libc, mmap)
USE_REMOTE_FUNC(libdl, dlopen)
USE_REMOTE_FUNC(libdl, dlclose)


int main() {
    pid_t pid; scanf("%d", &pid);

   ptrace_attach(pid);

    pt_regs regs, backup_regs;
    ptrace_get_regs(pid, &regs);
    memcpy(&backup_regs, &regs, sizeof(regs));

    void *buffer = mmap_remote<void *>(
        pid, &regs, 
        (int64_t) nullptr, 
        (int64_t) getpagesize(), 
        (int64_t) PROT_READ | PROT_WRITE | PROT_EXEC,
        (int64_t) MAP_ANONYMOUS | MAP_PRIVATE,
        0L,
        0L
    );
    INFO("buffer: %p", buffer);

    const char *INJECT = "/proc/self/cwd/hack.so";
    ptrace_write_data(pid, (void *) buffer, (void *) INJECT, strlen(INJECT));

    void *handle = dlopen_remote<void *>(
        pid, &regs,
        (int64_t) buffer,
        1L /* RTLD_LAZY */
    );
    INFO("handle: %p", handle);

    // /*
    dlclose_remote<int>(
        pid, &regs, 
        (int64_t) handle
    );
    // */

    ptrace_set_regs(pid, &backup_regs);
    INFO("registers restored");

    ptrace_detach(pid);
 
    char command[128];
    snprintf(command, sizeof(command), "pmap %d | grep rwx", pid);
    INFO("%s", command);
    execlp("sh", "sh", "-c", command, nullptr);
    ERROR("pmap");

    return 0;
} 
