/*********************************************************************
  Blosc - Blocked Shuffling and Compression Library

  Author: Francesc Alted <francesc@blosc.org>
  Creation date: 2018-07-04

  See LICENSE.txt for details about copyright and rights to use.
**********************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>
#include "blosc2.h"
#include "blosc-private.h"
#include "context.h"
#include "frame.h"

#if defined(_WIN32) && !defined(__MINGW32__)
#include <windows.h>
  #include <malloc.h>

/* stdint.h only available in VS2010 (VC++ 16.0) and newer */
  #if defined(_MSC_VER) && _MSC_VER < 1600
    #include "win32/stdint-windows.h"
  #else
    #include <stdint.h>
  #endif

  #define fseek _fseeki64

#endif  /* _WIN32 */

/* If C11 is supported, use it's built-in aligned allocation. */
#if __STDC_VERSION__ >= 201112L
#include <stdalign.h>
#endif


// big <-> little-endian and store it in a memory position.  Sizes supported: 1, 2, 4, 8 bytes.
void swap_store(void *dest, const void *pa, int size) {
    uint8_t* pa_ = (uint8_t*)pa;
    uint8_t* pa2_ = malloc((size_t)size);
    int i = 1;                    /* for big/little endian detection */
    char* p = (char*)&i;

    if (p[0] == 1) {
        /* little endian */
        switch (size) {
            case 8:
                pa2_[0] = pa_[7];
                pa2_[1] = pa_[6];
                pa2_[2] = pa_[5];
                pa2_[3] = pa_[4];
                pa2_[4] = pa_[3];
                pa2_[5] = pa_[2];
                pa2_[6] = pa_[1];
                pa2_[7] = pa_[0];
                break;
            case 4:
                pa2_[0] = pa_[3];
                pa2_[1] = pa_[2];
                pa2_[2] = pa_[1];
                pa2_[3] = pa_[0];
                break;
            case 2:
                pa2_[0] = pa_[1];
                pa2_[1] = pa_[0];
                break;
            case 1:
                pa2_[0] = pa_[1];
                break;
            default:
                fprintf(stderr, "Unhandled size: %d\n", size);
        }
    }
    memcpy(dest, pa2_, size);
    free(pa2_);
}


/* Create a new (empty) frame */
blosc2_frame* blosc2_new_frame(char* fname) {
  blosc2_frame* new_frame = malloc(sizeof(blosc2_frame));
  memset(new_frame, 0, sizeof(blosc2_frame));
  if (fname != NULL) {
    char* new_fname = malloc(strlen(fname) + 1);  // + 1 for the trailing NULL
    new_frame->fname = strcpy(new_fname, fname);
  }
  return new_frame;
}


/* Free all memory from a frame. */
int blosc2_free_frame(blosc2_frame *frame) {

  if (frame->sdata != NULL) {
    free(frame->sdata);
  }

  if (frame->fname != NULL) {
    free(frame->fname);
  }

  free(frame);

  return 0;
}


