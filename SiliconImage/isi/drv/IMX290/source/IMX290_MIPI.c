#include <ebase/types.h>
#include <ebase/trace.h>
#include <ebase/builtins.h>

#include <common/return_codes.h>
#include <common/misc.h>

#include "isi.h"
#include "isi_iss.h"
#include "isi_priv.h"
#include "IMX290_MIPI_priv.h"

#define  Sensor_NEWEST_TUNING_XML "IMX290_0808"

#define CC_OFFSET_SCALING  2.0f
#define I2C_COMPLIANT_STARTBIT 1U

/******************************************************************************
 * local macro definitions
 *****************************************************************************/
CREATE_TRACER( Sensor_INFO , "IMX290: ", /*INFO*/ERROR,    1U );
CREATE_TRACER( Sensor_WARN , "IMX290: ", WARNING, 1U );
CREATE_TRACER( Sensor_ERROR, "IMX290: ", ERROR,   1U );

CREATE_TRACER( Sensor_DEBUG, "IMX290: ", /*INFO*/ERROR,     1U );

CREATE_TRACER( Sensor_REG_INFO , "IMX290: ", /*INFO*/ERROR, 1);
CREATE_TRACER( Sensor_REG_DEBUG, "IMX290: ", /*INFO*/ERROR, 1U );

#define Sensor_SLAVE_ADDR       0x1aU                           /**< i2c slave address of the IMX290 camera sensor */
#define Sensor_SLAVE_ADDR2      0x34U
#define Sensor_SLAVE_AF_ADDR    0x00U
#define Sensor_OTP_SLAVE_ADDR   0x00U
#define Sensor_OTP_SLAVE_ADDR2   0x00U
#define Sensor_OTP_REG_SIZE     1U
#define Sensor_OTP_VALUE_SIZE     1U

#define Sensor_MIN_GAIN_STEP   ( 0.3f); 
//#define Sensor_MAX_GAIN_AEC    ( 72.0f )
#define Sensor_MAX_GAIN_AEC    ( 10.0f )


/*!<
 * Focus position values:
 * 65 logical positions ( 0 - 64 )
 * where 64 is the setting for infinity and 0 for macro
 * corresponding to
 * 1024 register settings (0 - 1023)
 * where 0 is the setting for infinity and 1023 for macro
 */
#define MAX_LOG   64U
#define MAX_REG 1023U


/*!<
 * Lens movement is triggered every 133ms (VGA, 7.5fps processed frames
 * worst case assumed, usually even much slower, see OV5630 driver for
 * details). Thus the lens has to reach the requested position after
 * max. 133ms. Minimum mechanical ringing is expected with mode 1 ,
 * 100us per step. A movement over the full range needs max. 102.3ms
 * (see table 9 AD5820 datasheet).
 */
#define MDI_SLEW_RATE_CTRL 6U /* S3..0 */



/******************************************************************************
 * local variable declarations
 *****************************************************************************/
const char Sensor_g_acName[] = "IMX290_MIPI";

extern const IsiRegDescription_t Sensor_IMX290_g_aRegDescription_fourlane[];
const IsiSensorCaps_t Sensor_g_IsiSensorDefaultConfig;


#define Sensor_I2C_START_BIT        (I2C_COMPLIANT_STARTBIT)    // I2C bus start condition
#define Sensor_I2C_NR_ADR_BYTES     (2U)                        // 1 byte base address and 2 bytes sub address
#define Sensor_I2C_NR_DAT_BYTES     (1U)                        // 8 bit registers


static uint16_t g_suppoted_mipi_lanenum_type = SUPPORT_MIPI_FOUR_LANE;//SUPPORT_MIPI_ONE_LANE|SUPPORT_MIPI_TWO_LANE|SUPPORT_MIPI_FOUR_LANE;
#define DEFAULT_NUM_LANES SUPPORT_MIPI_FOUR_LANE



/******************************************************************************
 * local function prototypes
 *****************************************************************************/
static RESULT Sensor_IsiCreateSensorIss( IsiSensorInstanceConfig_t *pConfig );
static RESULT Sensor_IsiReleaseSensorIss( IsiSensorHandle_t handle );
static RESULT Sensor_IsiGetCapsIss( IsiSensorHandle_t handle, IsiSensorCaps_t *pIsiSensorCaps );
static RESULT Sensor_IsiSetupSensorIss( IsiSensorHandle_t handle, const IsiSensorConfig_t *pConfig );
static RESULT Sensor_IsiSensorSetStreamingIss( IsiSensorHandle_t handle, bool_t on );
static RESULT Sensor_IsiSensorSetPowerIss( IsiSensorHandle_t handle, bool_t on );
static RESULT Sensor_IsiCheckSensorConnectionIss( IsiSensorHandle_t handle );
static RESULT Sensor_IsiGetSensorRevisionIss( IsiSensorHandle_t handle, uint32_t *p_value);

static RESULT Sensor_IsiGetGainLimitsIss( IsiSensorHandle_t handle, float *pMinGain, float *pMaxGain);
static RESULT Sensor_IsiGetIntegrationTimeLimitsIss( IsiSensorHandle_t handle, float *pMinIntegrationTime, float *pMaxIntegrationTime );
static RESULT Sensor_IsiExposureControlIss( IsiSensorHandle_t handle, float NewGain, float NewIntegrationTime, uint8_t *pNumberOfFramesToSkip, float *pSetGain, float *pSetIntegrationTime );
static RESULT Sensor_IsiGetCurrentExposureIss( IsiSensorHandle_t handle, float *pSetGain, float *pSetIntegrationTime );
static RESULT Sensor_IsiGetAfpsInfoIss ( IsiSensorHandle_t handle, uint32_t Resolution, IsiAfpsInfo_t* pAfpsInfo);
static RESULT Sensor_IsiGetGainIss( IsiSensorHandle_t handle, float *pSetGain );
static RESULT Sensor_IsiGetGainIncrementIss( IsiSensorHandle_t handle, float *pIncr );
static RESULT Sensor_IsiSetGainIss( IsiSensorHandle_t handle, float NewGain, float *pSetGain);
static RESULT Sensor_IsiGetIntegrationTimeIss( IsiSensorHandle_t handle, float *pSetIntegrationTime );
static RESULT Sensor_IsiGetIntegrationTimeIncrementIss( IsiSensorHandle_t handle, float *pIncr );
static RESULT Sensor_IsiSetIntegrationTimeIss( IsiSensorHandle_t handle, float NewIntegrationTime, float *pSetIntegrationTime, uint8_t *pNumberOfFramesToSkip);
static RESULT Sensor_IsiGetResolutionIss( IsiSensorHandle_t handle, uint32_t *pSetResolution );


static RESULT Sensor_IsiRegReadIss( IsiSensorHandle_t handle, const uint32_t address, uint32_t *p_value );
static RESULT Sensor_IsiRegWriteIss( IsiSensorHandle_t handle, const uint32_t address, const uint32_t value );

static RESULT Sensor_IsiGetCalibKFactor( IsiSensorHandle_t handle, Isi1x1FloatMatrix_t **pIsiKFactor );
static RESULT Sensor_IsiGetCalibPcaMatrix( IsiSensorHandle_t   handle, Isi3x2FloatMatrix_t **pIsiPcaMatrix );
static RESULT Sensor_IsiGetCalibSvdMeanValue( IsiSensorHandle_t   handle, Isi3x1FloatMatrix_t **pIsiSvdMeanValue );
static RESULT Sensor_IsiGetCalibCenterLine( IsiSensorHandle_t   handle, IsiLine_t  **ptIsiCenterLine);
static RESULT Sensor_IsiGetCalibClipParam( IsiSensorHandle_t   handle, IsiAwbClipParm_t    **pIsiClipParam );
static RESULT Sensor_IsiGetCalibGlobalFadeParam( IsiSensorHandle_t       handle, IsiAwbGlobalFadeParm_t  **ptIsiGlobalFadeParam);
static RESULT Sensor_IsiGetCalibFadeParam( IsiSensorHandle_t   handle, IsiAwbFade2Parm_t   **ptIsiFadeParam);
static RESULT Sensor_IsiGetIlluProfile( IsiSensorHandle_t   handle, const uint32_t CieProfile, IsiIlluProfile_t **ptIsiIlluProfile );

static RESULT Sensor_IsiMdiInitMotoDriveMds( IsiSensorHandle_t handle );
static RESULT Sensor_IsiMdiSetupMotoDrive( IsiSensorHandle_t handle, uint32_t *pMaxStep );
static RESULT Sensor_IsiMdiFocusSet( IsiSensorHandle_t handle, const uint32_t Position );
static RESULT Sensor_IsiMdiFocusGet( IsiSensorHandle_t handle, uint32_t *pAbsStep );
static RESULT Sensor_IsiMdiFocusCalibrate( IsiSensorHandle_t handle );

static RESULT Sensor_IsiGetSensorMipiInfoIss( IsiSensorHandle_t handle, IsiSensorMipiInfo *ptIsiSensorMipiInfo);
static RESULT Sensor_IsiGetSensorIsiVersion(  IsiSensorHandle_t   handle, unsigned int* pVersion);
static RESULT Sensor_IsiGetSensorTuningXmlVersion(  IsiSensorHandle_t   handle, char** pTuningXmlVersion);
static RESULT Sensor_IsiSetSensorFrameRateLimit(IsiSensorHandle_t handle, uint32_t minimum_framerate);


static float dctfloor( const float f )
{
    if ( f < 0 )
    {
        return ( (float)((int32_t)f - 1L) );
    }
    else
    {
        return ( (float)((uint32_t)f) );
    }
}



/*****************************************************************************/
/**
 *          Sensor_IsiCreateSensorIss
 *
 * @brief   This function creates a new OV13850 sensor instance handle.
 *
 * @param   pConfig     configuration structure to create the instance
 *
 * @return  Return the result of the function call.
 * @retval  RET_SUCCESS
 * @retval  RET_NULL_POINTER
 * @retval  RET_OUTOFMEM
 *
 *****************************************************************************/
static RESULT Sensor_IsiCreateSensorIss
(
    IsiSensorInstanceConfig_t *pConfig
)
{
    RESULT result = RET_SUCCESS;
    Sensor_Context_t *pSensorCtx;

    TRACE( Sensor_INFO, "%s (enter)\n", __FUNCTION__);

    if ( (pConfig == NULL) || (pConfig->pSensor ==NULL) )
    {
        return ( RET_NULL_POINTER );
    }

    pSensorCtx = ( Sensor_Context_t * )malloc ( sizeof (Sensor_Context_t) );
    if ( pSensorCtx == NULL )
    {
        TRACE( Sensor_ERROR,  "%s: Can't allocate IMX290 context\n",  __FUNCTION__ );
        return ( RET_OUTOFMEM );
    }
    MEMSET( pSensorCtx, 0, sizeof( Sensor_Context_t ) );

    result = HalAddRef( pConfig->HalHandle );
    if ( result != RET_SUCCESS )
    {
        free ( pSensorCtx );
        return ( result );
    }

    pSensorCtx->IsiCtx.HalHandle              = pConfig->HalHandle;
    pSensorCtx->IsiCtx.HalDevID               = pConfig->HalDevID;
    pSensorCtx->IsiCtx.I2cBusNum              = pConfig->I2cBusNum;
    pSensorCtx->IsiCtx.SlaveAddress           = ( pConfig->SlaveAddr == 0 ) ? Sensor_SLAVE_ADDR : pConfig->SlaveAddr;
    pSensorCtx->IsiCtx.NrOfAddressBytes       = 2U;

    pSensorCtx->IsiCtx.I2cAfBusNum            = pConfig->I2cAfBusNum;
    pSensorCtx->IsiCtx.SlaveAfAddress         = ( pConfig->SlaveAfAddr == 0 ) ? Sensor_SLAVE_AF_ADDR : pConfig->SlaveAfAddr;
    pSensorCtx->IsiCtx.NrOfAfAddressBytes     = 1U;

    pSensorCtx->IsiCtx.pSensor                = pConfig->pSensor;

    pSensorCtx->Configured             = BOOL_FALSE;
    pSensorCtx->Streaming              = BOOL_FALSE;
    pSensorCtx->TestPattern            = BOOL_FALSE;
    pSensorCtx->isAfpsRun              = BOOL_FALSE;
    
    pSensorCtx->IsiSensorMipiInfo.sensorHalDevID = pSensorCtx->IsiCtx.HalDevID;
    pSensorCtx->IsiSensorMipiInfo.ucMipiLanes = DEFAULT_NUM_LANES;

    pConfig->hSensor = ( IsiSensorHandle_t )pSensorCtx;

    result = HalSetCamConfig( pSensorCtx->IsiCtx.HalHandle, pSensorCtx->IsiCtx.HalDevID, false, true, false );
    RETURN_RESULT_IF_DIFFERENT( RET_SUCCESS, result );

    result = HalSetClock( pSensorCtx->IsiCtx.HalHandle, pSensorCtx->IsiCtx.HalDevID, 37125000U);
    RETURN_RESULT_IF_DIFFERENT( RET_SUCCESS, result );

    TRACE( Sensor_INFO, "%s (exit)\n", __FUNCTION__);

    return ( result );
}



/*****************************************************************************/
/**
 *          Sensor_IsiReleaseSensorIss
 *
 * @brief   This function destroys/releases an Sensor instance.
 *
 * @param   handle      Sensor instance handle
 *
 * @return  Return the result of the function call.
 * @retval  RET_SUCCESS
 * @retval  RET_WRONG_HANDLE
 *
 *****************************************************************************/
static RESULT Sensor_IsiReleaseSensorIss
(
    IsiSensorHandle_t handle
)
{
    Sensor_Context_t *pSensorCtx = (Sensor_Context_t *)handle;

    RESULT result = RET_SUCCESS;

    TRACE( Sensor_INFO, "%s (enter)\n", __FUNCTION__);

    if ( pSensorCtx == NULL )
    {
        return ( RET_WRONG_HANDLE );
    }

    (void)Sensor_IsiSensorSetStreamingIss( pSensorCtx, BOOL_FALSE );
    (void)Sensor_IsiSensorSetPowerIss( pSensorCtx, BOOL_FALSE );

    (void)HalDelRef( pSensorCtx->IsiCtx.HalHandle );

    MEMSET( pSensorCtx, 0, sizeof( Sensor_Context_t ) );
    free ( pSensorCtx );

    TRACE( Sensor_INFO, "%s (exit)\n", __FUNCTION__);

    return ( result );
}



/*****************************************************************************/
/**
 *          Sensor_IsiGetCapsIss
 *
 * @brief   fills in the correct pointers for the sensor description struct
 *
 * @param   param1      pointer to sensor capabilities structure
 *
 * @return  Return the result of the function call.
 * @retval  RET_SUCCESS
 * @retval  RET_NULL_POINTER
 *
 *****************************************************************************/
static RESULT Sensor_IsiGetCapsIssInternal
(
    IsiSensorCaps_t   *pIsiSensorCaps,
    uint32_t mipi_lanes
)
{
	
    TRACE( Sensor_INFO, "%s (enter)\n", __FUNCTION__);
    RESULT result = RET_SUCCESS;

    if ( pIsiSensorCaps == NULL )
    {
        return ( RET_NULL_POINTER );
    }
    else
    {
		if (pIsiSensorCaps->Index > 0) {
			return RET_OUTOFRANGE;
		}
	
        if(mipi_lanes == SUPPORT_MIPI_FOUR_LANE) {
			switch (pIsiSensorCaps->Index) 
            {
                case 0:
                {
                    pIsiSensorCaps->Resolution = ISI_RES_TV1080P30;
                    break;
                }
                default:
                {
                    result = RET_OUTOFRANGE;
                    goto end;
                }

            }   
    	} else {
			result = RET_OUTOFRANGE;
			goto end;
		}

    
        pIsiSensorCaps->BusWidth        = ISI_BUSWIDTH_10BIT;
        pIsiSensorCaps->Mode            = ISI_MODE_MIPI;
        pIsiSensorCaps->FieldSelection  = ISI_FIELDSEL_BOTH;
        pIsiSensorCaps->YCSequence      = ISI_YCSEQ_YCBYCR;           /**< only Bayer supported, will not be evaluated */
        pIsiSensorCaps->Conv422         = ISI_CONV422_NOCOSITED;
        pIsiSensorCaps->BPat            = ISI_BPAT_BGBGGRGR;
        pIsiSensorCaps->HPol            = ISI_HPOL_REFPOS;
        pIsiSensorCaps->VPol            = ISI_VPOL_POS;
        pIsiSensorCaps->Edge            = ISI_EDGE_RISING;
        pIsiSensorCaps->Bls             = ISI_BLS_OFF;
        pIsiSensorCaps->Gamma           = ISI_GAMMA_OFF;
        pIsiSensorCaps->CConv           = ISI_CCONV_OFF;
        pIsiSensorCaps->BLC             = ( ISI_BLC_AUTO);
        pIsiSensorCaps->AGC             = ( ISI_AGC_OFF );
        pIsiSensorCaps->AWB             = ( ISI_AWB_OFF );
        pIsiSensorCaps->AEC             = ( ISI_AEC_OFF );
        pIsiSensorCaps->DPCC            = ( ISI_DPCC_OFF );

        pIsiSensorCaps->DwnSz           = ISI_DWNSZ_SUBSMPL;
        pIsiSensorCaps->CieProfile      = ( ISI_CIEPROF_A
                                          | ISI_CIEPROF_D50
                                          | ISI_CIEPROF_D65
                                          | ISI_CIEPROF_D75
                                          | ISI_CIEPROF_F2
                                          | ISI_CIEPROF_F11 );
        pIsiSensorCaps->SmiaMode        = ISI_SMIA_OFF;
        pIsiSensorCaps->MipiMode        = ISI_MIPI_MODE_RAW_10;
        pIsiSensorCaps->AfpsResolutions = ( ISI_AFPS_NOTSUPP );
		pIsiSensorCaps->SensorOutputMode = ISI_SENSOR_OUTPUT_MODE_RAW;
    }
end:

    return ( result );
}

