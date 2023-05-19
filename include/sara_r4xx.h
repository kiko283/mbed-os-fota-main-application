#ifndef SARA_R4XX_H_
#define SARA_R4XX_H_

#include "mbed.h"
#include "UBLOX_PPP.h"

#define MODEM_POWER_PIN_ENGAGE    0
#define MODEM_POWER_PIN_RELEASE   1
#define MODEM_RESET_PIN_ENGAGE    0
#define MODEM_RESET_PIN_RELEASE   1

class Sara_R4xx : public UBLOX_PPP {

  private:
    DigitalOut gpio_pin_power;
    DigitalOut gpio_pin_reset;

  public:
    Sara_R4xx(FileHandle *fh);
    ~Sara_R4xx();

    virtual nsapi_error_t hard_power_on();
    virtual nsapi_error_t hard_power_off();
    virtual nsapi_error_t soft_power_on();
    virtual nsapi_error_t soft_power_off();

    void set_at_timeout(uint32_t timeout_milliseconds);
    void restore_at_timeout();

    nsapi_error_t check_at_commands_working(uint32_t max_ms = 2000);
    nsapi_error_t get_imei(char *imei, size_t size);
    nsapi_error_t set_mno_profile(const char* mnoprof);
    nsapi_error_t set_radio_access_technology(const char* rat);
    nsapi_error_t disable_power_save_mode();
    nsapi_error_t set_led_to_pwr_status();
    nsapi_error_t set_automatic_timezone_update(bool enabled);
    // void custom_at_command();
    void operator_selection_query();
    nsapi_error_t pdp_context_definition();
    nsapi_error_t select_operator(char *plnm);
    nsapi_error_t network_deregister();
    nsapi_error_t eps_network_registration_status();

};

#endif
