// 2023-01-11 cfgman.c
// Copyright (C) 2023 Vino Fernando Crescini  <vfcrescini@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "config.h"
#include "cfgman.h"
#include "storage.h"

#include <stdlib.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>

#include <esp_log.h>

typedef struct _camwebsrv_cfgman_node_t
{
  char *kstr;
  char *vstr;
  struct _camwebsrv_cfgman_node_t *next;
} _camwebsrv_cfgman_node_t;

typedef struct
{
  _camwebsrv_cfgman_node_t *head;
  _camwebsrv_cfgman_node_t *tail;
  size_t length;
} _camwebsrv_cfgman_t;

static bool _camwebsrv_cfgman_node(_camwebsrv_cfgman_t *pcfg, const char *kstr, _camwebsrv_cfgman_node_t **pnode);
static bool _camwebsrv_cfgman_set_full(_camwebsrv_cfgman_t *pcfg, const char *kstr, const char *vstr);
static bool _camwebsrv_cfgman_set_subst(_camwebsrv_cfgman_t *pcfg, const char *kstr, off_t ks, off_t ke, const char *vstr, off_t vs, off_t ve);
static bool _camwebsrv_cfgman_load_cb(const char *buf, size_t len, void *arg);

esp_err_t camwebsrv_cfgman_init(camwebsrv_cfgman_t *cfg)
{
  _camwebsrv_cfgman_t *tcfg;

  if (cfg == NULL)
  {
    return ESP_ERR_INVALID_ARG;
  }

  tcfg = (_camwebsrv_cfgman_t *) malloc(sizeof(_camwebsrv_cfgman_t));

  if (tcfg == NULL)
  {
    int e = errno;
    ESP_LOGE(CAMWEBSRV_TAG, "HTTPD camwebsrv_cfgman_init(): malloc() failed: [%d]: %s", e, strerror(e));
    return ESP_FAIL;
  }

  tcfg->head = NULL;
  tcfg->tail = NULL;
  tcfg->length = 0;

  *cfg = (camwebsrv_cfgman_t) tcfg;

  return ESP_OK;
}

esp_err_t camwebsrv_cfgman_destroy(camwebsrv_cfgman_t *cfg)
{
  _camwebsrv_cfgman_t *tcfg;
  _camwebsrv_cfgman_node_t *curr;

  if (cfg == NULL)
  {
    return ESP_ERR_INVALID_ARG;
  }

  tcfg = (_camwebsrv_cfgman_t *) *cfg;

  if (tcfg == NULL)
  {
    return ESP_OK;
  }

  while(tcfg->head)
  {
    curr = tcfg->head;
    tcfg->head = tcfg->head->next;

    free(curr->kstr);
    free(curr->vstr);
    free(curr);
  }

  free(tcfg);

  *cfg = NULL;

  return ESP_OK;
}

esp_err_t camwebsrv_cfgman_load(camwebsrv_cfgman_t cfg, const char *filename)
{
  esp_err_t rv;
  _camwebsrv_cfgman_t *tcfg;

  if (cfg == NULL || filename == NULL)
  {
    return ESP_ERR_INVALID_ARG;
  }

  tcfg = (_camwebsrv_cfgman_t *) cfg;

  rv = camwebsrv_storage_get(filename, _camwebsrv_cfgman_load_cb, (void *) tcfg);

  if (rv != ESP_OK)
  {
    ESP_LOGE(CAMWEBSRV_TAG, "CFGMAN camwebsrv_cfgman_load(): camwebsrv_storage_get() failed: [%d]: %s", rv, esp_err_to_name(rv));
    return rv;
  }

  return ESP_OK;
}

esp_err_t camwebsrv_cfgman_get(camwebsrv_cfgman_t cfg, const char *kstr, const char **vstr)
{
  _camwebsrv_cfgman_t *tcfg;
  _camwebsrv_cfgman_node_t *node;

  if (cfg == NULL || kstr == NULL || vstr == NULL)
  {
    return ESP_ERR_INVALID_ARG;
  }

  tcfg = (_camwebsrv_cfgman_t *) cfg;

  // find a node with the given kstr

  if (!_camwebsrv_cfgman_node(tcfg, kstr, &node))
  {
    return ESP_ERR_NOT_FOUND;
  }

  *vstr = node->vstr;

  return ESP_OK;
}

