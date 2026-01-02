#pragma once

#include <3ds.h>
#include <string.h>


#define MAKE_RECORD_HEADER(val) \
    ( ((u32)(u8)'N' <<  0) |    \
      ((u32)(u8)'P' <<  8) |    \
      ((u32)(val)   << 16) )

typedef enum {
    RECORD_HEADER_SAMPLE = MAKE_RECORD_HEADER(1),
} RecordHeader;

#undef MAKE_RECORD_HEADER

extern u8* recordHead;
extern u8* recordEnd;

void recordInit();
void recordExit();
void recordFlush();

inline void recordEnsureSpace(u32 size)
{
    if (recordHead + size >= recordEnd)
        recordFlush();
}

inline void recordData(const void* data, u32 size)
{
    memcpy(recordHead, data, size);
    recordHead += size;
}

#define MAKE_RECORD_FUNC(name, type)            \
    inline void record##name(type value)        \
    {                                           \
        *((type*)recordHead) = value;           \
        recordHead += sizeof(type);             \
    }

MAKE_RECORD_FUNC(U8, u8)
MAKE_RECORD_FUNC(U16, u16)
MAKE_RECORD_FUNC(U32, u32)
MAKE_RECORD_FUNC(U64, u64)
MAKE_RECORD_FUNC(S8, s8)
MAKE_RECORD_FUNC(S16, s16)
MAKE_RECORD_FUNC(S32, s32)
MAKE_RECORD_FUNC(S64, s64)
MAKE_RECORD_FUNC(F32, float)
MAKE_RECORD_FUNC(F64, double)

#undef MAKE_RECORD_FUNC

inline void recordHeader(RecordHeader header)
{
    recordU32((u32)header);
}
