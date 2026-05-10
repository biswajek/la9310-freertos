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
    /*---   HOST ==> CAMERA_MGR ==> TX : send BCH; camera_id/payload carry control parameters --- */
    MS_MSG_OPCODE_BCH_SEND = 0,

    /*---   TX ==> RX : BCH with control parameters was sent over the air --- */
    MS_MSG_OPCODE_CTRL_MSG_SENT,

    /*---   VSPA_IN ==> RX : ACK received from controller over the air --- */
    MS_MSG_OPCODE_CTRL_ACK,

    /*---   HOST ==> CAMERA_MGR ==> RX : begin video transmission to controller --- */
    MS_MSG_OPCODE_START_VIDEO_TX,

    /*---   RX ==> VSPA_IN : configure VSPA video encoder --- */
    MS_MSG_OPCODE_VIDEO_ENC_CFG,

    /*---   VSPA_IN ==> RX : video encoder cfg-done ACK received from VSPA --- */
    MS_MSG_OPCODE_VIDEO_ENC_CFG_ACK,

    /*---   RX ==> VSPA_IN : trigger VSPA video encoding --- */
    MS_MSG_OPCODE_VIDEO_ENC_RUN,

    /*---   VSPA_IN ==> RX : video encoding complete, frame ready to transmit --- */
    MS_MSG_OPCODE_VIDEO_ENC_DONE,

    /*---   RX ==> CAMERA_MGR ==> HOST : video TX starts next frame --- */
    MS_MSG_OPCODE_TX_READY,

    /*---   TX ==> VSPA_OUT : deliver control message to VSPA via mailbox --- */
    MS_MSG_OPCODE_VSPA_SEND_CTRL,

    /*---   Reserved: legacy request to wait for control ACK from VSPA. --- */
    MS_MSG_OPCODE_VSPA_WAIT_ACK,

    /*---   Reserved: legacy request to wait for video encoder cfg-done ACK. --- */
    MS_MSG_OPCODE_VSPA_WAIT_VIDEO_ENC_ACK,

    /*---   VSPA_IN ==> RX : ACK wait timed out, no response from controller --- */
    MS_MSG_OPCODE_CTRL_ACK_FAIL,

    /*---   CAMERA_MGR ==> OAM/HOST : host IPC test reply --- */
    MS_MSG_OPCODE_HOST_TEST_REPLY,

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
    void           *payload;
    uint8_t         camera_id;
    uint32_t        time;
} S_UNIFIED_MSG_BUFF;

typedef char S_UNIFIED_MSG_BUFF_must_match_host_wire_abi[
    ( sizeof( S_UNIFIED_MSG_BUFF ) == 16U ) ? 1 : -1 ];

#endif /* __CAMERA_GLOBAL_TYPEDEF_H */
