DEBUG ?= 0
TARGET := socketecho
INCDIRS = ./inc
SRCDIR = ./src
SRCS = $(TARGET).c
SRCS += regexp.c
SRCS += uriparser.c
LIBS = libpcre2-8
BUILDDIR = ./.build

CFLAGS = -O2 -std=gnu17 -fms-extensions -Wall -Wextra -Wpedantic
CFLAGS += $(shell pkg-config --cflags $(LIBS))
CFLAGS += -DDEBUG=$(DEBUG)
# This will turn all warnings into errors
#CFLAGS += -Werror
LDFLAGS = $(shell pkg-config --libs $(LIBS)) -Wl,--as-needed
CC := gcc

.PHONY: all clean tidy lint lint-all lint-oclint

# First target is default target when `make` is invoked with no target provided
all: $(TARGET)

$(BUILDDIR)/%.o: $(SRCDIR)/%.c | $(BUILDDIR)
	$(CC) $(CFLAGS) $(addprefix -I,$(INCDIRS)) -c $< -o $@

$(BUILDDIR)/$(TARGET): $(addprefix $(BUILDDIR)/,$(SRCS:.c=.o))
	$(CC) $(CFLAGS) $(LDFLAGS) $^ -o $@

$(TARGET): $(BUILDDIR)/$(TARGET)
	ln -sf $< $@

$(BUILDDIR):
	mkdir -p $@

clean:
	-rm -rf $(BUILDDIR)

tidy: clean
	-rm -f $(TARGET)

# specifies linters to run on lint target
lint: lint-all

# target to run all linters
lint-all: | lint-clang-tidy lint-splint lint-oclint lint-cppcheck
_oclint := oclint
_splint := splint +checks $(addprefix -I ,$(INCDIRS))
_cppcheck := cppcheck -q -j$$(($$(nproc)+1)) $(addprefix -I,$(INCDIRS)) \
	--platform=unix64 \
	--enable=warning,style,performance,portability,information \
	--std=c11 --language=c --verbose --inconclusive
_clang-tidy := clang-tidy --quiet --checks='*'

lint-oclint: $(addprefix $(SRCDIR)/,$(SRCS))
	@echo -e "\e[1m\e[92m>>> OCLint report\e[38;5;130m"
	$(_oclint) 2>/dev/null $^ | head -n -2 | tail -n +1
	@echo -en "\e[0m"

lint-clang-tidy: $(addprefix $(SRCDIR)/,$(SRCS))
	@echo -e "\e[1m\e[92m>>> Clang-Tidy report\e[38;5;130m"
	$(_clang-tidy) $^ -- $(addprefix -I,$(INCDIRS)) $(CFLAGS) 2>/dev/null | cat
	@echo -en "\e[0m"

lint-splint: $(addprefix $(SRCDIR)/,$(SRCS))
	@echo -e "\e[1m\e[92m>>> SPLint report\e[38;5;130m"
	$(_splint) 2>&1 $^ | tail -n +2
	@echo -en "\e[0m"

lint-cppcheck: $(addprefix $(SRCDIR)/,$(SRCS))
	@echo -e "\e[1m\e[92m>>> CPPCheck report\e[38;5;130m"
	$(_cppcheck) $^ && echo
	@echo -en "\e[0m"