static RESULT Sensor_IsiGetCapsIss
(
    IsiSensorHandle_t handle,
    IsiSensorCaps_t   *pIsiSensorCaps
)
{
    Sensor_Context_t *pSensorCtx = (Sensor_Context_t *)handle;

    RESULT result = RET_SUCCESS;

    TRACE( Sensor_INFO, "%s (enter)\n", __FUNCTION__);

    if ( pSensorCtx == NULL )
    {
        return ( RET_WRONG_HANDLE );
    }

    result = Sensor_IsiGetCapsIssInternal(pIsiSensorCaps, pSensorCtx->IsiSensorMipiInfo.ucMipiLanes);
    TRACE( Sensor_INFO, "%s (exit)\n", __FUNCTION__);

    return ( result );
}



/*****************************************************************************/
/**
 *          Sensor_g_IsiSensorDefaultConfig
 *
 * @brief   recommended default configuration for application use via call
 *          to IsiGetSensorIss()
 *
 *****************************************************************************/
const IsiSensorCaps_t Sensor_g_IsiSensorDefaultConfig =
{
    ISI_BUSWIDTH_10BIT,         // BusWidth
    ISI_MODE_MIPI,              // MIPI
    ISI_FIELDSEL_BOTH,          // FieldSel
    ISI_YCSEQ_YCBYCR,           // YCSeq
    ISI_CONV422_NOCOSITED,      // Conv422
    //ISI_BPAT_BGBGGRGR,          // BPat
    //ISI_BPAT_GBGBRGRG,		// BPat
    ISI_BPAT_RGRGGBGB,		//BPat
    ISI_HPOL_REFPOS,            // HPol
    ISI_VPOL_POS,               // VPol
    ISI_EDGE_RISING,            // Edge
    ISI_BLS_OFF,                // Bls
    ISI_GAMMA_OFF,              // Gamma
    ISI_CCONV_OFF,              // CConv
    ISI_RES_TV1080P30,          // Res
    ISI_DWNSZ_SUBSMPL,          // DwnSz
    ISI_BLC_AUTO,               // BLC
    ISI_AGC_OFF,                // AGC
    ISI_AWB_OFF,                // AWB
    ISI_AEC_OFF,                // AEC
    ISI_DPCC_OFF,               // DPCC
    ISI_CIEPROF_D65,            // CieProfile, this is also used as start profile for AWB (if not altered by menu settings)
    ISI_SMIA_OFF,               // SmiaMode
    ISI_MIPI_MODE_RAW_10,       // MipiMode
    ISI_AFPS_NOTSUPP,           // AfpsResolutions
    ISI_SENSOR_OUTPUT_MODE_RAW,
    0,
};



/*****************************************************************************/
/**
 *          Sensor_SetupOutputFormat
 *
 * @brief   Setup of the image sensor considering the given configuration.
 *
 * @param   handle      Sensor instance handle
 * @param   pConfig     pointer to sensor configuration structure
 *
 * @return  Return the result of the function call.
 * @retval  RET_SUCCESS
 * @retval  RET_NULL_POINTER
 *
 *****************************************************************************/
RESULT Sensor_SetupOutputFormat
(
    Sensor_Context_t       *pSensorCtx,
    const IsiSensorConfig_t *pConfig
)
{
    RESULT result = RET_SUCCESS;

     ALOGD("%s(%d): (enter)\n", __FUNCTION__,__LINE__);

    /* bus-width */
    switch ( pConfig->BusWidth )        /* only ISI_BUSWIDTH_12BIT supported, no configuration needed here */
    {
        case ISI_BUSWIDTH_12BIT:
        {
            break;
        }
	 case ISI_BUSWIDTH_10BIT:
        {
	     ALOGD("%s(%d): pConfig->BusWidth is ISI_BUSWIDTH_10BIT\n", __FUNCTION__,__LINE__);	
            break;
        }
	
        default:
        {
            TRACE( Sensor_ERROR, "%s%s: bus width not supported\n", __FUNCTION__, pSensorCtx->isAfpsRun?"(AFPS)":"" );
            return ( RET_NOTSUPP );
        }
    }

    /* mode */
	ALOGD("%s(%d): pConfig->Mode value is (0x%08x)\n", __FUNCTION__,__LINE__,pConfig->Mode);
    switch ( pConfig->Mode )            /* only ISI_MODE_BAYER supported, no configuration needed here */
    {
        case( ISI_MODE_MIPI ):
        {
            break;
        }

        default:
        {
            TRACE( Sensor_ERROR, "%s%s: mode not supported\n", __FUNCTION__, pSensorCtx->isAfpsRun?"(AFPS)":"" );
            return ( RET_NOTSUPP );
        }
    }

    /* field-selection */
    switch ( pConfig->FieldSelection )  /* only ISI_FIELDSEL_BOTH supported, no configuration needed */
    {
        case ISI_FIELDSEL_BOTH:
        {
            break;
        }

        default:
        {
            TRACE( Sensor_ERROR, "%s%s: field selection not supported\n", __FUNCTION__, pSensorCtx->isAfpsRun?"(AFPS)":"" );
            return ( RET_NOTSUPP );
        }
    }

    /* only Bayer mode is supported by Sensor, so the YCSequence parameter is not checked */
    switch ( pConfig->YCSequence )
    {
        default:
        {
            break;
        }
    }

    /* 422 conversion */
    switch ( pConfig->Conv422 )         /* only ISI_CONV422_NOCOSITED supported, no configuration needed */
    {
        case ISI_CONV422_NOCOSITED:
        {
            break;
        }

        default:
        {
            TRACE( Sensor_ERROR, "%s%s: 422 conversion not supported\n", __FUNCTION__, pSensorCtx->isAfpsRun?"(AFPS)":"" );
            return ( RET_NOTSUPP );
        }
    }

    /* bayer-pattern */
    switch ( pConfig->BPat )            /* only ISI_BPAT_BGBGGRGR supported, no configuration needed */
    {
        case ISI_BPAT_BGBGGRGR:
        {
            break;
        }
	case ISI_BPAT_RGRGGBGB:	
        {
	    ALOGD("%s(%d): pConfig->BPat is ISI_BPAT_RGRGGBGB\n", __FUNCTION__,__LINE__);
            break;
        }
	case ISI_BPAT_GBGBRGRG:	
        {
	    ALOGD("%s(%d): pConfig->BPat is ISI_BPAT_GBGBRGRG\n", __FUNCTION__,__LINE__);
            break;
        }
        default:
        {
            TRACE( Sensor_ERROR, "%s%s: bayer pattern not supported\n", __FUNCTION__, pSensorCtx->isAfpsRun?"(AFPS)":"" );
            return ( RET_NOTSUPP );
        }
    }

    /* horizontal polarity */
    switch ( pConfig->HPol )            /* only ISI_HPOL_REFPOS supported, no configuration needed */
    {
        case ISI_HPOL_REFPOS:
        {
            break;
        }

        default:
        {
            TRACE( Sensor_ERROR, "%s%s: HPol not supported\n", __FUNCTION__, pSensorCtx->isAfpsRun?"(AFPS)":"" );
            return ( RET_NOTSUPP );
        }
    }

    /* vertical polarity */
    switch ( pConfig->VPol )            /*no configuration needed */
    {
        case ISI_VPOL_NEG:
        {
            break;
        }
        case ISI_VPOL_POS:
        {
            break;
        }

        default:
        {
            TRACE( Sensor_ERROR, "%s%s: VPol not supported\n", __FUNCTION__, pSensorCtx->isAfpsRun?"(AFPS)":"" );
            return ( RET_NOTSUPP );
        }
    }


    /* edge */
    switch ( pConfig->Edge )            /* only ISI_EDGE_RISING supported, no configuration needed */
    {
        case ISI_EDGE_RISING:
        {
            break;
        }

        case ISI_EDGE_FALLING:          /*TODO for MIPI debug*/
        {
            break;
        }

        default:
        {
            TRACE( Sensor_ERROR, "%s%s:  edge mode not supported\n", __FUNCTION__, pSensorCtx->isAfpsRun?"(AFPS)":"" );
            return ( RET_NOTSUPP );
        }
    }

    /* gamma */
    switch ( pConfig->Gamma )           /* only ISI_GAMMA_OFF supported, no configuration needed */
    {
        case ISI_GAMMA_OFF:
        {
            break;
        }

        default:
        {
            TRACE( Sensor_ERROR, "%s%s:  gamma not supported\n", __FUNCTION__, pSensorCtx->isAfpsRun?"(AFPS)":"" );
            return ( RET_NOTSUPP );
        }
    }

    /* color conversion */
    switch ( pConfig->CConv )           /* only ISI_CCONV_OFF supported, no configuration needed */
    {
        case ISI_CCONV_OFF:
        {
            break;
        }

        default:
        {
            TRACE( Sensor_ERROR, "%s%s: color conversion not supported\n", __FUNCTION__, pSensorCtx->isAfpsRun?"(AFPS)":"" );
            return ( RET_NOTSUPP );
        }
    }

    switch ( pConfig->SmiaMode )        /* only ISI_SMIA_OFF supported, no configuration needed */
    {
        case ISI_SMIA_OFF:
        {
            break;
        }

        default:
        {
            TRACE( Sensor_ERROR, "%s%s: SMIA mode not supported\n", __FUNCTION__, pSensorCtx->isAfpsRun?"(AFPS)":"" );
            return ( RET_NOTSUPP );
        }
    }

    switch ( pConfig->MipiMode )        /* only ISI_MIPI_MODE_RAW_12 supported, no configuration needed */
    {
        case ISI_MIPI_MODE_RAW_12:
        {
            break;
        }
	 case ISI_MIPI_MODE_RAW_10:
        {
            break;
        }

        default:
        {
            TRACE( Sensor_ERROR, "%s%s: MIPI mode not supported\n", __FUNCTION__, pSensorCtx->isAfpsRun?"(AFPS)":"" );
            return ( RET_NOTSUPP );
        }
    }

    switch ( pConfig->AfpsResolutions ) /* no configuration needed */
    {
        case ISI_AFPS_NOTSUPP:
        {
            break;
        }
        default:
        {
            // don't care about what comes in here
            //TRACE( Sensor_ERROR, "%s%s: AFPS not supported\n", __FUNCTION__, pSensorCtx->isAfpsRun?"(AFPS)":"" );
            //return ( RET_NOTSUPP );
        }
    }

    TRACE( Sensor_INFO, "%s%s (exit)\n", __FUNCTION__, pSensorCtx->isAfpsRun?"(AFPS)":"");

    return ( result );
}

/*****************************************************************************/
/**
 *          Sensor_SetupOutputWindow
 *
 * @brief   Setup of the image sensor considering the given configuration.
 *
 * @param   handle      Sensor instance handle
 * @param   pConfig     pointer to sensor configuration structure
 *
 * @return  Return the result of the function call.
 * @retval  RET_SUCCESS
 * @retval  RET_NULL_POINTER
 *
 *****************************************************************************/
static RESULT Sensor_SetupOutputWindowInternal
(
    Sensor_Context_t        *pSensorCtx,
    const IsiSensorConfig_t *pConfig,
    bool_t set2Sensor,
    bool_t res_no_chg 
)
{
    RESULT result     = RET_SUCCESS;
    uint16_t usFrameLengthLines = 0;
    uint16_t usLineLengthPck    = 0;
    float    rVtPixClkFreq      = 0.0f;
    int xclk = 37125000;

    TRACE( Sensor_ERROR, "%s (enter)---pConfig->Resolution:%x\n", __FUNCTION__,pConfig->Resolution);
    
    if(pSensorCtx->IsiSensorMipiInfo.ucMipiLanes == SUPPORT_MIPI_FOUR_LANE) {
        switch ( pConfig->Resolution )
        {
            case ISI_RES_TV1080P30:
	    {
		 ALOGD("%s: ISI_RES_TV1080P30\n", __FUNCTION__);
		if (set2Sensor == BOOL_TRUE) {
	            if (res_no_chg == BOOL_FALSE) {
			if ((result = IsiRegDefaultsApply((IsiSensorHandle_t)pSensorCtx, Sensor_IMX290_g_aRegDescription_fourlane)) != RET_SUCCESS) {
			    result = RET_FAILURE;
			    TRACE( Sensor_ERROR, "%s: failed to set Sensor_g_aRegDescription_fourlane \n", __FUNCTION__ );
			}
		    }
		}
				
	        usLineLengthPck = 0x79c;
		usFrameLengthLines = 0x0449;
				
    		osSleep( 10 );
		pSensorCtx->IsiSensorMipiInfo.ulMipiFreq = 223;
                break;
            }

            default:
	    {
	        TRACE( Sensor_ERROR, "%s: four lane Resolution not supported\n", __FUNCTION__ );
	        return ( RET_NOTSUPP );
	    }
	}
    }
    
    // store frame timing for later use in AEC module  
    rVtPixClkFreq = xclk*2;
    pSensorCtx->LineLengthPck    = usLineLengthPck;
    pSensorCtx->FrameLengthLines = usFrameLengthLines;
    pSensorCtx->VtPixClkFreq     = rVtPixClkFreq;
   

ALOGD( "%s  (exit): Resolution %dx%d@%dfps  MIPI %dlanes  res_no_chg: %d   rVtPixClkFreq: %f, pSensorCtx->VtPixClkFreq:%f\n", __FUNCTION__,
                        ISI_RES_W_GET(pConfig->Resolution),ISI_RES_H_GET(pConfig->Resolution),
                        ISI_FPS_GET(pConfig->Resolution),
                        pSensorCtx->IsiSensorMipiInfo.ucMipiLanes,
                        res_no_chg,rVtPixClkFreq, pSensorCtx->VtPixClkFreq);
    return ( result );
}




/*****************************************************************************/
/**
 *          Sensor_SetupImageControl
 *
 * @brief   Sets the image control functions (BLC, AGC, AWB, AEC, DPCC ...)
 *
 * @param   handle      Sensor instance handle
 * @param   pConfig     pointer to sensor configuration structure
 *
 * @return  Return the result of the function call.
 * @retval  RET_SUCCESS
 * @retval  RET_NULL_POINTER
 *
 *****************************************************************************/
