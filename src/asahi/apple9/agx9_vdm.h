/*
 * Copyright 2026 Jiuyang Liu
 * SPDX-License-Identifier: MIT
 *
 * Apple9 VDM stream encoder. Native counterpart: AGX::VDMEncoderGen5 and
 * the draw entry points of AGXG16GFamilyRenderContext in AGXMetalG16G_B0
 * (26A428). See README.md for provenance; U: ranges cite that image.
 */

#ifndef AGX9_VDM_H
#define AGX9_VDM_H

#include <stdbool.h>
#include <stdint.h>
#include "agx_pack.h"

/*
 * Every block is written at the stream cursor and the cursor advances by the
 * block's size. The draw entry points do not check for space: the state
 * emitter reserves 0xb8 bytes per draw up front (U:2149c2244..2149c2288).
 */
#define AGX9_VDM_DRAW_RESERVATION 0xb8

/* "VDM State" word of the vertex state block, U:2148da1ac..2148da1c0. */
static inline uint32_t *
agx9_vdm_vertex_state(uint32_t *out,
                      const struct AGX_APPLE9_VDM_VERTEX_STATE *state)
{
   agx_pack(out, VDM_STATE, cfg) {
      cfg.vertex_shader_word_0_present = true;
      cfg.vertex_shader_word_1_present = true;
      cfg.vertex_outputs_present = true;
      cfg.vertex_unknown_present = true;
   }
   out += AGX_VDM_STATE_LENGTH / 4;

   AGX_APPLE9_VDM_VERTEX_STATE_pack(out, state);
   return out + AGX_APPLE9_VDM_VERTEX_STATE_LENGTH / 4;
}

/* The state-load address is split as bits 38..47 and bits 6..37. */
static inline void
agx9_vdm_set_state_load(struct AGX_APPLE9_VDM_VERTEX_STATE *state,
                        uint64_t address)
{
   state->state_load_hi = (address >> 38) & 0x3ff;
   state->state_load_lo = (uint32_t)(address >> 6);
}

/* 8 bytes, identical to Apple8 (U:2149c1ce4..2149c1cec). */
static inline uint32_t *
agx9_vdm_ppp_state(uint32_t *out, uint64_t address, unsigned words)
{
   agx_pack(out, PPP_STATE, cfg) {
      cfg.pointer_hi = (address >> 32) & 0xff;
      cfg.pointer_lo = (uint32_t)address;
      cfg.size_words = words;
   }
   return out + AGX_PPP_STATE_LENGTH / 4;
}

/* Non-indexed direct draw, U:2149ed86c..2149ed898: the control word comes
 * from PrimitiveTypeToVDMCTRLTypeNonIndexedDraw (U:215043db0) with restart
 * cleared, i.e. count, instance count and start present, index size U32. */
static inline uint32_t *
agx9_vdm_draw(uint32_t *out, enum agx_primitive primitive, uint32_t count,
              uint32_t instances, uint32_t start)
{
   agx_pack(out, INDEX_LIST, cfg) {
      cfg.primitive = primitive;
      cfg.index_size = AGX_INDEX_SIZE_U32;
      cfg.index_count_present = true;
      cfg.instance_count_present = true;
      cfg.start_present = true;
   }
   out += AGX_INDEX_LIST_LENGTH / 4;

   *(out++) = count;
   *(out++) = instances;
   *(out++) = start;
   return out;
}

/* {0x40000001, comparand} precedes every indexed draw unconditionally
 * (U:2149ee3d4..2149ee3e4). The comparand is clamped to 0xffff for 16-bit
 * indices (U:2149ee3b8..2149ee3d0). */
static inline uint32_t *
agx9_vdm_restart_index(uint32_t *out, enum agx_index_size size,
                       uint32_t comparand)
{
   agx_pack(out, VDM_STATE, cfg) {
      cfg.restart_index_present = true;
   }
   out += AGX_VDM_STATE_LENGTH / 4;

   if (size == AGX_INDEX_SIZE_U16 && comparand > 0xffff)
      comparand = 0xffff;

   *(out++) = comparand;
   return out;
}

/* Control word of an indexed draw, U:2149ee3f8..2149ee42c. */
static inline uint32_t *
agx9_vdm_index_list(uint32_t *out, enum agx_primitive primitive,
                    enum agx_index_size size, bool restart, uint64_t indices,
                    bool indirect)
{
   agx_pack(out, INDEX_LIST, cfg) {
      cfg.index_buffer_hi = (indices >> 32) & 0xff;
      cfg.primitive = primitive;
      cfg.restart_enable = restart;
      cfg.index_size = size;
      cfg.index_buffer_size_present = true;
      cfg.index_buffer_present = true;
      cfg.index_count_present = !indirect;
      cfg.instance_count_present = !indirect;
      cfg.start_present = !indirect;
      cfg.indirect_buffer_present = indirect;
   }
   return out + AGX_INDEX_LIST_LENGTH / 4;
}

