#define XXX extern
#define WAITABLE 1


#ifdef WAITABLE
#define _GNU_SOURCE
#include <errno.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>           /* For mode constants */
#include <fcntl.h>              /* For O_* constants */
#include <sys/types.h>
#endif


#include <stdlib.h>
#include <stdio.h>
#include <string.h>
//#include <stdatomic.h>
#include <stdint.h>
#include <inttypes.h>
#include <sys/random.h>
#include <assert.h>
#include <err.h>
#include <time.h>
#include <pthread.h>

#ifdef CRC
#include <zlib.h>               //for crc32c only
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
  size_t put_index;             // index. where to put
  size_t get_index;             // index. where to read.
  size_t mask;
  uint8_t cell_bits;
#ifdef WAITABLE
  pthread_cond_t can_read;
  pthread_mutex_t can_read_lock;
#endif
} ring;


#ifdef WAITABLE
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
      abort ();
    return 0;
  }

  ring *r = mmap (NULL, 4096 + datasize, PROT_WRITE | PROT_READ, MAP_SHARED_VALIDATE | MAP_LOCKED | MAP_POPULATE, fd, 0);

  if (close (fd))
    abort ();

  if (!r)
    return 0;                   // BUG uninitialized file (!)

  r->put_index = 0;
  r->get_index = 0;
  r->mask = (1 << slots_bits) - 1;
  r->cell_bits = cell_bits;

  pthread_mutexattr_t mattr;
  pthread_condattr_t cattr;

  if (pthread_mutexattr_init (&mattr))
    abort ();
  if (pthread_mutexattr_setpshared (&mattr, PTHREAD_PROCESS_SHARED))
    abort ();
  if (pthread_mutexattr_setrobust (&mattr, PTHREAD_MUTEX_ROBUST))
    abort ();
  if (pthread_mutex_init (&r->can_read_lock, &mattr))
    abort ();
  if (pthread_condattr_init (&cattr))
    abort ();
  if (pthread_condattr_setpshared (&cattr, PTHREAD_PROCESS_SHARED))
    abort ();
  if (pthread_cond_init (&r->can_read, &cattr))
    abort ();

  // TODO: link unnamed file
  if (munmap (r, 4096 + datasize))
    abort ();

  return 1;
}

XXX ring *ring_open (const char *name)
{
  int fd;
  if ((fd = shm_open (name, O_RDWR | O_CLOEXEC | O_NOCTTY, 0)) == -1)
    return NULL;

  struct stat st;

  if (fstat (fd, &st) == -1)
    abort ();

  ring *r = mmap (NULL, st.st_size, PROT_WRITE | PROT_READ, MAP_SHARED_VALIDATE | MAP_LOCKED | MAP_POPULATE, fd, 0);
  if (close (fd))
    abort ();
  if (!r)
    return NULL;

  if ((uint64_t) st.st_size != 4096 + (r->mask + 1) * (1ull << r->cell_bits))
  {
    if (munmap (r, st.st_size))
      abort ();
    return NULL;
  }

  fprintf(stderr, "Checking mutexes work\n");
  switch (pthread_mutex_lock (&r->can_read_lock))
  {
  case 0:
    break;
  case EOWNERDEAD:
    fprintf(stderr, "Restoring mutex\n");
    if (pthread_mutex_consistent (&r->can_read_lock))
      errx (EXIT_FAILURE, "pthread_mutex_consistent");
      fprintf(stderr, "Restoring mutex OK\n");
    break;
  default:
    abort ();
  }

  if (pthread_mutex_unlock (&r->can_read_lock))
    abort ();

  fprintf(stderr, "mutexes work OK!\n");

  return r;
}

XXX void ring_close (ring * r)
{
  //pthread_cond_destroy(&r->can_read); // what readers will do ?
  if (munmap (r, 4096 + (r->mask + 1) * (1ull << r->cell_bits)))
    abort ();
}

#else
XXX ring *ring_create (uint8_t slots_bits, size_t cell_bits)
{
  if (!cell_bits || cell_bits > 13 || !slots_bits || slots_bits > 16)
    return NULL;

  ring *r;

  size_t datasize = 1ul << (slots_bits + cell_bits);

  assert (sizeof (ring) <= 4096);

  // Should be sizeof(ring) + datasize, but alignment.
  if (!(r = malloc (4096 + datasize)))
    return r;

  r->put_index = 0;
  r->get_index = 0;
  r->mask = (1 << slots_bits) - 1;
  r->cell_bits = cell_bits;
  return r;
}

XXX void ring_close (ring * r)
{
  //pthread_cond_destroy(&r->can_read); // what readers will do ?
  free (r);
}

#endif



XXX int ring_put_prepare (ring * restrict r, void **restrict data)
{
  size_t get_index = __atomic_load_n (&r->get_index, __ATOMIC_ACQUIRE);
  size_t put_index = r->put_index;      // volatile put_index optimisation.
  if (get_index == ((put_index + 1) & r->mask)) // QUEUE is FULL
    return 0;
  *data = (uint8_t *) r + 4096 + (put_index << r->cell_bits);
  return 1;
}

