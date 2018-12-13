//OV9750_tables.c
/*****************************************************************************/
/*!
 *  \file        OV9750_tables.c \n
 *  \version     1.0 \n
 *  \author      Meinicke \n
 *  \brief       Image-sensor-specific tables and other
 *               constant values/structures for OV13850. \n
 *
 *  \revision    $Revision: 803 $ \n
 *               $Author: $ \n
 *               $Date: 2010-02-26 16:35:22 +0100 (Fr, 26 Feb 2010) $ \n
 *               $Id: OV13850_tables.c 803 2010-02-26 15:35:22Z  $ \n
 */
/*  This is an unpublished work, the copyright in which vests in Silicon Image
 *  GmbH. The information contained herein is the property of Silicon Image GmbH
 *  and is supplied without liability for errors or omissions. No part may be
 *  reproduced or used expect as authorized by contract or other written
 *  permission. Copyright(c) Silicon Image GmbH, 2009, all rights reserved.
 */
/*****************************************************************************/
/*
#include "stdinc.h"

#if( OV9750_DRIVER_USAGE == USE_CAM_DRV_EN )
*/


#include <ebase/types.h>
#include <ebase/trace.h>
#include <ebase/builtins.h>

#include <common/return_codes.h>

#include "isi.h"
#include "isi_iss.h"
#include "isi_priv.h"
#include "OV9750_MIPI_priv.h"


/*****************************************************************************
 * DEFINES
 *****************************************************************************/


/*****************************************************************************
 * GLOBALS
 *****************************************************************************/

// Image sensor register settings default values taken from data sheet OV13850_DS_1.1_SiliconImage.pdf.
// The settings may be altered by the code in IsiSetupSensor.

