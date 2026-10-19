/* SPDX-License-Identifier: MIT */
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2026 Analog Devices, Inc.
 */

#include <errno.h>
#include <iio/iio-backend.h>
#include <string.h>

#include "test_framework.h"

TEST_FUNCTION(format_int)
{
	char buf[16];

	TEST_ASSERT_EQ(iio_value_format_int(buf, sizeof(buf), 42), 3,
			"42 takes 3 bytes with its NUL");
	TEST_ASSERT_STR_EQ(buf, "42", "42 formats as decimal");
	TEST_ASSERT_EQ(iio_value_format_int(buf, sizeof(buf), -163986), 8,
			"negative value");
	TEST_ASSERT_STR_EQ(buf, "-163986", "negative value text");
	TEST_ASSERT_EQ(iio_value_format_int(buf, 3, 123), -ERANGE,
			"no room for the NUL is -ERANGE");
	TEST_ASSERT_EQ(iio_value_format_int(buf, 0, 1), -ERANGE,
			"an empty buffer is -ERANGE");
}

TEST_FUNCTION(format_str)
{
	char buf[8];

	TEST_ASSERT_EQ(iio_value_format_str(buf, sizeof(buf), "up"), 3,
			"string length plus NUL");
	TEST_ASSERT_STR_EQ(buf, "up", "string is copied");
	TEST_ASSERT_EQ(iio_value_format_str(buf, sizeof(buf), "too long!"), -ERANGE,
			"a string that does not fit is -ERANGE");
}

TEST_FUNCTION(format_fixed)
{
	char buf[16];

	TEST_ASSERT_EQ(iio_value_format_fixed(buf, sizeof(buf), 0, 38245, 9), 12,
			"nano scale");
	TEST_ASSERT_STR_EQ(buf, "0.000038245", "nano scale text");
	TEST_ASSERT_EQ(iio_value_format_fixed(buf, sizeof(buf), -110, 497238, 6), 12,
			"negative integer part");
	TEST_ASSERT_STR_EQ(buf, "-110.497238", "negative integer part text");
	TEST_ASSERT_EQ(iio_value_format_fixed(buf, sizeof(buf), 0, -250000, 6), 10,
			"negative fraction carries the sign");
	TEST_ASSERT_STR_EQ(buf, "-0.250000", "negative fraction text");
	TEST_ASSERT_EQ(iio_value_format_fixed(buf, sizeof(buf), 1, 1000000, 6), -EINVAL,
			"a fraction of 10^digits or more is -EINVAL");
	TEST_ASSERT_EQ(iio_value_format_fixed(buf, sizeof(buf), 1, 0, 0), -EINVAL,
			"zero digits is -EINVAL");
	TEST_ASSERT_EQ(iio_value_format_fixed(buf, sizeof(buf), 1, 0, 10), -EINVAL,
			"more than nine digits is -EINVAL");
	TEST_ASSERT_EQ(iio_value_format_fixed(buf, 4, 4000, 0, 6), -ERANGE,
			"a value that does not fit is -ERANGE");
}

TEST_FUNCTION(format_list)
{
	static const int odr[][2] = { { 4000, 0 }, { 62, 500000 }, { 3, 906000 } };
	char buf[64];

	TEST_ASSERT_EQ(iio_value_format_list(buf, sizeof(buf), odr, 3, 6), 31,
			"three values and two spaces");
	TEST_ASSERT_STR_EQ(buf, "4000.000000 62.500000 3.906000", "list text");
	TEST_ASSERT_EQ(iio_value_format_list(buf, sizeof(buf), odr, 0, 6), 1,
			"an empty list is just the NUL");
	TEST_ASSERT_STR_EQ(buf, "", "empty list text");
	TEST_ASSERT_EQ(iio_value_format_list(buf, 12, odr, 2, 6), -ERANGE,
			"a list that does not fit is -ERANGE");
	TEST_ASSERT_EQ(iio_value_format_list(buf, 0, odr, 1, 6), -ERANGE,
			"an empty buffer is -ERANGE");
}

