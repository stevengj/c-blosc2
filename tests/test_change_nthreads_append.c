/* Copyright (C) 2017 Francesc Alted
 * http://blosc.org
 * License: BSD (see LICENSE.txt)
 *
*/

#include <stdio.h>
#include "test_common.h"
#include "../blosc/context.h"


#define CHUNKSIZE (200 * 1000)
#define NCHUNKS 500
#define NTHREADS 4


/* Global vars */
int tests_run = 0;

static char* all_tests() {
  static int64_t data[CHUNKSIZE];
  static int64_t data_dest[CHUNKSIZE];
  const size_t isize = CHUNKSIZE * sizeof(int64_t);
  int dsize = 0;
  size_t nbytes, cbytes;
  blosc2_cparams cparams = BLOSC2_CPARAMS_DEFAULTS;
  blosc2_dparams dparams = BLOSC2_DPARAMS_DEFAULTS;
  blosc2_schunk* schunk;
  int32_t i;
  int32_t nchunk;
  int32_t nchunks;
  blosc_timestamp_t last, current;
  double ttotal;

  printf("Blosc version info: %s (%s)\n",
         BLOSC_VERSION_STRING, BLOSC_VERSION_DATE);

/* Initialize the Blosc compressor */
  blosc_init();

/* Create a super-chunk container */
  cparams.typesize = 8;
  cparams.filters[0] = BLOSC_DELTA;
  cparams.compcode = BLOSC_BLOSCLZ;
  cparams.clevel = 9;
  cparams.nthreads = NTHREADS;
  dparams.nthreads = NTHREADS;
  schunk = blosc2_new_schunk(cparams, dparams, NULL);

  struct blosc2_context_s * cctx = schunk->cctx;
  blosc_set_timestamp(&last);
  for (nchunk = 1; nchunk <= NCHUNKS; nchunk++) {
    for (i = 0; i < CHUNKSIZE; i++) {
      data[i] = i * (int64_t)nchunk;
    }
    // Alternate between 1 and NTHREADS
    cctx->new_nthreads = nchunk % NTHREADS + 1;
    nchunks = blosc2_schunk_append_buffer(schunk, data, isize);
    mu_assert("ERROR: nchunk is not correct", nchunks == nchunk);
  }
  /* Gather some info */
  nbytes = (size_t)schunk->nbytes;
  cbytes = (size_t)schunk->cbytes;
  blosc_set_timestamp(&current);
  ttotal = blosc_elapsed_secs(last, current);
  printf("Compression ratio: %.1f MB -> %.1f MB (%.1fx)\n",
         (double)nbytes / MB, (double)cbytes / MB, (double)nbytes / cbytes);
  printf("Compression time: %.3g s, %.1f MB/s\n",
         ttotal, nbytes / (ttotal * MB));

  /* Retrieve and decompress the chunks (0-based count) */
  struct blosc2_context_s * dctx = schunk->dctx;
  blosc_set_timestamp(&last);
  for (nchunk = NCHUNKS-1; nchunk >= 0; nchunk--) {
    // Alternate between 1 and NTHREADS
    dctx->new_nthreads = nchunk % NTHREADS + 1;
    dsize = blosc2_schunk_decompress_chunk(schunk, nchunk, data_dest, isize);
  }
  mu_assert("ERROR: chunk decompression error", dsize > 0);
  blosc_set_timestamp(&current);
  ttotal = blosc_elapsed_secs(last, current);
  printf("Decompression time: %.3g s, %.1f MB/s\n",
         ttotal, nbytes / (ttotal * MB));

  /* Check integrity of the first chunk */
  for (i = 0; i < CHUNKSIZE; i++) {
    mu_assert("ERROR: decompressed data differs from original", data_dest[i] == (int64_t)i);
  }

  printf("Successful roundtrip!\n");

  /* Free resources */
  blosc2_free_schunk(schunk);

  return 0;
}


int main(int argc, char **argv) {
  char *result;

  if (argc > 0) {
    printf("STARTING TESTS for %s", argv[0]);
  }

  blosc_init();

  /* Run all the suite */
  result = all_tests();
  if (result != EXIT_SUCCESS) {
    printf(" (%s)\n", result);
  }
  else {
    printf(" ALL TESTS PASSED");
  }
  printf("\tTests run: %d\n", tests_run);

  blosc_destroy();

  return result != EXIT_SUCCESS;
}
