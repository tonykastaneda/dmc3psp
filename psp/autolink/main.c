/* AutoLink: an XMB plugin for ARK-4 that brings PSPLink back when a test run drops to the XMB, so
 * test runs can recover without anyone picking up the PSP.
 *
 * It does nothing unless ms0:/SEPLUGINS/AUTOLINK.ON exists. That file holds one digit: how many more
 * times it may launch PSPLink. Each launch takes one off, and at 0 it stops, so it can't loop forever.
 * tools/autolink.py writes the file while tests run and removes it after.
 *
 * Hold L while the XMB comes up to skip the launch.
 * To remove it by hand, delete SEPLUGINS/AUTOLINK.PRX from the card. */
#include <pspkernel.h>
#include <pspctrl.h>
#include <pspiofilemgr.h>
#include <psploadexec_kernel.h>
#include <string.h>

PSP_MODULE_INFO("AutoLink", 0x1000, 1, 0);

#define ARMED_FILE "ms0:/SEPLUGINS/AUTOLINK.ON"
#define PSPLINK "ms0:/PSP/GAME/CAT_07BREW/PSPLINK/EBOOT.PBP"
#define APITYPE_MS_GAME 0x141
#define SETTLE_MICROSECONDS 2000000

/* ARK-4's SystemControl: see stub.S. */
int sctrlKernelLoadExecVSHWithApitype(int apitype, const char *file, struct SceKernelLoadExecVSHParam *param);

/* Takes one launch off the file. Returns whether there was one to take. */
static int take_a_launch(void) {
    char count = 0;
    SceUID fd = sceIoOpen(ARMED_FILE, PSP_O_RDONLY, 0);
    if (fd < 0) return 0;
    sceIoRead(fd, &count, 1);
    sceIoClose(fd);
    if (count < '1' || count > '9') return 0;
    --count;
    fd = sceIoOpen(ARMED_FILE, PSP_O_WRONLY | PSP_O_TRUNC | PSP_O_CREAT, 0777);
    if (fd < 0) return 0;  /* can't count it down: don't risk looping */
    sceIoWrite(fd, &count, 1);
    sceIoClose(fd);
    return 1;
}

static int thread(SceSize args, void *argp) {
    SceCtrlData pad;
    (void)args; (void)argp;
    sceKernelDelayThread(SETTLE_MICROSECONDS);
    memset(&pad, 0, sizeof(pad));
    sceCtrlPeekBufferPositive(&pad, 1);
    if (!(pad.Buttons & PSP_CTRL_LTRIGGER) && take_a_launch()) {
        struct SceKernelLoadExecVSHParam param;
        memset(&param, 0, sizeof(param));
        param.size = sizeof(param);
        param.args = strlen(PSPLINK) + 1;
        param.argp = (void *)PSPLINK;
        param.key = "game";
        sctrlKernelLoadExecVSHWithApitype(APITYPE_MS_GAME, PSPLINK, &param);
    }
    sceKernelExitDeleteThread(0);
    return 0;
}

int module_start(SceSize args, void *argp) {
    SceUID id = sceKernelCreateThread("AutoLink", thread, 0x18, 0x1000, 0, NULL);
    (void)args; (void)argp;
    if (id >= 0) sceKernelStartThread(id, 0, NULL);
    return 0;
}

int module_stop(SceSize args, void *argp) {
    (void)args; (void)argp;
    return 0;
}
