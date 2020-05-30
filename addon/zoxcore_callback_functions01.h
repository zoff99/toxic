/*
 *  Copyright (C) 2019 Zoff <zoff@zoff.cc>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef ZOXCORE_CALLBACK_FUNCTIONS01_H
#define ZOXCORE_CALLBACK_FUNCTIONS01_H

#include <tox/toxutil.h>

// for timestamps
#include <time.h>
#include <sys/time.h>

void friend_message_v2_cb(Tox *m, uint32_t friendnumber, const uint8_t *raw_message, size_t raw_message_len)
{

    // now get the real data from msgV2 buffer
    uint8_t *message_text = calloc(1, raw_message_len);

    if (message_text) {
        // uint32_t ts_sec = tox_messagev2_get_ts_sec(raw_message);
        // uint16_t ts_ms = tox_messagev2_get_ts_ms(raw_message);
        uint32_t text_length = 0;
        tox_messagev2_get_message_text(raw_message, (uint32_t) raw_message_len, (bool) false, (uint32_t) 0,
                   message_text, &text_length);

        char msg[MAX_STR_SIZE + 1];
        int length_new = copy_tox_str(msg, sizeof(msg), (const char *) message_text, text_length);

        for (uint8_t i = 0; i < MAX_WINDOWS_NUM; ++i) {
            if (windows[i] != NULL && windows[i]->onMessage != NULL) {
                windows[i]->onMessage(windows[i], m, friendnumber, TOX_MESSAGE_TYPE_NORMAL, msg, length_new);
            }
        }

        // send msgV2 receipt
        uint8_t *msgid_buffer = calloc(1, TOX_PUBLIC_KEY_SIZE + 1);
        if (msgid_buffer)
        {
            memset(msgid_buffer, 0, TOX_PUBLIC_KEY_SIZE + 1);
            tox_messagev2_get_message_id(raw_message, msgid_buffer);
            uint32_t ts_sec = (uint32_t)time(NULL);
            tox_util_friend_send_msg_receipt_v2(m, (uint32_t) friendnumber, msgid_buffer, ts_sec);
            free(msgid_buffer);
        }
    }

}

void friend_read_receipt_message_v2_cb(Tox *m, uint32_t friendnumber, uint32_t ts_sec, const uint8_t *msgid)
{
    printf("friend_read_receipt_message_v2_cb:friendnum=%d\n", (int)friendnumber);
}


void friend_sync_message_v2_cb(Tox *m, uint32_t friendnumber, const uint8_t *message, size_t length)
{
    printf("friend_sync_message_v2_cb:friendnum=%d\n", (int)friendnumber);
}


#endif /* ZOXCORE_CALLBACK_FUNCTIONS01_H */
