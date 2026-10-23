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
	
	// Macchine a stati per la temporizzazione reale
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

	// Guardia Wi-Fi iniziale: ignora tutto per i primi 15 secondi di boot
	if (g_timeMs < 15000) {
		return false;
	}

	// Acquisizione impostazioni dalla Web UI
	int val_ch11 = CHANNEL_Get(11);
	if (val_ch11 == 0) return false;
	int soglia_rssi = (val_ch11 > 0) ? -val_ch11 : val_ch11;

	// I Canali 17 e 18 ora sono i SECONDI ESATTI di durata
	int val_ch17 = CHANNEL_Get(17);
	unsigned int pulse_duration_ms = (val_ch17 > 0) ? (val_ch17 * 1000) : 10000; // Default 10 sec

	int val_ch18 = CHANNEL_Get(18);
	unsigned int cooldown_duration_ms = (val_ch18 > 0) ? (val_ch18 * 1000) : 60000; // Default 60 sec

	// =========================================================================
	// MACCHINA A STATI: GESTIONE IMPULSO IN CORSO (RELE' ATTIVO)
	// =========================================================================
	if (is_pulsing) {
		// Controlla se il tempo di impulso è terminato
		if ((g_timeMs - pulse_start_time) >= pulse_duration_ms) {
			is_pulsing = 0;
			is_cooldown = 1; // Avvia il cooldown
			cooldown_start_time = g_timeMs;
			matched_channel = 0;
			
			CHANNEL_Set(0, 0, 0);  // Spegne il relè (Fine apertura)
			CHANNEL_Set(10, 0, 4); // Spegne il LED
		} 
		else {
			// Gestione temporizzata dei micro-lampeggi del LED in base al MAC/UUID riconosciuto
			unsigned int elapsed = g_timeMs - pulse_start_time;
			int target_blinks = (matched_channel >= 12 && matched_channel <= 15) ? (matched_channel - 10) : 1;
			
			// 150 millisecondi per ogni stato (acceso/spento)
			unsigned int current_toggle = elapsed / 150; 
			if (current_toggle < (unsigned int)(target_blinks * 2)) {
				CHANNEL_Set(10, (current_toggle % 2 == 0) ? 1 : 0, 4);
			} else {
				CHANNEL_Set(10, 1, 4); // Lascia il LED acceso fisso per il resto del tempo
			}
		}
		return false; // Blocca la lettura di nuovi MAC mentre il cancello è in manovra
	}

	// =========================================================================
	// MACCHINA A STATI: GESTIONE COOLDOWN (ATTESA DI SICUREZZA)
	// =========================================================================
	if (is_cooldown) {
		if ((g_timeMs - cooldown_start_time) >= cooldown_duration_ms) {
			is_cooldown = 0; // Cooldown terminato, sistema di nuovo in ascolto
		} else {
			return false; // Sistema in attesa, ignora letture Bluetooth
		}
	}

	// =========================================================================
	// LETTURA E MATCHING BLUETOOTH
	// =========================================================================
	
	// 1. Verifica riscontro MAC Address
	if (mac != NULL) {
		// Hack per salvare mezzo MAC in un canale OpenBeken
		mac_dritto = (mac[3] << 16) | (mac[4] << 8) | mac[5];
		mac_invertito = (mac[2] << 16) | (mac[1] << 8) | mac[0];

		// Logga massimo una volta al secondo per non sovraccaricare il chip
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

	// 2. Verifica riscontro Advertiser UUID (iBeacon)
	if (found == 0 && data != NULL && data_len >= 16) {
		for (i = 0; i <= data_len - 16; i++) {
			if (memcmp(&data[i], my_uuid, 16) == 0) {
				found = 1;
				matched_channel = 0;
				break;
			}
		}
	}

	// 3. Esecuzione Apertura
	if (found == 1 && rssi > soglia_rssi) {
		is_pulsing = 1;
		pulse_start_time = g_timeMs;

		CHANNEL_Set(0, 1, 0);  // Relè ON (Inizia impulso)
		CHANNEL_Set(10, 1, 4); // LED ON
		
		if (matched_channel > 0) {
			ADDLOG_INFO(LOG_FEATURE_MAIN, "BLE MATCH SUCCESS! Canale %d attivato (RSSI: %d dBm)", matched_channel, rssi);
		} else {
			ADDLOG_INFO(LOG_FEATURE_MAIN, "BLE MATCH SUCCESS! UUID Advertiser attivato (RSSI: %d dBm)", rssi);
		}
	}

	return false;
}

#endif
