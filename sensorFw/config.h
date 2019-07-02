//--------------------------------------------------------------------------------------------------
/**
 * Build-time configuration values for sensor framework
 *
 * Copyright (C) Sierra Wireless Inc.
 */
//--------------------------------------------------------------------------------------------------

#ifndef SENSOR_FW_CONFIG_H_INCLUDE_GUARD
#define SENSOR_FW_CONFIG_H_INCLUDE_GUARD

#include "legato.h"

#if LE_CONFIG_REDUCE_FOOTPRINT
#define SENSOR_HANDLER_POOL_SIZE (100)
#else
#define SENSOR_HANDLER_POOL_SIZE (1000)
#endif

#endif /* end SENSOR_FW_CONFIG_H_INCLUDE_GUARD */
