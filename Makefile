CXX ?= g++
GO  ?= go

SRCDIR := src
BUILDDIR := build
TARGETDIR := bin
BINARY := proj2
TARGET := $(TARGETDIR)/$(BINARY)

SRCEXT := cc
SOURCES := $(shell find $(SRCDIR) -type f -name *.$(SRCEXT))
OBJECTS := $(patsubst $(SRCDIR)/%,$(BUILDDIR)/%,$(SOURCES:.$(SRCEXT)=.o))

CFLAGS := -g -Wall -std=c++14
LIB := -pthread
INC := -I include

$(TARGET): $(OBJECTS)
	@mkdir -p $(TARGETDIR)
	$(CXX) $^ -o $(TARGET) $(LIB)

$(BUILDDIR)/%.o: $(SRCDIR)/%.$(SRCEXT)
	@mkdir -p $(BUILDDIR)
	$(CXX) $(CFLAGS) $(INC) -c -o $@ $<

.PHONY: clean
clean:
	$(RM) -r $(BUILDDIR) $(TARGETDIR)

.PHONY: test
test: $(TARGET)
	@pkill $(BINARY); true
	@$(GO) test -v -timeout 2m ./test