void *new_header_frame(blosc2_schunk *schunk, blosc2_frame *frame) {
  assert(frame != NULL);
  uint8_t* h2 = calloc(FRAME_HEADER_MINLEN, 1);
  uint8_t* h2p = h2;

  // The msgpack header starts here
  *h2p = 0x90;  // fixarray...
  *h2p += 13;   // ...with 13 elements
  h2p += 1;

  // Magic number
  *h2p = 0xa0 + 8;  // str with 8 elements
  h2p += 1;
  assert(h2p - h2 < FRAME_HEADER_MINLEN);
  strcpy((char*)h2p, "b2frame");
  h2p += 8;

  // Header size
  *h2p = 0xd2;  // int32
  h2p += 1 + 4;
  assert(h2p - h2 < FRAME_HEADER_MINLEN);

  // Total frame size
  *h2p = 0xcf;  // uint64
  // Fill it with frame->len which is known *after* the creation of the frame (e.g. when updating the header)
  int64_t flen = frame->len;
  swap_store(h2 + FRAME_LEN, &flen, sizeof(flen));
  h2p += 1 + 8;
  assert(h2p - h2 < FRAME_HEADER_MINLEN);

  // Flags
  *h2p = 0xa0 + 4;  // str with 4 elements
  h2p += 1;
  assert(h2p - h2 < FRAME_HEADER_MINLEN);

  // General flags
  *h2p = BLOSC2_VERSION_FRAME_FORMAT;  // version
  *h2p += 0x20;  // 64-bit offsets
  h2p += 1;
  assert(h2p - h2 < FRAME_HEADER_MINLEN);

  // Reserved flags
  h2p += 1;
  assert(h2p - h2 < FRAME_HEADER_MINLEN);

  // Codec flags
  *h2p = schunk->compcode;
  *h2p += (schunk->clevel) << 4u;  // clevel
  h2p += 1;
  assert(h2p - h2 < FRAME_HEADER_MINLEN);

  // Reserved flags
  *h2p = 0;
  h2p += 1;
  assert(h2p - h2 < FRAME_HEADER_MINLEN);

  // Uncompressed size
  *h2p = 0xd3;  // int64
  h2p += 1;
  int64_t nbytes = schunk->nbytes;
  swap_store(h2p, &nbytes, sizeof(nbytes));
  h2p += 8;
  assert(h2p - h2 < FRAME_HEADER_MINLEN);

  // Compressed size
  *h2p = 0xd3;  // int64
  h2p += 1;
  int64_t cbytes = schunk->cbytes;
  swap_store(h2p, &cbytes, sizeof(cbytes));
  h2p += 8;
  assert(h2p - h2 < FRAME_HEADER_MINLEN);

  // Type size
  *h2p = 0xd2;  // int32
  h2p += 1;
  int32_t typesize = schunk->typesize;
  swap_store(h2p, &typesize, sizeof(typesize));
  h2p += 4;
  assert(h2p - h2 < FRAME_HEADER_MINLEN);

  // Chunk size
  *h2p = 0xd2;  // int32
  h2p += 1;
  int32_t chunksize = schunk->chunksize;
  swap_store(h2p, &chunksize, sizeof(chunksize));
  h2p += 4;
  assert(h2p - h2 < FRAME_HEADER_MINLEN);

  // Number of threads for compression
  *h2p = 0xd1;  // int16
  h2p += 1;
  int16_t nthreads = (int16_t)schunk->cctx->nthreads;
  swap_store(h2p, &nthreads, sizeof(nthreads));
  h2p += 2;
  assert(h2p - h2 < FRAME_HEADER_MINLEN);

  // Number of threads for decompression
  *h2p = 0xd1;  // int16
  h2p += 1;
  nthreads = (int16_t)schunk->dctx->nthreads;
  swap_store(h2p, &nthreads, sizeof(nthreads));
  h2p += 2;
  assert(h2p - h2 < FRAME_HEADER_MINLEN);

  // The boolean for FRAME_HAS_USERMETA
  *h2p = (schunk->usermeta_len > 0) ? (uint8_t)0xc3 : (uint8_t)0xc2;
  h2p += 1;
  assert(h2p - h2 < FRAME_HEADER_MINLEN);

  // The space for FRAME_FILTER_PIPELINE
  *h2p = 0xd8;  //  fixext 16
  h2p += 1;
  assert(BLOSC2_MAX_FILTERS <= FRAME_FILTER_PIPELINE_MAX);
  // Store the filter pipeline in header
  uint8_t* mp_filters = h2 + FRAME_FILTER_PIPELINE + 1;
  uint8_t* mp_meta = h2 + FRAME_FILTER_PIPELINE + 1 + FRAME_FILTER_PIPELINE_MAX;
  int nfilters = 0;
  for (int i = 0; i < BLOSC2_MAX_FILTERS; i++) {
    if (schunk->filters[i] != BLOSC_NOFILTER) {
      mp_filters[nfilters] = schunk->filters[i];
      mp_meta[nfilters] = schunk->filters_meta[i];
      nfilters++;
    }
  }
  *h2p = (uint8_t)nfilters;
  h2p += 1;
  h2p += 16;
  assert(h2p - h2 == FRAME_HEADER_MINLEN);

  int32_t hsize = FRAME_HEADER_MINLEN;

  // Now, deal with metalayers
  int16_t nmetalayers = schunk->nmetalayers;

  // Make space for the header of metalayers (array marker, size, map of offsets)
  h2 = realloc(h2, (size_t)hsize + 1 + 1 + 2 + 1 + 2);
  h2p = h2 + hsize;

  // The msgpack header for the metalayers (array_marker, size, map of offsets, list of metalayers)
  *h2p = 0x90 + 3;  // array with 3 elements
  h2p += 1;

  // Size for the map (index) of offsets, including this uint16 size (to be filled out later on)
  *h2p = 0xcd;  // uint16
  h2p += 1 + 2;

  // Map (index) of offsets for optional metalayers
  *h2p = 0xde;  // map 16 with N keys
  h2p += 1;
  swap_store(h2p, &nmetalayers, sizeof(nmetalayers));
  h2p += sizeof(nmetalayers);
  int32_t current_header_len = (int32_t)(h2p - h2);
  int32_t *offtooff = malloc(nmetalayers * sizeof(int32_t));
  for (int nmetalayer = 0; nmetalayer < nmetalayers; nmetalayer++) {
    assert(frame != NULL);
    blosc2_metalayer *metalayer = schunk->metalayers[nmetalayer];
    uint8_t namelen = (uint8_t) strlen(metalayer->name);
    h2 = realloc(h2, (size_t)current_header_len + 1 + namelen + 1 + 4);
    h2p = h2 + current_header_len;
    // Store the metalayer
    assert(namelen < (1U << 5U));  // metalayer strings cannot be longer than 32 bytes
    *h2p = (uint8_t)0xa0 + namelen;  // str
    h2p += 1;
    memcpy(h2p, metalayer->name, namelen);
    h2p += namelen;
    // Space for storing the offset for the value of this metalayer
    *h2p = 0xd2;  // int32
    h2p += 1;
    offtooff[nmetalayer] = (int32_t)(h2p - h2);
    h2p += 4;
    current_header_len += 1 + namelen + 1 + 4;
  }
  int32_t hsize2 = (int32_t)(h2p - h2);
  assert(hsize2 == current_header_len);  // sanity check

  // Map size + int16 size
  assert((hsize2 - hsize) < (1U << 16U));
  uint16_t map_size = (uint16_t) (hsize2 - hsize);
  swap_store(h2 + FRAME_IDX_SIZE, &map_size, sizeof(map_size));

  // Make space for an (empty) array
  hsize = (int32_t)(h2p - h2);
  h2 = realloc(h2, (size_t)hsize + 2 + 1 + 2);
  h2p = h2 + hsize;

  // Now, store the values in an array
  *h2p = 0xdc;  // array 16 with N elements
  h2p += 1;
  swap_store(h2p, &nmetalayers, sizeof(nmetalayers));
  h2p += sizeof(nmetalayers);
  current_header_len = (int32_t)(h2p - h2);
  for (int nmetalayer = 0; nmetalayer < nmetalayers; nmetalayer++) {
    assert(frame != NULL);
    blosc2_metalayer *metalayer = schunk->metalayers[nmetalayer];
    h2 = realloc(h2, (size_t)current_header_len + 1 + 4 + metalayer->content_len);
    h2p = h2 + current_header_len;
    // Store the serialized contents for this metalayer
    *h2p = 0xc6;  // bin32
    h2p += 1;
    swap_store(h2p, &(metalayer->content_len), sizeof(metalayer->content_len));
    h2p += 4;
    memcpy(h2p, metalayer->content, metalayer->content_len);  // buffer, no need to swap
    h2p += metalayer->content_len;
    // Update the offset now that we know it
    swap_store(h2 + offtooff[nmetalayer], &current_header_len, sizeof(current_header_len));
    current_header_len += 1 + 4 + metalayer->content_len;
  }
  free(offtooff);
  hsize = (int32_t)(h2p - h2);
  assert(hsize == current_header_len);  // sanity check

  // Set the length of the whole header now that we know it
  swap_store(h2 + FRAME_HEADER_LEN, &hsize, sizeof(hsize));

  return h2;
}


