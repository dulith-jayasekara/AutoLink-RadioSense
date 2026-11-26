#ifndef LORA_MANAGER_H
#define LORA_MANAGER_H

//LoRa module pin configuration
#define LORA_NSS 5
#define LORA_RST 14
#define LORA_DI0 26
#define LORA_SCK 18
#define LORA_MISO 19
#define LORA_MOSI 23

//LoRa signal parameters
#define FREQ 433E6
#define SYNC_WORD 0x12
#define BANDWIDTH 125E3
#define SPREADING_FACTOR 7
#define CODING_RATE 5
#define TX_POWER 17
#define PREAMBLE_LENGTH 8

//LoRa initialization functions
void LoRa_Initialization();

#endif