/* The two words that close an indexed draw: range and address bits 40..47
 * (U:2149ee3e8..2149ee3f4, U:2149ee430..2149ee44c). The range runs from the
 * 4-byte aligned start of the index data for extent_B bytes. */
static inline uint32_t *
agx9_vdm_index_tail(uint32_t *out, uint64_t indices, uint64_t extent_B)
{
   agx_pack(out, APPLE9_INDEX_LIST_BUFFER_SIZE, cfg) {
      cfg.words = (extent_B + (indices & 3) + 3) >> 2;
   }
   out += AGX_APPLE9_INDEX_LIST_BUFFER_SIZE_LENGTH / 4;

   agx_pack(out, APPLE9_INDEX_LIST_BUFFER_HI, cfg) {
      cfg.address_bits_40_to_47 = (indices >> 40) & 0xff;
   }
   return out + AGX_APPLE9_INDEX_LIST_BUFFER_HI_LENGTH / 4;
}

/* Indexed direct draw, nine words, U:2149ee388..2149ee454. */
static inline uint32_t *
agx9_vdm_draw_indexed(uint32_t *out, enum agx_primitive primitive,
                      enum agx_index_size size, bool restart,
                      uint32_t comparand, uint64_t indices, uint64_t extent_B,
                      uint32_t count, uint32_t instances, int32_t base_vertex)
{
   out = agx9_vdm_restart_index(out, size, comparand);
   out = agx9_vdm_index_list(out, primitive, size, restart, indices, false);
   *(out++) = (uint32_t)indices;
   *(out++) = count;
   *(out++) = instances;
   *(out++) = (uint32_t)base_vertex;
   return agx9_vdm_index_tail(out, indices, extent_B);
}

/* U:2149ee738..2149ee760. */
static inline uint32_t *
agx9_vdm_indirect_buffer(uint32_t *out, uint64_t indirect)
{
   agx_pack(out, APPLE9_INDEX_LIST_INDIRECT_BUFFER, cfg) {
      cfg.address_hi = (indirect >> 32) & 0xffff;
      cfg.address_lo = (uint32_t)indirect & ~3u;
   }
   return out + AGX_APPLE9_INDEX_LIST_INDIRECT_BUFFER_LENGTH / 4;
}

/* Non-indexed indirect draw, three words: the control word comes from
 * PrimitiveTypeToVDMCTRLTypeNonIndexedDrawIndirect (U:215043e00). */
static inline uint32_t *
agx9_vdm_draw_indirect(uint32_t *out, enum agx_primitive primitive,
                       uint64_t indirect)
{
   agx_pack(out, INDEX_LIST, cfg) {
      cfg.primitive = primitive;
      cfg.index_size = AGX_INDEX_SIZE_U32;
      cfg.indirect_buffer_present = true;
   }
   out += AGX_INDEX_LIST_LENGTH / 4;

   return agx9_vdm_indirect_buffer(out, indirect);
}

/* Indexed indirect draw, eight words, U:2149ee9b4..2149eeaa8. */
static inline uint32_t *
agx9_vdm_draw_indexed_indirect(uint32_t *out, enum agx_primitive primitive,
                               enum agx_index_size size, bool restart,
                               uint32_t comparand, uint64_t indices,
                               uint64_t extent_B, uint64_t indirect)
{
   out = agx9_vdm_restart_index(out, size, comparand);
   out = agx9_vdm_index_list(out, primitive, size, restart, indices, true);
   *(out++) = (uint32_t)indices;
   out = agx9_vdm_indirect_buffer(out, indirect);
   return agx9_vdm_index_tail(out, indices, extent_B);
}

/* Written at the old cursor when a segment is full; the cursor then moves
 * to the start of the new segment (U:2149c2244..2149c2288). */
static inline uint32_t *
agx9_vdm_link(uint32_t *out, uint64_t target)
{
   agx_pack(out, VDM_STREAM_LINK, cfg) {
      cfg.target_hi = (target >> 32) & 0xff;
      cfg.target_lo = (uint32_t)target;
   }
   return out + AGX_VDM_STREAM_LINK_LENGTH / 4;
}

/* U:2149c9f38..2149c9f48. */
static inline uint32_t *
agx9_vdm_terminate(uint32_t *out)
{
   agx_pack(out, APPLE9_VDM_STREAM_TERMINATE, cfg)
      ;
   return out + AGX_APPLE9_VDM_STREAM_TERMINATE_LENGTH / 4;
}

#endif
