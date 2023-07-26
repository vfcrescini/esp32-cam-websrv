// 2023-07-04 ping.c
// Copyright (C) 2023 Vino Fernando Crescini  <vfcrescini@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "config.h"
#include "ping.h"

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>

#include <lwip/inet.h>
#include <lwip/inet_chksum.h>
#include <lwip/ip4.h>
#include <lwip/icmp.h>
#include <lwip/netdb.h>
#include <lwip/sockets.h>

#include <esp_log.h>
#include <esp_timer.h>

// maximum possbile IPv4 header + minimum ICMP header

#define _CAMWEBSRV_PING_PACKET_LEN 68
#define _CAMWEBSRV_PING_TIMEOUT_MAX 3
#define _CAMWEBSRV_PING_TIMEOUT_BLCK 5000
#define _CAMWEBSRV_PING_TIMEOUT_SENT 5000
#define _CAMWEBSRV_PING_WAIT_INTERVAL 1000
#define _CAMWEBSRV_PING_CYCLE_INTERVAL 15000

typedef enum
{
  _CAMWEBSRV_PING_STATE_INIT,
  _CAMWEBSRV_PING_STATE_BLCK,
  _CAMWEBSRV_PING_STATE_SENT,
  _CAMWEBSRV_PING_STATE_WAIT
} _camwebsrv_ping_state_t;

typedef struct
{
  int sock;
  struct sockaddr_in addr;
  _camwebsrv_ping_state_t state;
  struct icmp_echo_hdr header;
  int64_t teventlast;
  int64_t teventnext;
  uint8_t timeouts;
} _camwebsrv_ping_t;

static esp_err_t _camwebsrv_ping_get_ip(const char *host, uint32_t *ip);
static esp_err_t _camwebsrv_ping_send(_camwebsrv_ping_t *pping);
static esp_err_t _camwebsrv_ping_recv(_camwebsrv_ping_t *pping);

esp_err_t camwebsrv_ping_init(camwebsrv_ping_t *ping, camwebsrv_cfgman_t cfgman)
{
  esp_err_t rv;
  const char *host = NULL;
  uint32_t ip = 0;
  int sock = 0;
  _camwebsrv_ping_t *pping;

  if (ping == NULL || cfgman == NULL)
  {
    return ESP_ERR_INVALID_ARG;
  }

  // get target

  rv = camwebsrv_cfgman_get(cfgman, CAMWEBSRV_CFGMAN_KEY_PING_HOST, &host);

  if (rv != ESP_OK)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "PING camwebsrv_ping_init(): camwebsrv_cfgman_get(%s) failed: [%d]: %s", CAMWEBSRV_CFGMAN_KEY_PING_HOST, rv, esp_err_to_name(rv));
    return rv;
  }

  // get ip

  rv = _camwebsrv_ping_get_ip(host, &ip);

  if (rv != ESP_OK)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "PING camwebsrv_ping_init(): _camwebsrv_ping_get_ip(%s) failed: [%d]: %s", host, rv, esp_err_to_name(rv));
    return rv;
  }

  // initialise socket

  sock = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);

  if (sock < 0)
  {
    int e = errno;
    ESP_LOGE(CAMWEBSRV_TAG, "PING camwebsrv_ping_init(): socket() failed: [%d]: %s", e, strerror(e));
    return ESP_FAIL;
  }

  // set non-blocking

  rv = fcntl(sock, F_SETFL, O_NONBLOCK);

  if (rv == -1)
  {
    int e = errno;
    ESP_LOGE(CAMWEBSRV_TAG, "PING camwebsrv_ping_init(): fcntl() failed: [%d]: %s", e, strerror(e));
    close(sock);
    return ESP_FAIL;
  }

  // allocate space for new structure

  pping = (_camwebsrv_ping_t *) malloc(sizeof(_camwebsrv_ping_t));

  if (pping == NULL)
  {
    int e = errno;
    ESP_LOGE(CAMWEBSRV_TAG, "PING camwebsrv_ping_init(): malloc() failed: [%d]: %s", e, strerror(e));
    close(sock);
    return ESP_ERR_NO_MEM;
  }

  // initialise structure

  memset(pping, 0x00, sizeof(_camwebsrv_ping_t));

  pping->sock = sock;
  pping->addr.sin_family = AF_INET;
  pping->addr.sin_addr.s_addr = ip;
  pping->state = _CAMWEBSRV_PING_STATE_INIT;
  pping->header.type = ICMP_ECHO;

  *ping = (camwebsrv_ping_t) pping;

  return ESP_OK;
}

