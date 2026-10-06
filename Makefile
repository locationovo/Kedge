ARCHS   := arm64
TARGET  := arm64-apple-ios14.0
SDK_PATH ?= $(shell xcrun --sdk iphoneos --show-sdk-path)

CC      := xcrun --sdk iphoneos clang
GLIB_CFLAGS := $(shell pkg-config --cflags glib-2.0)
GLIB_LIBS   := $(shell pkg-config --libs glib-2.0)

CFLAGS  := -O2 -fobjc-arc -Wall -Wno-unused-parameter -I./include \
           $(GLIB_CFLAGS) \
           -target $(TARGET) -isysroot $(SDK_PATH)
LDFLAGS := -framework Foundation -framework CoreFoundation \
           -target $(TARGET) -isysroot $(SDK_PATH)
LIBS    := -L./lib -lfrida-gum -lfrida-gumjs -lreadline \
           -lpthread -ldl -lm -lresolv $(GLIB_LIBS)

BIN   := kedge
SRCS  := main.c kg_util.c kg_process.c kg_hook.c kg_trace.c \
         kg_memory.c kg_discover.c kg_cloak.c kg_except.c \
         kg_backtracer.c kg_memmon.c kg_load.c kg_inject.c kg_repl.c
OBJS  := $(SRCS:.c=.o)

all: $(BIN) payload

$(BIN): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

%.o: %.c kg.h
	$(CC) $(CFLAGS) -c -o $@ $<

payload: kg_agent.c
	$(CC) -shared -fPIC -o kg_agent.dylib kg_agent.c \
	      -I./include $(GLIB_CFLAGS) \
	      -L./lib -lfrida-gum \
	      -target $(TARGET) -isysroot $(SDK_PATH) \
	      -framework Foundation -framework CoreFoundation \
	      -lpthread -ldl -lm $(GLIB_LIBS)

clean:
	rm -f $(OBJS) $(BIN) kg_agent.dylib