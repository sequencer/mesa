/*
 * Copyright 2026 Jiuyang Liu
 * SPDX-License-Identifier: MIT
 *
 * Apple9 per-draw fixed-function state. Native counterpart:
 * AGX::PPPEncoderGen6 and the record storage of AGX::RenderContext in
 * AGXMetalG16G_B0 (26A428). See README.md for provenance; U: ranges cite
 * that image.
 */

#ifndef AGX9_PPP_H
#define AGX9_PPP_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "agx_pack.h"

/*
 * The native driver keeps the nine records in separate data-buffer segments
 * and re-emits a record only when its dirty bit is set. Here one draw owns
 * one page that holds all nine at fixed offsets. The order is the order
 * encodeAndEmitRenderState references them in (U:2149c1c70..2149c21f0).
 */
#define AGX9_PPP_PAGE_SIZE 0x100

enum agx9_ppp_record {
   AGX9_PPP_VARYING_COUNTS,
   AGX9_PPP_FRAGMENT_SHADER,
   AGX9_PPP_OUTPUT_SELECT,
   AGX9_PPP_FRAGMENT,
   AGX9_PPP_FRAGMENT_2,
   AGX9_PPP_RASTER,
   AGX9_PPP_W_CLAMP,
   AGX9_PPP_CULL,
   AGX9_PPP_DEPTH_BIAS_SCISSOR,
   AGX9_PPP_RECORD_COUNT,
};

static const struct {
   uint16_t offset;
   uint8_t words;
} agx9_ppp_records[AGX9_PPP_RECORD_COUNT] = {
   [AGX9_PPP_VARYING_COUNTS] = {0x00, 5},
   [AGX9_PPP_FRAGMENT_SHADER] = {0x40, 7},
   [AGX9_PPP_OUTPUT_SELECT] = {0x5c, 5},
   [AGX9_PPP_FRAGMENT] = {0x70, 7},
   [AGX9_PPP_FRAGMENT_2] = {0x8c, 5},
   /* One viewport: header, region clip, viewport control, viewport. */
   [AGX9_PPP_RASTER] = {0xc0, 0xa},
   [AGX9_PPP_W_CLAMP] = {0xa0, 3},
   [AGX9_PPP_CULL] = {0xac, 2},
   [AGX9_PPP_DEPTH_BIAS_SCISSOR] = {0xf0, 2},
};

struct agx9_ppp_draw {
   struct AGX_APPLE9_VARYING_COUNTS varying_counts;

   /* Absolute GPU addresses of the coefficient table and of the fragment
    * state-load program. */
   uint64_t cf_bindings;
   uint64_t fragment_state_load;
   struct AGX_APPLE9_FRAGMENT_SHADER fragment_shader;

   struct AGX_APPLE9_OUTPUT_SELECT output_select;
   struct AGX_APPLE9_OUTPUT_SIZE output_size;

   /* Fragment control, front and back face and stencil words in their
    * Apple8 layouts, packed by the caller. */
   uint32_t fragment_control;
   uint32_t fragment_face[2];
   uint32_t fragment_stencil[2];
   struct AGX_FRAGMENT_OCCLUSION_QUERY occlusion_query;

   struct AGX_FRAGMENT_CONTROL fragment_control_2;
   struct AGX_APPLE9_FRAGMENT_FACE_2 fragment_face_2[2];
   struct AGX_APPLE9_FRAGMENT_OCCLUSION_QUERY_2 occlusion_query_2;

   struct AGX_REGION_CLIP region_clip;
   struct AGX_VIEWPORT viewport;

   struct AGX_APPLE9_CULL_2 cull_2;
   /* Apple8 "Cull" layout, packed by the caller. */
   uint32_t cull;

   struct AGX_DEPTH_BIAS_SCISSOR depth_bias_scissor;
};

static inline uint32_t *
agx9_ppp_header(uint32_t *out, const struct AGX_PPP_HEADER *present)
{
   AGX_PPP_HEADER_pack(out, present);
   return out + AGX_PPP_HEADER_LENGTH / 4;
}

#define agx9_ppp_push(out, T, values)                                          \
   do {                                                                        \
      AGX_##T##_pack(out, values);                                             \
      out += AGX_##T##_LENGTH / 4;                                             \
   } while (0)

