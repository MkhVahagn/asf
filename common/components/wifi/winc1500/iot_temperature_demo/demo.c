/**
 *
 * \file
 *
 * \brief IoT Temperature Sensor Demo.
 *
 * Copyright (c) 2015 Atmel Corporation. All rights reserved.
 *
 * \asf_license_start
 *
 * \page License
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. The name of Atmel may not be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * 4. This software may only be redistributed and used in connection with an
 *    Atmel microcontroller product.
 *
 * THIS SOFTWARE IS PROVIDED BY ATMEL "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT ARE
 * EXPRESSLY AND SPECIFICALLY DISCLAIMED. IN NO EVENT SHALL ATMEL BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * \asf_license_stop
 *
 */

#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include "asf.h"
#include "demo.h"
#include "driver/include/m2m_wifi.h"
#include "socket/include/socket.h"
#include "conf_winc.h"

/** Configure and enable access point mode with provisioning page. */
tstrM2MAPConfig ap_config = {
	DEMO_WLAN_AP_NAME,			// Access Point Name.
	DEMO_WLAN_AP_CHANNEL,		// Channel to use.
	DEMO_WLAN_AP_WEP_INDEX,		// Wep key index.
	DEMO_WLAN_AP_WEP_SIZE,		// Wep key size.
	DEMO_WLAN_AP_WEP_KEY,		// Wep key.
	DEMO_WLAN_AP_SECURITY,		// Security mode.
	DEMO_WLAN_AP_MODE,			// SSID visible.
	DEMO_WLAN_AP_IP_ADDRESS
};

uint8 gau8HttpProvServerIP[] = DEMO_WLAN_AP_IP_ADDRESS;
char gacHttpProvDomainName[] = DEMO_WLAN_AP_DOMAIN_NAME;
char user_credentials[108];
char provision_ssid[70];
char provision_pwd[30];

/** Message format definitions. */
typedef struct s_msg_user_input {
    uint8_t channel;
	uint8_t security;
	char* password;
	char* SSID;
} t_msg_user_input;

typedef struct s_msg_temp_keepalive {
	uint8_t id0;
	uint8_t id1;
	uint8_t name[9];
	uint8_t type;
} t_msg_temp_keepalive;

typedef struct s_msg_temp_report {
	uint8_t id0;
	uint8_t id1;
	uint8_t name[9];
	uint8_t led;
	uint32_t temp;
} t_msg_temp_report;

static t_msg_user_input user_data;

/** Message format declarations. */
static t_msg_temp_keepalive msg_temp_keepalive =
{
	.id0 = 0,
	.id1 = 1,
	.name = DEMO_PRODUCT_NAME,
	.type = 2,
};

static t_msg_temp_report msg_temp_report =
{
	.id0 = 0,
	.id1 = 2,
	.name = DEMO_PRODUCT_NAME,
	.led = 0,
	.temp = 0,
};

/** Receive buffer definition. */
#define TEST_BUFFER_SIZE 1460
static uint8 gau8SocketTestBuffer[TEST_BUFFER_SIZE];

/** RX and TX socket descriptors. */
static SOCKET rx_socket = -1;
static SOCKET tx_socket = -1;

/** WiFi status variable. */
static volatile uint8 wifi_connected = 0;
static uint8 wifi_provisioned = 1; //Assuming we are already provisioned

/** Global counter delay for timer. */
static uint32_t delay = 0;

/** Global counters for LED toggling. */
static uint32_t toggle_led_ms = 0;

/** SysTick counter for non busy wait delay. */
extern uint32_t ms_ticks;

/**
 * \brief Parse user input data.
 */
static void parse_user_input(char *user_input)
{
	char *uinput;
	
	uinput = strtok (user_input,":");
	uinput != NULL ? (user_data.SSID = uinput) : (user_data.SSID = (char *)DEFAULT_SSID);
	uinput = strtok (NULL, ":");
	uinput != NULL ? (user_data.password = uinput) : (user_data.password = (char *)DEFAULT_PWD);
	uinput = strtok (NULL, ":");
	uinput != NULL ? (user_data.security = atoi(uinput)) : (user_data.security = DEFAULT_AUTH);
	uinput = strtok (NULL, ":");
	uinput != NULL ? (user_data.channel = atoi(uinput)-1) : (user_data.channel = DEFAULT_CHANNEL);
}

/**
 * \brief Print provisioning information.
 */
static void print_provisioning_details(void)
{
	printf("\r\nStarted as Access point %s\r\n",DEMO_WLAN_AP_NAME);
	puts("Enter \"atmelconfig.com\" in your browser and enter credentials in the served webpage\r\n");
	puts("Please press SW0 on D21 and enter credentials if the webpage is not served through above method\r\n");
}

