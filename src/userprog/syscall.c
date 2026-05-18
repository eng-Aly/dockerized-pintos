#include "userprog/syscall.h"

#include <stdio.h>
#include <syscall-nr.h>

#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/synch.h"

#include "userprog/pagedir.h"
#include "userprog/process.h"

#include "devices/shutdown.h"
#include "devices/input.h"

#include "filesys/filesys.h"
#include "filesys/file.h"
#include "threads/malloc.h"

#define ERROR -1

/* ---------- Prototypes ---------- */

static void syscall_handler (struct intr_frame *f);

static int validate_uaddr (const void *uaddr);
static void validate_buffer (const void *buffer, unsigned size);
static void validate_string (const char *str);
static void verify_ptr (const void *ptr);

static void get_args (void *esp, int *args, int count);

/* process syscalls */
static void sys_halt (void);
static void sys_exit (int status);
static tid_t sys_exec (const char *cmd_line);

/* file syscalls */
static int sys_filesize (int fd);
static int sys_read (int fd, void *buffer, unsigned size);
static int sys_write (int fd, const void *buffer, unsigned size);

static struct file *get_file (int fd);
static void sys_seek (int fd, unsigned position);
static unsigned sys_tell (int fd);
static void sys_close (int fd);



static bool sys_create (const char *file, unsigned initial_size);
static bool sys_remove (const char *file);
static int  sys_open   (const char *file);
/* ---------- Global Locks ---------- */

struct lock filesys_lock;
struct lock fs_lock;

/* ---------- Init ---------- */

