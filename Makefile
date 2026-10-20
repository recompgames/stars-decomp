# Build and test entry points. Conventions: docs/PLAN.md section 4.
#   make doctor   print tool versions, fail if a required tool is missing
#   make test     build and run every native C test, then the Python tool tests
#   make clean    remove build/

# Bare `make` builds everything (see the all target below).
.DEFAULT_GOAL := all

# Use clang unless the caller overrides CC (make's built-in default is cc).
ifeq ($(origin CC),default)
CC := clang
endif

UNICORN_PREFIX  := $(shell brew --prefix unicorn 2>/dev/null)
CAPSTONE_PREFIX := $(shell brew --prefix capstone 2>/dev/null)

CFLAGS    := -std=c11 -Wall -Wextra -Werror -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined -DSOFTFLOAT_FAST_INT64
INCLUDES  := -I. -Itests -Ithird_party/softfloat/source/include -I$(UNICORN_PREFIX)/include -I$(CAPSTONE_PREFIX)/include
LDFLAGS   := -fsanitize=address,undefined -fno-sanitize-recover=undefined -L$(UNICORN_PREFIX)/lib -L$(CAPSTONE_PREFIX)/lib
LDLIBS    := -lunicorn -lcapstone
AR        ?= ar

# Libraries: each directory's .c files form one static library. A directory
# with no .c files is skipped, so later tasks only add files.
RUNTIME_SRCS := $(wildcard runtime/*.c)
HARNESS_SRCS := $(filter-out harness/run_original.c,$(wildcard harness/*.c)) build/registry.c
GAME_SRCS    := $(wildcard src/*.c)

LIBS :=
ifneq ($(RUNTIME_SRCS),)
LIBS += build/libruntime.a
endif
ifneq ($(HARNESS_SRCS),)
LIBS += build/libharness.a
endif
ifneq ($(GAME_SRCS),)
LIBS += build/libgame.a
endif

# Vendored Berkeley SoftFloat 3e (third_party/softfloat, BSD-3): exact x87 80-bit
# arithmetic, see docs/CONVENTIONS.md "Floating point". Third-party code is built
# without -Werror and without sanitizers.
SF_DIR    := third_party/softfloat
include $(SF_DIR)/sources.mk
SF_OBJS   := $(patsubst %.c,build/obj/%.o,$(SF_SRCS))
SF_CFLAGS := -std=c11 -O2 -w -DSOFTFLOAT_FAST_INT64 -DSOFTFLOAT_ROUND_ODD -DINLINE_LEVEL=5 \
             -DSOFTFLOAT_FAST_DIV32TO16 -DSOFTFLOAT_FAST_DIV64TO32 \
             -I$(SF_DIR) -I$(SF_DIR)/source/8086-SSE -I$(SF_DIR)/source/include
ifneq ($(SF_SRCS),)
LIBS += build/libsoftfloat.a
build/libsoftfloat.a: $(SF_OBJS)
	$(AR) rcs $@ $^
$(SF_OBJS): build/obj/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(SF_CFLAGS) -MMD -MP -c $< -o $@
endif

define LIB_RULE
build/$(1).a: $$(patsubst %.c,build/obj/%.o,$(2))
	$$(AR) rcs $$@ $$^
endef
ifneq ($(RUNTIME_SRCS),)
$(eval $(call LIB_RULE,libruntime,$(RUNTIME_SRCS)))
endif
ifneq ($(HARNESS_SRCS),)
$(eval $(call LIB_RULE,libharness,$(HARNESS_SRCS)))
endif
ifneq ($(GAME_SRCS),)
$(eval $(call LIB_RULE,libgame,$(GAME_SRCS)))
endif

build/obj/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) -MMD -MP -c $< -o $@

# Test binaries: tests/test_*.c, runtime/tests/test_*.c, harness/tests/test_*.c
# and tests/scenarios/test_*.c. Each is linked with every existing library.
# The output name flattens the source path: runtime/tests/test_x.c becomes
# build/tests/runtime_tests_test_x.
TEST_SRCS := $(wildcard tests/test_*.c runtime/tests/test_*.c harness/tests/test_*.c tests/scenarios/test_*.c)
TEST_BINS := $(addprefix build/tests/,$(subst /,_,$(TEST_SRCS:.c=)))

# -MMD -MP -MF writes build/tests/<name>.d, so editing a header (tests/test.h,
# a module header) rebuilds every test that includes it.
define TEST_RULE
build/tests/$(subst /,_,$(basename $(1))): $(1) $(LIBS)
	@mkdir -p build/tests
	$(CC) $(CFLAGS) $(INCLUDES) -MMD -MP -MF $$@.d $$< $(LIBS) $(LDFLAGS) $(LDLIBS) -o $$@
endef
$(foreach s,$(TEST_SRCS),$(eval $(call TEST_RULE,$(s))))

-include $(shell find build/obj build/tests -name '*.d' 2>/dev/null)

# Generated Win16 import table (tools/gen_imports.py, inputs in tools/winespec/).
# Rebuilt only when a spec file or the generator changes. Written via a .tmp
# file so a failed run leaves no partial output behind.
GEN_IMPORTS := runtime/win16_api.inc
$(GEN_IMPORTS): tools/gen_imports.py $(wildcard tools/winespec/*.spec)
	@mkdir -p runtime
	python3 tools/gen_imports.py tools/winespec > $@.tmp || { rm -f $@.tmp; exit 1; }
	mv $@.tmp $@

# Registry of decompiled functions (tools/gen_registry.py). docs/functions.csv is
# local data, so it is a dependency only when present. Written via a .tmp file.
build/registry.c: tools/gen_registry.py $(wildcard src/*.c) $(wildcard docs/functions.csv)
	@mkdir -p build
	python3 tools/gen_registry.py --src src --csv docs/functions.csv -o $@

# nedump: NE image dumper (tools/nedump.c), linked against the runtime library.
NEDUMP := build/nedump
$(NEDUMP): tools/nedump.c runtime/ne.h build/libruntime.a
	@mkdir -p build
	$(CC) $(CFLAGS) $(INCLUDES) $< build/libruntime.a $(LDFLAGS) -o $@

# run_original: the original stars26i.exe on the runtime under the harness (stars-9gm.3).
RUN_ORIGINAL := build/run_original
$(RUN_ORIGINAL): harness/run_original.c $(LIBS)
	@mkdir -p build
	$(CC) $(CFLAGS) $(INCLUDES) -MMD -MP -MF $@.d $< $(LIBS) $(LDFLAGS) $(LDLIBS) -o $@

.PHONY: all test doctor clean check-split lint

# The runtime must not depend on the harness (the split is one-way: harness -> runtime).
check-split:
	@if grep -nE '#[[:space:]]*include[[:space:]]*[<"]harness/' runtime/*.c runtime/*.h; then \
	  echo "check-split: runtime/ must not include harness/ headers"; exit 1; \
	fi

# Lint the decompiled sources (tools/lint_src.py). docs/functions.csv is local-only, so it is a
# no-op where it is absent.
lint:
	python3 tools/lint_src.py

all: $(LIBS) $(TEST_BINS) $(GEN_IMPORTS) $(NEDUMP) $(RUN_ORIGINAL)

# The scenario binaries (tests/scenarios, the long diff tests) run TEST_JOBS at a time, each into
# its own log under build/test-logs; the logs are then printed in TEST_BINS order. They share no
# writable file: each writes build/coverage/<seg>_<off>.txt only for its own segment's functions
# (test_seg21 also for a few 8:/9: functions no other scenario DIFFs). The other binaries run one
# after another first, as before (harness/tests/test_difftest.c reads back a build/coverage file
# that another harness test could write). TEST_JOBS=1 runs every scenario one at a time.
TEST_JOBS     ?= 8
SCENARIO_BINS := $(filter build/tests/tests_scenarios_%,$(TEST_BINS))
SERIAL_BINS   := $(filter-out $(SCENARIO_BINS),$(TEST_BINS))
TEST_LOGS     := build/test-logs

test: check-split $(TEST_BINS) $(GEN_IMPORTS)
	@fail=0; \
	echo "== python3 tools/lint_src.py"; \
	python3 tools/lint_src.py || fail=1; \
	for t in $(SERIAL_BINS); do \
	  echo "== $$t"; \
	  ./$$t || fail=1; \
	done; \
	if [ -n "$(SCENARIO_BINS)" ]; then \
	  rm -rf $(TEST_LOGS); mkdir -p $(TEST_LOGS); \
	  printf '%s\n' $(SCENARIO_BINS) | xargs -P $(TEST_JOBS) -I{} \
	    sh -c 'n=$$(basename {}); ./{} > $(TEST_LOGS)/$$n.log 2>&1; echo $$? > $(TEST_LOGS)/$$n.rc'; \
	  for t in $(SCENARIO_BINS); do \
	    n=$$(basename $$t); \
	    echo "== $$t"; \
	    cat $(TEST_LOGS)/$$n.log; \
	    rc=$$(cat $(TEST_LOGS)/$$n.rc 2>/dev/null); \
	    if [ "$$rc" != 0 ]; then echo "== $$t exited with status $${rc:-unknown}"; fail=1; fi; \
	  done; \
	fi; \
	if ls tools/test_*.py >/dev/null 2>&1; then \
	  echo "== python3 -m unittest discover -s tools"; \
	  python3 -m unittest discover -s tools -p 'test_*.py' || fail=1; \
	fi; \
	exit $$fail

doctor:
	@fail=0; \
	for tool in clang python3 java; do \
	  if command -v $$tool >/dev/null 2>&1; then \
	    printf '%-18s %s\n' "$$tool" "$$($$tool --version 2>&1 | head -n 1)"; \
	  else \
	    printf '%-18s MISSING\n' "$$tool"; fail=1; \
	  fi; \
	done; \
	if command -v analyzeHeadless >/dev/null 2>&1; then \
	  printf '%-18s %s\n' "analyzeHeadless" "$$(command -v analyzeHeadless)"; \
	else \
	  printf '%-18s MISSING\n' "analyzeHeadless"; fail=1; \
	fi; \
	if ls $(UNICORN_PREFIX)/lib/libunicorn.* >/dev/null 2>&1; then \
	  printf '%-18s %s\n' "unicorn" "$$(brew list --versions unicorn 2>/dev/null)"; \
	else \
	  printf '%-18s MISSING (brew install unicorn)\n' "unicorn"; fail=1; \
	fi; \
	if ls $(CAPSTONE_PREFIX)/lib/libcapstone.* >/dev/null 2>&1; then \
	  printf '%-18s %s\n' "capstone" "$$(brew list --versions capstone 2>/dev/null)"; \
	else \
	  printf '%-18s MISSING (brew install capstone)\n' "capstone"; fail=1; \
	fi; \
	exit $$fail

clean:
	rm -rf build
