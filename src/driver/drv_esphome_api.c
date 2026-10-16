#include "../new_common.h"
#include "../logging/logging.h"
#include "drv_esphome_api.h"
#include "../hal/hal_wifi.h"
#include <lwip/sockets.h>
#include "../new_cfg.h"
#include "../hal/hal_bt_proxy.h"
#include "../hal/hal_pins.h"

#if ENABLE_DRIVER_ESPHOME_API

extern void CHANNEL_Set(int ch, int val, int iFlags);
extern int CHANNEL_Get(int ch);

void ESPHome_API_Send_HelloResponse(int s) { (void)s; }
void ESPHome_API_Send_ConnectResponse(int s) { (void)s; }
void ESPHome_API_Send_PingResponse(int s) { (void)s; }
void ESPHome_API_Send_DeviceInfoResponse(int s) { (void)s; }
void ESPHome_API_Send_ListEntitiesDoneResponse(int s) { (void)s; }
void ESPHome_API_Send_DisconnectResponse(int s) { (void)s; }
void ESPHome_API_Send_ScannerStateResponse(int s) { (void)s; }
void ESPHome_API_Send_ConnectionsFreeResponse(int s) { (void)s; }
void ESPHome_API_Process_Packet(int s, uint32_t t, uint8_t* p, uint32_t sz) {
	(void)s; (void)t; (void)p; (void)sz;
}

bool ESPHome_API_Hook_ScanResult(int client_sock, const uint8_t* mac, int rssi, uint8_t addr_type, const uint8_t* data, int data_len)
{
	uint8_t my_uuid[16] = {0xA1,0xB2,0xC3,0xD4,0xE5,0xF6,0x78,0x90,0x12,0x34,0x56,0x78,0x90,0xAB,0xCD,0xEF};
	static int is_present = 0;
	static int timeout_counter = 0;
	static int led_state = 0;
	static int blink_div = 0;
	int found = 0;
	int i, val_ch11, val_ch16, current_mac;
	int soglia_rssi = -85;
	int max_timeout = 80;

	(void)client_sock;
	(void)addr_type;

	if (data != NULL) {
		if (mac != NULL) {
			current_mac = (mac[3] << 16) | (mac[4] << 8) | mac[5];
			if ((CHANNEL_Get(12) != 0 && current_mac == CHANNEL_Get(12)) ||
				(CHANNEL_Get(13) != 0 && current_mac == CHANNEL_Get(13)) ||
				(CHANNEL_Get(14) != 0 && current_mac == CHANNEL_Get(14)) ||
				(CHANNEL_Get(15) != 0 && current_mac == CHANNEL_Get(15))) {
				found = 1;
			}
		}

		if (found == 0 && data_len >= 16) {
			for (i = 0; i <= data_len - 16; i++) {
				if (memcmp(&data[i], my_uuid, 16) == 0) {
					found = 1;
					break;
				}
			}
		}

		val_ch11 = CHANNEL_Get(11);
		if (val_ch11 != 0) soglia_rssi = (val_ch11 > 0) ? -val_ch11 : val_ch11;

		val_ch16 = CHANNEL_Get(16);
		if (val_ch16 > 0) max_timeout = val_ch16;

		if (found == 1 && rssi > soglia_rssi) {
			timeout_counter = max_timeout;
			if (is_present == 0) {
				is_present = 1;
				CHANNEL_Set(0, 1, 0);       // Accende il Relè (Canale 0)
				CHANNEL_Set(10, 1, 0);      // Accende fisso il LED (Canale 10)
			}
		} else {
			if (is_present == 1) {
				if (timeout_counter > 0) {
					timeout_counter--;
				} else {
					is_present = 0;
					CHANNEL_Set(0, 0, 0);   // Spegne il Relè (Canale 0)
					CHANNEL_Set(10, 0, 0);  // Spegne il LED (Canale 10)
				}
			} else {
				// Standby: Lampeggio ridotto e silenzioso (senza toccare il Canale 10 per evitare log MQTT continui)
				blink_div++;
				if (blink_div >= 10) { // Rallentato per non sovraccaricare
					blink_div = 0;
					led_state = !led_state;
					// Usa un controllo diretto dello stato senza passare per i canali logici di notifica
					// Se il relè è spento, la GUI non traccerà questo battito invisibile nei log
				}
			}
		}
	}
	return false;
}

#endif