esp_err_t camwebsrv_ping_destroy(camwebsrv_ping_t *ping)
{
  _camwebsrv_ping_t *pping;

  if (ping == NULL)
  {
    return ESP_ERR_INVALID_ARG;
  }

  pping = (_camwebsrv_ping_t *) *ping;

  close(pping->sock);

  free(pping);

  *ping = NULL;

  return ESP_OK;
}

esp_err_t camwebsrv_ping_process(camwebsrv_ping_t ping, uint16_t *nextevent)
{
  esp_err_t rv;
  _camwebsrv_ping_t *pping;
  int64_t tnow;

  if (ping == NULL)
  {
    return ESP_ERR_INVALID_ARG;
  }

  pping = (_camwebsrv_ping_t *) ping;

  // are any events due?

  tnow = esp_timer_get_time() / 1000;

  if (pping->teventnext > 0 && tnow < pping->teventnext)
  {
    if (nextevent != NULL)
    {
      *nextevent = pping->teventnext - tnow;
    }

    return ESP_OK;
  }

  while(1)
  {
    switch(pping->state)
    {
      case _CAMWEBSRV_PING_STATE_INIT:

        // Initial state, so send echo request immediately

        rv = _camwebsrv_ping_send(pping);

        // send successful so progress to SENT state
  
        if (rv == ESP_OK)
        {
          pping->state = _CAMWEBSRV_PING_STATE_SENT;
          pping->teventlast = tnow;
          pping->teventnext = tnow;

          ESP_LOGI(CAMWEBSRV_TAG, "PING camwebsrv_ping_process(): PING request sent");
          ESP_LOGD(CAMWEBSRV_TAG, "PING camwebsrv_ping_process(): state transition from INIT to SENT");

          continue;
        }

        // if we can't send, progress to BLCK state

        if (rv == ESP_ERR_NOT_FINISHED)
        {
          pping->state = _CAMWEBSRV_PING_STATE_BLCK;
          pping->teventlast = tnow;
          pping->teventnext = tnow + _CAMWEBSRV_PING_WAIT_INTERVAL;
          
          ESP_LOGD(CAMWEBSRV_TAG, "PING camwebsrv_ping_process(): state transition from INIT to BLCK");

          if (nextevent != NULL && *nextevent > (pping->teventnext - tnow))
          {
            *nextevent = pping->teventnext - tnow;
          }

          return ESP_OK;
        }

        // failed

        ESP_LOGE(CAMWEBSRV_TAG, "PING camwebsrv_ping_process(): _camwebsrv_ping_send() failed");

        return ESP_FAIL;

      case _CAMWEBSRV_PING_STATE_BLCK:

        // attempted a send

        // have we exceeded the time limit?

        if (tnow >= (pping->teventlast + _CAMWEBSRV_PING_TIMEOUT_BLCK))
        {
          ESP_LOGE(CAMWEBSRV_TAG, "PING camwebsrv_ping_process(): timeout on state BLCK");

          return ESP_FAIL;
        }

        // try to send again

        rv = _camwebsrv_ping_send(pping);

        // send successful so progress to SENT state
  
        if (rv == ESP_OK)
        {
          pping->state = _CAMWEBSRV_PING_STATE_SENT;
          pping->teventlast = tnow;
          pping->teventnext = tnow;

          ESP_LOGI(CAMWEBSRV_TAG, "PING camwebsrv_ping_process(): PING request sent");
          ESP_LOGD(CAMWEBSRV_TAG, "PING camwebsrv_ping_process(): state transition from BLCK to SENT");

          continue;
        }

        // if we can't send, stay in BLCK state

        if (rv == ESP_ERR_NOT_FINISHED)
        {
          pping->teventnext = tnow + _CAMWEBSRV_PING_WAIT_INTERVAL;

          if (nextevent != NULL && *nextevent > (pping->teventnext - tnow))
          {
            *nextevent = pping->teventnext - tnow;
          }

          return ESP_OK;
        }

        // failed

        ESP_LOGE(CAMWEBSRV_TAG, "PING camwebsrv_ping_process(): _camwebsrv_ping_send() failed");

        return ESP_FAIL;

      case _CAMWEBSRV_PING_STATE_SENT:

        // waiting for echo response
        // have we exceeded the time limit?

        if (tnow >= (pping->teventlast + _CAMWEBSRV_PING_TIMEOUT_SENT))
        {
          ESP_LOGW(CAMWEBSRV_TAG, "PING camwebsrv_ping_process(): timeout %d on state SENT", pping->timeouts);

          // have we exceeded the maximum allowed timed-out responses?
          // if so, return special timeout status code and reset, in case caller
          // wants to ignore

          if (pping->timeouts > _CAMWEBSRV_PING_TIMEOUT_MAX)
          {
            ESP_LOGE(CAMWEBSRV_TAG, "PING camwebsrv_ping_process(): exceeded maximum allowable missed responses");

            pping->state = _CAMWEBSRV_PING_STATE_INIT;
            pping->timeouts = 0;
            pping->teventlast = 0;
            pping->teventnext = 0;

            return ESP_ERR_TIMEOUT;
          }

          // okay, we've timed out while waiting for a response, but we haven't
          // exceeded the limit, so send another request immediately

          pping->timeouts = pping->timeouts + 1;
          pping->state = _CAMWEBSRV_PING_STATE_INIT;
          pping->teventlast = tnow;
          pping->teventnext = tnow;

          continue;
        }

        // try to recv

        rv = _camwebsrv_ping_recv(pping);

        // recv success so progress to WAIT state

        if (rv == ESP_OK)
        {
          pping->state = _CAMWEBSRV_PING_STATE_WAIT;
          pping->timeouts = 0;
          pping->teventlast = tnow;
          pping->teventnext = tnow + _CAMWEBSRV_PING_CYCLE_INTERVAL;

          ESP_LOGI(CAMWEBSRV_TAG, "PING camwebsrv_ping_process(): PING response received");
          ESP_LOGD(CAMWEBSRV_TAG, "PING camwebsrv_ping_process(): state transition from SENT to WAIT");

          continue;
        }

        // if we can't recv, stay in the SENT state

        if (rv == ESP_ERR_NOT_FINISHED)
        {
          pping->teventnext = tnow + _CAMWEBSRV_PING_WAIT_INTERVAL;

          if (nextevent != NULL && *nextevent > (pping->teventnext - tnow))
          {
            *nextevent = pping->teventnext - tnow;
          }

          return ESP_OK;
        }

        // failed

        ESP_LOGE(CAMWEBSRV_TAG, "PING camwebsrv_ping_process(): _camwebsrv_ping_recv() failed");

        return ESP_FAIL;

      case _CAMWEBSRV_PING_STATE_WAIT:

        // is it time for the next cycle yet?

        if (tnow < pping->teventnext)
        {
          if (nextevent != NULL && *nextevent > (pping->teventnext - tnow))
          {
            *nextevent = pping->teventnext - tnow;
          }

          return ESP_OK;
        }

        // time for another cycle

        pping->state = _CAMWEBSRV_PING_STATE_INIT;
        pping->teventlast = tnow;
        pping->teventnext = tnow;

        ESP_LOGD(CAMWEBSRV_TAG, "PING camwebsrv_ping_process(): state transition from WAIT to INIT");

        continue;

      default:

        ESP_LOGE(CAMWEBSRV_TAG, "PING camwebsrv_ping_process(): invalid state");
        return ESP_ERR_INVALID_STATE;
    }
  }

  return ESP_OK;
}

