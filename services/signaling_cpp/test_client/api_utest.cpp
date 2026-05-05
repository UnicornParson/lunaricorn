#include <boost/test/unit_test.hpp>
#include <span>
#include <vector>
#include <algorithm>
#include <iostream>

#include <proto/signaling.h>
BOOST_AUTO_TEST_SUITE(api_suite)

BOOST_AUTO_TEST_CASE(serialization_test)
{
    std::vector<int> v = {1, 2, 3, 4, 5};
    std::span s(v);

    BOOST_TEST(s.size() == 5u);
    BOOST_TEST(s.front() == 1);
    BOOST_TEST(s.back() == 5);


}

BOOST_AUTO_TEST_SUITE_END()