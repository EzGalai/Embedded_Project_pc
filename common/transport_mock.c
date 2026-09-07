///*
// * transport_mock.c — host-side-only mock backend for transport.h (Phase 4).
// *
// * A pure in-memory ring buffer, no hardware involved: Transport_Send writes
// * directly into the same buffer Transport_Recv reads from, giving a genuine
// * self-contained loopback. Used only to prove the transport.h interface
// * itself is sound, and to test higher-level code against it without needing
// * the board. Never linked into the real LNC firmware or lnc_bridge.
// */
//
//#include "transport.h"
//
//#define TRANSPORT_MOCK_BUFFER_SIZE 256
//
//static uint8_t mockBuffer[TRANSPORT_MOCK_BUFFER_SIZE];
//static uint16_t mockHead = 0;
//static uint16_t mockTail = 0;
//
//void Transport_Init(void)
//{
//    mockHead = 0;
//    mockTail = 0;
//}
//
//void Transport_Send(const uint8_t *data, uint16_t len)
//{
//    uint16_t i;
//
//    for (i = 0; i < len; i++) {
//        uint16_t nextHead = (uint16_t)((mockHead + 1) % TRANSPORT_MOCK_BUFFER_SIZE);
//        if (nextHead == mockTail) {
//            break; /* buffer full — drop remaining bytes, mirroring the real ring buffer's behavior */
//        }
//        mockBuffer[mockHead] = data[i];
//        mockHead = nextHead;
//    }
//}
//
//uint16_t Transport_Recv(uint8_t *outBuf, uint16_t maxLen)
//{
//    uint16_t count = 0;
//
//    while (count < maxLen && mockTail != mockHead) {
//        outBuf[count++] = mockBuffer[mockTail];
//        mockTail = (uint16_t)((mockTail + 1) % TRANSPORT_MOCK_BUFFER_SIZE);
//    }
//    return count;
//}
