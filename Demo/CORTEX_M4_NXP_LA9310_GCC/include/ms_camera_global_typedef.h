/*
 * Mundo Sense
 */

#ifndef __CAMERA_GLOBAL_TYPEDEF_H
#define __CAMERA_GLOBAL_TYPEDEF_H

#include <stdint.h>
#include <string.h>

/*------------------------- CAMERA FIRMWARE VERSION --------------------------*/
#define CAM_SW_BUILD    0xA0
#define CAM_SW_MAJOR    0x00
#define CAM_SW_MINOR    0x00
#define CAM_SW_PATCH    0x00

#define CAMERA_VERSION  ( ((uint32_t)CAM_SW_BUILD << 24) | \
                          ((uint32_t)CAM_SW_MAJOR << 16) | \
                          ((uint32_t)CAM_SW_MINOR <<  8) | \
                          ((uint32_t)CAM_SW_PATCH <<  0) )

#define MAX_CHAR_FOR_TASK_NAME      30
#define MAX_NUM_OF_SLOTS_IN_FRAME   10

#define UNUSED(x) (void)(x)

/*----------------------------------------------------------------------------
                        ENUMS
------------------------------------------------------------------------------*/
typedef enum MS_MOD_SCHEME
{
    MS_OFDM_GMSK = 0,
    MS_OFDM_QPSK = 1,
} MS_MOD_SCHEME;

typedef enum MS_MSG_OPCODE
{
    /*---   RX ==> VSPA_IN : configure LDPC decoder for BCH reception --- */
    MS_MSG_OPCODE_BCH_LDPC_CFG = 0,

    /*---   VSPA_IN ==> RX : BCH successfully decoded by VSPA (carries camera_id) --- */
    MS_MSG_OPCODE_BCH_DECODED,

    /*---   VSPA_IN ==> RX : BCH decode timed out or failed --- */
    MS_MSG_OPCODE_BCH_DECODE_FAIL,

    /*---   RX ==> CAMERA_MGR : BCH decoded — notify upper layer via IPC --- */
    MS_MSG_OPCODE_BCH_NOTIFY_HOST,

    /*---   RX ==> TX : BCH decoded, prepare ACK transmission for slot 1 --- */
    MS_MSG_OPCODE_PREPARE_ACK_TX,

    /*---   TX ==> VSPA_IN : configure VSPA TX encoder during slot 0 --- */
    MS_MSG_OPCODE_TX_ENC_CFG,

    /*---   VSPA_IN ==> TX : TX encoder configured, send ACK over the air --- */
    MS_MSG_OPCODE_TX_ENC_CFG_ACK,

    /*---   TX ==> CAMERA_MGR : ACK successfully transmitted to controller --- */
    MS_MSG_OPCODE_ACK_SENT,

    /*---   HOST ==> CAMERA_MGR : start video transmission to controller --- */
    MS_MSG_OPCODE_START_VIDEO_TX,

    MS_MSG_OPCODE_MAX
} MS_MSG_OPCODE;

typedef enum MS_FEC_MODE
{
    MS_FEC_ENCODER = 0,
    MS_FEC_DECODER = 1,
} MS_FEC_MODE;

typedef enum MS_CC_Err_A
{
    MS_CC_NO_ERR           = 0x00000000,
    MS_CC_ERR_NULL_POINTER = 0x00000001,
} MS_CC_Err_A;

/*----------------------------------------------------------------------------
                        STRUCTURES
------------------------------------------------------------------------------*/
typedef struct S_GlobalDebugInfo
{
    uint64_t    g_Error_A;
    uint8_t     CoreNum;
    uint32_t    VspaInQFull;
    uint32_t    VspaOutQFull;
} S_GlobalDebugInfo, *P_S_GlobalDebugInfo;

typedef struct S_UNIFIED_MSG_BUFF
{
    MS_MSG_OPCODE   opcode;
    uint8_t         camera_id;
    uint32_t        time;
} S_UNIFIED_MSG_BUFF;

#endif /* __CAMERA_GLOBAL_TYPEDEF_H */
