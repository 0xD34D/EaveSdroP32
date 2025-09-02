#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"

#define DNS_PORT 53
static const char *TAG = "CaptiveDNS";

void captive_dns_task(void *pvParameters) {
  struct sockaddr_in server_addr, client_addr;
  int sock = socket(AF_INET, SOCK_DGRAM, 0);
  if (sock < 0) {
    ESP_LOGE(TAG, "Failed to create socket");
    vTaskDelete(NULL);
    return;
  }

  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(DNS_PORT);
  server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

  bind(sock, (struct sockaddr *)&server_addr, sizeof(server_addr));

  uint8_t buf[512];
  socklen_t addr_len = sizeof(client_addr);

  // Get AP IP using esp_netif
  esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
  esp_netif_ip_info_t ip_info;
  esp_netif_get_ip_info(netif, &ip_info);

  while (1) {
    int len = recvfrom(sock, buf, sizeof(buf), 0,
                       (struct sockaddr *)&client_addr, &addr_len);
    if (len > 0) {
      // Minimal DNS response: copy header, set QR=1, ANCOUNT=1, and reply with
      // AP IP
      buf[2] |= 0x80;  // QR=1
      buf[7] = 1;      // ANCOUNT=1

      int resp_len = len;
      // Add answer section (A record)
      buf[resp_len++] = 0xc0;
      buf[resp_len++] = 0x0c;  // Name pointer
      buf[resp_len++] = 0x00;
      buf[resp_len++] = 0x01;  // Type A
      buf[resp_len++] = 0x00;
      buf[resp_len++] = 0x01;  // Class IN
      buf[resp_len++] = 0x00;
      buf[resp_len++] = 0x00;
      buf[resp_len++] = 0x00;
      buf[resp_len++] = 0x3c;  // TTL
      buf[resp_len++] = 0x00;
      buf[resp_len++] = 0x04;  // Data length
      buf[resp_len++] = ip_info.ip.addr & 0xFF;
      buf[resp_len++] = (ip_info.ip.addr >> 8) & 0xFF;
      buf[resp_len++] = (ip_info.ip.addr >> 16) & 0xFF;
      buf[resp_len++] = (ip_info.ip.addr >> 24) & 0xFF;

      sendto(sock, buf, resp_len, 0, (struct sockaddr *)&client_addr, addr_len);
    }
  }
  close(sock);
  vTaskDelete(NULL);
}