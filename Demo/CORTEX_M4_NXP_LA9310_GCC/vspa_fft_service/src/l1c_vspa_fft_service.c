#include <stddef.h>
#include <stdint.h>
#include <vspa/intrinsics.h>

#include "vcpu.h"
#include "host.h"
#include "diffft.h"
#include "fecu.h"
#include "l1c_vspa_fft_service.h"

extern void boot( void );

static cfixed16_t xFftIn[ L1C_VSPA_FFT_POINTS ] __attribute__( ( aligned( 128 ) ) ) __attribute__( ( section( ".vbss0" ) ) );
static cfixed16_t xFftOut[ L1C_VSPA_FFT_POINTS ] __attribute__( ( aligned( 128 ) ) ) __attribute__( ( section( ".vbss1" ) ) );
static cfixed16_t xIfftOut[ L1C_VSPA_FFT_POINTS ] __attribute__( ( aligned( 128 ) ) ) __attribute__( ( section( ".vbss2" ) ) );
static uint32_t ulProfileReady;
static uint32_t ulProfilePeakIndex;
static uint32_t ulProfilePeakMagnitude;
static uint32_t ulLastLdpcNonZero;
static uint32_t ulLastLdpcFecuStatus;
static uint32_t ulLastLdpcBitErrors;

#define L1C_VSPA_LDPC_LOOPBACK_CASE_SPARSE_FIRST       0u
#define L1C_VSPA_LDPC_LOOPBACK_CASE_ALL_ZERO           1u
#define L1C_VSPA_LDPC_LOOPBACK_CASE_ALL_ONE            2u
#define L1C_VSPA_LDPC_LOOPBACK_CASE_SPARSE_SECOND      3u
#define L1C_VSPA_LDPC_LOOPBACK_CASE_SPARSE_MID         4u
#define L1C_VSPA_LDPC_LOOPBACK_CASE_COUNT              3u
#define L1C_VSPA_LDPC_LOOPBACK_NO_FAIL                 0xFFu
#define L1C_VSPA_LDPC_LOOPBACK_TRIAL_COUNT             2u
#define L1C_VSPA_LDPC_LOOPBACK_MAP_COUNT               12u
#define L1C_VSPA_LDPC_ENCODED_STRIDE_HALFWORDS         42u
#define L1C_VSPA_LDPC_LOOPBACK_DECODE_CHUNK_BLOCKS     3u

static void vPostMbox0( uint32_t ulMsb, uint32_t ulLsb )
{
    while( host_mbox0_is_full() )
    {
    }

    host_mbox0_post( MAKEDWORD( ulMsb, ulLsb ) );
}

static void vClearBuffer( cfixed16_t * pxBuf )
{
    uint32_t * pulBuf = ( uint32_t * ) pxBuf;
    uint32_t ulIndex;

    for( ulIndex = 0; ulIndex < L1C_VSPA_FFT_POINTS; ulIndex++ )
    {
        pulBuf[ ulIndex ] = 0u;
    }
}

static uint32_t ulChecksumBuffer( cfixed16_t * pxBuf )
{
    uint32_t * pulBuf = ( uint32_t * ) pxBuf;
    uint32_t ulChecksum = 0x9E3779B9u;
    uint32_t ulIndex;

    for( ulIndex = 0; ulIndex < L1C_VSPA_FFT_POINTS; ulIndex++ )
    {
        ulChecksum ^= pulBuf[ ulIndex ] + 0x7F4A7C15u + ( ulChecksum << 6 ) + ( ulChecksum >> 2 );
    }

    return ulChecksum;
}

static uint32_t ulCountNonZeroBuffer( cfixed16_t * pxBuf )
{
    uint32_t * pulBuf = ( uint32_t * ) pxBuf;
    uint32_t ulCount = 0u;
    uint32_t ulIndex;

    for( ulIndex = 0; ulIndex < L1C_VSPA_FFT_POINTS; ulIndex++ )
    {
        if( pulBuf[ ulIndex ] != 0u )
        {
            ulCount++;
        }
    }

    return ulCount;
}

static void vClearHalfwordBuffer( uint16_t * pusBuf, uint32_t ulHalfwordCount )
{
    uint32_t ulIndex;

    for( ulIndex = 0u; ulIndex < ulHalfwordCount; ulIndex++ )
    {
        pusBuf[ ulIndex ] = 0u;
    }
}

static uint32_t ulChecksumHalfwordBuffer( uint16_t * pusBuf, uint32_t ulHalfwordCount )
{
    uint32_t ulChecksum = 0x811C9DC5u;
    uint32_t ulIndex;

    for( ulIndex = 0u; ulIndex < ulHalfwordCount; ulIndex++ )
    {
        ulChecksum ^= pusBuf[ ulIndex ];
        ulChecksum *= 0x01000193u;
    }

    return ulChecksum;
}

static uint32_t ulCountNonZeroHalfwordBuffer( uint16_t * pusBuf, uint32_t ulHalfwordCount )
{
    uint32_t ulCount = 0u;
    uint32_t ulIndex;

    for( ulIndex = 0u; ulIndex < ulHalfwordCount; ulIndex++ )
    {
        if( pusBuf[ ulIndex ] != 0u )
        {
            ulCount++;
        }
    }

    return ulCount;
}

static uint32_t ulWaitForFecuIdle( void )
{
    uint32_t ulTimeout;

    for( ulTimeout = 0u; ulTimeout < 10000000u; ulTimeout++ )
    {
        if( FECU_GET_STATUS( FECU_STATUS_BUSY_MASK ) == 0u )
        {
            return 1u;
        }
    }

    return 0u;
}

static void vResetFecu( void )
{
    uint32_t ulDelay;

    __builtin_ip_write( FECU_CONFIG_ADDR, 0xFFFFFFFFu,
                        FECU_CONFIG_SW_RESET_MASK | FECU_CONFIG_CLEAR_PENDING_MASK );

    for( ulDelay = 0u; ulDelay < 256u; ulDelay++ )
    {
        __fnop();
    }

    __builtin_ip_write( FECU_CONFIG_ADDR, 0xFFFFFFFFu, FECU_CONFIG_CLEAR_PENDING_MASK );
    ( void ) ulWaitForFecuIdle();
}

static uint32_t ulGetPackedBit( uint16_t * pusBuf, uint32_t ulBitIndex )
{
    return ( pusBuf[ ulBitIndex >> 4u ] >> ( ulBitIndex & 0x0Fu ) ) & 0x1u;
}

static void vSetPackedBit( uint16_t * pusBuf, uint32_t ulBitIndex, uint32_t ulBit )
{
    uint16_t usMask = ( uint16_t ) ( 1u << ( ulBitIndex & 0x0Fu ) );

    if( ulBit != 0u )
    {
        pusBuf[ ulBitIndex >> 4u ] |= usMask;
    }
    else
    {
        pusBuf[ ulBitIndex >> 4u ] &= ( uint16_t ) ~usMask;
    }
}