static bool _camwebsrv_cfgman_node(_camwebsrv_cfgman_t *pcfg, const char *kstr, _camwebsrv_cfgman_node_t **pnode)
{
  _camwebsrv_cfgman_node_t *curr = pcfg->head;

  while(curr != NULL)
  {
    if (strcmp(kstr, curr->kstr) == 0)
    {
      *pnode = curr;
      return true;
    }
    curr = curr->next;
  }

  return false;
}

static bool _camwebsrv_cfgman_set_full(_camwebsrv_cfgman_t *pcfg, const char *kstr, const char *vstr)
{
  char *tkstr; 
  char *tvstr; 
  _camwebsrv_cfgman_node_t *node;

  // a node with the given kstr already exists

  if (_camwebsrv_cfgman_node(pcfg, kstr, &node))
  {
    tvstr = (char *) realloc(node->vstr, strlen(vstr) + 1);

    if (tvstr == NULL)
    {
      int e = errno;
      ESP_LOGE(CAMWEBSRV_TAG, "CFGMAN _camwebsrv_cfgman_set_full(): realloc() failed: [%d]: %s", e, strerror(e));
      return false;
    }

    node->vstr = tvstr;
    strcpy(node->vstr, vstr);

    return true;
  }

  // copy key

  tkstr = (char *) malloc(strlen(kstr) + 1);

  if (tkstr == NULL)
  {
    int e = errno;
    ESP_LOGE(CAMWEBSRV_TAG, "CFGMAN _camwebsrv_cfgman_set_full(): malloc() failed: [%d]: %s", e, strerror(e));
    return false;
  }

  strcpy(tkstr, kstr);

  // copy value

  tvstr = (char *) malloc(strlen(vstr) + 1);

  if (tvstr == NULL)
  {
    int e = errno;
    ESP_LOGE(CAMWEBSRV_TAG, "CFGMAN _camwebsrv_cfgman_set_full(): malloc() failed: [%d]: %s", e, strerror(e));
    free(tkstr);
    return false;
  }

  strcpy(tvstr, vstr);

  // create new node

  node = (_camwebsrv_cfgman_node_t *) malloc(sizeof(_camwebsrv_cfgman_node_t));

  if (node == NULL)
  {
    int e = errno;
    ESP_LOGE(CAMWEBSRV_TAG, "CFGMAN _camwebsrv_cfgman_set_full(): malloc() failed: [%d]: %s", e, strerror(e));
    free(tkstr);
    free(tvstr);
    return false;
  }

  // fill 'er up

  node->kstr = tkstr;
  node->vstr = tvstr;
  node->next = NULL;

  // attach to list

  if (pcfg->tail == NULL)
  {
    pcfg->head = node;
  }
  else
  {
    pcfg->tail->next = node;
  }

  pcfg->tail = node;
  pcfg->length = pcfg->length + 1;

  return true;
}

static bool _camwebsrv_cfgman_set_subst(_camwebsrv_cfgman_t *pcfg, const char *kstr, off_t ks, off_t ke, const char *vstr, off_t vs, off_t ve)
{
  size_t klen;
  size_t vlen;
  char *tkstr;
  char *tvstr;
  bool rv;

  klen = ke - ks + 1;
  vlen = ((vs == 0 && ve == 0) ? 0 : ve - vs + 1);

  tkstr = (char *) malloc((klen + 1));

  if (tkstr == NULL)
  {
    int e = errno;
    ESP_LOGE(CAMWEBSRV_TAG, "CFGMAN __camwebsrv_cfgman_set_subst(): malloc() failed: [%d]: %s", e, strerror(e));
    return false;
  }

  tvstr = (char *) malloc((vlen + 1));

  if (tvstr == NULL)
  {
    int e = errno;
    ESP_LOGE(CAMWEBSRV_TAG, "CFGMAN __camwebsrv_cfgman_set_subst(): malloc() failed: [%d]: %s", e, strerror(e));
    free(tkstr);
    return false;
  }

  memcpy(tkstr, kstr + ks, klen);
  memcpy(tvstr, vstr + vs, vlen);

  tkstr[klen] = 0x00;
  tvstr[vlen] = 0x00;

  rv = _camwebsrv_cfgman_set_full(pcfg, tkstr, tvstr);

  free(tkstr);
  free(tvstr);

  return rv;
}

