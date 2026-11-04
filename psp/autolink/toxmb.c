/* Test only: drops the PSP to the XMB, the way a crashed test run can, to see AutoLink recover. */
#include <pspkernel.h>
#include <psploadexec_kernel.h>

PSP_MODULE_INFO("ToXmb", 0x1000, 1, 0);

int sctrlKernelExitVSH(struct SceKernelLoadExecVSHParam *param);

int module_start(SceSize args, void *argp) {
    (void)args; (void)argp;
    sctrlKernelExitVSH(NULL);
    return 1;
}

int module_stop(SceSize args, void *argp) {
    (void)args; (void)argp;
    return 0;
}