static uint32_t ulReadLdpcCodeBitVariant( uint16_t * pusEncoded,
                                           uint32_t ulBitIndex,
                                           uint32_t ulVariantId )
{
    uint32_t ulPairIndex = ulBitIndex / 32u;
    uint32_t ulBitInWord = ulBitIndex % 32u;
    uint32_t ulLo = pusEncoded[ 2u * ulPairIndex ];
    uint32_t ulHi = pusEncoded[ ( 2u * ulPairIndex ) + 1u ];
    uint32_t ulWord32;

    if( ( ulVariantId & 0x2u ) != 0u )
    {
        ulWord32 = ( ulLo << 16u ) | ulHi;
    }
    else
    {
        ulWord32 = ( ulHi << 16u ) | ulLo;
    }

    if( ( ulVariantId & 0x1u ) != 0u )
    {
        return ( ulWord32 >> ( 31u - ulBitInWord ) ) & 0x1u;
    }

    return ( ulWord32 >> ulBitInWord ) & 0x1u;
}

static void vBuildLdpcLlrFromPackedBits( uint16_t * pusEncoded,
                                          uint8_t * pucLlr,
                                          uint32_t ulVariantId,
                                          uint32_t ulInvertPolarity )
{
    uint32_t ulBitIndex;
    uint32_t ulBit;

    for( ulBitIndex = 0u; ulBitIndex < L1C_VSPA_LDPC_CODE_BITS; ulBitIndex++ )
    {
        ulBit = ulReadLdpcCodeBitVariant( pusEncoded, ulBitIndex, ulVariantId );

        if( ulInvertPolarity != 0u )
        {
            ulBit ^= 1u;
        }

        /* FECU LDPC decoder input is byte LLR. 0x40 is strong positive and
         * 0xC0 is strong negative, matching the original FECU sample pattern. */
        pucLlr[ ulBitIndex ] = ( ulBit != 0u ) ? 0xC0u : 0x40u;
    }
}

static uint32_t ulExpectedLoopbackBit( uint32_t ulCaseId, uint32_t ulBitIndex )
{
    if( ulCaseId == L1C_VSPA_LDPC_LOOPBACK_CASE_SPARSE_FIRST )
    {
        return ( ulBitIndex == 0u ) ? 1u : 0u;
    }

    if( ulCaseId == L1C_VSPA_LDPC_LOOPBACK_CASE_ALL_ZERO )
    {
        return 0u;
    }

    if( ulCaseId == L1C_VSPA_LDPC_LOOPBACK_CASE_ALL_ONE )
    {
        return 1u;
    }

    if( ulCaseId == L1C_VSPA_LDPC_LOOPBACK_CASE_SPARSE_SECOND )
    {
        return ( ulBitIndex == 1u ) ? 1u : 0u;
    }

    if( ulCaseId == L1C_VSPA_LDPC_LOOPBACK_CASE_SPARSE_MID )
    {
        return ( ulBitIndex == ( L1C_VSPA_LDPC_MESSAGE_BITS / 2u ) ) ? 1u : 0u;
    }

    return 0u;
}

static void vBuildLdpcSystematicLlr( uint8_t * pucLlr,
                                     uint32_t ulCaseId,
                                     uint32_t ulInvertPolarity )
{
    uint32_t ulBitIndex;
    uint32_t ulBit;

    for( ulBitIndex = 0u; ulBitIndex < L1C_VSPA_LDPC_CODE_BITS; ulBitIndex++ )
    {
        if( ulBitIndex < L1C_VSPA_LDPC_MESSAGE_BITS )
        {
            ulBit = ulExpectedLoopbackBit( ulCaseId, ulBitIndex );
            if( ulInvertPolarity != 0u )
            {
                ulBit ^= 1u;
            }

            pucLlr[ ulBitIndex ] = ( ulBit != 0u ) ? 0xC0u : 0x40u;
        }
        else
        {
            pucLlr[ ulBitIndex ] = 0u;
        }
    }
}

static uint32_t ulLdpcDecodeOutputBitOffset( void )
{
    uint32_t ulInfoHalfwords = ( L1C_VSPA_LDPC_MESSAGE_BITS + 15u ) / 16u;
    uint32_t ulInfoRemainderBits = L1C_VSPA_LDPC_MESSAGE_BITS & 0x1Fu;
    uint32_t ulRepeatBase = ( 2u * ulInfoRemainderBits ) & 0x1Fu;

    return ( ulInfoHalfwords * 16u ) + ulRepeatBase;
}

static uint32_t ulGetPackedBit32Variant( uint16_t * pusBuf,
                                          uint32_t ulBitOffset,
                                          uint32_t ulBitIndex,
                                          uint32_t ulVariantId )
{
    uint32_t ulAbsoluteBit = ulBitOffset + ulBitIndex;
    uint32_t ulPairIndex = ulAbsoluteBit / 32u;
    uint32_t ulBitInWord = ulAbsoluteBit % 32u;
    uint32_t ulLo = pusBuf[ 2u * ulPairIndex ];
    uint32_t ulHi = pusBuf[ ( 2u * ulPairIndex ) + 1u ];
    uint32_t ulWord32;

    if( ( ulVariantId & 0x2u ) != 0u )
    {
        ulWord32 = ( ulLo << 16u ) | ulHi;
    }
    else
    {
        ulWord32 = ( ulHi << 16u ) | ulLo;
    }

    if( ( ulVariantId & 0x1u ) != 0u )
    {
        return ( ulWord32 >> ( 31u - ulBitInWord ) ) & 0x1u;
    }

    return ( ulWord32 >> ulBitInWord ) & 0x1u;
}

static void vFillLdpcLoopbackPayload( uint16_t * pusData, uint32_t ulCaseId )
{
    uint32_t ulBitIndex;

    for( ulBitIndex = 0u; ulBitIndex < L1C_VSPA_LDPC_MESSAGE_BITS; ulBitIndex++ )
    {
        vSetPackedBit( pusData, ulBitIndex, ulExpectedLoopbackBit( ulCaseId, ulBitIndex ) );
    }
}

static void vFillLdpcLoopbackPayloadBlock( uint16_t * pusData,
                                            uint32_t ulBlockId,
                                            uint32_t ulCaseId )
{
    uint32_t ulBitIndex;
    uint32_t ulBaseBit = ulBlockId * L1C_VSPA_LDPC_MESSAGE_BITS;

    for( ulBitIndex = 0u; ulBitIndex < L1C_VSPA_LDPC_MESSAGE_BITS; ulBitIndex++ )
    {
        vSetPackedBit( pusData, ulBaseBit + ulBitIndex,
                       ulExpectedLoopbackBit( ulCaseId, ulBitIndex ) );
    }
}