static bool _camwebsrv_cfgman_load_cb(const char *buf, size_t len, void *arg)
{
  off_t i;
  off_t ks = 0;
  off_t ke = 0;
  off_t vs = 0;
  off_t ve = 0;
  unsigned char state = 0;
  unsigned int line = 0;
  _camwebsrv_cfgman_t *pcfg = (_camwebsrv_cfgman_t *) arg;

  for(i = 0; i < len; i++)
  {
    // ignore \r and count \n

    switch(buf[i])
    {
      case 0x0D:
        continue;
      case 0x0A:
        line = line + 1;
	break;
    }

    // parse with an fsm

    switch(state)
    {
      case 0:
        // start
        if (buf[i] == 0x0A || buf[i] == 0x20 || buf[i] == 0x09)
        {
        }
        else if (buf[i] == '#')
        {
          state = 1;
        }
        else if ((buf[i] >= 'A' && buf[i] <= 'Z') || (buf[i] >= 'a' && buf[i] <= 'z') || (buf[i] == '_'))
        {
          ks = i;
          ke = i;
          state = 2;
        }
        else
        {
          ESP_LOGE(CAMWEBSRV_TAG, "CFGMAN _camwebsrv_cfgman_load_cb(): parse error line %u: invalid key character", line + 1);
          return false;
        }
        break;
      case 1:
        // comment
        if (buf[i] == 0x0A)
        {
          state = 0;
        }
        break;
      case 2:
        // key
        if ((buf[i] >= 'A' && buf[i] <= 'Z') || (buf[i] >= 'a' && buf[i] <= 'z') || (buf[i] >= '0' && buf[i] <= '9') || (buf[i] == '_'))
        {
          // part of key
          ke = i;
        }
        else if (buf[i] == 0x20 || buf[i] == 0x09)
        {
          // space; end of key
          state = 3;
        }
        else if (buf[i] == '=')
        {
          // equals; end of key
          state = 4;
        }
        else
        {
          ESP_LOGE(CAMWEBSRV_TAG, "CFGMAN _camwebsrv_cfgman_load_cb(): parse error line %u: invalid key character", line + 1);
          return false;
        }
        break;
      case 3:
        // space between key and equals
        if (buf[i] == '=')
        {
          // equals
          state = 4;
        }
        else if (buf[i] != 0x20 && buf[i] != 0x09)
        {
          ESP_LOGE(CAMWEBSRV_TAG, "CFGMAN _camwebsrv_cfgman_load_cb(): parse error line %u: expecting space or equals character; found 0x%X", line + 1, buf[i]);
          return false;
        }
        break;
      case 4:
        // space between equals and value
        if (buf[i] == 0x0A)
        {
          // EOL, so value is blank
          if (!_camwebsrv_cfgman_set_subst(pcfg, buf, ks, ke, "", 0, 0))
          {
            ESP_LOGE(CAMWEBSRV_TAG, "CFGMAN _camwebsrv_cfgman_load_cb(): parse error line %u: internal error", line + 1);
            return false;
          }

	  ks = 0;
	  ke = 0;
	  vs = 0;
	  ve = 0;
          state = 0;
        }
        else if (buf[i] != 0x20 && buf[i] != 0x09)
        {
          // start of value
	  vs = i;
	  ve = i;
          state = 5;
        }
        break;
      case 5:
        // value
	if (buf[i] == 0x0a)
        {
          // end of value
          if (!_camwebsrv_cfgman_set_subst(pcfg, buf, ks, ke, buf, vs, ve))
          {
            ESP_LOGE(CAMWEBSRV_TAG, "CFGMAN _camwebsrv_cfgman_load_cb(): parse error line %u: internal error", line + 1);
            return false;
          }

	  ks = 0;
	  ke = 0;
	  vs = 0;
	  ve = 0;
          state = 0;
        }
        else if (buf[i] == 0x20 || buf[i] == 0x09)
        {
          // space
          state = 6;
        }
        else
        {
          // not EOL and not space; part of value
	  ve = i;
        }
        break;
      case 6:
        // space between value and EOF
        if (buf[i] == 0x0a)
        {
          // end of value
          if (!_camwebsrv_cfgman_set_subst(pcfg, buf, ks, ke, buf, vs, ve))
          {
            ESP_LOGE(CAMWEBSRV_TAG, "CFGMAN _camwebsrv_cfgman_load_cb(): parse error line %u: internal error", line + 1);
            return false;
          }

	  ks = 0;
	  ke = 0;
	  vs = 0;
	  ve = 0;
          state = 0;
        }
        else if (buf[i] != 0x20 && buf[i] != 0x09)
        {
          // not space; so this plus all previous spaces are part of value
	  ve = i;
          state = 5;
        }
        break;
    }
  }

  return true;
}