void
syscall_init (void)
{
  lock_init (&filesys_lock);
  lock_init (&fs_lock);

  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

/* ---------- Syscall Handler ---------- */

static void
syscall_handler (struct intr_frame *f)
{
  validate_buffer(f->esp, sizeof(int));

  int syscall_num = *(int *)f->esp;

  int args[3];

  switch (syscall_num)
    {
      /* ---------- Process Syscalls ---------- */

      case SYS_HALT:
        sys_halt ();
        break;

      case SYS_EXIT:
        get_args (f->esp, args, 1);
        sys_exit (args[0]);
        break;

      case SYS_EXEC:
        get_args (f->esp, args, 1);

        validate_string ((const char *) args[0]);

        args[0] = validate_uaddr ((const void *) args[0]);

        f->eax = sys_exec ((const char *) args[0]);
        break;

      case SYS_WAIT:
        get_args(f->esp, args, 1);

        f->eax = process_wait((tid_t) args[0]);

        break;
      /* ---------- File Syscalls ---------- */
      case SYS_OPEN:
        get_args(f->esp, args, 1);
        validate_string((const char *) args[0]);
        args[0] = validate_uaddr((const void *) args[0]);
        f->eax = sys_open((const char *) args[0]);
        break; 

      case SYS_CREATE:
        get_args(f->esp, args, 2);
        validate_string((const char *) args[0]);
        args[0] = validate_uaddr((const void *) args[0]);
        f->eax = sys_create((const char *) args[0], (unsigned) args[1]);
        break;

      case SYS_REMOVE:
        get_args(f->esp, args, 1);
        validate_string((const char *) args[0]);
        args[0] = validate_uaddr((const void *) args[0]);
        f->eax = sys_remove((const char *) args[0]);
        break;
      /* ---------- File Syscalls ---------- */

      case SYS_FILESIZE:
        get_args(f->esp, args, 1);

        f->eax = sys_filesize(args[0]);
        break;

      case SYS_READ:
        get_args(f->esp, args, 3);

        validate_buffer((const void *) args[1], (unsigned) args[2]);

        f->eax = sys_read(args[0], (void *) args[1], args[2]);
        break;

      case SYS_WRITE:
        get_args(f->esp, args, 3);

        validate_buffer((const void *) args[1], (unsigned) args[2]);

        f->eax = sys_write(args[0], (const void *) args[1], args[2]);
        break;

      case SYS_SEEK:
        get_args (f->esp, args, 2);

        f->eax = 0;

        sys_seek ((int) args[0], (unsigned) args[1]);
        break;

      case SYS_TELL:
        get_args (f->esp, args, 1);

        f->eax = sys_tell ((int) args[0]);
        break;

      case SYS_CLOSE:
        get_args (f->esp, args, 1);

        sys_close ((int) args[0]);
        break;

      default:
        sys_exit (ERROR);
        break;
    }
}

/* ---------- Validation ---------- */

static int
validate_uaddr (const void *uaddr)
{
  verify_ptr (uaddr);

  if (pagedir_get_page (thread_current ()->pagedir, uaddr) == NULL)
    {
      sys_exit (ERROR);
    }
  else
    {
      return (int*) uaddr;
    }
}

static void
validate_buffer (const void *buffer, unsigned size)
{
  unsigned i;

  char *buf = (char *) buffer;

  for (i = 0; i < size; i++)
    {
      validate_uaddr ((const void *) (buf + i));
    }
}

static void
validate_string (const char *str)
{
    while (true)
    {
        validate_uaddr(str);

        if (*str == '\0')
            return;

        str++;
    }
}

/* ---------- Stack Helpers ---------- */

static void
get_args (void *esp, int *args, int count)
{
    int i;

    for (i = 0; i < count; i++)
    {
        int *arg_ptr = (int *) esp + i + 1;

        validate_buffer(arg_ptr, sizeof(int));

        args[i] = *arg_ptr;
    }
}

static void
verify_ptr (const void *ptr)
{
  if (ptr < (void *) 0x08048000 || !is_user_vaddr (ptr))
    {
      sys_exit (ERROR);
    }
}

/* ---------- Process Syscalls ---------- */

static void
sys_halt (void)
{
  shutdown_power_off ();
}

static void
sys_exit (int status)
{
  struct thread *cur = thread_current ();

  cur->exit_status = status;
  printf("%s: exit(%d)\n", cur->name, status);
  thread_exit ();
}

static tid_t
sys_exec (const char *cmd_line)
{
  validate_string (cmd_line);

  return process_execute (cmd_line);
}

/* ---------- Added File Syscalls ---------- */

static int
sys_filesize (int fd)
{
  struct file *f = thread_current ()->fd_table[fd]->file;

  if (f == NULL)
    {
      return -1;
    }

  lock_acquire (&fs_lock);

  int size = file_length (f);

  lock_release (&fs_lock);

  return size;
}

static int
sys_read (int fd, void *buffer, unsigned size)
{
  validate_buffer (buffer, size);
  if (fd < 0 || fd >= MAX_FILES || fd == 1) return -1;

  if (fd == 0)
    {
      uint8_t *buf = (uint8_t *) buffer;

      for (unsigned i = 0; i < size; i++)
        {
          buf[i] = input_getc ();
        }

      return size;
    }
  if (thread_current ()->fd_table[fd] == NULL) return -1;
  struct file *f = thread_current ()->fd_table[fd]->file;

  if (f == NULL)
    {
      return -1;
    }

  lock_acquire (&fs_lock);

  int bytes_read = file_read (f, buffer, size);

  lock_release (&fs_lock);

  return bytes_read;
}

static int
sys_write (int fd, const void *buffer, unsigned size)
{
  validate_buffer (buffer, size);

  if (fd < 0 || fd >= MAX_FILES || fd == 0) return -1;

  if (fd == 1)
    {
      putbuf (buffer, size);
      return size;
    }

  if (thread_current ()->fd_table[fd] == NULL) return -1;

  struct file *f = thread_current ()->fd_table[fd]->file;

  if (f == NULL)
    {
      return -1;
    }

  lock_acquire (&fs_lock);

  int bytes_written = file_write (f, buffer, size);

  lock_release (&fs_lock);

  return bytes_written;
}

/* ---------- File Helpers ---------- */

static struct file *
get_file (int fd)
{
  struct thread *t = thread_current ();

  if (fd < 2 || fd >= MAX_FILES)
    {
      return NULL;
    }

  return t->fd_table[fd]->file;
}

/* ---------- File Syscalls ---------- */

static void
sys_seek (int fd, unsigned position)
{
  struct file *f = get_file (fd);

  if (f != NULL)
    {
      lock_acquire (&filesys_lock);

      file_seek (f, position);

      lock_release (&filesys_lock);
    }
}

static unsigned
sys_tell (int fd)
{
  struct file *f = get_file (fd);

  if (f == NULL)
    {
      return -1;
    }

  lock_acquire (&filesys_lock);

  unsigned pos = (unsigned) file_tell (f);

  lock_release (&filesys_lock);

  return pos;
}

static void
sys_close (int fd)
{
  struct thread *t = thread_current ();

  if (fd < 2 || fd >= MAX_FILES)
    {
      return;
    }

  struct file *f = t->fd_table[fd]->file;

  if (f == NULL)
    {
      return;
    }

  lock_acquire (&filesys_lock);

  file_close (f);

  lock_release (&filesys_lock);

  t->fd_table[fd]->file = NULL;
  t->fd_table[fd]->fd = -1;
}
/*create functions for  create,open and remove*/
static bool
sys_create (const char *file, unsigned initial_size)
{
    validate_string(file);

    lock_acquire(&filesys_lock);

    bool success = filesys_create(file, initial_size);

    lock_release(&filesys_lock);

    return success;
}

static bool
sys_remove (const char *file)
{
  lock_acquire (&filesys_lock);
  bool success = filesys_remove (file);
  lock_release (&filesys_lock);
  return success;
}

static int
sys_open (const char *file)
{
    validate_string(file);

    lock_acquire(&filesys_lock);

    struct file *f = filesys_open(file);

    lock_release(&filesys_lock);

    if (f == NULL)
        return -1;

    struct thread *t = thread_current();

    int fd;

    for (fd = 2; fd < MAX_FILES; fd++)
    {
        if (t->fd_table[fd] == NULL)
            break;
    }

    if (fd == MAX_FILES)
    {
        file_close(f);
        return -1;
    }

    struct file_descriptor *fd_entry =
        malloc(sizeof(struct file_descriptor));

    if (fd_entry == NULL)
    {
        file_close(f);
        return -1;
    }

    fd_entry->fd = fd;
    fd_entry->file = f;

    t->fd_table[fd] = fd_entry;

    return fd;
}