static uint32_t ulGetDecodedLoopbackBit( uint16_t * pusDecoded,
                                         uint32_t ulBitIndex,
                                         uint32_t ulMapId )
{
    uint32_t ulDecodedBitOffset = ulLdpcDecodeOutputBitOffset();

    if( ulMapId == 1u )
    {
        return ulGetPackedBit32Variant( pusDecoded, ulDecodedBitOffset, ulBitIndex, 1u );
    }

    if( ulMapId == 2u )
    {
        return ulGetPackedBit32Variant( pusDecoded, ulDecodedBitOffset, ulBitIndex, 2u );
    }

    if( ulMapId == 3u )
    {
        return ulGetPackedBit32Variant( pusDecoded, ulDecodedBitOffset, ulBitIndex, 3u );
    }

    if( ulMapId == 4u )
    {
        return ulGetPackedBit32Variant( pusDecoded, 0u, ulBitIndex, 0u );
    }

    if( ulMapId == 5u )
    {
        return ulGetPackedBit32Variant( pusDecoded, 0u, ulBitIndex, 1u );
    }

    if( ulMapId == 6u )
    {
        return ulGetPackedBit32Variant( pusDecoded, 0u, ulBitIndex, 2u );
    }

    if( ulMapId == 7u )
    {
        return ulGetPackedBit32Variant( pusDecoded, 0u, ulBitIndex, 3u );
    }

    if( ulMapId == 8u )
    {
        return ulGetPackedBit( pusDecoded, ulDecodedBitOffset + ( 2u * ulBitIndex ) );
    }

    if( ulMapId == 9u )
    {
        return ulGetPackedBit( pusDecoded, ulDecodedBitOffset + ( 2u * ulBitIndex ) + 1u );
    }

    if( ulMapId == 10u )
    {
        return ulGetPackedBit( pusDecoded, 2u * ulBitIndex );
    }

    if( ulMapId == 11u )
    {
        return ulGetPackedBit( pusDecoded, ( 2u * ulBitIndex ) + 1u );
    }

    return ulGetPackedBit32Variant( pusDecoded, ulDecodedBitOffset, ulBitIndex, 0u );
}

static uint32_t ulCountLoopbackBitErrorsForMap( uint16_t * pusDecoded,
                                                uint32_t ulCaseId,
                                                uint32_t ulMapId )
{
    uint32_t ulBitIndex;
    uint32_t ulExpected;
    uint32_t ulActual;
    uint32_t ulErrors = 0u;

    for( ulBitIndex = 0u; ulBitIndex < L1C_VSPA_LDPC_MESSAGE_BITS; ulBitIndex++ )
    {
        ulExpected = ulExpectedLoopbackBit( ulCaseId, ulBitIndex );
        ulActual = ulGetDecodedLoopbackBit( pusDecoded, ulBitIndex, ulMapId );

        if( ulActual != ulExpected )
        {
            ulErrors++;
        }
    }

    return ulErrors;
}

static uint32_t ulCountLoopbackBitErrorsDirect( uint16_t * pusDecoded,
                                                uint32_t ulLocalBlockId,
                                                uint32_t ulCaseId,
                                                uint32_t ulMapId )
{
    uint32_t ulBitIndex;
    uint32_t ulBaseBit = ulLocalBlockId * L1C_VSPA_LDPC_MESSAGE_BITS;
    uint32_t ulErrors = 0u;
    uint32_t ulActual;

    for( ulBitIndex = 0u; ulBitIndex < L1C_VSPA_LDPC_MESSAGE_BITS; ulBitIndex++ )
    {
        if( ulMapId < 4u )
        {
            ulActual = ulGetPackedBit32Variant( pusDecoded, ulBaseBit, ulBitIndex, ulMapId );
        }
        else if( ulMapId == 4u )
        {
            ulActual = ulGetPackedBit( pusDecoded, ulBaseBit + ( 2u * ulBitIndex ) );
        }
        else
        {
            ulActual = ulGetPackedBit( pusDecoded, ulBaseBit + ( 2u * ulBitIndex ) + 1u );
        }

        if( ulActual != ulExpectedLoopbackBit( ulCaseId, ulBitIndex ) )
        {
            ulErrors++;
        }
    }

    return ulErrors;
}

static uint32_t ulCodingLdpcEncodeTimed( uint16_t * pusDataIn, uint16_t * pusEncodedOut )
{
    unsigned int addr;

    __builtin_ip_write( FECU_CONFIG_ADDR, 0xFFFFFFFFu,
                        ( 1u << FECU_CONFIG_NUM_STREAM_OFFSET ) |
                        FECU_CONFIG_USE_LDPC_MASK |
                        FECU_CONFIG_ENCODE_MASK );
    __builtin_ip_write( FECU_SIZES_ADDR, 0xFFFFFFFFu, ( 648u << 16 ) | 324u );
    __builtin_ip_write( FECU_LDPC_CONFIG_ADDR, 0xFFFFFFFFu,
                        FECU_LDPC_CONFIG_CODING_RATE_1_2 |
                        FECU_LDPC_CONFIG_BLOCK_LENGTH_648 );
    __builtin_ip_write( FECU_LDPC_SIZES_ADDR, 0xFFFFFFFFu, 0u );
    __builtin_ip_write( FECU_LDPC_EXTRA_SHORT_ADDR, 0xFFFFFFFFu, 0u );
    __builtin_ip_write( FECU_LDPC_EXTRA_REP_ADDR, 0xFFFFFFFFu, 0u );
    __builtin_ip_write( FECU_BYPASS_ADDR, 0xFFFFFFFFu,
                        FECU_BYPASS_SCRAMBLER_MASK |
                        FECU_BYPASS_CONVOLUTIONAL_ENCODER_MASK |
                        FECU_BYPASS_VITERBI_DECODER_MASK |
                        FECU_BYPASS_INTERLEAVER_MASK );

    __builtin_ip_write( FECU_DMEM_READ_COUNT_ADDR, 0xFFFFFFu, 324u + 12u );
    addr = ( unsigned int ) pusDataIn;
    __builtin_ip_write( FECU_DMEM_SRC_ADR_ADDR, 0xFF07FFFFu,
                        ( addr & 0x7FFFFu ) | ( 12u << 24 ) );
    addr = ( unsigned int ) pusEncodedOut;
    __builtin_ip_write( FECU_DMEM_DST_ADR_ADDR, 0xFF07FFFFu, addr & 0x7FFFFu );
    __builtin_ip_write( FECU_CONTROL_ADDR, 0xFFFFFFFFu,
                        FECU_CONTROL_IRQEN_DONE_MASK |
                        FECU_CONTROL_FIRST_SYMBOL_MASK |
                        FECU_CONTROL_START_TYPE_START_IMMEDIATELY );

    if( ulWaitForFecuIdle() == 0u )
    {
        return 0u;
    }

    __builtin_ip_write( FECU_DMEM_READ_COUNT_ADDR, 0xFFFFFFu, 312u + 8u );
    addr = ( unsigned int ) &pusDataIn[ 21 ];
    __builtin_ip_write( FECU_DMEM_SRC_ADR_ADDR, 0xFF07FFFFu,
                        ( addr & 0x7FFFFu ) | ( 8u << 24 ) );
    addr = ( unsigned int ) &pusEncodedOut[ 42 ];
    __builtin_ip_write( FECU_DMEM_DST_ADR_ADDR, 0xFF07FFFFu, addr & 0x7FFFFu );
    __builtin_ip_write( FECU_CONTROL_ADDR, 0xFFFFFFFFu,
                        FECU_CONTROL_IRQEN_DONE_MASK |
                        FECU_CONTROL_ONE_BEFORE_FINAL_SYMBOL_MASK |
                        FECU_CONTROL_START_TYPE_START_IMMEDIATELY );

    if( ulWaitForFecuIdle() == 0u )
    {
        return 0u;
    }

    __builtin_ip_write( FECU_DMEM_READ_COUNT_ADDR, 0xFFFFFFu, 316u );
    addr = ( unsigned int ) &pusDataIn[ 41 ];
    __builtin_ip_write( FECU_DMEM_SRC_ADR_ADDR, 0xFF07FFFFu, addr & 0x7FFFFu );
    addr = ( unsigned int ) &pusEncodedOut[ 84 ];
    __builtin_ip_write( FECU_DMEM_DST_ADR_ADDR, 0xFF07FFFFu, addr & 0x7FFFFu );
    __builtin_ip_write( FECU_CONTROL_ADDR, 0xFFFFFFFFu,
                        FECU_CONTROL_IRQEN_DONE_MASK |
                        FECU_CONTROL_FINAL_SYMBOL_MASK |
                        FECU_CONTROL_START_TYPE_START_IMMEDIATELY );

    return ulWaitForFecuIdle();
}