int get_header_info(blosc2_frame *frame, int32_t *header_len, int64_t *frame_len, int64_t *nbytes,
                    int64_t *cbytes, int32_t *chunksize, int32_t *nchunks, int32_t *typesize,
                    uint8_t *compcode, uint8_t *clevel, uint8_t *filters, uint8_t *filters_meta) {
  uint8_t* framep = frame->sdata;
  uint8_t* header = NULL;

  assert(frame->len > 0);

  if (frame->sdata == NULL) {
    header = malloc(FRAME_HEADER_MINLEN);
    FILE* fp = fopen(frame->fname, "rb");
    size_t rbytes = fread(header, 1, FRAME_HEADER_MINLEN, fp);
    assert(rbytes == FRAME_HEADER_MINLEN);
    framep = header;
    fclose(fp);
  }

  // Fetch some internal lengths
  swap_store(header_len, framep + FRAME_HEADER_LEN, sizeof(*header_len));
  swap_store(frame_len, framep + FRAME_LEN, sizeof(*frame_len));
  swap_store(nbytes, framep + FRAME_NBYTES, sizeof(*nbytes));
  swap_store(cbytes, framep + FRAME_CBYTES, sizeof(*cbytes));
  swap_store(chunksize, framep + FRAME_CHUNKSIZE, sizeof(*chunksize));
  if (typesize != NULL) {
    swap_store(typesize, framep + FRAME_TYPESIZE, sizeof(*typesize));
  }

  // Codecs
  uint8_t frame_codecs = framep[FRAME_CODECS];
  if (clevel != NULL) {
    *clevel = frame_codecs >> 4u;
  }
  if (compcode != NULL) {
    *compcode = frame_codecs & 0xFu;
  }

  // Filters
  if (filters != NULL && filters_meta != NULL) {
    uint8_t nfilters = framep[FRAME_FILTER_PIPELINE];
    if (nfilters > BLOSC2_MAX_FILTERS) {
      fprintf(stderr, "Error: the number of filters in frame header are too large for Blosc2");
      return -1;
    }
    uint8_t *filters_ = framep + FRAME_FILTER_PIPELINE + 1;
    uint8_t *filters_meta_ = framep + FRAME_FILTER_PIPELINE + 1 + FRAME_FILTER_PIPELINE_MAX;
    for (int i = 0; i < nfilters; i++) {
      filters[i] = filters_[i];
      filters_meta[i] = filters_meta_[i];
    }
  }

  if (*nbytes > 0) {
    // We can compute the chunks only when the frame has actual data
    *nchunks = (int32_t) (*nbytes / *chunksize);
    if (*nchunks * *chunksize < *nbytes) {
      *nchunks += 1;
    }
  } else {
    *nchunks = 0;
  }

  if (frame->sdata == NULL) {
    free(header);
  }

  return 0;
}


int64_t get_trailer_offset(blosc2_frame *frame, int32_t header_len, int64_t cbytes) {
  if (cbytes == 0) {
    // No data chunks yet
    return header_len;
  }

  return frame->len - frame->trailer_len;
}


// Update the length in the header
int update_frame_len(blosc2_frame* frame, int64_t len) {
  int rc = 1;
  if (frame->sdata != NULL) {
    swap_store(frame->sdata + FRAME_LEN, &len, sizeof(int64_t));
  }
  else {
    FILE* fp = fopen(frame->fname, "rb+");
    fseek(fp, FRAME_LEN, SEEK_SET);
    int64_t swap_len;
    swap_store(&swap_len, &len, sizeof(int64_t));
    size_t wbytes = fwrite(&swap_len, 1, sizeof(int64_t), fp);
    if (wbytes != sizeof(int64_t)) {
      fprintf(stderr, "Error: cannot write the frame length in header");
      return -1;
    }
    fclose(fp);
  }
  return rc;
}


int frame_update_trailer(blosc2_frame* frame, blosc2_schunk* schunk) {
  if (frame->len == 0) {
    fprintf(stderr, "Error: the trailer cannot be updated on empty frames");
  }

  // Create the trailer in msgpack (see the frame format document)
  uint32_t trailer_len = FRAME_TRAILER_MINLEN + schunk->usermeta_len;
  uint8_t* trailer = calloc((size_t)trailer_len, 1);
  uint8_t* ptrailer = trailer;
  *ptrailer = 0x90 + 4;  // fixarray with 4 elements
  ptrailer += 1;
  // Trailer format version
  *ptrailer = FRAME_TRAILER_VERSION;
  ptrailer += 1;
  // usermeta
  *ptrailer = 0xc6;     // bin32
  ptrailer += 1;
  swap_store(ptrailer, &(schunk->usermeta_len), 4);
  ptrailer += 4;
  memcpy(ptrailer, schunk->usermeta, schunk->usermeta_len);
  ptrailer += schunk->usermeta_len;
  // Trailer length
  *ptrailer = 0xce;  // uint32
  ptrailer += 1;
  swap_store(ptrailer, &(trailer_len), sizeof(uint32_t));
  ptrailer += sizeof(uint32_t);
  // Up to 16 bytes for frame fingerprint (using XXH3 included in https://github.com/Cyan4973/xxHash)
  // Maybe someone would need 256-bit in the future, but for the time being 128-bit seems like a good tradeoff
  *ptrailer = 0xd8;  // fixext 16
  ptrailer += 1;
  *ptrailer = 0;  // fingerprint type: 0 -> no fp; 1 -> 32-bit; 2 -> 64-bit; 3 -> 128-bit
  ptrailer += 1;
  // Uncomment this when we compute an actual fingerprint
  // memcpy(ptrailer, xxh3_fingerprint, sizeof(xxh3_fingerprint));
  ptrailer += 16;
  // Sanity check
  assert(ptrailer - trailer == trailer_len);

  int32_t header_len;
  int64_t frame_len;
  int64_t nbytes;
  int64_t cbytes;
  int32_t chunksize;
  int32_t nchunks;
  int ret = get_header_info(frame, &header_len, &frame_len, &nbytes, &cbytes, &chunksize, &nchunks,
                            NULL, NULL, NULL, NULL, NULL);
  if (ret < 0) {
    fprintf(stderr, "unable to get meta info from frame");
    return -1;
  }

  int64_t trailer_offset = get_trailer_offset(frame, header_len, cbytes);

  // Update the trailer.  As there are no internal offsets to the trailer section,
  // and it is always at the end of the frame, we can just write (or overwrite) it
  // at the end of the frame.
  if (frame->sdata != NULL) {
    frame->sdata = realloc(frame->sdata, (size_t)(trailer_offset + trailer_len));
    if (frame->sdata == NULL) {
      fprintf(stderr, "Error: cannot realloc space for the frame.");
      return -1;
    }
    memcpy(frame->sdata + trailer_offset, trailer, trailer_len);
  }
  else {
    FILE* fp = fopen(frame->fname, "rb+");
    fseek(fp, trailer_offset, SEEK_SET);
    size_t wbytes = fwrite(trailer, 1, trailer_len, fp);
    if (wbytes != (size_t)trailer_len) {
      fprintf(stderr, "Error: cannot write the trailer length in trailer");
      return -2;
    }
    fclose(fp);
  }
  free(trailer);

  int rc = update_frame_len(frame, trailer_offset + trailer_len);
  if (rc < 0) {
    return rc;
  }
  frame->len = trailer_offset + trailer_len;
  frame->trailer_len = trailer_len;

  return 1;
}


