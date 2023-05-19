#include "sara_r4xx.h"

Sara_R4xx::Sara_R4xx(mbed::FileHandle *fh) : 
    UBLOX_PPP(fh), gpio_pin_power(ARDUINO_UNO_D5, MODEM_POWER_PIN_RELEASE),
    gpio_pin_reset(ARDUINO_UNO_D6, MODEM_RESET_PIN_RELEASE) {}

Sara_R4xx::~Sara_R4xx() {}

nsapi_error_t Sara_R4xx::hard_power_on() {
    gpio_pin_power.write(MODEM_POWER_PIN_ENGAGE);
    ThisThread::sleep_for(1000ms);
    gpio_pin_power.write(MODEM_POWER_PIN_RELEASE);
    ThisThread::sleep_for(500ms);
    return NSAPI_ERROR_OK;
}

nsapi_error_t Sara_R4xx::hard_power_off() {
    gpio_pin_power.write(MODEM_POWER_PIN_ENGAGE);
    ThisThread::sleep_for(2000ms);
    gpio_pin_power.write(MODEM_POWER_PIN_RELEASE);
    ThisThread::sleep_for(500ms);
    return NSAPI_ERROR_OK;
}

nsapi_error_t Sara_R4xx::soft_power_on() {
    return NSAPI_ERROR_OK;
}

nsapi_error_t Sara_R4xx::soft_power_off() {
    return NSAPI_ERROR_OK;
}

void Sara_R4xx::set_at_timeout(uint32_t timeout_milliseconds) {
    _at.set_at_timeout(mbed::chrono::milliseconds_u32(timeout_milliseconds));
}

void Sara_R4xx::restore_at_timeout() {
    _at.restore_at_timeout();
}

nsapi_error_t Sara_R4xx::check_at_commands_working(uint32_t max_ms) {
    nsapi_error_t ret;
    _at.set_at_timeout(200ms);
    for (uint32_t i = 0; i < max_ms; i += 200) {
        _at.lock();
        ret = _at.at_cmd_discard("", "");
        if (_at.unlock_return_error() == NSAPI_ERROR_OK) {
            break;
        } else {
            ThisThread::sleep_for(200ms);
        }
    }
    _at.restore_at_timeout();
    return ret;
}

nsapi_error_t Sara_R4xx::get_imei(char *imei, size_t size) {

    _at.lock();
    _at.set_at_timeout(2000ms);
    _at.at_cmd_discard("E0", "");

    _at.cmd_start_stop("+CGSN", "");
    _at.resp_start();
    _at.read_string(imei, size);
    _at.resp_stop();
    _at.restore_at_timeout();

    return _at.unlock_return_error();
}

nsapi_error_t Sara_R4xx::set_mno_profile(const char* mnoprof) {

    nsapi_error_t ret = NSAPI_ERROR_OK;
    const size_t buf_size = 100;
    char buf[buf_size];
    memset(buf, 0, buf_size);

    _at.set_at_timeout(2000ms);
    _at.lock();
    _at.cmd_start_stop("+UMNOPROF","?");
    _at.resp_start("+UMNOPROF:");
    _at.read_string(buf, buf_size);
    _at.resp_stop();
    _at.set_default_delimiter();
    ret = _at.unlock_return_error();

    // // for debug:
    // printf("umnoprof: '%s'. ret=%d\r\n", buf, ret);

    if (strstr(buf, mnoprof) == NULL) {
        _at.lock();
        _at.at_cmd_discard("E0", "");
        _at.at_cmd_discard("+CFUN=0", "");
        _at.at_cmd_discard("+UMNOPROF=", mnoprof);
        _at.at_cmd_discard("+CFUN=15", "");

        ret = _at.unlock_return_error();

        check_at_commands_working();
    } else {
        // // for debug
        // printf("MNO profile already set to '%s'\r\n", mnoprof);
    }

    _at.restore_at_timeout();
    return ret;
}

nsapi_error_t Sara_R4xx::set_radio_access_technology(const char* rat) {

    nsapi_error_t ret = NSAPI_ERROR_OK;
    const size_t buf_size = 100;
    char buf[buf_size];
    memset(buf, 0, buf_size);

    _at.set_at_timeout(2000ms);
    _at.lock();
    _at.cmd_start_stop("+URAT","?");
    _at.resp_start("+URAT:");
    int32_t first_param = 666;
    first_param = _at.read_int();
    _at.resp_stop();
    _at.set_default_delimiter();
    ret = _at.unlock_return_error();

    int32_t rat_as_int = std::strtol(rat, NULL, 10);

    // // for debug:
    // printf("urat: '%d'. ret=%d\r\n", first_param, ret);

    if (first_param != rat_as_int) {
        _at.lock();
        _at.at_cmd_discard("E0", "");
        _at.at_cmd_discard("+CFUN=0", "");
        _at.at_cmd_discard("+URAT=", rat);
        _at.at_cmd_discard("+CFUN=15", "");

        ret = _at.unlock_return_error();

        check_at_commands_working();
    } else {
        // // for debug
        // printf("RAT already set to '%s'\r\n", rat);
    }

    _at.restore_at_timeout();
    return ret;

}