static uint32_t ulCodingLdpcEncodeSingleBlockTimed( uint16_t * pusDataIn,
                                                    uint16_t * pusEncodedOut )
{
    unsigned int addr;

    __builtin_ip_write( FECU_CONFIG_ADDR, 0xFFFFFFFFu,
                        ( 1u << FECU_CONFIG_NUM_STREAM_OFFSET ) |
                        FECU_CONFIG_USE_LDPC_MASK |
                        FECU_CONFIG_ENCODE_MASK );
    __builtin_ip_write( FECU_SIZES_ADDR, 0xFFFFFFFFu, ( 648u << 16 ) | 324u );
    __builtin_ip_write( FECU_LDPC_CONFIG_ADDR, 0xFFFFFFFFu,
                        FECU_LDPC_CONFIG_MAX_ITERATION_SET( 20u ) |
                        FECU_LDPC_CONFIG_CODING_RATE_1_2 |
                        FECU_LDPC_CONFIG_BLOCK_LENGTH_648 );
    __builtin_ip_write( FECU_LDPC_SIZES_ADDR, 0xFFFFFFFFu, 0u );
    __builtin_ip_write( FECU_LDPC_EXTRA_SHORT_ADDR, 0xFFFFFFFFu, 0u );
    __builtin_ip_write( FECU_LDPC_EXTRA_REP_ADDR, 0xFFFFFFFFu, 0u );
    __builtin_ip_write( FECU_BYPASS_ADDR, 0xFFFFFFFFu,
                        FECU_BYPASS_SCRAMBLER_MASK |
                        FECU_BYPASS_CONVOLUTIONAL_ENCODER_MASK |
                        FECU_BYPASS_VITERBI_DECODER_MASK |
                        FECU_BYPASS_INTERLEAVER_MASK );

    __builtin_ip_write( FECU_DMEM_READ_COUNT_ADDR, 0xFFFFFFu, L1C_VSPA_LDPC_MESSAGE_BITS );
    addr = ( unsigned int ) pusDataIn;
    __builtin_ip_write( FECU_DMEM_SRC_ADR_ADDR, 0xFF07FFFFu, addr & 0x7FFFFu );
    addr = ( unsigned int ) pusEncodedOut;
    __builtin_ip_write( FECU_DMEM_DST_ADR_ADDR, 0xFF07FFFFu, addr & 0x7FFFFu );
    __builtin_ip_write( FECU_CONTROL_ADDR, 0xFFFFFFFFu,
                        FECU_CONTROL_IRQEN_DONE_MASK |
                        FECU_CONTROL_FIRST_SYMBOL_MASK |
                        FECU_CONTROL_FINAL_SYMBOL_MASK |
                        FECU_CONTROL_START_TYPE_START_IMMEDIATELY );

    return ulWaitForFecuIdle();
}

static uint32_t ulCodingLdpcEncodeBlocksTimed( uint16_t * pusDataIn,
                                               uint16_t * pusEncodedOut,
                                               uint32_t ulNumBlocks )
{
    uint32_t ulBlockId;
    uint32_t ulTotalOps = ulNumBlocks;
    uint32_t ulPrevKeep = 0u;
    uint32_t ulSrcBitPos = 0u;
    unsigned int addr;

    if( ulTotalOps < 3u )
    {
        ulTotalOps = 3u;
    }

    __builtin_ip_write( FECU_CONFIG_ADDR, 0xFFFFFFFFu,
                        ( 1u << FECU_CONFIG_NUM_STREAM_OFFSET ) |
                        FECU_CONFIG_USE_LDPC_MASK |
                        FECU_CONFIG_ENCODE_MASK );
    __builtin_ip_write( FECU_SIZES_ADDR, 0xFFFFFFFFu,
                        ( L1C_VSPA_LDPC_CODE_BITS << 16 ) | L1C_VSPA_LDPC_MESSAGE_BITS );
    __builtin_ip_write( FECU_LDPC_CONFIG_ADDR, 0xFFFFFFFFu,
                        FECU_LDPC_CONFIG_MAX_ITERATION_SET( 20u ) |
                        FECU_LDPC_CONFIG_CODING_RATE_1_2 |
                        FECU_LDPC_CONFIG_BLOCK_LENGTH_648 );
    __builtin_ip_write( FECU_LDPC_SIZES_ADDR, 0xFFFFFFFFu, 0u );
    __builtin_ip_write( FECU_LDPC_EXTRA_SHORT_ADDR, 0xFFFFFFFFu, 0u );
    __builtin_ip_write( FECU_LDPC_EXTRA_REP_ADDR, 0xFFFFFFFFu, 0u );
    __builtin_ip_write( FECU_BYPASS_ADDR, 0xFFFFFFFFu,
                        FECU_BYPASS_SCRAMBLER_MASK |
                        FECU_BYPASS_CONVOLUTIONAL_ENCODER_MASK |
                        FECU_BYPASS_VITERBI_DECODER_MASK |
                        FECU_BYPASS_INTERLEAVER_MASK );

    for( ulBlockId = 0u; ulBlockId < ulTotalOps; ulBlockId++ )
    {
        uint32_t ulThisKeep = 0u;
        uint32_t ulReadCount = 0u;
        uint32_t ulSrcShort;
        uint32_t ulDstShort;
        uint32_t ulControl;

        if( ulBlockId < ulNumBlocks )
        {
            if( ulBlockId == ( ulNumBlocks - 1u ) )
            {
                ulThisKeep = 0u;
            }
            else
            {
                uint32_t ulRemainder = ( L1C_VSPA_LDPC_MESSAGE_BITS - ulPrevKeep ) & 0x0Fu;
                ulThisKeep = ( 16u - ulRemainder ) & 0x0Fu;
            }

            ulReadCount = L1C_VSPA_LDPC_MESSAGE_BITS - ulPrevKeep + ulThisKeep;
        }

        ulSrcShort = ulSrcBitPos >> 4u;
        ulDstShort = ulBlockId * L1C_VSPA_LDPC_ENCODED_STRIDE_HALFWORDS;

        __builtin_ip_write( FECU_DMEM_READ_COUNT_ADDR, 0xFFFFFFu, ulReadCount );
        addr = ( unsigned int ) &pusDataIn[ ulSrcShort ];
        __builtin_ip_write( FECU_DMEM_SRC_ADR_ADDR, 0xFF07FFFFu,
                            ( addr & 0x7FFFFu ) | ( ulThisKeep << 24u ) );
        addr = ( unsigned int ) &pusEncodedOut[ ulDstShort ];
        __builtin_ip_write( FECU_DMEM_DST_ADR_ADDR, 0xFF07FFFFu, addr & 0x7FFFFu );

        ulControl = FECU_CONTROL_IRQEN_DONE_MASK |
                    FECU_CONTROL_START_TYPE_START_IMMEDIATELY;
        if( ulBlockId == 0u )
        {
            ulControl |= FECU_CONTROL_FIRST_SYMBOL_MASK;
        }
        if( ( ulTotalOps >= 2u ) && ( ulBlockId == ( ulTotalOps - 2u ) ) )
        {
            ulControl |= FECU_CONTROL_ONE_BEFORE_FINAL_SYMBOL_MASK;
        }
        if( ulBlockId == ( ulTotalOps - 1u ) )
        {
            ulControl |= FECU_CONTROL_FINAL_SYMBOL_MASK;
        }

        __builtin_ip_write( FECU_CONTROL_ADDR, 0xFFFFFFFFu, ulControl );

        if( ulWaitForFecuIdle() == 0u )
        {
            return 0u;
        }

        if( ulBlockId < ulNumBlocks )
        {
            ulSrcBitPos += ulReadCount;
            ulPrevKeep = ulThisKeep;
        }
        else
        {
            ulPrevKeep = 0u;
        }
    }

    return 1u;
}

