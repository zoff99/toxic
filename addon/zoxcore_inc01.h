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

#ifndef ZOXCORE_INC01_H
#define ZOXCORE_INC01_H

#undef TOX_HAVE_TOXUTIL
#define TOX_HAVE_TOXUTIL 1

#ifdef TOX_HAVE_TOXUTIL
#include <tox/toxutil.h>

// #define TC_LOGGING_ACTIVE 1

void friend_message_v2_cb(Tox *m, uint32_t friendnumber, const uint8_t *raw_message, size_t raw_message_len);
void friend_read_receipt_message_v2_cb(Tox *m, uint32_t friendnumber, uint32_t ts_sec, const uint8_t *msgid);
void friend_sync_message_v2_cb(Tox *m, uint32_t friendnumber, const uint8_t *message, size_t length);

void tox_log_cb__custom(Tox *m, TOX_LOG_LEVEL level, const char *file, uint32_t line, const char *func,
                        const char *message, void *user_data)
{
#ifdef TC_LOGGING_ACTIVE
    FILE *logfile = fopen("toxic_toxcore_log.txt", "ab");
    setvbuf(logfile, NULL, _IOLBF, 0);
    fprintf(logfile, "%d:%s:%d:%s:%s\n", (int)level, file, (int)line, func, message);
    fclose(logfile);
#endif
}


#endif

#endif /* ZOXCORE_INC01_H */