/* Create a frame out of a super-chunk. */
int64_t blosc2_schunk_to_frame(blosc2_schunk *schunk, blosc2_frame *frame) {
  int32_t nchunks = schunk->nchunks;
  int64_t cbytes = schunk->cbytes;
  FILE* fp = NULL;

  uint8_t* h2 = new_header_frame(schunk, frame);
  uint32_t h2len;
  swap_store(&h2len, h2 + FRAME_HEADER_LEN, sizeof(h2len));

  // Build the offsets chunk
  int32_t chunksize = 0;
  int32_t off_cbytes = 0;
  uint64_t coffset = 0;
  size_t off_nbytes = (size_t)nchunks * 8;
  uint64_t* data_tmp = malloc(off_nbytes);
  for (int i = 0; i < nchunks; i++) {
    uint8_t* data_chunk = schunk->data[i];
    int32_t chunk_cbytes = sw32_(data_chunk + 12);
    data_tmp[i] = coffset;
    coffset += chunk_cbytes;
    int32_t chunksize_ = sw32_(data_chunk + 4);
    if (i == 0) {
      chunksize = chunksize_;
    }
    else if (chunksize != chunksize_) {
      // Variable size  // TODO: update flags for this (or do not use them at all)
      chunksize = 0;
    }
  }
  assert ((int64_t)coffset == cbytes);

  uint8_t *off_chunk = NULL;
  if (nchunks > 0) {
    // Compress the chunk of offsets
    off_chunk = malloc(off_nbytes + BLOSC_MAX_OVERHEAD);
    blosc2_context *cctx = blosc2_create_cctx(BLOSC2_CPARAMS_DEFAULTS);
    cctx->typesize = 8;
    off_cbytes = blosc2_compress_ctx(cctx, off_nbytes, data_tmp, off_chunk,
                                     off_nbytes + BLOSC_MAX_OVERHEAD);
    blosc2_free_ctx(cctx);
    if (off_cbytes < 0) {
      free(off_chunk);
      free(h2);
      return -1;
    }
  }
  else {
    off_cbytes = 0;
  }
  free(data_tmp);

  // Now that we know them, fill the chunksize and frame length in header
  swap_store(h2 + FRAME_CHUNKSIZE, &chunksize, sizeof(chunksize));
  frame->len = h2len + cbytes + off_cbytes + FRAME_TRAILER_MINLEN + schunk->usermeta_len;
  int64_t tbytes = frame->len;
  swap_store(h2 + FRAME_LEN, &tbytes, sizeof(tbytes));

  // Create the frame and put the header at the beginning
  if (frame->fname == NULL) {
    frame->sdata = malloc((size_t)frame->len);
    memcpy(frame->sdata, h2, h2len);
  }
  else {
    fp = fopen(frame->fname, "wb");
    fwrite(h2, h2len, 1, fp);
  }
  free(h2);

  // Fill the frame with the actual data chunks
  coffset = 0;
  for (int i = 0; i < nchunks; i++) {
    uint8_t* data_chunk = schunk->data[i];
    int32_t chunk_cbytes = sw32_(data_chunk + 12);
    if (frame->fname == NULL) {
      memcpy(frame->sdata + h2len + coffset, data_chunk, (size_t)chunk_cbytes);
    } else {
      fwrite(data_chunk, (size_t)chunk_cbytes, 1, fp);
    }
    coffset += chunk_cbytes;
  }
  assert ((int64_t)coffset == cbytes);

  // Copy the offsets chunk at the end of the frame
  if (frame->fname == NULL) {
    memcpy(frame->sdata + h2len + cbytes, off_chunk, off_cbytes);
  }
  else {
    fwrite(off_chunk, (size_t)off_cbytes, 1, fp);
    fclose(fp);
  }
  free(off_chunk);

  int rc = frame_update_trailer(frame, schunk);
  if (rc < 0) {
    return rc;
  }

  return frame->len;
}


/* Write an in-memory frame out to a file. */
int64_t blosc2_frame_to_file(blosc2_frame *frame, char *fname) {
    // make sure that we are using an in-memory frame
    if (frame->fname != NULL) {
      fprintf(stderr, "Error: the original frame must be in-memory");
      return -1;
    }
    FILE* fp = fopen(fname, "wb");
    fwrite(frame->sdata, (size_t)frame->len, 1, fp);
    fclose(fp);
    return frame->len;
}


