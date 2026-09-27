"""
freertos_build.py - BCA182 Laboratory Activity 1

PlatformIO's stm32cube framework builder compiles HAL + CMSIS only: it never
touches Middlewares/Third_Party/FreeRTOS (there is not a single reference to
"freertos" anywhere in the ststm32 platform builder).  Section 6 of the
laboratory specification makes FreeRTOS mandatory, so this pre-script wires
the bundled kernel into the build.

Design notes
------------
* The kernel is compiled straight out of the framework package, so it is not
  vendored into the repository (keeps the commit history readable) and it is
  always the exact version that ships with framework-stm32cubef1.
* Only the files this application actually links are compiled: the core
  kernel, the Cortex-M3 GCC port and heap_4.  Timers, co-routines, stream
  buffers and every foreign port (IAR/Keil/RVDS/Tasking) are excluded, which
  keeps flash usage down on the 64 KB part.
* One documented, build-time patch is applied to the port before it is
  compiled - see WOKWI_PORT_PATCH below.  It has three parts (the startup
  CPSIE fix, the SVC context-switch handler and the deferred-yield exit from
  the critical section); all are re-derived from the framework file on every
  build and the script aborts if any expected vendor text is missing, so a
  package update can never silently drop them.
"""

Import("env")

import os
import re

platform = env.PioPlatform()
framework_dir = platform.get_package_dir("framework-stm32cubef1")

if not framework_dir:
    print("ERROR: framework-stm32cubef1 package not found - cannot build FreeRTOS.")
    env.Exit(1)

freertos_dir = os.path.join(
    framework_dir, "Middlewares", "Third_Party", "FreeRTOS", "Source"
)

if not os.path.isdir(freertos_dir):
    print("ERROR: FreeRTOS sources not found at %s" % freertos_dir)
    env.Exit(1)

# FreeRTOS.h must be able to find FreeRTOSConfig.h (in include/) and the
# Cortex-M3 port must be able to find portmacro.h.
env.Append(
    CPPPATH=[
        os.path.join(freertos_dir, "include"),
        os.path.join(freertos_dir, "portable", "GCC", "ARM_CM3"),
    ]
)

env.BuildSources(
    os.path.join("$BUILD_DIR", "FreeRTOS"),
    freertos_dir,
    src_filter=[
        "-<*>",
        "+<tasks.c>",
        "+<queue.c>",
        "+<list.c>",
        "+<event_groups.c>",
        "+<portable/MemMang/heap_4.c>",
    ],
)

# ---------------------------------------------------------------------------
# WOKWI_PORT_PATCH
#
# prvPortStartFirstTask() starts the first task with
#
#     cpsie i        / clear PRIMASK, i.e. globally enable interrupts
#     cpsie f        / clear FAULTMASK
#     svc 0          / enter the SVC handler, which restores the first context
#
# The Wokwi STM32F103 emulator decodes the CPS instruction with the enable and
# disable halves swapped: measured in simulation, `cpsid i` cleared PRIMASK and
# `cpsie i` set it, while `msr primask, r0` and `msr faultmask, r0` behave
# correctly.  Executed unpatched, `cpsie i` therefore raises PRIMASK just
# before `svc 0`, the SVC is pended forever, prvPortStartFirstTask() falls
# through and xPortStartScheduler() hits prvTaskExitError() - no task ever
# runs.  (The other cpsie/cpsid sites in this port are all inside
# configUSE_TICKLESS_IDLE, which this application leaves at 0, and
# portDISABLE_INTERRUPTS()/portENABLE_INTERRUPTS() use `msr basepri`, which the
# emulator models correctly.)
#
# The replacement is architecturally identical on real silicon - CPSIE I/F and
# MSR PRIMASK,#0 / MSR FAULTMASK,#0 both clear the two mask bits - so this file
# still builds and runs unchanged on a physical Blue Pill.
#
# ---------------------------------------------------------------------------
# Part 2: vPortSVCHandler() performs the yield itself
#
# The stock port yields from thread mode with a bare store:
#
#     *( ( volatile uint32_t * ) 0xE000ED04 ) = 0x10000000;  // ICSR.PENDSVSET
#     __asm volatile ( "dsb sy\n isb sy" );
#
# On silicon PendSV is taken straight after that store and the hardware stacks
# the address of the *following* instruction, so the task resumes one
# instruction past the yield.  The Wokwi STM32F103 model services PendSV at
# that very store and stacks the address of the store itself.  Two measured
# consequences:
#
#   * from thread mode the yielding task resumes on the store and re-pends
#     forever, so the task freezes on the yield instruction while the
#     scheduler itself keeps running;
#   * a store performed from inside the SVC handler makes PendSV return into
#     the SVC handler, so the two exceptions bounce and no task context ever
#     completes a switch.
#
# The only ICSR write proven safe in this emulator is the one made by
# xPortSysTickHandler, with BASEPRI held and taken on SysTick's own exception
# return.  So yields use no ICSR write at all.  include/FreeRTOSConfig.h
# redefines portYIELD_WITHIN_API() as `svc 0`, and the handler below branches
# on EXC_RETURN bit 2 (loaded from LR):
#
#     bit 2 = 0  -> MSP: prvPortStartFirstTask().  The vendor first-task
#                   restore, byte for byte.
#     bit 2 = 1  -> PSP: a task yielding.  Save the context (r4-r11 plus
#                   pxTopOfStack, exactly like PendSV does), call
#                   vTaskSwitchContext(), restore the winner, point PSP at it
#                   and return straight to thread mode with bx lr.  There is
#                   no second exception and therefore no stacked PC to get
#                   wrong.
#
# A yield taken while uxCriticalNesting != 0 is remembered in
# wokwiYieldDeferred and reissued by vPortExitCritical() (Part 3) once
# BASEPRI is clear - the same instant at which the pended PendSV would have
# run on silicon - so the kernel's "never switch inside a critical section"
# rule is preserved.  PRIMASK is raised for the duration of the swap so a
# SysTick landing mid-switch cannot observe a half-migrated task.
# ---------------------------------------------------------------------------
_PORT_SRC = os.path.join(freertos_dir, "portable", "GCC", "ARM_CM3", "port.c")

