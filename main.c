#include "libq.h"

#include <locale.h>

static void sender_fun (ring * restrict r)
{
  size_t drops = 0;
  size_t success = 0;
  size_t futex_wakes = 0;
  struct timespec start;
  if (clock_gettime (CLOCK_MONOTONIC, &start))
    err (EXIT_FAILURE, "clock_gettime");

  for (size_t event_id = ((uint64_t) (start.tv_sec) * 1000000000ull + start.tv_nsec);; event_id++)
  {
    char *x;
    if (ring_put_prepare (r, (void **) &x))
    {
      success++;
      sprintf (x, "{\"event_id\": %zu}", event_id);	// TODO: cell maxlen = (((size_t)1) << r->cell_bits);
      if (ring_put_commit (r))
	futex_wakes++;
    }
    else
    {
      drops++;
    }

    if ((success + drops) % 100000000 == 0)
    {
      struct timespec end;
      if (clock_gettime (CLOCK_MONOTONIC, &end))
	err (EXIT_FAILURE, "clock_gettime");
      uint64_t diff = ((uint64_t) (end.tv_sec) * 1000000000ull + end.tv_nsec) - ((uint64_t) (start.tv_sec) * 1000000000ull + start.tv_nsec);
      start = end;

      printf ("Sent %'" PRIu64 " msgs/sec, "	//
	      "Dropped %'" PRIu64 " msgs/sec (%" PRIu64 "%%). "	//
	      "%'" PRIu64 " wakes/sec."	//
	      "\n",		//
	      success * 1000000000ul / diff,	//
	      drops * 1000000000ul / diff,	//
	      (uint64_t) (drops * 100 / (drops + success)),	//
	      futex_wakes * 1000000000ul / diff);
      success = 0;
      drops = 0;
      futex_wakes = 0;
    }
  }
}

static void getter_fun (ring * restrict r)
{
  size_t messages = 0;
  srand (time (NULL));
  for (;;)
  {
    const uint8_t *x;
    if (messages % 1000000 == 0)
      printf ("Received %" PRIu64 " Mmsgs\n", messages / 1000000);

    ring_get_prepare (r, (const void **) &x);
    messages++;
    ring_get_commit (r);
  }
}


int main (int argc, char **argv)
{
  setlocale (LC_ALL, "");

  ring *r;

  if (argc < 2)
    errx (EXIT_FAILURE, "please provide mode");

  if (!strcmp (argv[1], "alloc"))
  {
    if (!ring_alloc ("qwe", 10 /* slots size bits === 1024 bytes */ , 9 /* cell bits = 512 slots */ ))
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
