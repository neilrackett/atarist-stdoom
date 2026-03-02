#include <mint/osbind.h>
#include <mint/cookie.h>

#pragma GCC diagnostic ignored "-Wunused-value"

#ifndef C__MCH
#define C__MCH 0x5f4d4348L /* '_MCH' */
#endif

#define MCH_MEGA_STE   0x00010010L

#define MEGASTE_CTRL_WORD_ADDR 0xffff8e20UL
#define MEGASTE_CTRL_BYTE_ADDR 0xffff8e21UL
#define MEGASTE_CTRL_16MHZ_CACHE 0x03

static int megaste_ctrl_saved_valid = 0;
static unsigned char megaste_ctrl_saved = 0;

int atari_is_megaste_16mhz_cache(void)
{
    static int cached = -1;
    long mch_cookie = 0;

    if (cached >= 0)
        return cached;

    cached = 0;
    if (C_FOUND != Getcookie(C__MCH, &mch_cookie))
        return cached;
    if (mch_cookie != MCH_MEGA_STE)
        return cached;

    {
        volatile unsigned char *ctrlb = (volatile unsigned char *)MEGASTE_CTRL_BYTE_ADDR;
        if ((*ctrlb & MEGASTE_CTRL_16MHZ_CACHE) == MEGASTE_CTRL_16MHZ_CACHE)
            cached = 1;
    }

    return cached;
}

void atari_enable_megaste_turbo(void)
{
    long mch_cookie = 0;
    if (C_FOUND != Getcookie(C__MCH, &mch_cookie))
        return;
    if (mch_cookie != MCH_MEGA_STE)
        return;

    {
        volatile unsigned short *ctrlw = (volatile unsigned short *)MEGASTE_CTRL_WORD_ADDR;
        volatile unsigned char *ctrlb = (volatile unsigned char *)MEGASTE_CTRL_BYTE_ADDR;

        if (!megaste_ctrl_saved_valid) {
            megaste_ctrl_saved = *ctrlb;
            megaste_ctrl_saved_valid = 1;
        }

        /*
         * Mega STe CPU speed/cache register:
         * bit0: 0=8MHz, 1=16MHz
         * bit1: 0=cache off, 1=cache on
         */
        *ctrlw = 0xffff;
        *ctrlb = MEGASTE_CTRL_16MHZ_CACHE;

        Cconws("Mega STe: ");
        if ((*ctrlb & MEGASTE_CTRL_16MHZ_CACHE) == MEGASTE_CTRL_16MHZ_CACHE) {
            Cconws("16MHz + cache on\r\n");
            (void)atari_is_megaste_16mhz_cache();
        } else {
            Cconws("could not lock 16MHz+cache\r\n");
        }
    }
}

void atari_restore_megaste_turbo(void)
{
    long mch_cookie = 0;
    volatile unsigned char *ctrlb = (volatile unsigned char *)MEGASTE_CTRL_BYTE_ADDR;

    if (!megaste_ctrl_saved_valid)
        return;
    if (C_FOUND != Getcookie(C__MCH, &mch_cookie))
        return;
    if (mch_cookie != MCH_MEGA_STE)
        return;

    *ctrlb = megaste_ctrl_saved;
}

// This function is mainly for suppressing mintlib's CPU and FPU detection, but we can also print some 
void _checkcpu() {
    long cookie = 0;
    char m020_detected = 0;
    if (C_FOUND == Getcookie(C__CPU, &cookie)) {
        Cconws("CPU: ");
        if (cookie == 0) Cconws("68000");
        else if (cookie == 10) Cconws("68010");
        else if (cookie == 20) Cconws("68020");
        else if (cookie == 30) Cconws("68030");
        else if (cookie == 40) Cconws("68040");
        else if (cookie == 60) Cconws("68060");
        else Cconws("unknown");
        m020_detected = cookie >= 20;
        Cconws("\r\n");
    } else {
        Cconws("CPU type not detected\r\n");
    }
    char fpu_detected = 0;
    if (C_FOUND == Getcookie(C__FPU, &cookie)) {
        Cconws("FPU:");
        if (1 & (cookie >> 16)) {
            Cconws(" SFP-004");
        }
        if (3 & (cookie >> 17)) {
            unsigned short fpu = 3 & (cookie >> 17);
            if (fpu == 1) Cconws(" 6888?");
            else if (fpu == 2) Cconws(" 68881");
            else if (fpu == 3) Cconws(" 68882");
            fpu_detected = 1;
        }
        if (1 & (cookie >> 19)) {
            Cconws(" integrated");
            fpu_detected = 1;
        }
        Cconws("\r\n");
    } else {
        Cconws("FPU type not detected\r\n");
    }

    char sufficient = 1;
    #ifdef __HAVE_68881__
    sufficient &= fpu_detected;
    #endif
    
    #if defined(__mc68020__) || defined(__mc68030__) || defined(__mc68040__) || defined(__mc68060__)
    sufficient &= m020_detected;
    #endif

    if (sufficient) {
        Cconws("CPU check passed.\r\n");
        return;
    }

    Cconws("\r\nDetected hardware does not\r\n");
    Cconws("meet the requirements for\r\n");
    Cconws("running this executable.\r\n\r\n");
    #if defined(__mc68020__) || defined(__mc68030__) || defined(__mc68040__) || defined(__mc68060__)
    Cconws("This executable requires a 68020+ CPU.\r\n");
    #endif
    #ifdef __HAVE_68881__
    Cconws("This executable requires an FPU.\r\n");
    #endif

    Cconws("\r\n");
    Cconws("Press [i] to ignore.\r\n");
    Cconws("Press any other key to abort.\r\n");

    long key = Cconin() & 0x7f;
    Cconws("\r\n");
    if (key == 'i' || key == 'I') {
        return;
    }

    Pterm(-1);
}
