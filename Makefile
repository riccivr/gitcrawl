include config.mk

SRC = strbuf.c process_utils.c entity.c sharder.c sanitizer.c parser.c git_plumbing.c approx_search.c fetcher.c gitcrawl.c
OBJ = $(SRC:.c=.o)

TEST_SRC = tests/test_sharder.c tests/test_sanitizer.c tests/test_parser.c tests/test_git_plumbing.c tests/test_properties.c tests/test_fuzz.c
TEST_BIN = tests/test_sharder tests/test_sanitizer tests/test_parser tests/test_git_plumbing tests/test_properties tests/test_fuzz
BENCH_BIN = tests/benchmark

all: gitcrawl

.c.o:
	$(CC) $(CFLAGS) -c $< -o $@

gitcrawl: $(OBJ)
	$(CC) $(OBJ) $(LDFLAGS) -o $@

test: $(TEST_BIN) gitcrawl
	@echo "Running unit test suite..."
	@for t in $(TEST_BIN); do ./$$t || exit 1; done
	@sh tests/run_tests.sh
	@echo "All unit tests passed successfully!"

test-posix: gitcrawl
	@sh tests/test_posix.sh

test-properties: tests/test_properties
	@./tests/test_properties

test-fuzz: tests/test_fuzz
	@./tests/test_fuzz

test-stress: gitcrawl
	@sh tests/test_stress.sh

test-all: test test-posix test-properties test-fuzz test-stress

bench: $(BENCH_BIN) gitcrawl
	@sh tests/benchmark.sh

tests/test_sharder: tests/test_sharder.c sharder.o strbuf.o
	$(CC) $(CFLAGS) $^ -o $@

tests/test_sanitizer: tests/test_sanitizer.c sanitizer.o strbuf.o
	$(CC) $(CFLAGS) $^ -o $@

tests/test_parser: tests/test_parser.c parser.o entity.o strbuf.o
	$(CC) $(CFLAGS) $^ -o $@

tests/test_git_plumbing: tests/test_git_plumbing.c git_plumbing.o process_utils.o strbuf.o
	$(CC) $(CFLAGS) $^ $(LDFLAGS) -o $@

tests/test_properties: tests/test_properties.c sharder.o sanitizer.o parser.o entity.o approx_search.o git_plumbing.o process_utils.o strbuf.o
	$(CC) $(CFLAGS) $^ $(LDFLAGS) -o $@

tests/test_fuzz: tests/test_fuzz.c sharder.o sanitizer.o parser.o entity.o strbuf.o
	$(CC) $(CFLAGS) $^ -o $@

tests/benchmark: tests/benchmark.c sanitizer.o parser.o entity.o approx_search.o git_plumbing.o process_utils.o strbuf.o
	$(CC) $(CFLAGS) $^ $(LDFLAGS) -o $@

sanitize: clean
	@echo "Building gitcrawl with AddressSanitizer and UndefinedBehaviorSanitizer..."
	@$(CC) $(CFLAGS) -g -fsanitize=address,undefined $(SRC) -o gitcrawl $(LDFLAGS) -fsanitize=address,undefined
	@echo "Building test binaries with Sanitizers..."
	@$(CC) $(CFLAGS) -g -fsanitize=address,undefined tests/test_sharder.c sharder.c strbuf.c -o tests/test_sharder $(LDFLAGS) -fsanitize=address,undefined
	@$(CC) $(CFLAGS) -g -fsanitize=address,undefined tests/test_sanitizer.c sanitizer.c strbuf.c -o tests/test_sanitizer $(LDFLAGS) -fsanitize=address,undefined
	@$(CC) $(CFLAGS) -g -fsanitize=address,undefined tests/test_parser.c parser.c entity.c strbuf.c -o tests/test_parser $(LDFLAGS) -fsanitize=address,undefined
	@$(CC) $(CFLAGS) -g -fsanitize=address,undefined tests/test_git_plumbing.c git_plumbing.c process_utils.c strbuf.c -o tests/test_git_plumbing $(LDFLAGS) -fsanitize=address,undefined
	@$(CC) $(CFLAGS) -g -fsanitize=address,undefined tests/test_properties.c sharder.c sanitizer.c parser.c entity.c approx_search.c git_plumbing.c process_utils.c strbuf.c -o tests/test_properties $(LDFLAGS) -fsanitize=address,undefined
	@$(CC) $(CFLAGS) -g -fsanitize=address,undefined tests/test_fuzz.c sharder.c sanitizer.c parser.c entity.c strbuf.c -o tests/test_fuzz $(LDFLAGS) -fsanitize=address,undefined
	@for t in $(TEST_BIN); do ./$$t || exit 1; done
	@sh tests/run_tests.sh
	@sh tests/test_posix.sh
	@sh tests/test_stress.sh
	@echo "Sanitizer checks passed clean!"

valgrind: gitcrawl $(TEST_BIN)
	@echo "Running valgrind memory leak verification..."
	@valgrind --leak-check=full --show-leak-kinds=all --errors-for-leak-kinds=all --error-exitcode=1 ./tests/test_sharder >/dev/null
	@valgrind --leak-check=full --show-leak-kinds=all --errors-for-leak-kinds=all --error-exitcode=1 ./tests/test_sanitizer >/dev/null
	@valgrind --leak-check=full --show-leak-kinds=all --errors-for-leak-kinds=all --error-exitcode=1 ./tests/test_parser >/dev/null
	@valgrind --leak-check=full --show-leak-kinds=all --errors-for-leak-kinds=all --error-exitcode=1 ./tests/test_properties >/dev/null
	@valgrind --leak-check=full --show-leak-kinds=all --errors-for-leak-kinds=all --error-exitcode=1 ./tests/test_fuzz >/dev/null
	@echo "valgrind: 0 memory leaks, 0 errors"

clean:
	rm -f gitcrawl gitcrawl.exe $(OBJ) $(TEST_BIN) $(BENCH_BIN)
	rm -rf /tmp/gitcrawl_test_* /tmp/gitcrawl_posix_* /tmp/gitcrawl_stress_* /tmp/gitcrawl_bench_*

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
	cp -R Makefile config.mk arg.h gitcrawl.h *.c *.h gitcrawl.1 README.md LICENSE tests scripts assets packaging gitcrawl-$(VERSION)
	tar -czvf gitcrawl-$(VERSION).tar.gz gitcrawl-$(VERSION)
	rm -rf gitcrawl-$(VERSION)

.PHONY: all test test-posix test-properties test-fuzz test-stress test-all bench sanitize valgrind clean install uninstall dist
