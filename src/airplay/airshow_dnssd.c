/**
 *  AirShow's replacement for UxPlay's lib/dnssd.c (same API, dnssd.h).
 *
 *  UxPlay's version registers the AirPlay and RAOP services through Apple's
 *  dns_sd API and, on Windows, cannot even be created unless Bonjour's
 *  dnssd.dll is installed. AirShow advertises every service itself
 *  (src/discovery), so this keeps only what the RAOP server reads back: name,
 *  hardware address, feature bits, public key, and the TXT records it serves
 *  from GET /info. The register functions build those TXT records and
 *  advertise nothing.
 *
 *  Derived from UxPlay lib/dnssd.c:
 *  Copyright (C) 2011-2012  Juho Vähä-Herttua, modified by fduncanh 2022.
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2.1 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dnssd.h"
#include "dnssdint.h"
#include "global.h"
#include "utils.h"

#define MAX_TXT 1024

typedef struct {
    unsigned char data[MAX_TXT];
    int len;
} txt_record_t;

struct dnssd_s {
    char *name;
    int name_len;

    char *hw_addr;
    int hw_addr_len;

    char *pk;

    uint32_t features1;
    uint32_t features2;

    unsigned char pin_pw;

    txt_record_t raop_txt;
    txt_record_t airplay_txt;
};

/* Same behaviour as Bonjour's TXTRecordSetValue: an existing key is removed
 * and the new "key=value" entry appended. */
static void
txt_set(txt_record_t *txt, const char *key, const char *value)
{
    size_t key_len = strlen(key);
    size_t entry_len = key_len + 1 + strlen(value);
    int pos = 0;

    while (pos < txt->len) {
        int len = txt->data[pos];
        if ((size_t) len >= key_len && memcmp(txt->data + pos + 1, key, key_len) == 0 &&
            ((size_t) len == key_len || txt->data[pos + 1 + key_len] == '=')) {
            memmove(txt->data + pos, txt->data + pos + 1 + len, txt->len - pos - 1 - len);
            txt->len -= 1 + len;
            continue;
        }
        pos += 1 + len;
    }

    if (entry_len > 255 || txt->len + 1 + (int) entry_len > MAX_TXT) {
        return;
    }
    txt->data[txt->len++] = (unsigned char) entry_len;
    memcpy(txt->data + txt->len, key, key_len);
    txt->data[txt->len + key_len] = '=';
    memcpy(txt->data + txt->len + key_len + 1, value, strlen(value));
    txt->len += (int) entry_len;
}

static const char *
dnssd_pk(dnssd_t *dnssd)
{
    return dnssd->pk ? dnssd->pk : "";
}

static void
build_raop_txt(dnssd_t *dnssd)
{
    char features[22] = {0};
    txt_record_t *txt = &dnssd->raop_txt;

    snprintf(features, sizeof(features), "0x%X,0x%X", dnssd->features1, dnssd->features2);

    txt->len = 0;
    txt_set(txt, "ch", RAOP_CH);
    txt_set(txt, "cn", RAOP_CN);
    txt_set(txt, "da", RAOP_DA);
    txt_set(txt, "et", RAOP_ET);
    txt_set(txt, "vv", RAOP_VV);
    txt_set(txt, "ft", features);
    txt_set(txt, "am", GLOBAL_MODEL);
    txt_set(txt, "md", RAOP_MD);
    txt_set(txt, "rhd", RAOP_RHD);
    switch (dnssd->pin_pw) {
    case 2:
    case 3:
        txt_set(txt, "pw", "true");
        txt_set(txt, "sf", "0x84");
        break;
    case 1:
        txt_set(txt, "pw", "true");
        txt_set(txt, "sf", "0x8c");
        break;
    default:
        txt_set(txt, "pw", "false");
        txt_set(txt, "sf", RAOP_SF);
        break;
    }
    txt_set(txt, "sr", RAOP_SR);
    txt_set(txt, "ss", RAOP_SS);
    txt_set(txt, "sv", RAOP_SV);
    txt_set(txt, "tp", RAOP_TP);
    txt_set(txt, "txtvers", RAOP_TXTVERS);
    txt_set(txt, "sf", RAOP_SF);
    txt_set(txt, "vs", RAOP_VS);
    txt_set(txt, "vn", RAOP_VN);
    txt_set(txt, "pk", dnssd_pk(dnssd));
}

static void
build_airplay_txt(dnssd_t *dnssd)
{
    char device_id[3 * MAX_HWADDR_LEN];
    char features[22] = {0};
    txt_record_t *txt = &dnssd->airplay_txt;

    snprintf(features, sizeof(features), "0x%X,0x%X", dnssd->features1, dnssd->features2);
    if (utils_hwaddr_airplay(device_id, sizeof(device_id), dnssd->hw_addr, dnssd->hw_addr_len) < 0) {
        device_id[0] = '\0';
    }

    txt->len = 0;
    txt_set(txt, "deviceid", device_id);
    txt_set(txt, "features", features);
    txt_set(txt, "pw", dnssd->pin_pw ? "true" : "false");
    txt_set(txt, "flags", "0x4");
    txt_set(txt, "model", GLOBAL_MODEL);
    txt_set(txt, "pk", dnssd_pk(dnssd));
    txt_set(txt, "pi", AIRPLAY_PI);
    txt_set(txt, "srcvers", AIRPLAY_SRCVERS);
    txt_set(txt, "vv", AIRPLAY_VV);
}

