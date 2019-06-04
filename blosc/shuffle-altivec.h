/*********************************************************************
  Blosc - Blocked Shuffling and Compression Library

  Author: Francesc Alted <francesc@blosc.org>

  See LICENSE.txt for details about copyright and rights to use.
**********************************************************************/

/* ALTIVEC-accelerated shuffle/unshuffle routines. */

#ifndef SHUFFLE_ALTIVEC_H
#define SHUFFLE_ALTIVEC_H

#include "blosc-common.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
  ALTIVEC-accelerated shuffle routine.
*/
BLOSC_NO_EXPORT void shuffle_altivec(const int32_t bytesoftype, const int32_t blocksize,
                                     const uint8_t *_src, uint8_t *_dest);

/**
  ALTIVEC-accelerated unshuffle routine.
*/
BLOSC_NO_EXPORT void unshuffle_altivec(const int32_t bytesoftype, const int32_t blocksize,
                                       const uint8_t *_src, uint8_t *_dest);

#ifdef __cplusplus
}
#endif

#endif /* SHUFFLE_ALTIVEC_H */
