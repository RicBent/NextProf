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

#define RECORD_BUFFER_SIZE (0x100000)
#define RECORD_FILE_WRITE_CHUNK_SIZE (0x4000)
#define RECORD_NETWORK_SEND_CHUNK_SIZE (0x1000)

extern u8 recordBuffer[RECORD_BUFFER_SIZE];
extern u8* recordHead;

void recordInit(bool toFile, bool toNetwork);
void recordExit();
void recordFlush();

inline void recordEnsureSpace(u32 size)
{
    if (recordHead + size >= recordBuffer + RECORD_BUFFER_SIZE)
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
