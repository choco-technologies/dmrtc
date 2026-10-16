set(DMOD_MODULE_NAME        test_dmrtc)
set(DMOD_MODULE_VERSION     ${PROJECT_VERSION})
set(DMOD_AUTHOR_NAME        "Patryk Kubiak")
set(DMOD_STACK_SIZE         2048)
set(DMOD_PRIORITY           0)

set(DMOD_DMR_PATH ${CMAKE_CURRENT_SOURCE_DIR}/test_dmrtc.dmr)

dmod_add_test(${DMOD_MODULE_NAME} ${DMOD_MODULE_VERSION}
    dmrtc_test.c
)

target_include_directories(${DMOD_MODULE_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

target_link_libraries(${DMOD_MODULE_NAME} dmrtc_if)
if(DMOD_CPU_FAMILY STREQUAL "x86_64")
    target_compile_definitions(${DMOD_MODULE_NAME} PRIVATE DMRTC_MOCK)
    target_link_libraries(${DMOD_MODULE_NAME} dmrtc_port_if)
endif()

dmod_link_modules(${DMOD_MODULE_NAME} dmosi dmdrvi dmini)

# Register the test module with ctest - this replays the same two steps CI
# runs (see .github/templates/ci.yml.template): install the test module's
# dependencies with dmf-get, then execute it through dmod_loader. The test's
# exit code equals its number of failed steps (see dmod_add_test).
find_program(DMF_GET_EXECUTABLE dmf-get HINTS ${DMOD_TOOLS_BIN_DIR} PATHS ENV PATH)
find_program(DMOD_LOADER_EXECUTABLE dmod_loader HINTS ${DMOD_TOOLS_BIN_DIR} PATHS ENV PATH)

if(DMF_GET_EXECUTABLE AND DMOD_LOADER_EXECUTABLE)
    add_test(
        NAME ${DMOD_MODULE_NAME}
        COMMAND ${CMAKE_COMMAND} -E env DMOD_DMF_DIR=${DMOD_DMF_DIR}
            bash -c "${DMF_GET_EXECUTABLE} install -d ${DMOD_DMF_DIR}/${DMOD_MODULE_NAME}-local.dmd -y && ${DMOD_LOADER_EXECUTABLE} ${DMOD_DMF_DIR}/${DMOD_MODULE_NAME}.dmf"
    )
else()
    message(WARNING "dmf-get/dmod_loader not found - 'ctest' will not run ${DMOD_MODULE_NAME}. Build the tools first with -DDMOD_BUILD_TOOLS=ON")
endif()

# Exercise the actual STM32 wait implementation with MMIO and OS test doubles.
# Invoke the host compiler directly to avoid the DMOD module linker script.
if(DMOD_CPU_FAMILY STREQUAL "x86_64")
    find_program(HOST_CC NAMES cc gcc REQUIRED)
    set(WAIT_TEST ${CMAKE_CURRENT_SOURCE_DIR}/stm32_wait)
    set(WAIT_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/../src/port/stm32_common)
    add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/test_stm32_wait
        COMMAND ${HOST_CC} -D_POSIX_C_SOURCE=200809L -std=c11 -Wall -Wextra -Werror -pthread
            -include ${WAIT_TEST}/mmio.h -I${WAIT_TEST}/stubs
            -I${CMAKE_CURRENT_SOURCE_DIR}/../include -I${WAIT_SOURCE}
            ${WAIT_TEST}/wait_test.c ${WAIT_SOURCE}/clock_wait.c
            -o ${CMAKE_CURRENT_BINARY_DIR}/test_stm32_wait
        DEPENDS ${WAIT_TEST}/wait_test.c ${WAIT_SOURCE}/clock_wait.c
            ${WAIT_SOURCE}/clock_wait.h ${WAIT_SOURCE}/registers.h
            ${WAIT_TEST}/mmio.h ${WAIT_TEST}/stubs/dmod.h ${WAIT_TEST}/stubs/dmosi.h)
    add_custom_target(stm32_wait_test_build ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/test_stm32_wait)
    add_test(NAME stm32_rtc_wait COMMAND ${CMAKE_CURRENT_BINARY_DIR}/test_stm32_wait)
    set_tests_properties(stm32_rtc_wait PROPERTIES TIMEOUT 10)
    set(RTC_CLIENT_TEST ${CMAKE_CURRENT_SOURCE_DIR}/rtc_client)
    add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/test_rtc_client
        COMMAND ${HOST_CC} -std=c11 -Wall -Wextra -Werror
            -I${RTC_CLIENT_TEST}/stubs -I${DMOD_DMF_DIR}/inc/dmdrvi/include
            ${RTC_CLIENT_TEST}/client_test.c -o ${CMAKE_CURRENT_BINARY_DIR}/test_rtc_client
        DEPENDS ${RTC_CLIENT_TEST}/client_test.c ${RTC_CLIENT_TEST}/stubs/dmod.h
            ${CMAKE_CURRENT_SOURCE_DIR}/../apps/rtc/rtc.c
            ${DMOD_DMF_DIR}/inc/dmdrvi/include/dmdrvi_ioctl.h)
    add_custom_target(rtc_client_test_build ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/test_rtc_client)
    add_test(NAME rtc_generic_client COMMAND ${CMAKE_CURRENT_BINARY_DIR}/test_rtc_client)
    set_tests_properties(rtc_generic_client PROPERTIES TIMEOUT 10)
endif()
