/* ESP32 TWAI transmit example.
  This transmits a message every second.

  Connect a CAN bus transceiver to the RX/TX pins.
  For example: SN65HVD230

  The API gives other possible speeds and alerts:
  https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/twai.html

  created 27-06-2023 by Stephan Martin (designer2k2)
*/

#include <Arduino.h>
#include "driver/twai.h"
#include <stdio.h>

// Pins used to connect to CAN bus transceiver:
#define RX_PIN 41
#define TX_PIN 42

//Pins for Potentiometers
#define POT1_PIN 4

// Interval:
#define TRANSMIT_RATE_MS 1000
#define POLLING_RATE_MS 1000

static bool driver_installed = false;

unsigned long previousMillis = 0;  // will store last time a message was send

void can_monitor_task(void *arg)
{
    uint32_t alerts;
    twai_status_info_t status;

    twai_reconfigure_alerts(TWAI_ALERT_ERR_PASS | TWAI_ALERT_BUS_OFF |
                            TWAI_ALERT_BUS_RECOVERED | TWAI_ALERT_TX_FAILED,
                            NULL);

    for (;;) {
        if (twai_read_alerts(&alerts, pdMS_TO_TICKS(1000)) == ESP_OK) {
            if (alerts & TWAI_ALERT_ERR_PASS) {
                printf("error passive\n");
            }
            if (alerts & TWAI_ALERT_BUS_OFF) {
                printf("bus-off, starting recovery\n");
                twai_initiate_recovery();
            }
            if (alerts & TWAI_ALERT_BUS_RECOVERED) {
                printf("recovered, restarting\n");
                twai_start();
            }
        }

        if (twai_get_status_info(&status) == ESP_OK) {
            printf("state=%d tec=%lu rec=%lu bus_errors=%lu\n",
                   (int)status.state,
                   (unsigned long)status.tx_error_counter,
                   (unsigned long)status.rx_error_counter,
                   (unsigned long)status.bus_error_count);
        }
    }
}

void setup() {
  // Start Serial:

  pinMode(21, OUTPUT);
  digitalWrite(21, LOW);
  Serial.begin(115200);
  delay(10000);
  // Initialize configuration structures using macro initializers
  twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT((gpio_num_t)TX_PIN, (gpio_num_t)RX_PIN, TWAI_MODE_NORMAL);
  twai_timing_config_t t_config = TWAI_TIMING_CONFIG_500KBITS();  //Look in the api-reference for other speed sets.
  twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

  // Install TWAI driver
  if (twai_driver_install(&g_config, &t_config, &f_config) == ESP_OK) {
    Serial.println("Driver installed");
  } else {
    Serial.println("Failed to install driver");
    return;
  }

  // Start TWAI driver
  if (twai_start() == ESP_OK) {
    Serial.println("Driver started");
  } else {
    Serial.println("Failed to start driver");
    return;
  }
  xTaskCreate(can_monitor_task, "can_mon", 4096, NULL, 5, NULL);

  // Reconfigure alerts to detect TX alerts and Bus-Off errors
  uint32_t alerts_to_enable = TWAI_ALERT_TX_IDLE | TWAI_ALERT_TX_SUCCESS | TWAI_ALERT_TX_FAILED | TWAI_ALERT_ERR_PASS | TWAI_ALERT_BUS_ERROR;
  if (twai_reconfigure_alerts(alerts_to_enable, NULL) == ESP_OK) {
    Serial.println("CAN Alerts reconfigured");
  } else {
    Serial.println("Failed to reconfigure alerts");
    return;
  }

  // TWAI driver is now successfully installed and started
  driver_installed = true;
}

esp_err_t send_message(uint32_t id, const uint8_t *buf, uint8_t len) {
  // Send message

  if (len > 8) {
    Serial.println("Yuck");
    return ESP_ERR_INVALID_ARG;
  }

  twai_message_t msg = {0};       // zeroes extd/rtr/ss flags too
  msg.identifier = id;
  msg.data_length_code = len;     // only these bytes go on the wire
  memcpy(msg.data, buf, len);

  // Queue message for transmission
  esp_err_t status = twai_transmit(&msg, pdMS_TO_TICKS(1000));
  if(status == ESP_OK) {
    printf("Message queued for transmission\n");

    //Test Outputs
    uint16_t payload = (msg.data[0] << 8) | msg.data[1];
    printf("Sending message with ID: 0x%X, Payload: %04X\n", msg.identifier, payload);
    //

  } else {
    printf("Failed to queue message for transmission\n");
  }
  return status;
}



void loop() {
  if (!driver_installed) {
    // Driver not installed
    delay(1000);
    return;
  }
  // Check if alert happened
  uint32_t alerts_triggered;
  twai_read_alerts(&alerts_triggered, pdMS_TO_TICKS(POLLING_RATE_MS));
  twai_status_info_t twaistatus;
  twai_get_status_info(&twaistatus);

  // Handle alerts
  if (alerts_triggered & TWAI_ALERT_ERR_PASS) {
    Serial.println("Alert: TWAI controller has become error passive.");
  }
  if (alerts_triggered & TWAI_ALERT_BUS_ERROR) {
    Serial.println("Alert: A (Bit, Stuff, CRC, Form, ACK) error has occurred on the bus.");
    Serial.printf("Bus error count: %" PRIu32 "\n", twaistatus.bus_error_count);
  }
  if (alerts_triggered & TWAI_ALERT_TX_FAILED) {
    Serial.println("Alert: The Transmission failed.");
    Serial.printf("TX buffered: %" PRIu32 "\t", twaistatus.msgs_to_tx);
    Serial.printf("TX error: %" PRIu32 "\t", twaistatus.tx_error_counter);
    Serial.printf("TX failed: %" PRIu32 "\n", twaistatus.tx_failed_count);
  }
  if (alerts_triggered & TWAI_ALERT_TX_SUCCESS) {
    Serial.println("Alert: The Transmission was successful.");
    Serial.printf("TX buffered: %" PRIu32 "\t", twaistatus.msgs_to_tx);
  }


  uint16_t potValue = 0;
  potValue = analogRead(POT1_PIN);
  printf("Potentiometer Value: %d\n", potValue);
  delay(1000);

  
  uint8_t payload[2];
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= TRANSMIT_RATE_MS) {
    previousMillis = currentMillis;

    payload[0] = (uint8_t)(potValue >> 8);  // high byte
    payload[1] = (uint8_t)(potValue & 0xFF); // low byte
  
    send_message(0x0F6, payload, sizeof(payload));
  }
  
}