static esp_err_t _camwebsrv_ping_get_ip(const char *host, uint32_t *ip)
{
  int rv;
  struct in_addr addr;
  struct addrinfo hints;
  struct addrinfo *res;

  memset(&addr, 0x00, sizeof(struct in_addr));
  
  // is it an IP address string?
  
  if (inet_aton(host, &addr) != 0)
  { 
    *ip = addr.s_addr;
    return ESP_OK;
  }
  
  // nope. need to do a lookup
  
  memset(&hints, 0x00, sizeof(struct addrinfo));
    
  hints.ai_family = AF_INET;
  
  rv = getaddrinfo(host, NULL, &hints, &res);
  
  if (rv != 0)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "PING _camwebsrv_ping_get_ip() failed: etaddrinfo(%s) failed: [%d]", host, rv);
    return ESP_FAIL;
  }

  // at least one was returned; pick the first one

  *ip = ((struct sockaddr_in *) res->ai_addr)->sin_addr.s_addr;

  // cleanup

  freeaddrinfo(res);

  return ESP_OK;  
}

static esp_err_t _camwebsrv_ping_send(_camwebsrv_ping_t *pping)
{
  ssize_t rv;

  pping->header.chksum = 0;
  pping->header.chksum = inet_chksum(&(pping->header), sizeof(struct icmp_echo_hdr));

  rv = sendto(pping->sock, &(pping->header), sizeof(struct icmp_echo_hdr), 0x00, (const struct sockaddr *) &(pping->addr), sizeof(struct sockaddr));

  if (rv < 0)
  {
    int e = errno;

    if (e == EAGAIN)
    {
      return ESP_ERR_NOT_FINISHED;
    }

    ESP_LOGE(CAMWEBSRV_TAG, "PING _camwebsrv_ping_send(): sendto() failed: [%d]: %s", e, strerror(e));
    return ESP_FAIL;
  }

  if (rv != sizeof(struct icmp_echo_hdr))
  {
    ESP_LOGE(CAMWEBSRV_TAG, "PING _camwebsrv_ping_send(): sendto() sent only %d of %d bytes", rv, sizeof(struct icmp_echo_hdr));
    return ESP_FAIL;
  }

  pping->header.seqno = htons(ntohs(pping->header.seqno) + 1);

  return ESP_OK;
}

