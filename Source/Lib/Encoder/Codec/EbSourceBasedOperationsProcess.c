/*
* Copyright(c) 2019 Intel Corporation
*
* This source code is subject to the terms of the BSD 2 Clause License and
* the Alliance for Open Media Patent License 1.0. If the BSD 2 Clause License
* was not distributed with this source code in the LICENSE file, you can
* obtain it at https://www.aomedia.org/license/software-license. If the Alliance for Open
* Media Patent License 1.0 was not distributed with this source code in the
* PATENTS file, you can obtain it at https://www.aomedia.org/license/patent-license.
*/

#include "EbPictureControlSet.h"
#include "EbSequenceControlSet.h"

#include "EbSourceBasedOperationsProcess.h"
#include "EbInitialRateControlResults.h"
#include "EbPictureDemuxResults.h"
#ifdef ARCH_X86_64
#include <emmintrin.h>
#endif
#include "EbEncHandle.h"
#include "EbUtility.h"
#include "EbPictureManagerProcess.h"
#include "EbReferenceObject.h"
/**************************************
 * Context
 **************************************/

typedef struct SourceBasedOperationsContext {
    EbDctor dctor;
    EbFifo *initial_rate_control_results_input_fifo_ptr;
    EbFifo *picture_demux_results_output_fifo_ptr;
    // local zz cost array
    uint32_t complete_sb_count;
    uint8_t *y_mean_ptr;
    uint8_t *cr_mean_ptr;
    uint8_t *cb_mean_ptr;
} SourceBasedOperationsContext;

static void source_based_operations_context_dctor(EbPtr p) {
    EbThreadContext *             thread_context_ptr = (EbThreadContext *)p;
    SourceBasedOperationsContext *obj = (SourceBasedOperationsContext *)thread_context_ptr->priv;
    EB_FREE_ARRAY(obj);
}

/************************************************
* Source Based Operation Context Constructor
************************************************/
EbErrorType source_based_operations_context_ctor(EbThreadContext *  thread_context_ptr,
                                                 const EbEncHandle *enc_handle_ptr, int index) {
    SourceBasedOperationsContext *context_ptr;
    EB_CALLOC_ARRAY(context_ptr, 1);
    thread_context_ptr->priv  = context_ptr;
    thread_context_ptr->dctor = source_based_operations_context_dctor;

    context_ptr->initial_rate_control_results_input_fifo_ptr =
        svt_system_resource_get_consumer_fifo(
            enc_handle_ptr->initial_rate_control_results_resource_ptr, index);
    context_ptr->picture_demux_results_output_fifo_ptr = svt_system_resource_get_producer_fifo(
        enc_handle_ptr->picture_demux_results_resource_ptr, index);
    return EB_ErrorNone;
}

/***************************************************
* Derives BEA statistics and set activity flags
***************************************************/
void derive_picture_activity_statistics(PictureParentControlSet *pcs_ptr)

{
    uint64_t non_moving_index_min = ~0u;
    uint64_t non_moving_index_max = 0;
    uint64_t non_moving_index_sum = 0;
    uint32_t complete_sb_count    = 0;
    uint32_t non_moving_sb_count  = 0;
    uint32_t sb_total_count       = pcs_ptr->sb_total_count;

    for (uint32_t sb_index = 0; sb_index < sb_total_count; ++sb_index) {
        SbParams *sb_params = &pcs_ptr->sb_params_array[sb_index];
        if (sb_params->is_complete_sb) {
            non_moving_index_min = pcs_ptr->non_moving_index_array[sb_index] < non_moving_index_min
                ? pcs_ptr->non_moving_index_array[sb_index]
                : non_moving_index_min;

            non_moving_index_max = pcs_ptr->non_moving_index_array[sb_index] > non_moving_index_max
                ? pcs_ptr->non_moving_index_array[sb_index]
                : non_moving_index_max;
            if (pcs_ptr->non_moving_index_array[sb_index] < NON_MOVING_SCORE_1)
                non_moving_sb_count++;
            complete_sb_count++;

            non_moving_index_sum += pcs_ptr->non_moving_index_array[sb_index];
        }
    }

    if (complete_sb_count > 0) {
        pcs_ptr->non_moving_index_average = (uint16_t)(non_moving_index_sum / complete_sb_count);
        pcs_ptr->kf_zeromotion_pct        = (non_moving_sb_count * 100) / complete_sb_count;
    }
    pcs_ptr->non_moving_index_min_distance = (uint16_t)(
        ABS((int32_t)(pcs_ptr->non_moving_index_average) - (int32_t)non_moving_index_min));
    pcs_ptr->non_moving_index_max_distance = (uint16_t)(
        ABS((int32_t)(pcs_ptr->non_moving_index_average) - (int32_t)non_moving_index_max));
    return;
}

