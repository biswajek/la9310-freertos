/*
 * Mundo Sense
 */ 

#ifndef IPI_QUEUE_H_
#define IPI_QUEUE_H_

/***********************************************************************
                                INCLUDES
***********************************************************************/
#include <stdbool.h>
#include <stddef.h>
#include "ms_global_typedef.h"
#include "pal_msgq.h"
#include "pal_sem.h"


#define IPI_NOTIFY_QUEUE_SIZE       8
#define CONFIG_NUM_CORES_IPI    1


/***********************************************************************
                                VARIABLES                                
***********************************************************************/
typedef enum IPIEventID
{
    IPI_EVT_ID_NULL = -1,
    IPI_EVT_ID0 = 0,
    IPI_EVT_ID1 = 1,
    IPI_EVT_ID2 = 2,
    IPI_EVT_ID3 = 3,
    IPI_EVT_ID4 = 4,
    IPI_EVT_ID5 = 5,
    IPI_EVT_ID6 = 6,
    IPI_EVT_ID7 = 7,
    IPI_EVT_ID8 = 8,
    IPI_EVT_ID9 = 9,
    IPI_EVT_ID10 = 10,
    IPI_EVT_ID11 = 11,
    IPI_EVT_ID12 = 12,
    IPI_EVT_ID13 = 13,
    IPI_EVT_ID14 = 14,
    IPI_EVT_ID15 = 15,
   // #ifdef TESTFRAMEWORK_ENABLE
    IPI_EVT_TESTFRAMEWORK = 16,
    //#endif
    #ifdef HAWK_ENABLED
        IPI_EVT_HAWK,
    #endif
    IPI_EVT_ID_MAX
} IPIEventID_t;


typedef struct IPIStatsData
{
    uint32_t current_core;
    uint32_t IPISentStats[ CONFIG_NUM_CORES_IPI ]; 
    uint32_t IPIRecvStats[ CONFIG_NUM_CORES_IPI ]; 
    uint32_t IPIGlobalEnq[ CONFIG_NUM_CORES_IPI ]; 
    uint32_t IPIGlobalDeq[ CONFIG_NUM_CORES_IPI ]; 
} IPIStatsData_t;

extern MsgQ_Handle_t pxRxQueue[ IPI_EVT_ID_MAX ];

extern enum IPIEventID IPIGlobalEventID[ IPI_EVT_ID_MAX ];


/***********************************************************************
                            FUNCTION PROTOTYPES                                
***********************************************************************/

typedef void (* pxEventCb)( enum IPIEventID eventID,
                            void * userData,
                            void * cookie );

enum IPIEventID vIPIEventRegister( enum IPIEventID eventID,
                                   MsgQ_Handle_t * Queue,
                                   pxEventCb cb,
                                   void *cookie,
                                   uint64_t sizeOfElem, char* mqName);

void vIPIEventUnRegister( enum IPIEventID eventID );

bool vIPICoreInit( uint8_t core_id );

bool vIPISendData( uint8_t dstCore,
                         enum IPIEventID eventID,
                         void * useData );

uint32_t vIPISendDatafromISR( uint8_t dstCore,
                         enum IPIEventID eventID,
                         void * useData );

void vIPIGetStats( void * statsData );

#ifdef IPI_GLOBAL_Q_DBG

    void vIPIGlobalQStatusCheck( int dstCore,
                                 int srcCore );
#endif

#endif /* IPI_QUEUE_H_ */
