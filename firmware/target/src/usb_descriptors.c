/*
 * USB descriptor（CDC 1つ。製品firmware（FW_APP）はJetson protocol用にもう1つ）
 *
 * VID/PIDはTinyUSBの試験用値0xCAFEを仮に使う。製品用の値はfirmware/test_plan.md のP-21で決める。
 * serial numberはMCUの96-bit UIDから作る。
 */
#include <string.h>

#include "stm32g0xx.h"
#include "tusb.h"

#define USB_VID 0xCAFEu
#define USB_PID 0x4001u

enum {
    ITF_NUM_CDC = 0,
    ITF_NUM_CDC_DATA,
#ifdef FW_APP
    ITF_NUM_CDC2,
    ITF_NUM_CDC2_DATA,
#endif
    ITF_NUM_TOTAL
};

enum {
    STRID_LANGID = 0,
    STRID_MANUFACTURER,
    STRID_PRODUCT,
    STRID_SERIAL,
    STRID_CDC,
    STRID_CDC2,
};

#define EPNUM_CDC_NOTIF 0x81
#define EPNUM_CDC_OUT 0x02
#define EPNUM_CDC_IN 0x82
#define EPNUM_CDC2_NOTIF 0x83
#define EPNUM_CDC2_OUT 0x04
#define EPNUM_CDC2_IN 0x84

#ifdef FW_APP
#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + 2 * TUD_CDC_DESC_LEN)
#else
#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN)
#endif

static const tusb_desc_device_t desc_device = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = USB_VID,
    .idProduct = USB_PID,
    .bcdDevice = 0x0100,
    .iManufacturer = STRID_MANUFACTURER,
    .iProduct = STRID_PRODUCT,
    .iSerialNumber = STRID_SERIAL,
    .bNumConfigurations = 1,
};

/* bus-powered、最大100 mA（power_budget.mdの合否基準） */
static const uint8_t desc_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 100),
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, STRID_CDC, EPNUM_CDC_NOTIF, 8, EPNUM_CDC_OUT, EPNUM_CDC_IN, 64),
#ifdef FW_APP
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC2, STRID_CDC2, EPNUM_CDC2_NOTIF, 8, EPNUM_CDC2_OUT, EPNUM_CDC2_IN, 64),
#endif
};

static const char *const string_desc[] = {
    [STRID_MANUFACTURER] = "cub2026",
    [STRID_PRODUCT] = "timing_sync_if",
    [STRID_CDC] = "timing_sync_if console",
    [STRID_CDC2] = "timing_sync_if jetson",
};

static uint16_t desc_str[32 + 1];

uint8_t const *tud_descriptor_device_cb(void)
{
    return (uint8_t const *)&desc_device;
}

uint8_t const *tud_descriptor_configuration_cb(uint8_t index)
{
    (void)index;
    return desc_configuration;
}

static size_t uid_to_utf16(uint16_t *out, size_t max)
{
    static const char hex[] = "0123456789ABCDEF";
    const volatile uint32_t *uid = (const volatile uint32_t *)UID_BASE;
    size_t n = 0;

    for (int w = 2; w >= 0; w--) {
        const uint32_t v = uid[w];
        for (int nib = 7; nib >= 0 && n < max; nib--) {
            out[n++] = (uint16_t)hex[(v >> (nib * 4)) & 0xFu];
        }
    }
    return n;
}

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
    (void)langid;
    size_t count = 0;
    const size_t max = sizeof(desc_str) / sizeof(desc_str[0]) - 1u;

    if (index == STRID_LANGID) {
        desc_str[1] = 0x0409;
        count = 1;
    } else if (index == STRID_SERIAL) {
        count = uid_to_utf16(&desc_str[1], max);
    } else {
        if (index >= sizeof(string_desc) / sizeof(string_desc[0]) || string_desc[index] == NULL) {
            return NULL;
        }
        const char *s = string_desc[index];
        count = strlen(s);
        if (count > max) {
            count = max;
        }
        for (size_t i = 0; i < count; i++) {
            desc_str[1 + i] = (uint16_t)s[i];
        }
    }
    desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2u * count + 2u));
    return desc_str;
}
