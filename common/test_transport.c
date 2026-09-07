///*
// * test_transport.c — host-side unit tests for transport.h (Phase 4 test,
// * mock-backend half). Exercises only Transport_Init/Send/Recv — nothing
// * backend-specific — so this exact test logic can later be reused unchanged
// * against the real UART/serial-backed implementations for the cross-device
// * half of Phase 4.
// */
//
//#include "transport.h"
//#include <assert.h>
//#include <stdio.h>
//#include <string.h>
//
//static void test_transport_send_recv_roundtrip(void)
//{
//    uint8_t sent[5] = { 0x01u, 0x02u, 0x03u, 0x04u, 0x05u };
//    uint8_t received[5] = { 0 };
//    uint16_t n;
//
//    Transport_Init();
//    Transport_Send(sent, sizeof(sent));
//    n = Transport_Recv(received, sizeof(received));
//
//    assert(n == sizeof(sent));
//    assert(memcmp(sent, received, sizeof(sent)) == 0);
//
//    printf("PASS: Transport send/recv round-trip (mock backend)\n");
//}
//
//static void test_transport_recv_empty_when_nothing_sent(void)
//{
//    uint8_t buf[5];
//    uint16_t n;
//
//    Transport_Init();
//    n = Transport_Recv(buf, sizeof(buf));
//    assert(n == 0u);
//
//    printf("PASS: Transport_Recv returns 0 when nothing was sent\n");
//}
//
//int main(void)
//{
//    test_transport_send_recv_roundtrip();
//    test_transport_recv_empty_when_nothing_sent();
//
//    printf("\nAll transport (mock backend) tests passed.\n");
//    return 0;
//}
