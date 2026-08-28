/* lib/sp_process.c -- Process.spawn and Process.waitpid2 (CRuby-compatible)
 *
 * Lives in its own TU because the codegen calls sp_process_spawn
 * with pre-resolved positional args (in_fd, out_fd, err_fd, pgroup,
 * rlimit_cpu, rlimit_as, chdir). All opts-hash unpacking happens at
 * compile time in the codegen; the runtime just needs primitive int
 * / int-fd / int-pgroup / int-rlimit values. The cmd is either a
 * String or an Array (boxed as a PolyArray). args is a PolyArray of
 * extra String args appended after cmd.
 *
 * The TU deliberately does NOT include spinel_rt.h (it pulls in only
 * sp_alloc.h and the low-level headers). spinel_rt.h's static-inline
 * family references sp_class_to_s / sp_sym_to_s which are emitted
 * per-program by the codegen and not present in the runtime archive;
 * including spinel_rt.h from here would force those symbols into the
 * link and break the build.
 *
 * Process.spawn(cmd, *args, opts) -> child pid
 * Process.waitpid2(pid) -> [pid, raw_status]
 *
 * opts is a PolyArray of 7 elements in this order:
 *   [0] in_fd        - Integer fd (>= 0), -1 for false/nil, IO was
 *                      resolved to its fd by the codegen, String path
 *                      was opened by the codegen
 *   [1] out_fd       - same conventions
 *   [2] err_fd       - same conventions
 *   [3] pgroup       - Integer (0=inherit, 1=new, >1=specific pgid)
 *   [4] rlimit_cpu   - Integer (seconds), nil = no limit
 *   [5] rlimit_as    - Integer (bytes), nil = no limit
 *   [6] chdir        - String path or nil
 *
 * IO and String values in opts[0..2] are resolved to fds by the
 * codegen (which has access to sp_File_fileno and open(2)). The
 * [:child, :out|:err|Integer] form is also resolved by the codegen
 * (it sees the literal array at parse time).
 */

#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <errno.h>

#include "sp_alloc.h"   /* sp_PolyArray, sp_RbVal, sp_box_*, sp_raise_cls */
#include "sp_process_status.h"   /* sp_ProcessStatus, sp_box_process_status */

/* Local error-message builder. Returns a static buffer; copy the
   result before another call. Avoids sp_sprintf which would pull
   sp_class_to_s / sp_sym_to_s into the link. */
static char sp_err_buf[512];
static const char *sp_errf_errno(const char *prefix, int err) {
  snprintf(sp_err_buf, sizeof sp_err_buf, "%s - %s", prefix, strerror(err));
  return sp_err_buf;
}

/* Apply the redirect in the child: dup2 src_fd onto target_fd, close src.
   If src_fd is -1 (false), redirect to /dev/null. */
static void apply_redirect(int target_fd, int src_fd) {
  int fd = src_fd;
  if (fd < 0) {
    fd = open("/dev/null", target_fd == 1 ? O_WRONLY : O_RDONLY);
    if (fd < 0) return;
  }
  if (fd != target_fd) {
    dup2(fd, target_fd);
    if (fd > 2) close(fd);
  }
}

/* Extract a resolved Integer fd from a pre-resolved opts slot. The
   codegen turns IO/String/false into Integer before passing; we
   just unbox. -1 means "not set" (/dev/null). */
static int slot_to_fd(sp_RbVal v) {
  if (v.tag == SP_TAG_NIL) return -1;
  if (v.tag == SP_TAG_BOOL && v.v.i == 0) return -1;
  if (v.tag == SP_TAG_INT) return (int)v.v.i;
  sp_raise_cls("TypeError", "redirect slot must be Integer (codegen bug)");
  return -1;
}

