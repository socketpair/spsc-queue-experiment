#pragma once
// https://dept-info.labri.fr/~denis/Enseignement/2008-IR/Articles/01-futex.pdf

// define XXX extern
#define XXX static

#include <errno.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>		/* For mode constants */
#include <fcntl.h>		/* For O_* constants */
#include <sys/types.h>
#include <linux/futex.h>	/* Definition of FUTEX_* constants */
#include <sys/syscall.h>	/* Definition of SYS_* constants */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <sys/random.h>
#include <assert.h>
#include <err.h>
#include <time.h>
#include <pthread.h>

typedef struct
{
  // uint32 is a requirement. futex is 32-bit even on 64-bit machine.
  uint32_t put_index __attribute__((aligned (64)));	// index. where to put
  uint32_t get_index __attribute__((aligned (64)));	// index. where to read.
  uint32_t mask __attribute__((aligned (64)));
  uint8_t cell_bits;
} ring;

static uint32_t atomic_load (const uint32_t * restrict ptr)
{
  return __atomic_load_n (ptr, __ATOMIC_SEQ_CST /*__ATOMIC_ACQUIRE */ );
}

static void atomic_store (uint32_t * restrict ptr, uint32_t value)
{
  __atomic_store_n (ptr, value, __ATOMIC_SEQ_CST /*__ATOMIC_RELEASE */ );
}

static uint32_t myinc (const ring * restrict r, uint32_t value)
{
  return (value + 1) & r->mask;
}

XXX int ring_alloc (const char *name, uint8_t slots_bits, uint8_t cell_bits)
{
  if (!slots_bits || slots_bits > 31)
    return 0;

  // 4096 == sysconf(_SC_PAGE_SIZE)

  assert (sizeof (ring) <= 4096);

  size_t datasize = ((size_t) 1) << (slots_bits + cell_bits);

  // TODO: open .tmp and rename in the end.
  // remove .tmp before.
  // use *at syscalls
  int fd;
  if ((fd = shm_open (name, O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC | O_NOCTTY, 0600)) == -1)
    return 0;

  // Should be sizeof(ring) + datasize, but alignment for slots.
  if (fallocate (fd, 0, 0, 4096 + datasize) == -1)
  {
    if (close (fd))
      err (EXIT_FAILURE, "fd_close");
    return 0;
  }

  // TODO: hugepages ?
  ring *r = mmap (NULL, 4096 + datasize, PROT_WRITE | PROT_READ,
		  MAP_SHARED_VALIDATE | MAP_LOCKED | MAP_POPULATE, fd, 0);

  if (close (fd))
    err (EXIT_FAILURE, "fd close");

  if (!r)
    return 0;			// BUG uninitialized file (!)

  r->put_index = 0;
  r->get_index = 0;
  r->mask = (((uint32_t) 1) << slots_bits) - 1;
  r->cell_bits = cell_bits;

  // TODO: link unnamed file
  if (munmap (r, 4096 + datasize))
    err (EXIT_FAILURE, "munmap");

  return 1;
}

XXX ring *ring_open (const char *name)
{
  int fd;
  if ((fd = shm_open (name, O_RDWR | O_CLOEXEC | O_NOCTTY, 0)) == -1)
    return NULL;

  struct stat st;

  if (fstat (fd, &st) == -1)
    err (EXIT_FAILURE, "fstat");

  ring *r = mmap (NULL, st.st_size, PROT_WRITE | PROT_READ,
		  MAP_SHARED_VALIDATE | MAP_LOCKED | MAP_POPULATE, fd, 0);
  if (close (fd))
    err (EXIT_FAILURE, "fd close");

  if (!r)
    return NULL;

  if ((uint64_t) st.st_size != 4096 + (size_t) (r->mask + 1) * (((size_t) 1) << r->cell_bits))
  {
    if (munmap (r, st.st_size))
      err (EXIT_FAILURE, "munmap");
    return NULL;
  }

  return r;
}

XXX void ring_close (ring * r)
{
  if (munmap (r, 4096 + (size_t) (r->mask + 1) * (((size_t) 1) << r->cell_bits)))
    err (EXIT_FAILURE, "munmap");
}

XXX int ring_put_prepare (ring * restrict r, void **restrict data)
{
  if (atomic_load (&r->get_index) == myinc (r, r->put_index))	// QUEUE is FULL
    return 0;
  *data = (uint8_t *) r + 4096 + (((size_t) r->put_index) << r->cell_bits);
  return 1;
}

XXX int ring_put_commit (ring * restrict r)
{
  // https://stackoverflow.com/questions/60403801/x86-lock-vs-sfence-mfence
  // mfence
  // __sync_synchronize()
  // __atomic_thread_fence()
  // cflush ?

  // PUT index can be changed only by putter.
  // get index can be changed in parallel thread
  atomic_store (&r->put_index, myinc (r, r->put_index));

  if (myinc (r, atomic_load (&r->get_index)) != r->put_index)
    return 0;

again:
  long ret = syscall (SYS_futex, &r->put_index, FUTEX_WAKE, 1, NULL, 0);
  if (ret == -1)
  {
    if (errno == EAGAIN)
      goto again;

    err (EXIT_FAILURE, "FUTEX_WAKE");
  }
//  if (ret == 0) {  printf ("no one is awoken?!\n"); }

  return 1;
}

XXX void ring_get_prepare (ring * restrict r, const void **restrict buf)
{
//  const struct timespec ts = { 2, 0 };

  while (atomic_load (&r->put_index) == r->get_index)
  {
    int futex_ret = syscall (SYS_futex, &r->put_index, FUTEX_WAIT, r->get_index, /*&ts */ NULL, NULL, 0);
    if (futex_ret == 0)
      continue;
    if (futex_ret != -1)
      errx (EXIT_FAILURE, "FUTEX_WAIT returned something strange");
    switch (errno)
    {
    case EAGAIN:
      continue;
//    case ETIMEDOUT:
//      fprintf (stderr, "futex timeout\n. get_index=%" PRIu32 ", put_index=%" PRIu32 "\n", r->get_index, atomic_load (&r->put_index));
//      break;
    default:
      err (EXIT_FAILURE, "FUTEX_WAIT");
    }
  }

  *buf = (uint8_t *) r + 4096 + (((size_t) r->get_index) << r->cell_bits);
}

XXX void ring_get_commit (ring * restrict r)
{
  atomic_store (&r->get_index, myinc (r, r->get_index));
}
