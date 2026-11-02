/* Minimal freestanding PSP entry point for the research bootstrap. */
typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;

typedef struct SceModuleInfo {
    u16 modattribute;
    u8 modversion[2];
    char modname[27];
    char terminal;
    void *gp_value;
    void *ent_top;
    void *ent_end;
    void *stub_top;
    void *stub_end;
} SceModuleInfo;

extern char _gp[];
extern char __lib_ent_top[];
extern char __lib_ent_bottom[];
extern char __lib_stub_top[];
extern char __lib_stub_bottom[];

int app_main(void);
void sceKernelExitGame(void);

const SceModuleInfo module_info
    __attribute__((section(".rodata.sceModuleInfo"), aligned(16), used)) = {
        0, {1, 0}, "DMC3PSP_RESEARCH", 0, _gp,
        __lib_ent_top, __lib_ent_bottom, __lib_stub_top, __lib_stub_bottom
    };

int _start(u32 args, void *argp) {
    (void)args;
    (void)argp;
    (void)app_main();
    sceKernelExitGame();
    for (;;) { }
}

static const u32 __entrytable[4]
    __attribute__((section(".rodata.sceResident"), used)) = {
        0xD632ACDB, 0xF01D73A7, (u32)&_start, (u32)&module_info
    };

struct library_entry {
    const char *name;
    u16 version;
    u16 attribute;
    u8 entLen;
    u8 varCount;
    u16 funcCount;
    const u32 *entrytable;
};

static const struct library_entry _library_entry
    __attribute__((section(".lib.ent"), used)) = {
        (const char *)0, 0, 0x8000, 4, 1, 1, __entrytable
    };
