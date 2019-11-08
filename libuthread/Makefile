# Target library

targets := libuthread.a
newObjs := sem.o tps.o
allObjs := sem.o queue.o thread.o tps.o

CC      := gcc
CFLAGS  := -Wall -Werror
AR      := ar
ARFLAGS := rcs

ifneq ($(V),1)
B = @
endif

all: $(targets)

deps := $(patsubst %.o,%.d,$(newObjs))
-include $(deps)
DEPFLAGS = -MMD -MF $(@:.o=.d)

libuthread.a: $(allObjs)
	@echo "AR   $@"
	$(B)$(AR) $(ARFLAGS) $@ $^

%.o: %.c
	@echo "CC   $@"
	$(B)$(CC) $(CFLAGS) -c -o $@ $< $(DEPFLAGS)

clean: 
	@echo "CLEAN"
	$(B)rm -f $(newObjs) $(targets) $(deps)