RESULT Sensor_SetupImageControl
(
    Sensor_Context_t        *pSensorCtx,
    const IsiSensorConfig_t *pConfig
)
{
    RESULT result = RET_SUCCESS;


    TRACE( Sensor_INFO, "%s (enter)\n", __FUNCTION__);

    switch ( pConfig->Bls )      /* only ISI_BLS_OFF supported, no configuration needed */
    {
        case ISI_BLS_OFF:
        {
            break;
        }

        default:
        {
            TRACE( Sensor_ERROR, "%s: Black level not supported\n", __FUNCTION__ );
            return ( RET_NOTSUPP );
        }
    }

    /* black level compensation */
    switch ( pConfig->BLC )
    {
        case ISI_BLC_OFF:
        {
            /* turn off black level correction (clear bit 0) */
            //result = Sensor_IsiRegReadIss(  pSensorCtx, Sensor_BLC_CTRL00, &RegValue );
            //result = Sensor_IsiRegWriteIss( pSensorCtx, Sensor_BLC_CTRL00, RegValue & 0x7F);
            break;
        }

        case ISI_BLC_AUTO:
        {
            /* turn on black level correction (set bit 0)
             * (0x331E[7] is assumed to be already setup to 'auto' by static configration) */
            //result = Sensor_IsiRegReadIss(  pSensorCtx, Sensor_BLC_CTRL00, &RegValue );
            //result = Sensor_IsiRegWriteIss( pSensorCtx, Sensor_BLC_CTRL00, RegValue | 0x80 );
            break;
        }

        default:
        {
            TRACE( Sensor_ERROR, "%s: BLC not supported\n", __FUNCTION__ );
            return ( RET_NOTSUPP );
        }
    }

    /* automatic gain control */
    switch ( pConfig->AGC )
    {
        case ISI_AGC_OFF:
        {
            // manual gain (appropriate for AEC with Marvin)
            //result = Sensor_IsiRegReadIss(  pSensorCtx, Sensor_AEC_MANUAL, &RegValue );
            //result = Sensor_IsiRegWriteIss( pSensorCtx, Sensor_AEC_MANUAL, RegValue | 0x02 );
            break;
        }

        default:
        {
            TRACE( Sensor_ERROR, "%s: AGC not supported\n", __FUNCTION__ );
            return ( RET_NOTSUPP );
        }
    }

    /* automatic white balance */
    switch( pConfig->AWB )
    {
        case ISI_AWB_OFF:
        {
            //result = Sensor_IsiRegReadIss(  pSensorCtx, Sensor_ISP_CTRL01, &RegValue );
            //result = Sensor_IsiRegWriteIss( pSensorCtx, Sensor_ISP_CTRL01, RegValue | 0x01 );
            break;
        }

        default:
        {
            TRACE( Sensor_ERROR, "%s: AWB not supported\n", __FUNCTION__ );
            return ( RET_NOTSUPP );
        }
    }

    switch( pConfig->AEC )
    {
        case ISI_AEC_OFF:
        {
            //result = Sensor_IsiRegReadIss(  pSensorCtx, Sensor_AEC_MANUAL, &RegValue );
            //result = Sensor_IsiRegWriteIss( pSensorCtx, Sensor_AEC_MANUAL, RegValue | 0x01 );
            break;
        }

        default:
        {
            TRACE( Sensor_ERROR, "%s: AEC not supported\n", __FUNCTION__ );
            return ( RET_NOTSUPP );
        }
    }


    switch( pConfig->DPCC )
    {
        case ISI_DPCC_OFF:
        {
            // disable white and black pixel cancellation (clear bit 6 and 7)
            //result = Sensor_IsiRegReadIss( pSensorCtx, Sensor_ISP_CTRL00, &RegValue );
            //RETURN_RESULT_IF_DIFFERENT( RET_SUCCESS, result );
            //result = Sensor_IsiRegWriteIss( pSensorCtx, Sensor_ISP_CTRL00, (RegValue &0x7c) );
            //RETURN_RESULT_IF_DIFFERENT( RET_SUCCESS, result );
            break;
        }

        default:
        {
            TRACE( Sensor_ERROR, "%s: DPCC not supported\n", __FUNCTION__ );
            return ( RET_NOTSUPP );
        }
    }// I have not update this commented part yet, as I did not find DPCC setting in the current 8810 driver of Trillian board. - SRJ

    return ( result );
}
static RESULT Sensor_SetupOutputWindow
(
    Sensor_Context_t        *pSensorCtx,
    const IsiSensorConfig_t *pConfig    
)
{
    bool_t res_no_chg;
    bool_t group_chg;

    if ((ISI_RES_W_GET(pConfig->Resolution)==ISI_RES_W_GET(pSensorCtx->Config.Resolution)) && 
        (ISI_RES_H_GET(pConfig->Resolution)==ISI_RES_H_GET(pSensorCtx->Config.Resolution)) &&
        (ISI_FPS_GET(pConfig->Resolution)==ISI_FPS_GET(pSensorCtx->Config.Resolution)) ) {
        res_no_chg = BOOL_TRUE;
        group_chg = BOOL_FALSE;
        
    } else {
        res_no_chg = BOOL_FALSE;
        group_chg = BOOL_TRUE;
    }

    return Sensor_SetupOutputWindowInternal(pSensorCtx,pConfig,BOOL_TRUE, BOOL_FALSE);
}


/*****************************************************************************/
/**
 *          Sensor_AecSetModeParameters
 *
 * @brief   This function fills in the correct parameters in Sensor-Instances
 *          according to AEC mode selection in IsiSensorConfig_t.
 *
 * @note    It is assumed that IsiSetupOutputWindow has been called before
 *          to fill in correct values in instance structure.
 *
 * @param   handle      Sensor context
 * @param   pConfig     pointer to sensor configuration structure
 *
 * @return  Return the result of the function call.
 * @retval  RET_SUCCESS
 * @retval  RET_NULL_POINTER
 *
 *****************************************************************************/
static RESULT Sensor_AecSetModeParameters
(
    Sensor_Context_t       *pSensorCtx,
    const IsiSensorConfig_t *pConfig
)
{
    RESULT result = RET_SUCCESS;

    TRACE( Sensor_INFO, "%s%s (enter)  Res: 0x%x  0x%x\n", __FUNCTION__, pSensorCtx->isAfpsRun?"(AFPS)":"",
						pSensorCtx->Config.Resolution, pConfig->Resolution);

    pSensorCtx->AecMaxIntegrationTime = 1123;
    pSensorCtx->AecMinIntegrationTime = 1;    

    pSensorCtx->AecMaxGain = Sensor_MAX_GAIN_AEC;
    pSensorCtx->AecMinGain = 0.3f;

    pSensorCtx->AecIntegrationTimeIncrement = 1;
    pSensorCtx->AecGainIncrement = Sensor_MIN_GAIN_STEP;

    pSensorCtx->AecCurGain               = pSensorCtx->AecMinGain;
    pSensorCtx->AecCurIntegrationTime    = 1;
    pSensorCtx->OldIntegrationTime 		 = 1;

    TRACE( Sensor_INFO, "%s%s (exit)\n pSensorCtx->AecMaxIntegrationTime:%lu\n",
    __FUNCTION__, pSensorCtx->isAfpsRun?"(AFPS)":"", pSensorCtx->AecMaxIntegrationTime);

    return ( result );
}


/*****************************************************************************/
/**
 *          Sensor_IsiSetupSensorIss
 *
 * @brief   Setup of the image sensor considering the given configuration.
 *
 * @param   handle      Sensor instance handle
 * @param   pConfig     pointer to sensor configuration structure
 *
 * @return  Return the result of the function call.
 * @retval  RET_SUCCESS
 * @retval  RET_NULL_POINTER
 *
 *****************************************************************************/
static RESULT Sensor_IsiSetupSensorIss
(
    IsiSensorHandle_t       handle,
    const IsiSensorConfig_t *pConfig
)
{
    Sensor_Context_t *pSensorCtx = (Sensor_Context_t *)handle;
    RESULT result = RET_SUCCESS;

    TRACE( Sensor_ERROR, "%s (enter)\n", __FUNCTION__);

    if ( pSensorCtx == NULL )
    {
        TRACE( Sensor_ERROR, "%s: Invalid sensor handle (NULL pointer detected)\n", __FUNCTION__ );
        return ( RET_WRONG_HANDLE );
    }

    if ( pConfig == NULL )
    {
        TRACE( Sensor_ERROR, "%s: Invalid configuration (NULL pointer detected)\n", __FUNCTION__ );
        return ( RET_NULL_POINTER );
    }

    if ( pSensorCtx->Streaming != BOOL_FALSE )
    {
        return RET_WRONG_STATE;
    }

    MEMCPY( &pSensorCtx->Config, pConfig, sizeof( IsiSensorConfig_t ) );

    /* 1.) SW reset of image sensor (via I2C register interface)  be careful, bits 6..0 are reserved, reset bit is not sticky */
    result = Sensor_IsiRegWriteIss ( pSensorCtx, Sensor_SOFTWARE_RST, 0x01U );
    RETURN_RESULT_IF_DIFFERENT( RET_SUCCESS, result );

    osSleep( 10 );

    result = Sensor_IsiRegWriteIss( pSensorCtx, Sensor_MODE_SELECT, 0x01);  // standby
    if ( result != RET_SUCCESS )
    {
        TRACE( Sensor_ERROR, "%s: Can't write Sensor Image System Register (disable streaming failed)\n", __FUNCTION__ );
        return ( result );
    }
    
    /* 2.) write default values derived from datasheet and evaluation kit (static setup altered by dynamic setup further below) */
	if(pSensorCtx->IsiSensorMipiInfo.ucMipiLanes == SUPPORT_MIPI_FOUR_LANE){
        result = IsiRegDefaultsApply( pSensorCtx, Sensor_IMX290_g_aRegDescription_fourlane);
    }
	
    if ( result != RET_SUCCESS )
    {
        return ( result );
    }

    /* sleep a while, that sensor can take over new default values */
    osSleep( 10 );

    /* 3.) verify default values to make sure everything has been written correctly as expected */
	#if 1
	result = IsiRegDefaultsVerify(pSensorCtx, Sensor_IMX290_g_aRegDescription_fourlane );
    if ( result != RET_SUCCESS )
    {
        return ( result );
    }
	#endif
	
    /* 4.) setup output format (RAW10|RAW12) */
    result = Sensor_SetupOutputFormat( pSensorCtx, pConfig );
    if ( result != RET_SUCCESS )
    {
        TRACE( Sensor_ERROR, "%s: SetupOutputFormat failed.\n", __FUNCTION__);
        return ( result );
    }

    /* 5.) setup output window */
    result = Sensor_SetupOutputWindow( pSensorCtx, pConfig );
    if ( result != RET_SUCCESS )
    {
        TRACE( Sensor_ERROR, "%s: SetupOutputWindow failed.\n", __FUNCTION__);
        return ( result );
    }

    result = Sensor_SetupImageControl( pSensorCtx, pConfig );
    if ( result != RET_SUCCESS )
    {
        TRACE( Sensor_ERROR, "%s: SetupImageControl failed.\n", __FUNCTION__);
        return ( result );
    }

    result = Sensor_AecSetModeParameters( pSensorCtx, pConfig );
    if ( result != RET_SUCCESS )
    {
        TRACE( Sensor_ERROR, "%s: AecSetModeParameters failed.\n", __FUNCTION__);
        return ( result );
    }
    if (result == RET_SUCCESS)
    {
        pSensorCtx->Configured = BOOL_TRUE;
    }

    TRACE( Sensor_ERROR, "%s: (exit)\n", __FUNCTION__);

    return ( result );
}



/*****************************************************************************/
/**
 *          Sensor_IsiChangeSensorResolutionIss
 *
 * @brief   Change image sensor resolution while keeping all other static settings.
 *          Dynamic settings like current gain & integration time are kept as
 *          close as possible. Sensor needs 2 frames to engage (first 2 frames
 *          are not correctly exposed!).
 *
 * @note    Re-read current & min/max values as they will probably have changed!
 *
 * @param   handle                  Sensor instance handle
 * @param   Resolution              new resolution ID
 * @param   pNumberOfFramesToSkip   reference to storage for number of frames to skip
 *
 * @return  Return the result of the function call.
 * @retval  RET_SUCCESS
 * @retval  RET_WRONG_HANDLE
 * @retval  RET_WRONG_STATE
 * @retval  RET_OUTOFRANGE
 *
 *****************************************************************************/
static RESULT Sensor_IsiChangeSensorResolutionIss
(
    IsiSensorHandle_t   handle,
    uint32_t            Resolution,
    uint8_t             *pNumberOfFramesToSkip
)
{
    Sensor_Context_t *pSensorCtx = (Sensor_Context_t *)handle;
    RESULT result = RET_SUCCESS;

    TRACE( Sensor_INFO, "%s (enter)  Resolution: %dx%d@%dfps\n", __FUNCTION__,
        ISI_RES_W_GET(Resolution),ISI_RES_H_GET(Resolution), ISI_FPS_GET(Resolution));

    if ( pSensorCtx == NULL )
    {
        return ( RET_WRONG_HANDLE );
    }

    if (pNumberOfFramesToSkip == NULL)
    {
        return ( RET_NULL_POINTER );
    }

    if ( (pSensorCtx->Configured != BOOL_TRUE) )
    {
        return RET_WRONG_STATE;
    }

    IsiSensorCaps_t Caps;
    Caps.Index = 0;
    Caps.Resolution = 0;
    while (Sensor_IsiGetCapsIss( handle, &Caps) == RET_SUCCESS) {
        if (Resolution == Caps.Resolution) {            
            break;
        }
        Caps.Index++;
    }

     if (Resolution != Caps.Resolution) {
        return RET_OUTOFRANGE;
    }
	
    if ( Resolution == pSensorCtx->Config.Resolution )
    {
        // well, no need to worry
        *pNumberOfFramesToSkip = 0;
		TRACE( Sensor_ERROR, "%s (Resolution:0x%08x;pSensorCtx->Config.Resolution:0x%08x)  \n", __FUNCTION__,Resolution,pSensorCtx->Config.Resolution);
    }
    else
    {
        // change resolution
        char *szResName = NULL;

		bool_t res_no_chg;
	    if (!((ISI_RES_W_GET(Resolution)==ISI_RES_W_GET(pSensorCtx->Config.Resolution)) && 
            (ISI_RES_H_GET(Resolution)==ISI_RES_H_GET(pSensorCtx->Config.Resolution))) ) {

            if (pSensorCtx->Streaming != BOOL_FALSE) {
                TRACE( Sensor_ERROR, "%s: Sensor is streaming, Change resolution is not allow\n",__FUNCTION__);
                return RET_WRONG_STATE;
            }
            res_no_chg = BOOL_FALSE;
        } else {
            res_no_chg = BOOL_TRUE;
        }
	
        result = IsiGetResolutionName( Resolution, &szResName );
        TRACE( Sensor_INFO, "%s: NewRes=0x%08x (%s)\n", __FUNCTION__, Resolution, szResName);

        // update resolution in copy of config in context
        pSensorCtx->Config.Resolution = Resolution;

        // tell sensor about that
        result = Sensor_SetupOutputWindowInternal( pSensorCtx, &pSensorCtx->Config, BOOL_TRUE, res_no_chg);
        if ( result != RET_SUCCESS )
        {
            TRACE( Sensor_ERROR, "%s: SetupOutputWindow failed.\n", __FUNCTION__);
            return ( result );
        }

        // remember old exposure values
        float OldGain = pSensorCtx->AecCurGain;
        float OldIntegrationTime = pSensorCtx->AecCurIntegrationTime;

        // update limits & stuff (reset current & old settings)
        result = Sensor_AecSetModeParameters( pSensorCtx, &pSensorCtx->Config );
        if ( result != RET_SUCCESS )
        {
            TRACE( Sensor_ERROR, "%s: AecSetModeParameters failed.\n", __FUNCTION__);
            return ( result );
        }

        // restore old exposure values (at least within new exposure values' limits)
        uint8_t NumberOfFramesToSkip;
        float   DummySetGain;
        float   DummySetIntegrationTime;
        result = Sensor_IsiExposureControlIss( handle, OldGain, OldIntegrationTime, &NumberOfFramesToSkip, &DummySetGain, &DummySetIntegrationTime );
        if ( result != RET_SUCCESS )
        {
            TRACE( Sensor_ERROR, "%s: Sensor_IsiExposureControlIss failed.\n", __FUNCTION__);
            return ( result );
        }

        // return number of frames that aren't exposed correctly
        *pNumberOfFramesToSkip = NumberOfFramesToSkip + 1;
    }

    TRACE( Sensor_ERROR, "%s (exit)  result: 0x%x\n", __FUNCTION__, result);

    return ( result );
}



/*****************************************************************************/
/**
 *          Sensor_IsiSensorSetStreamingIss
 *
 * @brief   Enables/disables streaming of sensor data, if possible.
 *
 * @param   handle      Sensor instance handle
 * @param   on          new streaming state (BOOL_TRUE=on, BOOL_FALSE=off)
 *
 * @return  Return the result of the function call.
 * @retval  RET_SUCCESS
 * @retval  RET_WRONG_HANDLE
 * @retval  RET_WRONG_STATE
 *
 *****************************************************************************/