/**
 * \brief Get duration of the button pressed.
 *
 * \return -1 if the button has not been released.
 */
static int32_t button_press_duration(bool current_button_state)
{
	static bool previous_button_state = false;
	static int32_t button_press_start_ms = 0;
	int32_t button_press_duration_ms = -1;

	/* Button has been pressed. */
	if (current_button_state == true && previous_button_state == false) {
		button_press_start_ms = ms_ticks;
	}
	/* Button has been released. */
	if (current_button_state == false && previous_button_state == true) {
		button_press_duration_ms = ms_ticks - button_press_start_ms;
	}
	previous_button_state = current_button_state;
	return button_press_duration_ms;
}

/**
 * \brief Callback to get the Data from socket.
 *
 * \param[in] sock socket handler.
 * \param[in] u8Msg socket event type. Possible values are:
 *  - SOCKET_MSG_BIND
 *  - SOCKET_MSG_LISTEN
 *  - SOCKET_MSG_ACCEPT
 *  - SOCKET_MSG_CONNECT
 *  - SOCKET_MSG_RECV
 *  - SOCKET_MSG_SEND
 *  - SOCKET_MSG_SENDTO
 *  - SOCKET_MSG_RECVFROM
 * \param[in] pvMsg is a pointer to message structure. Existing types are:
 *  - tstrSocketBindMsg
 *  - tstrSocketListenMsg
 *  - tstrSocketAcceptMsg
 *  - tstrSocketConnectMsg
 *  - tstrSocketRecvMsg
 */
static void demo_wifi_socket_handler(SOCKET sock, uint8 u8Msg, void *pvMsg)
{
	/* Check for socket event on RX socket. */
	if (sock == rx_socket) {
		if (u8Msg == SOCKET_MSG_BIND) {
			tstrSocketBindMsg *pstrBind = (tstrSocketBindMsg *)pvMsg;
			if (pstrBind && pstrBind->status == 0) {
				/* Prepare next buffer reception. */
				recvfrom(sock, gau8SocketTestBuffer, TEST_BUFFER_SIZE, 0);
			}
			else {
				puts("m2m_wifi_socket_handler: bind error!");
			}
		}
		else if (u8Msg == SOCKET_MSG_RECVFROM) {
			tstrSocketRecvMsg *pstrRx = (tstrSocketRecvMsg *)pvMsg;
			if (pstrRx->pu8Buffer && pstrRx->s16BufferSize) {

				/* Check for server report and update led status if necessary. */
				t_msg_temp_report report;
				memcpy(&report, pstrRx->pu8Buffer, sizeof(t_msg_temp_report));
				if (report.id0 == 0 && report.id1 == 2 && (strcmp((char *)report.name, DEMO_PRODUCT_NAME) == 0)) {
					puts("wifi_nc_data_callback: received app message");
					port_pin_set_output_level(LED_0_PIN, report.led ? true : false);
					delay = 0;
				}

				/* Prepare next buffer reception. */
				recvfrom(sock, gau8SocketTestBuffer, TEST_BUFFER_SIZE, 0);
			}
			else {
				if (pstrRx->s16BufferSize == SOCK_ERR_TIMEOUT) {
					/* Prepare next buffer reception. */
					recvfrom(sock, gau8SocketTestBuffer, TEST_BUFFER_SIZE, 0);
				}
			}
		}
	}
}

/**
 * \brief Callback to get the WiFi status update.
 *
 * \param[in] u8MsgType type of WiFi notification. Possible types are:
 *  - [M2M_WIFI_RESP_CURRENT_RSSI](@ref M2M_WIFI_RESP_CURRENT_RSSI)
 *  - [M2M_WIFI_RESP_CON_STATE_CHANGED](@ref M2M_WIFI_RESP_CON_STATE_CHANGED)
 *  - [M2M_WIFI_RESP_CONNTION_STATE](@ref M2M_WIFI_RESP_CONNTION_STATE)
 *  - [M2M_WIFI_RESP_SCAN_DONE](@ref M2M_WIFI_RESP_SCAN_DONE)
 *  - [M2M_WIFI_RESP_SCAN_RESULT](@ref M2M_WIFI_RESP_SCAN_RESULT)
 *  - [M2M_WIFI_REQ_WPS](@ref M2M_WIFI_REQ_WPS)
 *  - [M2M_WIFI_RESP_IP_CONFIGURED](@ref M2M_WIFI_RESP_IP_CONFIGURED)
 *  - [M2M_WIFI_RESP_IP_CONFLICT](@ref M2M_WIFI_RESP_IP_CONFLICT)
 *  - [M2M_WIFI_RESP_P2P](@ref M2M_WIFI_RESP_P2P)
 *  - [M2M_WIFI_RESP_AP](@ref M2M_WIFI_RESP_AP)
 *  - [M2M_WIFI_RESP_CLIENT_INFO](@ref M2M_WIFI_RESP_CLIENT_INFO)
 * \param[in] pvMsg A pointer to a buffer containing the notification parameters
 * (if any). It should be casted to the correct data type corresponding to the
 * notification type. Existing types are:
 *  - tstrM2mWifiStateChanged
 *  - tstrM2MWPSInfo
 *  - tstrM2MP2pResp
 *  - tstrM2MAPResp
 *  - tstrM2mScanDone
 *  - tstrM2mWifiscanResult
 */