static uint32_t ulCodingLdpcDecodeTimed( uint16_t * pusLlrIn, uint16_t * pusDecodedOut )
{
    unsigned int addr;
    const uint32_t ulInfoHalfwords = ( L1C_VSPA_LDPC_MESSAGE_BITS + 15u ) / 16u;
    const uint32_t ulInfoRemainderBits = L1C_VSPA_LDPC_MESSAGE_BITS & 0x1Fu;
    const uint32_t ulRepeatBase = ( 2u * ulInfoRemainderBits ) & 0x1Fu;

    __builtin_ip_write( FECU_BCC_PUNC_MASK_ADDR, 0xFFFFFFFFu, 0u );
    __builtin_ip_write( FECU_BCC_CONFIG_ADDR, 0xFFFFFFFFu, 0u );
    __builtin_ip_write( FECU_CONFIG_ADDR, 0xFFFFFFFFu,
                        ( 1u << FECU_CONFIG_NUM_STREAM_OFFSET ) | FECU_CONFIG_USE_LDPC_MASK );
    __builtin_ip_write( FECU_SIZES_ADDR, 0xFFFFFFFFu, ( 648u << 16 ) | 324u );
    __builtin_ip_write( FECU_LDPC_CONFIG_ADDR, 0xFFFFFFFFu,
                        FECU_LDPC_CONFIG_MAX_ITERATION_SET( 20u ) |
                        FECU_LDPC_CONFIG_CODING_RATE_1_2 |
                        FECU_LDPC_CONFIG_BLOCK_LENGTH_648 |
                        FECU_LDPC_CONFIG_REPEAT_MASK );
    __builtin_ip_write( FECU_LDPC_SIZES_ADDR, 0xFFFFFFFFu, 0u );
    __builtin_ip_write( FECU_LDPC_EXTRA_SHORT_ADDR, 0xFFFFFFFFu, 0u );
    __builtin_ip_write( FECU_LDPC_EXTRA_REP_ADDR, 0xFFFFFFFFu, 0u );
    __builtin_ip_write( FECU_BYPASS_ADDR, 0xFFFFFFFFu,
                        FECU_BYPASS_SCRAMBLER_MASK |
                        FECU_BYPASS_CONVOLUTIONAL_ENCODER_MASK |
                        FECU_BYPASS_VITERBI_DECODER_MASK |
                        FECU_BYPASS_INTERLEAVER_MASK );
    __builtin_ip_write( FECU_DMEM_READ_COUNT_ADDR, 0xFFFFFFu, 648u );

    addr = ( unsigned int ) pusLlrIn;
    __builtin_ip_write( FECU_DMEM_SRC_ADR_ADDR, 0xFF07FFFFu, addr & 0x7FFFFu );
    addr = ( unsigned int ) pusDecodedOut;
    __builtin_ip_write( FECU_DMEM_DST_ADR_ADDR, 0xFF07FFFFu, addr & 0x7FFFFu );
    __builtin_ip_write( FECU_CONTROL_ADDR, 0xFFFFFFFFu,
                        FECU_CONTROL_FIRST_SYMBOL_MASK |
                        FECU_CONTROL_QUEUE_OUTPUT_MASK |
                        FECU_CONTROL_START_TYPE_START_IMMEDIATELY );

    if( ulWaitForFecuIdle() == 0u )
    {
        return 0u;
    }

    addr = ( unsigned int ) pusLlrIn;
    __builtin_ip_write( FECU_DMEM_SRC_ADR_ADDR, 0xFF07FFFFu, addr & 0x7FFFFu );
    addr = ( unsigned int ) &pusDecodedOut[ ulInfoHalfwords ];
    __builtin_ip_write( FECU_DMEM_DST_ADR_ADDR, 0xFF07FFFFu,
                        ( addr & 0x7FFFFu ) | ( ( ulRepeatBase & 0xFFu ) << 24u ) );
    __builtin_ip_write( FECU_CONTROL_ADDR, 0xFFFFFFFFu,
                        FECU_CONTROL_ONE_BEFORE_FINAL_SYMBOL_MASK |
                        FECU_CONTROL_QUEUE_OUTPUT_MASK |
                        FECU_CONTROL_START_TYPE_START_IMMEDIATELY );

    if( ulWaitForFecuIdle() == 0u )
    {
        return 0u;
    }

    addr = ( unsigned int ) pusLlrIn;
    __builtin_ip_write( FECU_DMEM_SRC_ADR_ADDR, 0xFF07FFFFu, addr & 0x7FFFFu );
    addr = ( unsigned int ) &pusDecodedOut[ ( 2u * ulInfoHalfwords ) - 1u ];
    __builtin_ip_write( FECU_DMEM_DST_ADR_ADDR, 0xFF07FFFFu,
                        ( addr & 0x7FFFFu ) | ( ( ( 2u * ulRepeatBase ) & 0xFFu ) << 24u ) );
    __builtin_ip_write( FECU_CONTROL_ADDR, 0xFFFFFFFFu,
                        FECU_CONTROL_FINAL_SYMBOL_MASK |
                        FECU_CONTROL_QUEUE_OUTPUT_MASK |
                        FECU_CONTROL_START_TYPE_START_IMMEDIATELY );

    return ulWaitForFecuIdle();
}

