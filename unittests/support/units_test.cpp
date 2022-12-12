/*
 *
 * Copyright 2013-2022 Software Radio Systems Limited
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the distribution.
 *
 */

#include "srsgnb/support/units.h"
#include <gtest/gtest.h>

using namespace srsgnb;

TEST(bit_units, basic)
{
  using namespace units::literals;

  units::bits a = 2_bits;
  ASSERT_EQ(a.value(), 2);
}

TEST(byte_units, basic)
{
  using namespace units::literals;

  units::bytes a = 2_bytes;

  // To bit translation methods.
  ASSERT_EQ(a.to_bits(), 16_bits);
  units::bits c = static_cast<units::bits>(a);
  ASSERT_EQ(c, 16_bits);
}
