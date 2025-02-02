/*********************************************************************
  Blosc - Blocked Shuffling and Compression Library

  Author: Francesc Alted <francesc@blosc.org>
  Creation date: 2017-08-29

  See LICENSE.txt for details about copyright and rights to use.
**********************************************************************/

#ifndef CONTEXT_H
#define CONTEXT_H

#if defined(_WIN32) && !defined(__GNUC__)
  #include "win32/pthread.h"
#else
  #include <pthread.h>
#endif

/* Have problems using posix barriers when symbol value is 200112L */
/* This requires more investigation, but will work for the moment */
#if defined(_POSIX_BARRIERS) && (_POSIX_BARRIERS >= 200112L)
#define BLOSC_POSIX_BARRIERS
#endif

#include "blosc2.h"

#if defined(HAVE_ZSTD)
  #include "zstd.h"
#endif /*  HAVE_ZSTD */

#ifdef HAVE_IPP
  #include <ipps.h>
#endif /* HAVE_IPP */

struct blosc2_context_s {
  const uint8_t* src;
  /* The source buffer */
  uint8_t* dest;
  /* The destination buffer */
  uint8_t* header_flags;
  /* Flags for header */
  int32_t sourcesize;
  /* Number of bytes in source buffer */
  int32_t nblocks;
  /* Number of total blocks in buffer */
  int32_t leftover;
  /* Extra bytes at end of buffer */
  int32_t blocksize;
  /* Length of the block in bytes */
  int32_t output_bytes;
  /* Counter for the number of output bytes */
  int32_t destsize;
  /* Maximum size for destination buffer */
  int32_t typesize;
  /* Type size */
  int32_t* bstarts;
  /* Starts for every block inside the compressed buffer */
  int compcode;
  /* Compressor code to use */
  int clevel;
  /* Compression level (1-9) */
  int use_dict;
  /* Whether to use dicts or not */
  void* dict_buffer;
  /* The buffer to keep the trained dictionary */
  size_t dict_size;
  /* The size of the trained dictionary */
  void* dict_cdict;
  /* The dictionary in digested form for compression */
  void* dict_ddict;
  /* The dictionary in digested form for decompression */
  uint8_t filter_flags;
  /* The filter flags in the filter pipeline */
  uint8_t filters[BLOSC2_MAX_FILTERS];
  /* the (sequence of) filters */
  uint8_t filters_meta[BLOSC2_MAX_FILTERS];
  /* the metainfo for filters */
  blosc2_prefilter_fn prefilter;
  /* prefilter function */
  blosc2_prefilter_params *pparams;
  /* prefilter params */

  /* metadata for filters */
  blosc2_schunk* schunk;
  /* Associated super-chunk (if available) */
  struct thread_context* serial_context;
  /* Cache for temporaries for serial operation */
  int do_compress;
  /* 1 if we are compressing, 0 if decompressing */
  void *btune;
  /* Entry point for BTune persistence between runs */

  /* Threading */
  int nthreads;
  int new_nthreads;
  int threads_started;
  int end_threads;
  pthread_t *threads;
  pthread_mutex_t count_mutex;
#ifdef BLOSC_POSIX_BARRIERS
  pthread_barrier_t barr_init;
  pthread_barrier_t barr_finish;
#else
  int count_threads;
  pthread_mutex_t count_threads_mutex;
  pthread_cond_t count_threads_cv;
#endif
#if !defined(_WIN32)
  pthread_attr_t ct_attr;      /* creation time attrs for threads */
#endif
  int thread_giveup_code;
  /* error code when give up */
  int thread_nblock;       /* block counter */
  int dref_not_init;       /* data ref in delta not initialized */
  pthread_mutex_t delta_mutex;
  pthread_cond_t delta_cv;
};

struct thread_context {
  blosc2_context* parent_context;
  int tid;
  uint8_t* tmp;
  uint8_t* tmp2;
  uint8_t* tmp3;
  uint8_t* tmp4;
  int32_t tmpblocksize; /* keep track of how big the temporary buffers are */
#if defined(HAVE_ZSTD)
  /* The contexts for ZSTD */
  ZSTD_CCtx* zstd_cctx;
  ZSTD_DCtx* zstd_dctx;
#endif /* HAVE_ZSTD */
#ifdef HAVE_IPP
  Ipp8u* lz4_hash_table;
#endif
};


#endif  /* CONTEXT_H */
