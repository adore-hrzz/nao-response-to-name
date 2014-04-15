cmake_minimum_required(VERSION 2.8)
project(ResponseToName)
find_package(qibuild)

include_directories(include)
## building Logger module

option(LOGGER_IS_REMOTE
  "module is compiled as a remote module (ON or OFF)"
  OFF)

set(_srcsLogger
  src/logmodule_loader.cpp
  include/logmodule.hpp
  src/logmodule.cpp
)

if(LOGGER_IS_REMOTE)
  add_definitions(" -DLOGGER_IS_REMOTE ")
  qi_create_bin(logger ${_srcsLogger})
else()
  qi_create_lib(logger SHARED ${_srcsLogger} SUBFOLDER Logger)
endif()

qi_use_lib(logger ALCOMMON)

## building Interface module

option(INTERFACE_IS_REMOTE
  "module is compiled as a remote module (ON or OFF)"
  OFF)

set(_srcsInterface
  src/uimodule_loader.cpp
  include/uimodule.hpp
  src/uimodule.cpp
)

if(INTERFACE_IS_REMOTE)
  add_definitions(" -DINTERFACE_IS_REMOTE ")
  qi_create_bin(interface ${_srcsInterface})
else()
  qi_create_lib(interface SHARED ${_srcsInterface} SUBFOLDER Interface)
endif()

qi_use_lib(interface ALCOMMON)
