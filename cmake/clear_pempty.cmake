# STM32CubeProgrammer's erase leaves FLASH_SR.PEMPTY set on the STM32L432, so the following
# reset boots the ROM bootloader instead of the freshly flashed image. Clear PEMPTY (write-1-to-clear)
# and reset again. The CLI reports a failed write-verify on this register, so its result is ignored.
execute_process(COMMAND STM32_Programmer_CLI -c port=SWD mode=HOTPLUG -w32 0x40022010 0x00020000
                OUTPUT_QUIET ERROR_QUIET)
execute_process(COMMAND STM32_Programmer_CLI -c port=SWD mode=HOTPLUG -rst
                OUTPUT_QUIET ERROR_QUIET)
message(STATUS "Cleared FLASH_SR.PEMPTY and reset target")
