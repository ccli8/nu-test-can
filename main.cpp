#include "mbed.h"

/* CAN test: Loopback, Tx, or Rx exclusively */
#define NU_CAN_TEST_LOOPBACK        1
#define NU_CAN_TEST_TX              0
#define NU_CAN_TEST_RX              0

/* CAN test: Extended message ID */
#define NU_CAN_TEST_EXTENDED        1

/* CAN test: Filter */
#define NU_CAN_TEST_FILTER          1

/* CAN test: IRQ
 *
 * Avoid crash with the issue: Duplicate post of the same event object in ISR:
 * https://github.com/ARMmbed/mbed-os/issues/15243
 */
#define NU_CAN_TEST_IRQ             0

/* CAN test: Start message ID */
#define NU_CAN_START_MSG_ID         1337U

static void send_can_msg(void);
static void recv_can_msg(void);
static void dump_msg(const CANMessage &msg, bool send);
static void dump_bin(const unsigned char *data, size_t length);

Ticker ticker;

DigitalOut led1(LED1);
DigitalOut led2(LED2);

#if TARGET_NUMAKER_IOT_M467

#if NU_CAN_TEST_LOOPBACK
CAN can_send(PJ_11, PJ_10);
CAN &can_recv = can_send;
#elif NU_CAN_TEST_TX
CAN can_send(PJ_11, PJ_10);
#elif NU_CAN_TEST_RX
CAN can_recv(PJ_11, PJ_10);
#endif

#endif

#if NU_CAN_TEST_FILTER
static int filter_handle = 5;
#endif

unsigned char counter = 0;

#if NU_CAN_TEST_LOOPBACK || NU_CAN_TEST_TX
static auto send_can_msg_event = mbed_event_queue()->make_user_allocated_event(send_can_msg);
#endif

#if NU_CAN_TEST_LOOPBACK || NU_CAN_TEST_RX
#if NU_CAN_TEST_IRQ
static auto recv_can_msg_event = mbed_event_queue()->make_user_allocated_event(recv_can_msg);
#endif
#endif

int main()
{
#if NU_CAN_TEST_LOOPBACK
    if (!can_send.mode(CAN::SilentTest)) {
        printf("CAN: Configure to SilentTest mode failed\n\n");
        return EXIT_FAILURE;
    }
#endif

#if NU_CAN_TEST_LOOPBACK || NU_CAN_TEST_RX
#if NU_CAN_TEST_FILTER
    /* According to link below, filter #0 will accept any message, and
     * no other filters can accept messages without re-configuring filter #0.
     * https://os.mbed.com/questions/85183/How-to-use-CAN-filter-function
     *
     * Re-configure filter #0 to accept message ID 0 only.
     */
    can_recv.filter(0, 0xFFFFFFFF);

    unsigned int id = NU_CAN_START_MSG_ID;
    unsigned int mask = 0x3;
    filter_handle = can_recv.filter(id, mask, CANAny, filter_handle);
    if (!filter_handle) {
        printf("CAN: Configure filter failed\n\n");
        return EXIT_FAILURE;
    }
    printf("CAN: Filter handle: %d\n", filter_handle);
    printf("CAN: Accept (id & 0x%x) == %d\n", mask, (id & mask));
#endif
#endif

    /* Use event queue to avoid running in interrupt context */

#if NU_CAN_TEST_LOOPBACK || NU_CAN_TEST_RX
#if NU_CAN_TEST_IRQ
    can_recv.attach(std::ref(recv_can_msg_event), mbed::interface::can::RxIrq);
#endif
#endif

#if NU_CAN_TEST_LOOPBACK || NU_CAN_TEST_TX
    /* send_can_msg() will get invoked through ticker. */
    ticker.attach(std::ref(send_can_msg_event), 1s);
#endif

    while(1) {
#if NU_CAN_TEST_LOOPBACK || NU_CAN_TEST_RX
#if NU_CAN_TEST_IRQ
        /* recv_can_msg() will get invoked through RxIrq. */
#else
        recv_can_msg();
#endif
#endif

        ThisThread::sleep_for(200ms);
    }

    return 0;
}

#if NU_CAN_TEST_LOOPBACK || NU_CAN_TEST_TX
static void send_can_msg(void)
{
#if NU_CAN_TEST_EXTENDED
    /* Send standard and extended message IDs alternately */
    CANMessage msg(NU_CAN_START_MSG_ID + counter, &counter, 1, CANData, (counter & 1) ? CANExtended : CANStandard);
#else    
    CANMessage msg(NU_CAN_START_MSG_ID + counter, &counter, 1, CANData, CANStandard);
#endif

    if (can_send.write(msg)) {
        dump_msg(msg, true);

        counter++;
    }
    led1 = !led1;
}
#endif

#if NU_CAN_TEST_LOOPBACK || NU_CAN_TEST_RX
static void recv_can_msg(void)
{
    CANMessage msg;

#if NU_CAN_TEST_FILTER
    if (can_recv.read(msg, filter_handle)) {
#else
    if (can_recv.read(msg)) {
#endif
        dump_msg(msg, false);

        led2 = !led2;
    }
}
#endif

static Mutex mutex;

static void dump_msg(const CANMessage &msg, bool send)
{
    ScopedMutexLock lock(mutex);

    printf("Message %s:\n", send ? "sent" : "received");
    printf("ID: %d\n", msg.id);
    printf("Format: %s\n", (msg.format == CANStandard) ? "Standard" : "Extended");
    printf("Type: %s\n", (msg.type == CANData) ? "Data" : "Remote");
    printf("Data length: %d\n", msg.len);
    printf("Data: ");
    dump_bin(msg.data, msg.len);
    printf("\n");
}

static void dump_bin(const unsigned char *data, size_t length)
{
    size_t rmn = length;

    while (rmn >= 8) {
        printf("0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x,\n",
               data[0], data[1], data[2], data[3], data[4], data[5], data[6], data[7]);
        data += 8;
        rmn -= 8;
    }

    if (rmn >= 4) {
        printf("0x%02x, 0x%02x, 0x%02x, 0x%02x, ", data[0], data[1], data[2], data[3]);
        data += 4;
        rmn -= 4;
    }

    if (rmn >= 2) {
        printf("0x%02x, 0x%02x, ", data[0], data[1]);
        data += 2;
        rmn -= 2;
    }

    if (rmn) {
        printf("0x%02x, ", data[0]);
        data += 1;
        rmn -= 1;
    }

    printf("\n");
}
