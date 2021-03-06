/* Teensyduino Core Library
 * http://www.pjrc.com/teensy/
 * Copyright (c) 2017 PJRC.COM, LLC.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * 1. The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * 2. If the Software is incorporated into a build system that allows
 * selection among a list of target devices, then similar target
 * devices manufactured by PJRC.COM must be included in the list of
 * target devices and selectable in the same manner.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Wayne Holder, Sep 2019 - Removed unused code
 */

#include "usb_dev.h"
#include "usb_serial.h"

static usb_packet_t *rx_packet = NULL;
static usb_packet_t *tx_packet = NULL;
static volatile uint8_t tx_noautoflush = 0;

uint32_t usb_cdc_line_coding[2];
volatile uint32_t usb_cdc_line_rtsdtr_millis;
volatile uint8_t usb_cdc_line_rtsdtr = 0;
volatile uint8_t usb_cdc_transmit_flush_timer = 0;
#define TRANSMIT_FLUSH_TIMEOUT  5   /* in milliseconds */

// get the next character, or -1 if nothing received
int usb_serial_getchar (void) {
  unsigned int i;
  int c;
  if (!rx_packet) {
    if (!usb_configuration) {
      return -1;
    }
    rx_packet = usb_rx(CDC_RX_ENDPOINT);
    if (!rx_packet) {
      return -1;
    }
  }
  i = rx_packet->index;
  c = rx_packet->buf[i++];
  if (i >= rx_packet->len) {
    usb_free(rx_packet);
    rx_packet = NULL;
  } else {
    rx_packet->index = i;
  }
  return c;
}

// number of bytes available in the receive buffer
int usb_serial_available (void) {
  int count = usb_rx_byte_count(CDC_RX_ENDPOINT);
  if (rx_packet) {
    count += rx_packet->len - rx_packet->index;
  }
  return count;
}

// Maximum number of transmit packets to queue so we don't starve other endpoints for memory
#define TX_PACKET_LIMIT 8

// When the PC isn't listening, how long do we wait before discarding data?  If this is
// too short, we risk losing data during the stalls that are common with ordinary desktop
// software.  If it's too long, we stall the user's program when no software is running.
#define TX_TIMEOUT_MSEC (70)
#define TX_TIMEOUT (TX_TIMEOUT_MSEC*512)
// When we've suffered the transmit timeout, don't wait again until the computer
// begins accepting data.  If no software is running to receive, we'll just discard
// data as rapidly as Serial.print() can generate it, until there's something to
// actually receive it.
static uint8_t transmit_previous_timeout = 0;

int usb_serial_write (const void *buffer, uint32_t size) {
  uint32_t ret = size, len, wait_count;
  const uint8_t *src = (const uint8_t *) buffer;
  uint8_t *dest;
  tx_noautoflush = 1;
  while (size > 0) {
    if (!tx_packet) {
      wait_count = 0;
      while (1) {
        if (!usb_configuration) {
          tx_noautoflush = 0;
          return -1;
        }
        if (usb_tx_packet_count(CDC_TX_ENDPOINT) < TX_PACKET_LIMIT) {
          tx_noautoflush = 1;
          tx_packet = usb_malloc();
          if (tx_packet)
            break;
          tx_noautoflush = 0;
        }
        if (++wait_count > TX_TIMEOUT || transmit_previous_timeout) {
          transmit_previous_timeout = 1;
          return -1;
        }
      }
    }
    transmit_previous_timeout = 0;
    len = CDC_TX_SIZE - tx_packet->index;
    if (len > size) {
      len = size;
    }
    dest = tx_packet->buf + tx_packet->index;
    tx_packet->index += len;
    size -= len;
    while (len-- > 0) {
      *dest++ = *src++;
    }
    if (tx_packet->index >= CDC_TX_SIZE) {
      tx_packet->len = CDC_TX_SIZE;
      usb_tx(CDC_TX_ENDPOINT, tx_packet);
      tx_packet = NULL;
    }
    usb_cdc_transmit_flush_timer = TRANSMIT_FLUSH_TIMEOUT;
  }
  tx_noautoflush = 0;
  return ret;
}

void usb_serial_flush_output (void) {
  if (!usb_configuration) {
    return;
  }
  tx_noautoflush = 1;
  if (tx_packet) {
    usb_cdc_transmit_flush_timer = 0;
    tx_packet->len = tx_packet->index;
    usb_tx(CDC_TX_ENDPOINT, tx_packet);
    tx_packet = NULL;
  } else {
    usb_packet_t *tx = usb_malloc();
    if (tx) {
      usb_cdc_transmit_flush_timer = 0;
      usb_tx(CDC_TX_ENDPOINT, tx);
    } else {
      usb_cdc_transmit_flush_timer = 1;
    }
  }
  tx_noautoflush = 0;
}

void usb_serial_flush_callback (void) {
  if (tx_noautoflush)
    return;
  if (tx_packet) {
    tx_packet->len = tx_packet->index;
    usb_tx(CDC_TX_ENDPOINT, tx_packet);
    tx_packet = NULL;
  } else {
    usb_packet_t *tx = usb_malloc();
    if (tx) {
      usb_tx(CDC_TX_ENDPOINT, tx);
    } else {
      usb_cdc_transmit_flush_timer = 1;
    }
  }
}