static void demo_wifi_state(uint8 u8MsgType, void *pvMsg)
{
	switch (u8MsgType) {
		case M2M_WIFI_RESP_CON_STATE_CHANGED: {
			tstrM2mWifiStateChanged *pstrWifiState = (tstrM2mWifiStateChanged*) pvMsg;
			if (pstrWifiState->u8CurrState == M2M_WIFI_CONNECTED) {
				puts("m2m_wifi_state: M2M_WIFI_RESP_CON_STATE_CHANGED: CONNECTED");
				m2m_wifi_get_connection_info();
				m2m_wifi_request_dhcp_client();
			}
			else if(pstrWifiState->u8CurrState == M2M_WIFI_DISCONNECTED) {
				puts("m2m_wifi_state: M2M_WIFI_RESP_CON_STATE_CHANGED: DISCONNECTED");
				m2m_wifi_connect(user_data.SSID, strlen(user_data.SSID),
				user_data.security, user_data.password,user_data.channel);
				wifi_connected = 0;
			}
			break;
		}

		case M2M_WIFI_RESP_CONN_INFO: {
			tstrM2MConnInfo *pstrConnInfo = (tstrM2MConnInfo*) pvMsg;
			printf("Connected to %s\r\n",pstrConnInfo->acSSID);
			break;
		}

		case M2M_WIFI_RESP_DEFAULT_CONNECT: {
			tstrM2MDefaultConnResp *pstrResp = (tstrM2MDefaultConnResp*) pvMsg;
			if(pstrResp->s8ErrorCode == M2M_DEFAULT_CONN_SCAN_MISMATCH || 
			pstrResp->s8ErrorCode == M2M_DEFAULT_CONN_EMPTY_LIST)
			{
				/* If we didn't find previously provisioned Access point
				or we don't find any Access point around, we enter provisioning mode. */
				wifi_provisioned = 0; /* Also set provisioning to zero, as our assumption was incorrect. */
				m2m_wifi_start_provision_mode(&ap_config,gacHttpProvDomainName,1);
				print_provisioning_details();
			}
			break;
		}

		case M2M_WIFI_REQ_DHCP_CONF: {
			uint8 *pu8IPAddress = (uint8*) pvMsg;
			printf("m2m_wifi_state: M2M_WIFI_REQ_DHCP_CONF: IP is %u.%u.%u.%u\n",
					pu8IPAddress[0], pu8IPAddress[1], pu8IPAddress[2], pu8IPAddress[3]);
			if(wifi_provisioned == 1){
				/* Switch ON the LED when connected. */
				port_pin_set_output_level(LED_0_PIN, false); 
				wifi_connected = 1;
			}
			break;
		}

		case M2M_WIFI_RESP_PROVISION_INFO : {
			tstrM2MProvisionInfo *pstrProvInfo = (tstrM2MProvisionInfo*)pvMsg;

			if (pstrProvInfo->u8Status == M2M_SUCCESS) {
				wifi_provisioned = 1;
				m2m_wifi_connect((char*)pstrProvInfo->au8SSID,strlen((char*)pstrProvInfo->au8SSID),
				pstrProvInfo->u8SecType, pstrProvInfo->au8Password, M2M_WIFI_CH_ALL);

				/* Copy the provisioned info into user_data, so we can connect back when disconnected. */
				strncpy(provision_ssid,(char*)pstrProvInfo->au8SSID,strlen((char*)pstrProvInfo->au8SSID));
				strncpy(provision_pwd,(char*)pstrProvInfo->au8Password,strlen((char*)pstrProvInfo->au8Password));
				user_data.SSID = provision_ssid;
				user_data.password = provision_pwd;
				user_data.security = pstrProvInfo->u8SecType;
				user_data.channel = M2M_WIFI_CH_ALL;
				
				printf("m2m_wifi_state: M2M_WIFI_RESP_PROVISION_INFO using SSID %s\n", pstrProvInfo->au8SSID);
				if (pstrProvInfo->u8SecType != M2M_WIFI_SEC_OPEN) {
					printf("m2m_wifi_state: M2M_WIFI_RESP_PROVISION_INFO using PSK %s\n", pstrProvInfo->au8Password);
				}
			}
			break;
		}

	default: {
			break;
		}
	}
}