static RESULT Sensor_IsiSensorSetStreamingIss
(
    IsiSensorHandle_t   handle,
    bool_t              on
)
{
    Sensor_Context_t *pSensorCtx = (Sensor_Context_t *)handle;
    RESULT result = RET_SUCCESS;

    TRACE( Sensor_ERROR, "%s (enter)  on = %d\n", __FUNCTION__,on);
    if ( pSensorCtx == NULL )
    {
        return ( RET_WRONG_HANDLE );
    }

    if ( (pSensorCtx->Configured != BOOL_TRUE) || (pSensorCtx->Streaming == on) )
    {
        return RET_WRONG_STATE;
    }

    if (on == BOOL_TRUE)
    {
        /* enable streaming */
        result = Sensor_IsiRegWriteIss(pSensorCtx, 0x3000, 0x0);
        RETURN_RESULT_IF_DIFFERENT( RET_SUCCESS, result );
        result = Sensor_IsiRegWriteIss(pSensorCtx, 0x3002, 0x0);
        RETURN_RESULT_IF_DIFFERENT( RET_SUCCESS, result );		
    }
    else
    {   
        /* disable streaming */
        result = Sensor_IsiRegWriteIss(pSensorCtx, 0x3000, 0x01);
        RETURN_RESULT_IF_DIFFERENT( RET_SUCCESS, result );
        result = Sensor_IsiRegWriteIss(pSensorCtx, 0x3002, 0x01);
        RETURN_RESULT_IF_DIFFERENT( RET_SUCCESS, result );	

        TRACE(Sensor_ERROR," STREAM OFF ++++++++++++++");
    }

    if (result == RET_SUCCESS)
    {
        pSensorCtx->Streaming = on;
    }

    TRACE( Sensor_INFO, "%s (exit)\n", __FUNCTION__);

    return ( result );
}



/*****************************************************************************/
/**
 *          Sensor_IsiSensorSetPowerIss
 *
 * @brief   Performs the power-up/power-down sequence of the camera, if possible.
 *
 * @param   handle      Sensor instance handle
 * @param   on          new power state (BOOL_TRUE=on, BOOL_FALSE=off)
 *
 * @return  Return the result of the function call.
 * @retval  RET_SUCCESS
 * @retval  RET_NULL_POINTER
 *
 *****************************************************************************/
static RESULT Sensor_IsiSensorSetPowerIss
(
    IsiSensorHandle_t   handle,
    bool_t              on
)
{
    Sensor_Context_t *pSensorCtx = (Sensor_Context_t *)handle;

    RESULT result = RET_SUCCESS;

    TRACE( Sensor_INFO, "%s (enter)\n", __FUNCTION__);

    if ( pSensorCtx == NULL )
    {
        return ( RET_WRONG_HANDLE );
    }

    pSensorCtx->Configured = BOOL_FALSE;
    pSensorCtx->Streaming  = BOOL_FALSE;

    result = HalSetPower( pSensorCtx->IsiCtx.HalHandle, pSensorCtx->IsiCtx.HalDevID, false );
    RETURN_RESULT_IF_DIFFERENT( RET_SUCCESS, result );

    result = HalSetReset( pSensorCtx->IsiCtx.HalHandle, pSensorCtx->IsiCtx.HalDevID, true );
    RETURN_RESULT_IF_DIFFERENT( RET_SUCCESS, result );

    if (on == BOOL_TRUE)
    {
        result = HalSetPower( pSensorCtx->IsiCtx.HalHandle, pSensorCtx->IsiCtx.HalDevID, true );
        RETURN_RESULT_IF_DIFFERENT( RET_SUCCESS, result );

        osSleep( 10 );

        result = HalSetReset( pSensorCtx->IsiCtx.HalHandle, pSensorCtx->IsiCtx.HalDevID, false );
        RETURN_RESULT_IF_DIFFERENT( RET_SUCCESS, result );

        osSleep( 10 );

        result = HalSetReset( pSensorCtx->IsiCtx.HalHandle, pSensorCtx->IsiCtx.HalDevID, true );
        RETURN_RESULT_IF_DIFFERENT( RET_SUCCESS, result );

        osSleep( 10 );

        result = HalSetReset( pSensorCtx->IsiCtx.HalHandle, pSensorCtx->IsiCtx.HalDevID, false );

        osSleep( 50 );
    }

    TRACE( Sensor_INFO, "%s (exit)\n", __FUNCTION__);

    return ( result );
}



/*****************************************************************************/
/**
 *          Sensor_IsiCheckSensorConnectionIss
 *
 * @brief   Checks the I2C-Connection to sensor by reading sensor revision id.
 *
 * @param   handle      Sensor instance handle
 *
 * @return  Return the result of the function call.
 * @retval  RET_SUCCESS
 * @retval  RET_NULL_POINTER
 *
 *****************************************************************************/
static RESULT Sensor_IsiCheckSensorConnectionIss
(
    IsiSensorHandle_t   handle
)
{
    uint32_t RevId;
    RESULT result = RET_SUCCESS;

    TRACE( Sensor_INFO, "%s (enter)\n", __FUNCTION__);
    if ( handle == NULL )
    {
        return ( RET_WRONG_HANDLE );
    }

    RevId = Sensor_CHIP_ID_HIGH_BYTE_DEFAULT;
    RevId = (RevId << 8U) | (Sensor_CHIP_ID_LOW_BYTE_DEFAULT);

    result = RET_SUCCESS;
    TRACE( Sensor_INFO, "%s (exit)\n", __FUNCTION__);

    return ( result );
}



/*****************************************************************************/
/**
 *          Sensor_IsiGetSensorRevisionIss
 *
 * @brief   reads the sensor revision register and returns this value
 *
 * @param   handle      pointer to sensor description struct
 * @param   p_value     pointer to storage value
 *
 * @return  Return the result of the function call.
 * @retval  RET_SUCCESS
 * @retval  RET_WRONG_HANDLE
 * @retval  RET_NULL_POINTER
 *
 *****************************************************************************/
static RESULT Sensor_IsiGetSensorRevisionIss
(
    IsiSensorHandle_t   handle,
    uint32_t            *p_value
)
{
    RESULT result = RET_SUCCESS;

    uint32_t data = 0xffff;
	uint32_t vcm_pos = MAX_LOG;

    TRACE( Sensor_INFO, "%s: (enter)\n", __FUNCTION__);

    if ( handle == NULL )
    {
        return ( RET_WRONG_HANDLE );
    }

    if ( p_value == NULL )
    {
        return ( RET_NULL_POINTER );
    }

    *p_value = 0xffff;
    result = Sensor_IsiRegReadIss ( handle, Sensor_CHIP_ID_HIGH_BYTE, &data );
    *p_value &= ( (data & 0xFF) << 8U );
    result = Sensor_IsiRegReadIss ( handle, Sensor_CHIP_ID_LOW_BYTE, &data );
    *p_value &= ( (data & 0xFF) );

    TRACE( Sensor_INFO, "%s (exit)\n", __FUNCTION__);

    return ( result );
}



/*****************************************************************************/
/**
 *          Sensor_IsiRegReadIss
 *
 * @brief   grants user read access to the camera register
 *
 * @param   handle      pointer to sensor description struct
 * @param   address     sensor register to write
 * @param   p_value     pointer to value
 *
 * @return  Return the result of the function call.
 * @retval  RET_SUCCESS
 * @retval  RET_WRONG_HANDLE
 * @retval  RET_NULL_POINTER
 *
 *****************************************************************************/
static RESULT Sensor_IsiRegReadIss
(
    IsiSensorHandle_t   handle,
    const uint32_t      address,
    uint32_t            *p_value
)
{
    RESULT result = RET_SUCCESS;
	
    ALOGD("%s: (enter)\n", __FUNCTION__);
    if ( handle == NULL )
    {
        return ( RET_WRONG_HANDLE );
    }

    if ( p_value == NULL )
    {
        return ( RET_NULL_POINTER );
    }
    else
    {
        *p_value = 0;
        result = IsiI2cReadSensorRegister( handle, address, (uint8_t *)p_value, 1, BOOL_TRUE );
    }

    ALOGD("%s (exit: 0x%04x 0x%02x)\n", __FUNCTION__, address, *p_value);
    return ( result );
}



/*****************************************************************************/
/**
 *          Sensor_IsiRegWriteIss
 *
 * @brief   grants user write access to the camera register
 *
 * @param   handle      pointer to sensor description struct
 * @param   address     sensor register to write
 * @param   value       value to write
 *
 * @return  Return the result of the function call.
 * @retval  RET_SUCCESS
 * @retval  RET_WRONG_HANDLE
 *
 *****************************************************************************/
static RESULT Sensor_IsiRegWriteIss
(
    IsiSensorHandle_t   handle,
    const uint32_t      address,
    const uint32_t      value
)
{
    RESULT result = RET_SUCCESS;

    uint8_t NrOfBytes;

    ALOGD("%s: (enter)\n", __FUNCTION__);

    if ( handle == NULL )
    {
        return ( RET_WRONG_HANDLE );
    }

//    if ( !NrOfBytes )
    {
        NrOfBytes = 1;
    }

    result = IsiI2cWriteSensorRegister( handle, address, (uint8_t *)(&value), NrOfBytes, BOOL_TRUE );
	if(result != RET_SUCCESS)
    {
        ALOGE("IsiI2cWriteSensorRegister_A faile (%d), address :%x\n", result, address);
    }
#if 0
    if(result != RET_SUCCESS)
    {
        ALOGD( "%s IsiI2cWriteSensorRegister faile (%d)\n", __FUNCTION__, result);
    }
    else
    {
        ALOGD( "%s IsiI2cWriteSensorRegister OK (%d)\n", __FUNCTION__, result);
    }

    ALOGD( "%s (exit: 0x%08x 0x%08x)\n", __FUNCTION__, address, value);
#endif
    ALOGD( "%s (exit: 0x%04x 0x%02x)\n", __FUNCTION__, address, value);

    return ( result );
}



/*****************************************************************************/
/**
 *          Sensor_IsiGetGainLimitsIss
 *
 * @brief   Returns the exposure minimal and maximal values of an
 *          Sensor instance
 *
 * @param   handle       Sensor instance handle
 * @param   pMinExposure Pointer to a variable receiving minimal exposure value
 * @param   pMaxExposure Pointer to a variable receiving maximal exposure value
 *
 * @return  Return the result of the function call.
 * @retval  RET_SUCCESS
 * @retval  RET_NULL_POINTER
 *
 *****************************************************************************/
static RESULT Sensor_IsiGetGainLimitsIss
(
    IsiSensorHandle_t   handle,
    float               *pMinGain,
    float               *pMaxGain
)
{
    Sensor_Context_t *pSensorCtx = (Sensor_Context_t *)handle;

    RESULT result = RET_SUCCESS;


    TRACE( Sensor_INFO, "%s: (enter)\n", __FUNCTION__);

    if ( pSensorCtx == NULL )
    {
        TRACE( Sensor_ERROR, "%s: Invalid sensor handle (NULL pointer detected)\n", __FUNCTION__ );
        return ( RET_WRONG_HANDLE );
    }

    if ( (pMinGain == NULL) || (pMaxGain == NULL) )
    {
        TRACE( Sensor_ERROR, "%s: NULL pointer received!!\n" );
        return ( RET_NULL_POINTER );
    }

    *pMinGain = pSensorCtx->AecMinGain;
    *pMaxGain = pSensorCtx->AecMaxGain;

    TRACE( Sensor_INFO, "%s: pMinGain:%f,pMaxGain:%f(exit)\n", __FUNCTION__,pSensorCtx->AecMinGain,pSensorCtx->AecMaxGain);

    return ( result );
}



/*****************************************************************************/
/**
 *          Sensor_IsiGetIntegrationTimeLimitsIss
 *
 * @brief   Returns the minimal and maximal integration time values of an
 *          Sensor instance
 *
 * @param   handle       Sensor instance handle
 * @param   pMinExposure Pointer to a variable receiving minimal exposure value
 * @param   pMaxExposure Pointer to a variable receiving maximal exposure value
 *
 * @return  Return the result of the function call.
 * @retval  RET_SUCCESS
 * @retval  RET_NULL_POINTER
 *
 *****************************************************************************/
static RESULT Sensor_IsiGetIntegrationTimeLimitsIss
(
    IsiSensorHandle_t   handle,
    float               *pMinIntegrationTime,
    float               *pMaxIntegrationTime
)
{
    Sensor_Context_t *pSensorCtx = (Sensor_Context_t *)handle;

    RESULT result = RET_SUCCESS;


    TRACE( Sensor_INFO, "%s: (------oyyf enter) \n", __FUNCTION__);

    if ( pSensorCtx == NULL )
    {
        TRACE( Sensor_ERROR, "%s: Invalid sensor handle (NULL pointer detected)\n", __FUNCTION__ );
        return ( RET_WRONG_HANDLE );
    }

    if ( (pMinIntegrationTime == NULL) || (pMaxIntegrationTime == NULL) )
    {
        TRACE( Sensor_ERROR, "%s: NULL pointer received!!\n" );
        return ( RET_NULL_POINTER );
    }

    *pMinIntegrationTime = pSensorCtx->AecMinIntegrationTime;
    *pMaxIntegrationTime = pSensorCtx->AecMaxIntegrationTime;

    TRACE( Sensor_INFO, "%s: (------oyyf exit) (\n", __FUNCTION__);

    return ( result );
}


/*****************************************************************************/
/**
 *          Sensor_IsiGetGainIss
 *
 * @brief   Reads gain values from the image sensor module.
 *
 * @param   handle                  Sensor instance handle
 * @param   pSetGain                set gain
 *
 * @return  Return the result of the function call.
 * @retval  RET_SUCCESS
 * @retval  RET_NULL_POINTER
 *
 *****************************************************************************/
RESULT Sensor_IsiGetGainIss
(
    IsiSensorHandle_t   handle,
    float               *pSetGain
)
{
    Sensor_Context_t *pSensorCtx = (Sensor_Context_t *)handle;

    RESULT result = RET_SUCCESS;

    TRACE( Sensor_INFO, "%s: (enter)\n", __FUNCTION__);

    if ( pSensorCtx == NULL )
    {
        TRACE( Sensor_ERROR, "%s: Invalid sensor handle (NULL pointer detected)\n", __FUNCTION__ );
        return ( RET_WRONG_HANDLE );
    }

    if ( pSetGain == NULL)
    {
        return ( RET_NULL_POINTER );
    }

    *pSetGain = pSensorCtx->AecCurGain;

    TRACE( Sensor_INFO, "%s: (exit)\n", __FUNCTION__);

    return ( result );
}



/*****************************************************************************/
/**
 *          Sensor_IsiGetGainIncrementIss
 *
 * @brief   Get smallest possible gain increment.
 *
 * @param   handle                  Sensor instance handle
 * @param   pIncr                   increment
 *
 * @return  Return the result of the function call.
 * @retval  RET_SUCCESS
 * @retval  RET_NULL_POINTER
 *
 *****************************************************************************/
RESULT Sensor_IsiGetGainIncrementIss
(
    IsiSensorHandle_t   handle,
    float               *pIncr
)
{
    Sensor_Context_t *pSensorCtx = (Sensor_Context_t *)handle;

    RESULT result = RET_SUCCESS;

    TRACE( Sensor_INFO, "%s: (enter)\n", __FUNCTION__);

    if ( pSensorCtx == NULL )
    {
        TRACE( Sensor_ERROR, "%s: Invalid sensor handle (NULL pointer detected)\n", __FUNCTION__ );
        return ( RET_WRONG_HANDLE );
    }

    if ( pIncr == NULL)
    {
        return ( RET_NULL_POINTER );
    }

    //_smallest_ increment the sensor/driver can handle (e.g. used for sliders in the application)
    *pIncr = pSensorCtx->AecGainIncrement;

    TRACE( Sensor_INFO, "%s: (exit)\n", __FUNCTION__);

    return ( result );
}

/*****************************************************************************/
/**
 *          Sensor_IsiSetGainIss
 *
 * @brief   Writes gain values to the image sensor module.
 *          Updates current gain and exposure in sensor struct/state.
 *
 * @param   handle                  Sensor instance handle
 * @param   NewGain                 gain to be set
 * @param   pSetGain                set gain
 *
 * @return  Return the result of the function call.
 * @retval  RET_SUCCESS
 * @retval  RET_WRONG_HANDLE
 * @retval  RET_NULL_POINTER
 * @retval  RET_INVALID_PARM
 * @retval  RET_FAILURE
 *
 *****************************************************************************/