//one lane
const IsiRegDescription_t Sensor_g_aRegDescription[] =
{
{0x0103, 0x01, "eReadWrite",eReadWrite},
{0x0000, 0x10, "eDelay",eReadWrite},
{0x0100, 0x00, "eReadWrite",eReadWrite},
{0x0000, 0x10, "eDelay",eReadWrite},
{0x0300, 0x04, "eReadWrite",eReadWrite},
{0x0302, 0x64, "eReadWrite",eReadWrite},
{0x0303, 0x00, "eReadWrite",eReadWrite},
{0x0304, 0x03, "eReadWrite",eReadWrite},
{0x0305, 0x01, "eReadWrite",eReadWrite},
{0x0306, 0x01, "eReadWrite",eReadWrite},
{0x030a, 0x00, "eReadWrite",eReadWrite},
{0x030b, 0x00, "eReadWrite",eReadWrite},
{0x030d, 0x1e, "eReadWrite",eReadWrite},
{0x030e, 0x01, "eReadWrite",eReadWrite},
{0x030f, 0x04, "eReadWrite",eReadWrite},
{0x0312, 0x01, "eReadWrite",eReadWrite},
{0x031e, 0x04, "eReadWrite",eReadWrite},
{0x3000, 0x00, "eReadWrite",eReadWrite},
{0x3001, 0x00, "eReadWrite",eReadWrite},
{0x3002, 0x21, "eReadWrite",eReadWrite},
{0x3005, 0xf0, "eReadWrite",eReadWrite},
{0x3011, 0x00, "eReadWrite",eReadWrite},
{0x3016, 0x53, "eReadWrite",eReadWrite},
{0x3018, 0x32, "eReadWrite",eReadWrite},
{0x301a, 0xf0, "eReadWrite",eReadWrite},
{0x301b, 0xf0, "eReadWrite",eReadWrite},
{0x301c, 0xf0, "eReadWrite",eReadWrite},
{0x301d, 0xf0, "eReadWrite",eReadWrite},
{0x301e, 0xf0, "eReadWrite",eReadWrite},
{0x3022, 0x01, "eReadWrite",eReadWrite},
{0x3031, 0x0a, "eReadWrite",eReadWrite},
{0x3032, 0x80, "eReadWrite",eReadWrite},
{0x303c, 0xff, "eReadWrite",eReadWrite},
{0x303e, 0xff, "eReadWrite",eReadWrite},
{0x3040, 0xf0, "eReadWrite",eReadWrite},
{0x3041, 0x00, "eReadWrite",eReadWrite},
{0x3042, 0xf0, "eReadWrite",eReadWrite},
{0x3104, 0x01, "eReadWrite",eReadWrite},
{0x3106, 0x15, "eReadWrite",eReadWrite},
{0x3107, 0x01, "eReadWrite",eReadWrite},
{0x3500, 0x00, "eReadWrite",eReadWrite},
{0x3501, 0x38, "eReadWrite",eReadWrite},
{0x3502, 0x40, "eReadWrite",eReadWrite},
{0x3503, 0x08, "eReadWrite",eReadWrite},
{0x3504, 0x03, "eReadWrite",eReadWrite},
{0x3505, 0x83, "eReadWrite",eReadWrite},
{0x3508, 0x02, "eReadWrite",eReadWrite},
{0x3509, 0x80, "eReadWrite",eReadWrite},
{0x3600, 0x65, "eReadWrite",eReadWrite},
{0x3601, 0x60, "eReadWrite",eReadWrite},
{0x3602, 0x22, "eReadWrite",eReadWrite},
{0x3610, 0xe8, "eReadWrite",eReadWrite},
{0x3611, 0x5c, "eReadWrite",eReadWrite},
{0x3612, 0x18, "eReadWrite",eReadWrite},
{0x3613, 0x3a, "eReadWrite",eReadWrite},
{0x3614, 0x91, "eReadWrite",eReadWrite},
{0x3615, 0x79, "eReadWrite",eReadWrite},
{0x3617, 0x57, "eReadWrite",eReadWrite},
{0x3621, 0x90, "eReadWrite",eReadWrite},
{0x3622, 0x00, "eReadWrite",eReadWrite},
{0x3623, 0x00, "eReadWrite",eReadWrite},
{0x3633, 0x10, "eReadWrite",eReadWrite},
{0x3634, 0x10, "eReadWrite",eReadWrite},
{0x3635, 0x14, "eReadWrite",eReadWrite},
{0x3636, 0x14, "eReadWrite",eReadWrite},
{0x3650, 0x00, "eReadWrite",eReadWrite},
{0x3652, 0xff, "eReadWrite",eReadWrite},
{0x3654, 0x00, "eReadWrite",eReadWrite},
{0x3653, 0x34, "eReadWrite",eReadWrite},
{0x3655, 0x20, "eReadWrite",eReadWrite},
{0x3656, 0xff, "eReadWrite",eReadWrite},
{0x3657, 0xc4, "eReadWrite",eReadWrite},
{0x365a, 0xff, "eReadWrite",eReadWrite},
{0x365b, 0xff, "eReadWrite",eReadWrite},
{0x365e, 0xff, "eReadWrite",eReadWrite},
{0x365f, 0x00, "eReadWrite",eReadWrite},
{0x3668, 0x00, "eReadWrite",eReadWrite},
{0x366a, 0x07, "eReadWrite",eReadWrite},
{0x366d, 0x00, "eReadWrite",eReadWrite},
{0x366e, 0x10, "eReadWrite",eReadWrite},
{0x3702, 0x1d, "eReadWrite",eReadWrite},
{0x3703, 0x10, "eReadWrite",eReadWrite},
{0x3704, 0x14, "eReadWrite",eReadWrite},
{0x3705, 0x00, "eReadWrite",eReadWrite},
{0x3706, 0x27, "eReadWrite",eReadWrite},
{0x3709, 0x24, "eReadWrite",eReadWrite},
{0x370a, 0x00, "eReadWrite",eReadWrite},
{0x370b, 0x7d, "eReadWrite",eReadWrite},
{0x3714, 0x24, "eReadWrite",eReadWrite},
{0x371a, 0x5e, "eReadWrite",eReadWrite},
{0x3730, 0x82, "eReadWrite",eReadWrite},
{0x3733, 0x10, "eReadWrite",eReadWrite},
{0x373e, 0x18, "eReadWrite",eReadWrite},
{0x3755, 0x00, "eReadWrite",eReadWrite},
{0x3758, 0x00, "eReadWrite",eReadWrite},
{0x375b, 0x13, "eReadWrite",eReadWrite},
{0x3772, 0x23, "eReadWrite",eReadWrite},
{0x3773, 0x05, "eReadWrite",eReadWrite},
{0x3774, 0x16, "eReadWrite",eReadWrite},
{0x3775, 0x12, "eReadWrite",eReadWrite},
{0x3776, 0x08, "eReadWrite",eReadWrite},
{0x37a8, 0x38, "eReadWrite",eReadWrite},
{0x37b5, 0x36, "eReadWrite",eReadWrite},
{0x37c2, 0x04, "eReadWrite",eReadWrite},
{0x37c5, 0x00, "eReadWrite",eReadWrite},
{0x37c7, 0x31, "eReadWrite",eReadWrite},
{0x37c8, 0x00, "eReadWrite",eReadWrite},
{0x37d1, 0x13, "eReadWrite",eReadWrite},
{0x3800, 0x00, "eReadWrite",eReadWrite},
{0x3801, 0x00, "eReadWrite",eReadWrite},
{0x3802, 0x00, "eReadWrite",eReadWrite},
{0x3803, 0x04, "eReadWrite",eReadWrite},
{0x3804, 0x05, "eReadWrite",eReadWrite},
{0x3805, 0x0f, "eReadWrite",eReadWrite},
{0x3806, 0x03, "eReadWrite",eReadWrite},
{0x3807, 0xcb, "eReadWrite",eReadWrite},
{0x3808, 0x05, "eReadWrite",eReadWrite},
{0x3809, 0x00, "eReadWrite",eReadWrite},
{0x380a, 0x03, "eReadWrite",eReadWrite},
{0x380b, 0xc0, "eReadWrite",eReadWrite},
{0x380c, 0x03, "eReadWrite",eReadWrite},//hts
{0x380d, 0x2a, "eReadWrite",eReadWrite},
{0x380e, 0x03, "eReadWrite",eReadWrite},//vts
{0x380f, 0xdc, "eReadWrite",eReadWrite},
{0x3810, 0x00, "eReadWrite",eReadWrite},
{0x3811, 0x08, "eReadWrite",eReadWrite},
{0x3812, 0x00, "eReadWrite",eReadWrite},
{0x3813, 0x04, "eReadWrite",eReadWrite},
{0x3814, 0x01, "eReadWrite",eReadWrite},
{0x3815, 0x01, "eReadWrite",eReadWrite},
{0x3816, 0x00, "eReadWrite",eReadWrite},
{0x3817, 0x00, "eReadWrite",eReadWrite},
{0x3818, 0x00, "eReadWrite",eReadWrite},
{0x3819, 0x00, "eReadWrite",eReadWrite},
{0x3820, 0x80, "eReadWrite",eReadWrite},
{0x3821, 0x40, "eReadWrite",eReadWrite},
{0x3826, 0x00, "eReadWrite",eReadWrite},
{0x3827, 0x08, "eReadWrite",eReadWrite},
{0x382a, 0x01, "eReadWrite",eReadWrite},
{0x382b, 0x01, "eReadWrite",eReadWrite},
{0x3836, 0x02, "eReadWrite",eReadWrite},
{0x3838, 0x10, "eReadWrite",eReadWrite},
{0x3861, 0x00, "eReadWrite",eReadWrite},
{0x3862, 0x00, "eReadWrite",eReadWrite},
{0x3863, 0x02, "eReadWrite",eReadWrite},
{0x3b00, 0x00, "eReadWrite",eReadWrite},
{0x3c00, 0x89, "eReadWrite",eReadWrite},
{0x3c01, 0xab, "eReadWrite",eReadWrite},
{0x3c02, 0x01, "eReadWrite",eReadWrite},
{0x3c03, 0x00, "eReadWrite",eReadWrite},
{0x3c04, 0x00, "eReadWrite",eReadWrite},
{0x3c05, 0x03, "eReadWrite",eReadWrite},
{0x3c06, 0x00, "eReadWrite",eReadWrite},
{0x3c07, 0x05, "eReadWrite",eReadWrite},
{0x3c0c, 0x00, "eReadWrite",eReadWrite},
{0x3c0d, 0x00, "eReadWrite",eReadWrite},
{0x3c0e, 0x00, "eReadWrite",eReadWrite},
{0x3c0f, 0x00, "eReadWrite",eReadWrite},
{0x3c40, 0x00, "eReadWrite",eReadWrite},
{0x3c41, 0xa3, "eReadWrite",eReadWrite},
{0x3c43, 0x7d, "eReadWrite",eReadWrite},
{0x3c56, 0x80, "eReadWrite",eReadWrite},
{0x3c80, 0x08, "eReadWrite",eReadWrite},
{0x3c82, 0x01, "eReadWrite",eReadWrite},
{0x3c83, 0x61, "eReadWrite",eReadWrite},
{0x3d85, 0x17, "eReadWrite",eReadWrite},
{0x3f08, 0x08, "eReadWrite",eReadWrite},
{0x3f0a, 0x00, "eReadWrite",eReadWrite},
{0x3f0b, 0x30, "eReadWrite",eReadWrite},
{0x4000, 0xcd, "eReadWrite",eReadWrite},
{0x4003, 0x40, "eReadWrite",eReadWrite},
{0x4009, 0x0d, "eReadWrite",eReadWrite},
{0x4010, 0xf0, "eReadWrite",eReadWrite},
{0x4011, 0x70, "eReadWrite",eReadWrite},
{0x4017, 0x10, "eReadWrite",eReadWrite},
{0x4040, 0x00, "eReadWrite",eReadWrite},
{0x4041, 0x00, "eReadWrite",eReadWrite},
{0x4303, 0x00, "eReadWrite",eReadWrite},
{0x4307, 0x30, "eReadWrite",eReadWrite},
{0x4500, 0x30, "eReadWrite",eReadWrite},
{0x4502, 0x40, "eReadWrite",eReadWrite},
{0x4503, 0x06, "eReadWrite",eReadWrite},
{0x4508, 0xaa, "eReadWrite",eReadWrite},
{0x450b, 0x00, "eReadWrite",eReadWrite},
{0x450c, 0x00, "eReadWrite",eReadWrite},
{0x4600, 0x00, "eReadWrite",eReadWrite},
{0x4601, 0x80, "eReadWrite",eReadWrite},
{0x4700, 0x04, "eReadWrite",eReadWrite},
{0x4704, 0x00, "eReadWrite",eReadWrite},
{0x4705, 0x04, "eReadWrite",eReadWrite},
{0x4837, 0x14, "eReadWrite",eReadWrite},
{0x484a, 0x3f, "eReadWrite",eReadWrite},
{0x5000, 0x10, "eReadWrite",eReadWrite},
{0x5001, 0x01, "eReadWrite",eReadWrite},
{0x5002, 0x28, "eReadWrite",eReadWrite},
{0x5004, 0x0c, "eReadWrite",eReadWrite},
{0x5006, 0x0c, "eReadWrite",eReadWrite},
{0x5007, 0xe0, "eReadWrite",eReadWrite},
{0x5008, 0x01, "eReadWrite",eReadWrite},
{0x5009, 0xb0, "eReadWrite",eReadWrite},
{0x502a, 0x18, "eReadWrite",eReadWrite},
{0x5901, 0x00, "eReadWrite",eReadWrite},
{0x5a01, 0x00, "eReadWrite",eReadWrite},
{0x5a03, 0x00, "eReadWrite",eReadWrite},
{0x5a04, 0x0c, "eReadWrite",eReadWrite},
{0x5a05, 0xe0, "eReadWrite",eReadWrite},
{0x5a06, 0x09, "eReadWrite",eReadWrite},
{0x5a07, 0xb0, "eReadWrite",eReadWrite},
{0x5a08, 0x06, "eReadWrite",eReadWrite},
{0x5e00, 0x00, "eReadWrite",eReadWrite},
{0x5e10, 0xfc, "eReadWrite",eReadWrite},
{0x300f, 0x00, "eReadWrite",eReadWrite},
{0x3733, 0x10, "eReadWrite",eReadWrite},
{0x3610, 0xe8, "eReadWrite",eReadWrite},
{0x3611, 0x5c, "eReadWrite",eReadWrite},
{0x3635, 0x14, "eReadWrite",eReadWrite},
{0x3636, 0x14, "eReadWrite",eReadWrite},
{0x3620, 0x84, "eReadWrite",eReadWrite},
{0x3614, 0x96, "eReadWrite",eReadWrite},
{0x481f, 0x30, "eReadWrite",eReadWrite},
{0x3788, 0x00, "eReadWrite",eReadWrite},
{0x3789, 0x04, "eReadWrite",eReadWrite},
{0x378a, 0x01, "eReadWrite",eReadWrite},
{0x378b, 0x60, "eReadWrite",eReadWrite},
{0x3799, 0x27, "eReadWrite",eReadWrite},
{0x0000 ,0x00, "eReadWrite",eTableEnd}

};

