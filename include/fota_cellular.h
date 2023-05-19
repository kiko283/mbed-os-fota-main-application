#ifndef FOTA_CELLULAR_H_
#define FOTA_CELLULAR_H_

#include "mbed.h"
#include "SPIFBlockDevice.h"
#include "sara_r4xx.h"
#include "http_request.h"
#include "MQTTClientMbedOs.h"
#include <array>
#include "tracing.h"

class FOTACellular {

  public:

    FOTACellular(SPIFBlockDevice &spif, Sara_R4xx &modem, NetworkInterface &network);
    ~FOTACellular();
    void run();

  private:

    enum Color {red, green, blue};

  private:

    bool fw_update_available();
    bool fw_update_download_prepare();

    nsapi_error_t connect_cellular();
    nsapi_error_t resolve_hostname();
    void set_tcp_socket_timeout(uint32_t timeout);
    nsapi_error_t open_socket();
    nsapi_error_t connect_socket();
    nsapi_error_t close_socket();

    nsapi_error_t send_data_via_mqtt();

  private:

    Sara_R4xx &_modem;
    NetworkInterface &_net;
    TCPSocket _tcp_socket;
    SocketAddress _socket_address;
    SPIFBlockDevice &_spif;
    // std::array<uint16_t, CELLULAR_RETRY_ARRAY_SIZE> _retry_times = {1, 3, 5, 7, 9, 11, 13, 15, 17, 19};
    std::array<uint16_t, CELLULAR_RETRY_ARRAY_SIZE> _retry_times = {5, 10, 15, 20, 25, 30, 60, 60, 60, 60};
    // std::array<uint16_t, CELLULAR_RETRY_ARRAY_SIZE> _retry_times = {10, 20, 30, 60, 120, 120, 120, 120, 120, 120};
};

#endif
