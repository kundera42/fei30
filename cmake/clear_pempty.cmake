# FLASH_SR.PEMPTY on the STM32L432 is latched only at power-on reset / option-byte load: it is set
# when 0x08000000 reads 0xFFFFFFFF at that moment. Once set, every system reset (including the
# programmer's -rst) boots the ROM bootloader, even after the flash has been programmed, until the
# next power cycle. See RM0394 3.3.1 "Empty check" and the FLASH_SR description.
#
# Writing 1 to PEMPTY *toggles* it (RM0394: "1: The bit value is toggling"), so only write it when it
# is actually set, otherwise we would set it ourselves. The CLI reports a failed write-verify on this
# register, so the write result is ignored and the register is read back instead.
function(read_flash_sr out_var)
    execute_process(COMMAND STM32_Programmer_CLI -c port=SWD mode=HOTPLUG -r32 0x40022010 4
                    OUTPUT_VARIABLE _sr ERROR_QUIET)
    string(REGEX MATCH "0x40022010 : ([0-9A-Fa-f]+)" _ "${_sr}")
    if("${CMAKE_MATCH_1}" STREQUAL "")
        message(FATAL_ERROR "Could not read FLASH_SR over SWD")
    endif()
    math(EXPR _pempty "(0x${CMAKE_MATCH_1} >> 17) & 1")
    set(${out_var} ${_pempty} PARENT_SCOPE)
endfunction()

read_flash_sr(_pempty)
if(_pempty)
    execute_process(COMMAND STM32_Programmer_CLI -c port=SWD mode=HOTPLUG -w32 0x40022010 0x00020000
                    OUTPUT_QUIET ERROR_QUIET)
    read_flash_sr(_pempty)
    if(_pempty)
        message(FATAL_ERROR "Could not clear FLASH_SR.PEMPTY; target would boot the ROM bootloader")
    endif()
    execute_process(COMMAND STM32_Programmer_CLI -c port=SWD mode=HOTPLUG -rst
                    OUTPUT_QUIET ERROR_QUIET)
    message(STATUS "FLASH_SR.PEMPTY was set: cleared it and reset target")
else()
    message(STATUS "FLASH_SR.PEMPTY not set, nothing to do")
endif()
