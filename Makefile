ARCHS   := arm64
TARGET  := arm64-apple-ios14.0
SDK_PATH ?= $(shell xcrun --sdk iphoneos --show-sdk-path)

CC      := xcrun --sdk iphoneos clang

CFLAGS  := -O2 -fobjc-arc -Wall -Wno-unused-parameter \
           -I./include -I./include/glib-2.0 \
           -target $(TARGET) -isysroot $(SDK_PATH)
LDFLAGS := -framework Foundation -framework CoreFoundation \
           -target $(TARGET) -isysroot $(SDK_PATH)

LIBS    := -L./lib -lfrida-gum \
           -lpthread -ldl -lm -lresolv -lc++ -lobjc

LIBSJS  := -L./lib -lfrida-gumjs -lfrida-gum \
           -lpthread -ldl -lm -lresolv -lc++ -lobjc

BIN   := kedge
SRCS  := main.c kg_util.c kg_process.c kg_hook.c kg_trace.c \
         kg_memory.c kg_discover.c kg_cloak.c kg_except.c \
         kg_backtracer.c kg_memmon.c kg_load.c kg_inject.c
OBJS  := $(SRCS:.c=.o)

all: $(BIN) kedge-repl payload

$(BIN): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

kedge-repl: kg_repl_jsc.c
	$(CC) $(CFLAGS) $(LDFLAGS) -Wl,-no_fixup_chains \
	    -framework JavaScriptCore \
	    -o $@ kg_repl_jsc.c $(LIBS)

%.o: %.c kg.h
	$(CC) $(CFLAGS) -c -o $@ $<

payload: kg_agent.c
	$(CC) -shared -fPIC -o kg_agent.dylib kg_agent.c \
	      -I./include -I./include/glib-2.0 \
	      -L./lib -lfrida-gum \
	      -target $(TARGET) -isysroot $(SDK_PATH) \
	      -framework Foundation -framework CoreFoundation \
	      -lpthread -ldl -lm

clean:
	rm -f $(OBJS) $(BIN) kedge-repl kg_agent.dylib