_CPSIE_RE = re.compile(
    r'([ \t]*)" cpsie i[ \t]*\\n"[ \t]*/\*[ \t]*Globally enable interrupts\.'
    r'[ \t]*\*/\r?\n[ \t]*" cpsie f[ \t]*\\n"'
)

# Matches the whole of vPortSVCHandler() - from its definition down to the
# first closing brace at column zero.  The vendor body is a single asm block
# with no nested braces, so a non-greedy match stops exactly there.
_SVC_RE = re.compile(r"void vPortSVCHandler\( void \)\s*\n\{.*?\n\}", re.S)

_SVC_YIELD_TRAMPOLINE = """/* A yield requested from inside a critical section is remembered here and
 * performed by vPortExitCritical() once BASEPRI has been cleared again. */
static volatile uint32_t wokwiYieldDeferred = 0;

void vPortSVCHandler( void )
{
	__asm volatile (
					"	tst lr, #4						\\n" /* EXC_RETURN bit 2: 0 = MSP, 1 = PSP. */
					"	beq 1f							\\n" /* MSP: start the first task (below).    */
					"	ldr r3, wokwiNestingAddr		\\n" /* PSP: a task yielding.                */
					"	ldr r2, [r3]					\\n"
					"	cbz r2, 3f						\\n" /* Inside a critical section: defer the  */
					"	ldr r3, wokwiDeferAddr			\\n" /* switch to vPortExitCritical().        */
					"	movs r2, #1						\\n"
					"	str r2, [r3]					\\n"
					"	bx r14							\\n"
					"3:								\\n"
					"	movs r2, #1						\\n" /* Hold every exception off while the    */
					"	msr primask, r2					\\n" /* context is being swapped.             */
					"	mrs r0, psp						\\n" /* Save r4-r11 and the top of stack,     */
					"	stmdb r0!, {r4-r11}				\\n" /* exactly like PendSV does.             */
					"	ldr r3, pxCurrentTCBConst2		\\n"
					"	ldr r2, [r3]					\\n"
					"	str r0, [r2]					\\n"
					"	stmdb sp!, {r3, lr}				\\n" /* Keep EXC_RETURN across the C call.    */
					"	bl vTaskSwitchContext			\\n"
					"	ldmia sp!, {r3, lr}				\\n"
					"	ldr r3, pxCurrentTCBConst2		\\n"
					"	ldr r1, [r3]					\\n"
					"	ldr r0, [r1]					\\n"
					"	ldmia r0!, {r4-r11}				\\n"
					"	msr psp, r0						\\n"
					"	isb								\\n"
					"	movs r2, #0						\\n" /* Back to normal, then leave directly    */
					"	msr primask, r2					\\n" /* to the (possibly new) thread task.     */
					"	bx r14							\\n"
					"1:\\n"
					"	ldr	r3, pxCurrentTCBConst2		\\n" /* Restore the first task's context. */
					"	ldr r1, [r3]					\\n" /* Use pxCurrentTCBConst to get the pxCurrentTCB address. */
					"	ldr r0, [r1]					\\n" /* The first item in pxCurrentTCB is the task top of stack. */
					"	ldmia r0!, {r4-r11}				\\n" /* Pop the registers that are not automatically saved on exception entry and the critical nesting count. */
					"	msr psp, r0						\\n" /* Restore the task stack pointer. */
					"	isb								\\n"
					"	mov r0, #0 						\\n"
					"	msr	basepri, r0					\\n"
					"	orr r14, #0xd					\\n"
					"	bx r14							\\n"
					"									\\n"
					"	.align 4						\\n"
					"pxCurrentTCBConst2: .word pxCurrentTCB				\\n"
					"wokwiNestingAddr: .word uxCriticalNesting			\\n"
					"wokwiDeferAddr: .word wokwiYieldDeferred			\\n"
				);
}"""

