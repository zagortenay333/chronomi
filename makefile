.SILENT:
.PHONY := release debug asan pp clean_pp bt clean run_no_aslr run loc

SRC_DIR       := src
SRC_FILES     := $(shell find $(SRC_DIR) \
				   -path $(SRC_DIR)"/os/linux" -prune -false -o \
				   -iname *.c)
OBJ_FILES     := $(SRC_FILES:.c=.o)
DEP_FILES     := $(SRC_FILES:.c=.dep)
EXE           := chronomi.bin
CC            := gcc
RELEASE_FLAGS := -fno-omit-frame-pointer -g -O2 -DBUILD_RELEASE=1 -DBUILD_DEBUG=0 -DNDEBUG -Wno-unused-parameter
DEBUG_FLAGS   := -g3 -DBUILD_RELEASE=0 -DBUILD_DEBUG=1 -fno-omit-frame-pointer
CFLAGS        := -std=c23 -fno-delete-null-pointer-checks -fno-strict-aliasing -fwrapv -Werror=vla \
                 -Wall -Wextra -Wimplicit-fallthrough -Wswitch -Wno-unused-function -Wno-unused-value -Wno-unused-parameter -Wno-missing-braces \
				 -I$(SRC_DIR) -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=600 $(shell pkg-config --cflags freetype2 harfbuzz sdl3)
LDFLAGS       := -lm -lGL -lpthread \
				 $(shell pkg-config --static --libs freetype2 harfbuzz sdl3)

ifeq ($(CC), clang)
	CFLAGS    += -ferror-limit=2 -fno-spell-checking -Wno-initializer-overrides
	CC_DEPGEN := clang
else ifeq ($(CC), gcc)
	CFLAGS    += -fmax-errors=2 -Wno-empty-body -Wno-override-init
	CC_DEPGEN := gcc
endif

ifdef CC_DEPGEN
	# Create dependencies between .c files and .h files they include.
	# This needs compiler support. Clang and gcc provide the MM flag.
    -include $(DEP_FILES)
    %.dep: %.c
		$(CC_DEPGEN) $(CFLAGS) $< -MM -MT $(@:.dep=.o) >$@
endif

$(EXE): $(OBJ_FILES)
	@$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

release: CFLAGS  += $(RELEASE_FLAGS) -Wno-unused -g
release: $(EXE)

debug: CFLAGS  += $(DEBUG_FLAGS)
debug: LDFLAGS += -fsanitize=address # For asan stack traces.
debug: $(EXE)

asan: CFLAGS  += -fsanitize=address,undefined -DASAN_ENABLED=1 $(DEBUG_FLAGS)
asan: LDFLAGS += -fsanitize=address,undefined
asan: $(EXE)

appimage:
	./scripts/build_appimage

pp:
	$(foreach f, $(SRC_FILES), $(CC) -E -P $(CFLAGS) $(f) > $(f:.c=.pp);)

clean_pp:
	rm -rf $(SRC_FILES:.c=.pp)

clean:
	rm -rf $(EXE) $(SRC_FILES:.c=.pp) $(DEP_FILES) $(OBJ_FILES) $(COVERAGE_DIR)

run_no_aslr:
	setarch $(uname -m) -R ./$(EXE)

run:
	ASAN_OPTIONS=symbolize=1:detect_leaks=0:abort_on_error=1:disable_coredump=0:umap_shadow_on_exit=1\
		./$(EXE)

loc:
	find $(SRC_DIR) -path $(SRC_DIR)"/vendor" -prune -false -o -iname "*.c" -o -iname "*.h" | xargs wc -l | sort -g -r