dnssd_t *
dnssd_init(const char* name, int name_len, const char* hw_addr, int hw_addr_len, int *error, unsigned char pin_pw)
{
    /* pin_pw = 0: no pin or password
                1: use onscreen pin for client access control
                2 or 3: require password for client access control
    */
    char *end = NULL;
    unsigned long features;
    dnssd_t *dnssd;

    if (error) *error = DNSSD_ERROR_NOERROR;

    dnssd = (dnssd_t *) calloc(1, sizeof(dnssd_t));
    if (!dnssd) {
        if (error) *error = DNSSD_ERROR_OUTOFMEM;
        return NULL;
    }
    dnssd->pin_pw = pin_pw;

    features = strtoul(FEATURES_1, &end, 16);
    if (!end || (features & 0xFFFFFFFF) != features) {
        free(dnssd);
        if (error) *error = DNSSD_ERROR_BADFEATURES;
        return NULL;
    }
    dnssd->features1 = (uint32_t) features;

    features = strtoul(FEATURES_2, &end, 16);
    if (!end || (features & 0xFFFFFFFF) != features) {
        free(dnssd);
        if (error) *error = DNSSD_ERROR_BADFEATURES;
        return NULL;
    }
    dnssd->features2 = (uint32_t) features;

    dnssd->name_len = name_len;
    dnssd->name = calloc(1, name_len + 1);
    dnssd->hw_addr_len = hw_addr_len;
    dnssd->hw_addr = calloc(1, hw_addr_len + 1);
    if (!dnssd->name || !dnssd->hw_addr) {
        dnssd_destroy(dnssd);
        if (error) *error = DNSSD_ERROR_OUTOFMEM;
        return NULL;
    }
    memcpy(dnssd->name, name, name_len);
    memcpy(dnssd->hw_addr, hw_addr, hw_addr_len);
    return dnssd;
}

void
dnssd_destroy(dnssd_t *dnssd)
{
    if (dnssd) {
        free(dnssd->name);
        free(dnssd->hw_addr);
        free(dnssd);
    }
}

int
dnssd_register_raop(dnssd_t *dnssd, unsigned short port)
{
    (void) port;
    assert(dnssd);
    build_raop_txt(dnssd);
    return 0;
}

int
dnssd_register_airplay(dnssd_t *dnssd, unsigned short port)
{
    (void) port;
    assert(dnssd);
    build_airplay_txt(dnssd);
    return 0;
}

void
dnssd_unregister_raop(dnssd_t *dnssd)
{
    assert(dnssd);
}

void
dnssd_unregister_airplay(dnssd_t *dnssd)
{
    assert(dnssd);
}

/* Rebuilt on every call: the public key is only known once raop_init2 ran. */
const char *
dnssd_get_raop_txt(dnssd_t *dnssd, int *length)
{
    build_raop_txt(dnssd);
    *length = dnssd->raop_txt.len;
    return (const char *) dnssd->raop_txt.data;
}

const char *
dnssd_get_airplay_txt(dnssd_t *dnssd, int *length)
{
    build_airplay_txt(dnssd);
    *length = dnssd->airplay_txt.len;
    return (const char *) dnssd->airplay_txt.data;
}

const char *
dnssd_get_name(dnssd_t *dnssd, int *length)
{
    *length = dnssd->name_len;
    return dnssd->name;
}

const char *
dnssd_get_hw_addr(dnssd_t *dnssd, int *length)
{
    *length = dnssd->hw_addr_len;
    return dnssd->hw_addr;
}

uint64_t
dnssd_get_airplay_features(dnssd_t *dnssd)
{
    uint64_t features = ((uint64_t) dnssd->features2) << 32;
    features += (uint64_t) dnssd->features1;
    return features;
}

void
dnssd_set_pk(dnssd_t *dnssd, char *pk_str)
{
    dnssd->pk = pk_str;
}

void
dnssd_set_airplay_features(dnssd_t *dnssd, int bit, int val)
{
    uint32_t mask = 0;
    uint32_t *features = 0;
    if (bit < 0 || bit > 63) return;
    if (val < 0 || val > 1) return;
    if (bit >= 32) {
        mask = 0x1 << (bit - 32);
        features = &(dnssd->features2);
    } else {
        mask = 0x1 << bit;
        features = &(dnssd->features1);
    }
    if (val) {
        *features = *features | mask;
    } else {
        *features = *features & ~mask;
    }
}
