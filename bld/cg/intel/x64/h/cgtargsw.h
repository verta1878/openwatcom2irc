/*
 * openwatcomirc — x86_64 target-specific code generation switches
 */

#ifndef CG_X64_TARGSW_H
#define CG_X64_TARGSW_H

/* x86_64 target switches — extends the x86 switches */
typedef enum {
    CGSW_X64_RIP_RELATIVE       = 0x00000001,   /* use RIP-relative addressing (always on) */
    CGSW_X64_SYSV_ABI           = 0x00000002,   /* use SysV AMD64 ABI */
    CGSW_X64_RED_ZONE           = 0x00000004,   /* enable 128-byte red zone */
    CGSW_X64_NO_FRAME_POINTER   = 0x00000008,   /* omit frame pointer */
} cg_target_switches;

/* Default switches for Linux x86_64 */
#define CGSW_X64_LINUX_DEFAULT  (CGSW_X64_RIP_RELATIVE | CGSW_X64_SYSV_ABI | CGSW_X64_RED_ZONE)

#endif
