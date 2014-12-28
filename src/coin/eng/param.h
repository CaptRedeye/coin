/*######     Copyright (c) 1997-2015 Ufasoft  http://ufasoft.com  mailto:support@ufasoft.com,  Sergey Pavlov  mailto:dev@ufasoft.com #########################################################################################################
#                                                                                                                                                                                                                                            #
# This program is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation;  either version 3, or (at your option) any later version.          #
# This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.   #
# You should have received a copy of the GNU General Public License along with this program; If not, see <http://www.gnu.org/licenses/>                                                                                                      #
############################################################################################################################################################################################################################################*/

#pragma once

namespace Coin {

const uint32_t PROTOCOL_VERSION = 70002;

const uint64_t SEND_FEE_THOUSANDTH = 4;		// 0.4%

const int MAX_BLOCK_SIZE = 1000000;
const int MAX_BLOCK_SIZE_GEN = MAX_BLOCK_SIZE/2;
const uint32_t MAX_PAYLOAD = 32*1024*1024;
const int MAX_FUTURE_SECONDS = 7200;
const size_t MAX_ORPHAN_TRANSACTIONS = MAX_BLOCK_SIZE/100;
const size_t MAX_SCRIPT_ELEMENT_SIZE = 520;

const size_t MAX_BLOOM_FILTER_SIZE = 36000; // bytes
const int MAX_HASH_FUNCS = 50;

static const int MAX_BLOCK_SIGOPS = MAX_BLOCK_SIZE/50;

const int MAX_SEND_SIZE = 1000000;

//!!!static const int64_t MIN_TX_FEE = 50000;
//!!!static const int64_t MIN_RELAY_TX_FEE = 10000;
static const int KEYPOOL_SIZE = 100;

const int INITIAL_BLOCK_THRESHOLD = 120;


const size_t MAX_INV_SZ = 50000;

} // Coin::

#define COIN_BACKEND_DBLITE 'D'
#define COIN_BACKEND_SQLITE 'S'

#ifndef UCFG_COIN_COINCHAIN_BACKEND
#	define UCFG_COIN_COINCHAIN_BACKEND COIN_BACKEND_DBLITE
#endif

#ifndef UCFG_COIN_CONVERT_TO_UDB
#	define UCFG_COIN_CONVERT_TO_UDB 0
#endif

#ifndef UCFG_COIN_MERKLE_FUTURES
#	define UCFG_COIN_MERKLE_FUTURES 1
#endif

#ifndef UCFG_COIN_PKSCRIPT_FUTURES
#	define UCFG_COIN_PKSCRIPT_FUTURES 1
#endif

#ifndef UCFG_COIN_COMPACT_AUX
#	define UCFG_COIN_COMPACT_AUX 0					// incompatible space optimization
#endif

