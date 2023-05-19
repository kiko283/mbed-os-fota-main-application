#include "fota_cellular.h"

static const char* mqtt_broker_hostname = "broker.emqx.io";
static const uint32_t mqtt_broker_port = 1883;
static const char* mqtt_topic_prefix = "FOTAExample/";
// static const char* mqtt_username = "username";
// static const char* mqtt_password = "password";
static const char* mqtt_client_id = "FOTAExample";

static const uint32_t retry_count = 3;

static const size_t imei_len = 15;
char imei[imei_len + 1] = {'\0'};

#ifndef MBED_CONF_APP_FOTA_CLOUD_HOSTNAME
#error "fota_cloud_hostname must be set inside config in mbed_app.json"
#endif
#ifndef MBED_CONF_APP_FLASH_SIZE
#error "flash_size must be set inside config in mbed_app.json"
#endif
#ifndef MBED_CONF_APP_BOOTLOADER_SIZE
#error "bootloader_size must be set inside config in mbed_app.json"
#endif

static const char* url_fota_get_latest = MBED_CONF_APP_FOTA_CLOUD_HOSTNAME "/fota/latest";
static const char* url_fota_dl_latest = MBED_CONF_APP_FOTA_CLOUD_HOSTNAME "/fota/dl";
ParsedUrl* url_obj_ref;

const bd_size_t mcu_flash_size = MBED_CONF_APP_FLASH_SIZE;
const bd_size_t bootloader_size = MBED_CONF_APP_BOOTLOADER_SIZE;
const bd_size_t fw_max_size = mcu_flash_size - bootloader_size;
bd_size_t spif_size = 0;
bd_size_t spif_erase_size = 0;

// version format => 20230101_010101
static const uint8_t fw_version_len = 15;
bd_addr_t current_fw_version_addr;
bd_addr_t dl_fw_version_addr;
bd_addr_t fw_addr = 0;
bd_addr_t fw_chunk_addr;

// char current_fw_version[fw_version_len + 1] = {'\0'};
char latest_fw_version[fw_version_len + 1] = {'\0'};

FOTACellular::FOTACellular(SPIFBlockDevice &spif, Sara_R4xx &modem, NetworkInterface &network) :
    _spif(spif), _modem(modem), _net(network)
{
    _modem.set_retry_timeout_array(_retry_times.begin(), _retry_times.size());
    // // to disable modem connection retries, uncomment following
    // _modem.set_retry_timeout_array(NULL, 0);
}

FOTACellular::~FOTACellular() {}

void FOTACellular::run() {

    nsapi_error_t ret;

    tr_info("spif init");
    _spif.init();
    spif_size = _spif.size();
    spif_erase_size = _spif.get_erase_size();

    if (fw_max_size % spif_erase_size != 0) {
        fw_addr = spif_size - (((fw_max_size / spif_erase_size)+1) * spif_erase_size);
    } else {
        fw_addr = spif_size - fw_max_size;
    }
    dl_fw_version_addr = fw_addr - spif_erase_size;
    current_fw_version_addr = dl_fw_version_addr - spif_erase_size;
    tr_info("spif size: 0x%llx", spif_size);
    tr_info("fw addr: 0x%llx", fw_addr);
    tr_info("dl fw version addr: 0x%llx", dl_fw_version_addr);
    tr_info("current fw version addr: 0x%llx", current_fw_version_addr);

    // char* buf = (char*) malloc(sizeof(char) * (fw_version_len));
    // memset(buf, 0x23, fw_version_len);
    // _spif.read(buf, current_fw_version_addr, fw_version_len);
    // printf("current fw version: ");
    // for(uint8_t i=0; i<fw_version_len; i++) printf("%c", buf[i] >= 32 ? buf[i] : ' ');
    // printf("\n");
    // memset(buf, 0x23, fw_version_len);
    // _spif.read(buf, dl_fw_version_addr, fw_version_len);
    // printf("dl fw version: ");
    // for(uint8_t i=0; i<fw_version_len; i++) printf("%c", buf[i] >= 32 ? buf[i] : ' ');
    // printf("\n");
    // free(buf);

    tr_info("net init");
    _net.set_default_parameters();

    for (size_t i=0; i<3; i++) {
        tr_info("turning modem on (attempt #%d)", i+1);
        _modem.hard_power_on();
        ret = _modem.check_at_commands_working();
        if (ret == NSAPI_ERROR_OK) {
            tr_info("modem is on");
            break;
        }
    }

    if (ret != NSAPI_ERROR_OK) {
        tr_info("unable to start modem");
        _modem.hard_power_off();
        NVIC_SystemReset();
        return;
    }

    // Set default timeout for modem
    _modem.set_timeout(5000);

    _modem.get_imei(imei, imei_len + 1);
    tr_info("imei=%s", imei);

    _modem.set_mno_profile("100");
    // Give some time for the modem to process things
    ThisThread::sleep_for(100ms);

    _modem.disable_power_save_mode();
    // Give some time for the modem to process things
    ThisThread::sleep_for(100ms);

    _modem.set_radio_access_technology("9");
    // Give some time for the modem to process things
    ThisThread::sleep_for(100ms);

    if (connect_cellular() != NSAPI_ERROR_OK) {
        tr_info("Connect cellular failed");
    } else {

        if (send_data_via_mqtt() != NSAPI_ERROR_OK) {
            tr_error("Sending data to MQTT failed");
        }

        if (fw_update_available()) {
            tr_info("Firmware update available");
            if (fw_update_download_prepare()) {
                // firmware will be installed by bootloader upon device reboot
                NVIC_SystemReset();
            }
        } else {
            tr_info("Firmware up to date");
        }

        ret = _net.disconnect();
        if (ret != NSAPI_ERROR_OK) {
            tr_error("Disconnect failed: %d", ret);
        } else {
            tr_info("Disconnect success");
        }
    }

    tr_info("spif deinit");
    _spif.deinit();

    tr_info("turning modem off");
    _modem.hard_power_off();

}