/* Initialize a frame out of a file */
blosc2_frame* blosc2_frame_from_file(const char *fname) {
  blosc2_frame* frame = calloc(1, sizeof(blosc2_frame));
  char* fname_cpy = malloc(strlen(fname) + 1);
  frame->fname = strcpy(fname_cpy, fname);

  // Get the length of the frame
  uint8_t* header = malloc(FRAME_HEADER_MINLEN);
  FILE* fp = fopen(fname, "rb");
  size_t rbytes = fread(header, 1, FRAME_HEADER_MINLEN, fp);
  if (rbytes != FRAME_HEADER_MINLEN) {
    fprintf(stderr, "Error: cannot read from file '%s'\n", fname);
    return NULL;
  }
  int64_t frame_len;
  swap_store(&frame_len, header + FRAME_LEN, sizeof(frame_len));
  frame->len = frame_len;
  free(header);

  // Now, the trailer length
  uint8_t* trailer = malloc(FRAME_TRAILER_MINLEN);
  fseek(fp, frame_len - FRAME_TRAILER_MINLEN, SEEK_SET);
  rbytes = fread(trailer, 1, FRAME_TRAILER_MINLEN, fp);
  if (rbytes != FRAME_TRAILER_MINLEN) {
    fprintf(stderr, "Error: cannot read from file '%s'\n", fname);
    return NULL;
  }
  int trailer_offset = FRAME_TRAILER_MINLEN - FRAME_TRAILER_LEN_OFFSET;
  assert(trailer[trailer_offset - 1] == 0xce);
  uint32_t trailer_len;
  swap_store(&trailer_len, trailer + trailer_offset, sizeof(trailer_len));
  frame->trailer_len = trailer_len;
  free(trailer);

  fclose(fp);

  return frame;
}


// Get the data pointers section
int get_offsets(blosc2_frame *frame, int32_t header_len, int64_t cbytes, int32_t nchunks, void *offsets) {
  uint8_t* framep = frame->sdata;
  uint8_t* coffsets;

  if (frame->sdata != NULL) {
    coffsets = framep + header_len + cbytes;
  } else {
    int64_t trailer_offset = get_trailer_offset(frame, header_len, cbytes);
    int64_t off_cbytes = trailer_offset - (header_len + cbytes);
    FILE* fp = fopen(frame->fname, "rb");
    coffsets = malloc((size_t)off_cbytes);
    fseek(fp, header_len + cbytes, SEEK_SET);
    size_t rbytes = fread(coffsets, 1, (size_t)off_cbytes, fp);
    if (rbytes != (size_t)off_cbytes) {
      fprintf(stderr, "Error: cannot read the offsets out of the fileframe.\n");
      fclose(fp);
      return -1;
    }
    fclose(fp);
  }

  blosc2_dparams off_dparams = BLOSC2_DPARAMS_DEFAULTS;
  blosc2_context *dctx = blosc2_create_dctx(off_dparams);
  int32_t off_nbytes = blosc2_decompress_ctx(dctx, coffsets, offsets, (size_t)nchunks * 8);
  blosc2_free_ctx(dctx);
  if (off_nbytes < 0) {
    free(offsets);
    return -1;
  }
  if (frame->sdata == NULL) {
    free(coffsets);
  }

  return 1;
}


int frame_update_header(blosc2_frame* frame, blosc2_schunk* schunk, bool new) {
  uint8_t* header = frame->sdata;

  assert(frame->len > 0);

  if (new && schunk->cbytes > 0) {
    fprintf(stderr, "Error: new metalayers cannot be added after actual data has been appended\n");
    return -1;
  }

  if (frame->sdata == NULL) {
    header = malloc(FRAME_HEADER_MINLEN);
    FILE* fp = fopen(frame->fname, "rb");
    size_t rbytes = fread(header, 1, FRAME_HEADER_MINLEN, fp);
    assert(rbytes == FRAME_HEADER_MINLEN);
    fclose(fp);
  }
  uint32_t prev_h2len;
  swap_store(&prev_h2len, header + FRAME_HEADER_LEN, sizeof(prev_h2len));

  // Build a new header
  uint8_t* h2 = new_header_frame(schunk, frame);
  uint32_t h2len;
  swap_store(&h2len, h2 + FRAME_HEADER_LEN, sizeof(h2len));

  // The frame length is outdated when adding a new metalayer, so update it
  if (new) {
    int64_t frame_len = h2len;  // at adding time, we only have to worry of the header for now
    swap_store(h2 + FRAME_LEN, &frame_len, sizeof(frame_len));
    frame->len = frame_len;
  }

  if (!new && prev_h2len != h2len) {
    fprintf(stderr, "Error: the new metalayer sizes should be equal the existing ones");
    return -2;
  }

  if (frame->sdata == NULL) {
    // Write updated header down to file
    FILE* fp = fopen(frame->fname, "rb+");
    fwrite(h2, h2len, 1, fp);
    fclose(fp);
    free(header);
  }
  else {
    if (new) {
      frame->sdata = realloc(frame->sdata, h2len);
    }
    memcpy(frame->sdata, h2, h2len);
  }
  free(h2);

  return 1;
}