RESULT Sensor_IsiSetGainIss
(
    IsiSensorHandle_t   handle,
    float               NewGain,
    float               *pSetGain
)
{
    Sensor_Context_t *pSensorCtx = (Sensor_Context_t *)handle;
    RESULT result = RET_SUCCESS;
    uint8_t gainIndex = 0;

    if ( pSensorCtx == NULL )
    {
        TRACE( Sensor_ERROR, "%s: Invalid sensor handle (NULL pointer detected)\n", __FUNCTION__ );
        return ( RET_WRONG_HANDLE );
    }

    if ( pSetGain == NULL)
    {
        TRACE( Sensor_ERROR, "%s: Invalid parameter (NULL pointer detected)\n", __FUNCTION__ );
        return ( RET_NULL_POINTER );
    }
  
    if( NewGain < pSensorCtx->AecMinGain ) NewGain = pSensorCtx->AecMinGain;
    if( NewGain > pSensorCtx->AecMaxGain ) NewGain = pSensorCtx->AecMaxGain;
   
    gainIndex = (uint8_t)(NewGain * 10 / 3);
    
    // write new gain into sensor registers, do not write if nothing has changed
    if(pSensorCtx->AecCurGain != NewGain)
    {
        result = Sensor_IsiRegWriteIss( pSensorCtx, 0x3014, gainIndex);
        RETURN_RESULT_IF_DIFFERENT( RET_SUCCESS, result );
		
        pSensorCtx->OldGain = pSensorCtx->AecCurGain;
        pSensorCtx->AecCurGain = NewGain;
    }

    *pSetGain = pSensorCtx->AecCurGain;

    //TRACE( Sensor_ERROR, "%s: setgain mubiao(%f) shiji(%f) usGain(%d)\n", __FUNCTION__, NewGain, *pSetGain,usGain);

    return ( result );
}

     

/*****************************************************************************/
/**
 *          Sensor_IsiGetIntegrationTimeIss
 *
 * @brief   Reads integration time values from the image sensor module.
 *
 * @param   handle                  Sensor instance handle
 * @param   pSetIntegrationTime     set integration time
 *
 * @return  Return the result of the function call.
 * @retval  RET_SUCCESS
 * @retval  RET_NULL_POINTER
 *
 *****************************************************************************/
RESULT Sensor_IsiGetIntegrationTimeIss
(
    IsiSensorHandle_t   handle,
    float               *pSetIntegrationTime
)
{
    Sensor_Context_t *pSensorCtx = (Sensor_Context_t *)handle;

    RESULT result = RET_SUCCESS;

    TRACE( Sensor_INFO, "%s: (enter)\n", __FUNCTION__);

    if ( pSensorCtx == NULL )
    {
        TRACE( Sensor_ERROR, "%s: Invalid sensor handle (NULL pointer detected)\n", __FUNCTION__ );
        return ( RET_WRONG_HANDLE );
    }

    if ( pSetIntegrationTime == NULL )
    {
        return ( RET_NULL_POINTER );
    }

    *pSetIntegrationTime = pSensorCtx->AecCurIntegrationTime;

    TRACE( Sensor_INFO, "%s: (exit)\n", __FUNCTION__);

    return ( result );
}



/*****************************************************************************/
/**
 *          Sensor_IsiGetIntegrationTimeIncrementIss
 *
 * @brief   Get smallest possible integration time increment.
 *
 * @param   handle                  Sensor instance handle
 * @param   pIncr                   increment
 *
 * @return  Return the result of the function call.
 * @retval  RET_SUCCESS
 * @retval  RET_NULL_POINTER
 *
 *****************************************************************************/
RESULT Sensor_IsiGetIntegrationTimeIncrementIss
(
    IsiSensorHandle_t   handle,
    float               *pIncr
)
{
    Sensor_Context_t *pSensorCtx = (Sensor_Context_t *)handle;

    RESULT result = RET_SUCCESS;

    TRACE( Sensor_INFO, "%s: (-------oyyf)(enter)\n", __FUNCTION__);

    if ( pSensorCtx == NULL )
    {
        TRACE( Sensor_ERROR, "%s: Invalid sensor handle (NULL pointer detected)\n", __FUNCTION__ );
        return ( RET_WRONG_HANDLE );
    }

    if ( pIncr == NULL )
    {
        return ( RET_NULL_POINTER );
    }

    //_smallest_ increment the sensor/driver can handle (e.g. used for sliders in the application)
    *pIncr = (float)pSensorCtx->AecIntegrationTimeIncrement;

    TRACE( Sensor_INFO, "%s: (------oyyf)(exit) pSensorCtx->AecIntegrationTimeIncrement(%u)\n", __FUNCTION__,pSensorCtx->AecIntegrationTimeIncrement);

    return ( result );
}



/*****************************************************************************/
/**
 *          Sensor_IsiSetIntegrationTimeIss
 *
 * @brief   Writes gain and integration time values to the image sensor module.
 *          Updates current integration time and exposure in sensor
 *          struct/state.
 *
 * @param   handle                  Sensor instance handle
 * @param   NewIntegrationTime      integration time to be set
 * @param   pSetIntegrationTime     set integration time
 * @param   pNumberOfFramesToSkip   number of frames to skip until AE is
 *                                  executed again
 *
 * @return  Return the result of the function call.
 * @retval  RET_SUCCESS
 * @retval  RET_WRONG_HANDLE
 * @retval  RET_NULL_POINTER
 * @retval  RET_INVALID_PARM
 * @retval  RET_FAILURE
 * @retval  RET_DIVISION_BY_ZERO
 *
 *****************************************************************************/
RESULT Sensor_IsiSetIntegrationTimeIss
(
    IsiSensorHandle_t   handle,
    float               NewIntegrationTime,
    float               *pSetIntegrationTime,
    uint8_t 			*pNumberOfFramesToSkip
)
{
    Sensor_Context_t *pSensorCtx = (Sensor_Context_t *)handle;
    RESULT result = RET_SUCCESS;
	unsigned char low, middle, high;
	uint32_t nit = (uint32_t)NewIntegrationTime;

    TRACE( Sensor_DEBUG, "%s: (enter) NewIntegrationTime: %f (min: %lu   max: %lu)\n", __FUNCTION__,
        NewIntegrationTime,
        pSensorCtx->AecMinIntegrationTime,
        pSensorCtx->AecMaxIntegrationTime);

    if ( pSensorCtx == NULL )
    {
        TRACE( Sensor_ERROR, "%s: Invalid sensor handle (NULL pointer detected)\n", __FUNCTION__ );
        return ( RET_WRONG_HANDLE );
    }

    if ( (pSetIntegrationTime == NULL) || (pNumberOfFramesToSkip == NULL) )
    {
        TRACE( Sensor_ERROR, "%s: Invalid parameter (NULL pointer detected)\n", __FUNCTION__ );
        return ( RET_NULL_POINTER );
    }

    //maximum and minimum integration time is limited by the sensor, if this limit is not
    //considered, the exposure control loop needs lots of time to return to a new state
    //so limit to allowed range
    if ( nit > pSensorCtx->AecMaxIntegrationTime ) nit = pSensorCtx->AecMaxIntegrationTime;
    if ( nit < pSensorCtx->AecMinIntegrationTime ) nit = pSensorCtx->AecMinIntegrationTime;

	low = (unsigned char)(nit & 0xFF);
	middle = (unsigned char)((nit & 0xFF00) >> 8);
	high = (unsigned char)((nit & 0x30000) >> 16);
	
	result = Sensor_IsiRegWriteIss(pSensorCtx, 0x3020, low);
	RETURN_RESULT_IF_DIFFERENT(RET_SUCCESS, result);
	result = Sensor_IsiRegWriteIss(pSensorCtx, 0x3021, middle);
	RETURN_RESULT_IF_DIFFERENT(RET_SUCCESS, result);
	result = Sensor_IsiRegWriteIss(pSensorCtx, 0x3022, high);
	RETURN_RESULT_IF_DIFFERENT(RET_SUCCESS, result);
	pSensorCtx->OldIntegrationTime = pSensorCtx->AecCurIntegrationTime;
	pSensorCtx->AecCurIntegrationTime = nit;
	*pNumberOfFramesToSkip = 1;
	
    *pSetIntegrationTime = (float)nit;

	TRACE(Sensor_DEBUG, "%s: exit\n", __FUNCTION__);
    return ( result );
}




/*****************************************************************************/
/**
 *          Sensor_IsiExposureControlIss
 *
 * @brief   Camera hardware dependent part of the exposure control loop.
 *          Calculates appropriate register settings from the new exposure
 *          values and writes them to the image sensor module.
 *
 * @param   handle                  Sensor instance handle
 * @param   NewGain                 newly calculated gain to be set
 * @param   NewIntegrationTime      newly calculated integration time to be set
 * @param   pNumberOfFramesToSkip   number of frames to skip until AE is
 *                                  executed again
 *
 * @return  Return the result of the function call.
 * @retval  RET_SUCCESS
 * @retval  RET_WRONG_HANDLE
 * @retval  RET_NULL_POINTER
 * @retval  RET_INVALID_PARM
 * @retval  RET_FAILURE
 * @retval  RET_DIVISION_BY_ZERO
 *
 *****************************************************************************/
RESULT Sensor_IsiExposureControlIss
(
    IsiSensorHandle_t   handle,
    float               NewGain,
    float               NewIntegrationTime,
    uint8_t             *pNumberOfFramesToSkip,
    float               *pSetGain,
    float               *pSetIntegrationTime
)
{
    Sensor_Context_t *pSensorCtx = (Sensor_Context_t *)handle;
    RESULT result = RET_SUCCESS;

    TRACE( Sensor_DEBUG, "%s: (enter)\n", __FUNCTION__);
    if ( pSensorCtx == NULL )
    {
        TRACE( Sensor_ERROR, "%s: Invalid sensor handle (NULL pointer detected)\n", __FUNCTION__ );
        return ( RET_WRONG_HANDLE );
    }

    if ( (pNumberOfFramesToSkip == NULL)
            || (pSetGain == NULL)
            || (pSetIntegrationTime == NULL) )
    {
        TRACE( Sensor_ERROR, "%s: Invalid parameter (NULL pointer detected)\n", __FUNCTION__ );
        return ( RET_NULL_POINTER );
    }

    TRACE( Sensor_DEBUG, "%s: g=%f, Ti=%f\n", __FUNCTION__, NewGain, NewIntegrationTime );

	/*
  	result = Sensor_IsiRegWriteIss( pSensorCtx, 0x0104,1);
    RETURN_RESULT_IF_DIFFERENT( RET_SUCCESS, result );
	*/

    result = Sensor_IsiSetIntegrationTimeIss( handle, NewIntegrationTime, pSetIntegrationTime, pNumberOfFramesToSkip);
    result = Sensor_IsiSetGainIss( handle, NewGain, pSetGain);

	//result = Sensor_IsiRegWriteIss( pSensorCtx, 0x0104,0);
	
	//setting gain 
	//result = Sensor_IsiRegWriteIss( pSensorCtx, 0x3014,0x14);

	//setting IntegrationTime 
	//result = Sensor_IsiRegWriteIss( pSensorCtx, 0x3022,0x63);
	//result = Sensor_IsiRegWriteIss( pSensorCtx, 0x3021,0x04);
	//result = Sensor_IsiRegWriteIss( pSensorCtx, 0x3020,0x00);

	
    RETURN_RESULT_IF_DIFFERENT( RET_SUCCESS, result );  
    TRACE( Sensor_DEBUG, "%s: set: g=%f, Ti=%f, skip=%d\n", __FUNCTION__, *pSetGain, *pSetIntegrationTime, *pNumberOfFramesToSkip ); 

    return ( result );
}



/*****************************************************************************/
/**
 *          Sensor_IsiGetCurrentExposureIss
 *
 * @brief   Returns the currently adjusted AE values
 *
 * @param   handle                  Sensor instance handle
 *
 * @return  Return the result of the function call.
 * @retval  RET_SUCCESS
 * @retval  RET_NULL_POINTER
 *
 *****************************************************************************/
RESULT Sensor_IsiGetCurrentExposureIss
(
    IsiSensorHandle_t   handle,
    float               *pSetGain,
    float               *pSetIntegrationTime
)
{
    Sensor_Context_t *pSensorCtx = (Sensor_Context_t *)handle;
    RESULT result = RET_SUCCESS;

    TRACE( Sensor_INFO, "%s: (enter)\n", __FUNCTION__);

    if ( pSensorCtx == NULL )
    {
        TRACE( Sensor_ERROR, "%s: Invalid sensor handle (NULL pointer detected)\n", __FUNCTION__ );
        return ( RET_WRONG_HANDLE );
    }

    if ( (pSetGain == NULL) || (pSetIntegrationTime == NULL) )
    {
        return ( RET_NULL_POINTER );
    }

    *pSetGain            = pSensorCtx->AecCurGain;
    *pSetIntegrationTime = pSensorCtx->AecCurIntegrationTime * 1.0;

    TRACE( Sensor_INFO, "%s: (exit)\n", __FUNCTION__);

    return ( result );
}



/*****************************************************************************/
/**
 *          Sensor_IsiGetResolutionIss
 *
 * @brief   Reads integration time values from the image sensor module.
 *
 * @param   handle                  sensor instance handle
 * @param   pSettResolution         set resolution
 *
 * @return  Return the result of the function call.
 * @retval  RET_SUCCESS
 * @retval  RET_WRONG_HANDLE
 * @retval  RET_NULL_POINTER
 *
 *****************************************************************************/
RESULT Sensor_IsiGetResolutionIss
(
    IsiSensorHandle_t   handle,
    uint32_t            *pSetResolution
)
{
    Sensor_Context_t *pSensorCtx = (Sensor_Context_t *)handle;
    RESULT result = RET_SUCCESS;

    TRACE( Sensor_INFO, "%s: (enter)\n", __FUNCTION__);

    if ( pSensorCtx == NULL )
    {
        TRACE( Sensor_ERROR, "%s: Invalid sensor handle (NULL pointer detected)\n", __FUNCTION__ );
        return ( RET_WRONG_HANDLE );
    }

    if ( pSetResolution == NULL )
    {
        return ( RET_NULL_POINTER );
    }

    *pSetResolution = pSensorCtx->Config.Resolution;

    TRACE( Sensor_INFO, "%s: (exit)\n", __FUNCTION__);

    return ( result );
}



/*****************************************************************************/
/**
 *          Sensor_IsiGetAfpsInfoHelperIss
 *
 * @brief   Calc AFPS sub resolution settings for the given resolution
 *
 * @param   pSensorCtx             Sensor instance (dummy!) context
 * @param   Resolution              Any supported resolution to query AFPS params for
 * @param   pAfpsInfo               Reference of AFPS info structure to write the results to
 * @param   AfpsStageIdx            Index of current AFPS stage to use
 *
 * @return  Return the result of the function call.
 * @retval  RET_SUCCESS
 *
 *****************************************************************************/
static RESULT Sensor_IsiGetAfpsInfoHelperIss(
    Sensor_Context_t   *pSensorCtx,
    uint32_t            Resolution,
    IsiAfpsInfo_t*      pAfpsInfo,
    uint32_t            AfpsStageIdx
)
{
    RESULT result = RET_SUCCESS;

    TRACE( Sensor_INFO, "%s: (-----------oyyf enter) pAfpsInfo->AecMaxIntTime(%lu) pSensorCtx->AecMaxIntegrationTime(%lu)\n", __FUNCTION__, pAfpsInfo->AecMaxIntTime,pSensorCtx->AecMaxIntegrationTime);

    DCT_ASSERT(pSensorCtx != NULL);
    DCT_ASSERT(pAfpsInfo != NULL);
    DCT_ASSERT(AfpsStageIdx <= ISI_NUM_AFPS_STAGES);

    // update resolution in copy of config in context
    pSensorCtx->Config.Resolution = Resolution;

    // tell sensor about that
    result = Sensor_SetupOutputWindowInternal( pSensorCtx, &pSensorCtx->Config,BOOL_FALSE,BOOL_FALSE);
    if ( result != RET_SUCCESS )
    {
        TRACE( Sensor_ERROR, "%s: SetupOutputWindow failed for resolution ID %08x.\n", __FUNCTION__, Resolution);
        return ( result );
    }

    // update limits & stuff (reset current & old settings)
    result = Sensor_AecSetModeParameters( pSensorCtx, &pSensorCtx->Config );
    if ( result != RET_SUCCESS )
    {
        TRACE( Sensor_ERROR, "%s: AecSetModeParameters failed for resolution ID %08x.\n", __FUNCTION__, Resolution);
        return ( result );
    }

    // take over params
    pAfpsInfo->Stage[AfpsStageIdx].Resolution = Resolution;
    pAfpsInfo->Stage[AfpsStageIdx].MaxIntTime = pSensorCtx->AecMaxIntegrationTime * 1.0;
    pAfpsInfo->AecMinGain           = pSensorCtx->AecMinGain;
    pAfpsInfo->AecMaxGain           = pSensorCtx->AecMaxGain;
    pAfpsInfo->AecMinIntTime        = pSensorCtx->AecMinIntegrationTime * 1.0;
    pAfpsInfo->AecMaxIntTime        = pSensorCtx->AecMaxIntegrationTime * 1.0;
    pAfpsInfo->AecSlowestResolution = Resolution;

    TRACE( Sensor_INFO, "%s: (-----------oyyf exit) pAfpsInfo->AecMaxIntTime(%lu) pSensorCtx->AecMaxIntegrationTime(%lu)\n", __FUNCTION__, pAfpsInfo->AecMaxIntTime,pSensorCtx->AecMaxIntegrationTime);

    return ( result );
}

