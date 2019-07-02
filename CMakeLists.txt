#*******************************************************************************
# Copyright (C) Sierra Wireless, Inc.
#*******************************************************************************

mkapp(sensorFw.adef -i ${LEGATO_ROOT}/apps/sample/dataHub -i ${LEGATO_ROOT}/interfaces/modemServices)

# This is a sample application
add_dependencies(samples_c sensorFw)
