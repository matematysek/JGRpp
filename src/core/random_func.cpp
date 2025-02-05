/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file random_func.cpp Implementation of the pseudo random generator. */

#include "../stdafx.h"
#include "random_func.hpp"
#include "bitmath_func.hpp"
#include "hash_func.hpp"
#include "../debug.h"
#include <atomic>
#include <bit>
#include <chrono>

#ifdef RANDOM_DEBUG
#include "../network/network.h"
#include "../network/network_server.h"
#include "../network/network_internal.h"
#include "../company_func.h"
#include "../fileio_func.h"
#include "../date_func.h"
#endif /* RANDOM_DEBUG */

#if defined(_WIN32)
#	include <windows.h>
#	include <bcrypt.h>
#elif defined(__APPLE__) || defined(__NetBSD__) || defined(__FreeBSD__)
// No includes required.
#elif defined(__GLIBC__) && ((__GLIBC__ > 2) || ((__GLIBC__ == 2) && (__GLIBC_MINOR__ >= 25)))
#	include <sys/random.h>
#elif defined(__EMSCRIPTEN__)
#	include <emscripten.h>
#endif

#include "../safeguards.h"

Randomizer _random, _interactive_random;

/**
 * Generate the next pseudo random number
 * @return the random number
 */
uint32_t Randomizer::Next()
{
	const uint32_t s = this->state[0];
	const uint32_t t = this->state[1];

	this->state[0] = s + std::rotr(t ^ 0x1234567F, 7) + 1;
	return this->state[1] = std::rotr(s, 3) - 1;
}

/**
 * Generate the next pseudo random number scaled to \a limit, excluding \a limit
 * itself.
 * @param limit Limit of the range to be generated from.
 * @return Random number in [0,\a limit)
 */
uint32_t Randomizer::Next(uint32_t limit)
{
	return ((uint64_t)this->Next() * (uint64_t)limit) >> 32;
}

/**
 * (Re)set the state of the random number generator.
 * @param seed the new state
 */
void Randomizer::SetSeed(uint32_t seed)
{
	this->state[0] = seed;
	this->state[1] = seed;
}

/**
 * (Re)set the state of the random number generators.
 * @param seed the new state
 */
void SetRandomSeed(uint32_t seed)
{
	_random.SetSeed(seed);
	_interactive_random.SetSeed(seed * 0x1234567);
}

#ifdef RANDOM_DEBUG
uint32_t DoRandom(int line, const char *file)
{
	if (_networking && (!_network_server || (NetworkClientSocket::IsValidID(0) && NetworkClientSocket::Get(0)->status != NetworkClientSocket::STATUS_INACTIVE))) {
		DEBUG(random, 0, "%s; %04x; %02x; %s:%d", debug_date_dumper().HexDate(), _frame_counter, (uint8_t)_current_company, file, line);
	}

	return _random.Next();
}

uint32_t DoRandomRange(uint32_t limit, int line, const char *file)
{
	return ((uint64_t)DoRandom(line, file) * (uint64_t)limit) >> 32;
}
#endif /* RANDOM_DEBUG */

/**
 * Fill the given buffer with random bytes.
 *
 * This function will attempt to use a cryptographically-strong random
 * generator, but will fall back to a weaker random generator if none is
 * available.
 *
 * In the end, the buffer will always be filled with some form of random
 * bytes when this function returns.
 *
 * @param buf The buffer to fill with random bytes.
 */
void RandomBytesWithFallback(std::span<uint8_t> buf)
{
#if defined(_WIN32)
	auto res = BCryptGenRandom(nullptr, static_cast<PUCHAR>(buf.data()), static_cast<ULONG>(buf.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
	if (res >= 0) return;
#elif defined(__APPLE__) || defined(__NetBSD__) || defined(__FreeBSD__) || defined(__OpenBSD__)
	arc4random_buf(buf.data(), buf.size());
	return;
#elif defined(__GLIBC__) && ((__GLIBC__ > 2) || ((__GLIBC__ == 2) && (__GLIBC_MINOR__ >= 25)))
	auto res = getrandom(buf.data(), buf.size(), 0);
	if (res > 0 && static_cast<size_t>(res) == buf.size()) return;
#elif defined(__EMSCRIPTEN__)
	auto res = EM_ASM_INT({
		var buf = $0;
		var bytes = $1;

		var crypto = window.crypto;
		if (crypto === undefined || crypto.getRandomValues === undefined) {
			return -1;
		}

		crypto.getRandomValues(Module.HEAPU8.subarray(buf, buf + bytes));
		return 1;
	}, buf.data(), buf.size());
	if (res > 0) return;
#else
#	warning "No cryptographically-strong random generator available; using a fallback instead"
#endif

	static std::atomic<bool> warned_once = false;
	bool have_warned = warned_once.exchange(true);
	DEBUG(misc, have_warned ? 1 : 0, "Cryptographically-strong random generator unavailable; using fallback");

	for (uint i = 0; i < buf.size(); i ++) {
		uint64_t current_time = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
		buf[i] = static_cast<uint8_t>(SimpleHash64(current_time ^ InteractiveRandom()));
	}
}
