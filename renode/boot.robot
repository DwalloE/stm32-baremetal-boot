*** Settings ***
Documentation     stm32-baremetal-boot in Renode: the firmware grades its own
...               startup and this suite only matches the text. Three cases:
...               the healthy image, the CONTROL image (startup.s without the
...               .data copy) whose integrity check MUST fail, and the
...               deliberate HardFault. Renode's F103 has no RCC model (see
...               bluepill.repl), so the clock line asserted here is the
...               firmware's honest "clk UNVERIFIED" - the verdict text, not a
...               fixed-up expectation.
Suite Setup       Setup
Suite Teardown    Teardown
Test Teardown     Test Teardown
Resource          ${RENODEKEYWORDS}

*** Variables ***
${ELF}            ${CURDIR}/../build/firmware.elf
${CONTROL_ELF}    ${CURDIR}/../build/control.elf
${RESC}           ${CURDIR}/bluepill.resc

*** Keywords ***
Boot Image
    [Arguments]    ${elf}
    Execute Command           $elf=@${elf}
    Execute Command           include @${RESC}
    Create Terminal Tester    sysbus.usart1    timeout=10
    Create LED Tester         sysbus.gpioPortC.led    defaultTimeout=5

*** Test Cases ***
Healthy Image Boots And Grades Itself
    [Documentation]    .data copied, .bss zeroed, MSP in the stack window, vector table
    ...                as linked, SysTick driving the uptime counter and PC13 heartbeat.
    Boot Image    ${ELF}
    Register Failing Uart String    boot INTEGRITY VIOLATION
    Register Failing Uart String    vec MISMATCH
    Register Failing Uart String    hardfault: caught

    Wait For Line On Uart    stm32-baremetal-boot: STM32F103C8, reset vector to main() with no HAL, no CMSIS
    Wait For Line On Uart    boot: data ok, bss ok, sp ok
    Wait For Line On Uart    vec: table at 0x08000000 (VTOR=0x08000000), entry[0] = _estack 0x20005000, entry[1] = Reset_Handler 0x0800    treatAsRegex=false
    # Renode: RCC_CR's tag reports HSE and PLL ready, but RCC_CFGR is unmapped so SWS reads 0 - the firmware must say so.
    Wait For Line On Uart    clk UNVERIFIED: hse_ready=1 pll_ready=1 sws=0x00000000 \\(want 0x00000002\\) after 100000 polls    treatAsRegex=true
    Wait For Line On Uart    shell: commands: help | boot | vec | clk | map | fault

    # SysTick (vector 15) -> SysTick_Handler -> g_ms -> uptime lines, one per second of virtual time.
    Wait For Line On Uart    uptime: t=1s
    Wait For Line On Uart    uptime: t=3s

    # The heartbeat: 500 ms on, 500 ms off on PC13. Measured over 3 s of virtual time.
    Assert LED Is Blinking    testDuration=3    onDuration=0.5    offDuration=0.5    tolerance=0.2

    Write Line To Uart       map
    Wait For Line On Uart    map: _sidata=0x0800    treatAsRegex=false
    Wait For Line On Uart    map: _sstack=0x20004c00 _estack=0x20005000 msp now=0x2000
    Write Line To Uart       vec
    Wait For Line On Uart    vec\\[1\\] = 0x0800[0-9a-f]{3}[13579bdf]  Reset_Handler    treatAsRegex=true
    Wait For Line On Uart    vec\\[15\\] = 0x0800[0-9a-f]{3}[13579bdf]  SysTick_Handler    treatAsRegex=true
    Write Line To Uart       clk
    Wait For Line On Uart    clk: RCC_CR=0x0a020083 RCC_CFGR=0x00000000

Control Image Without The Data Copy Must Fail The Integrity Check
    [Documentation]    Same C, same linker script, startup.s built with
    ...                CONTROL_SKIP_DATA_COPY. If this image printed "boot: data ok"
    ...                the healthy image's pass line would mean nothing.
    Boot Image    ${CONTROL_ELF}
    Register Failing Uart String    boot: data ok

    Wait For Line On Uart    boot INTEGRITY VIOLATION: data 4/4 words differ from flash, first at 0x20000000 ram=0x00000000 flash=0xc0de1234;
    # Everything after the verdict still works: the printer never depended on .data.
    Wait For Line On Uart    vec: table at 0x08000000
    Wait For Line On Uart    uptime: t=1s

Deliberate UDF Lands In HardFault_Handler And Is Decoded
    [Documentation]    Vector 3 proven: an undefined instruction escalates to HardFault
    ...                (UsageFault disabled), the shim in startup.s hands the stacked
    ...                frame to C, and the decoder names the cause.
    Boot Image    ${ELF}
    Wait For Line On Uart    shell: commands: help | boot | vec | clk | map | fault
    Wait For Line On Uart    uptime: t=1s
    Write Line To Uart       fault
    Wait For Line On Uart    fault: executing UDF #0 - HardFault_Handler must catch it
    Wait For Line On Uart    hardfault: caught, pc=0x0800[0-9a-f]{4} lr=0x[0-9a-f]{8} xpsr=0x[0-9a-f]{8} cfsr=0x[0-9a-f]{8}.*    treatAsRegex=true
    Wait For Line On Uart    hardfault: halted
    Should Not Be On Uart    fault NOT CAUGHT    timeout=1
