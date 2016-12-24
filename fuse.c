#include <avr/io.h>

FUSES = {
    .low = (FUSE_CKSEL0 & FUSE_CKSEL2 & FUSE_CKSEL3 & FUSE_SUT0) /*0xE2*/ /*LFUSE_DEFAULT*/,
    .high = HFUSE_DEFAULT,
    .extended = EFUSE_DEFAULT
};
