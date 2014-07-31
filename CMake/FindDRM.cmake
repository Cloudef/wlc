############################################################################
#
# Copyright 2010, 2011 BMW Car IT GmbH
# Copyright (C) 2011 DENSO CORPORATION and Robert Bosch Car Multimedia Gmbh
#
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#       http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
############################################################################

FIND_PATH(UDEV_INCLUDE_DIR /libudev.h
/usr/include
)

FIND_LIBRARY(UDEV_LIBRARIES
NAMES udev
PATHS /usr/lib
)

FIND_PATH(GBM_INCLUDE_DIR /gbm.h
/usr/include
)

FIND_LIBRARY(GBM_LIBRARIES
NAMES gbm
PATHS /usr/lib
)

FIND_PATH(DRM_INCLUDE_DIR /drm.h
/usr/include/libdrm /usr/include/drm
)

FIND_LIBRARY(DRM_LIBRARIES
NAMES drm
PATHS /usr/lib
)

SET( DRM_FOUND "NO" )
IF(UDEV_LIBRARIES AND GBM_LIBRARIES AND DRM_LIBRARIES)
    SET( DRM_FOUND "YES" )
    message(STATUS "Found udev libs: ${UDEV_LIBRARIES}")
    message(STATUS "Found udev includes: ${UDEV_INCLUDE_DIR}")
    message(STATUS "Found gbm libs: ${GBM_LIBRARIES}")
    message(STATUS "Found gbm includes: ${GBM_INCLUDE_DIR}")
    message(STATUS "Found drm includes: ${DRM_INCLUDE_DIR}")
    message(STATUS "Found drm libs: ${DRM_LIBRARIES}")
ENDIF(UDEV_LIBRARIES AND GBM_LIBRARIES AND DRM_LIBRARIES)

MARK_AS_ADVANCED(
  UDEV_INCLUDE_DIR
  UDEV_LIBRARIES
  GBM_INCLUDE_DIR
  GBM_LIBRARIES
  DRM_INCLUDE_DIR
  DRM_LIBRARIES
)