/* Get the (compressed) usermeta chunk out of a frame */
int32_t frame_get_usermeta(blosc2_frame* frame, uint8_t** usermeta) {
  int32_t header_len;
  int64_t frame_len;
  int64_t nbytes;
  int64_t cbytes;
  int32_t chunksize;
  int32_t nchunks;
  int ret = get_header_info(frame, &header_len, &frame_len, &nbytes, &cbytes, &chunksize, &nchunks,
                            NULL, NULL, NULL, NULL, NULL);
  if (ret < 0) {
    fprintf(stderr, "Unable to get the header info from frame");
    return -1;
  }
  int64_t trailer_offset = get_trailer_offset(frame, header_len, cbytes);
  if (trailer_offset < 0) {
    fprintf(stderr, "Unable to get the trailer offset from frame");
    return -1;
  }

  // Get the size of usermeta (inside the trailer)
  int32_t usermeta_len_network;
  if (frame->sdata != NULL) {
    memcpy(&usermeta_len_network, frame->sdata + trailer_offset + FRAME_TRAILER_USERMETA_LEN_OFFSET, sizeof(int32_t));
  } else {
    FILE* fp = fopen(frame->fname, "rb");
    fseek(fp, trailer_offset + FRAME_TRAILER_USERMETA_LEN_OFFSET, SEEK_SET);
    size_t rbytes = fread(&usermeta_len_network, 1, sizeof(int32_t), fp);
    if (rbytes != sizeof(int32_t)) {
      fprintf(stderr, "Cannot access the usermeta_len out of the fileframe.\n");
      fclose(fp);
      return -1;
    }
    fclose(fp);
  }
  int32_t usermeta_len;
  swap_store(&usermeta_len, &usermeta_len_network, sizeof(int32_t));

  if (usermeta_len == 0) {
    *usermeta = NULL;
    return 0;
  }

  *usermeta = malloc(usermeta_len);
  if (frame->sdata != NULL) {
    memcpy(*usermeta, frame->sdata + trailer_offset + FRAME_TRAILER_USERMETA_OFFSET, usermeta_len);
  }
  else {
    FILE* fp = fopen(frame->fname, "rb+");
    fseek(fp, trailer_offset + FRAME_TRAILER_USERMETA_OFFSET, SEEK_SET);
    size_t rbytes = fread(*usermeta, 1, usermeta_len, fp);
    if (rbytes != (size_t)usermeta_len) {
      fprintf(stderr, "Error: cannot read the complete usermeta chunk in frame. %ld != %ld \n",
              (long)rbytes, (long)usermeta_len);
      return -1;
    }
    fclose(fp);
  }

  return usermeta_len;
}


int frame_get_metalayers(blosc2_frame* frame, blosc2_schunk* schunk) {
  int32_t header_len;
  int64_t frame_len;
  int64_t nbytes;
  int64_t cbytes;
  int32_t chunksize;
  int32_t nchunks;
  int ret = get_header_info(frame, &header_len, &frame_len, &nbytes, &cbytes, &chunksize, &nchunks,
                            NULL, NULL, NULL, NULL, NULL);
  if (ret < 0) {
    fprintf(stderr, "Unable to get the header info from frame");
    return -1;
  }

  // Get the header
  uint8_t* header = NULL;
  if (frame->sdata != NULL) {
    header = frame->sdata;
  } else {
    header = malloc(header_len);
    FILE* fp = fopen(frame->fname, "rb");
    size_t rbytes = fread(header, 1, header_len, fp);
    if (rbytes != header_len) {
      fprintf(stderr, "Cannot access the header out of the fileframe.\n");
      fclose(fp);
      return -2;
    }
    fclose(fp);
  }

  // Get the size for the index of metalayers
  uint16_t idx_size;
  swap_store(&idx_size, header + FRAME_IDX_SIZE, sizeof(idx_size));

  // Get the actual index of metalayers
  uint8_t* metalayers_idx = header + FRAME_IDX_SIZE + 2;
  assert(metalayers_idx[0] == 0xde);   // sanity check
  uint8_t* idxp = metalayers_idx + 1;
  uint16_t nmetalayers;
  swap_store(&nmetalayers, idxp, sizeof(uint16_t));
  idxp += 2;
  schunk->nmetalayers = nmetalayers;

  // Populate the metalayers and its serialized values
  for (int nmetalayer = 0; nmetalayer < nmetalayers; nmetalayer++) {
    assert((*idxp & 0xe0u) == 0xa0u);   // sanity check
    blosc2_metalayer* metalayer = calloc(sizeof(blosc2_metalayer), 1);
    schunk->metalayers[nmetalayer] = metalayer;

    // Populate the metalayer string
    int8_t nslen = *idxp & (uint8_t)0x1fu;
    idxp += 1;
    char* ns = malloc((size_t)nslen + 1);
    memcpy(ns, idxp, nslen);
    ns[nslen] = '\0';
    idxp += nslen;
    metalayer->name = ns;

    // Populate the serialized value for this metalayer
    // Get the offset
    assert((*idxp & 0xffu) == 0xd2u);   // sanity check
    idxp += 1;
    int32_t offset;
    swap_store(&offset, idxp, sizeof(offset));
    idxp += 4;

    // Go to offset and see if we have the correct marker
    uint8_t* content_marker = header + offset;
    assert(*content_marker == 0xc6);

    // Read the size of the content
    int32_t content_len;
    swap_store(&content_len, content_marker + 1, sizeof(content_len));
    metalayer->content_len = content_len;

    // Finally, read the content
    char* content = malloc((size_t)content_len);
    memcpy(content, content_marker + 1 + 4, (size_t)content_len);
    metalayer->content = (uint8_t*)content;
  }

  if (frame->sdata == NULL) {
    free(header);
  }
  return 1;
}