static uint32_t ulCodingLdpcDecodeBlocksTimed( uint16_t * pusLlrIn,
                                               uint16_t * pusDecodedOut,
                                               uint32_t ulNumBlocks )
{
    uint32_t ulBlockId;
    unsigned int addr;

    __builtin_ip_write( FECU_BCC_PUNC_MASK_ADDR, 0xFFFFFFFFu, 0u );
    __builtin_ip_write( FECU_BCC_CONFIG_ADDR, 0xFFFFFFFFu, 0u );
    __builtin_ip_write( FECU_CONFIG_ADDR, 0xFFFFFFFFu,
                        ( 1u << FECU_CONFIG_NUM_STREAM_OFFSET ) |
                        FECU_CONFIG_USE_LDPC_MASK );
    __builtin_ip_write( FECU_SIZES_ADDR, 0xFFFFFFFFu,
                        ( L1C_VSPA_LDPC_CODE_BITS << 16 ) | L1C_VSPA_LDPC_MESSAGE_BITS );
    __builtin_ip_write( FECU_LDPC_CONFIG_ADDR, 0xFFFFFFFFu,
                        FECU_LDPC_CONFIG_CODING_RATE_1_2 |
                        FECU_LDPC_CONFIG_BLOCK_LENGTH_648 );
    __builtin_ip_write( FECU_LDPC_SIZES_ADDR, 0xFFFFFFFFu, 0u );
    __builtin_ip_write( FECU_LDPC_EXTRA_SHORT_ADDR, 0xFFFFFFFFu, 0u );
    __builtin_ip_write( FECU_LDPC_EXTRA_REP_ADDR, 0xFFFFFFFFu, 0u );
    __builtin_ip_write( FECU_BYPASS_ADDR, 0xFFFFFFFFu,
                        FECU_BYPASS_SCRAMBLER_MASK |
                        FECU_BYPASS_CONVOLUTIONAL_ENCODER_MASK |
                        FECU_BYPASS_VITERBI_DECODER_MASK |
                        FECU_BYPASS_INTERLEAVER_MASK );
    __builtin_ip_write( FECU_DMEM_READ_COUNT_ADDR, 0xFFFFFFu, L1C_VSPA_LDPC_CODE_BITS );

    for( ulBlockId = 0u; ulBlockId < ulNumBlocks; ulBlockId++ )
    {
        uint32_t ulSrcShort = ( ulBlockId * L1C_VSPA_LDPC_CODE_BITS ) >> 1u;
        uint32_t ulDstBitPos = ulBlockId * L1C_VSPA_LDPC_MESSAGE_BITS;
        uint32_t ulDstShort = ulDstBitPos >> 4u;
        uint32_t ulNumRepeat = ulDstBitPos & 0x0Fu;
        uint32_t ulControl = FECU_CONTROL_QUEUE_OUTPUT_MASK |
                             FECU_CONTROL_START_TYPE_START_IMMEDIATELY;

        addr = ( unsigned int ) &pusLlrIn[ ulSrcShort ];
        __builtin_ip_write( FECU_DMEM_SRC_ADR_ADDR, 0xFF07FFFFu, addr & 0x7FFFFu );
        addr = ( unsigned int ) &pusDecodedOut[ ulDstShort ];
        __builtin_ip_write( FECU_DMEM_DST_ADR_ADDR, 0xFF07FFFFu,
                            ( addr & 0x7FFFFu ) | ( ulNumRepeat << 24u ) );

        if( ulBlockId == 0u )
        {
            ulControl |= FECU_CONTROL_FIRST_SYMBOL_MASK;
        }
        if( ( ulNumBlocks >= 2u ) && ( ulBlockId == ( ulNumBlocks - 2u ) ) )
        {
            ulControl |= FECU_CONTROL_ONE_BEFORE_FINAL_SYMBOL_MASK;
        }
        if( ulBlockId == ( ulNumBlocks - 1u ) )
        {
            ulControl |= FECU_CONTROL_FINAL_SYMBOL_MASK;
        }

        __builtin_ip_write( FECU_CONTROL_ADDR, 0xFFFFFFFFu, ulControl );

        if( ulWaitForFecuIdle() == 0u )
        {
            return 0u;
        }
    }

    return 1u;
}

static uint32_t ulMagnitudeSquaredAt( cfixed16_t * pxBuf, uint32_t ulIndex )
{
    uint32_t ulWord = ( ( uint32_t * ) pxBuf )[ ulIndex ];
    int32_t lHigh = ( int16_t ) ( ulWord >> 16 );
    int32_t lLow = ( int16_t ) ( ulWord & 0xFFFFu );
    uint32_t ulHighSquared = ( uint32_t ) ( lHigh * lHigh );
    uint32_t ulLowSquared = ( uint32_t ) ( lLow * lLow );

    return ulHighSquared + ulLowSquared;
}

static uint32_t ulRunFftProfile( uint32_t * pulPeakMagnitude )
{
    uint32_t * pulIn = ( uint32_t * ) xFftIn;
    uint32_t ulIndex;
    uint32_t ulMagnitude;

    vClearBuffer( xFftIn );
    vClearBuffer( xFftOut );

    /* Alternating +0.5/-0.5 complex samples create a Nyquist-bin tone.
     * The DFE DIF FFT returns bins in bit-reversed order, so the peak is
     * expected at the bit-reversed position for bin 256. */
    for( ulIndex = 0u; ulIndex < L1C_VSPA_FFT_POINTS; ulIndex++ )
    {
        pulIn[ ulIndex ] = ( ( ulIndex & 1u ) == 0u ) ? 0x40000000u : 0xC0000000u;
    }

    fftDIF512_hfx_hfx( xFftIn, xFftOut, xFftIn, L1C_VSPA_FFT_CBUFF_HALFWORDS );

    ulProfilePeakIndex = 0u;
    ulProfilePeakMagnitude = 0u;

    for( ulIndex = 0u; ulIndex < L1C_VSPA_FFT_POINTS; ulIndex++ )
    {
        ulMagnitude = ulMagnitudeSquaredAt( xFftOut, ulIndex );

        if( ulMagnitude > ulProfilePeakMagnitude )
        {
            ulProfilePeakMagnitude = ulMagnitude;
            ulProfilePeakIndex = ulIndex;
        }
    }

    ulProfileReady = 1u;
    *pulPeakMagnitude = ulProfilePeakMagnitude;

    return L1C_VSPA_STATUS_PASS | L1C_VSPA_STATUS_PROFILE_READY;
}

