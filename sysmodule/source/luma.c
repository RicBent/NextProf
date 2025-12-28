#include <3ds.h>

Result PMDBG_LumaDebugNextApplicationByForce(bool debug)
{
    Result ret = 0;
    u32 *cmdbuf = getThreadCommandBuffer();
    cmdbuf[0] = IPC_MakeHeader(0x101, 1, 0);
    cmdbuf[1] = (u32)debug;

    if(R_FAILED(ret = svcSendSyncRequest(*pmDbgGetSessionHandle()))) return ret;
    return cmdbuf[1];
}