/* Get a super-chunk out of a frame */
blosc2_schunk* blosc2_schunk_from_frame(blosc2_frame* frame, bool copy) {
  int32_t header_len;
  int64_t frame_len;

  blosc2_schunk* schunk = calloc(1, sizeof(blosc2_schunk));
  schunk->frame = frame;
  int ret = get_header_info(frame, &header_len, &frame_len, &schunk->nbytes, &schunk->cbytes,
                            &schunk->chunksize, &schunk->nchunks, &schunk->typesize,
                            &schunk->compcode, &schunk->clevel, schunk->filters, schunk->filters_meta);
  if (ret < 0) {
    fprintf(stderr, "unable to get meta info from frame");
    return NULL;
  }
  int32_t nchunks = schunk->nchunks;
  int64_t nbytes = schunk->nbytes;
  int64_t cbytes = schunk->cbytes;

  // Compression and decompression contexts
  blosc2_cparams *cparams;
  blosc2_schunk_get_cparams(schunk, &cparams);
  schunk->cctx = blosc2_create_cctx(*cparams);
  free(cparams);
  blosc2_dparams *dparams;
  blosc2_schunk_get_dparams(schunk, &dparams);
  schunk->dctx = blosc2_create_dctx(*dparams);
  free(dparams);

  if (!copy || nchunks == 0) {
    goto out;
  }

  // We are not attached to a frame anymore
  schunk->frame = NULL;

  // Get the offsets
  int64_t *offsets = (int64_t *) calloc((size_t) nchunks * 8, 1);
  int rc = get_offsets(frame, header_len, cbytes, nchunks, offsets);
  if (rc < 0) {
    fprintf(stderr, "Error: cannot get the offsets for the frame\n");
    return NULL;
  }

  // We want the sparse schunk, so create the actual data chunks (and, while doing this,
  // get a guess at the blocksize used in this frame)
  schunk->data = malloc(nchunks * sizeof(void*));
  int64_t acc_nbytes = 0;
  int64_t acc_cbytes = 0;
  int32_t blocksize = 0;
  int32_t csize = 0;
  uint8_t* data_chunk = NULL;
  int32_t prev_alloc = BLOSC_MIN_HEADER_LENGTH;
  FILE* fp = NULL;
  if (frame->sdata == NULL) {
    data_chunk = malloc((size_t)prev_alloc);
    fp = fopen(frame->fname, "rb");
  }
  for (int i = 0; i < nchunks; i++) {
    if (frame->sdata != NULL) {
      data_chunk = frame->sdata + header_len + offsets[i];
      csize = sw32_(data_chunk + 12);
    }
    else {
      fseek(fp, header_len + offsets[i], SEEK_SET);
      size_t rbytes = fread(data_chunk, 1, BLOSC_MIN_HEADER_LENGTH, fp);
      assert(rbytes == BLOSC_MIN_HEADER_LENGTH);
      csize = sw32_(data_chunk + 12);
      if (csize > prev_alloc) {
        data_chunk = realloc(data_chunk, (size_t)csize);
        prev_alloc = csize;
      }
      fseek(fp, header_len + offsets[i], SEEK_SET);
      rbytes = fread(data_chunk, 1, (size_t)csize, fp);
      assert(rbytes == (size_t)csize);
    }
    uint8_t* new_chunk = malloc((size_t)csize);
    memcpy(new_chunk, data_chunk, (size_t)csize);
    schunk->data[i] = new_chunk;
    acc_nbytes += sw32_(data_chunk + 4);
    acc_cbytes += csize;
    int32_t blocksize_ = sw32_(data_chunk + 8);
    if (i == 0) {
      blocksize = blocksize_;
    }
    else if (blocksize != blocksize_) {
      // Blocksize varies
      blocksize = 0;
    }
  }
  schunk->blocksize = blocksize;

  free(offsets);
  if (frame->sdata == NULL) {
    free(data_chunk);
    fclose(fp);
  }
  assert(acc_nbytes == nbytes);
  assert(acc_cbytes == cbytes);

  uint8_t* usermeta;
  int32_t usermeta_len;

  out:
  rc = frame_get_metalayers(frame, schunk);
  if (rc < 0) {
    fprintf(stderr, "Error: cannot access the metalayers");
    return NULL;
  }

  usermeta_len = frame_get_usermeta(frame, &usermeta);
  if (usermeta_len < 0) {
    fprintf(stderr, "Error: cannot access the usermeta chunk");
    return NULL;
  }
  schunk->usermeta = usermeta;
  schunk->usermeta_len = usermeta_len;

  return schunk;
}


/* Return a compressed chunk that is part of a frame in the `chunk` parameter.
 * If the frame is disk-based, a buffer is allocated for the (compressed) chunk,
 * and hence a free is needed.  You can check if the chunk requires a free with the `needs_free`
 * parameter.
 * If the chunk does not need a free, it means that a pointer to the location in frame is returned
 * in the `chunk` parameter.
 *
 * The size of the (compressed) chunk is returned.  If some problem is detected, a negative code
 * is returned instead.
*/
int frame_get_chunk(blosc2_frame *frame, int nchunk, uint8_t **chunk, bool *needs_free) {
  int32_t header_len;
  int64_t frame_len;
  int64_t nbytes;
  int64_t cbytes;
  int32_t chunksize;
  int32_t nchunks;

  *chunk = NULL;
  *needs_free = false;
  int ret = get_header_info(frame, &header_len, &frame_len, &nbytes, &cbytes, &chunksize, &nchunks,
                            NULL, NULL, NULL, NULL, NULL);
  if (ret < 0) {
    fprintf(stderr, "unable to get meta info from frame");
    return -1;
  }

  if (nchunk >= nchunks) {
    fprintf(stderr, "nchunk ('%d') exceeds the number of chunks "
                    "('%d') in frame\n", nchunk, nchunks);
    return -2;
  }

  // Get the offset to the chunk
  int32_t off_nbytes = nchunks * 8;
  int64_t* offsets = malloc((size_t)off_nbytes);
  int rc = get_offsets(frame, header_len, cbytes, nchunks, offsets);
  if (rc < 0) {
    fprintf(stderr, "Error: cannot get the offset for chunk %d for the frame\n", nchunk);
    return -3;
  }
  int64_t offset = offsets[nchunk];
  free(offsets);

  int32_t chunk_cbytes;
  if (frame->sdata == NULL) {
    FILE* fp = fopen(frame->fname, "rb");
    fseek(fp, header_len + offset + 12, SEEK_SET);
    size_t rbytes = fread(&chunk_cbytes, 1, sizeof(chunk_cbytes), fp);
    if (rbytes != sizeof(chunk_cbytes)) {
      fprintf(stderr, "Cannot read the cbytes for chunk in the fileframe.\n");
      return -4;
    }
    chunk_cbytes = sw32_(&chunk_cbytes);
    *chunk = malloc((size_t)chunk_cbytes);
    fseek(fp, header_len + offset, SEEK_SET);
    rbytes = fread(*chunk, 1, (size_t)chunk_cbytes, fp);
    if (rbytes != (size_t)chunk_cbytes) {
      fprintf(stderr, "Cannot read the chunk out of the fileframe.\n");
      return -5;
    }
    fclose(fp);
    *needs_free = true;
  } else {
    *chunk = frame->sdata + header_len + offset;
    chunk_cbytes = sw32_(*chunk + 12);
  }

  return chunk_cbytes;
}