TEST_FUNCTION(parse_int)
{
	long long val = 0;

	TEST_ASSERT_EQ(iio_value_parse_int("42", &val), 0, "plain integer");
	TEST_ASSERT_EQ(val, 42, "plain integer value");
	TEST_ASSERT_EQ(iio_value_parse_int("-7\n", &val), 0, "trailing newline");
	TEST_ASSERT_EQ(val, -7, "trailing newline value");
	TEST_ASSERT_EQ(iio_value_parse_int("+3", &val), 0, "explicit plus sign");
	TEST_ASSERT_EQ(val, 3, "explicit plus sign value");
	TEST_ASSERT_EQ(iio_value_parse_int("4x", &val), -EINVAL, "trailing junk");
	TEST_ASSERT_EQ(iio_value_parse_int("", &val), -EINVAL, "empty string");
	TEST_ASSERT_EQ(iio_value_parse_int(" 5", &val), -EINVAL, "leading space");
	TEST_ASSERT_EQ(iio_value_parse_int("1\n\n", &val), -EINVAL, "two newlines");
	TEST_ASSERT_EQ(iio_value_parse_int("99999999999999999999", &val), -EINVAL,
			"out of range");
	TEST_ASSERT_EQ(iio_value_parse_int("010", &val), 0, "leading zero");
	TEST_ASSERT_EQ(val, 10, "leading zero is still decimal");
}

TEST_FUNCTION(parse_fixed)
{
	int val = 0, val2 = 0;

	TEST_ASSERT_EQ(iio_value_parse_fixed("125", &val, &val2, 6), 0,
			"no decimal point");
	TEST_ASSERT(val == 125 && val2 == 0, "125 is 125.000000");
	TEST_ASSERT_EQ(iio_value_parse_fixed("62.5\n", &val, &val2, 6), 0,
			"short fraction and newline");
	TEST_ASSERT(val == 62 && val2 == 500000, "62.5 is 62.500000");
	TEST_ASSERT_EQ(iio_value_parse_fixed("-0.25", &val, &val2, 6), 0,
			"negative below one");
	TEST_ASSERT(val == 0 && val2 == -250000, "-0.25 puts the sign on the fraction");
	TEST_ASSERT_EQ(iio_value_parse_fixed("-1.5", &val, &val2, 6), 0,
			"negative above one");
	TEST_ASSERT(val == -1 && val2 == 500000, "-1.5 puts the sign on the integer");
	TEST_ASSERT_EQ(iio_value_parse_fixed("3.9060009", &val, &val2, 6), 0,
			"extra digits");
	TEST_ASSERT(val == 3 && val2 == 906000, "extra digits are dropped");
	TEST_ASSERT_EQ(iio_value_parse_fixed("0.000038245", &val, &val2, 9), 0,
			"nano precision");
	TEST_ASSERT(val == 0 && val2 == 38245, "nano value");
	TEST_ASSERT_EQ(iio_value_parse_fixed(".5", &val, &val2, 6), 0,
			"no integer digits");
	TEST_ASSERT(val == 0 && val2 == 500000, ".5 is 0.500000");
	TEST_ASSERT_EQ(iio_value_parse_fixed(".", &val, &val2, 6), -EINVAL,
			"no digits at all");
	TEST_ASSERT_EQ(iio_value_parse_fixed("-", &val, &val2, 6), -EINVAL,
			"sign alone");
	TEST_ASSERT_EQ(iio_value_parse_fixed("1.2.3", &val, &val2, 6), -EINVAL,
			"two decimal points");
	TEST_ASSERT_EQ(iio_value_parse_fixed("1a", &val, &val2, 6), -EINVAL,
			"trailing junk");
	TEST_ASSERT_EQ(iio_value_parse_fixed("2147483648", &val, &val2, 6), -EINVAL,
			"integer part out of range");
	TEST_ASSERT_EQ(iio_value_parse_fixed("1", &val, &val2, 0), -EINVAL,
			"zero digits");
}

TEST_FUNCTION(match_str)
{
	static const char *const table[] = { "in", NULL, "out" };

	TEST_ASSERT_EQ(iio_value_match_str(table, 3, "out\n", 4), 2,
			"trailing newline");
	TEST_ASSERT_EQ(iio_value_match_str(table, 3, "in", 3), 0,
			"trailing NUL counted in len");
	TEST_ASSERT_EQ(iio_value_match_str(table, 3, "ou", 2), -EINVAL,
			"a prefix is not a match");
	TEST_ASSERT_EQ(iio_value_match_str(table, 3, "", 0), -EINVAL,
			"empty input");
}

int main(void)
{
	DEBUG_PRINT("=== libiio Attribute Value Helper Tests ===\n\n");

	RUN_TEST(format_int);
	RUN_TEST(format_str);
	RUN_TEST(format_fixed);
	RUN_TEST(format_list);
	RUN_TEST(parse_int);
	RUN_TEST(parse_fixed);
	RUN_TEST(match_str);

	TEST_SUMMARY();
	return 0;
}
