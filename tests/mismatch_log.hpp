#pragma once
#include <doctest/doctest.h>

#include <cstdio>
#include <string>

// Collects mismatches over many inputs and reports the count plus the first few examples,
// instead of one doctest assertion per input.
struct mismatch_log
{
	long long count = 0;
	std::string examples;

	template <typename... Args>
	void add(const char* fmt, Args... args)
	{
		if (count++ < 5) {
			char buf[512];
			std::snprintf(buf, sizeof buf, fmt, args...);
			examples += buf;
			examples += '\n';
		}
	}
};

#define CHECK_NO_MISMATCH(log)            \
	do {                                  \
		INFO("first mismatches:\n" << (log).examples); \
		CHECK((log).count == 0);          \
	} while (0)
