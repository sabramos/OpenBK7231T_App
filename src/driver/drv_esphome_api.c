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
extern unsigned int g_timeMs; 

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
	// =========================================================================
	// DEFINIZIONE DEI 3 UUID iBEACON
	// =========================================================================
	uint8_t uuid_1[16] = {0xA1,0xB2,0xC3,0xD4,0xE5,0xF6,0x78,0x90,0x12,0x34,0x56,0x78,0x90,0xAB,0xCD,0xEF};
	uint8_t uuid_2[16] = {0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,0x99,0xAA,0xBB,0xCC,0xDD,0xEE,0xFF,0x00};
	uint8_t uuid_3[16] = {0x00,0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,0x99,0xAA,0xBB,0xCC,0xDD,0xEE,0xFF};

	static int is_pulsing = 0;
	static int is_cooldown = 0;
	static unsigned int pulse_start_time = 0;
	static unsigned int cooldown_start_time = 0;
	static unsigned int last_debug_time = 0;
	static int matched_channel = 0;

	int found = 0;
	int i;
	int mac_dritto = 0, mac_invertito = 0;

	(void)client_sock;
	(void)addr_type;

	if (g_timeMs < 15000) {
		CHANNEL_Set(10, 0, 4); 
		return false;
	}

	int val_ch11 = CHANNEL_Get(11);
	if (val_ch11 == 0) {
		CHANNEL_Set(10, 0, 4); 
		return false;
	}
	int soglia_rssi = (val_ch11 > 0) ? -val_ch11 : val_ch11;

	int val_ch17 = CHANNEL_Get(17);
	unsigned int pulse_duration_ms = (val_ch17 > 0) ? (val_ch17 * 1000) : 5000; 

	int val_ch18 = CHANNEL_Get(18);
	unsigned int cooldown_duration_ms = (val_ch18 > 0) ? (val_ch18 * 1000) : 60000;

	// =========================================================================
	// GESTIONE STATI
	// =========================================================================
	if (is_pulsing) {
		unsigned int elapsed = g_timeMs - pulse_start_time;
		if (elapsed >= pulse_duration_ms) {
			is_pulsing = 0;
			is_cooldown = 1; 
			cooldown_start_time = g_timeMs;
			CHANNEL_Set(0, 0, 0);  
		} 
		else {
			if (elapsed < 2000) {
				CHANNEL_Set(10, 1, 4);
			} else {
				unsigned int blink_elapsed = elapsed - 2000;
				int target_blinks = 1;
				
				if (matched_channel >= 12 && matched_channel <= 15) {
					target_blinks = matched_channel - 10; 
				} else if (matched_channel >= 19 && matched_channel <= 21) {
					target_blinks = matched_channel - 13; 
				}
				
				unsigned int current_toggle = blink_elapsed / 150; 
				if (current_toggle < (unsigned int)(target_blinks * 2)) {
					CHANNEL_Set(10, (current_toggle % 2 == 0) ? 0 : 1, 4);
				} else {
					CHANNEL_Set(10, 1, 4); 
				}
			}
		}
		return false; 
	}

	if (is_cooldown) {
		if ((g_timeMs - cooldown_start_time) >= cooldown_duration_ms) {
			is_cooldown = 0;
		} else {
			CHANNEL_Set(10, ((g_timeMs / 1000) % 2 == 0) ? 1 : 0, 4);
			return false; 
		}
	}

	CHANNEL_Set(10, ((g_timeMs / 250) % 2 == 0) ? 1 : 0, 4);

	// =========================================================================
	// MATCHING
	// =========================================================================
	if (mac != NULL) {
		mac_dritto = (mac[3] << 16) | (mac[4] << 8) | mac[5];
		mac_invertito = (mac[2] << 16) | (mac[1] << 8) | mac[0];

		if ((g_timeMs - last_debug_time) >= 1000) {
			last_debug_time = g_timeMs;
			ADDLOG_INFO(LOG_FEATURE_MAIN, "BLE Check: MAC %02X:%02X:%02X:%02X:%02X:%02X | RSSI: %d dBm | Inv: %d", 
				mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], rssi, mac_invertito);
		}

		if (CHANNEL_Get(12) != 0 && (mac_dritto == CHANNEL_Get(12) || mac_invertito == CHANNEL_Get(12))) {
			found = 1; matched_channel = 12;
		} else if (CHANNEL_Get(13) != 0 && (mac_dritto == CHANNEL_Get(13) || mac_invertito == CHANNEL_Get(13))) {
			found = 1; matched_channel = 13;
		} else if (CHANNEL_Get(14) != 0 && (mac_dritto == CHANNEL_Get(14) || mac_invertito == CHANNEL_Get(14))) {
			found = 1; matched_channel = 14;
		} else if (CHANNEL_Get(15) != 0 && (mac_dritto == CHANNEL_Get(15) || mac_invertito == CHANNEL_Get(15))) {
			found = 1; matched_channel = 15;
		}
	}

	// BLOCCO UUID BLINDATO
	if (found == 0 && data != NULL && data_len >= 16) {
		for (i = 0; i <= data_len - 16; i++) {
			// Esegue la memcmp SOLO SE il canale corrispondente è rigorosamente maggiore di 0
			if (CHANNEL_Get(19) > 0) {
				if (memcmp(&data[i], uuid_1, 16) == 0) {
					found = 1; matched_channel = 19; break;
				}
			}
			if (CHANNEL_Get(20) > 0) {
				if (memcmp(&data[i], uuid_2, 16) == 0) {
					found = 1; matched_channel = 20; break;
				}
			}
			if (CHANNEL_Get(21) > 0) {
				if (memcmp(&data[i], uuid_3, 16) == 0) {
					found = 1; matched_channel = 21; break;
				}
			}
		}
	}

	if (found == 1 && rssi > soglia_rssi) {
		is_pulsing = 1;
		pulse_start_time = g_timeMs;

		CHANNEL_Set(0, 1, 0);  
		CHANNEL_Set(10, 1, 4); 
		
		if (matched_channel >= 19 && matched_channel <= 21) {
			ADDLOG_INFO(LOG_FEATURE_MAIN, "BLE MATCH SUCCESS! UUID Canale %d attivato (RSSI: %d dBm)", matched_channel, rssi);
		} else {
			ADDLOG_INFO(LOG_FEATURE_MAIN, "BLE MATCH SUCCESS! MAC Canale %d attivato (RSSI: %d dBm)", matched_channel, rssi);
		}
	}

	return false;
}

#endif
