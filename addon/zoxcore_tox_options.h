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

#ifndef ZOXCORE_TOX_OPTIONS_H
#define ZOXCORE_TOX_OPTIONS_H

// ------------------------------------------------------------
// set our own handler for c-toxcore logging messages!!
// tox_opts->log_callback = tox_log_cb__custom;
tox_options_set_log_callback(tox_opts, tox_log_cb__custom);
// ------------------------------------------------------------

#endif /* ZOXCORE_TOX_OPTIONS_H */

