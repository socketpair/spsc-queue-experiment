// https://dept-info.labri.fr/~denis/Enseignement/2008-IR/Articles/01-futex.pdf
#define XXX static

#define _GNU_SOURCE
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

#ifdef CRC
#include <zlib.h>		//for crc32c only
#endif

/*
uint32_t crc32c(const uint8_t* restrict value, size_t length)
{
    uint32_t hash_value = 0;

    if ((size_t)value & 1 && length >=1) {
        hash_value =_mm_crc32_u8(hash_value, *value);
        length--;
        value++;
    }

    if((size_t)value & 2 && length >=2) {
        hash_value =_mm_crc32_u16(hash_value, *(const uint16_t*) value);
        length-=2;
        value+=2;
    }

    for(;length >= 4; value += 4, length -= 4)
        hash_value = _mm_crc32_u32(hash_value, *(const uint32_t*) value);

    if ((size_t)value & 2 && length >=2)
    {
        hash_value =_mm_crc32_u16(hash_value, *(const uint16_t*) value);
        length-=2;
        value+=2;
    }

    if (length >=1 ) {
        hash_value = _mm_crc32_u8(hash_value, *value);
        length--;
        value++;
    }
    assert(length==0);
    return hash_value;
}
*/

typedef struct
{
  uint32_t put_index __attribute__ ((aligned (64)));		// index. where to put
  uint32_t get_index __attribute__ ((aligned (64)));		// index. where to read.
  uint32_t mask __attribute__ ((aligned (64)));
  uint8_t cell_bits;
} ring;

static uint32_t atomic_load (const uint32_t * restrict ptr)
{
  return __atomic_load_n (ptr,/* __ATOMIC_SEQ_CST */ __ATOMIC_ACQUIRE );
}

static void atomic_store (uint32_t * restrict ptr, uint32_t value)
{
  __atomic_store_n (ptr, value, /*__ATOMIC_SEQ_CST*/ __ATOMIC_RELEASE );
}

static uint32_t myinc(const ring*r, uint32_t value) {
    return (value + 1) & r->mask;
}

XXX int ring_alloc (const char *name, uint8_t slots_bits, size_t cell_bits)
{
  if (!cell_bits || cell_bits > 13 || !slots_bits || slots_bits > 16)
    return 0;

  // 4096 == sysconf(_SC_PAGE_SIZE)

  assert (sizeof (ring) <= 4096);

  size_t datasize = 1ul << (slots_bits + cell_bits);

  // TODO: open .tmp and rename in the end.
  // remove .tmp before.
  // use *at syscalls
  int fd;
  if ((fd = shm_open (name, O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC | O_NOCTTY, 0600)) == -1)
    return 0;

  // Should be sizeof(ring) + datasize, but alignment.
  if (fallocate (fd, 0, 0, 4096 + datasize) == -1)
  {
    if (close (fd))
      err (EXIT_FAILURE, "fd_close");
    return 0;
  }

  ring *r = mmap (NULL, 4096 + datasize, PROT_WRITE | PROT_READ,
		  MAP_SHARED_VALIDATE | MAP_LOCKED | MAP_POPULATE, fd, 0);

  if (close (fd))
    err (EXIT_FAILURE, "fd close");

  if (!r)
    return 0;			// BUG uninitialized file (!)

  r->put_index = 0;
  r->get_index = 0;
  r->mask = (1 << slots_bits) - 1;
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

  if ((uint64_t) st.st_size != 4096 + (r->mask + 1) * (1ull << r->cell_bits))
  {
    if (munmap (r, st.st_size))
      err (EXIT_FAILURE, "munmap");
    return NULL;
  }

  return r;
}

XXX void ring_close (ring * r)
{
  if (munmap (r, 4096 + (r->mask + 1) * (1ull << r->cell_bits)))
    err (EXIT_FAILURE, "munmap");
}

XXX int ring_put_prepare (ring * restrict r, void **restrict data)
{
  if (atomic_load (&r->get_index) == myinc(r, r->put_index))	// QUEUE is FULL
    return 0;
  *data = (uint8_t *) r + 4096 + (r->put_index << r->cell_bits);
  return 1;
}

XXX int ring_put_commit (ring * restrict r)
{
  // https://stackoverflow.com/questions/60403801/x86-lock-vs-sfence-mfence
  //__sync_synchronize();
  // TODO: we will write atomic, so mfence / __sync_synchronize is not required.
  // only compiler's fence.
  // __atomic_thread_fence ?
  // put_index is guaranteed read-only.
  // cflush ?

  // PUT index can be changed only by putter.
  // get index can be changed in parallel thread
  atomic_store (&r->put_index, myinc(r, r->put_index));

   if (myinc(r, atomic_load (&r->get_index)) != r->put_index)
    return 0;

  long ret = syscall (SYS_futex, &r->put_index, FUTEX_WAKE, 1, NULL, 0);
  if (ret == -1)
    err (EXIT_FAILURE, "FUTEX_WAKE");
//  if (ret == 0)
//    printf ("no one is awoken?!\n");
  return 1;
}

