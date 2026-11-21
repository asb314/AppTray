// vcpkg keeps gtest_main in lib\manual-link, so the tests bring their own entry point

#include <gtest/gtest.h>

int main(int argc, char** argv)
{
	testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
