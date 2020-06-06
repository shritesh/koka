/*---------------------------------------------------------------------------
  Copyright 2020 Daan Leijen, Microsoft Corporation.

  This is free software; you can redistribute it and/or modify it under the
  terms of the Apache License, Version 2.0. A copy of the License can be
  found in the file "license.txt" at the root of this distribution.
---------------------------------------------------------------------------*/
#include "runtime.h"

/*--------------------------------------------------------------------------------------
  Decrementing reference counts
  When freeing a block, we need to decrease reference counts of its children
  recursively. We carefully optimize to use no stack space in case of single field
  chains (like lists) and recurse to limited depth in other cases, using a
  `delayed_free` list in the thread local data. The `delayed_free` list is
  encoded in the headers and thus needs no allocation.
--------------------------------------------------------------------------------------*/

// Decrement a refcount without freeing the block yet. Returns true if there are no more references.
static bool block_decref_no_free(block_t* b) {
  uint32_t count = (b->header.rc32.lo)--;
#if REFCOUNT_LIMIT_TO_32BIT
  if (count == 0) return true;
#else
  if (count==0) {
    if (likely(b->header.rc32.hi == 0)) return true;
    b->header.rc32.hi--; // propagate borrow.
  }
#endif
  return false;
}

// Push a block on the delayed-free list
static void block_push_delayed_free(block_t* b, context_t* ctx) {
  assert_internal(b->header.h.refcount == 0);
  block_t* delayed = ctx->delayed_free;
  // encode the next pointer into the block header
  b->header.rc32.lo = (uint32_t)((uintx_t)delayed);
#if (INTPTR_SIZE > 4)
  b->header.h.tag = (uint16_t)(sar((intx_t)delayed,32));
  assert_internal(sar((intx_t)delayed,48) == 0 || sar((intx_t)delayed, 48) == -1);
#endif
  ctx->delayed_free = b;
}

static void block_free_raw(block_t* b);
static noinline void block_decref_free(block_t* b, size_t depth, context_t* ctx);

// Free all delayed free blocks.
// TODO: limit to a certain number to limit worst-case free times?
static void block_decref_delayed(context_t* ctx) {
  block_t* delayed;
  while ((delayed = ctx->delayed_free) != NULL) {
    ctx->delayed_free = NULL;
    do {
      block_t* b = delayed;
      // decode the next element in the delayed list from the block header
      intx_t next = (intx_t)b->header.rc32.lo;
#if (INTPTR_SIZE>4)
      next += ((intx_t)((int16_t)(b->header.h.tag)) << 32); // sign extended
#endif
#ifndef NDEBUG
      b->header.rc32.lo = UINT32_MAX;
#endif
      delayed = (block_t*)next;
      // and free the block
      block_decref_free(b, 0, ctx);
    } while (delayed != NULL);
  }
}

#define MAX_RECURSE_DEPTH (100)

typedef struct block_fields_s {
  block_t _block;
  box_t   fields[1];
} block_fields_t;

static inline box_t block_field(block_t* b, size_t index) {
  block_fields_t* bf = (block_fields_t*)b;  // must overlap with datatypes with scanned fields.
  return bf->fields[index];
}

// Free recursively a block -- if the recursion becomes too deep, push
// blocks on the delayed free list to free them later. The delayed free list
// is encoded in the headers and needs no further space.
static noinline void block_decref_free(block_t* b, size_t depth, context_t* ctx) {
  while(true) {
    assert_internal(b->header.rc32.lo == UINT32_MAX);
    size_t scan_fsize = b->header.h.scan_fsize;
    if (scan_fsize == 0) {
      // nothing to scan, just free
      if (tag_is_raw(block_tag(b))) block_free_raw(b); // potentially call custom `free` function on the data
      runtime_free(b);
      return;
    }
    else if (scan_fsize == 1) {
      // if just one field, we can recursively free without using stack space
      const box_t v = block_field(b, 0);;
      runtime_free(b);
      if (is_non_null_ptr(v)) {
        // try to free the child now
        b = unbox_ptr(v);
        if (block_decref_no_free(b)) {
          // continue freeing on this block
          continue; // tailcall
        }
      }
      return;
    }
    else {
      // more than 1 field
      if (depth < MAX_RECURSE_DEPTH) {
        size_t i = 0;
        if (unlikely(scan_fsize >= SCAN_FSIZE_MAX)) { 
          scan_fsize = unbox_enum(block_field(b, 0)); 
          i++;
        }
        // free fields up to the last one
        for (; i < (scan_fsize-1); i++) {
          box_t v = block_field(b, i);
          if (is_non_null_ptr(v)) {
            block_t* vb = unbox_ptr(v);
            if (block_decref_no_free(vb)) {
              block_decref_free(vb, depth+1, ctx); // recurse with increased depth
            }
          }
        }
        // and recurse into the last one
        box_t v = block_field(b,scan_fsize - 1);
        runtime_free(b);
        if (is_non_null_ptr(v)) {
          b = unbox_ptr(v);
          if (block_decref_no_free(b)) {
            continue; // tailcall
          }
        }
        return;
      }
      else {
        // recursed too deep, push this block onto the todo list
        block_push_delayed_free(b,ctx);
        return;
      }
    }
  }
}

static void block_free_raw(block_t* b) {
  assert_internal(tag_is_raw(block_tag(b)));
  struct cptr_raw_s* raw = (struct cptr_raw_s*)b;  // all raw structures must overlap this!
  if (raw->free != NULL) {
    (*raw->free)(raw->cptr);
  }
}

// Free a block and recursively decrement reference counts on children.
void block_free(block_t* b, context_t* ctx) {
  assert_internal(b->header.rc32.lo == UINT32_MAX);
  if (b->header.h.scan_fsize==0) {
    if (tag_is_raw(block_tag(b))) block_free_raw(b);
    runtime_free(b); // deallocate directly if nothing to scan
  }
  else {
    block_decref_free(b, 0, ctx);  // free recursively
    block_decref_delayed(ctx);     // process delayed frees
  }
}

// Check if a reference decrement caused the block to be free.
void noinline block_check_free(block_t* b, context_t* ctx) {
  assert_internal(b->header.rc32.lo == UINT32_MAX);  // just underflowed
  if (b->header.rc32.hi==0) {
    block_free(b, ctx);  // no more references, free it.
  }
  else {
    // large refcount, propagate the borrow
    b->header.rc32.hi--;
  }
}