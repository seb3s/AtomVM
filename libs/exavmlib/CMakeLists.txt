##
## Copyright (c) 2019 Riccardo Binetti <rbino@gmx.com>
##

project(exavmlib)

include(BuildElixir)

set(ELIXIR_MODULES
    Console
    GPIO
)

pack_archive(exavmlib ${ELIXIR_MODULES})

install(FILES exavmlib.avm DESTINATION lib/AtomVM/ebin)