sp_int sp_process_spawn(sp_RbVal cmd, sp_RbVal args_box,
                        sp_RbVal opts_box) {
  SP_GC_ROOT_RBVAL(cmd);
  SP_GC_ROOT_RBVAL(args_box);
  SP_GC_ROOT_RBVAL(opts_box);

  /* opts_box must be a PolyArray of 7 elements. */
  if (opts_box.tag != SP_TAG_OBJ ||
      opts_box.cls_id != SP_BUILTIN_POLY_ARRAY) {
    sp_raise_cls("TypeError", "opts must be a PolyArray (codegen bug)");
  }
  sp_PolyArray *opts = (sp_PolyArray *)opts_box.v.p;
  if (opts->len < 7) {
    sp_raise_cls("ArgumentError", "opts array too short (codegen bug)");
  }
  int in_fd  = slot_to_fd(opts->data[0]);
  int out_fd = slot_to_fd(opts->data[1]);
  int err_fd = slot_to_fd(opts->data[2]);

  int pgroup = 0;
  if (opts->data[3].tag == SP_TAG_NIL) pgroup = 0;
  else if (opts->data[3].tag == SP_TAG_BOOL && opts->data[3].v.i == 1) pgroup = 1;
  else if (opts->data[3].tag == SP_TAG_INT) pgroup = (int)opts->data[3].v.i;
  else sp_raise_cls("TypeError", "pgroup must be true, 0, or Integer");

  int rlimit_cpu_set = 0;
  rlim_t rlimit_cpu_val = 0;
  if (opts->data[4].tag == SP_TAG_INT) { rlimit_cpu_set = 1; rlimit_cpu_val = (rlim_t)opts->data[4].v.i; }
  else if (opts->data[4].tag != SP_TAG_NIL)
    sp_raise_cls("TypeError", "rlimit_cpu must be Integer");

  int rlimit_as_set = 0;
  rlim_t rlimit_as_val = 0;
  if (opts->data[5].tag == SP_TAG_INT) { rlimit_as_set = 1; rlimit_as_val = (rlim_t)opts->data[5].v.i; }
  else if (opts->data[5].tag != SP_TAG_NIL)
    sp_raise_cls("TypeError", "rlimit_as must be Integer");

  const char *chdir_to = NULL;
  if (opts->data[6].tag == SP_TAG_STR) chdir_to = opts->data[6].v.s;
  else if (opts->data[6].tag != SP_TAG_NIL)
    sp_raise_cls("TypeError", "chdir must be a String");

  /* Resolve cmd + args into argv. */
  const char *prog = NULL;
  char **argv = NULL;
  sp_PolyArray *cmd_arr = NULL;
  sp_PolyArray *args_arr = NULL;
  int extra_from_cmd = 0;
  int extra_from_args = 0;

  if (cmd.tag == SP_TAG_STR) {
    prog = cmd.v.s;
    if (args_box.tag == SP_TAG_OBJ &&
        args_box.cls_id == SP_BUILTIN_POLY_ARRAY) {
      args_arr = (sp_PolyArray *)args_box.v.p;
    } else if (args_box.tag != SP_TAG_NIL) {
      sp_raise_cls("TypeError", "args must be a PolyArray of extra args");
    }
  } else if (cmd.tag == SP_TAG_OBJ &&
             cmd.cls_id == SP_BUILTIN_POLY_ARRAY) {
    cmd_arr = (sp_PolyArray *)cmd.v.p;
    if (cmd_arr->len < 1) sp_raise_cls("ArgumentError", "empty command array");
    if (cmd_arr->data[0].tag != SP_TAG_STR)
      sp_raise_cls("ArgumentError", "command[0] must be a String");
    prog = cmd_arr->data[0].v.s;
    extra_from_cmd = cmd_arr->len - 1;
    if (args_box.tag == SP_TAG_OBJ &&
        args_box.cls_id == SP_BUILTIN_POLY_ARRAY) {
      args_arr = (sp_PolyArray *)args_box.v.p;
    }
  } else {
    sp_raise_cls("TypeError",
                 "wrong first argument type (expected String or Array)");
  }
  if (args_arr) extra_from_args = (int)args_arr->len;

  int total = 1 + extra_from_cmd + extra_from_args;
  argv = (char **)malloc(sizeof(char *) * (size_t)(total + 1));
  if (!argv) sp_raise_cls("NoMemoryError", "out of memory");
  argv[0] = (char *)prog;
  int ai = 1;
  if (cmd_arr) {
    for (int i = 1; i < cmd_arr->len; i++) {
      if (cmd_arr->data[i].tag != SP_TAG_STR)
        sp_raise_cls("ArgumentError", "command array element must be a String");
      argv[ai++] = (char *)cmd_arr->data[i].v.s;
    }
  }
  if (args_arr) {
    for (int i = 0; i < args_arr->len; i++) {
      if (args_arr->data[i].tag != SP_TAG_STR)
        sp_raise_cls("ArgumentError", "spawn args must be Strings");
      argv[ai++] = (char *)args_arr->data[i].v.s;
    }
  }
  argv[ai] = NULL;

  /* Pre-exec error pipe: the child writes the exec errno here if execve
     fails, so the parent can raise the matching Errno (CRuby raises
     Errno::ENOENT for "no such file or directory" instead of returning
     a dead pid). FD_CLOEXEC on the write end so a successful execvp
     closes it; the parent then sees a zero-byte read and no raise. */
  int err_pipe[2];
  if (pipe(err_pipe) < 0) {
    free(argv);
    sp_raise_cls("SystemCallError", sp_errf_errno("pipe failed", errno));
  }

  pid_t pid = fork();
  if (pid < 0) {
    free(argv);
    sp_raise_cls("SystemCallError", sp_errf_errno("fork failed", errno));
  }
  if (pid == 0) {
    /* CHILD. If execve fails, write the errno to the parent's pipe
       and exit 127; the parent will raise the matching Errno
       (Errno::ENOENT for "no such file") to match CRuby semantics.
       Using a pipe (not relying on the child's exit code alone)
       because the exit code is the same regardless of exec failure
       reason. */
    close(err_pipe[0]);
    if (fcntl(err_pipe[1], F_SETFD, FD_CLOEXEC) < 0) { _exit(126); }
    if (chdir_to) {
      if (chdir(chdir_to) != 0) {
        int e = errno;
        (void)!write(err_pipe[1], &e, sizeof e);
        _exit(127);
      }
    }
    if (rlimit_cpu_set) {
      struct rlimit rl = { rlimit_cpu_val, RLIM_INFINITY };
      setrlimit(RLIMIT_CPU, &rl);
    }
    if (rlimit_as_set) {
      struct rlimit rl = { rlimit_as_val, RLIM_INFINITY };
      setrlimit(RLIMIT_AS, &rl);
    }
    if (pgroup == 1) {
      setpgid(0, 0);
    } else if (pgroup > 1) {
      setpgid(0, pgroup);
    }
    apply_redirect(0, in_fd);
    apply_redirect(1, out_fd);
    apply_redirect(2, err_fd);
    execvp(prog, argv);
    /* exec returned: failure. Send the errno to the parent. */
    int e = errno;
    (void)!write(err_pipe[1], &e, sizeof e);
    _exit(127);
  }
  /* PARENT. Close the child's write end, read the errno if any. */
  close(err_pipe[1]);
  int exec_errno = 0;
  ssize_t got = read(err_pipe[0], &exec_errno, sizeof exec_errno);
  close(err_pipe[0]);
  free(argv);
  if (got > 0) {
    /* child failed to exec; the child has already exited 127, so the
       waitpid2 caller will still find a (zombie) pid, but we raise the
       CRuby-style exception first. The pid is leaked but harmless: the
       init process reaps the zombie. */
    errno = exec_errno;
    sp_raise_cls(errno == ENOENT ? "Errno::ENOENT" :
                 errno == EACCES ? "Errno::EACCES" :
                 "SystemCallError",
                 sp_errf_errno("cannot execute", errno));
  }
  return (sp_int)pid;
}

sp_PolyArray *sp_process_waitpid2(sp_int pid) {
  int status = 0;
  pid_t r;
  do {
    r = waitpid((pid_t)pid, &status, 0);
  } while (r < 0 && errno == EINTR);
  if (r < 0) {
    if (errno == ECHILD) {
      sp_raise_cls("Errno::ECHILD", "No child processes");
    }
    sp_raise_cls("SystemCallError", sp_errf_errno("waitpid failed", errno));
  }
  sp_PolyArray *pa = sp_PolyArray_new();
  sp_PolyArray_push(pa, sp_box_int((sp_int)r));
  /* Second element is a Process::Status instance wrapping (pid, status),
     not a raw int -- .signaled? / .termsig dispatch on the boxed cls_id. */
  sp_PolyArray_push(pa, sp_box_process_status(sp_process_status_new((sp_int)r, (sp_int)status)));
  return pa;
}