const IsiRegDescription_t Sensor_g_1280x960[] =
{

	{0x0000, 0x00, "eReadWrite",eTableEnd}
	
};


const IsiRegDescription_t Sensor_g_1280x960_30fps[] =
{
	{0x380e, 0x07, "eReadWrite",eReadWrite},// VTS H
	{0x380f, 0xb8, "eReadWrite",eReadWrite},// VTS L
	{0x0000, 0x00, "eReadWrite",eTableEnd}
	
};
const IsiRegDescription_t Sensor_g_1280x960_20fps[] =
{
	{0x380e, 0x0b, "eReadWrite",eReadWrite},// VTS H
	{0x380f, 0x92, "eReadWrite",eReadWrite},// VTS L
	{0x0000, 0x00, "eReadWrite",eTableEnd}
	
};
const IsiRegDescription_t Sensor_g_1280x960_15fps[] =
{
	{0x380e, 0x0f, "eReadWrite",eReadWrite},// VTS H
	{0x380f, 0x6e, "eReadWrite",eReadWrite},// VTS L
	{0x0000, 0x00, "eReadWrite",eTableEnd}
	
};

const IsiRegDescription_t Sensor_g_1280x960_10fps[] =
{
	{0x380e, 0x17, "eReadWrite",eReadWrite},// VTS H
	{0x380f, 0x24, "eReadWrite",eReadWrite},// VTS L
	{0x0000, 0x00, "eReadWrite",eTableEnd}
	
};


