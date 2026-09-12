# Host-native tests for src/clocktree.c. Needs only cc and make; ~1 s.
#
#   make          - functional suite, then the 100% branch-coverage gate
#
# Only the PURE arithmetic is tested here (02/03/04's no-cargo-culting
# rule): what PLL multiplier, prescalers, wait states and USART divisor a
# requested SYSCLK needs, and which requests the reference manual forbids.
# Register pokes and the startup code are proven in-simulator over serial
# (renode/boot.robot, wokwi-ci.scenario.yaml), never mocked on the host.
#
# The coverage object is compiled inside build/ with -c then linked, so
# Apple clang and GNU gcc drop .gcno/.gcda in the same place and the awk
# gate works on macOS as well as in CI (02's lesson).

CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra -Werror -std=c11 -pedantic
BUILD   := build
SRC     := ../src

.PHONY: all run functional coverage clean

all: run

run: functional coverage
	@echo
	@echo "ALL TEST STAGES PASS"

$(BUILD):
	@mkdir -p $(BUILD)

# ---------------------------------------------------------------- functional
$(BUILD)/test_clocktree: test_clocktree.c $(SRC)/clocktree.c $(SRC)/clocktree.h | $(BUILD)
	$(CC) $(CFLAGS) -I$(SRC) -o $@ test_clocktree.c $(SRC)/clocktree.c

functional: $(BUILD)/test_clocktree
	@echo "--- functional: PLL/prescaler/latency table, rejections, BRR, SysTick"
	./$(BUILD)/test_clocktree

# ------------------------------------------------------------------- coverage
# Gate: "Taken at least once" == 100.00% for every branch in clocktree.c.
coverage: | $(BUILD)
	@echo "--- coverage: every branch in clocktree.c must be taken both ways"
	cd $(BUILD) \
	    && $(CC) -O0 --coverage -std=c11 -Wall -Wextra -Werror -I../$(SRC) \
	           -c ../$(SRC)/clocktree.c -o cov_core.o \
	    && $(CC) -O0 --coverage -std=c11 -Wall -Wextra -Werror -I../$(SRC) \
	           -c ../test_clocktree.c -o cov_test.o \
	    && $(CC) --coverage cov_core.o cov_test.o -o cov_test \
	    && ./cov_test > /dev/null \
	    && gcov -b cov_core.gcda > gcov_summary.txt
	@awk -v RS='' '/clocktree\.c/ { found = 1;                                \
	        if ($$0 ~ /Taken at least once:100\.00%/) ok = 1;                 \
	        print }                                                           \
	    END {                                                                 \
	        if (!found) { print "no gcov stanza for clocktree.c"; exit 2 }    \
	        exit ok ? 0 : 1 }' $(BUILD)/gcov_summary.txt

clean:
	rm -rf $(BUILD) *.gcno *.gcda *.gcov