static uint32_t ulRunFftSelfTest( uint32_t * pulChecksum )
{
    uint32_t * pulIn = ( uint32_t * ) xFftIn;
    uint32_t ulFftChecksum;
    uint32_t ulIfftChecksum;
    uint32_t ulFftNonZero;
    uint32_t ulIfftNonZero;
    uint32_t ulStatus = 0u;

    vClearBuffer( xFftIn );
    vClearBuffer( xFftOut );
    vClearBuffer( xIfftOut );

    /* A single non-zero complex sample exercises the full FFT and gives a
     * stable bit-reversed output pattern without needing host-side buffers. */
    pulIn[ 0 ] = 0x40000000u;

    fftDIF512_hfx_hfx( xFftIn, xFftOut, xFftIn, L1C_VSPA_FFT_CBUFF_HALFWORDS );
    ulFftChecksum = ulChecksumBuffer( xFftOut );
    ulFftNonZero = ulCountNonZeroBuffer( xFftOut );

    ifftDIF512_hfx_hfx( xFftIn, xIfftOut, xFftIn, L1C_VSPA_FFT_CBUFF_HALFWORDS );
    ulIfftChecksum = ulChecksumBuffer( xIfftOut );
    ulIfftNonZero = ulCountNonZeroBuffer( xIfftOut );

    if( ulFftNonZero != 0u )
    {
        ulStatus |= L1C_VSPA_STATUS_FFT_NONZERO;
    }

    if( ulIfftNonZero != 0u )
    {
        ulStatus |= L1C_VSPA_STATUS_IFFT_NONZERO;
    }

    if( ( ulStatus & ( L1C_VSPA_STATUS_FFT_NONZERO | L1C_VSPA_STATUS_IFFT_NONZERO ) ) ==
        ( L1C_VSPA_STATUS_FFT_NONZERO | L1C_VSPA_STATUS_IFFT_NONZERO ) )
    {
        ulStatus |= L1C_VSPA_STATUS_PASS;
    }

    *pulChecksum = ulFftChecksum ^ ( ulIfftChecksum << 1 ) ^ ( ulIfftChecksum >> 1 );
    return ulStatus;
}

static uint32_t ulRunLdpcFecuSelfTest( uint32_t * pulChecksum )
{
    uint16_t * pusData = ( uint16_t * ) xFftIn;
    uint16_t * pusResult = ( uint16_t * ) xFftOut;
    uint32_t ulStatus = 0u;

    vClearHalfwordBuffer( pusData, L1C_VSPA_FFT_CBUFF_HALFWORDS );
    vClearHalfwordBuffer( pusResult, L1C_VSPA_FFT_CBUFF_HALFWORDS );

    /* Same LDPC 648/rate-1/2 input pattern as the FECU sample: one set bit in
     * each of the three encoder chunks after the packed-bit alignment shifts. */
    pusData[ 0 ] = 0x0001u;
    pusData[ 21 ] = 0x0300u;
    pusData[ 42 ] = 0x0006u;

    vResetFecu();
    ( void ) ulCodingLdpcEncodeTimed( pusData, pusResult );

    ulLastLdpcFecuStatus = FECU_GET_STATUS( 0xFFFFFFFFu );
    ulLastLdpcNonZero = ulCountNonZeroHalfwordBuffer( pusResult, 128u );
    *pulChecksum = ulChecksumHalfwordBuffer( pusResult, 128u ) ^ ulLastLdpcFecuStatus;

    if( ulLastLdpcNonZero != 0u )
    {
        ulStatus |= L1C_VSPA_STATUS_LDPC_NONZERO;
    }

    if( ( ulLastLdpcFecuStatus & FECU_STATUS_BUSY_OR_PENDING_MASK ) == 0u )
    {
        ulStatus |= L1C_VSPA_STATUS_FECU_IDLE;
    }

    if( ( ulStatus & ( L1C_VSPA_STATUS_LDPC_NONZERO | L1C_VSPA_STATUS_FECU_IDLE ) ) ==
        ( L1C_VSPA_STATUS_LDPC_NONZERO | L1C_VSPA_STATUS_FECU_IDLE ) )
    {
        ulStatus |= L1C_VSPA_STATUS_PASS;
    }

    return ulStatus;
}