/*****************************************************************************/
/**
 *          Sensor_IsiGetAfpsInfoIss
 *
 * @brief   Returns the possible AFPS sub resolution settings for the given resolution series
 *
 * @param   handle                  Sensor instance handle
 * @param   Resolution              Any resolution within the AFPS group to query;
 *                                  0 (zero) to use the currently configured resolution
 * @param   pAfpsInfo               Reference of AFPS info structure to store the results
 *
 * @return  Return the result of the function call.
 * @retval  RET_SUCCESS
 * @retval  RET_WRONG_HANDLE
 * @retval  RET_NULL_POINTER
 * @retval  RET_NOTSUPP
 *
 *****************************************************************************/
RESULT Sensor_IsiGetAfpsInfoIss(
    IsiSensorHandle_t   handle,
    uint32_t            Resolution,
    IsiAfpsInfo_t*      pAfpsInfo
)
{
    Sensor_Context_t *pSensorCtx = (Sensor_Context_t *)handle;
    RESULT result = RET_SUCCESS;

    TRACE( Sensor_INFO, "%s: (enter)\n", __FUNCTION__);
    if ( pSensorCtx == NULL )
    {
        TRACE( Sensor_ERROR, "%s: Invalid sensor handle (NULL pointer detected)\n", __FUNCTION__ );
        return ( RET_WRONG_HANDLE );
    }

    if ( pAfpsInfo == NULL )
    {
        return ( RET_NULL_POINTER );
    }

    // use currently configured resolution?
    if (Resolution == 0)
    {
        Resolution = pSensorCtx->Config.Resolution;
    }

    // prepare index
    uint32_t idx = 0;

    // set current resolution data in info struct
    pAfpsInfo->CurrResolution = pSensorCtx->Config.Resolution;
    pAfpsInfo->CurrMinIntTime = pSensorCtx->AecMinIntegrationTime * 1.0;
    pAfpsInfo->CurrMaxIntTime = pSensorCtx->AecMaxIntegrationTime * 1.0;
	TRACE( Sensor_INFO, "#%s: (-----------oyyf) pAfpsInfo->AecMaxIntTime(%lu) pSensorCtx->AecMaxIntegrationTime(%lu)\n", __FUNCTION__, pAfpsInfo->AecMaxIntTime,pSensorCtx->AecMaxIntegrationTime);

    // allocate dummy context used for Afps parameter calculation as a copy of current context
    Sensor_Context_t *pDummyCtx = (Sensor_Context_t*) malloc( sizeof(Sensor_Context_t) );
    if ( pDummyCtx == NULL )
    {
        TRACE( Sensor_ERROR,  "%s: Can't allocate dummy ov14825 context\n",  __FUNCTION__ );
        return ( RET_OUTOFMEM );
    }
    *pDummyCtx = *pSensorCtx;

    // set AFPS mode in dummy context
    pDummyCtx->isAfpsRun = BOOL_TRUE;

#define AFPSCHECKANDADD(_res_) \
    { \
        RESULT lres = Sensor_IsiGetAfpsInfoHelperIss( pDummyCtx, _res_, pAfpsInfo, idx ); \
        if ( lres == RET_SUCCESS ) \
        { \
            ++idx; \
        } \
        else \
        { \
            UPDATE_RESULT( result, lres ); \
        } \
    }

    // check which AFPS series is requested and build its params list for the enabled AFPS resolutions
	switch (pSensorCtx->IsiSensorMipiInfo.ucMipiLanes)
	{
		case SUPPORT_MIPI_FOUR_LANE:
		{
		    switch(Resolution)
		    {
				case ISI_RES_TV1080P30:
					AFPSCHECKANDADD(ISI_RES_TV1080P30);
					break;
						
		        default:
		            TRACE( Sensor_ERROR,  "%s: Resolution %08x not supported by AFPS\n",  __FUNCTION__, Resolution );
		            result = RET_NOTSUPP;
		            break;
			}
			break;
		}
			
		default:
			TRACE( Sensor_ERROR,  "%s: pSensorCtx->IsiSensorMipiInfo.ucMipiLanes(0x%x) is invalidate!\n", 
									__FUNCTION__, pSensorCtx->IsiSensorMipiInfo.ucMipiLanes );
			result = RET_FAILURE;
			break;			        
	}

    // release dummy context again
    free(pDummyCtx);
    TRACE( Sensor_INFO, "%s: (exit)\n", __FUNCTION__);

    return ( result );
}



/*****************************************************************************/
/**
 *          Sensor_IsiGetCalibKFactor
 *
 * @brief   Returns the Sensor specific K-Factor
 *
 * @param   handle       Sensor instance handle
 * @param   pIsiKFactor  Pointer to Pointer receiving the memory address
 *
 * @return  Return the result of the function call.
 * @retval  RET_SUCCESS
 * @retval  RET_WRONG_HANDLE
 * @retval  RET_NULL_POINTER
 *
 *****************************************************************************/
static RESULT Sensor_IsiGetCalibKFactor
(
    IsiSensorHandle_t   handle,
    Isi1x1FloatMatrix_t **pIsiKFactor
)
{
    Sensor_Context_t *pSensorCtx = (Sensor_Context_t *)handle;
    RESULT result = RET_SUCCESS;

    TRACE( Sensor_INFO, "%s: (enter)\n", __FUNCTION__);

    if ( pSensorCtx == NULL )
    {
        return ( RET_WRONG_HANDLE );
    }

    if ( pIsiKFactor == NULL )
    {
        return ( RET_NULL_POINTER );
    }

    //*pIsiKFactor = (Isi1x1FloatMatrix_t *)&Sensor_KFactor;

    TRACE( Sensor_INFO, "%s: (exit)\n", __FUNCTION__);
    return ( result );
}


/*****************************************************************************/
/**
 *          Sensor_IsiGetCalibPcaMatrix
 *
 * @brief   Returns the Sensor specific PCA-Matrix
 *
 * @param   handle          Sensor instance handle
 * @param   pIsiPcaMatrix   Pointer to Pointer receiving the memory address
 *
 * @return  Return the result of the function call.
 * @retval  RET_SUCCESS
 * @retval  RET_WRONG_HANDLE
 * @retval  RET_NULL_POINTER
 *
 *****************************************************************************/
static RESULT Sensor_IsiGetCalibPcaMatrix
(
    IsiSensorHandle_t   handle,
    Isi3x2FloatMatrix_t **pIsiPcaMatrix
)
{
    Sensor_Context_t *pSensorCtx = (Sensor_Context_t *)handle;
    RESULT result = RET_SUCCESS;

    TRACE( Sensor_INFO, "%s: (enter)\n", __FUNCTION__);
    if ( pSensorCtx == NULL )
    {
        return ( RET_WRONG_HANDLE );
    }

    if ( pIsiPcaMatrix == NULL )
    {
        return ( RET_NULL_POINTER );
    }

    //*pIsiPcaMatrix = (Isi3x2FloatMatrix_t *)&Sensor_PCAMatrix;

    TRACE( Sensor_INFO, "%s: (exit)\n", __FUNCTION__);
    return ( result );
}



/*****************************************************************************/
/**
 *          Sensor_IsiGetCalibSvdMeanValue
 *
 * @brief   Returns the sensor specific SvdMean-Vector
 *
 * @param   handle              Sensor instance handle
 * @param   pIsiSvdMeanValue    Pointer to Pointer receiving the memory address
 *
 * @return  Return the result of the function call.
 * @retval  RET_SUCCESS
 * @retval  RET_WRONG_HANDLE
 * @retval  RET_NULL_POINTER
 *
 *****************************************************************************/
static RESULT Sensor_IsiGetCalibSvdMeanValue
(
    IsiSensorHandle_t   handle,
    Isi3x1FloatMatrix_t **pIsiSvdMeanValue
)
{
    IsiSensorContext_t *pSensorCtx = (IsiSensorContext_t *)handle;
    RESULT result = RET_SUCCESS;

    TRACE( Sensor_INFO, "%s: (enter)\n", __FUNCTION__);
    if ( pSensorCtx == NULL )
    {
        return ( RET_WRONG_HANDLE );
    }

    if ( pIsiSvdMeanValue == NULL )
    {
        return ( RET_NULL_POINTER );
    }

    //*pIsiSvdMeanValue = (Isi3x1FloatMatrix_t *)&Sensor_SVDMeanValue;

    TRACE( Sensor_INFO, "%s: (exit)\n", __FUNCTION__);
    return ( result );
}



/*****************************************************************************/
/**
 *          Sensor_IsiGetCalibSvdMeanValue
 *
 * @brief   Returns a pointer to the sensor specific centerline, a straight
 *          line in Hesse normal form in Rg/Bg colorspace
 *
 * @param   handle              Sensor instance handle
 * @param   pIsiSvdMeanValue    Pointer to Pointer receiving the memory address
 *
 * @return  Return the result of the function call.
 * @retval  RET_SUCCESS
 * @retval  RET_WRONG_HANDLE
 * @retval  RET_NULL_POINTER
 *
 *****************************************************************************/
static RESULT Sensor_IsiGetCalibCenterLine
(
    IsiSensorHandle_t   handle,
    IsiLine_t           **ptIsiCenterLine
)
{
    IsiSensorContext_t *pSensorCtx = (IsiSensorContext_t *)handle;
    RESULT result = RET_SUCCESS;

    TRACE( Sensor_INFO, "%s: (enter)\n", __FUNCTION__);
    if ( pSensorCtx == NULL )
    {
        return ( RET_WRONG_HANDLE );
    }

    if ( ptIsiCenterLine == NULL )
    {
        return ( RET_NULL_POINTER );
    }

    //*ptIsiCenterLine = (IsiLine_t*)&Sensor_CenterLine;

    TRACE( Sensor_INFO, "%s: (exit)\n", __FUNCTION__);
    return ( result );
}



/*****************************************************************************/
/**
 *          Sensor_IsiGetCalibClipParam
 *
 * @brief   Returns a pointer to the sensor specific arrays for Rg/Bg color
 *          space clipping
 *
 * @param   handle              Sensor instance handle
 * @param   pIsiSvdMeanValue    Pointer to Pointer receiving the memory address
 *
 * @return  Return the result of the function call.
 * @retval  RET_SUCCESS
 * @retval  RET_WRONG_HANDLE
 * @retval  RET_NULL_POINTER
 *
 *****************************************************************************/
static RESULT Sensor_IsiGetCalibClipParam
(
    IsiSensorHandle_t   handle,
    IsiAwbClipParm_t    **pIsiClipParam
)
{
    IsiSensorContext_t *pSensorCtx = (IsiSensorContext_t *)handle;
    RESULT result = RET_SUCCESS;

    TRACE( Sensor_INFO, "%s: (enter)\n", __FUNCTION__);
    if ( pSensorCtx == NULL )
    {
        return ( RET_WRONG_HANDLE );
    }

    if ( pIsiClipParam == NULL )
    {
        return ( RET_NULL_POINTER );
    }

    //*pIsiClipParam = (IsiAwbClipParm_t *)&Sensor_AwbClipParm;

    TRACE( Sensor_INFO, "%s: (exit)\n", __FUNCTION__);
    return ( result );
}



/*****************************************************************************/
/**
 *          Sensor_IsiGetCalibGlobalFadeParam
 *
 * @brief   Returns a pointer to the sensor specific arrays for AWB out of
 *          range handling
 *
 * @param   handle              Sensor instance handle
 * @param   pIsiSvdMeanValue    Pointer to Pointer receiving the memory address
 *
 * @return  Return the result of the function call.
 * @retval  RET_SUCCESS
 * @retval  RET_WRONG_HANDLE
 * @retval  RET_NULL_POINTER
 *
 *****************************************************************************/
static RESULT Sensor_IsiGetCalibGlobalFadeParam
(
    IsiSensorHandle_t       handle,
    IsiAwbGlobalFadeParm_t  **ptIsiGlobalFadeParam
)
{
    IsiSensorContext_t *pSensorCtx = (IsiSensorContext_t *)handle;
    RESULT result = RET_SUCCESS;

    TRACE( Sensor_INFO, "%s: (enter)\n", __FUNCTION__);
    if ( pSensorCtx == NULL )
    {
        return ( RET_WRONG_HANDLE );
    }

    if ( ptIsiGlobalFadeParam == NULL )
    {
        return ( RET_NULL_POINTER );
    }

    //*ptIsiGlobalFadeParam = (IsiAwbGlobalFadeParm_t *)&Sensor_AwbGlobalFadeParm;

    TRACE( Sensor_INFO, "%s: (exit)\n", __FUNCTION__);
    return ( result );
}



/*****************************************************************************/
/**
 *          Sensor_IsiGetCalibFadeParam
 *
 * @brief   Returns a pointer to the sensor specific arrays for near white
 *          pixel parameter calculations
 *
 * @param   handle              Sensor instance handle
 * @param   pIsiSvdMeanValue    Pointer to Pointer receiving the memory address
 *
 * @return  Return the result of the function call.
 * @retval  RET_SUCCESS
 * @retval  RET_WRONG_HANDLE
 * @retval  RET_NULL_POINTER
 *
 *****************************************************************************/
static RESULT Sensor_IsiGetCalibFadeParam
(
    IsiSensorHandle_t   handle,
    IsiAwbFade2Parm_t   **ptIsiFadeParam
)
{
    IsiSensorContext_t *pSensorCtx = (IsiSensorContext_t *)handle;
    RESULT result = RET_SUCCESS;

    TRACE( Sensor_INFO, "%s: (enter)\n", __FUNCTION__);
    if ( pSensorCtx == NULL )
    {
        return ( RET_WRONG_HANDLE );
    }

    if ( ptIsiFadeParam == NULL )
    {
        return ( RET_NULL_POINTER );
    }

    //*ptIsiFadeParam = (IsiAwbFade2Parm_t *)&Sensor_AwbFade2Parm;

    TRACE( Sensor_INFO, "%s: (exit)\n", __FUNCTION__);
    return ( result );
}

/*****************************************************************************/
/**
 *          Sensor_IsiGetIlluProfile
 *
 * @brief   Returns a pointer to illumination profile idetified by CieProfile
 *          bitmask
 *
 * @param   handle              sensor instance handle
 * @param   CieProfile
 * @param   ptIsiIlluProfile    Pointer to Pointer receiving the memory address
 *
 * @return  Return the result of the function call.
 * @retval  RET_SUCCESS
 * @retval  RET_WRONG_HANDLE
 * @retval  RET_NULL_POINTER
 *
 *****************************************************************************/
static RESULT Sensor_IsiGetIlluProfile
(
    IsiSensorHandle_t   handle,
    const uint32_t      CieProfile,
    IsiIlluProfile_t    **ptIsiIlluProfile
)
{
    Sensor_Context_t *pSensorCtx = (Sensor_Context_t *)handle;
    RESULT result = RET_SUCCESS;

    TRACE( Sensor_INFO, "%s: (enter)\n", __FUNCTION__);
    if ( pSensorCtx == NULL )
    {
        return ( RET_WRONG_HANDLE );
    }

    if ( ptIsiIlluProfile == NULL )
    {
        return ( RET_NULL_POINTER );
    }
    else
    {
    }

    TRACE( Sensor_INFO, "%s: (exit)\n", __FUNCTION__);
    return ( result );
}



