#include "mbed.h"

/* CAN test: Loopback */
#define NU_CAN_TEST_LOOPBACK        1

/* CAN test: Extended message ID */
#define NU_CAN_TEST_EXTENDED        1

/* CAN test: Filter */
#define NU_CAN_TEST_FILTER          1

/* CAN test: IRQ
 *
 * Meet crash with the issue:
 * https://github.com/ARMmbed/mbed-os/issues/15243
 *
 * This test is not passed yet.
 */
#define NU_CAN_TEST_IRQ             1

static void send_can_msg(void);
static void recv_can_msg(void);
static void dump_msg(const CANMessage &msg, bool send);
static void dump_bin(const unsigned char *data, size_t length);

#if !NU_CAN_TEST_IRQ
Ticker ticker;
#endif

DigitalOut led1(LED1);
DigitalOut led2(LED2);

//The constructor takes in RX, and TX pin respectively.
//These pins, for this example, are defined in mbed_app.json
#if TARGET_NUMAKER_IOT_M467
CAN can1(PA_13, PA_12);
#if !NU_CAN_TEST_LOOPBACK
CAN can2(PC_0, PC_1);
#endif
#endif

CAN &can_send = can1;

#if NU_CAN_TEST_LOOPBACK
CAN &can_recv = can1;
#else
CAN &can_recv = can2;
#endif

#if NU_CAN_TEST_FILTER
static int filter_handle;
#endif

unsigned char counter = 0;

static auto send_can_msg_event = mbed_event_queue()->make_user_allocated_event(send_can_msg);
#if NU_CAN_TEST_IRQ
static auto recv_can_msg_event = mbed_event_queue()->make_user_allocated_event(recv_can_msg);
#endif

int main()
{
#if NU_CAN_TEST_LOOPBACK
    if (!can_send.mode(CAN::SilentTest)) {
        printf("CAN: Configure to SilentTest mode failed\n\n");
        return EXIT_FAILURE;
    }
#endif

#if NU_CAN_TEST_FILTER
    unsigned int id = 2;
    unsigned int mask = 0x3;
    filter_handle = can_recv.filter(id, mask);
    if (!filter_handle) {
        printf("CAN: Configure filter failed\n\n");
        return EXIT_FAILURE;
    }
    printf("CAN: Filter handle: %d\n", filter_handle);
    printf("CAN: Accept (id & 0x%x) == %d\n", mask, (id & mask));
#endif

    /* Use event queue to avoid running in interrupt context */
#if NU_CAN_TEST_IRQ
    can_send.attach(std::ref(send_can_msg_event), mbed::interface::can::TxIrq);
    can_recv.attach(std::ref(recv_can_msg_event), mbed::interface::can::RxIrq);
    /* Start first send_can_msg(). Following send_can_msg() will get
     * invoked through TxIrq. */
    send_can_msg();
#else
    /* send_can_msg() will get invoked through ticker. */
    ticker.attach(std::ref(send_can_msg_event), 1s);
#endif

    while(1) {
#if NU_CAN_TEST_IRQ
        /* recv_can_msg() will get invoked through RxIrq. */
#else
        recv_can_msg();
#endif

        ThisThread::sleep_for(200ms);
    }

    return 0;
}

static void send_can_msg(void)
{
#if NU_CAN_TEST_EXTENDED
    /* Send standard and extended message IDs alternately */
    CANMessage msg(1337U + counter, &counter, 1, CANData, (counter & 1) ? CANExtended : CANStandard);
#else    
    CANMessage msg(1337U + counter, &counter, 1, CANData, CANStandard);
#endif

    if (can_send.write(msg)) {
        dump_msg(msg, true);

        counter++;
    }
    led1 = !led1;
}

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