/**
 * \brief Demo main function.
 */
void demo_start(void)
{
	tstrWifiInitParam winc_init_config;
	struct sockaddr_in addr;
	sint8 ret;

	/* Initialize socket address structure. */
	addr.sin_family	= AF_INET;
	addr.sin_port = _htons(DEMO_SERVER_PORT);
	addr.sin_addr.s_addr = 0xFFFFFFFF;

	/* Initialize temperature sensor. */
	at30tse_init();
	
	/* Initialize WINC1500 hardware. */
	nm_bsp_init();
	
	/* Initialize WINC1500 driver with WiFi status callback. */
	winc_init_config.pfAppWifiCb = demo_wifi_state;
	ret = m2m_wifi_init(&winc_init_config);
	if (M2M_SUCCESS != ret) {
		puts("demo_start: nm_drv_init call error!");
		while (1)
			;
	}
	
	/* Initialize Socket driver with socket status callback. */
	socketInit();
	registerSocketCallback(demo_wifi_socket_handler, NULL);
	//Start the WINC to connect with previously provisioned AP
	m2m_wifi_default_connect();
	while (1) {
		/* Handle pending events from WINC1500. */
		m2m_wifi_handle_events(NULL);
		
		/* Calculate duration of time the button was pressed.
		If button was pressed and held for more than 10s we would enter provisioning mode
		short press would allow us to enter credentials from serial console. */
		int32_t button_pressed_duration_ms = button_press_duration(!port_pin_get_input_level(CREDENTIAL_ENTRY_BUTTON));
		if ((button_pressed_duration_ms != -1 && button_pressed_duration_ms < 2000)
		 && !wifi_provisioned) {
			printf("Enter Credentials and press ENTER\r\n");
			printf("eg: <SSID>:<Password>:<SecurityType>:<WifiChannel>\r\n");
			printf("For Security\r\n");
			printf("Enter 1 for OPEN\r\n2 for WPA PSK\r\n");
			printf("3 for WEP\r\n4 for WPA/WPA2 Ent.\r\n"); 
            scanf("%[^\r]s",user_credentials);
			getc(stdin); //work around to clear out previous character
			parse_user_input(user_credentials);
			printf("\r\n");
			//Disable the AP mode
			m2m_wifi_disable_ap();
			delay_ms(250); //delay after disabling AP mode
			//Connect to the router using user supplied credentials
			m2m_wifi_connect(user_data.SSID, strlen(user_data.SSID),
			user_data.security, user_data.password,user_data.channel);
			wifi_provisioned = 1;
		}
		
		/* Force provisioning mode if the user presses SW0 for more than 10s. */
		if (button_pressed_duration_ms != -1 && (button_pressed_duration_ms > 10000)) {
			wifi_provisioned = 0;
			wifi_connected = 0;
			
			m2m_wifi_start_provision_mode(&ap_config, gacHttpProvDomainName, 1);
			print_provisioning_details();
		}
		
		if (!wifi_provisioned) {
			if(ms_ticks - toggle_led_ms >= 500) {
				toggle_led_ms = ms_ticks;
				port_pin_toggle_output_level(LED_0_PIN);	
			}
		}
		
		if ((wifi_connected == 1) && (ms_ticks - delay > DEMO_REPORT_INTERVAL)) {
			delay = ms_ticks;

			/* Open server socket. */
			if (rx_socket < 0) {
				if ((rx_socket = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
					puts("demo_start: failed to create RX UDP client socket error!");
					continue;
				}
				bind(rx_socket, (struct sockaddr *)&addr, sizeof(struct sockaddr_in));
			}
			
			/* Open client socket. */
			if (tx_socket < 0) {
				if ((tx_socket = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
					puts("demo_start: failed to create TX UDP client socket error!");
					continue;
				}
			}
			
			/* Send client discovery frame. */
			sendto(tx_socket, &msg_temp_keepalive, sizeof(t_msg_temp_keepalive), 0,
					(struct sockaddr *)&addr, sizeof(addr));
			
			/* Send client report. */
			msg_temp_report.temp = (uint32_t)(at30tse_read_temperature() * 100);
			msg_temp_report.led = !port_pin_get_output_level(LED_0_PIN);
			ret = sendto(tx_socket, &msg_temp_report, sizeof(t_msg_temp_report), 0,
					(struct sockaddr *)&addr, sizeof(addr));

			if (ret == M2M_SUCCESS) {
				puts("demo_start: sensor report sent");
			} else {
				puts("demo_start: failed to send status report error!");
			}
		}
	}
}
