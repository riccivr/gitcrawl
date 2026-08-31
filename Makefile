include config.mk

SRC = strbuf.c entity.c sharder.c sanitizer.c parser.c git_plumbing.c approx_search.c fetcher.c gitcrawl.c
OBJ = $(SRC:.c=.o)

TEST_SRC = tests/test_sharder.c tests/test_sanitizer.c tests/test_parser.c tests/test_git_plumbing.c
TEST_BIN = tests/test_sharder tests/test_sanitizer tests/test_parser tests/test_git_plumbing

all: gitcrawl

.c.o:
	$(CC) $(CFLAGS) -c $< -o $@

gitcrawl: $(OBJ)
	$(CC) $(OBJ) $(LDFLAGS) -o $@

test: $(TEST_BIN) gitcrawl
	@echo "Running unit test suite..."
	@for t in $(TEST_BIN); do ./$$t || exit 1; done
	@tests/run_tests.sh
	@echo "All tests passed successfully!"

tests/test_sharder: tests/test_sharder.c sharder.o strbuf.o
	$(CC) $(CFLAGS) $^ -o $@

tests/test_sanitizer: tests/test_sanitizer.c sanitizer.o strbuf.o
	$(CC) $(CFLAGS) $^ -o $@

tests/test_parser: tests/test_parser.c parser.o entity.o strbuf.o
	$(CC) $(CFLAGS) $^ -o $@

tests/test_git_plumbing: tests/test_git_plumbing.c git_plumbing.o strbuf.o
	$(CC) $(CFLAGS) $^ $(LDFLAGS) -o $@

clean:
	rm -f gitcrawl $(OBJ) $(TEST_BIN)
	rm -rf /tmp/gitcrawl_test_*

install: all
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp -f gitcrawl $(DESTDIR)$(PREFIX)/bin
	chmod 755 $(DESTDIR)$(PREFIX)/bin/gitcrawl
	mkdir -p $(DESTDIR)$(MANPREFIX)/man1
	cp -f gitcrawl.1 $(DESTDIR)$(MANPREFIX)/man1/gitcrawl.1
	chmod 644 $(DESTDIR)$(MANPREFIX)/man1/gitcrawl.1

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/gitcrawl
	rm -f $(DESTDIR)$(MANPREFIX)/man1/gitcrawl.1

dist: clean
	mkdir -p gitcrawl-$(VERSION)
	cp -R Makefile config.mk arg.h gitcrawl.h *.c *.h gitcrawl.1 README.md LICENSE tests gitcrawl-$(VERSION)
	tar -czvf gitcrawl-$(VERSION).tar.gz gitcrawl-$(VERSION)
	rm -rf gitcrawl-$(VERSION)

.PHONY: all test clean install uninstall dist