/**
 * Checks if firmware update is available by connecting to cloud server.
 * Gets the latest firmware version from the server, then compares it with the current fw version.
 *
 * @note
 *   Firmware versions are in format YYYYmmdd_HHMMSS
 *
 * @returns
 *   A boolean value representing if firmware update is available
*/
bool FOTACellular::fw_update_available(){

    bool ret = false;

    if (open_socket() != NSAPI_ERROR_OK) {
        tr_error("unable to open socket");
        return false;
    }

    set_tcp_socket_timeout(20000);

    url_obj_ref = new ParsedUrl(url_fota_get_latest);
    _socket_address.set_ip_address(url_obj_ref->host());
    _socket_address.set_port(url_obj_ref->port());
    delete url_obj_ref;

    if (connect_socket() != NSAPI_ERROR_OK) {
        tr_error("unable to open socket");
        return false;
    }

    HttpRequest* request = new HttpRequest(&_tcp_socket, HTTP_GET, url_fota_get_latest);
    HttpResponse* response = request->send(NULL, 0);
    int response_status_code = response->get_status_code();

    tr_info("status is %d - %s", response_status_code, response->get_status_message().c_str());

    if (response_status_code == 200) {
        memcpy(latest_fw_version, response->get_body_as_string().c_str(), fw_version_len);
        // printf("latest fw version from cloud: %s\r\n", response->get_body_as_string().c_str());
        // printf("latest fw version in var: %s\r\n", latest_fw_version);
    }

    delete request; // also clears out the response

    if (close_socket() != NSAPI_ERROR_OK) {
        tr_err("unable to close socket");
        return false;
    }

    if (response_status_code != 200) return false;

    char *current_fw_version = (char*) malloc(fw_version_len+1);
    memset(current_fw_version, 0x23, fw_version_len);
    _spif.read(current_fw_version, current_fw_version_addr, fw_version_len);
    current_fw_version[fw_version_len] = '\0';

    tr_info("latest fw (%s) <=> (%s) current fw", latest_fw_version, current_fw_version);
    if (strncmp(latest_fw_version, current_fw_version, fw_version_len) != 0) {
        ret = true;
    } else {
        ret = false;
    }

    free(current_fw_version);
    return ret;
}

/**
 * Downloads the latest firmware from the cloud server, stores it at the end of SPI flash.
 * Firmware download is performed in chunks, because embedded devices will not have enough RAM to store the full firmware.
 * On successful download, latest firmware version is updated as well (also in SPI flash).
 *
 * @returns
 *   A boolean value representing firmware download success
*/
bool FOTACellular::fw_update_download_prepare(){

    bool ret = true;

    if (open_socket() != NSAPI_ERROR_OK) {
        tr_error("unable to open socket");
        return false;
    }

    set_tcp_socket_timeout(120000);

    url_obj_ref = new ParsedUrl(url_fota_dl_latest);
    _socket_address.set_ip_address(url_obj_ref->host());
    _socket_address.set_port(url_obj_ref->port());
    delete url_obj_ref;

    if (connect_socket() != NSAPI_ERROR_OK) {
        tr_error("unable to open socket");
        return false;
    }

    fw_chunk_addr = fw_addr;
    // spif has to be erased before programming
    _spif.erase(fw_addr, fw_max_size);

    HttpRequest* request = new HttpRequest(&_tcp_socket, HTTP_GET, url_fota_dl_latest, [this] (const char* data, uint32_t data_len) {
        // spif has to be initialized
        _spif.program(data, fw_chunk_addr, (mbed::bd_size_t)data_len);
        fw_chunk_addr += data_len;
    });
    HttpResponse* response = request->send(NULL, 0);

    if (response->get_status_code() != 200) {
        ret = false;
        tr_info("status is %d - %s", response->get_status_code(), response->get_status_message().c_str());
    } else {
        // update the dl_fw_version variable inside spi flash
        _spif.erase(dl_fw_version_addr, spif_erase_size);
        _spif.program(latest_fw_version, dl_fw_version_addr, fw_version_len);
    }

    delete request; // also clears out the response

    if (close_socket() != NSAPI_ERROR_OK) {
        tr_err("unable to close socket");
    }

    return ret;
}