XXX void ring_get_prepare (ring * restrict r, const void **restrict buf)
{
//  const struct timespec ts = { 2, 0 };

  while (atomic_load (&r->put_index) == r->get_index)
  {
    if (syscall (SYS_futex, &r->put_index, FUTEX_WAIT, r->get_index, /*&ts */ NULL, NULL, 0) == 0)
      continue;
    switch (errno)
    {
    case EAGAIN:
      break;
//    case ETIMEDOUT:
//      fprintf (stderr, "futex timeout\n. get_index=%" PRIu32 ", put_index=%" PRIu32 "\n", r->get_index, atomic_load (&r->put_index));
//      break;
    default:
      err (EXIT_FAILURE, "FUTEX_WAIT");
    }
  }

  *buf = (uint8_t *) r + 4096 + (r->get_index << r->cell_bits);
}

XXX void ring_get_commit (ring * restrict r)
{
  atomic_store (&r->get_index, myinc(r, r->get_index));
}

void ring_show (const ring * r)
{
  printf ("RING(put_index=%" PRIu32 " get_index=%" PRIu32 ")\n", r->put_index, r->get_index);
}

void *sender_fun (void *arg)
{
  ring *r = arg;
  size_t drops = 0;
  size_t success = 1;
  size_t futex_wakes = 0;
  struct timespec start, end;
  if (clock_gettime (CLOCK_MONOTONIC, &start))
    err (EXIT_FAILURE, "clock_gettime");

  for (;;)
  {
    if ((success + drops) % 100000000 == 0)
    {
      if (clock_gettime (CLOCK_MONOTONIC, &end))
	err (EXIT_FAILURE, "clock_gettime");
      uint64_t diff = ((uint64_t) (end.tv_sec) * 1000000000ull + end.tv_nsec) - ((uint64_t) (start.tv_sec) * 1000000000ull + start.tv_nsec);
      start = end;

      printf ("Sent %" PRIu64 " msgs, "	//
	      "Dropped %" PRIu64 " msgs (%" PRIu64 "%%). "	//
	      "%" PRIu64 " Mmsgs/sec total, "	//
	      "%" PRIu64 " success msgs/sec, "	//
	      "%" PRIu64 " wakes."	//
	      "\n",		//
	      success,		//
	      drops,		//
	      (uint64_t) (drops * 100 / (drops + success)),	//
	      (success + drops) * 1000 / diff,	//
	      success*1000000000ul / diff,	//
	      futex_wakes);
      success = 1;
      drops = 0;
      futex_wakes = 0;
    }

    uint8_t *x;
    if (!ring_put_prepare (r, (void **) &x))
    {
      drops++;
      continue;
    }
    success++;

#ifdef CRC
    memset (x + 4, rand (), (1 << (r->cell_bits)) - 4);
    *(uint32_t *) x = crc32_z ( /*crc32_z (0, Z_NULL, 0) */ 0, x + 4,
			       (1 << (r->cell_bits)) - 4);
#endif
    if (ring_put_commit (r))
      futex_wakes++;
  }

  return NULL;
}

void *getter_fun (void *arg)
{
  ring *r = arg;
  size_t messages = 0;
  srand (time (NULL));
  for (;;)
  {
    const uint8_t *x;
    if (messages % 1000000 == 0)
      printf ("Received %" PRIu64 " Mmsgs\n", messages / 1000000);

    ring_get_prepare (r, (const void **) &x);
    messages++;
#ifdef CRC
    if (*(const uint32_t *) x != crc32_z ( /*crc32_z (0, Z_NULL, 0) */ 0, x + 4,
					  (1 << (r->cell_bits)) - 4))
      errx (EXIT_FAILURE, "CRC32 error");
#endif
    ring_get_commit (r);
  }

  return NULL;
}


int main (int argc, char **argv)
{
  ring *r;

  if (argc < 2)
    errx (EXIT_FAILURE, "please provide mode");

  if (!strcmp (argv[1], "alloc"))
  {
    if (!ring_alloc ("qwe", 10, 9))
      errx (EXIT_FAILURE, "ring alloc failure");
  }
  else if (!strcmp (argv[1], "send"))
  {
    if (!(r = ring_open ("qwe")))
      errx (EXIT_FAILURE, "ring_open");
    sender_fun (r);
    ring_close (r);

  }
  else if (!strcmp (argv[1], "recv"))
  {
    if (!(r = ring_open ("qwe")))
      errx (EXIT_FAILURE, "ring_open");
    getter_fun (r);
    ring_close (r);
  }
  else
    errx (EXIT_FAILURE, "wrong mode");

  return 0;
}