static void ring_put_commit (ring * restrict r)
{
  // https://stackoverflow.com/questions/60403801/x86-lock-vs-sfence-mfence
  //__sync_synchronize();
  // TODO: we will write atomic, so mfence / __sync_synchronize is not required.
  // only compiler's fence.
  // __atomic_thread_fence ?
  // put_index is guaranteed read-only.
  //    r->put_index = (r->put_index + 1) % r->slots;
  // cflush ?

  __atomic_store_n (&r->put_index, (r->put_index + 1) & r->mask, __ATOMIC_RELEASE);

#ifdef WAITABLE
  if (pthread_mutex_lock (&r->can_read_lock))
    abort ();
  if (pthread_cond_signal (&r->can_read))
    abort ();
  if (pthread_mutex_unlock (&r->can_read_lock))
    abort ();
#endif
}

static int ring_get_prepare (ring * restrict r, const void **restrict buf)
{
#ifdef WAITABLE

  while (__atomic_load_n (&r->put_index, __ATOMIC_ACQUIRE) == r->get_index) {
   if (pthread_mutex_lock (&r->can_read_lock))
      abort ();
    pthread_cond_wait (&r->can_read, &r->can_read_lock);
  if (pthread_mutex_unlock (&r->can_read_lock))
    abort ();
  }

#else
  if (__atomic_load_n (&r->put_index, __ATOMIC_ACQUIRE) == r->get_index)
    return 0;
#endif
  *buf = (uint8_t *) r + 4096 + (r->get_index << r->cell_bits);
  return 1;
}

XXX void ring_get_commit (ring * restrict r)
{
  __atomic_store_n (&r->get_index, (r->get_index + 1) & r->mask, __ATOMIC_RELEASE);
}

void ring_show (const ring * r)
{
  printf ("RING(put_index=%zu get_index=%zu)\n", r->put_index, r->get_index);
}

static uint64_t ITERS;

void *sender_fun (void *arg)
{
  ring *r = arg;
  size_t drops = 0;
  size_t success = 0;
  struct timespec start, end;
  if (clock_gettime (CLOCK_MONOTONIC, &start))
    abort ();

  while (!ITERS || (success + drops) < ITERS)
  {
    if (!ITERS && (success + drops) && (success + drops) % 100000000 == 0)
      printf ("Sent %" PRIu64 " Msgs, Dropped %" PRIu64 " Mmsgs (%" PRIu64 "%%)\n", success / 1000000, drops / 1000000,
              (uint64_t) (drops * 100 / (drops + success)));

    uint8_t *x;
    if (!ring_put_prepare (r, (void **) &x))
    {
      drops++;
      continue;
    }
    success++;
    *(uint64_t *) x = 0;        // erase possible exit marker.
#ifdef CRC
    memset (x + 4, rand (), (1 << (r->cell_bits)) - 4);
    *(uint32_t *) x = crc32_z ( /*crc32_z (0, Z_NULL, 0) */ 0, x + 4, (1 << (r->cell_bits)) - 4);
#endif
    ring_put_commit (r);
  }

  if (clock_gettime (CLOCK_MONOTONIC, &end))
    abort ();
  printf ("sender drops: %zu (%" PRIu64 "%%)\n", drops, (uint64_t) (drops * 100 / (drops + success)));
  uint64_t *x;

  // wait for ready ring. (spinlock)
  while (!ring_put_prepare (r, (void **) &x));
  *x = 0x12345678abcdef01;
  ring_put_commit (r);
  uint64_t diff = ((uint64_t) (end.tv_sec) * 1000000000ull + end.tv_nsec) - ((uint64_t) (start.tv_sec) * 1000000000ull + start.tv_nsec);
  printf ("sender complete. %" PRIu64 " success Mmsgs/sec\n", success * 1000 / diff);
  printf ("sender complete. %" PRIu64 " total Mmsgs/sec\n", (success + drops) * 1000 / diff);

  return NULL;
}

void *getter_fun (void *arg)
{
  ring *r = arg;
  size_t spins = 0;
  size_t messages = 0;
  srand (time (NULL));
  for (;;)
  {
    const uint8_t *x;
    if (!ITERS && (messages + spins) && (spins + messages) % 1000000 == 0)
      printf ("Received %" PRIu64 " Msgs\n", messages / 1000000);

    if (!ring_get_prepare (r, (const void **) &x))
    {
      spins++;
      continue;
    }

    if (*(const uint64_t *) x == 0x12345678abcdef01)
    {
      ring_get_commit (r);
      break;
    }

    messages++;
#ifdef CRC
    if (*(const uint32_t *) x != crc32_z ( /*crc32_z (0, Z_NULL, 0) */ 0, x + 4,
                                          (1 << (r->cell_bits)) - 4))
    {
      fprintf (stderr, "CRC32 error\n");
      abort ();
    }
#endif

    for (int i = 0; i < 10; i++)
      asm volatile ("");


    ring_get_commit (r);
  }

  printf ("Read messages: %zu\n", messages);
  printf ("Read spins: %zu\n", spins);
  return NULL;
}


int main (int argc, char **argv)
{
  ring *r;

#if WAITABLE
  ITERS = 0;
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
      abort ();
    sender_fun (r);
    ring_close (r);

  }
  else if (!strcmp (argv[1], "recv"))
  {
    if (!(r = ring_open ("qwe")))
      abort ();
    getter_fun (r);
    ring_close (r);

  }
  else
    errx (EXIT_FAILURE, "wrong mode");
#else
  ITERS = 30000000;

  if (!(r = ring_create (10, 9)))
    abort ();

  pthread_t sender;
  pthread_t getter;

  if (pthread_create (&getter, NULL, getter_fun, r))
    abort ();
  if (pthread_create (&sender, NULL, sender_fun, r))
    abort ();
  pthread_join (sender, NULL);
  pthread_join (getter, NULL);
  ring_close (r);
#endif
  return 0;
}