EbErrorType tpl_get_open_loop_me(PictureManagerContext *context_ptr, SequenceControlSet *scs_ptr,
                                 PictureParentControlSet *pcs_tpl_base_ptr);
EbErrorType tpl_mc_flow(EncodeContext *encode_context_ptr, SequenceControlSet *scs_ptr,
                        PictureParentControlSet *pcs_ptr);
/************************************************
 * Source Based Operations Kernel
 * Source-based operations process involves a number of analysis algorithms
 * to identify spatiotemporal characteristics of the input pictures.
 ************************************************/
void *source_based_operations_kernel(void *input_ptr) {
    EbThreadContext *             thread_context_ptr = (EbThreadContext *)input_ptr;
    SourceBasedOperationsContext *context_ptr        = (SourceBasedOperationsContext *)
                                                    thread_context_ptr->priv;
    PictureParentControlSet *  pcs_ptr;
    EbObjectWrapper *          in_results_wrapper_ptr;
    InitialRateControlResults *in_results_ptr;
    EbObjectWrapper *          out_results_wrapper_ptr;

    for (;;) {
        // Get Input Full Object
        EB_GET_FULL_OBJECT(context_ptr->initial_rate_control_results_input_fifo_ptr,
                           &in_results_wrapper_ptr);

        in_results_ptr = (InitialRateControlResults *)in_results_wrapper_ptr->object_ptr;
        pcs_ptr        = (PictureParentControlSet *)in_results_ptr->pcs_wrapper_ptr->object_ptr;
        context_ptr->complete_sb_count = 0;
        uint32_t sb_total_count        = pcs_ptr->sb_total_count;
        uint32_t sb_index;

        SequenceControlSet *scs_ptr = (SequenceControlSet *)pcs_ptr->scs_wrapper_ptr->object_ptr;
        // Get TPL ME

        if (scs_ptr->in_loop_me == 0 && scs_ptr->static_config.enable_tpl_la) {
            tpl_get_open_loop_me(NULL, scs_ptr, pcs_ptr);

            if (/*scs_ptr->in_loop_me &&*/ scs_ptr->static_config.enable_tpl_la &&
                pcs_ptr->temporal_layer_index == 0) {
                tpl_mc_flow(scs_ptr->encode_context_ptr, scs_ptr, pcs_ptr);
            }
            //any picture not belonging to any TPL group should release its PA references
            if (pcs_ptr->num_tpl_grps == 0) {
                release_pa_reference_objects(scs_ptr, pcs_ptr);
            }
        }

        /***********************************************SB-based operations************************************************************/
        for (sb_index = 0; sb_index < sb_total_count; ++sb_index) {
            SbParams *sb_params      = &pcs_ptr->sb_params_array[sb_index];
            EbBool    is_complete_sb = sb_params->is_complete_sb;
            uint8_t * y_mean_ptr     = pcs_ptr->y_mean[sb_index];
#ifdef ARCH_X86_64
            _mm_prefetch((const char *)y_mean_ptr, _MM_HINT_T0);
#endif
            uint8_t *cr_mean_ptr = pcs_ptr->cr_mean[sb_index];
            uint8_t *cb_mean_ptr = pcs_ptr->cb_mean[sb_index];
#ifdef ARCH_X86_64
            _mm_prefetch((const char *)cr_mean_ptr, _MM_HINT_T0);
            _mm_prefetch((const char *)cb_mean_ptr, _MM_HINT_T0);
#endif
            context_ptr->y_mean_ptr  = y_mean_ptr;
            context_ptr->cr_mean_ptr = cr_mean_ptr;
            context_ptr->cb_mean_ptr = cb_mean_ptr;

            if (is_complete_sb) {
                context_ptr->complete_sb_count++;
            }
        }
        /*********************************************Picture-based operations**********************************************************/

        // Activity statistics derivation
        derive_picture_activity_statistics(pcs_ptr);

        // Get Empty Results Object
        svt_get_empty_object(context_ptr->picture_demux_results_output_fifo_ptr,
                             &out_results_wrapper_ptr);

        PictureDemuxResults *out_results_ptr = (PictureDemuxResults *)
                                                   out_results_wrapper_ptr->object_ptr;
        out_results_ptr->pcs_wrapper_ptr = in_results_ptr->pcs_wrapper_ptr;
        out_results_ptr->picture_type    = EB_PIC_INPUT;

        // Release the Input Results
        svt_release_object(in_results_wrapper_ptr);

        // Post the Full Results Object
        svt_post_full_object(out_results_wrapper_ptr);
    }
    return NULL;
}
