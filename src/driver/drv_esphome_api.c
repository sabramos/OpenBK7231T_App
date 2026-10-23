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
extern unsigned int g_timeMs; // Variabile di sistema OS per il tempo in millisecondi

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
	// L'UUID del tuo iBeacon per l'apertura (16 byte)
	uint8_t my_uuid[16] = {0xA1,0xB2,0xC3,0xD4,0xE5,0xF6,0x78,0x90,0x12,0x34,0x56,0x78,0x90,0xAB,0xCD,0xEF};
	
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

	// Guardia Wi-Fi iniziale (15s di boot)
	if (g_timeMs < 15000) {
		CHANNEL_Set(10, 0, 4); // LED Spento all'avvio
		return false;
	}

	// Acquisizione impostazioni dalla Web UI
	int val_ch11 = CHANNEL_Get(11);
	if (val_ch11 == 0) {
		CHANNEL_Set(10, 0, 4); // Se il sistema è disattivato (Canale 11 a 0), spegni il LED
		return false;
	}
	int soglia_rssi = (val_ch11 > 0) ? -val_ch11 : val_ch11;

	int val_ch17 = CHANNEL_Get(17);
	unsigned int pulse_duration_ms = (val_ch17 > 0) ? (val_ch17 * 1000) : 10000; 

	int val_ch18 = CHANNEL_Get(18);
	unsigned int cooldown_duration_ms = (val_ch18 > 0) ? (val_ch18 * 1000) : 60000;

	// =========================================================================
	// MACCHINA A STATI: GESTIONE IMPULSO (RELE' ATTIVO)
	// =========================================================================
	if (is_pulsing) {
		unsigned int elapsed = g_timeMs - pulse_start_time;
		
		// Controllo fine impulso
		if (elapsed >= pulse_duration_ms) {
			is_pulsing = 0;
			is_cooldown = 1; 
			cooldown_start_time = g_timeMs;
			matched_channel = 0;
			
			CHANNEL_Set(0, 0, 0);  // Spegne il relè
		} 
		else {
			// Gestione visiva LED durante l'impulso
			if (elapsed < 2000) {
				// Primi 2 secondi: LED fisso per confermare l'attivazione immediata
				CHANNEL_Set(10, 1, 4);
			} else {
				// Dopo 2 secondi: inizia la sequenza di identificazione
				unsigned int blink_elapsed = elapsed - 2000;
				int target_blinks = (matched_channel >= 12 && matched_channel <= 15) ? (matched_channel - 10) : 1;
				
				unsigned int current_toggle = blink_elapsed / 150; 
				if (current_toggle < (unsigned int)(target_blinks * 2)) {
					// Inizia con lo zero (spento) per far notare lo stacco dal fisso
					CHANNEL_Set(10, (current_toggle % 2 == 0) ? 0 : 1, 4);
				} else {
					// Finita l'identificazione, torna fisso per il tempo rimanente
					CHANNEL_Set(10, 1, 4); 
				}
			}
		}
		return false; 
	}

	// =========================================================================
	// MACCHINA A STATI: GESTIONE COOLDOWN (ATTESA DI SICUREZZA)
	// =========================================================================
	if (is_cooldown) {
		if ((g_timeMs - cooldown_start_time) >= cooldown_duration_ms) {
			is_cooldown = 0; // Cooldown terminato, torna operativo
		} else {
			// LED: Lampeggio molto lento (1 sec acceso, 1 sec spento)
			CHANNEL_Set(10, ((g_timeMs / 1000) % 2 == 0) ? 1 : 0, 4);
			return false; 
		}
	}

	// =========================================================================
	// MACCHINA A STATI: SCANSIONE IN CORSO (PRONTO)
	// =========================================================================
	// LED: Lampeggio veloce (250ms acceso, 250ms spento)
	CHANNEL_Set(10, ((g_timeMs / 250) % 2 == 0) ? 1 : 0, 4);

	// =========================================================================
	// LETTURA E MATCHING BLUETOOTH
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

	if (found == 0 && data != NULL && data_len >= 16) {
		for (i = 0; i <= data_len - 16; i++) {
			if (memcmp(&data[i], my_uuid, 16) == 0) {
				found = 1;
				matched_channel = 0;
				break;
			}
		}
	}

	if (found == 1 && rssi > soglia_rssi) {
		is_pulsing = 1;
		pulse_start_time = g_timeMs;

		CHANNEL_Set(0, 1, 0);  
		CHANNEL_Set(10, 1, 4); // Forza subito il LED acceso al comando
		
		if (matched_channel > 0) {
			ADDLOG_INFO(LOG_FEATURE_MAIN, "BLE MATCH SUCCESS! Canale %d attivato (RSSI: %d dBm)", matched_channel, rssi);
		} else {
			ADDLOG_INFO(LOG_FEATURE_MAIN, "BLE MATCH SUCCESS! UUID Advertiser attivato (RSSI: %d dBm)", rssi);
		}
	}

	return false;
}

#endif
