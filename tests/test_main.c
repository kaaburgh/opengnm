/* test_main.c — opengnm test harness entry point
 *
 * Runs all test suites and returns non-zero if any fail.
 */
#include <stdio.h>

int run_tests_surface(void);
int run_tests_drawcmd(void);
int run_tests_validate(void);
int run_tests_api(void);

int main(void) {
	printf("=== opengnm test suite ===\n\n");

	int failed = 0;
	failed += run_tests_surface();
	failed += run_tests_drawcmd();
	failed += run_tests_validate();
	failed += run_tests_api();

	printf("=== Summary ===\n");
	if (failed == 0) {
		printf("ALL TESTS PASSED\n");
	} else {
		printf("%d SUITE(S) FAILED\n", failed);
	}
	return failed ? 1 : 0;
}
