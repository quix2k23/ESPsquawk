# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file LICENSE.rst or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "C:/esp/v6.0.2/esp-idf/components/bootloader/subproject")
  file(MAKE_DIRECTORY "C:/esp/v6.0.2/esp-idf/components/bootloader/subproject")
endif()
file(MAKE_DIRECTORY
  "E:/Drones/ESPbuzz/ESPsquawk_ESP32_DRONE_REMOTE_ID_Firmware/build/bootloader"
  "E:/Drones/ESPbuzz/ESPsquawk_ESP32_DRONE_REMOTE_ID_Firmware/build/bootloader-prefix"
  "E:/Drones/ESPbuzz/ESPsquawk_ESP32_DRONE_REMOTE_ID_Firmware/build/bootloader-prefix/tmp"
  "E:/Drones/ESPbuzz/ESPsquawk_ESP32_DRONE_REMOTE_ID_Firmware/build/bootloader-prefix/src/bootloader-stamp"
  "E:/Drones/ESPbuzz/ESPsquawk_ESP32_DRONE_REMOTE_ID_Firmware/build/bootloader-prefix/src"
  "E:/Drones/ESPbuzz/ESPsquawk_ESP32_DRONE_REMOTE_ID_Firmware/build/bootloader-prefix/src/bootloader-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "E:/Drones/ESPbuzz/ESPsquawk_ESP32_DRONE_REMOTE_ID_Firmware/build/bootloader-prefix/src/bootloader-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "E:/Drones/ESPbuzz/ESPsquawk_ESP32_DRONE_REMOTE_ID_Firmware/build/bootloader-prefix/src/bootloader-stamp${cfgdir}") # cfgdir has leading slash
endif()