/* page is AGX9_PPP_PAGE_SIZE bytes. */
static inline void
agx9_ppp_pack_draw(uint32_t *page, const struct agx9_ppp_draw *draw)
{
   uint32_t *out;
   memset(page, 0, AGX9_PPP_PAGE_SIZE);

   /* Header constant U:2148d686c. */
   out = page + agx9_ppp_records[AGX9_PPP_VARYING_COUNTS].offset / 4;
   out = agx9_ppp_header(out, &(struct AGX_PPP_HEADER){
                                 AGX_PPP_HEADER_header,
                                 .varying_counts_32 = true,
                                 .varying_word_2 = true,
                              });
   agx9_ppp_push(out, APPLE9_VARYING_COUNTS, &draw->varying_counts);

   /* Header constant U:21496a1a4..21496a1c4. */
   out = page + agx9_ppp_records[AGX9_PPP_FRAGMENT_SHADER].offset / 4;
   out = agx9_ppp_header(out, &(struct AGX_PPP_HEADER){
                                 AGX_PPP_HEADER_header,
                                 .fragment_shader = true,
                              });
   struct AGX_APPLE9_FRAGMENT_SHADER fs = draw->fragment_shader;
   fs.cf_bindings_hi = (draw->cf_bindings >> 32) & 0xffff;
   fs.cf_bindings = (uint32_t)draw->cf_bindings;
   fs.state_load_hi = (draw->fragment_state_load >> 38) & 0x3ff;
   fs.state_load_lo = (uint32_t)(draw->fragment_state_load >> 6);
   agx9_ppp_push(out, APPLE9_FRAGMENT_SHADER, &fs);

   /* Header constant U:21504f0a0. */
   out = page + agx9_ppp_records[AGX9_PPP_OUTPUT_SELECT].offset / 4;
   out = agx9_ppp_header(out, &(struct AGX_PPP_HEADER){
                                 AGX_PPP_HEADER_header,
                                 .output_select = true,
                                 .varying_counts_16 = true,
                                 .output_unknown = true,
                                 .output_size = true,
                              });
   agx9_ppp_push(out, APPLE9_OUTPUT_SELECT, &draw->output_select);
   /* The vertex path writes zero for the "Varying counts 16" word
    * (U:2149cf24c). */
   *(out++) = 0;
   agx9_ppp_push(out, APPLE9_OUTPUT_AMPLIFICATION,
                 &(struct AGX_APPLE9_OUTPUT_AMPLIFICATION){.count = 1});
   agx9_ppp_push(out, APPLE9_OUTPUT_SIZE, &draw->output_size);

   /* Header constant U:215050dc0: the control word is present although
    * "Fragment control" is not set. */
   out = page + agx9_ppp_records[AGX9_PPP_FRAGMENT].offset / 4;
   out = agx9_ppp_header(out, &(struct AGX_PPP_HEADER){
                                 AGX_PPP_HEADER_header,
                                 .fragment_front_face = true,
                                 .fragment_front_stencil = true,
                                 .fragment_back_face = true,
                                 .fragment_back_stencil = true,
                                 .occlusion_query = true,
                              });
   *(out++) = draw->fragment_control;
   *(out++) = draw->fragment_face[0];
   *(out++) = draw->fragment_stencil[0];
   *(out++) = draw->fragment_face[1];
   *(out++) = draw->fragment_stencil[1];
   agx9_ppp_push(out, FRAGMENT_OCCLUSION_QUERY, &draw->occlusion_query);

   /* Header constant U:215050dd0, same remark. */
   out = page + agx9_ppp_records[AGX9_PPP_FRAGMENT_2].offset / 4;
   out = agx9_ppp_header(out, &(struct AGX_PPP_HEADER){
                                 AGX_PPP_HEADER_header,
                                 .fragment_front_face_2 = true,
                                 .fragment_back_face_2 = true,
                                 .occlusion_query_2 = true,
                              });
   agx9_ppp_push(out, FRAGMENT_CONTROL, &draw->fragment_control_2);
   agx9_ppp_push(out, APPLE9_FRAGMENT_FACE_2, &draw->fragment_face_2[0]);
   agx9_ppp_push(out, APPLE9_FRAGMENT_FACE_2, &draw->fragment_face_2[1]);
   agx9_ppp_push(out, APPLE9_FRAGMENT_OCCLUSION_QUERY_2,
                 &draw->occlusion_query_2);

   /* PPPEncoderGen6::RasterToken, U:2149c1ef4..2149c200c. */
   out = page + agx9_ppp_records[AGX9_PPP_RASTER].offset / 4;
   out = agx9_ppp_header(out, &(struct AGX_PPP_HEADER){
                                 AGX_PPP_HEADER_header,
                                 .region_clip = true,
                                 .viewport = true,
                              });
   agx9_ppp_push(out, REGION_CLIP, &draw->region_clip);
   agx9_ppp_push(out, APPLE9_VIEWPORT_CONTROL,
                 &(struct AGX_APPLE9_VIEWPORT_CONTROL){0});
   agx9_ppp_push(out, VIEWPORT, &draw->viewport);

   /* Header constant U:21504f0c0. The W clamp is 0.0 natively: the pipeline
    * constructor writes it and nothing else does (U:214a1abfc). */
   out = page + agx9_ppp_records[AGX9_PPP_W_CLAMP].offset / 4;
   out = agx9_ppp_header(out, &(struct AGX_PPP_HEADER){
                                 AGX_PPP_HEADER_header,
                                 .w_clamp = true,
                                 .cull_2 = true,
                              });
   agx9_ppp_push(out, W_CLAMP, &(struct AGX_W_CLAMP){.w_clamp = 0.0f});
   agx9_ppp_push(out, APPLE9_CULL_2, &draw->cull_2);

   /* Header constant U:21504f098. */
   out = page + agx9_ppp_records[AGX9_PPP_CULL].offset / 4;
   out = agx9_ppp_header(out, &(struct AGX_PPP_HEADER){
                                 AGX_PPP_HEADER_header,
                                 .cull = true,
                              });
   *(out++) = draw->cull;

   /* Header constant U:21504f090. */
   out = page + agx9_ppp_records[AGX9_PPP_DEPTH_BIAS_SCISSOR].offset / 4;
   out = agx9_ppp_header(out, &(struct AGX_PPP_HEADER){
                                 AGX_PPP_HEADER_header,
                                 .depth_bias_scissor = true,
                              });
   agx9_ppp_push(out, DEPTH_BIAS_SCISSOR, &draw->depth_bias_scissor);
}

#endif
