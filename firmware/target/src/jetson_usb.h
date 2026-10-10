/* Jetson時刻protocolをUSB CDCの2つ目のinterfaceで受ける（製品firmware） */
#ifndef JETSON_USB_H
#define JETSON_USB_H

void jetson_usb_init(void);

/* main loopから呼ぶ */
void jetson_usb_task(void);

#endif /* JETSON_USB_H */