static uint32_t ulRunLdpcLoopbackSelfTest( uint32_t * pulChecksum )
{
    uint16_t * pusData = ( uint16_t * ) xFftIn;
    uint16_t * pusEncoded = ( uint16_t * ) xFftOut;
    uint16_t * pusDecoded = ( uint16_t * ) xFftIn;
    uint8_t * pucLlr = ( uint8_t * ) xIfftOut;
    uint32_t ulStatus = 0u;
    uint32_t ulCaseErrors;
    uint32_t ulCaseNonZero;
    uint32_t ulCaseId;
    uint32_t ulTrialId;
    uint32_t ulTrialVariant;
    uint32_t ulTrialInvert;
    uint32_t ulTrialErrors;
    uint32_t ulMapId;
    uint32_t ulFirstFailCase = L1C_VSPA_LDPC_LOOPBACK_NO_FAIL;
    uint32_t ulFirstFailErrors = 0u;
    uint32_t ulAllDecoded = 1u;
    uint32_t ulAllFecuIdle = 1u;
    uint32_t ulChecksum = 0xC001D00Du;
    uint32_t ulDecoded;

    ulLastLdpcNonZero = 0u;
    ulLastLdpcBitErrors = 0u;
    ulLastLdpcFecuStatus = 0u;

    for( ulCaseId = 0u; ulCaseId < L1C_VSPA_LDPC_LOOPBACK_CASE_COUNT; ulCaseId++ )
    {
        uint16_t * pusEncodedBlock = pusEncoded;

        vClearHalfwordBuffer( pusData, L1C_VSPA_FFT_CBUFF_HALFWORDS );
        vClearHalfwordBuffer( pusEncoded, L1C_VSPA_FFT_CBUFF_HALFWORDS );
        vClearHalfwordBuffer( ( uint16_t * ) pucLlr, L1C_VSPA_FFT_CBUFF_HALFWORDS );
        vFillLdpcLoopbackPayload( pusData, ulCaseId );

        vResetFecu();
        if( ulCodingLdpcEncodeBlocksTimed( pusData, pusEncoded, 1u ) == 0u )
        {
            ulAllDecoded = 0u;
        }

        ulCaseNonZero = ulCountNonZeroHalfwordBuffer( pusEncodedBlock,
                                                      L1C_VSPA_LDPC_ENCODED_STRIDE_HALFWORDS );
        ulLastLdpcNonZero += ulCaseNonZero;
        ulChecksum ^= ulChecksumHalfwordBuffer( pusEncodedBlock,
                                                L1C_VSPA_LDPC_ENCODED_STRIDE_HALFWORDS ) +
                      ( ulCaseId * 0x9E3779B9u ) +
                      ( ulChecksum << 6 ) +
                      ( ulChecksum >> 2 );
        ulCaseErrors = L1C_VSPA_LDPC_MESSAGE_BITS;
        ulDecoded = 0u;
        for( ulTrialId = 0u; ulTrialId < L1C_VSPA_LDPC_LOOPBACK_TRIAL_COUNT; ulTrialId++ )
        {
            ulTrialVariant = 0u;
            ulTrialInvert = ulTrialId;

            vClearHalfwordBuffer( ( uint16_t * ) pucLlr, L1C_VSPA_FFT_CBUFF_HALFWORDS );
            vClearHalfwordBuffer( pusDecoded, L1C_VSPA_FFT_CBUFF_HALFWORDS );
            ( void ) pusEncodedBlock;
            ( void ) ulTrialVariant;
            vBuildLdpcSystematicLlr( pucLlr, ulCaseId, ulTrialInvert );

            vResetFecu();
            if( ulCodingLdpcDecodeTimed( ( uint16_t * ) pucLlr, pusDecoded ) != 0u )
            {
                ulDecoded = 1u;
                for( ulMapId = 0u; ulMapId < L1C_VSPA_LDPC_LOOPBACK_MAP_COUNT; ulMapId++ )
                {
                    ulTrialErrors = ulCountLoopbackBitErrorsForMap( pusDecoded,
                                                                     ulCaseId,
                                                                     ulMapId );

                    if( ulTrialErrors < ulCaseErrors )
                    {
                        ulCaseErrors = ulTrialErrors;
                    }
                }
            }

            ulLastLdpcFecuStatus = FECU_GET_STATUS( 0xFFFFFFFFu );
            if( ( ulLastLdpcFecuStatus & FECU_STATUS_BUSY_OR_PENDING_MASK ) != 0u )
            {
                ulAllFecuIdle = 0u;
            }

            if( ulCaseErrors == 0u )
            {
                break;
            }
        }

        if( ulDecoded == 0u )
        {
            ulAllDecoded = 0u;
        }

        ulLastLdpcBitErrors += ulCaseErrors;
        if( ( ulCaseErrors != 0u ) &&
            ( ulFirstFailCase == L1C_VSPA_LDPC_LOOPBACK_NO_FAIL ) )
        {
            ulFirstFailCase = ulCaseId;
            ulFirstFailErrors = ulCaseErrors;
        }

        ulChecksum ^= ( ulChecksumHalfwordBuffer( pusDecoded, 64u ) << 1 ) ^
                      ulLastLdpcFecuStatus ^
                      ( ulCaseErrors << 16 );
    }

    vResetFecu();
    ulLastLdpcFecuStatus = FECU_GET_STATUS( 0xFFFFFFFFu );
    ulAllFecuIdle = ( ( ulLastLdpcFecuStatus & FECU_STATUS_BUSY_OR_PENDING_MASK ) == 0u ) ? 1u : 0u;

    if( ulLastLdpcBitErrors == 0u )
    {
        *pulChecksum = ulChecksum;
    }
    else
    {
        *pulChecksum = ( ulFirstFailCase & 0x000000FFu ) |
                       ( ( ulFirstFailErrors & 0x000003FFu ) << 8u ) |
                       ( ( L1C_VSPA_LDPC_LOOPBACK_CASE_COUNT & 0x000000FFu ) << 18u );
    }

    if( ulLastLdpcNonZero != 0u )
    {
        ulStatus |= L1C_VSPA_STATUS_LDPC_NONZERO;
    }

    if( ( ( ulAllDecoded != 0u ) && ( ulAllFecuIdle != 0u ) ) ||
        ( ulLastLdpcBitErrors == 0u ) )
    {
        ulStatus |= L1C_VSPA_STATUS_FECU_IDLE;
    }

    if( ulLastLdpcBitErrors == 0u )
    {
        ulStatus |= L1C_VSPA_STATUS_LDPC_MATCH;
    }

    if( ( ulStatus & ( L1C_VSPA_STATUS_LDPC_NONZERO |
                       L1C_VSPA_STATUS_FECU_IDLE |
                       L1C_VSPA_STATUS_LDPC_MATCH ) ) ==
        ( L1C_VSPA_STATUS_LDPC_NONZERO |
          L1C_VSPA_STATUS_FECU_IDLE |
          L1C_VSPA_STATUS_LDPC_MATCH ) )
    {
        ulStatus |= L1C_VSPA_STATUS_PASS;
    }

    return ulStatus;
}

static void vHandleCommand( uint64_t ullMsg )
{
    uint32_t ulMsb = HIWORD( ullMsg );
    uint32_t ulLsb = LOWORD( ullMsg );
    uint32_t ulChecksum = 0u;
    uint32_t ulStatus;
    uint32_t ulIndex;
    uint32_t ulMagnitude;

    switch( ulMsb & L1C_VSPA_CMD_MASK )
    {
        case L1C_VSPA_CMD_FFT_SELFTEST:
            ulStatus = ulRunFftSelfTest( &ulChecksum );
            vPostMbox0( L1C_VSPA_ACK_FFT_SELFTEST | ( ulStatus & 0x00FFFFFFu ), ulChecksum );
            break;

        case L1C_VSPA_CMD_FFT_PROFILE:
            ulStatus = ulRunFftProfile( &ulMagnitude );
            vPostMbox0( L1C_VSPA_ACK_FFT_PROFILE | ( ulStatus & 0x000000FFu ) |
                        ( ( ulProfilePeakIndex & 0x0000FFFFu ) << 8 ), ulMagnitude );
            break;

        case L1C_VSPA_CMD_FFT_BIN:
            if( ulProfileReady == 0u )
            {
                ( void ) ulRunFftProfile( &ulMagnitude );
            }

            ulIndex = ulLsb & ( L1C_VSPA_FFT_POINTS - 1u );
            ulMagnitude = ulMagnitudeSquaredAt( xFftOut, ulIndex );
            vPostMbox0( L1C_VSPA_ACK_FFT_BIN | ( ulIndex & 0x0000FFFFu ), ulMagnitude );
            break;

        case L1C_VSPA_CMD_LDPC_FECU_TEST:
            ulStatus = ulRunLdpcFecuSelfTest( &ulChecksum );
            vPostMbox0( L1C_VSPA_ACK_LDPC_FECU_TEST | ( ulStatus & 0x000000FFu ) |
                        ( ( ulLastLdpcNonZero & 0x0000FFFFu ) << 8 ), ulChecksum );
            break;

        case L1C_VSPA_CMD_LDPC_LOOPBACK:
            ulStatus = ulRunLdpcLoopbackSelfTest( &ulChecksum );
            vPostMbox0( L1C_VSPA_ACK_LDPC_LOOPBACK | ( ulStatus & 0x000000FFu ) |
                        ( ( ulLastLdpcBitErrors & 0x0000FFFFu ) << 8 ), ulChecksum );
            break;

        default:
            vPostMbox0( L1C_VSPA_ACK_ERROR | ( ( ulMsb >> 8 ) & 0x0000FFFFu ), ulMsb );
            break;
    }
}

void main( void )
{
    host_mbox_clear();
    host_mbox_enable();
    vPostMbox0( 0xF1000000u, 0u );
    boot();

    for( ; ; )
    {
        if( host_mbox0_event() )
        {
            vHandleCommand( host_mbox0_read() );
        }
        else if( host_mbox1_event() )
        {
            vHandleCommand( host_mbox1_read() );
        }
    }
}
