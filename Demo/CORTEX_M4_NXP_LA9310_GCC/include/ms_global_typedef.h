/*
 * Mundo Sense
 */

#ifndef __GLOBAL_TYPEDEF_H
#define __GLOBAL_TYPEDEF_H

#include <stdint.h>
#include <string.h>

/*------------------------- CONTROLLER VERSION ----------------------------
Field	Width	Purpose
MAJOR	8 bits	Increments on breaking protocol/API changes
MINOR	8 bits	Increments on backward-compatible feature additions
PATCH	8 bits	Bug fix increments
BUILD	8 bits	Build type or CI build number
Example: Version 1.2.3 beta would be encoded as:
BUILD = 0xB0 (beta)
MAJOR = 0x01        
MINOR = 0x02
PATCH = 0x03
Packed 32-bit: [BUILD | MAJOR | MINOR | PATCH] = 0xB0010203
*/
#define SW_BUILD        0xA0    /* 0xA0=alpha, 0xB0=beta, 0xC0=RC, 0xFF=release */
#define SW_MAJOR        0x00
#define SW_MINOR        0x00
#define SW_PATCH        0x00

/* Packed 32-bit: [BUILD | MAJOR | MINOR | PATCH] */
#define CONTROLLER_VERSION  ( ((uint32_t)SW_BUILD << 24) | \
                              ((uint32_t)SW_MAJOR << 16) | \
                              ((uint32_t)SW_MINOR <<  8) | \
                              ((uint32_t)SW_PATCH <<  0) )

/**//** @brief Maximum Length for Task Name.*/
#define MAX_CHAR_FOR_TASK_NAME				30

/**//** @brief Define the number of available slots configuration.*/
#define MAX_NUM_OF_SLOTS_IN_FRAME			10



/*----------------------------------------------------------------------------
                        OFDM PARAMETERS
------------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------
                        TX PARAMETERS
------------------------------------------------------------------------------*/


/*----------------------------------------------------------------------------
                        RF PARAMETERS
------------------------------------------------------------------------------*/


/*----------------------------------------------------------------------------
                        RX PARAMETERS
------------------------------------------------------------------------------*/


/*----------------------------------------------------------------------------
                        MSG DATA STRUCTURE PARAMETERS
------------------------------------------------------------------------------*/


/*----------------------------------------------------------------------------
                        GENERAL DEFINES
------------------------------------------------------------------------------*/
#define UNUSED(x) (void) (x)


/*----------------------------------------------------------------------------
                        ENUMS
------------------------------------------------------------------------------*/
/**//** @brief Modulation scheme index*/
typedef enum MS_MOD_SCHEME
{
	MS_OFDM_GMSK	= 0,
	MS_OFDM_QPSK 	= 1,
}MS_MOD_SCHEME;

/**//** @brief MSG Opcode ID*/
typedef enum MS_MSG_OPCODE
{
	/*---   OAM ==> CONTROLLER --- */
	MS_MSG_OPCODE_CONTROL_MSG = 0,

	/*---   CONTROLLER ==> PHY --- */
	MS_MSG_OPCODE_BCH_SEND,

	MS_MSG_OPCODE_MAX
}MS_MSG_OPCODE;

typedef enum
{
	MS_TX_TSK_MSG_START_TX = 0,
	MS_TX_TSK_MSG_MAX
}MS_TX_TSK_MSG;

typedef enum
{
	MS_RX_TSK_MSG_START_RX = 0,
	MS_RX_TSK_MSG_DEC_DONE,
	MS_RX_TSK_MSG_MAX
}MS_RX_TSK_MSG;

typedef enum
{
	MS_MGR_TSK_MSG_MAX
}MS_MGR_TSK_MSG;

/**//** @brief Enum modulation type*/
typedef enum MS_MODULATION_TYPES
{
    MS_MODULATION_8PSK_TYPE  = 3,
    NO_VALID_MODULATION_TYPES = 0xF,
}MS_MODULATION_TYPES;

/**//** @brief Enum number of bits per symbol for each modulation*/
typedef enum MS_MODULATION_NUM_OF_BITS //modType
{
	MS_N_BITS_8PSK = 3
}MS_MODULATION_NUM_OF_BITS;

/**//** @brief Enum FEC mode - Encoder or Decoder*/
typedef enum MS_FEC_MODE  //checksum
{
	MS_FEC_ENCODER = 0,
	MS_FEC_DECODER = 1,
}MS_FEC_MODE;


/**@brief Enum Section-A of Enumerates type for the available errors.*/
typedef enum MS_CC_Err_A{
	MS_CC_NO_ERR 								        		= 0x00000000,	/*!<There are no errors.*/
    MS_CC_ERR_NULL_POINTER						        		= 0x00000001,	/*!<Null pointer error.*/
}MS_CC_Err_A;


/*----------------------------------------------------------------------------
                        STRUCTURES
------------------------------------------------------------------------------*/
typedef struct S_GlobalDebugInfo
{
	uint64_t	g_Error_A;
	uint8_t     CoreNum;
	uint32_t    VspaInQFull;  /*!< Count of messages dropped because the VSPA-in queue was full. */
	uint32_t    VspaOutQFull; /*!< Count of messages dropped because the VSPA-out queue was full. */
}S_GlobalDebugInfo, * P_S_GlobalDebugInfo;

/*----------------------------------------------------------------------------
                        MSG DATA STRUCTURE
------------------------------------------------------------------------------*/

/**
 * @brief Unified inter-task message buffer.
 *
 * Placeholder structure passed between L1 subsystems via proc queues.
 * Extend with opcode and payload fields as data paths are defined.
 */
typedef struct S_UNIFIED_MSG_BUFF
{
    MS_MSG_OPCODE   opcode;   /*!< Message operation code. */
    void           *payload;  /*!< Pointer to message-specific data. */
	uint8_t         camera_id;
	uint32_t        time;
} S_UNIFIED_MSG_BUFF;




#endif