/*****************************************************************************/
/**
 *          Sensor_IsiGetLscMatrixTable
 *
 * @brief   Returns a pointer to illumination profile idetified by CieProfile
 *          bitmask
 *
 * @param   handle              sensor instance handle
 * @param   CieProfile
 * @param   ptIsiIlluProfile    Pointer to Pointer receiving the memory address
 *
 * @return  Return the result of the function call.
 * @retval  RET_SUCCESS
 * @retval  RET_WRONG_HANDLE
 * @retval  RET_NULL_POINTER
 *
 *****************************************************************************/
static RESULT Sensor_IsiGetLscMatrixTable
(
    IsiSensorHandle_t   handle,
    const uint32_t      CieProfile,
    IsiLscMatrixTable_t **pLscMatrixTable
)
{
    Sensor_Context_t *pSensorCtx = (Sensor_Context_t *)handle;
    RESULT result = RET_SUCCESS;

    TRACE( Sensor_INFO, "%s: (enter)\n", __FUNCTION__);
    if ( pSensorCtx == NULL )
    {
        return ( RET_WRONG_HANDLE );
    }

    if ( pLscMatrixTable == NULL )
    {
        return ( RET_NULL_POINTER );
    }
    else
    {
    }

    TRACE( Sensor_INFO, "%s: (exit)\n", __FUNCTION__);
    return ( result );
}


/*****************************************************************************/
/**
 *          Sensor_IsiMdiInitMotoDriveMds
 *
 * @brief   General initialisation tasks like I/O initialisation.
 *
 * @param   handle              Sensor instance handle
 *
 * @return  Return the result of the function call.
 * @retval  RET_SUCCESS
 * @retval  RET_WRONG_HANDLE
 * @retval  RET_NULL_POINTER
 *
 *****************************************************************************/
static RESULT Sensor_IsiMdiInitMotoDriveMds
(
    IsiSensorHandle_t   handle
)
{
    Sensor_Context_t *pSensorCtx = (Sensor_Context_t *)handle;
    RESULT result = RET_SUCCESS;

    TRACE( Sensor_INFO, "%s: (enter)\n", __FUNCTION__);
    if ( pSensorCtx == NULL )
    {
        return ( RET_WRONG_HANDLE );
    }

    TRACE( Sensor_INFO, "%s: (exit)\n", __FUNCTION__);
    return ( result );
}



/*****************************************************************************/
/**
 *          Sensor_IsiMdiSetupMotoDrive
 *
 * @brief   Setup of the MotoDrive and return possible max step.
 *
 * @param   handle          Sensor instance handle
 *          pMaxStep        pointer to variable to receive the maximum
 *                          possible focus step
 *
 * @return  Return the result of the function call.
 * @retval  RET_SUCCESS
 * @retval  RET_WRONG_HANDLE
 * @retval  RET_NULL_POINTER
 *
 *****************************************************************************/
static RESULT Sensor_IsiMdiSetupMotoDrive
(
    IsiSensorHandle_t   handle,
    uint32_t            *pMaxStep
)
{
    Sensor_Context_t *pSensorCtx = (Sensor_Context_t *)handle;
	uint32_t vcm_movefull_t;
    RESULT result = RET_SUCCESS;
	uint8_t data = 0;
	
    #if 0
    TRACE( Sensor_INFO, "%s: (enter)\n", __FUNCTION__);

    if ( pSensorCtx == NULL )
    {
        return ( RET_WRONG_HANDLE );
    }

    if ( pMaxStep == NULL )
    {
        return ( RET_NULL_POINTER );
    }

	//dw9718s
	data = 0x00;
	result = HalWriteI2CMem( pSensorCtx->IsiCtx.HalHandle,
                             pSensorCtx->IsiCtx.I2cAfBusNum,
                             pSensorCtx->IsiCtx.SlaveAfAddress,
                             0x00,
                             pSensorCtx->IsiCtx.NrOfAfAddressBytes,
                             &data,
                             1U );
    RETURN_RESULT_IF_DIFFERENT( RET_SUCCESS, result );
	
	usleep(100);
	
	data = 0x39;
	result = HalWriteI2CMem( pSensorCtx->IsiCtx.HalHandle,
                             pSensorCtx->IsiCtx.I2cAfBusNum,
                             pSensorCtx->IsiCtx.SlaveAfAddress,
                             0x01,
                             pSensorCtx->IsiCtx.NrOfAfAddressBytes,
                             &data,
                             1U );
    RETURN_RESULT_IF_DIFFERENT( RET_SUCCESS, result );


	data = 0x65;
	result = HalWriteI2CMem( pSensorCtx->IsiCtx.HalHandle,
                             pSensorCtx->IsiCtx.I2cAfBusNum,
                             pSensorCtx->IsiCtx.SlaveAfAddress,
                             0x05,
                             pSensorCtx->IsiCtx.NrOfAfAddressBytes,
                             &data,
                             1U );
    RETURN_RESULT_IF_DIFFERENT( RET_SUCCESS, result );
	usleep(500);
	
	result = Sensor_IsiMdiFocusSet( handle, MAX_LOG);	
	vcm_movefull_t = 50;
	*pMaxStep = (MAX_LOG|(vcm_movefull_t<<16));

    #endif


    
    return ( result );
}



/*****************************************************************************/
/**
 *          Sensor_IsiMdiFocusSet
 *
 * @brief   Drives the lens system to a certain focus point.
 *
 * @param   handle          Sensor instance handle
 *          AbsStep         absolute focus point to apply
 *
 * @return  Return the result of the function call.
 * @retval  RET_SUCCESS
 * @retval  RET_WRONG_HANDLE
 * @retval  RET_NULL_POINTER
 *
 *****************************************************************************/
static RESULT Sensor_IsiMdiFocusSet
(
    IsiSensorHandle_t   handle,
    const uint32_t      Position
)
{
	Sensor_Context_t *pSensorCtx = (Sensor_Context_t *)handle;
    RESULT result = RET_SUCCESS;

    
    #if 0
    uint32_t nPosition;
    uint8_t  data[2] = { 0, 0 };

    TRACE( Sensor_INFO, "%s: (enter)\n", __FUNCTION__);

    if ( pSensorCtx == NULL )
    {
    	TRACE( Sensor_ERROR, "%s: pSensorCtx IS NULL\n", __FUNCTION__);
        return ( RET_WRONG_HANDLE );
    }

	//TRACE( Sensor_ERROR, "%s: pSensorCtx Position (%d) max_position(%d)\n", __FUNCTION__,Position, MAX_LOG);
    /* map 64 to 0 -> infinity */
    //nPosition = ( Position >= MAX_LOG ) ? 0 : ( MAX_REG - (Position * 16U) );
	if( Position > MAX_LOG ){
		TRACE( Sensor_ERROR, "%s: pSensorCtx Position (%d) max_position(%d)\n", __FUNCTION__,Position, MAX_LOG);
		//Position = MAX_LOG;
	}
	/* ddl@rock-chips.com: v0.3.0 */
    if ( Position >= MAX_LOG )
        nPosition = pSensorCtx->VcmInfo.StartCurrent;
    else 
        nPosition = pSensorCtx->VcmInfo.StartCurrent + (pSensorCtx->VcmInfo.Step*(MAX_LOG-Position));
    /* ddl@rock-chips.com: v0.6.0 */
    if (nPosition > MAX_VCMDRV_REG)  
        nPosition = MAX_VCMDRV_REG;
    
	data[0] = (uint8_t)(0x00U | (( nPosition & 0xF00U ) >> 8U));                 
	data[1] = (uint8_t)( nPosition & 0xFFU );

    //TRACE( Sensor_ERROR, "%s:  dw9718s value = %d, 0x%02x 0x%02x af_addr(0x%x) bus(%d)\n", __FUNCTION__, nPosition, data[0], data[1],pSensorCtx->IsiCtx.SlaveAfAddress,pSensorCtx->IsiCtx.I2cAfBusNum );

    result = HalWriteI2CMem( pSensorCtx->IsiCtx.HalHandle,
                             pSensorCtx->IsiCtx.I2cAfBusNum,
                             pSensorCtx->IsiCtx.SlaveAfAddress,
                             0x02,
                             pSensorCtx->IsiCtx.NrOfAfAddressBytes,
                             data,
                             2U );
	RETURN_RESULT_IF_DIFFERENT( RET_SUCCESS, result );

    TRACE( Sensor_INFO, "%s: (exit)\n", __FUNCTION__);
    #endif
    
    return ( result );
}



/*****************************************************************************/
/**
 *          Sensor_IsiMdiFocusGet
 *
 * @brief   Retrieves the currently applied focus point.
 *
 * @param   handle          Sensor instance handle
 *          pAbsStep        pointer to a variable to receive the current
 *                          focus point
 *
 * @return  Return the result of the function call.
 * @retval  RET_SUCCESS
 * @retval  RET_WRONG_HANDLE
 * @retval  RET_NULL_POINTER
 *
 *****************************************************************************/
static RESULT Sensor_IsiMdiFocusGet
(
    IsiSensorHandle_t   handle,
    uint32_t            *pAbsStep
)
{
    Sensor_Context_t *pSensorCtx = (Sensor_Context_t *)handle;

    RESULT result = RET_SUCCESS;
    uint8_t  data[2] = { 0, 0 };

    #if 0
    TRACE( Sensor_INFO, "%s: (enter)\n", __FUNCTION__);

    if ( pSensorCtx == NULL )
    {
        return ( RET_WRONG_HANDLE );
    }

    if ( pAbsStep == NULL )
    {
        return ( RET_NULL_POINTER );
    }

	result = HalReadI2CMem( pSensorCtx->IsiCtx.HalHandle,
					 pSensorCtx->IsiCtx.I2cAfBusNum,
					 pSensorCtx->IsiCtx.SlaveAfAddress,
					 0x02,
					 pSensorCtx->IsiCtx.NrOfAfAddressBytes,
					 &data[0],
					 1U );
    RETURN_RESULT_IF_DIFFERENT( RET_SUCCESS, result );


	result = HalReadI2CMem( pSensorCtx->IsiCtx.HalHandle,
						 pSensorCtx->IsiCtx.I2cAfBusNum,
						 pSensorCtx->IsiCtx.SlaveAfAddress,
						 0x03,
						 pSensorCtx->IsiCtx.NrOfAfAddressBytes,
						 &data[1],
						 1U );
	RETURN_RESULT_IF_DIFFERENT( RET_SUCCESS, result );
	
	*pAbsStep = ( ((uint32_t)(data[0] & 0x0F)) << 8U ) | ( ((uint32_t)data[1]) & 0xff);
	TRACE( Sensor_ERROR, "%s: dw9718s value = 0x%02x 0x%02x\n", __FUNCTION__, data[0], data[1]);


    /* map 0 to 64 -> infinity */	 /* ddl@rock-chips.com: v0.3.0 */
	 if( *pAbsStep <= pSensorCtx->VcmInfo.StartCurrent)
	 {
		 *pAbsStep = MAX_LOG;
	 }
	 else if((*pAbsStep>pSensorCtx->VcmInfo.StartCurrent) && (*pAbsStep<=pSensorCtx->VcmInfo.RatedCurrent))
	 {
		 *pAbsStep = (pSensorCtx->VcmInfo.RatedCurrent - *pAbsStep ) / pSensorCtx->VcmInfo.Step;
	 }
	 else
	 {
		 *pAbsStep = 0;
	 }
	 
    TRACE( Sensor_INFO, "%s: (exit)\n", __FUNCTION__);

    #endif
	*pAbsStep = 0;
    return ( result );
}



/*****************************************************************************/
/**
 *          Sensor_IsiMdiFocusCalibrate
 *
 * @brief   Triggers a forced calibration of the focus hardware.
 *
 * @param   handle          Sensor instance handle
 *
 * @return  Return the result of the function call.
 * @retval  RET_SUCCESS
 * @retval  RET_WRONG_HANDLE
 * @retval  RET_NULL_POINTER
 *
 *****************************************************************************/
static RESULT Sensor_IsiMdiFocusCalibrate
(
    IsiSensorHandle_t   handle
)
{
    Sensor_Context_t *pSensorCtx = (Sensor_Context_t *)handle;
    RESULT result = RET_SUCCESS;

    #if 1
    TRACE( Sensor_INFO, "%s: (enter)\n", __FUNCTION__);

    if ( pSensorCtx == NULL )
    {
        return ( RET_WRONG_HANDLE );
    }

    TRACE( Sensor_INFO, "%s: (exit)\n", __FUNCTION__);
    #endif
    
    return ( result );
}



/*****************************************************************************/
/**
 *          Sensor_IsiActivateTestPattern
 *
 * @brief   Triggers a forced calibration of the focus hardware.
 *
 * @param   handle          Sensor instance handle
 *
 * @return  Return the result of the function call.
 * @retval  RET_SUCCESS
 * @retval  RET_WRONG_HANDLE
 * @retval  RET_NULL_POINTER
 *
 ******************************************************************************/
static RESULT Sensor_IsiActivateTestPattern
(
    IsiSensorHandle_t   handle,
    const bool_t        enable
)
{
    Sensor_Context_t *pSensorCtx = (Sensor_Context_t *)handle;

    RESULT result = RET_SUCCESS;

    #if 0
    uint32_t ulRegValue = 0UL;

    TRACE( Sensor_INFO, "%s: (enter)\n", __FUNCTION__);

    if ( pSensorCtx == NULL )
    {
        return ( RET_WRONG_HANDLE );
    }

    if ( BOOL_TRUE == enable )
    {
        /* enable test-pattern */
        result = Sensor_IsiRegReadIss( pSensorCtx, 0x5e00, &ulRegValue );
        RETURN_RESULT_IF_DIFFERENT( RET_SUCCESS, result );

        ulRegValue |= ( 0x80U );

        result = Sensor_IsiRegWriteIss( pSensorCtx, 0x5e00, ulRegValue );
        RETURN_RESULT_IF_DIFFERENT( RET_SUCCESS, result );
    }
    else
    {
        /* disable test-pattern */
        result = Sensor_IsiRegReadIss( pSensorCtx, 0x5e00, &ulRegValue );
        RETURN_RESULT_IF_DIFFERENT( RET_SUCCESS, result );

        ulRegValue &= ~( 0x80 );

        result = Sensor_IsiRegWriteIss( pSensorCtx, 0x5e00, ulRegValue );
        RETURN_RESULT_IF_DIFFERENT( RET_SUCCESS, result );
    }

     pSensorCtx->TestPattern = enable;
    TRACE( Sensor_INFO, "%s: (exit)\n", __FUNCTION__);

    #endif
    return ( result );
}



/*****************************************************************************/
/**
 *          Sensor_IsiGetSensorMipiInfoIss
 *
 * @brief   Triggers a forced calibration of the focus hardware.
 *
 * @param   handle          Sensor instance handle
 *
 * @return  Return the result of the function call.
 * @retval  RET_SUCCESS
 * @retval  RET_WRONG_HANDLE
 * @retval  RET_NULL_POINTER
 *
 ******************************************************************************/
static RESULT Sensor_IsiGetSensorMipiInfoIss
(
    IsiSensorHandle_t   handle,
    IsiSensorMipiInfo   *ptIsiSensorMipiInfo
)
{
    Sensor_Context_t *pSensorCtx = (Sensor_Context_t *)handle;
    RESULT result = RET_SUCCESS;

    TRACE( Sensor_INFO, "%s: (enter)\n", __FUNCTION__);
    if ( pSensorCtx == NULL )
    {
        return ( RET_WRONG_HANDLE );
    }

    if ( ptIsiSensorMipiInfo == NULL )
    {
        return ( result );
    }

    ptIsiSensorMipiInfo->ucMipiLanes = pSensorCtx->IsiSensorMipiInfo.ucMipiLanes;
    ptIsiSensorMipiInfo->ulMipiFreq= pSensorCtx->IsiSensorMipiInfo.ulMipiFreq;
    ptIsiSensorMipiInfo->sensorHalDevID = pSensorCtx->IsiSensorMipiInfo.sensorHalDevID;

    TRACE( Sensor_INFO, "%s: (exit)\n", __FUNCTION__);
    return ( result );
}