nsapi_error_t FOTACellular::connect_cellular() {
    nsapi_error_t ret;
    tr_debug("Establishing cellular connection");
    _modem.set_at_timeout(30000);
    if (_net.get_connection_status() != NSAPI_STATUS_GLOBAL_UP) {
        ret = _net.connect();
        if (ret == NSAPI_ERROR_OK) {
            tr_debug("Cellular connection established");
        } else if (ret == NSAPI_ERROR_AUTH_FAILURE) {
            tr_error("Authentication Failure");
        } else {
            tr_error("Cellular connection failure: %d", ret);
        }
    }
    _modem.restore_at_timeout();
    return ret;
}

nsapi_error_t FOTACellular::resolve_hostname() {
    nsapi_error_t ret;
    ret = _net.gethostbyname(mqtt_broker_hostname, &_socket_address);
    if (ret != NSAPI_ERROR_OK) {
        tr_error("Couldn't resolve remote host: %s, code: %d", mqtt_broker_hostname, ret);
    }
    _socket_address.set_port(mqtt_broker_port);
    return ret;
}

void FOTACellular::set_tcp_socket_timeout(uint32_t timeout) {
    _tcp_socket.set_timeout(timeout);
}

nsapi_error_t FOTACellular::open_socket() {
    nsapi_error_t ret;
    for (size_t i = 0; i < retry_count; i++) {
        ret = _tcp_socket.open(&_net);
        if (ret) {
            tr_error("TCP-Socket open failure: %d", ret);
        } else {
            tr_debug("TCP-Socket open success");
            break;
        }
        ThisThread::sleep_for(10ms);
    }
    return ret;
}

nsapi_error_t FOTACellular::connect_socket() {
    nsapi_error_t ret;
    for (size_t i = 0; i < retry_count; i++) {
        ret = _tcp_socket.connect(_socket_address);
        if (ret != NSAPI_ERROR_OK) {
            tr_error("TCP-Socket connect failure: %d", ret);
        } else {
            tr_debug("TCP-Socket connected to address %s:%u", _socket_address.get_ip_address(), _socket_address.get_port());
            break;
        }
        ThisThread::sleep_for(10ms);
    }
    return ret;
}

nsapi_error_t FOTACellular::close_socket() {
    nsapi_error_t ret;
    for (size_t i = 0; i < retry_count; i++) {
        ret = _tcp_socket.close();
        if (ret != NSAPI_ERROR_OK) {
            tr_error("TCP-socket close failure: %d", ret);
        } else {
            tr_debug("TCP-socket close success");
            break;
        }
        ThisThread::sleep_for(10ms);
    }
    return ret;
}

nsapi_error_t FOTACellular::send_data_via_mqtt() {

    nsapi_error_t ret;

    ret = open_socket();
    if (ret != NSAPI_ERROR_OK) return ret;

    set_tcp_socket_timeout(60000);

    ret = resolve_hostname();
    if (ret != NSAPI_ERROR_OK) return ret;

    ret = connect_socket();
    if (ret != NSAPI_ERROR_OK) return ret;

    /* MQTT part */
    MQTTPacket_connectData options = MQTTPacket_connectData_initializer;
    options.clientID.cstring = (char*) mqtt_client_id;
    // options.username.cstring = (char*) mqtt_username;
    // options.password.cstring = (char*) mqtt_password;
    MQTTClient* mqtt_client = new MQTTClient(&_tcp_socket);

    ret = mqtt_client->connect(options);
    if (ret != NSAPI_ERROR_OK) {
        tr_error("MQTT-client connect fails: %d", ret);
        return ret;
    } else {
        tr_info("MQTT-client connect success");
    }

    char buf[15] = "test message";
    char topic[30];
    sprintf(topic, "%s/%.*s", mqtt_topic_prefix, imei_len, imei);

    MQTT::Message message;
    message.qos = MQTT::QOS0;
    message.retained = false;
    message.dup = false;
    message.payload = buf;
    message.payloadlen = strlen(buf);

    tr_info("topic: \"%s\"", topic);

    ret = mqtt_client->publish(topic, message);
    if (ret != NSAPI_ERROR_OK) {
        tr_error("MQTT-client publish failure: %d", ret);
    } else {
        tr_info("MQTT-client publish success");
    }

    ret = mqtt_client->yield(100);
    if (ret != NSAPI_ERROR_OK) {
        tr_info("MQTT-client yield failure: %d", ret);
    } else {
        tr_info("MQTT-client yield success");
    }

    ret = mqtt_client->disconnect();
    if (ret != NSAPI_ERROR_OK) {
        tr_error("MQTT-client disconnect failure: %d", ret);
    } else {
        tr_info("MQTT-client disconnect success");
    }

    delete mqtt_client;
    /*************/

    ret = close_socket();
    if (ret != NSAPI_ERROR_OK) return ret;

    return NSAPI_ERROR_OK;
}