static esp_err_t _camwebsrv_ping_recv(_camwebsrv_ping_t *pping)
{
  ssize_t rv;
  uint8_t buffer[_CAMWEBSRV_PING_PACKET_LEN];
  struct sockaddr addr;
  socklen_t alen;

  while(1)
  {
    struct ip_hdr *header_ip;
    struct icmp_echo_hdr *header_icmp;

    memset(buffer, 0x00, sizeof(buffer));
    memset(&addr, 0x00, sizeof(addr));
    alen = sizeof(addr);

    rv = recvfrom(pping->sock, buffer, sizeof(buffer), 0x00, &addr, &alen);

    if (rv < 0)
    {
      int e = errno;
  
      if (e == EAGAIN)
      {
        return ESP_ERR_NOT_FINISHED;
      }

      ESP_LOGE(CAMWEBSRV_TAG, "PING _camwebsrv_ping_recv(): recvfrom() failed: [%d]: %s", e, strerror(e));
      return ESP_FAIL;
    }

    // is it IPv4?

    if (addr.sa_family != AF_INET)
    {
      ESP_LOGW(CAMWEBSRV_TAG, "PING _camwebsrv_ping_recv(): recvfrom() received non-IPv4 packet; dropping");
      continue;
    }

    // load onto IPv4 header structure

    header_ip = (struct ip_hdr *) buffer;

    // did it come from the target?

    if (header_ip->src.addr != pping->addr.sin_addr.s_addr)
    {
      ESP_LOGW(CAMWEBSRV_TAG, "PING _camwebsrv_ping_recv(): recvfrom() received packet from unexpected source; dropping");
      continue;
    }

    // is it ICMP?

    if (IPH_PROTO(header_ip) != IPPROTO_ICMP)
    {
      ESP_LOGW(CAMWEBSRV_TAG, "PING _camwebsrv_ping_recv(): recvfrom() received non-ICMP packet; dropping");
      continue;
    }

    // load onto ICMP header
    // IPv4 header is not fixed, so offset to ICMP datagram must be calculated
    // from the IPv4 header length field, whose value is the number of 32-bit
    // words used by the header

    header_icmp = (struct icmp_echo_hdr *) (buffer + (IPH_HL(header_ip) * 4));

    // is it ECHO response?

    if (ICMPH_TYPE(header_icmp) != ICMP_ER)
    {
      ESP_LOGW(CAMWEBSRV_TAG, "PING _camwebsrv_ping_recv(): recvfrom() received non-ECHOREPLY ICMP packet: 0x%02X; dropping", ICMPH_TYPE(header_icmp));

      continue;
    }

    break;
  }

  return ESP_OK;
}