static RESULT Sensor_IsiGetSensorIsiVersion
(  IsiSensorHandle_t   handle,
   unsigned int*     pVersion
)
{
    Sensor_Context_t *pSensorCtx = (Sensor_Context_t *)handle;
    RESULT result = RET_SUCCESS;

    TRACE( Sensor_INFO, "%s: (enter)\n", __FUNCTION__);

    if ( pSensorCtx == NULL )
    {
    	TRACE( Sensor_ERROR, "%s: pSensorCtx IS NULL\n", __FUNCTION__);
        return ( RET_WRONG_HANDLE );
    }

	if(pVersion == NULL)
	{
		TRACE( Sensor_ERROR, "%s: pVersion IS NULL\n", __FUNCTION__);
        return ( RET_WRONG_HANDLE );
	}

	*pVersion = CONFIG_ISI_VERSION;
	return result;
}

static RESULT Sensor_IsiGetSensorTuningXmlVersion
(  IsiSensorHandle_t   handle,
   char**     pTuningXmlVersion
)
{
    Sensor_Context_t *pSensorCtx = (Sensor_Context_t *)handle;
    RESULT result = RET_SUCCESS;

    TRACE( Sensor_INFO, "%s: (enter)\n", __FUNCTION__);
    if ( pSensorCtx == NULL )
    {
    	TRACE( Sensor_ERROR, "%s: pSensorCtx IS NULL\n", __FUNCTION__);
        return ( RET_WRONG_HANDLE );
    }

	if(pTuningXmlVersion == NULL)
	{
		TRACE( Sensor_ERROR, "%s: pVersion IS NULL\n", __FUNCTION__);
        return ( RET_WRONG_HANDLE );
	}

	*pTuningXmlVersion = Sensor_NEWEST_TUNING_XML;
	return result;
}


/*****************************************************************************/
/**
 *          Sensor_IsiGetSensorIss
 *
 * @brief   fills in the correct pointers for the sensor description struct
 *
 * @param   param1      pointer to sensor description struct
 *
 * @return  Return the result of the function call.
 * @retval  RET_SUCCESS
 * @retval  RET_NULL_POINTER
 *
 *****************************************************************************/
RESULT Sensor_IsiGetSensorIss
(
    IsiSensor_t *pIsiSensor
)
{
    RESULT result = RET_SUCCESS;

    TRACE( Sensor_INFO, "%s: (enter)\n", __FUNCTION__);

    if ( pIsiSensor != NULL )
    {
        pIsiSensor->pszName                             = Sensor_g_acName;
        pIsiSensor->pRegisterTable                      = Sensor_IMX290_g_aRegDescription_fourlane;
        pIsiSensor->pIsiSensorCaps                      = &Sensor_g_IsiSensorDefaultConfig;
		pIsiSensor->pIsiGetSensorIsiVer					= Sensor_IsiGetSensorIsiVersion;//oyyf
		pIsiSensor->pIsiGetSensorTuningXmlVersion		= Sensor_IsiGetSensorTuningXmlVersion;//oyyf
        pIsiSensor->pIsiCreateSensorIss                 = Sensor_IsiCreateSensorIss;
        pIsiSensor->pIsiReleaseSensorIss                = Sensor_IsiReleaseSensorIss;
        pIsiSensor->pIsiGetCapsIss                      = Sensor_IsiGetCapsIss;
        pIsiSensor->pIsiSetupSensorIss                  = Sensor_IsiSetupSensorIss;
        pIsiSensor->pIsiChangeSensorResolutionIss       = Sensor_IsiChangeSensorResolutionIss;
        pIsiSensor->pIsiSensorSetStreamingIss           = Sensor_IsiSensorSetStreamingIss;
        pIsiSensor->pIsiSensorSetPowerIss               = Sensor_IsiSensorSetPowerIss;
        pIsiSensor->pIsiCheckSensorConnectionIss        = Sensor_IsiCheckSensorConnectionIss;
        pIsiSensor->pIsiGetSensorRevisionIss            = Sensor_IsiGetSensorRevisionIss;
        pIsiSensor->pIsiRegisterReadIss                 = Sensor_IsiRegReadIss;
        pIsiSensor->pIsiRegisterWriteIss                = Sensor_IsiRegWriteIss;

        /* AEC functions */
        pIsiSensor->pIsiExposureControlIss              = Sensor_IsiExposureControlIss;
        pIsiSensor->pIsiGetGainLimitsIss                = Sensor_IsiGetGainLimitsIss;
        pIsiSensor->pIsiGetIntegrationTimeLimitsIss     = Sensor_IsiGetIntegrationTimeLimitsIss;
        pIsiSensor->pIsiGetCurrentExposureIss           = Sensor_IsiGetCurrentExposureIss;
        pIsiSensor->pIsiGetGainIss                      = Sensor_IsiGetGainIss;
        pIsiSensor->pIsiGetGainIncrementIss             = Sensor_IsiGetGainIncrementIss;
        pIsiSensor->pIsiSetGainIss                      = Sensor_IsiSetGainIss;
        pIsiSensor->pIsiGetIntegrationTimeIss           = Sensor_IsiGetIntegrationTimeIss;
        pIsiSensor->pIsiGetIntegrationTimeIncrementIss  = Sensor_IsiGetIntegrationTimeIncrementIss;
        pIsiSensor->pIsiSetIntegrationTimeIss           = Sensor_IsiSetIntegrationTimeIss;
        pIsiSensor->pIsiGetResolutionIss                = Sensor_IsiGetResolutionIss;
        pIsiSensor->pIsiGetAfpsInfoIss                  = Sensor_IsiGetAfpsInfoIss;

        /* AWB specific functions */
        pIsiSensor->pIsiGetCalibKFactor                 = Sensor_IsiGetCalibKFactor;
        pIsiSensor->pIsiGetCalibPcaMatrix               = Sensor_IsiGetCalibPcaMatrix;
        pIsiSensor->pIsiGetCalibSvdMeanValue            = Sensor_IsiGetCalibSvdMeanValue;
        pIsiSensor->pIsiGetCalibCenterLine              = Sensor_IsiGetCalibCenterLine;
        pIsiSensor->pIsiGetCalibClipParam               = Sensor_IsiGetCalibClipParam;
        pIsiSensor->pIsiGetCalibGlobalFadeParam         = Sensor_IsiGetCalibGlobalFadeParam;
        pIsiSensor->pIsiGetCalibFadeParam               = Sensor_IsiGetCalibFadeParam;
        pIsiSensor->pIsiGetIlluProfile                  = Sensor_IsiGetIlluProfile;
        pIsiSensor->pIsiGetLscMatrixTable               = Sensor_IsiGetLscMatrixTable;

        /* AF functions */
        pIsiSensor->pIsiMdiInitMotoDriveMds             = Sensor_IsiMdiInitMotoDriveMds;
        pIsiSensor->pIsiMdiSetupMotoDrive               = Sensor_IsiMdiSetupMotoDrive;
        pIsiSensor->pIsiMdiFocusSet                     = Sensor_IsiMdiFocusSet;
        pIsiSensor->pIsiMdiFocusGet                     = Sensor_IsiMdiFocusGet;
        pIsiSensor->pIsiMdiFocusCalibrate               = Sensor_IsiMdiFocusCalibrate;

        /* MIPI */
        pIsiSensor->pIsiGetSensorMipiInfoIss            = Sensor_IsiGetSensorMipiInfoIss;

        /* Testpattern */
        pIsiSensor->pIsiActivateTestPattern             = Sensor_IsiActivateTestPattern;
		pIsiSensor->pIsiSetSensorFrameRateLimit			= Sensor_IsiSetSensorFrameRateLimit;
    }
    else
    {
        result = RET_NULL_POINTER;
    }

    TRACE( Sensor_INFO, "%s (exit)\n", __FUNCTION__);

    return ( result );
}

static RESULT Sensor_IsiGetSensorI2cInfo(sensor_i2c_info_t** pdata)
{
    sensor_i2c_info_t* pSensorI2cInfo;
    pSensorI2cInfo = ( sensor_i2c_info_t * )malloc ( sizeof (sensor_i2c_info_t) );

    if ( pSensorI2cInfo == NULL )
    {
        TRACE( Sensor_ERROR,  "%s: Can't allocate ov14825 context\n",  __FUNCTION__ );
        return ( RET_OUTOFMEM );
    }
    MEMSET( pSensorI2cInfo, 0, sizeof( sensor_i2c_info_t ) );
    
    
    pSensorI2cInfo->i2c_addr = Sensor_SLAVE_ADDR;
    pSensorI2cInfo->i2c_addr2 = Sensor_SLAVE_ADDR2;
    pSensorI2cInfo->soft_reg_addr = Sensor_SOFTWARE_RST;
    pSensorI2cInfo->soft_reg_value = 0x01;
    pSensorI2cInfo->reg_size = 2;
    pSensorI2cInfo->value_size = 1;

    {
        IsiSensorCaps_t Caps;
        sensor_caps_t *pCaps;
        uint32_t lanes,i;        

        for (i=0; i<3; i++) {
            lanes = (1<<i);
            ListInit(&pSensorI2cInfo->lane_res[i]);
            if (g_suppoted_mipi_lanenum_type & lanes) {
                Caps.Index = 0;            
                while(Sensor_IsiGetCapsIssInternal(&Caps,lanes)==RET_SUCCESS) {
                    pCaps = malloc(sizeof(sensor_caps_t));
                    if (pCaps != NULL) {
                        memcpy(&pCaps->caps,&Caps,sizeof(IsiSensorCaps_t));
                        ListPrepareItem(pCaps);
                        ListAddTail(&pSensorI2cInfo->lane_res[i], pCaps);
                    }
                    Caps.Index++;
                }
            }
        }
    }
    
    ListInit(&pSensorI2cInfo->chipid_info);

    sensor_chipid_info_t* pChipIDInfo_H = (sensor_chipid_info_t *) malloc( sizeof(sensor_chipid_info_t) );
    if ( !pChipIDInfo_H )
    {
        return RET_OUTOFMEM;
    }
    MEMSET( pChipIDInfo_H, 0, sizeof(*pChipIDInfo_H) );    
    pChipIDInfo_H->chipid_reg_addr = Sensor_CHIP_ID_HIGH_BYTE;  
    pChipIDInfo_H->chipid_reg_value = Sensor_CHIP_ID_HIGH_BYTE_DEFAULT;
    ListPrepareItem( pChipIDInfo_H );
    ListAddTail( &pSensorI2cInfo->chipid_info, pChipIDInfo_H );

    sensor_chipid_info_t* pChipIDInfo_L = (sensor_chipid_info_t *) malloc( sizeof(sensor_chipid_info_t) );
    if ( !pChipIDInfo_L )
    {
        return RET_OUTOFMEM;
    }
    MEMSET( pChipIDInfo_L, 0, sizeof(*pChipIDInfo_L) ); 
    pChipIDInfo_L->chipid_reg_addr = Sensor_CHIP_ID_LOW_BYTE;
    pChipIDInfo_L->chipid_reg_value = Sensor_CHIP_ID_LOW_BYTE_DEFAULT;
    ListPrepareItem( pChipIDInfo_L );
    ListAddTail( &pSensorI2cInfo->chipid_info, pChipIDInfo_L );

	//oyyf sensor drv version
	pSensorI2cInfo->sensor_drv_version = CONFIG_SENSOR_DRV_VERSION;
	
    *pdata = pSensorI2cInfo;
    return RET_SUCCESS;
}

static RESULT Sensor_IsiSetSensorFrameRateLimit(IsiSensorHandle_t handle, uint32_t minimum_framerate)
{
    Sensor_Context_t *pSensorCtx = (Sensor_Context_t *)handle;

    TRACE( Sensor_ERROR, "%s: (enter)\n", __FUNCTION__);
    if ( pSensorCtx == NULL ){
		TRACE( Sensor_ERROR, "%s: pSensorCtx IS NULL\n", __FUNCTION__);
        return ( RET_WRONG_HANDLE );
    }

	pSensorCtx->preview_minimum_framerate = minimum_framerate;
	return RET_SUCCESS;
}


/******************************************************************************
 * See header file for detailed comment.
 *****************************************************************************/


/*****************************************************************************/
/**
 */
/*****************************************************************************/
IsiCamDrvConfig_t IsiCamDrvConfig =
{
    0,
    Sensor_IsiGetSensorIss,
    {
        0,                      /**< IsiSensor_t.pszName */
        0,                      /**< IsiSensor_t.pRegisterTable */
        0,                      /**< IsiSensor_t.pIsiSensorCaps */
        0,						/**< IsiSensor_t.pIsiGetSensorIsiVer_t>*/   //oyyf add
        0,                      /**< IsiSensor_t.pIsiGetSensorTuningXmlVersion_t>*/   //oyyf add 
        0,                      /**< IsiSensor_t.pIsiWhiteBalanceIlluminationChk>*/   //ddl@rock-chips.com 
        0,                      /**< IsiSensor_t.pIsiWhiteBalanceIlluminationSet>*/   //ddl@rock-chips.com
        0,                      /**< IsiSensor_t.pIsiCheckOTPInfo>*/  //zyc
        0,						/**< IsiSensor_t.pIsiSetSensorOTPInfo>*/  //zyl
        0,						/**< IsiSensor_t.pIsiEnableSensorOTP>*/  //zyl
        0,                      /**< IsiSensor_t.pIsiCreateSensorIss */
        0,                      /**< IsiSensor_t.pIsiReleaseSensorIss */
        0,                      /**< IsiSensor_t.pIsiGetCapsIss */
        0,                      /**< IsiSensor_t.pIsiSetupSensorIss */
        0,                      /**< IsiSensor_t.pIsiChangeSensorResolutionIss */
        0,                      /**< IsiSensor_t.pIsiSensorSetStreamingIss */
        0,                      /**< IsiSensor_t.pIsiSensorSetPowerIss */
        0,                      /**< IsiSensor_t.pIsiCheckSensorConnectionIss */
        0,                      /**< IsiSensor_t.pIsiGetSensorRevisionIss */
        0,                      /**< IsiSensor_t.pIsiRegisterReadIss */
        0,                      /**< IsiSensor_t.pIsiRegisterWriteIss */

        0,                      /**< IsiSensor_t.pIsiIsEvenFieldIss */
        0,                      /**< IsiSensor_t.pIsiExposureControlIss */
        0,                      /**< IsiSensor_t.pIsiGetGainLimitsIss */
        0,                      /**< IsiSensor_t.pIsiGetIntegrationTimeLimitsIss */
        0,                      /**< IsiSensor_t.pIsiGetCurrentExposureIss */
        0,                      /**< IsiSensor_t.pIsiGetGainIss */
        0,                      /**< IsiSensor_t.pIsiGetGainIncrementIss */
        0,                      /**< IsiSensor_t.pIsiSetGainIss */
        0,                      /**< IsiSensor_t.pIsiGetIntegrationTimeIss */
        0,                      /**< IsiSensor_t.pIsiGetIntegrationTimeIncrementIss */
        0,                      /**< IsiSensor_t.pIsiSetIntegrationTimeIss */
        0,                      /**< IsiSensor_t.pIsiGetResolutionIss */
        0,                      /**< IsiSensor_t.pIsiGetAfpsInfoIss */

        0,                      /**< IsiSensor_t.pIsiGetCalibKFactor */
        0,                      /**< IsiSensor_t.pIsiGetCalibPcaMatrix */
        0,                      /**< IsiSensor_t.pIsiGetCalibSvdMeanValue */
        0,                      /**< IsiSensor_t.pIsiGetCalibCenterLine */
        0,                      /**< IsiSensor_t.pIsiGetCalibClipParam */
        0,                      /**< IsiSensor_t.pIsiGetCalibGlobalFadeParam */
        0,                      /**< IsiSensor_t.pIsiGetCalibFadeParam */
        0,                      /**< IsiSensor_t.pIsiGetIlluProfile */
        0,                      /**< IsiSensor_t.pIsiGetLscMatrixTable */

        0,                      /**< IsiSensor_t.pIsiMdiInitMotoDriveMds */
        0,                      /**< IsiSensor_t.pIsiMdiSetupMotoDrive */
        0,                      /**< IsiSensor_t.pIsiMdiFocusSet */
        0,                      /**< IsiSensor_t.pIsiMdiFocusGet */
        0,                      /**< IsiSensor_t.pIsiMdiFocusCalibrate */

        0,                      /**< IsiSensor_t.pIsiGetSensorMipiInfoIss */

        0,                      /**< IsiSensor_t.pIsiActivateTestPattern */
        0,
        0,						/**< IsiSensor_t.pIsiGetColorIss */
    },
    Sensor_IsiGetSensorI2cInfo,
};



