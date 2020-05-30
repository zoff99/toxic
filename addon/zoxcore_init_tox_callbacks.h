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

#ifndef ZOXCORE_INIT_TOX_CALLBACKS_H
#define ZOXCORE_INIT_TOX_CALLBACKS_H

#ifdef TOX_HAVE_TOXUTIL

    printf("using toxutil\n");
    
    // -- wrapped callbacks --
    tox_utils_callback_self_connection_status(m, on_self_connection_status);
    tox_callback_self_connection_status(m, tox_utils_self_connection_status_cb);

    tox_utils_callback_friend_connection_status(m, on_friend_connection_status);
    tox_callback_friend_connection_status(m, tox_utils_friend_connection_status_cb);

    tox_callback_friend_lossless_packet(m, tox_utils_friend_lossless_packet_cb);

    tox_utils_callback_file_recv_control(m, on_file_recv_control);
    tox_callback_file_recv_control(m, tox_utils_file_recv_control_cb);

    tox_utils_callback_file_chunk_request(m, on_file_chunk_request);
    tox_callback_file_chunk_request(m, tox_utils_file_chunk_request_cb);

    tox_utils_callback_file_recv(m, on_file_recv);
    tox_callback_file_recv(m, tox_utils_file_recv_cb);

    tox_utils_callback_file_recv_chunk(m, on_file_recv_chunk);
    tox_callback_file_recv_chunk(m, tox_utils_file_recv_chunk_cb);
    // -- wrapped callbacks --

    // -- old-only callbacks --
    tox_callback_friend_typing(m, on_friend_typing);
    tox_callback_friend_request(m, on_friend_request);
    tox_callback_friend_message(m, on_friend_message);
    tox_callback_friend_name(m, on_friend_name);
    tox_callback_friend_status(m, on_friend_status);
    tox_callback_friend_status_message(m, on_friend_status_message);
    tox_callback_friend_read_receipt(m, on_friend_read_receipt);
    tox_callback_conference_invite(m, on_conference_invite);
    tox_callback_conference_message(m, on_conference_message);
    tox_callback_conference_peer_list_changed(m, on_conference_peer_list_changed);
    tox_callback_conference_peer_name(m, on_conference_peer_name);
    tox_callback_conference_title(m, on_conference_title);
    // -- old-only callbacks --

    // -- msgV2 callbacks --
    tox_utils_callback_friend_message_v2(m, friend_message_v2_cb);
    tox_utils_callback_friend_read_receipt_message_v2(m, friend_read_receipt_message_v2_cb);
    tox_utils_callback_friend_sync_message_v2(m, friend_sync_message_v2_cb);
    // -- msgV2 callbacks --

#else
    printf("** NOT USING toxutil **\n");

    tox_callback_self_connection_status(m, on_self_connection_status);
    tox_callback_friend_connection_status(m, on_friend_connection_status);
    tox_callback_friend_typing(m, on_friend_typing);
    tox_callback_friend_request(m, on_friend_request);
    tox_callback_friend_message(m, on_friend_message);
    tox_callback_friend_name(m, on_friend_name);
    tox_callback_friend_status(m, on_friend_status);
    tox_callback_friend_status_message(m, on_friend_status_message);
    tox_callback_friend_read_receipt(m, on_friend_read_receipt);
    tox_callback_conference_invite(m, on_conference_invite);
    tox_callback_conference_message(m, on_conference_message);
    tox_callback_conference_peer_list_changed(m, on_conference_peer_list_changed);
    tox_callback_conference_peer_name(m, on_conference_peer_name);
    tox_callback_conference_title(m, on_conference_title);
    tox_callback_file_recv(m, on_file_recv);
    tox_callback_file_chunk_request(m, on_file_chunk_request);
    tox_callback_file_recv_control(m, on_file_recv_control);
    tox_callback_file_recv_chunk(m, on_file_recv_chunk);
#endif

#endif /* ZOXCORE_INIT_TOX_CALLBACKS_H */