# ---------------------------------------------------------------------------
# Part 3: vPortExitCritical() reissues a yield that was deferred
#
# FreeRTOS never switches from inside a critical section.  When
# portYIELD_WITHIN_API() expands to `svc 0` and uxCriticalNesting is not zero,
# vPortSVCHandler() records the request in wokwiYieldDeferred instead of
# switching, and the switch is issued here, after portENABLE_INTERRUPTS() -
# the same instant at which the stock port's pended PendSV would have run.
# ---------------------------------------------------------------------------
_EXITCRIT_RE = re.compile(r"void vPortExitCritical\( void \)\s*\n\{.*?\n\}", re.S)

_EXITCRIT_PATCH = """void vPortExitCritical( void )
{
	configASSERT( uxCriticalNesting );
	uxCriticalNesting--;
	if( uxCriticalNesting == 0 )
	{
		portENABLE_INTERRUPTS();

		/* A yield requested from inside this critical section was deferred by
		 * vPortSVCHandler; perform it now, with BASEPRI clear - the same
		 * instant at which the pended PendSV would have been taken. */
		if( wokwiYieldDeferred != 0 )
		{
			wokwiYieldDeferred = 0;
			__asm volatile ( "svc 0" );
		}
	}
}"""


def _fail(what):
    print(
        "ERROR: WOKWI_PORT_PATCH could not find %s in\n"
        "       %s  The framework's port.c has changed; update the patch in\n"
        "       scripts/freertos_build.py." % (what, _PORT_SRC)
    )
    env.Exit(1)


def _wokwi_patch(text):
    def _repl(match):
        indent = match.group(1)
        lines = (
            '" mov r0, #0				\\n" /* CPSIE is decoded inverted by the */',
            '" msr primask, r0			\\n" /* Wokwi emulator, so clear PRIMASK */',
            '" msr faultmask, r0			\\n" /* and FAULTMASK with MSR instead. */',
        )
        return "\n".join(indent + line for line in lines)

    # A function replacement is used on purpose: re.sub() would otherwise
    # reinterpret the \n escapes that belong to the C string literals below.
    text, count = _CPSIE_RE.subn(_repl, text)
    if count != 1:
        _fail("the cpsie i / cpsie f sequence in prvPortStartFirstTask()")

    text, count = _SVC_RE.subn(lambda _m: _SVC_YIELD_TRAMPOLINE, text)
    if count != 1:
        _fail("the definition of vPortSVCHandler()")

    text, count = _EXITCRIT_RE.subn(lambda _m: _EXITCRIT_PATCH, text)
    if count != 1:
        _fail("the definition of vPortExitCritical()")

    return text


with open(_PORT_SRC, "r", newline="") as _fh:
    _port_text = _fh.read()

_port_out_dir = os.path.join(env.subst("$PROJECT_DIR"), ".pio", "freertos_port")
os.makedirs(_port_out_dir, exist_ok=True)
_port_out = os.path.join(_port_out_dir, "port_wokwi.c")

with open(_port_out, "w", newline="") as _fh:
    _fh.write(_wokwi_patch(_port_text))

env.BuildSources(os.path.join("$BUILD_DIR", "FreeRTOSPort"), _port_out_dir)