/* Append an existing chunk into a frame. */
void* frame_append_chunk(blosc2_frame* frame, void* chunk, blosc2_schunk* schunk) {
  int32_t header_len;
  int64_t frame_len;
  int64_t nbytes;
  int64_t cbytes;
  int32_t chunksize;
  int32_t nchunks;
  int rc = get_header_info(frame, &header_len, &frame_len, &nbytes, &cbytes, &chunksize, &nchunks,
                           NULL, NULL, NULL, NULL, NULL);
  if (rc < 0) {
    fprintf(stderr, "unable to get meta info from frame");
    return NULL;
  }

  int64_t trailer_offset = get_trailer_offset(frame, header_len, cbytes);
  int64_t trailer_len = frame->len - trailer_offset;

  /* The uncompressed and compressed sizes start at byte 4 and 12 */
  int32_t nbytes_chunk = sw32_((uint8_t*)chunk + 4);
  int32_t cbytes_chunk = sw32_((uint8_t*)chunk + 12);
  int64_t new_cbytes = cbytes + cbytes_chunk;

  if ((nchunks > 0) && (nbytes_chunk > chunksize)) {
    fprintf(stderr, "appending chunks with a larger chunksize than frame is not allowed yet"
                    "%d != %d", nbytes_chunk, chunksize);
    return NULL;
  }

  // Check that we are not appending a small chunk after another small chunk
  if ((nchunks > 0) && (nbytes_chunk < chunksize)) {
    uint8_t* last_chunk;
    bool needs_free;
    int retcode = frame_get_chunk(frame, nchunks - 1, &last_chunk, &needs_free);
    if (retcode < 0) {
      fprintf(stderr,
              "cannot get the last chunk (in position %d)", nchunks - 1);
      return NULL;
    }
    int32_t last_nbytes = sw32_(last_chunk + 4);
    if (needs_free) {
      free(last_chunk);
    }
    if ((last_nbytes < chunksize) && (nbytes < chunksize)) {
      fprintf(stderr,
              "appending two consecutive chunks with a chunksize smaller than the frame chunksize"
              "is not allowed yet: "
              "%d != %d", nbytes_chunk, chunksize);
      return NULL;
    }
  }
  // Get the current offsets and add one more
  int32_t off_nbytes = (nchunks + 1) * 8;
  int64_t* offsets = malloc((size_t)off_nbytes);
  if (nchunks > 0) {
    rc = get_offsets(frame, header_len, cbytes, nchunks, offsets);
    if (rc < 0) {
      fprintf(stderr, "Error: cannot get the offset for chunk %d for the frame\n", nchunks);
      return NULL;
    }
  }
  // Add the new offset
  offsets[nchunks] = cbytes;

  // Re-compress the offsets again
  blosc2_context* cctx = blosc2_create_cctx(BLOSC2_CPARAMS_DEFAULTS);
  cctx->typesize = 8;
  void* off_chunk = malloc((size_t)off_nbytes + BLOSC_MAX_OVERHEAD);
  int32_t new_off_cbytes = blosc2_compress_ctx(cctx, (size_t)off_nbytes, offsets,
          off_chunk, (size_t)off_nbytes + BLOSC_MAX_OVERHEAD);
  blosc2_free_ctx(cctx);
  free(offsets);
  if (new_off_cbytes < 0) {
    free(off_chunk);
    return NULL;
  }

  int64_t new_frame_len = header_len + new_cbytes + new_off_cbytes + trailer_len;

  FILE* fp = NULL;
  if (frame->sdata != NULL) {
    uint8_t* framep = frame->sdata;
    /* Make space for the new chunk and copy it */
    frame->sdata = framep = realloc(framep, (size_t)new_frame_len);
    if (framep == NULL) {
      fprintf(stderr, "cannot realloc space for the frame.");
      return NULL;
    }
    /* Copy the chunk */
    memcpy(framep + header_len + cbytes, chunk, (size_t)cbytes_chunk);
    /* Copy the offsets */
    memcpy(framep + header_len + new_cbytes, off_chunk, (size_t)new_off_cbytes);
  } else {
    // fileframe
    fp = fopen(frame->fname, "rb+");
    fseek(fp, header_len + cbytes, SEEK_SET);
    size_t wbytes = fwrite(chunk, 1, (size_t)cbytes_chunk, fp);  // the new chunk
    if (wbytes != (size_t)cbytes_chunk) {
      fprintf(stderr, "cannot write the full chunk to fileframe.");
      return NULL;
    }
    wbytes = fwrite(off_chunk, 1, (size_t)new_off_cbytes, fp);  // the new offsets
    if (wbytes != (size_t)new_off_cbytes) {
      fprintf(stderr, "cannot write the offsets to fileframe.");
      return NULL;
    }
    fclose(fp);
  }
  free(chunk);
  free(off_chunk);

  frame->len = new_frame_len;
  rc = frame_update_header(frame, schunk, false);
  if (rc < 0) {
    return NULL;
  }

  rc = frame_update_trailer(frame, schunk);
  if (rc < 0) {
    return NULL;
  }

  return frame;
}


/* Decompress and return a chunk that is part of a frame. */
int frame_decompress_chunk(blosc2_frame *frame, int nchunk, void *dest, size_t nbytes) {
  uint8_t* src;
  bool needs_free;
  int retcode = frame_get_chunk(frame, nchunk, &src, &needs_free);
  if (retcode < 0) {
    fprintf(stderr,
            "cannot get the chunk in position %d", nchunk);
    return -1;
  }

  /* Create a buffer for destination */
  int32_t nbytes_ = sw32_(src + 4);
  if (nbytes_ > (int32_t)nbytes) {
    fprintf(stderr, "Not enough space for decompressing in dest");
    return -1;
  }

  /* And decompress it */
  int32_t chunksize = blosc_decompress(src, dest, nbytes);

  if (needs_free) {
    free(src);
  }
  return (int)chunksize;
}
