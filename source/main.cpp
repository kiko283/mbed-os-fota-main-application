#include "mbed.h"
#include "AT_CellularContext.h"
#include "tracing.h"
#include "fota_cellular.h"
#include "sara_r4xx.h"
#include "SPIFBlockDevice.h"

Thread watchdog_thread(osPriorityNormal, OS_STACK_SIZE, nullptr, "wdt_kick");
LowPowerTimeout reset_timeout;

BufferedSerial serial(MBED_CONF_UBLOX_PPP_TX, MBED_CONF_UBLOX_PPP_RX, MBED_CONF_UBLOX_PPP_BAUDRATE);
SPIFBlockDevice spif;

namespace {
    bool running = true;
}

void kick_watchdog() {

    tr_debug("Watchdog thread");
    Watchdog &watchdog = Watchdog::get_instance();
    uint32_t wdt_max_timeout = 0;
    uint32_t max_sleep = 0;

    if (watchdog.start()) {
        wdt_max_timeout = watchdog.get_max_timeout();
        max_sleep = wdt_max_timeout * 0.9f;
        tr_info("WDT running: max timeout %u ms, set max sleep %u ms", wdt_max_timeout, max_sleep);
    }

    while (running) {
        watchdog.kick();
        tr_debug("kick dog");
        ThisThread::sleep_for(static_cast<Kernel::Clock::duration_u32>(max_sleep));
    }

    tr_debug("Watchdog thread: exit");

}

void run() {
    Sara_R4xx device(&serial);
    NetworkInterface* net = device.create_context(MBED_CONF_NSAPI_DEFAULT_CELLULAR_APN, MBED_CONF_CELLULAR_CONTROL_PLANE_OPT);
    FOTACellular cellular(spif, device, *net);

    if (net != NULL) {
        cellular.run();
    } else {
        tr_error("Failed to initialize network stack");
    }
}

int main() {
    ThisThread::sleep_for(100ms);
    printf("\r\n\r");
    printf("\r----------------------\r\n");
    printf("Main application running...\r\n");
    printf("----------------------\r\n");

    trace_open();

    watchdog_thread.start(kick_watchdog);

    // bd_addr_t fw_version_addr = 0x1c8000 - 4096;
    // spif.init();
    // char *buffer;

    // buffer = (char*)malloc(50);
    // memset(buffer, 'a', 50);
    // spif.erase(fw_version_addr, 4096);
    // spif.program(buffer, fw_version_addr, 50);
    // free(buffer);

    while (1) {
        // // If RTOS is functional and wdt-kick works,
        // // but run() freezes -> trigger software reset
        reset_timeout.attach(NVIC_SystemReset, 6min);
        run();
        reset_timeout.detach();

        // buffer = (char*)malloc(50);
        // spif.read(buffer, fw_version_addr, 50);
        // for(int i=0; i<50; i++) printf("%c", buffer[i] >= 32 ? buffer[i] : ' ');
        // printf("\n");
        // free(buffer);

        serial.enable_input(false);
        serial.enable_output(false);
        ThisThread::sleep_for(30s);
        serial.enable_input(true);
        serial.enable_output(true);
    }

    // spif.deinit();

    running = false;
    watchdog_thread.join();
    trace_close();

    return 0;
}