nsapi_error_t Sara_R4xx::disable_power_save_mode() {

    nsapi_error_t ret = NSAPI_ERROR_OK;
    const size_t buf_size = 100;
    char buf[buf_size];
    memset(buf, 0, buf_size);

    _at.set_at_timeout(2000ms);
    _at.lock();
    _at.cmd_start_stop("+CPSMS","?");
    _at.resp_start("+CPSMS:");
    int32_t first_param = 666;
    first_param = _at.read_int();
    _at.resp_stop();
    _at.set_default_delimiter();
    ret = _at.unlock_return_error();

    // // for debug:
    // printf("cpsms: '%d'. ret=%d\r\n", first_param, ret);

    if (first_param != 0) {
        _at.lock();
        _at.at_cmd_discard("E0", "");
        _at.at_cmd_discard("+CFUN=0", "");
        _at.at_cmd_discard("+CPSMS=0", "");
        _at.at_cmd_discard("+CFUN=15", "");

        ret = _at.unlock_return_error();

        check_at_commands_working();
    } else {
        // // for debug
        // printf("Power save mode already disabled\r\n");
    }

    _at.restore_at_timeout();
    return ret;
}

nsapi_error_t Sara_R4xx::set_led_to_pwr_status() {

    nsapi_error_t ret = NSAPI_ERROR_OK;

    _at.set_at_timeout(2000ms);
    _at.lock();
    _at.at_cmd_discard("E0", "");
    _at.at_cmd_discard("+UGPIOC=16,10", "");
    ret = _at.unlock_return_error();
    _at.restore_at_timeout();
    return ret;
}

nsapi_error_t Sara_R4xx::set_automatic_timezone_update(bool enabled) {

    _at.lock();
    _at.set_at_timeout(2000ms);
    _at.at_cmd_discard("+CTZU=", enabled ? "1" : "0");
    _at.restore_at_timeout();
    return _at.unlock_return_error();

}
/*
void Sara_R4xx::custom_at_command() {

}
// */

/*
void Sara_R4xx::operator_selection_query() {

    const size_t buf_size = 100;
    char buf[buf_size];
    memset(buf, 0, buf_size);

    _at.set_at_timeout(2000ms);
    _at.lock();
    _at.cmd_start_stop("+COPS","?");
    _at.resp_start();
    int32_t first_param = 666;
    int32_t second_param = 666;
    int32_t third_param = 666;
    first_param = _at.read_int();
    second_param = _at.read_int();
    _at.read_string(buf, buf_size);
    third_param = _at.read_int();
    _at.resp_stop();
    _at.set_default_delimiter();
    nsapi_error_t ret = _at.unlock_return_error();
    _at.restore_at_timeout();

    // // for debug:
    // printf("%ld, %ld, %s, %ld. ret=%d\r\n",
    //     first_param, second_param, buf, third_param, ret);

}
// */

/*
nsapi_error_t Sara_R4xx::pdp_context_definition() {

    _at.lock();
    _at.set_at_timeout(2000ms);
    _at.at_cmd_discard("+CGDCONT=", "1,\"IP\",\"super\"");
    _at.restore_at_timeout();
    return _at.unlock_return_error();

}
// */

/*
nsapi_error_t Sara_R4xx::select_operator(char *plnm) {

    _at.lock();
    _at.set_at_timeout(180000ms);
    char buf[50] = "1,2,\"";
    strncat(buf, plnm, 5);
    strcat(buf, "\",7");
    _at.at_cmd_discard("+COPS=", buf);
    _at.restore_at_timeout();
    return _at.unlock_return_error();

}
// */

/*
nsapi_error_t Sara_R4xx::network_deregister() {

    _at.lock();
    _at.set_at_timeout(2000ms);
    _at.at_cmd_discard("+COPS=", "2");
    _at.restore_at_timeout();
    return _at.unlock_return_error();

}
// */

/*
nsapi_error_t Sara_R4xx::eps_network_registration_status() {

    _at.lock();
    _at.set_at_timeout(2000ms);
    _at.at_cmd_discard("+CEREG?", "");
    _at.restore_at_timeout();
    return _at.unlock_return_error();

}
// */